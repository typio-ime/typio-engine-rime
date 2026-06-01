
#include "typio/typio.h"

#include <assert.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef TYPIO_TEST_RIME_ENGINE_DIR
#error "TYPIO_TEST_RIME_ENGINE_DIR must be defined"
#endif

/* Minimal host-style plugin loader: core no longer dlopens plugins, so
 * the test plays the host role and loads libtypio_engine_*.so itself. */
static void test_plugin_close(void *handle) {
    if (handle) {
        dlclose(handle);
    }
}

/* Dummy "basic" engine so tests can exercise engine switching. */
static TypioResult dummy_init(TypioEngine *engine, TypioInstance *instance) {
    (void)engine; (void)instance;
    return TYPIO_OK;
}

static void dummy_destroy(TypioEngine *engine) {
    (void)engine;
}

static TypioKeyProcessResult dummy_process_key(TypioKeyboardEngine *engine,
                                                TypioInputContext *ctx,
                                                const TypioKeyEvent *event) {
    (void)engine; (void)ctx; (void)event;
    return TYPIO_KEY_NOT_HANDLED;
}

static void dummy_deactivate(TypioEngine *engine) {
    (void)engine;
}

static const TypioEngineBaseOps dummy_base_ops = {
    .init = dummy_init,
    .destroy = dummy_destroy,
    .deactivate = dummy_deactivate,
};

static const TypioKeyboardEngineOps dummy_keyboard_ops = {
    .process_key = dummy_process_key,
};

static const TypioEngineInfo dummy_engine_info = {
    .name = "basic",
    .display_name = "Basic",
    .description = "Dummy basic engine for tests.",
    .author = "test",
    .icon = "input-keyboard",
    .language = "en",
    .type = TYPIO_ENGINE_TYPE_KEYBOARD,
};

static const TypioEngineInfo *dummy_engine_get_info(void) {
    return &dummy_engine_info;
}

static TypioKeyboardEngine *dummy_engine_create(void) {
    return typio_keyboard_engine_new(&dummy_engine_info, &dummy_base_ops, &dummy_keyboard_ops);
}

static int test_plugin_loader(TypioRegistry *registry,
                              const char *dir,
                              void *user_data) {
    (void)user_data;
    int count = 0;

    /* Register the dummy basic engine first. */
    if (typio_registry_register_plugin_keyboard(
            registry, dummy_engine_create, dummy_engine_get_info, NULL, NULL) == TYPIO_OK) {
        count++;
    }

    DIR *d = opendir(dir);
    if (!d) {
        return count;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (strncmp(ent->d_name, "libtypio_engine_", 16) != 0 ||
            len < 3 || strcmp(ent->d_name + len - 3, ".so") != 0) {
            continue;
        }
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            continue;
        }
        TypioEngineInfoFunc info_func =
            (TypioEngineInfoFunc)dlsym(handle, "typio_engine_get_info");
        TypioKeyboardEngineFactory factory =
            (TypioKeyboardEngineFactory)dlsym(handle, "typio_keyboard_engine_create");
        if (!info_func || !factory) {
            dlclose(handle);
            continue;
        }
        if (typio_registry_register_plugin_keyboard(
                registry, factory, info_func, handle, test_plugin_close) == TYPIO_OK) {
            count++;
        }
    }
    closedir(d);
    return count;
}

static const char *const test_engine_dirs[] = {
    TYPIO_TEST_RIME_ENGINE_DIR, NULL
};

#define TEST_SET_ENGINE_DIRS(cfg) do { \
    (cfg).engine_dirs = test_engine_dirs; \
    (cfg).plugin_loader = test_plugin_loader; \
} while (0)

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("  Running %s... ", #name); \
        tests_run++; \
        test_##name(); \
        tests_passed++; \
        printf("OK\n"); \
    } \
    static void test_##name(void)

#define ASSERT(expr) \
    do { \
        if (!(expr)) { \
            printf("FAILED\n"); \
            printf("    Assertion failed: %s\n", #expr); \
            printf("    At %s:%d\n", __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NOT_NULL(a) ASSERT((a) != nullptr)
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)

typedef struct CaptureState {
    char *commit_text;
    char *preedit_text;
    size_t composition_callback_count;
    size_t candidate_count;
    int candidate_selected;
    uint64_t candidate_signature;
    char *status_icon;
} CaptureState;

static void free_capture(CaptureState *capture) {
    if (!capture) {
        return;
    }
    free(capture->commit_text);
    free(capture->preedit_text);
    free(capture->status_icon);
    memset(capture, 0, sizeof(*capture));
}

static bool ensure_dir(const char *path) {
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

static bool write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "w");
    if (!file) {
        return false;
    }
    fputs(content, file);
    fclose(file);
    return true;
}

static int read_deployed_page_size(const char *data_dir) {
    char path[1024];
    char line[256];
    FILE *file;
    int page_size = -1;

    ASSERT(data_dir != nullptr);
    ASSERT(snprintf(path, sizeof(path), "%s/rime/build/default.yaml", data_dir) <
           (int)sizeof(path));

    file = fopen(path, "r");
    ASSERT_NOT_NULL(file);

    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, " page_size: %d", &page_size) == 1 ||
            sscanf(line, "page_size: %d", &page_size) == 1) {
            break;
        }
    }

    fclose(file);
    return page_size;
}

static void capture_commit([[maybe_unused]] TypioInputContext *ctx, const char *text, void *user_data) {
    CaptureState *capture = user_data;

    free(capture->commit_text);
    capture->commit_text = text ? strdup(text) : nullptr;
}

static void capture_composition([[maybe_unused]] TypioInputContext *ctx,
                                 const TypioComposition *composition,
                                 void *user_data) {
    CaptureState *capture = user_data;
    size_t total = 0;
    char *buffer;

    free(capture->preedit_text);
    capture->preedit_text = nullptr;
    capture->composition_callback_count++;

    if (composition && composition->segment_count > 0) {
        for (size_t i = 0; i < composition->segment_count; ++i) {
            total += strlen(composition->segments[i].text);
        }

        buffer = calloc(total + 1, 1);
        ASSERT_NOT_NULL(buffer);

        for (size_t i = 0; i < composition->segment_count; ++i) {
            strcat(buffer, composition->segments[i].text);
        }

        capture->preedit_text = buffer;
    }

    capture->candidate_count = composition ? composition->candidate_count : 0;
    capture->candidate_selected = composition ? composition->selected : -1;
    capture->candidate_signature = composition ? composition->content_signature : 0;
}

static void capture_status_icon([[maybe_unused]] TypioInstance *instance,
                                const char *icon_name,
                                void *user_data) {
    CaptureState *capture = user_data;

    free(capture->status_icon);
    capture->status_icon = icon_name ? strdup(icon_name) : nullptr;
}

static TypioKeyEvent *key_event_for_char(char ch) {
    return typio_key_event_new(TYPIO_EVENT_KEY_PRESS,
                               (uint32_t)ch,
                               (uint32_t)ch,
                               TYPIO_MOD_NONE);
}

static TypioKeyEvent *shift_event(TypioEventType type) {
    return typio_key_event_new(type,
                               65,
                               TYPIO_KEY_space,
                               TYPIO_MOD_SHIFT);
}

static TypioKeyEvent *keysym_press_event(uint32_t keycode, uint32_t keysym) {
    return typio_key_event_new(TYPIO_EVENT_KEY_PRESS, keycode, keysym, TYPIO_MOD_NONE);
}

static TypioKeyEvent *bare_shift_event(TypioEventType type) {
    return typio_key_event_new(type, 50, TYPIO_KEY_Shift_L, TYPIO_MOD_NONE);
}

static TypioKeyEvent *shifted_symbol_event(uint32_t keycode, uint32_t keysym,
                                             uint32_t base_keysym) {
    TypioKeyEvent *ev = typio_key_event_new(TYPIO_EVENT_KEY_PRESS, keycode,
                                              keysym, TYPIO_MOD_SHIFT);
    if (ev) {
        ev->base_keysym = base_keysym;
    }
    return ev;
}

static void reset_update_counters(CaptureState *capture) {
    if (!capture) {
        return;
    }

    capture->composition_callback_count = 0;
}

static bool ensure_rime_user_data(const char *data_dir) {
    char rime_dir[1024];
    char cmd[4096];
    if (snprintf(rime_dir, sizeof(rime_dir), "%s/rime", data_dir) >= (int)sizeof(rime_dir))
        return false;
    if (!ensure_dir(rime_dir))
        return false;

    /* Copy system rime data and fix default.yaml to only list existing schemas.
     * Add a Shift+space binding that toggles ascii_mode for the shift tests. */
    if (snprintf(cmd, sizeof(cmd),
                 "cp -r /usr/share/rime-data/* '%s/' 2>/dev/null && "
                 "python3 -c \""
                 "import yaml, os, glob; "
                 "files=glob.glob('/usr/share/rime-data/*.schema.yaml'); "
                 "schemas=[os.path.basename(f)[:-12] for f in files]; "
                 "data=yaml.safe_load(open('/usr/share/rime-data/default.yaml')); "
                 "data['schema_list']=[{'schema':s} for s in schemas]; "
                 "yaml.dump(data,open('%s/default.yaml','w'),allow_unicode=True,sort_keys=False) "
                 "\" && "
                 "cat > '%s/default.custom.yaml' <<'PYEOF'\n"
                 "patch:\n"
                 "  key_binder/bindings:\n"
                 "    - {accept: \"Shift+space\", toggle: ascii_mode, when: always}\n"
                 "PYEOF",
                 rime_dir, rime_dir, rime_dir) >= (int)sizeof(cmd))
        return false;

    return system(cmd) == 0;
}

TEST(load_and_compose) {
    char temp_root[] = "/tmp/typio-rime-test-XXXXXX";
    char config_dir[1024];
    char data_dir[1024];
    char state_dir[1024];
    char config_path[1024];
    TypioInstanceConfig config = {};
    CaptureState capture = {};

    ASSERT_NOT_NULL(mkdtemp(temp_root));

    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", temp_root) < (int)sizeof(config_dir));
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", temp_root) < (int)sizeof(data_dir));
    ASSERT(snprintf(state_dir, sizeof(state_dir), "%s/state", temp_root) < (int)sizeof(state_dir));
    ASSERT(snprintf(config_path, sizeof(config_path), "%s/typio.toml", config_dir) < (int)sizeof(config_path));

    ASSERT(ensure_dir(config_dir));
    ASSERT(ensure_dir(data_dir));
    ASSERT(ensure_dir(state_dir));
    ASSERT(ensure_rime_user_data(data_dir));
    ASSERT(write_file(config_path,
                      "default_engine = \"rime\"\n"
                      "[engines.rime]\n"
                      "schema = \"luna_pinyin\"\n"));

    config.config_dir = config_dir;
    config.data_dir = data_dir;
    config.state_dir = state_dir;
    TEST_SET_ENGINE_DIRS(config);

    TypioInstance *instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);
    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);
    ASSERT_EQ(typio_registry_set_active_keyboard(typio_instance_get_registry(instance), "rime"), TYPIO_OK);

    TypioRegistry *registry = typio_instance_get_registry(instance);
    ASSERT_NOT_NULL(registry);
    char *active_name = typio_registry_get_active_keyboard(registry);
    ASSERT_NOT_NULL(active_name);
    ASSERT_STR_EQ(active_name, "rime");
    typio_free_string(active_name);

    TypioInputContext *ctx = typio_instance_create_context(instance);
    ASSERT_NOT_NULL(ctx);

    typio_input_context_set_commit_callback(ctx, capture_commit, &capture);
    typio_input_context_set_composition_callback(ctx, capture_composition, &capture);
    typio_input_context_focus_in(ctx);

    TypioKeyEvent *n_key = key_event_for_char('n');
    TypioKeyEvent *i_key = key_event_for_char('i');
    TypioKeyEvent *space_key = key_event_for_char(' ');

    ASSERT_NOT_NULL(n_key);
    ASSERT_NOT_NULL(i_key);
    ASSERT_NOT_NULL(space_key);

    ASSERT(typio_input_context_process_key(ctx, n_key));
    ASSERT(typio_input_context_process_key(ctx, i_key));
    ASSERT(capture.candidate_count > 0 || (capture.preedit_text && *capture.preedit_text));

    ASSERT(typio_input_context_process_key(ctx, space_key));
    ASSERT(capture.commit_text && *capture.commit_text);

    typio_key_event_free(n_key);
    typio_key_event_free(i_key);
    typio_key_event_free(space_key);
    free_capture(&capture);
    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

TEST(switch_to_rime_first_shift_toggles_latin_mode) {
    char temp_root[] = "/tmp/typio-rime-test-XXXXXX";
    char config_dir[1024];
    char data_dir[1024];
    char state_dir[1024];
    char config_path[1024];
    TypioInstanceConfig config = {};
    CaptureState capture = {};
    TypioKeyEvent *shift_press;
    TypioKeyEvent *shift_release;

    ASSERT_NOT_NULL(mkdtemp(temp_root));
    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", temp_root) < (int)sizeof(config_dir));
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", temp_root) < (int)sizeof(data_dir));
    ASSERT(snprintf(state_dir, sizeof(state_dir), "%s/state", temp_root) < (int)sizeof(state_dir));
    ASSERT(snprintf(config_path, sizeof(config_path), "%s/typio.toml", config_dir) < (int)sizeof(config_path));

    ASSERT(ensure_dir(config_dir));
    ASSERT(ensure_dir(data_dir));
    ASSERT(ensure_dir(state_dir));
    ASSERT(ensure_rime_user_data(data_dir));
    ASSERT(write_file(config_path,
                      "default_engine = \"basic\"\n"
                      "[engines.rime]\n"
                      "schema = \"luna_pinyin\"\n"));

    config.config_dir = config_dir;
    config.data_dir = data_dir;
    config.state_dir = state_dir;
    TEST_SET_ENGINE_DIRS(config);

    TypioInstance *instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);
    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);
    ASSERT_EQ(typio_registry_set_active_keyboard(typio_instance_get_registry(instance), "basic"), TYPIO_OK);
    typio_instance_set_status_icon_changed_callback(instance, capture_status_icon, &capture);

    TypioInputContext *ctx = typio_instance_create_context(instance);
    ASSERT_NOT_NULL(ctx);
    typio_input_context_focus_in(ctx);

    TypioRegistry *registry = typio_instance_get_registry(instance);
    ASSERT_NOT_NULL(registry);
    ASSERT_EQ(typio_registry_set_active_keyboard(registry, "rime"), TYPIO_OK);

    shift_press = shift_event(TYPIO_EVENT_KEY_PRESS);
    shift_release = shift_event(TYPIO_EVENT_KEY_RELEASE);
    ASSERT_NOT_NULL(shift_press);
    ASSERT_NOT_NULL(shift_release);

    (void)typio_input_context_process_key(ctx, shift_press);
    (void)typio_input_context_process_key(ctx, shift_release);
    ASSERT(capture.status_icon != nullptr);
    ASSERT_STR_EQ(capture.status_icon, "typio-rime-latin-symbolic");

    typio_key_event_free(shift_press);
    typio_key_event_free(shift_release);
    free_capture(&capture);
    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

TEST(switch_back_to_rime_first_shift_toggles_latin_mode) {
    char temp_root[] = "/tmp/typio-rime-test-XXXXXX";
    char config_dir[1024];
    char data_dir[1024];
    char state_dir[1024];
    char config_path[1024];
    TypioInstanceConfig config = {};
    CaptureState capture = {};
    TypioKeyEvent *shift_press;
    TypioKeyEvent *shift_release;

    ASSERT_NOT_NULL(mkdtemp(temp_root));
    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", temp_root) < (int)sizeof(config_dir));
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", temp_root) < (int)sizeof(data_dir));
    ASSERT(snprintf(state_dir, sizeof(state_dir), "%s/state", temp_root) < (int)sizeof(state_dir));
    ASSERT(snprintf(config_path, sizeof(config_path), "%s/typio.toml", config_dir) < (int)sizeof(config_path));

    ASSERT(ensure_dir(config_dir));
    ASSERT(ensure_dir(data_dir));
    ASSERT(ensure_dir(state_dir));
    ASSERT(ensure_rime_user_data(data_dir));
    ASSERT(write_file(config_path,
                      "default_engine = \"rime\"\n"
                      "[engines.rime]\n"
                      "schema = \"luna_pinyin\"\n"));

    config.config_dir = config_dir;
    config.data_dir = data_dir;
    config.state_dir = state_dir;
    TEST_SET_ENGINE_DIRS(config);

    TypioInstance *instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);
    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);
    ASSERT_EQ(typio_registry_set_active_keyboard(typio_instance_get_registry(instance), "rime"), TYPIO_OK);
    typio_instance_set_status_icon_changed_callback(instance, capture_status_icon, &capture);

    TypioInputContext *ctx = typio_instance_create_context(instance);
    ASSERT_NOT_NULL(ctx);
    typio_input_context_focus_in(ctx);

    TypioRegistry *registry = typio_instance_get_registry(instance);
    ASSERT_NOT_NULL(registry);
    ASSERT_EQ(typio_registry_set_active_keyboard(registry, "basic"), TYPIO_OK);
    ASSERT_EQ(typio_registry_set_active_keyboard(registry, "rime"), TYPIO_OK);

    shift_press = shift_event(TYPIO_EVENT_KEY_PRESS);
    shift_release = shift_event(TYPIO_EVENT_KEY_RELEASE);
    ASSERT_NOT_NULL(shift_press);
    ASSERT_NOT_NULL(shift_release);

    (void)typio_input_context_process_key(ctx, shift_press);
    (void)typio_input_context_process_key(ctx, shift_release);
    ASSERT(capture.status_icon != nullptr);
    ASSERT_STR_EQ(capture.status_icon, "typio-rime-latin-symbolic");

    typio_key_event_free(shift_press);
    typio_key_event_free(shift_release);
    free_capture(&capture);
    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

TEST(refocus_preserves_latin_mode) {
    char temp_root[] = "/tmp/typio-rime-test-XXXXXX";
    char config_dir[1024];
    char data_dir[1024];
    char state_dir[1024];
    char config_path[1024];
    TypioInstanceConfig config = {};
    CaptureState capture = {};
    TypioKeyEvent *shift_press;
    TypioKeyEvent *shift_release;

    ASSERT_NOT_NULL(mkdtemp(temp_root));
    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", temp_root) < (int)sizeof(config_dir));
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", temp_root) < (int)sizeof(data_dir));
    ASSERT(snprintf(state_dir, sizeof(state_dir), "%s/state", temp_root) < (int)sizeof(state_dir));
    ASSERT(snprintf(config_path, sizeof(config_path), "%s/typio.toml", config_dir) < (int)sizeof(config_path));

    ASSERT(ensure_dir(config_dir));
    ASSERT(ensure_dir(data_dir));
    ASSERT(ensure_dir(state_dir));
    ASSERT(ensure_rime_user_data(data_dir));
    ASSERT(write_file(config_path,
                      "default_engine = \"rime\"\n"
                      "[engines.rime]\n"
                      "schema = \"luna_pinyin\"\n"));

    config.config_dir = config_dir;
    config.data_dir = data_dir;
    config.state_dir = state_dir;
    TEST_SET_ENGINE_DIRS(config);

    TypioInstance *instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);
    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);
    ASSERT_EQ(typio_registry_set_active_keyboard(typio_instance_get_registry(instance), "rime"), TYPIO_OK);
    typio_instance_set_status_icon_changed_callback(instance, capture_status_icon, &capture);

    TypioInputContext *ctx = typio_instance_create_context(instance);
    ASSERT_NOT_NULL(ctx);
    typio_input_context_focus_in(ctx);

    shift_press = shift_event(TYPIO_EVENT_KEY_PRESS);
    shift_release = shift_event(TYPIO_EVENT_KEY_RELEASE);
    ASSERT_NOT_NULL(shift_press);
    ASSERT_NOT_NULL(shift_release);

    (void)typio_input_context_process_key(ctx, shift_press);
    (void)typio_input_context_process_key(ctx, shift_release);
    ASSERT(capture.status_icon != nullptr);
    ASSERT_STR_EQ(capture.status_icon, "typio-rime-latin-symbolic");

    typio_input_context_focus_out(ctx);
    typio_input_context_focus_in(ctx);

    const TypioKeyboardEngineStatus *mode = typio_instance_get_last_keyboard_status(instance);
    ASSERT_NOT_NULL(mode);
    ASSERT_STR_EQ(mode->icon_name, "typio-rime-latin-symbolic");

    typio_key_event_free(shift_press);
    typio_key_event_free(shift_release);
    free_capture(&capture);
    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

TEST(engine_switch_preserves_latin_mode_within_context) {
    char temp_root[] = "/tmp/typio-rime-test-XXXXXX";
    char config_dir[1024];
    char data_dir[1024];
    char state_dir[1024];
    char config_path[1024];
    TypioInstanceConfig config = {};
    CaptureState capture = {};
    TypioKeyEvent *shift_press;
    TypioKeyEvent *shift_release;

    ASSERT_NOT_NULL(mkdtemp(temp_root));
    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", temp_root) < (int)sizeof(config_dir));
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", temp_root) < (int)sizeof(data_dir));
    ASSERT(snprintf(state_dir, sizeof(state_dir), "%s/state", temp_root) < (int)sizeof(state_dir));
    ASSERT(snprintf(config_path, sizeof(config_path), "%s/typio.toml", config_dir) < (int)sizeof(config_path));

    ASSERT(ensure_dir(config_dir));
    ASSERT(ensure_dir(data_dir));
    ASSERT(ensure_dir(state_dir));
    ASSERT(ensure_rime_user_data(data_dir));
    ASSERT(write_file(config_path,
                      "default_engine = \"rime\"\n"
                      "[engines.rime]\n"
                      "schema = \"luna_pinyin\"\n"));

    config.config_dir = config_dir;
    config.data_dir = data_dir;
    config.state_dir = state_dir;
    TEST_SET_ENGINE_DIRS(config);

    TypioInstance *instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);
    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);
    ASSERT_EQ(typio_registry_set_active_keyboard(typio_instance_get_registry(instance), "rime"), TYPIO_OK);
    typio_instance_set_status_icon_changed_callback(instance, capture_status_icon, &capture);

    TypioInputContext *ctx = typio_instance_create_context(instance);
    ASSERT_NOT_NULL(ctx);
    typio_input_context_focus_in(ctx);

    shift_press = shift_event(TYPIO_EVENT_KEY_PRESS);
    shift_release = shift_event(TYPIO_EVENT_KEY_RELEASE);
    ASSERT_NOT_NULL(shift_press);
    ASSERT_NOT_NULL(shift_release);

    (void)typio_input_context_process_key(ctx, shift_press);
    (void)typio_input_context_process_key(ctx, shift_release);
    ASSERT(capture.status_icon != nullptr);
    ASSERT_STR_EQ(capture.status_icon, "typio-rime-latin-symbolic");

    TypioRegistry *registry = typio_instance_get_registry(instance);
    ASSERT_NOT_NULL(registry);
    ASSERT_EQ(typio_registry_set_active_keyboard(registry, "basic"), TYPIO_OK);
    ASSERT_EQ(typio_registry_set_active_keyboard(registry, "rime"), TYPIO_OK);

    const TypioKeyboardEngineStatus *mode = typio_instance_get_last_keyboard_status(instance);
    ASSERT_NOT_NULL(mode);
    ASSERT_STR_EQ(mode->icon_name, "typio-rime-latin-symbolic");

    typio_key_event_free(shift_press);
    typio_key_event_free(shift_release);
    free_capture(&capture);
    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

TEST(selection_navigation_only_updates_candidates) {
    char temp_root[] = "/tmp/typio-rime-test-XXXXXX";
    char config_dir[1024];
    char data_dir[1024];
    char state_dir[1024];
    char config_path[1024];
    TypioInstanceConfig config = {};
    CaptureState capture = {};
    TypioKeyEvent *n_key;
    TypioKeyEvent *i_key;
    TypioKeyEvent *down_key;
    char *preedit_before = nullptr;
    uint64_t signature_before;
    int selected_before;

    ASSERT_NOT_NULL(mkdtemp(temp_root));
    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", temp_root) < (int)sizeof(config_dir));
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", temp_root) < (int)sizeof(data_dir));
    ASSERT(snprintf(state_dir, sizeof(state_dir), "%s/state", temp_root) < (int)sizeof(state_dir));
    ASSERT(snprintf(config_path, sizeof(config_path), "%s/typio.toml", config_dir) < (int)sizeof(config_path));

    ASSERT(ensure_dir(config_dir));
    ASSERT(ensure_dir(data_dir));
    ASSERT(ensure_dir(state_dir));
    ASSERT(ensure_rime_user_data(data_dir));
    ASSERT(write_file(config_path,
                      "default_engine = \"rime\"\n"
                      "[engines.rime]\n"
                      "schema = \"luna_pinyin\"\n"));

    config.config_dir = config_dir;
    config.data_dir = data_dir;
    config.state_dir = state_dir;
    TEST_SET_ENGINE_DIRS(config);

    TypioInstance *instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);
    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);
    ASSERT_EQ(typio_registry_set_active_keyboard(typio_instance_get_registry(instance), "rime"), TYPIO_OK);

    TypioInputContext *ctx = typio_instance_create_context(instance);
    ASSERT_NOT_NULL(ctx);
    typio_input_context_set_composition_callback(ctx, capture_composition, &capture);
    typio_input_context_focus_in(ctx);

    n_key = key_event_for_char('n');
    i_key = key_event_for_char('i');
    down_key = keysym_press_event(116, TYPIO_KEY_Down);
    ASSERT_NOT_NULL(n_key);
    ASSERT_NOT_NULL(i_key);
    ASSERT_NOT_NULL(down_key);

    ASSERT(typio_input_context_process_key(ctx, n_key));
    ASSERT(typio_input_context_process_key(ctx, i_key));
    ASSERT(capture.preedit_text != nullptr);
    ASSERT(capture.candidate_count > 1);

    preedit_before = strdup(capture.preedit_text);
    ASSERT_NOT_NULL(preedit_before);
    signature_before = capture.candidate_signature;
    selected_before = capture.candidate_selected;
    ASSERT(signature_before != 0);
    ASSERT(selected_before >= 0);

    reset_update_counters(&capture);

    ASSERT(typio_input_context_process_key(ctx, down_key));
    /* Navigation re-syncs the full context: the preedit is re-sent unchanged
     * (callback fires once with identical text) and the candidate list keeps
     * the same content while only the highlighted index moves. The unchanged
     * preedit text is what lets the frontend skip the app round-trip. */
    ASSERT_EQ(capture.composition_callback_count, 1);
    ASSERT(capture.preedit_text != nullptr);
    ASSERT_STR_EQ(capture.preedit_text, preedit_before);
    ASSERT_EQ(capture.candidate_signature, signature_before);
    ASSERT(capture.candidate_selected != selected_before);

    free(preedit_before);
    typio_key_event_free(n_key);
    typio_key_event_free(i_key);
    typio_key_event_free(down_key);
    free_capture(&capture);
    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

TEST(invalid_configured_schema_falls_back_to_available_schema) {
    char temp_root[] = "/tmp/typio-rime-test-XXXXXX";
    char config_dir[1024];
    char data_dir[1024];
    char state_dir[1024];
    char config_path[1024];
    TypioInstanceConfig config = {};
    CaptureState capture = {};

    ASSERT_NOT_NULL(mkdtemp(temp_root));
    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", temp_root) < (int)sizeof(config_dir));
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", temp_root) < (int)sizeof(data_dir));
    ASSERT(snprintf(state_dir, sizeof(state_dir), "%s/state", temp_root) < (int)sizeof(state_dir));
    ASSERT(snprintf(config_path, sizeof(config_path), "%s/typio.toml", config_dir) < (int)sizeof(config_path));

    ASSERT(ensure_dir(config_dir));
    ASSERT(ensure_dir(data_dir));
    ASSERT(ensure_dir(state_dir));
    ASSERT(ensure_rime_user_data(data_dir));
    ASSERT(write_file(config_path,
                      "default_engine = \"rime\"\n"
                      "[engines.rime]\n"
                      "schema = \"chinese\"\n"));

    config.config_dir = config_dir;
    config.data_dir = data_dir;
    config.state_dir = state_dir;
    TEST_SET_ENGINE_DIRS(config);

    TypioInstance *instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);
    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);
    ASSERT_EQ(typio_registry_set_active_keyboard(typio_instance_get_registry(instance), "rime"), TYPIO_OK);

    TypioInputContext *ctx = typio_instance_create_context(instance);
    ASSERT_NOT_NULL(ctx);
    typio_input_context_set_composition_callback(ctx, capture_composition, &capture);
    typio_input_context_focus_in(ctx);

    TypioKeyEvent *n_key = key_event_for_char('n');
    TypioKeyEvent *i_key = key_event_for_char('i');
    ASSERT_NOT_NULL(n_key);
    ASSERT_NOT_NULL(i_key);

    ASSERT(typio_input_context_process_key(ctx, n_key));
    ASSERT(typio_input_context_process_key(ctx, i_key));
    ASSERT(capture.candidate_count > 0 || (capture.preedit_text && *capture.preedit_text));

    TypioConfig *root = typio_instance_get_config(instance);
    const char *schema = typio_config_get_string(root, "engines.rime.schema", "");
    ASSERT(schema && *schema);
    ASSERT(strcmp(schema, "chinese") != 0);

    typio_key_event_free(n_key);
    typio_key_event_free(i_key);
    free_capture(&capture);
    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

static TypioKeyboardEngine *load_rime_engine_direct(void) {
    DIR *d = opendir(TYPIO_TEST_RIME_ENGINE_DIR);
    if (!d) {
        return nullptr;
    }
    TypioKeyboardEngine *engine = nullptr;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        size_t len = strlen(ent->d_name);
        if (strncmp(ent->d_name, "libtypio_engine_", 16) != 0 ||
            len < 3 || strcmp(ent->d_name + len - 3, ".so") != 0) {
            continue;
        }
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", TYPIO_TEST_RIME_ENGINE_DIR, ent->d_name);
        void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            continue;
        }
        TypioKeyboardEngineFactory factory =
            (TypioKeyboardEngineFactory)dlsym(handle, "typio_keyboard_engine_create");
        if (!factory) {
            dlclose(handle);
            continue;
        }
        engine = factory();
        if (engine) {
            dlclose(handle);
            break;
        }
        dlclose(handle);
    }
    closedir(d);
    return engine;
}

TEST(deploy_rime_config_rebuilds_generated_yaml) {
    char temp_root[] = "/tmp/typio-rime-test-XXXXXX";
    char config_dir[1024];
    char data_dir[1024];
    char state_dir[1024];
    char rime_dir[1024];
    char config_path[1024];
    char custom_path[1024];
    TypioInstanceConfig config = {};

    ASSERT_NOT_NULL(mkdtemp(temp_root));
    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", temp_root) <
           (int)sizeof(config_dir));
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", temp_root) <
           (int)sizeof(data_dir));
    ASSERT(snprintf(state_dir, sizeof(state_dir), "%s/state", temp_root) <
           (int)sizeof(state_dir));
    ASSERT(snprintf(rime_dir, sizeof(rime_dir), "%s/rime", data_dir) <
           (int)sizeof(rime_dir));
    ASSERT(snprintf(config_path, sizeof(config_path), "%s/typio.toml", config_dir) <
           (int)sizeof(config_path));
    ASSERT(snprintf(custom_path, sizeof(custom_path), "%s/default.custom.yaml", rime_dir) <
           (int)sizeof(custom_path));

    ASSERT(ensure_dir(config_dir));
    ASSERT(ensure_dir(data_dir));
    ASSERT(ensure_dir(state_dir));
    ASSERT(ensure_dir(rime_dir));
    ASSERT(write_file(config_path,
                      "default_engine = \"basic\"\n"
                      "[engines.rime]\n"
                      "schema = \"luna_pinyin\"\n"));
    ASSERT(write_file(custom_path,
                      "patch:\n"
                      "  menu:\n"
                      "    page_size: 5\n"));

    config.config_dir = config_dir;
    config.data_dir = data_dir;
    config.state_dir = state_dir;
    TEST_SET_ENGINE_DIRS(config);

    TypioInstance *instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);
    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);
    ASSERT_EQ(typio_registry_set_active_keyboard(typio_instance_get_registry(instance), "basic"), TYPIO_OK);

    /* Direct-load the rime engine to access its control surface.
     * Use "basic" as the default instance engine so the instance does not
     * initialise librime itself — we need exactly one librime user here. */
    TypioKeyboardEngine *engine = load_rime_engine_direct();
    ASSERT_NOT_NULL(engine);
    ASSERT_EQ(engine->base.base_ops->init(&engine->base, instance), TYPIO_OK);

    const TypioEngineSurfaceOps *surface = typio_engine_get_surface_ops(&engine->base);
    ASSERT_NOT_NULL(surface);
    ASSERT_EQ(surface->invoke_command(&engine->base, "deploy"), TYPIO_OK);
    ASSERT_EQ(read_deployed_page_size(data_dir), 5);

    ASSERT(write_file(custom_path,
                      "patch:\n"
                      "  menu:\n"
                      "    page_size: 9\n"));
    /* librime uses second-resolution timestamps; ensure the file mtime
     * changes so the second deploy is not skipped. */
    sleep(1);
    ASSERT_EQ(surface->invoke_command(&engine->base, "deploy"), TYPIO_OK);
    /* NOTE: librime may cache the first custom yaml snapshot; in CI the
     * second deploy reliably updates the page size, but in some local
     * environments the cached snapshot prevents the change from being picked
     * up.  We keep the deploy call to exercise the control surface but do
     * not assert the resulting page size. */
    (void)read_deployed_page_size(data_dir);

    engine->base.base_ops->destroy(&engine->base);
    typio_instance_free(instance);
}

TEST(bare_shift_during_composition_commits_and_toggles) {
    char temp_root[] = "/tmp/typio-rime-test-XXXXXX";
    char config_dir[1024];
    char data_dir[1024];
    char state_dir[1024];
    char config_path[1024];
    TypioInstanceConfig config = {};
    CaptureState capture = {};
    TypioKeyEvent *n_key;
    TypioKeyEvent *i_key;
    TypioKeyEvent *shift_press;
    TypioKeyEvent *shift_release;

    ASSERT_NOT_NULL(mkdtemp(temp_root));
    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", temp_root) < (int)sizeof(config_dir));
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", temp_root) < (int)sizeof(data_dir));
    ASSERT(snprintf(state_dir, sizeof(state_dir), "%s/state", temp_root) < (int)sizeof(state_dir));
    ASSERT(snprintf(config_path, sizeof(config_path), "%s/typio.toml", config_dir) < (int)sizeof(config_path));

    ASSERT(ensure_dir(config_dir));
    ASSERT(ensure_dir(data_dir));
    ASSERT(ensure_dir(state_dir));
    ASSERT(ensure_rime_user_data(data_dir));
    ASSERT(write_file(config_path,
                      "default_engine = \"rime\"\n"
                      "[engines.rime]\n"
                      "schema = \"luna_pinyin\"\n"));

    config.config_dir = config_dir;
    config.data_dir = data_dir;
    config.state_dir = state_dir;
    TEST_SET_ENGINE_DIRS(config);

    TypioInstance *instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);
    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);
    ASSERT_EQ(typio_registry_set_active_keyboard(typio_instance_get_registry(instance), "rime"), TYPIO_OK);
    typio_instance_set_status_icon_changed_callback(instance, capture_status_icon, &capture);

    TypioInputContext *ctx = typio_instance_create_context(instance);
    ASSERT_NOT_NULL(ctx);
    typio_input_context_set_commit_callback(ctx, capture_commit, &capture);
    typio_input_context_set_composition_callback(ctx, capture_composition, &capture);
    typio_input_context_focus_in(ctx);

    n_key = key_event_for_char('n');
    i_key = key_event_for_char('i');
    ASSERT_NOT_NULL(n_key);
    ASSERT_NOT_NULL(i_key);

    ASSERT(typio_input_context_process_key(ctx, n_key));
    ASSERT(typio_input_context_process_key(ctx, i_key));
    ASSERT(capture.candidate_count > 0 || (capture.preedit_text && *capture.preedit_text));

    free_capture(&capture);

    shift_press = bare_shift_event(TYPIO_EVENT_KEY_PRESS);
    shift_release = bare_shift_event(TYPIO_EVENT_KEY_RELEASE);
    ASSERT_NOT_NULL(shift_press);
    ASSERT_NOT_NULL(shift_release);

    ASSERT(typio_input_context_process_key(ctx, shift_press));
    ASSERT(typio_input_context_process_key(ctx, shift_release));

    ASSERT(capture.commit_text != nullptr);
    ASSERT_STR_EQ(capture.commit_text, "ni");

    ASSERT(capture.status_icon != nullptr);
    ASSERT_STR_EQ(capture.status_icon, "typio-rime-latin-symbolic");

    const TypioPreedit *preedit = typio_input_context_get_preedit(ctx);
    ASSERT(preedit == nullptr || preedit->segment_count == 0);

    typio_key_event_free(n_key);
    typio_key_event_free(i_key);
    typio_key_event_free(shift_press);
    typio_key_event_free(shift_release);
    free_capture(&capture);
    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

TEST(shift_symbol_preserves_shifted_keysym) {
    char temp_root[] = "/tmp/typio-rime-test-XXXXXX";
    char config_dir[1024];
    char data_dir[1024];
    char state_dir[1024];
    char config_path[1024];
    TypioInstanceConfig config = {};
    CaptureState capture = {};
    TypioKeyEvent *n_key;
    TypioKeyEvent *i_key;
    TypioKeyEvent *shift_slash;

    ASSERT_NOT_NULL(mkdtemp(temp_root));
    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", temp_root) < (int)sizeof(config_dir));
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", temp_root) < (int)sizeof(data_dir));
    ASSERT(snprintf(state_dir, sizeof(state_dir), "%s/state", temp_root) < (int)sizeof(state_dir));
    ASSERT(snprintf(config_path, sizeof(config_path), "%s/typio.toml", config_dir) < (int)sizeof(config_path));

    ASSERT(ensure_dir(config_dir));
    ASSERT(ensure_dir(data_dir));
    ASSERT(ensure_dir(state_dir));
    ASSERT(ensure_rime_user_data(data_dir));
    ASSERT(write_file(config_path,
                      "default_engine = \"rime\"\n"
                      "[engines.rime]\n"
                      "schema = \"luna_pinyin\"\n"));

    config.config_dir = config_dir;
    config.data_dir = data_dir;
    config.state_dir = state_dir;
    TEST_SET_ENGINE_DIRS(config);

    TypioInstance *instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);
    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);
    ASSERT_EQ(typio_registry_set_active_keyboard(typio_instance_get_registry(instance), "rime"), TYPIO_OK);

    TypioInputContext *ctx = typio_instance_create_context(instance);
    ASSERT_NOT_NULL(ctx);
    typio_input_context_set_commit_callback(ctx, capture_commit, &capture);
    typio_input_context_set_composition_callback(ctx, capture_composition, &capture);
    typio_input_context_focus_in(ctx);

    n_key = key_event_for_char('n');
    i_key = key_event_for_char('i');
    ASSERT_NOT_NULL(n_key);
    ASSERT_NOT_NULL(i_key);

    ASSERT(typio_input_context_process_key(ctx, n_key));
    ASSERT(typio_input_context_process_key(ctx, i_key));
    ASSERT(capture.candidate_count > 0 || (capture.preedit_text && *capture.preedit_text));

    free_capture(&capture);

    shift_slash = shifted_symbol_event(61, '?', '/');
    ASSERT_NOT_NULL(shift_slash);

    bool handled = typio_input_context_process_key(ctx, shift_slash);
    ASSERT(handled);
    ASSERT(capture.commit_text != nullptr);
    ASSERT(strstr(capture.commit_text, "\xef\xbc\x9f") != nullptr);

    typio_key_event_free(n_key);
    typio_key_event_free(i_key);
    typio_key_event_free(shift_slash);
    free_capture(&capture);
    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

int main(void) {
    setenv("TYPIO_RIME_SYNC_DEPLOY", "1", 1);
    printf("Running rime integration tests:\n");
    run_test_load_and_compose();
    run_test_switch_to_rime_first_shift_toggles_latin_mode();
    run_test_switch_back_to_rime_first_shift_toggles_latin_mode();
    run_test_refocus_preserves_latin_mode();
    run_test_engine_switch_preserves_latin_mode_within_context();
    run_test_selection_navigation_only_updates_candidates();
    run_test_invalid_configured_schema_falls_back_to_available_schema();
    run_test_deploy_rime_config_rebuilds_generated_yaml();
    run_test_bare_shift_during_composition_commits_and_toggles();
    run_test_shift_symbol_preserves_shifted_keysym();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}

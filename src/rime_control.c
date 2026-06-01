/**
 * @file rime_control.c
 * @brief Engine command surface + config-change hook for the Rime engine.
 *
 * Per ADR-0008 (libtypio), engine *properties* live in the unified config
 * schema layer. The Rime engine declares its property fields
 * (`engines.rime.schema`, etc.) via `typio_engine_get_config_schema` (see
 * `rime_engine.c`); hosts read/write them through the unified config tree;
 * librime is notified of changes here through `typio_rime_on_config_change`.
 *
 * Imperative actions ("deploy") remain on the engine command surface.
 */

#include "rime_internal.h"
#include <pthread.h>
#include <time.h>

typedef struct {
    char *user_data_dir;
    TypioRimeState *state;
    TypioInstance *instance;
} SetupJob;

static void *setup_thread_func(void *arg) {
    SetupJob *job = arg;

    TypioResult res = typio_rime_setup_rime_ice(job->user_data_dir);
    if (res != TYPIO_OK) {
        typio_log_error("rime: setup failed");
        free(job->user_data_dir);
        free(job);
        return NULL;
    }

    if (job->instance) {
        typio_instance_set_engine_config_key(
            job->instance, "rime", "schema", "rime_ice");
    }

    typio_log_info("rime: running deployment after rime-ice install");

    for (int i = 0; i < 60; i++) {
        if (!typio_rime_is_maintaining(job->state)) {
            break;
        }
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 500000000 };
        nanosleep(&ts, NULL);
    }

    typio_rime_invalidate_generated_yaml(job->state);
    typio_rime_run_maintenance(job->state, true);
    job->state->api->join_maintenance_thread();
    job->state->maintenance_done = true;
    typio_log_info("rime: setup and deployment complete");

    free(job->user_data_dir);
    free(job);
    return NULL;
}

/* -------------------------------------------------------------------------- */
/* Engine command surface                                                     */
/* -------------------------------------------------------------------------- */

static const TypioEngineCommand *rime_list_commands(TypioEngine *engine,
                                                    size_t *out_count) {
    TypioRimeState *state = typio_engine_get_user_data(engine);
    if (out_count) {
        *out_count = 0;
    }
    if (!state) {
        return nullptr;
    }
    state->control.commands[0].id = "setup";
    state->control.commands[0].label = "Download rime-ice scheme and deploy";
    if (out_count) {
        *out_count = 1;
    }
    return state->control.commands;
}

static TypioResult rime_invoke_command(TypioEngine *engine, const char *id) {
    TypioRimeState *state = typio_engine_get_user_data(engine);
    if (!state || !id) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }
    if (strcmp(id, "setup") == 0) {
        if (!typio_rime_ensure_dir(state->config.user_data_dir)) {
            return TYPIO_ERROR;
        }
        SetupJob *job = calloc(1, sizeof(SetupJob));
        if (!job) {
            return TYPIO_ERROR_OUT_OF_MEMORY;
        }
        job->user_data_dir = strdup(state->config.user_data_dir);
        job->state = state;
        job->instance = engine->instance;
        if (!job->user_data_dir) {
            free(job);
            return TYPIO_ERROR_OUT_OF_MEMORY;
        }
        pthread_t tid;
        if (pthread_create(&tid, NULL, setup_thread_func, job) != 0) {
            typio_log_error("rime: failed to spawn setup thread");
            free(job->user_data_dir);
            free(job);
            return TYPIO_ERROR;
        }
        pthread_detach(tid);
        typio_log_info("rime: setup started in background");
        return TYPIO_OK;
    }
    if (strcmp(id, "deploy") == 0) {
        state->deploy_requested = true;
        return typio_rime_reload_config(engine);
    }
    return TYPIO_ERROR_NOT_FOUND;
}

const TypioEngineSurfaceOps typio_rime_surface_ops = {
    .list_commands = rime_list_commands,
    .invoke_command = rime_invoke_command,
};

/* -------------------------------------------------------------------------- */
/* Config-change hook                                                         */
/* -------------------------------------------------------------------------- */
/*
 * `engines.rime.schema` — re-apply the selected schema live by routing
 * through `reload_config`, which already covers the librime-side work.
 */

void typio_rime_on_config_change(TypioEngine *engine,
                                 const char *key,
                                 const char *value) {
    TypioRimeState *state;

    (void)value;

    if (!engine || !key) {
        return;
    }
    state = typio_engine_get_user_data(engine);
    if (!state) {
        return;
    }

    if (strcmp(key, "engines.rime.schema") == 0) {
        typio_rime_reload_config(engine);
        return;
    }
}

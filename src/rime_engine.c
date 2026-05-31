/**
 * @file rime_engine.c
 * @brief Rime engine plugin entry point and TypioEngineBaseOps + TypioKeyboardEngineOps implementation
 *
 * Integration with librime 1.16.1:
 *   - Uses set_notification_handler for deploy / option events instead of
 *     polling is_maintenance_mode().
 *   - Removes defensive NULL checks on api function pointers (librime 1.0+
 *     guarantees their existence after setup()).
 *   - Logs the linked librime version on init for troubleshooting.
 */

#include "rime_internal.h"
#include <stddef.h>

/* -------------------------------------------------------------------------- */
/* librime notification handler                                               */
/* -------------------------------------------------------------------------- */

static void typio_rime_notification(void *context_object,
                                     RimeSessionId session_id,
                                     const char *message_type,
                                     const char *message_value) {
    TypioRimeState *state = context_object;

    if (!state || !message_type || !message_value) {
        return;
    }

    if (strcmp(message_type, "deploy") == 0) {
        if (strcmp(message_value, "start") == 0) {
            typio_log_info("Rime: deployment started");
            state->maintenance_done = false;
        } else if (strcmp(message_value, "success") == 0) {
            state->maintenance_done = true;
            typio_log_info("Rime: deployment finished");
        } else if (strcmp(message_value, "failure") == 0) {
            typio_log_error("Rime: deployment failed");
        }
        return;
    }

    if (strcmp(message_type, "option") == 0) {
        if (strcmp(message_value, "ascii_mode") == 0) {
            typio_log_info("Rime: switched to English (ascii_mode)");
        } else if (strcmp(message_value, "!ascii_mode") == 0) {
            typio_log_info("Rime: switched to Chinese (native mode)");
        }
        if (state->engine) {
            typio_rime_publish_status(state->engine, session_id);
        }
        return;
    }

    if (strcmp(message_type, "schema") == 0) {
        typio_log_info("Rime: schema changed to %s", message_value);
        if (state->engine) {
            typio_rime_publish_status(state->engine, session_id);
        }
        return;
    }
}

/* -------------------------------------------------------------------------- */
/* TypioEngineBaseOps + TypioKeyboardEngineOps implementation                 */
/* -------------------------------------------------------------------------- */

static TypioResult typio_rime_init(TypioEngine *engine, TypioInstance *instance) {
    TypioRimeState *state;
    TypioResult result;
    const char *rime_version;

    if (!engine || !instance) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    state = calloc(1, sizeof(*state));
    if (!state) {
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    result = typio_rime_load_config(engine, instance, &state->config);
    if (result != TYPIO_OK) {
        free(state);
        return result;
    }

    if (!typio_rime_ensure_dir(state->config.user_data_dir)) {
        typio_rime_free_config(&state->config);
        free(state);
        return TYPIO_ERROR;
    }

    state->engine = engine;
    state->api = rime_get_api();
    if (!state->api) {
        typio_rime_free_config(&state->config);
        free(state);
        return TYPIO_ERROR;
    }

    /* Log linked librime version for troubleshooting */
    rime_version = state->api->get_version();
    typio_log_info("librime version: %s", rime_version ? rime_version : "unknown");

    memset(&state->traits, 0, sizeof(state->traits));
    RIME_STRUCT_INIT(RimeTraits, state->traits);
    state->traits.shared_data_dir = state->config.shared_data_dir;
    state->traits.user_data_dir = state->config.user_data_dir;
    state->traits.distribution_name = "Typio";
    state->traits.distribution_code_name = "typio";
    state->traits.distribution_version = TYPIO_ENGINE_RIME_VERSION;
    state->traits.app_name = "rime.typio";
    state->traits.min_log_level = 1;

    /* librime requires setup() before any other API call. Register the
     * notification handler after setup() but before initialize()/maintenance,
     * so we still receive deploy start/success/failure events. */
    state->api->setup(&state->traits);
    state->api->set_notification_handler(typio_rime_notification, state);
    state->api->initialize(&state->traits);

    state->initialized = true;

    /* Kick off deployment if needed */
    typio_rime_ensure_deployed(state);

    typio_engine_set_user_data(engine, state);
    return TYPIO_OK;
}

static void typio_rime_destroy(TypioEngine *engine) {
    TypioRimeState *state = typio_engine_get_user_data(engine);

    if (!state) {
        return;
    }

    if (state->initialized) {
        state->api->finalize();
    }

    typio_rime_free_config(&state->config);
    free(state);
    typio_engine_set_user_data(engine, nullptr);
}

static void typio_rime_focus_in(TypioEngine *engine, TypioInputContext *ctx) {
    TypioRimeSession *session = typio_rime_get_session(engine, ctx, true);
    if (session) {
        typio_rime_publish_status(engine, session->session_id);
        typio_rime_sync_context(session, ctx);
    }
}

static void typio_rime_reset(TypioEngine *engine, TypioInputContext *ctx) {
    TypioRimeSession *session = typio_rime_get_session(engine, ctx, false);

    if (!session) {
        typio_rime_clear_state(ctx);
        return;
    }

    session->state->api->clear_composition(session->session_id);
    typio_rime_clear_state(ctx);
    typio_rime_publish_status(engine, session->session_id);
}

static void typio_rime_focus_out(TypioEngine *engine, TypioInputContext *ctx) {
    /* Focus loss ends composition, not the context-owned Rime session. */
    typio_rime_reset(engine, ctx);
}

static TypioKeyProcessResult typio_rime_process_key(TypioKeyboardEngine *engine,
                                                     TypioInputContext *ctx,
                                                     const TypioKeyEvent *event) {
    TypioEngine *base = (TypioEngine *)engine;
    TypioRimeSession *session;
    bool handled;
    bool committed;
    bool composing;
    bool is_release;
    uint32_t rime_mask;

    if (!engine || !ctx || !event) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    is_release = (event->type == TYPIO_EVENT_KEY_RELEASE);

    /* Handle Escape on press only */
    if (!is_release && typio_key_event_is_escape(event)) {
        const TypioPreedit *preedit = typio_input_context_get_preedit(ctx);

        if (preedit && preedit->segment_count > 0) {
            typio_rime_reset(base, ctx);
            return TYPIO_KEY_HANDLED;
        }

        return TYPIO_KEY_NOT_HANDLED;
    }

    session = typio_rime_get_session(base, ctx, true);
    if (!session) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    rime_mask = typio_rime_modifiers_to_mask(event->modifiers);
    if (is_release) {
        rime_mask |= TYPIO_RIME_RELEASE_MASK;
    }

    uint32_t rime_keysym = event->keysym;
    if (event->struct_size >= offsetof(TypioKeyEvent, base_keysym) + sizeof(uint32_t)
        && event->base_keysym != 0
        && (event->modifiers & TYPIO_MOD_SHIFT)) {
        rime_keysym = event->base_keysym;
    }

    handled = session->state->api->process_key(
        session->session_id,
        (int)rime_keysym,
        (int)rime_mask);

    /* ascii_mode / schema switches are reflected by the librime notification
     * handler (typio_rime_notification -> publish_status), which fires
     * synchronously during process_key. No post-key polling needed. */

    if (!handled) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    committed = typio_rime_flush_commit(session, ctx);
    composing = typio_rime_sync_context(session, ctx);

    if (committed) {
        return TYPIO_KEY_COMMITTED;
    }
    if (composing) {
        return TYPIO_KEY_COMPOSING;
    }
    return TYPIO_KEY_HANDLED;
}

static void typio_rime_apply_runtime_config(TypioEngine *engine) {
    TypioInputContext *ctx;
    TypioRimeSession *session;

    if (!engine || !engine->instance) {
        return;
    }

    ctx = typio_instance_get_focused_context(engine->instance);
    if (!ctx) {
        return;
    }

    /* Use create=true: after a deploy, the deploy_id mismatch causes
     * get_session to recreate it fresh with the new compiled data.
     * For a plain config reload there is no deploy_id change and the existing
     * valid session is returned as before. */
    session = typio_rime_get_session(engine, ctx, true);
    if (!session) {
        return;
    }

    session->state->api->clear_composition(session->session_id);

    if (!typio_rime_apply_schema(session)) {
        return;
    }

    typio_rime_sync_context(session, ctx);
}

TypioResult typio_rime_reload_config(TypioEngine *engine) {
    TypioRimeState *state = typio_engine_get_user_data(engine);
    TypioConfig *engine_config;
    const char *schema;

    if (!state) {
        return TYPIO_ERROR_NOT_INITIALIZED;
    }

    if (!engine->instance) {
        return TYPIO_OK;
    }

    if (state->deploy_requested) {
        state->deploy_requested = false;
        /* librime tracks source changes with second-resolution timestamps.
         * A user can rewrite default.custom.yaml twice within one second,
         * so explicit deploy must invalidate generated YAML to force rebuild. */
        typio_rime_invalidate_generated_yaml(state);
        if (!typio_rime_run_maintenance(state, true)) {
            return TYPIO_ERROR;
        }
        /* Sessions will be invalidated by deploy_id mismatch on next use */
    }

    engine_config = typio_instance_get_engine_config(engine->instance, "rime");
    if (!engine_config) {
        typio_rime_apply_runtime_config(engine);
        return TYPIO_OK;
    }

    /* The engine owns its schema selection (engines.rime.schema). */
    schema = typio_config_get_string(engine_config, "schema", nullptr);
    free(state->config.schema);
    state->config.schema = typio_strdup((schema && *schema) ? schema
                                                            : TYPIO_RIME_DEFAULT_SCHEMA);

    typio_config_free(engine_config);
    typio_rime_apply_runtime_config(engine);
    return TYPIO_OK;
}

/* -------------------------------------------------------------------------- */
/* Engine metadata and entry points                                           */
/* -------------------------------------------------------------------------- */

static const char *const typio_rime_required_caps[] = {
    "preedit", "candidates", NULL,
};

static const char *const typio_rime_optional_caps[] = {
    "prediction", "learning", NULL,
};

static const TypioEngineInfo typio_rime_engine_info = {
    .name = "rime",
    .display_name = "Rime",
    .description = "Chinese input engine powered by librime.",
    .author = "Typio",
    .icon = "typio-rime-symbolic",
    .language = "zh_CN",
    .type = TYPIO_ENGINE_TYPE_KEYBOARD,
    .required_capabilities = typio_rime_required_caps,
    .optional_capabilities = typio_rime_optional_caps,
};

static const TypioEngineBaseOps typio_rime_base_ops = {
    .init = typio_rime_init,
    .destroy = typio_rime_destroy,
    .deactivate = nullptr,
    .focus_in = typio_rime_focus_in,
    .focus_out = typio_rime_focus_out,
    .reset = typio_rime_reset,
    .reload_config = typio_rime_reload_config,
    .on_config_change = typio_rime_on_config_change,
};

static const TypioKeyboardEngineOps typio_rime_keyboard_ops = {
    .process_key = typio_rime_process_key,
    .get_status = typio_rime_get_status,
    .set_status = typio_rime_set_status,
};

static TypioKeyboardEngine *typio_rime_engine_create(void) {
    TypioKeyboardEngine *engine = typio_keyboard_engine_new(&typio_rime_engine_info,
                                                             &typio_rime_base_ops,
                                                             &typio_rime_keyboard_ops);
    if (engine) {
        typio_engine_set_surface_ops(&engine->base, &typio_rime_surface_ops);
    }
    return engine;
}

/* -------------------------------------------------------------------------- */
/* Config schema (ADR-0008)                                                   */
/* -------------------------------------------------------------------------- */
/*
 * Declared at plugin load time so the host knows about rime's knobs without
 * having to instantiate the engine. The librime-runtime schema choices are
 * intentionally not baked in here (they depend on what is installed); the
 * `schema` field is a free-form STRING and the engine validates the choice
 * during `on_config_change` / `reload_config`.
 */

static const TypioConfigField typio_rime_config_fields[] = {
    {
        .key = "engines.rime.schema",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
        .ui_label = "Schema",
        .ui_section = "rime",
        .runtime_property = NULL,
    },
    {
        .key = "engines.rime.shared_data_dir",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
        .ui_label = "Shared data directory",
        .ui_section = "rime",
        .runtime_property = NULL,
    },
    {
        .key = "engines.rime.user_data_dir",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
        .ui_label = "User data directory",
        .ui_section = "rime",
        .runtime_property = NULL,
    },
};

TYPIO__EXTERN_C TYPIO_EXPORT const TypioConfigField *
typio_engine_get_config_schema(size_t *out_count) {
    if (out_count) {
        *out_count = sizeof(typio_rime_config_fields) /
                     sizeof(typio_rime_config_fields[0]);
    }
    return typio_rime_config_fields;
}

TYPIO_KEYBOARD_ENGINE_DEFINE(typio_rime_engine_info, typio_rime_engine_create)

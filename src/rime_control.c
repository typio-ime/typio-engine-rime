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
        TypioResult res = typio_rime_setup_rime_ice(state->config.user_data_dir);
        if (res != TYPIO_OK) {
            return res;
        }
        state->deploy_requested = true;
        return typio_rime_reload_config(engine);
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
 *
 * `engines.rime.shared_data_dir` / `user_data_dir` — librime resolves these
 * during `setup`/`initialize` and cannot rebind them without re-running
 * deployment. We log a warning here so the host surfaces the restart
 * requirement; persistence is already in the unified config tree.
 */

void typio_rime_on_config_change(TypioEngine *engine,
                                 const char *key,
                                 const char *value) {
    TypioRimeState *state;

    (void)value; /* the canonical value is read back from the config tree */

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
    if (strcmp(key, "engines.rime.shared_data_dir") == 0 ||
        strcmp(key, "engines.rime.user_data_dir") == 0) {
        typio_log_warning(
            "Rime data directories require restarting Typio to take effect");
        return;
    }
}

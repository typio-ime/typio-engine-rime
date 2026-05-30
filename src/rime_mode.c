/**
 * @file rime_mode.c
 * @brief Rime engine status reflection (ADR-0009).
 *
 * The engine's observable state has two orthogonal axes:
 *   - engagement: composing (ACTIVE) vs ascii passthrough (PASSTHROUGH).
 *   - active profile: the Rime *schema* (an open, user-defined set).
 *
 * Both are derived from the single source of truth — librime's RimeStatus —
 * never from a shadow copy. `typio_rime_publish_status` reads the live status
 * and pushes a TypioEngineStatus to the framework; the tray/panel re-resolve
 * from there. Schema *switching* is owned by the config-schema layer
 * (ADR-0008); this file only *reflects* whatever librime currently reports,
 * and mirrors librime-initiated schema switches back into the config tree so
 * the canonical state never drifts.
 */

#include "rime_internal.h"

/* -------------------------------------------------------------------------- */
/* Icon (engagement axis only)                                                */
/* -------------------------------------------------------------------------- */
/*
 * The tray icon reflects *engagement*, not the active schema. The engine owns
 * exactly two icons — composing vs ascii passthrough. We deliberately do NOT
 * map the (open, user-defined) schema set onto per-schema icons: requiring a
 * user to ship an SVG for every custom schema would push an asset burden onto
 * them. The schema is conveyed by its name on the profile axis (profile_label
 * → tooltip / panel / candidate bar), never by the icon. (ADR-0009)
 */

#define TYPIO_RIME_ICON_NATIVE "typio-rime-symbolic"
#define TYPIO_RIME_ICON_LATIN  "typio-rime-latin-symbolic"

/* -------------------------------------------------------------------------- */
/* Mode construction from live RimeStatus                                     */
/* -------------------------------------------------------------------------- */

static void rime_fill_mode(TypioRimeState *state, RimeSessionId session_id,
                           TypioRimeModeBuf *buf) {
    bool ascii = false;

    buf->profile_id[0] = '\0';
    buf->profile_label[0] = '\0';

    if (state && state->api) {
        RIME_STRUCT(RimeStatus, status);
        if (state->api->get_status(session_id, &status)) {
            ascii = status.is_ascii_mode != 0;
            if (status.schema_id) {
                snprintf(buf->profile_id, sizeof(buf->profile_id),
                         "%s", status.schema_id);
            }
            if (status.schema_name) {
                snprintf(buf->profile_label, sizeof(buf->profile_label),
                         "%s", status.schema_name);
            }
            state->api->free_status(&status);
        } else if (state->config.schema) {
            snprintf(buf->profile_id, sizeof(buf->profile_id),
                     "%s", state->config.schema);
        }
    }

    snprintf(buf->icon, sizeof(buf->icon),
             "%s", ascii ? TYPIO_RIME_ICON_LATIN : TYPIO_RIME_ICON_NATIVE);
    snprintf(buf->display_label, sizeof(buf->display_label),
             "%s", ascii ? "A" : "中");

    buf->mode.engagement = ascii ? TYPIO_ENGAGE_PASSTHROUGH : TYPIO_ENGAGE_ACTIVE;
    buf->mode.profile_id = buf->profile_id;
    buf->mode.profile_label = buf->profile_label[0] ? buf->profile_label : nullptr;
    buf->mode.display_label = buf->display_label;
    buf->mode.icon_name = buf->icon;
}

/* -------------------------------------------------------------------------- */
/* Write-back: a librime-initiated schema switch (F4 menu, hotkey) must be     */
/* mirrored into the unified config tree so panel/D-Bus/persistence stay true. */
/* -------------------------------------------------------------------------- */

static void rime_writeback_schema(TypioEngine *engine, const char *schema_id) {
    TypioConfig *root;
    const char *current;

    if (!engine || !engine->instance || !schema_id || !*schema_id) {
        return;
    }
    /* Live root config — owned by the instance, must NOT be freed here. */
    root = typio_instance_get_config(engine->instance);
    if (!root) {
        return;
    }
    current = typio_config_get_string(root, "engines.rime.schema", "");
    if (current && strcmp(current, schema_id) == 0) {
        /* Already canonical. This equality also terminates the
         * config-set -> on_config_change -> select_schema -> schema
         * notification -> publish loop. */
        return;
    }
    typio_config_set_string(root, "engines.rime.schema", schema_id);
    typio_instance_save_config(engine->instance);
}

/* -------------------------------------------------------------------------- */
/* Public surface                                                             */
/* -------------------------------------------------------------------------- */

void typio_rime_publish_status(TypioEngine *engine, RimeSessionId session_id) {
    TypioRimeState *state;
    TypioRimeModeBuf buf;

    if (!engine || !engine->instance) {
        return;
    }
    state = typio_engine_get_user_data(engine);
    if (!state || !state->api) {
        return;
    }

    rime_fill_mode(state, session_id, &buf);
    typio_instance_notify_status(engine->instance, &buf.mode);
    rime_writeback_schema(engine, buf.profile_id);
}

/* Fallback used before any session/status exists. */
static const TypioEngineStatus typio_rime_default_mode = {
    .engagement = TYPIO_ENGAGE_ACTIVE,
    .profile_id = "",
    .profile_label = nullptr,
    .display_label = "中",
    .icon_name = "typio-rime-symbolic",
};

const TypioEngineStatus *typio_rime_get_status(TypioKeyboardEngine *engine,
                                            TypioInputContext *ctx) {
    TypioEngine *base = (TypioEngine *)engine;
    TypioRimeState *state = typio_engine_get_user_data(base);
    TypioRimeSession *session = typio_rime_get_session(base, ctx, false);

    if (!session || !state || !state->api) {
        return &typio_rime_default_mode;
    }

    /* Build into session-owned storage so the returned pointer stays valid
     * until the next engine operation on this context. */
    rime_fill_mode(state, session->session_id, &session->modebuf);
    return &session->modebuf.mode;
}

TypioResult typio_rime_set_status(TypioKeyboardEngine *engine,
                                 TypioInputContext *ctx,
                                 const char *profile_id) {
    TypioEngine *base = (TypioEngine *)engine;
    TypioRimeSession *session;

    if (!engine || !ctx || !profile_id || !*profile_id) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    session = typio_rime_get_session(base, ctx, true);
    if (!session || !session->state) {
        return TYPIO_ERROR_NOT_INITIALIZED;
    }

    /* The reported profile_id is the Rime schema id; restore it by selecting
     * that schema. librime's "schema" notification then drives publish. */
    if (!session->state->api->select_schema(session->session_id, profile_id)) {
        return TYPIO_ERROR_NOT_FOUND;
    }
    typio_rime_publish_status(base, session->session_id);
    return TYPIO_OK;
}

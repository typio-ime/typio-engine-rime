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
 * and pushes a TypioKeyboardEngineMode to the framework; the tray/panel re-resolve
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

static void rime_fill_status(TypioRimeState *state, RimeSessionId session_id,
                             TypioRimeStatusBuf *buf) {
    bool ascii = false;

    memset(buf, 0, sizeof(*buf));

    if (state && state->api) {
        RIME_STRUCT(RimeStatus, status);
        if (state->api->get_status(session_id, &status)) {
            ascii = status.is_ascii_mode != 0;
            if (status.schema_id) {
                snprintf(buf->mode_id, sizeof(buf->mode_id),
                         "%s", status.schema_id);
            }
            if (status.schema_name) {
                snprintf(buf->mode_label, sizeof(buf->mode_label),
                         "%s", status.schema_name);
            }
            state->api->free_status(&status);
        } else if (state->config.schema) {
            snprintf(buf->mode_id, sizeof(buf->mode_id),
                     "%s", state->config.schema);
        }
    }

    if (ascii && buf->mode_id[0]) {
        size_t len = strlen(buf->mode_id);
        if (len + 6 < sizeof(buf->mode_id)) {
            memcpy(buf->mode_id + len, ":ascii", 7);
        }
    }

    snprintf(buf->icon, sizeof(buf->icon),
             "%s", ascii ? TYPIO_RIME_ICON_LATIN : TYPIO_RIME_ICON_NATIVE);
    if (ascii) {
        snprintf(buf->display_label, sizeof(buf->display_label), "A");
    } else if (buf->mode_label[0]) {
        snprintf(buf->display_label, sizeof(buf->display_label),
                 "%s", buf->mode_label);
    } else {
        snprintf(buf->display_label, sizeof(buf->display_label), "中");
    }

    buf->status.id = buf->mode_id;
    buf->status.label = buf->mode_label[0] ? buf->mode_label : NULL;
    buf->status.display_label = buf->display_label;
    buf->status.icon_name = buf->icon;
    /* ascii_mode behaves like a plain Latin keyboard — what you type is what you
     * get, so it never warrants an unprompted announcement. Composing in a CJK
     * schema can surprise the user who starts typing without looking, so it is
     * notable. See docs/dev/keyboard-status-salience.md (libtypio). */
    buf->status.salience = ascii ? TYPIO_STATUS_SALIENCE_QUIET
                                 : TYPIO_STATUS_SALIENCE_NOTABLE;
}

/* -------------------------------------------------------------------------- */
/* Write-back: a librime-initiated schema switch (F4 menu, hotkey) must be     */
/* mirrored into the unified config tree so panel/D-Bus/persistence stay true. */
/* -------------------------------------------------------------------------- */

static void rime_writeback_schema(TypioEngine *engine, const char *mode_id) {
    TypioConfig *root;
    const char *current;
    char schema_id[128];
    size_t len;

    if (!engine || !engine->instance || !mode_id || !*mode_id) {
        return;
    }

    strncpy(schema_id, mode_id, sizeof(schema_id) - 1);
    schema_id[sizeof(schema_id) - 1] = '\0';
    len = strlen(schema_id);
    if (len > 6 && strcmp(schema_id + len - 6, ":ascii") == 0) {
        schema_id[len - 6] = '\0';
    }

    root = typio_instance_get_config(engine->instance);
    if (!root) {
        return;
    }
    current = typio_config_get_string(root, "engines.rime.schema", "");
    if (current && strcmp(current, schema_id) == 0) {
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
    TypioRimeStatusBuf buf;

    if (!engine || !engine->instance) {
        return;
    }
    state = typio_engine_get_user_data(engine);
    if (!state || !state->api) {
        return;
    }

    rime_fill_status(state, session_id, &buf);
    typio_instance_notify_keyboard_mode(engine->instance, &buf.status);
    rime_writeback_schema(engine, buf.mode_id);
}

/* Fallback used before any session/status exists. */
static const TypioKeyboardEngineMode typio_rime_default_mode = {
    .id = "",
    .label = NULL,
    .display_label = "中",
    .icon_name = "typio-rime-symbolic",
    .profile_id = NULL,
    .profile_label = NULL,
    .description = NULL,
    .salience = TYPIO_STATUS_SALIENCE_NOTABLE,
};

const TypioKeyboardEngineMode *typio_rime_list_modes(TypioKeyboardEngine *engine,
                                                     size_t *out_count) {
    (void)engine;
    if (out_count) {
        *out_count = 0;
    }
    return NULL;
}

const TypioKeyboardEngineMode *typio_rime_get_active_mode(TypioKeyboardEngine *engine,
                                                  TypioInputContext *ctx) {
    TypioEngine *base = (TypioEngine *)engine;
    TypioRimeState *state = typio_engine_get_user_data(base);
    TypioRimeSession *session = typio_rime_get_session(base, ctx, false);

    if (!session || !state || !state->api) {
        return &typio_rime_default_mode;
    }

    /* Build into session-owned storage so the returned pointer stays valid
     * until the next engine operation on this context. */
    rime_fill_status(state, session->session_id, &session->statusbuf);
    return &session->statusbuf.status;
}

TypioResult typio_rime_set_active_mode(TypioKeyboardEngine *engine,
                                       TypioInputContext *ctx,
                                       const char *mode_id) {
    TypioEngine *base = (TypioEngine *)engine;
    TypioRimeSession *session;
    char schema_id[128];
    size_t len;
    bool want_ascii;

    if (!engine || !ctx || !mode_id || !*mode_id) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    strncpy(schema_id, mode_id, sizeof(schema_id) - 1);
    schema_id[sizeof(schema_id) - 1] = '\0';
    len = strlen(schema_id);
    want_ascii = (len > 6 && strcmp(schema_id + len - 6, ":ascii") == 0);
    if (want_ascii) {
        schema_id[len - 6] = '\0';
    }

    session = typio_rime_get_session(base, ctx, true);
    if (!session || !session->state) {
        return TYPIO_ERROR_NOT_INITIALIZED;
    }

    if (!session->state->api->select_schema(session->session_id, schema_id)) {
        return TYPIO_ERROR_NOT_FOUND;
    }
    session->state->api->set_option(session->session_id, "ascii_mode", want_ascii);
    typio_rime_publish_status(base, session->session_id);
    return TYPIO_OK;
}

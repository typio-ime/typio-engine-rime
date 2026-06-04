/**
 * @file rime_session.c
 * @brief librime session lifecycle tied to TypioInputContext
 *
 * Rime sessions are owned by the input context rather than transient focus
 * state. Losing focus clears composition UI, but the session itself lives
 * until the context is destroyed so runtime options like ascii_mode survive
 * focus churn and engine switches within the same context.
 */

#include "rime_internal.h"

void typio_rime_free_session(void *data) {
    TypioRimeSession *session = data;

    if (!session) {
        return;
    }

    if (session->state &&
        session->session_id != 0 &&
        session->state->api->find_session(session->session_id)) {
        session->state->api->destroy_session(session->session_id);
    }

    free(session);
}

char *typio_rime_resolve_schema(TypioRimeState *state,
                                const char *requested_schema) {
    RimeSchemaList list = {0};
    char *fallback = nullptr;
    bool requested = requested_schema && *requested_schema;

    if (!state || !state->api) {
        return nullptr;
    }

    if (!state->api->get_schema_list(&list)) {
        return requested ? typio_strdup(requested_schema) : nullptr;
    }

    for (size_t i = 0; i < list.size; ++i) {
        const char *schema_id = list.list[i].schema_id;
        if (!schema_id || !*schema_id) {
            continue;
        }
        if (!fallback) {
            fallback = typio_strdup(schema_id);
        }
        if (requested && strcmp(schema_id, requested_schema) == 0) {
            state->api->free_schema_list(&list);
            free(fallback);
            return typio_strdup(requested_schema);
        }
    }

    state->api->free_schema_list(&list);

    if (requested) {
        if (fallback) {
            typio_log_warning(
                "Configured Rime schema '%s' is unavailable; falling back to '%s'",
                requested_schema,
                fallback);
        } else {
            typio_log_error("Configured Rime schema '%s' is unavailable and no fallback schema exists",
                            requested_schema);
        }
    } else if (fallback) {
        typio_log_info("Auto-selected Rime schema: %s", fallback);
    } else {
        typio_log_error("No Rime schema available to auto-select");
    }

    return fallback;
}

bool typio_rime_apply_schema(TypioRimeSession *session) {
    TypioRimeState *state;
    bool ok;
    char *schema_to_select = nullptr;

    if (!session || !session->state) {
        return false;
    }

    state = session->state;
    schema_to_select = typio_rime_resolve_schema(state, state->config.schema);
    if (!schema_to_select) {
        return false;
    }

    ok = state->api->select_schema(session->session_id, schema_to_select);
    if (!ok && !state->maintenance_done) {
        if (!typio_rime_ensure_deployed(state)) {
            free(schema_to_select);
            return false;
        }
        ok = state->api->select_schema(session->session_id, schema_to_select);
    }

    if (ok) {
        RIME_STRUCT(RimeStatus, status);
        if (state->api->get_status(session->session_id, &status)) {
            if (status.schema_id &&
                strcmp(status.schema_id, schema_to_select) != 0) {
                typio_log_warning(
                    "Schema mismatch after select_schema: expected '%s', got '%s'",
                    schema_to_select, status.schema_id);
            }
            state->api->free_status(&status);
        }
    } else {
        typio_log_warning("Failed to select Rime schema: %s", schema_to_select);
    }

    if (ok && (!state->config.schema ||
               strcmp(state->config.schema, schema_to_select) != 0)) {
        free(state->config.schema);
        state->config.schema = typio_strdup(schema_to_select);
    }

    free(schema_to_select);
    return ok;
}

TypioRimeSession *typio_rime_get_session(TypioEngine *engine,
                                          TypioInputContext *ctx,
                                          bool create) {
    TypioRimeState *state = typio_engine_get_user_data(engine);
    TypioRimeSession *session;

    if (!state || !state->api || !ctx) {
        return nullptr;
    }

    session = typio_input_context_get_property(ctx, TYPIO_RIME_SESSION_KEY);
    if (session && state->api->find_session(session->session_id)) {
        if (session->deploy_id == state->deploy_id) {
            return session;
        }

        /* Deployment happened; current session is stale. Clear it so we
         * create a new one below. */
        typio_input_context_set_property(ctx, TYPIO_RIME_SESSION_KEY, NULL, NULL);
        session = NULL;
    }

    if (!create) {
        return nullptr;
    }

    if (!typio_rime_ensure_deployed(state)) {
        /* Still deploying; don't block, just return null for now */
        return nullptr;
    }

    session = calloc(1, sizeof(*session));
    if (!session) {
        return nullptr;
    }

    session->state = state;
    session->deploy_id = state->deploy_id;
    session->session_id = state->api->create_session();
    if (session->session_id == 0) {
        free(session);
        typio_log_error("Failed to create Rime session");
        return nullptr;
    }

    if (!typio_rime_apply_schema(session)) {
        typio_rime_free_session(session);
        return nullptr;
    }

    typio_input_context_set_property(ctx, TYPIO_RIME_SESSION_KEY, session, typio_rime_free_session);
    typio_rime_publish_status(engine, session->session_id);
    return session;
}

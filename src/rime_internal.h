/**
 * @file rime_internal.h
 * @brief Internal shared definitions for the Rime engine plugin
 *
 * This header declares data structures, constants, and function prototypes
 * used across all rime engine modules.  It should not be included by code
 * outside engines/rime/.
 */

#ifndef TYPIO_RIME_INTERNAL_H
#define TYPIO_RIME_INTERNAL_H

#include "typio/abi/abi.h"
#include "typio/abi/instance.h"
#include "typio/schema/config_schema.h"

#include <rime_api.h>

/* Engine version reported in TypioEngineInfo and to librime's traits.
 * Defined by the build (meson) from the project version. */
#ifndef TYPIO_ENGINE_RIME_VERSION
#define TYPIO_ENGINE_RIME_VERSION "0.0.0"
#endif

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef TYPIO_RIME_SHARED_DATA_DIR
#define TYPIO_RIME_SHARED_DATA_DIR "/usr/share/rime-data"
#endif

#define TYPIO_RIME_SESSION_KEY       "rime.session"
#define TYPIO_RIME_DEFAULT_SCHEMA    ""
#define TYPIO_RIME_SLOW_SYNC_MS      8

/* Modifier mask mapping from Typio modifiers to Rime masks */
enum {
    TYPIO_RIME_SHIFT_MASK    = (1 << 0),
    TYPIO_RIME_LOCK_MASK     = (1 << 1),
    TYPIO_RIME_CONTROL_MASK  = (1 << 2),
    TYPIO_RIME_MOD1_MASK     = (1 << 3),
    TYPIO_RIME_MOD2_MASK     = (1 << 4),
    TYPIO_RIME_MOD4_MASK     = (1 << 6),
    TYPIO_RIME_RELEASE_MASK  = (1 << 30),
};

/* -------------------------------------------------------------------------- */
/* Data structures                                                            */
/* -------------------------------------------------------------------------- */

typedef struct TypioRimeConfig {
    char *schema;
    char *shared_data_dir;
    char *user_data_dir;
} TypioRimeConfig;

/*
 * Notification state tracks async librime events (deploy, option change).
 * Instead of polling is_maintenance_mode() we use set_notification_handler
 * and react to callbacks.
 */
/* Static-lifetime command array backing the engine command surface
 * (rime_control.c). The command list never changes after engine load. */
typedef struct TypioRimeControl {
    TypioEngineCommand commands[2];
} TypioRimeControl;

typedef struct TypioRimeState {
    RimeApi *api;
    RimeTraits traits;
    TypioRimeConfig config;
    bool initialized;
    bool maintenance_done;
    TypioEngineAvailability availability;
    const char *availability_reason;
    uint32_t deploy_id;
    /* Back-pointer to the owning engine for notification callbacks */
    struct TypioEngine *engine;
    /* Set by the "deploy" control command; consumed by reload_config. */
    bool deploy_requested;
    TypioRimeControl control;
} TypioRimeState;

/* Backing storage for a TypioKeyboardEngineMode built from RimeStatus. The mode's
 * const char* fields point into these buffers, so the buffer must outlive the
 * borrowed mode pointer (see typio_rime_get_active_mode). */
typedef struct TypioRimeStatusBuf {
    char mode_id[128];       /* Rime schema id, e.g. "luna_pinyin" */
    char mode_label[128];    /* Rime schema name, e.g. "朙月拼音" */
    char icon[160];           /* Resolved freedesktop icon name */
    char display_label[128];   /* schema name or "中" / "A" */
    TypioKeyboardEngineMode status;
} TypioRimeStatusBuf;

typedef struct TypioRimeSession {
    TypioRimeState *state;
    RimeSessionId session_id;
    uint32_t deploy_id;
    TypioRimeStatusBuf statusbuf;  /* storage for get_status's borrowed result */
    bool shift_held;
    bool shift_only;
} TypioRimeSession;

/* -------------------------------------------------------------------------- */
/* Utility functions (rime_utils.c)                                           */
/* -------------------------------------------------------------------------- */

uint64_t typio_rime_monotonic_ms(void);
bool typio_rime_ensure_dir(const char *path);
bool typio_rime_path_exists(const char *path);
bool typio_rime_has_yaml_suffix(const char *name);

/* -------------------------------------------------------------------------- */
/* Configuration (rime_config.c)                                              */
/* -------------------------------------------------------------------------- */

TypioResult typio_rime_load_config(TypioEngine *engine,
                                   TypioInstance *instance,
                                   TypioRimeConfig *config);
void typio_rime_free_config(TypioRimeConfig *config);

/* -------------------------------------------------------------------------- */
/* Command surface + config-change hook (rime_control.c)                      */
/* -------------------------------------------------------------------------- */

/* Re-read schema/config and apply; honours state->deploy_requested. */
TypioResult typio_rime_reload_config(TypioEngine *engine);
/* ADR-0008: invoked by the host when an `engines.rime.*` config key changes. */
void typio_rime_on_config_change(TypioEngine *engine,
                                 const char *key,
                                 const char *value);
extern const TypioEngineSurfaceOps typio_rime_surface_ops;

/* -------------------------------------------------------------------------- */
/* Deployment (rime_deploy.c)                                                */
/* -------------------------------------------------------------------------- */

void typio_rime_invalidate_generated_yaml(TypioRimeState *state);
bool typio_rime_run_maintenance(TypioRimeState *state, bool full_check);
bool typio_rime_is_maintaining(TypioRimeState *state);
bool typio_rime_ensure_deployed(TypioRimeState *state);
void typio_rime_set_availability(TypioRimeState *state,
                                  TypioEngineAvailability availability,
                                  const char *reason);

/* Setup (rime_setup.c)                                                      */

TypioResult typio_rime_setup_rime_ice(const char *user_data_dir);

/* -------------------------------------------------------------------------- */
/* Session management (rime_session.c)                                        */
/* -------------------------------------------------------------------------- */

void typio_rime_free_session(void *data);
bool typio_rime_apply_schema(TypioRimeSession *session);
char *typio_rime_resolve_schema(TypioRimeState *state,
                                const char *requested_schema);
TypioRimeSession *typio_rime_get_session(TypioEngine *engine,
                                          TypioInputContext *ctx,
                                          bool create);

/* -------------------------------------------------------------------------- */
/* Context synchronisation (rime_sync.c)                                      */
/* -------------------------------------------------------------------------- */

void typio_rime_clear_state(TypioInputContext *ctx);
bool typio_rime_flush_commit(TypioRimeSession *session,
                              TypioInputContext *ctx);
bool typio_rime_sync_context(TypioRimeSession *session,
                              TypioInputContext *ctx);

/* -------------------------------------------------------------------------- */
/* Mode management (rime_mode.c)                                              */
/* -------------------------------------------------------------------------- */

/* Read librime's live RimeStatus, push the derived status to the framework, and
 * mirror any librime-initiated schema change back into the config tree. */
void typio_rime_publish_status(TypioEngine *engine, RimeSessionId session_id);
const TypioKeyboardEngineMode *typio_rime_list_modes(TypioKeyboardEngine *engine,
                                                     size_t *out_count);
const TypioKeyboardEngineMode *typio_rime_get_active_mode(TypioKeyboardEngine *engine,
                                                  TypioInputContext *ctx);
TypioResult typio_rime_set_active_mode(TypioKeyboardEngine *engine,
                                       TypioInputContext *ctx,
                                       const char *mode_id);

/* -------------------------------------------------------------------------- */
/* Key handling (rime_key.c)                                                  */
/* -------------------------------------------------------------------------- */

bool typio_rime_is_shift_keysym(uint32_t keysym);
uint32_t typio_rime_modifiers_to_mask(uint32_t modifiers);
void typio_rime_reset_shift_state(TypioRimeSession *session);
TypioKeyProcessResult typio_rime_handle_bare_shift(TypioEngine *base,
                                                     TypioRimeSession *session,
                                                     TypioInputContext *ctx,
                                                     bool is_release);
uint32_t typio_rime_translate_keysym(const TypioKeyEvent *event);

#endif /* TYPIO_RIME_INTERNAL_H */

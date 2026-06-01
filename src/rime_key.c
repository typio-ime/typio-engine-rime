/**
 * @file rime_key.c
 * @brief Key-event translation helpers (modifiers, special keys)
 *
 * Owns modifier mask translation, keysym selection for librime's
 * process_key, and the bare-Shift press/release state machine that
 * replaces librime's schema-dependent ascii_composer/switch_key.
 */

#include "rime_internal.h"
#include <stddef.h>

bool typio_rime_is_shift_keysym(uint32_t keysym) {
    return keysym == TYPIO_KEY_Shift_L || keysym == TYPIO_KEY_Shift_R;
}

uint32_t typio_rime_modifiers_to_mask(uint32_t modifiers) {
    uint32_t mask = 0;

    if (modifiers & TYPIO_MOD_SHIFT) {
        mask |= TYPIO_RIME_SHIFT_MASK;
    }
    if (modifiers & TYPIO_MOD_CTRL) {
        mask |= TYPIO_RIME_CONTROL_MASK;
    }
    if (modifiers & TYPIO_MOD_ALT) {
        mask |= TYPIO_RIME_MOD1_MASK;
    }
    if (modifiers & TYPIO_MOD_SUPER) {
        mask |= TYPIO_RIME_MOD4_MASK;
    }
    if (modifiers & TYPIO_MOD_CAPSLOCK) {
        mask |= TYPIO_RIME_LOCK_MASK;
    }
    if (modifiers & TYPIO_MOD_NUMLOCK) {
        mask |= TYPIO_RIME_MOD2_MASK;
    }

    return mask;
}

void typio_rime_reset_shift_state(TypioRimeSession *session) {
    if (session) {
        session->shift_held = false;
        session->shift_only = false;
    }
}

/**
 * Select the keysym to send to librime.
 *
 * Always pass through the effective keysym unchanged. librime's
 * ascii_composer detects Shift+letter from the modifier mask, not
 * from the keysym case. Remapping to base_keysym caused the first
 * Shift+letter during composition to be treated as a plain letter
 * by librime's speller (keysym 'a' looks like pinyin input).
 */
uint32_t typio_rime_translate_keysym(const TypioKeyEvent *event) {
    return event->keysym;
}

/**
 * Handle bare Shift press/release without delegating to librime.
 *
 * librime's ascii_composer/switch_key is schema-dependent and varies
 * between `inline_ascii`, `commit_text`, `noop`, and `clear`. The
 * engine-level handler provides a single deterministic behavior:
 *
 *   press  → consume, mark shift_held + shift_only
 *   release (bare, no other keys pressed during hold) →
 *       commit raw preedit, clear composition, toggle ascii_mode
 *   release (non-bare) → consume without side effects
 *
 * This matches the expected behavior of mainstream Chinese input
 * methods (Sogou, Microsoft Pinyin, fcitx5).
 */
TypioKeyProcessResult typio_rime_handle_bare_shift(TypioEngine *base,
                                                     TypioRimeSession *session,
                                                     TypioInputContext *ctx,
                                                     bool is_release) {
    if (!is_release) {
        session->shift_held = true;
        session->shift_only = true;
        return TYPIO_KEY_HANDLED;
    }

    session->shift_held = false;

    if (!session->shift_only) {
        return TYPIO_KEY_HANDLED;
    }

    session->shift_only = false;

    /* Extract ASCII-only characters from the preedit and commit them
     * as raw text, so "ni" → "ni" rather than the librime preview. */
    RIME_STRUCT(RimeContext, rctx);
    if (session->state->api->get_context(session->session_id, &rctx)) {
        if (rctx.composition.preedit && *rctx.composition.preedit) {
            const char *p = rctx.composition.preedit;
            size_t len = strlen(p);
            char *raw = calloc(len + 1, 1);
            if (raw) {
                size_t j = 0;
                for (size_t i = 0; i < len; i++) {
                    unsigned char c = (unsigned char)p[i];
                    if (c >= 0x20 && c < 0x7f) {
                        raw[j++] = p[i];
                    }
                }
                raw[j] = '\0';
                if (j > 0) {
                    typio_input_context_commit(ctx, raw);
                }
                free(raw);
            }
        }
        session->state->api->free_context(&rctx);
    }

    session->state->api->clear_composition(session->session_id);

    bool was_ascii = session->state->api->get_option(session->session_id, "ascii_mode");
    session->state->api->set_option(session->session_id, "ascii_mode", !was_ascii);

    typio_rime_clear_state(ctx);
    typio_rime_publish_status(base, session->session_id);
    return TYPIO_KEY_COMMITTED;
}

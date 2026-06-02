# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-06-02

## [0.0.4] - 2026-06-01

### Fixed

- Fix bare Shift key during composition leaving stale preedit. The engine now
  explicitly handles bare Shift press/release instead of relying on librime's
  schema-dependent `key_binder`. On Shift release (when no other keys were pressed
  during the hold), the engine commits the raw preedit text (e.g., typed pinyin),
  clears the composition, and toggles `ascii_mode` via librime's `set_option` API.
  This prevents the candidate panel from disappearing while the preedit remains
  underlined and unresponsive to subsequent input.

- Fix Shift+symbol keys (e.g., Shift+/ → `?`, Shift+' → `"`) producing wrong
  punctuation during composition. The engine now passes the effective keysym
  directly to librime instead of remapping to `base_keysym`, so the punctuator
  receives the correct shifted character for `half_shape` lookup.

## [0.0.3] - 2026-06-01

### Fixed

- Fix indicator garbled text for schema names longer than 7 UTF-8 bytes.
  `display_label` buffer increased from 8 to 128 bytes; `snprintf` was
  truncating multi-byte sequences mid-character, producing invalid UTF-8 that
  rendered as .notdef glyphs (tofu) in the panel. (ADR-0016 in typio-wayland)

## [0.0.2] - 2026-05-30

### Added

- Schema resolution with automatic fallback to an available schema when the
  configured schema is missing; the resolved schema is persisted back to config.
- Engine `deactivate` hook support in base operations.
- New test coverage for invalid configured schema fallback.

### Changed

- Internal naming: renamed `TypioRimeModeBuf` and related symbols to
  `TypioRimeStatusBuf` for consistency with framework terminology.

### Fixed

- Test build: use `libtypio_dep` instead of removed `typio_abi_dep` in
  `tests/meson.build`.

## [0.0.1] - 2026-05-29

### Added

- Initial release of the Rime input engine for the Typio framework, building
  to `libtypio_engine_rime.so` and discovered by hosts under
  `<libdir>/typio/engines`.
- Configuration via the Typio host config `[engines.rime]` section
  (`schema`, `shared_data_dir`, `user_data_dir`).
- Symbolic mode-indicator icons installed under the hicolor icon theme:
  `typio-rime-symbolic` (Chinese input mode) and `typio-rime-latin-symbolic`
  (Latin/ASCII input mode).

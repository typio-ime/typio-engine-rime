# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.3.0] - 2026-06-13

### Added

- Declare supported languages in the manifest
  (`languages = ["zh-Hans", "zh-Hant"]`) for language-first switching
  (typio-linux ADR-0031). The legacy single `language` key is kept for
  older hosts.

### Changed

- Report the active keyboard mode after every mode-affecting request: the
  worker now appends an `ACTIVE_MODE` line to `process-key`, `set-active-mode`,
  `reset`, and `focus-in` replies, so the host's indicator and tray track
  Rime's internal mode (中/英, schema) over the IPC boundary.
- `MODE` / `ACTIVE_MODE` lines now carry a trailing `salience` field
  (`0` quiet, `1` notable) after `is_active`, preserving the engine's on-focus
  auto-reveal intent.

### Fixed

- Worker no longer crashes on shutdown once a session has been created. Teardown
  now runs deactivate → free instance → free engine: deactivate joins librime's
  async deployer and detaches its notification handler (so it can't race the
  host freeing the instance — a double-free), and freeing the instance before
  the engine lets each context's session destructor run while librime and the
  engine state are still alive (previously a use-after-free / `destroy_session`
  after `RimeFinalize`).

## [0.2.0] - 2026-06-06

### Changed

- Build and package Rime as a direct engine executable
  (`typio-engine-rime`) with a `typio-engine-rime.toml` manifest instead of a
  shared engine library.
- Install the private worker under `<libexecdir>/typio/engines` and the
  manifest under `<datadir>/typio/engines`. Installed manifests contain the
  absolute worker path.

## [0.1.4] - 2026-06-04

### Changed

- **Packaging guidance for staged installs.** Document Meson `--destdir`
  staging, expected package artifacts, explicit staging validation, and the
  required runtime role of `libcurl`.
- **Explicit development engine directory.** Update local debugging docs to
  use an explicit `--engine-dir` override instead of relying on implicit
  user-directory discovery.

## [0.1.3] - 2026-06-05

### Fixed

- **Null context guard in `typio_rime_get_session`.** Added `!ctx` check
  before calling `typio_input_context_get_property`, preventing SIGSEGV
  when the engine is queried with a null input context during early
  initialization.

## [0.1.2] - 2026-06-03

### Fixed

- Report `TypioEngineAvailability` during Rime deployment/setup and consume
  keys while not ready, preventing raw key leakage during focus-time warm-up.

## [0.1.1] - 2026-06-02

### Fixed

- Fix icon installation path to conform to the freedesktop/XDG icon-theme
  specification. Icons are now installed under `<datadir>/icons/hicolor/`
  instead of `<datadir>/data/icons/hicolor/`, so they are discoverable by
  standard icon lookups regardless of install prefix (`~/.local`, `/usr/local`,
  `/usr`, etc.).

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

# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

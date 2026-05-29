# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.1] - 2026-05-29

### Added

- Initial release of the Rime input engine for the Typio framework, building
  to `libtypio-engine-rime.so` and discovered by hosts under
  `<libdir>/typio/engines`.
- Configuration via the Typio host config `[engines.rime]` section
  (`schema`, `shared_data_dir`, `user_data_dir`).
- Symbolic mode-indicator icons installed under the hicolor icon theme:
  `typio-rime-symbolic` (Chinese input mode) and `typio-rime-latin-symbolic`
  (Latin/ASCII input mode).

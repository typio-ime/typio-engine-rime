# Setup

## Dependencies

- `meson` (≥ 1.0) and `ninja`
- `pkg-config`
- `librime` development files
- The Typio engine ABI (`typio-engine-abi.pc` and `libtypio-core`)

## Build

The following assumes `libtypio` is checked out as a sibling directory
(`../libtypio`). Adjust the path if your layout differs.

```sh
export PKG_CONFIG_PATH=../libtypio/target/release:$PKG_CONFIG_PATH
meson setup build
ninja -C build
```

To also build the integration tests, append `-Dbuild_tests=true` to the
`meson setup` command.

## Development Run

From the `typio-linux` repository:

```sh
LD_LIBRARY_PATH=../libtypio/target/release \
  ./build/src/typio --verbose \
  --engine-dir ../typio-engine-rime/build
```

The build directory contains `typio-engine-rime` and its development manifest.
The manifest starts `./typio-engine-rime` relative to that directory.

## Rime configuration paths

The engine expects two directories at runtime:

- **Shared data** — system schemas and default settings (e.g.
  `/usr/share/rime-data`).
- **User data** — personal overrides and build output, defaulting to
  `<typio-data-dir>/rime` (often `~/.local/share/typio/rime/`).

Override them in the Typio host configuration:

```toml
[engines.rime]
schema = "luna_pinyin"
user_data_dir = "~/.local/share/typio/rime"
```

See the [Rime Engine Configuration reference](../reference/rime-config.md) for
the full list of keys.

## Running tests

```sh
meson test -C build --print-errorlogs
```

The integration test suite (`suite: integration`) exercises the real IPC
boundary by starting the freshly built worker the same way a Typio host does.
It requires system Rime data and a network-free deploy environment.

To skip integration tests and run only the fast unit tests:

```sh
meson test -C build --no-suite integration --print-errorlogs
```

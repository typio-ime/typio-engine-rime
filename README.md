# typio-engine-rime

The [Rime](https://rime.im/) input engine for the
[Typio](https://github.com/) input method framework, packaged as a
standalone worker.

It builds to `typio-engine-rime`, which a Typio host starts as an
out-of-process worker from the `typio-engine-rime.toml` manifest.

## Building

Requires the [libtypio](https://github.com/ming2k/libtypio) engine ABI
(provides `typio-engine-abi.pc`) and `librime`.

```sh
meson setup build
ninja -C build
ninja -C build install
```

If libtypio is not installed system-wide, point pkg-config at a local
checkout:

```sh
PKG_CONFIG_PATH=/path/to/libtypio/target/release meson setup build
```

### Local debugging install

For quick iteration, point the Typio host at the Meson build directory, which
contains the generated manifest:

```sh
meson setup build
ninja -C build
PATH="$PWD/build:$PATH" typio --engine-dir "$PWD/build" --list
```

Packaged Typio hosts discover system-installed engines from
`<prefix>/<datadir>/typio/engines`. Development engine directories are explicit
runtime overrides via `--engine-dir` or `TYPIO_ENGINE_PATH`.

## Configuration

The engine reads settings from the Typio host config under `[engines.rime]`:

| Key | Default | Description |
|-----|---------|-------------|
| `schema` | `""` | Schema id to use (e.g. `luna_pinyin`). |
| `shared_data_dir` | `/usr/share/rime-data` | System Rime data directory. |
| `user_data_dir` | `<typio-data-dir>/rime` | Per-user Rime directory for `*.custom.yaml` and build output. |

## Tests

```sh
meson setup build -Dbuild_tests=true
meson test -C build
```

The integration test starts the freshly built worker through the same
host-loader path Typio uses, so it exercises the real IPC boundary.
It needs `librime` and the system `rime-data` package.

## ABI

This engine targets the Typio worker protocol and links the libtypio engine
ABI headers for engine-facing types and helper functions. Pre-1.0: rebuild
against each libtypio release.

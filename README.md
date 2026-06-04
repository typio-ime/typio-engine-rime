# typio-engine-rime

The [Rime](https://rime.im/) input engine for the
[Typio](https://github.com/) input method framework, packaged as a
standalone plugin.

It builds to `libtypio_engine_rime.so`, which a Typio host discovers and
loads at runtime from `<libdir>/typio/engines`.

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

For quick iteration, keep the built plugin in an explicit development engine
directory and point the Typio host at that directory:

```sh
meson setup build
ninja -C build
mkdir -p build/engines
cp build/libtypio_engine_rime.so build/engines/
typio --engine-dir "$PWD/build/engines" --list
```

Packaged Typio hosts discover system-installed engines from
`<prefix>/<libdir>/typio/engines`. Development engine directories are explicit
runtime overrides via `--engine-dir` or `TYPIO_ENGINE_DIR`.

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

The integration test loads the freshly built plugin through the same
host-loader path Typio uses, so it exercises the real ABI boundary.
It needs `librime` and the system `rime-data` package.

## ABI

This engine targets the Typio engine ABI version stamped in
`TYPIO_ENGINE_ABI_MAJOR` / `TYPIO_ENGINE_ABI_MINOR` from `typio/abi/types.h`
at build time, exported as the `typio_engine_abi_version` symbol.  The host
refuses to load a plugin with a different ABI major or a newer ABI minor.
Pre-1.0: rebuild against each libtypio release.

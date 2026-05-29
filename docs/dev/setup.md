# Setup

## Dependencies

- `meson` (≥ 1.0) and `ninja`
- `pkg-config`
- `librime` development files
- The Typio engine ABI (`typio-engine-abi.pc` and `libtypio-core`)

Install system packages on Debian/Ubuntu:

```sh
sudo apt-get install -y meson ninja-build pkg-config librime-dev
```

If Typio is built from source and installed to a non-standard prefix, make
sure `PKG_CONFIG_PATH` includes its `lib/pkgconfig` directory:

```sh
export PKG_CONFIG_PATH=/your/prefix/lib/pkgconfig:$PKG_CONFIG_PATH
```

## Build

```sh
meson setup build
ninja -C build
```

To also build the integration tests:

```sh
meson setup build -Dbuild_tests=true
```

## Install for debugging

For day-to-day development it is quickest to copy the built plugin directly
into a user-level engine directory. This avoids `sudo`, keeps the system
package manager clean, and does not require reconfiguring the build with a
custom prefix.

```sh
meson setup build
ninja -C build
mkdir -p ~/.local/share/typio/engines
cp build/libtypio-engine-rime.so ~/.local/share/typio/engines/
```

Typio hosts scan `~/.local/share/typio/engines/` at runtime, so the engine
will be discovered after restarting the host.

If you prefer a conventional `ninja install` (for example, to install to a
staging prefix), you can still use one:

```sh
meson setup build --prefix="$HOME/.local"
ninja -C build install
```

## Rime configuration paths

The engine expects two directories at runtime:

- **Shared data** — system schemas and default settings, usually
  `/usr/share/rime-data` (provided by the `rime-data` package).
- **User data** — personal overrides and build output, defaulting to
  `<typio-data-dir>/rime` (often `~/.local/share/typio/rime/`).

You can override them in the Typio host configuration:

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

The integration test suite (`suite: integration`) exercises the real ABI
boundary by loading the freshly built plugin the same way a Typio host does.
It requires the system `rime-data` package and a network-free deploy
environment.

To skip the integration tests and run only the fast unit tests:

```sh
meson test -C build --no-suite integration --print-errorlogs
```

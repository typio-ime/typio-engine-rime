# Getting Started

This tutorial walks you through building and installing the Rime engine
plugin for the Typio input method framework.

You will:

1. Verify your system has the required dependencies.
2. Build the engine plugin.
3. Install it to a local directory for testing.

## Prerequisites

- A C23-capable compiler (GCC 13+, Clang 16+, etc.)
- `meson` (≥ 1.0), `ninja`, and `pkg-config`
- `librime` and its development headers
- The Typio engine ABI (`libtypio-core` and `typio-engine-abi.pc`)

On Debian or Ubuntu you can install the system dependencies with:

```sh
sudo apt-get update
sudo apt-get install -y meson ninja-build pkg-config librime-dev
```

If you built Typio from source and installed it to a custom prefix, add that
prefix to `PKG_CONFIG_PATH` so Meson can find the engine ABI:

```sh
export PKG_CONFIG_PATH=/your/prefix/lib/pkgconfig:$PKG_CONFIG_PATH
```

## Step 1 — Configure the build

Clone or extract the engine source and create a build directory:

```sh
cd typio-engine-rime
meson setup build
```

If you want to run the test suite later, enable tests during configuration:

```sh
meson setup build -Dbuild_tests=true
```

## Step 2 — Build the plugin

Compile the shared module:

```sh
ninja -C build
```

When the build finishes you will have `build/libtypio_engine_rime.so`.

## Step 3 — Install locally for debugging

While testing or developing, the quickest way to make the engine available
to Typio is to copy the freshly built shared module directly into the user
engine directory. This avoids `sudo` and does not require reconfiguring the
build with a custom prefix.

```sh
mkdir -p ~/.local/share/typio/engines
cp build/libtypio_engine_rime.so ~/.local/share/typio/engines/
```

Typio hosts scan `~/.local/share/typio/engines/` at runtime. After copying
the file, restart Typio (or the Typio-based application you are testing) so
it rescans the directory and loads the new plugin.

### Rime configuration files

The engine looks for Rime data in two places:

- **Shared data** — system Rime schemas, usually `/usr/share/rime-data`
  (provided by the `rime-data` package).
- **User data** — your personal overrides, defaulting to
  `~/.local/share/typio/rime/` (a `rime/` subdirectory inside Typio’s data
  directory).

Place your `default.custom.yaml` and schema-specific `.custom.yaml` files in
the user data directory, then trigger a deploy so Rime rebuilds the `build/`
output:

```sh
mkdir -p ~/.local/share/typio/rime
# edit ~/.local/share/typio/rime/default.custom.yaml
# then restart Typio or invoke the engine's "deploy" command
```

See the [Rime Engine Configuration reference](../../reference/rime-config.md)
for all available config keys and their defaults.

## Step 4 — Verify the installation

If you built with tests enabled, run them to confirm everything is working:

```sh
meson test -C build --print-errorlogs
```

You should see all unit tests pass. The integration tests require the system
`rime-data` package; if they are skipped in your environment, that is normal
and the basic unit tests are sufficient to confirm the build is healthy.

## Next steps

- Read the [developer setup guide](../dev/setup.md) for day-to-day development
  workflows such as running a subset of tests or rebuilding after edits.

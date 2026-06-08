# How to Package for Distribution

Use this guide when preparing the Rime engine for distribution in a Linux
package (e.g. `.rpm`, `.deb`, PKGBUILD, or ebuild).

## Build dependencies

| Dependency | Minimum version | Notes |
|------------|-----------------|-------|
| `meson` | 1.0.0 | Build system |
| `ninja` | — | Meson backend |
| `pkg-config` | — | Dependency discovery |
| `typio-engine-abi` / `libtypio` | — | Engine ABI headers and link symbols |
| `librime` | 1.16.1 | Rime input method library |
| `libcurl` | — | Engine setup command downloads Rime schema bundles |
| `gcc` or `clang` | C23 support | Build compiler |

The engine uses `libtypio` as a meson subproject fallback if `typio-engine-abi`
is not installed system-wide.

## Build

```bash
meson setup build --prefix=/usr
ninja -C build
```

To run tests as well:

```bash
meson setup build --prefix=/usr -Dbuild_tests=true
ninja -C build test
```

## Install

```bash
meson install -C build --destdir "$pkgdir"
```

Always fail the packaging step if the staging directory is empty or the expected
worker and manifest are missing:

```bash
test -x "$pkgdir/usr/libexec/typio/engines/typio-engine-rime"
test -f "$pkgdir/usr/share/typio/engines/typio-engine-rime.toml"
test -f "$pkgdir/usr/share/icons/hicolor/scalable/apps/typio-rime-symbolic.svg"
test -f "$pkgdir/usr/share/icons/hicolor/scalable/apps/typio-rime-latin-symbolic.svg"
```

If the target distribution uses a non-default `libexecdir`, check that
directory instead of `/usr/libexec`.

## Generic Package Pipeline

Use the Meson install plan as the source of truth. The package manager should
provide a non-empty staging directory and fail if no files are staged.

```toml
build_deps = [
  "meson",
  "ninja",
  "pkg-config",
  "cc",
]

link_deps = [
  "libtypio",
  "librime",
  "libcurl",
]

[pipeline.configure]
script = '''
cd source
meson setup build --prefix=/usr --buildtype=release
'''

[pipeline.compile]
script = '''
cd source
ninja -C build
'''

[pipeline.staging]
script = '''
: "${STAGING_DIR:?STAGING_DIR is required}"
cd source
meson install -C build --destdir "$STAGING_DIR"
test -x "$STAGING_DIR/usr/libexec/typio/engines/typio-engine-rime"
test -f "$STAGING_DIR/usr/share/typio/engines/typio-engine-rime.toml"
test -f "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/typio-rime-symbolic.svg"
test -f "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/typio-rime-latin-symbolic.svg"
'''
```

Map `cc`, `libcurl`, and `rime-data` to the distribution's package names. For
example, some distributions package the runtime library as `curl` and the
development metadata as `libcurl-devel` or `libcurl4-openssl-dev`.

## Installed artifacts

| Source | Destination | Description |
|--------|-------------|-------------|
| `typio-engine-rime` | `<libexecdir>/typio/engines/` | Private engine executable started by the host |
| `typio-engine-rime.toml` | `<datadir>/typio/engines/` | Engine manifest discovered by the host at runtime |
| `data/icons/hicolor/scalable/apps/typio-rime-symbolic.svg` | `<datadir>/icons/hicolor/scalable/apps/` | Symbolic icon for Chinese input mode |
| `data/icons/hicolor/scalable/apps/typio-rime-latin-symbolic.svg` | `<datadir>/icons/hicolor/scalable/apps/` | Symbolic icon for ASCII (Latin) input mode |

Default paths (with `--prefix=/usr`):

- `libexecdir` → `/usr/libexec`
- `datadir` → `/usr/share`

## Runtime dependencies

| Package | Reason |
|---------|--------|
| `librime` | Engine runtime library |
| `libtypio` | Host framework that registers and calls the engine worker |
| `libcurl` | Runtime library for the `setup` command |
| Rime schema data (`rime-data` or similar) | Shared dictionaries and schemas for `shared_data_dir` |

The engine does **not** ship Rime schema data. Packagers should ensure the
system `rime-data` package is installed or document that users must provide
their own schemas under `shared_data_dir` (default `/usr/share/rime-data`).

## Packaging checklist

- [ ] Worker installs to `<libexecdir>/typio/engines/typio-engine-rime`
- [ ] Manifest installs to `<datadir>/typio/engines/typio-engine-rime.toml`
- [ ] Icons install to `<datadir>/icons/hicolor/scalable/apps/`
- [ ] `typio-engine-abi` or `libtypio` is listed as a build dependency
- [ ] `librime` is listed as both build and runtime dependency
- [ ] Rime schema data dependency is documented for end users
- [ ] The engine does **not** require a system-wide `data/` directory — all bundled resources are icons only

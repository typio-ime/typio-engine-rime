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
DESTDIR="$pkgdir" ninja -C build install
```

## Installed artifacts

| Source | Destination | Description |
|--------|-------------|-------------|
| `libtypio_engine_rime.so` | `<libdir>/typio/engines/` | Engine plugin discovered by the host at runtime |
| `data/icons/hicolor/scalable/apps/typio-rime-symbolic.svg` | `<datadir>/icons/hicolor/scalable/apps/` | Symbolic icon for Chinese input mode |
| `data/icons/hicolor/scalable/apps/typio-rime-latin-symbolic.svg` | `<datadir>/icons/hicolor/scalable/apps/` | Symbolic icon for ASCII (Latin) input mode |

Default paths (with `--prefix=/usr`):

- `libdir` → `/usr/lib` or `/usr/lib64`
- `datadir` → `/usr/share`

## Runtime dependencies

| Package | Reason |
|---------|--------|
| `librime` | Engine runtime library |
| `libtypio` | Host framework that loads the engine plugin |
| Rime schema data (`rime-data` or similar) | Shared dictionaries and schemas for `shared_data_dir` |

The engine does **not** ship Rime schema data. Packagers should ensure the
system `rime-data` package is installed or document that users must provide
their own schemas under `shared_data_dir` (default `/usr/share/rime-data`).

## Packaging checklist

- [ ] Engine installs to `<libdir>/typio/engines/libtypio_engine_rime.so`
- [ ] Icons install to `<datadir>/icons/hicolor/scalable/apps/`
- [ ] `typio-engine-abi` or `libtypio` is listed as a build dependency
- [ ] `librime` is listed as both build and runtime dependency
- [ ] Rime schema data dependency is documented for end users
- [ ] The engine does **not** require a system-wide `data/` directory — all bundled resources are icons only

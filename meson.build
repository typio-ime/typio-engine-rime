project('typio-engine-rime', 'c',
    version: '0.0.2',
    default_options: [
        'c_std=c23',
        'warning_level=2',
        'werror=false',
    ],
    meson_version: '>=1.0.0',
)

cc = meson.get_compiler('c')

add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')

# ── Dependencies ──────────────────────────────────────────────────────────
# The engine ABI: headers + libtypio-core to link the engine-facing symbols.
typio_abi_dep = dependency('typio-engine-abi',
    fallback: ['libtypio', 'libtypio_engine_abi_dep'])
rime_dep = dependency('rime', required: true)
curl_dep = dependency('libcurl', required: true)

# Where to install the plugin so a typio host discovers it. The host scans
# <libdir>/typio/engines for libtypio_engine_*.so.
enginedir = get_option('libdir') / 'typio' / 'engines'

subdir('src')

engine_c_args = ['-DTYPIO_ENGINE_RIME_VERSION="' + meson.project_version() + '"']

rime_engine = shared_module('typio_engine_rime',
    rime_sources,
    dependencies: [typio_abi_dep, rime_dep, curl_dep],
    include_directories: include_directories('src'),
    c_args: engine_c_args,
    install: true,
    install_dir: enginedir,
    # Plugins are loaded from a directory next to the typio libs; let the
    # loader resolve libtypio-core via the standard search path.
    name_prefix: 'lib',
)

# ── Icons (freedesktop/XDG icon-theme layout) ──────────────────────────────
# Installed under the system hicolor theme so panels resolve them through
# the standard XDG icon lookup path rather than a bundled engine directory.
install_subdir('data/icons',
    install_dir: get_option('datadir'),
)

if get_option('build_tests')
    subdir('tests')
endif

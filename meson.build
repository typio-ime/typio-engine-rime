project('typio-engine-rime', 'c',
    version: '0.3.2',
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

# Worker executables are private helpers; manifests are runtime metadata.
worker_install_dir = get_option('libexecdir') / 'typio' / 'engines'
manifest_install_dir = get_option('datadir') / 'typio' / 'engines'

subdir('src')

engine_c_args = ['-DTYPIO_ENGINE_RIME_VERSION="' + meson.project_version() + '"']

rime_worker = executable('typio-engine-rime',
    rime_sources, files('src/worker_main.c'),
    dependencies: [typio_abi_dep, rime_dep, curl_dep],
    include_directories: include_directories('src'),
    c_args: engine_c_args,
    install: true,
    install_dir: worker_install_dir,
)

dev_manifest_conf = configuration_data()
dev_manifest_conf.set('ENGINE_COMMAND', './typio-engine-rime')
configure_file(
    input: 'typio-engine-rime.toml.in',
    output: 'typio-engine-rime.toml',
    configuration: dev_manifest_conf,
)

install_manifest_conf = configuration_data()
install_manifest_conf.set(
    'ENGINE_COMMAND',
    get_option('prefix') / worker_install_dir / 'typio-engine-rime',
)
installed_manifest = configure_file(
    input: 'typio-engine-rime.toml.in',
    output: 'typio-engine-rime.installed.toml',
    configuration: install_manifest_conf,
)
install_data(installed_manifest,
    install_dir: manifest_install_dir,
    rename: 'typio-engine-rime.toml',
)

# ── Icons (freedesktop/XDG icon-theme layout) ──────────────────────────────
# Installed under the hicolor theme so panels resolve them through the
# standard XDG icon lookup path regardless of prefix (~/.local, /usr/local,
# /usr, etc.).
install_subdir('data/icons/hicolor',
    install_dir: get_option('datadir') / 'icons',
)

if get_option('build_tests')
    subdir('tests')
endif

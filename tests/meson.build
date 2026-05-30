if get_option('buildtype') in ['release', 'minsize']
    libtypio_libdir = meson.project_source_root() / '..' / 'libtypio' / 'target' / 'release'
else
    libtypio_libdir = meson.project_source_root() / '..' / 'libtypio' / 'target' / 'debug'
endif
libtypio_dep = dependency('libtypio',
    fallback: ['libtypio', 'libtypio_dep'])

test_env = environment()
test_env.prepend('LD_LIBRARY_PATH', libtypio_libdir)

test_rime_path_expand = executable('test_rime_path_expand',
    ['test_rime_path_expand.c', '../src/path_expand.c'],
    dependencies: libtypio_dep,
    include_directories: include_directories('../src'),
)
test('rime_path_expand', test_rime_path_expand, env: test_env)

test_rime_engine = executable('test_rime_engine', 'test_rime_engine.c',
    dependencies: libtypio_dep,
    c_args: ['-DTYPIO_TEST_RIME_ENGINE_DIR="' + meson.current_build_dir() + '/.."'],
)
test('rime_engine', test_rime_engine,
    depends: rime_engine,
    timeout: 120,
    env: test_env,
    suite: 'integration',
)

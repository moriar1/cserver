project('cserver', 'c', version: '0.1', default_options: ['warning_level=3'])
src = ['src/cserver.c', 'src/threadpool.c']
incdir = include_directories('include')

add_project_arguments(
    [
        '-fstrict-flex-arrays=3',
        '-fcf-protection',
        '-fstack-clash-protection',
        '-fstack-protector-strong',
        '-fPIE',
        '-fno-common',
        # '-Weverything',
        '-Wundef',
        '-Wvla',
        '-Wshift-count-overflow',
        '-Wshadow',
        '-Wconversion',
        '-Wsign-conversion',
        '-Wcast-qual',
        '-Wstrict-prototypes',
        '-Wmissing-prototypes',
        '-Wold-style-definition',
        '-Wredundant-decls',
        '-Wformat=2',
        '-Werror=format-security',
        '-Werror=incompatible-pointer-types',
    ],
    language: 'c',
)

if host_machine.system() == 'linux'
    add_project_arguments(['-D_POSIX_C_SOURCE=200809L'], language: 'c')
endif

thread_dep = dependency('threads')

exe = executable(
    'cserver',
    src,
    dependencies: thread_dep,
    include_directories: incdir,
    install: true,
)
test('basic', exe)

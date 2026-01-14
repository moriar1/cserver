project(
    'cserver',
    'c',
    version: '0.1',
    default_options: [
        'warning_level=3',
        'b_pie=true',
        'c_std=c17',
        'b_ndebug=if-release',
        # 'b_lto=true',
    ],
)
src = ['src/cserver.c', 'src/threadpool.c']
incdir = include_directories('include')

# ---- Common Compiler Flags ----

hardening_flags = [
    '-fcf-protection=full',
    '-fstack-clash-protection',
    '-fstack-protector-strong',
    # '-fstrict-flex-arrays=3',
    # '-U_FORTIFY_SOURCE',
    # '-D_FORTIFY_SOURCE=3',
]

warnings_flags = [
    '-Wundef',
    '-Wvla',
    '-Wshadow',
    '-Wconversion',
    '-Wcast-qual',
    '-Wmissing-prototypes',
    '-Wformat=2',
    '-Wwrite-strings',
    '-Wstack-protector',
    '-Walloca',
    '-Wimplicit-fallthrough',
    '-Wstrict-overflow=5',
    '-Werror=format-security',
    '-Werror=incompatible-pointer-types',
]

link_flags = [
    '-Wl,-z,relro',
    '-Wl,-z,now',
    '-Wl,-z,defs',
    '-Wl,-z,noexecstack',
    # '-Wl,--no-copy-dt-needed-entries',
    # '-Wl,-z,nodlopen',
]

buildtype = get_option('buildtype')
build_type_flags = []
build_type_link_flags = []

if buildtype == 'debug'
    build_type_flags += ['-DDEBUG', '-ftrapv']    # ,'-g3',  # '-fsanitize=float-divide-by-zero', # 'fno-omit-frame-pointer', #TODO: check with/without sans
elif buildtype == 'debugoptimized'
    build_type_flags += ['-ffunction-sections', '-fdata-sections']  #,'-g3', '-Og', '-march=native', '-mtune=native'
    build_type_link_flags += ['-Wl,--gc-sections']    #'-flto'
elif buildtype == 'release'
    build_type_flags += ['-ffunction-sections', '-fdata-sections']  #, '-march=native', '-mtune=native', #, '-march=x86-64-v3'
    build_type_link_flags += ['-Wl,--gc-sections']    #'-flto'
endif

# ---- Compiler Specific Flags ----

cc_id = meson.get_compiler('c').get_id()

if cc_id == 'clang'
    warnings_flags += [
        '-Wunreachable-code-aggressive',
        '-Wshorten-64-to-32',
        '-Wassign-enum',
        '-Warray-bounds-pointer-arithmetic',
        '-Wtautological-constant-in-range-compare',
        '-Wthread-safety',
        '-Wthread-safety-beta',
        '-Weverything',
        '-Wno-cast-align',
        '-Wno-padded',
        '-Wno-unsafe-buffer-usage',
        '-Wno-declaration-after-statement',
        '-Wno-conditional-uninitialized',
        '-Wno-covered-switch-default',
        '-Wno-disabled-macro-expansion',
    ]
    if buildtype == 'release'
        build_type_flags += ['-mretpoline']
    endif
elif cc_id == 'gcc'
    warnings_flags += [
        '-Wstringop-overflow=4',
        '-Wduplicated-cond',
        '-Wduplicated-branches',
        '-Wlogical-op',
    ]
    if buildtype == 'release'
        build_type_flags += ['-fipa-pta', '-fbranch-target-load-optimize']
    endif
endif

add_project_arguments(
    hardening_flags + warnings_flags + build_type_flags,
    language: 'c',
)
add_project_link_arguments(link_flags + build_type_link_flags, language: 'c')

# ---- Misc ----

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

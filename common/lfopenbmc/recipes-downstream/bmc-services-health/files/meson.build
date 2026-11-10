project(
    'bmc-services-health',
    'cpp',
    version: '0.1',
    default_options: [
        'buildtype=debugoptimized',
        'cpp_std=c++23',
        'warning_level=3',
        'werror=true',
    ],
    meson_version: '>=1.1.1',
)

lib = static_library('bmc-services-health-lib', ['services_health.cpp'])
lib_dep = declare_dependency(
    link_with: lib,
    include_directories: include_directories('.'),
)

if get_option('tests').allowed()
    subdir('test')
endif

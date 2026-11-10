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

phosphor_logging_dep = dependency('phosphor-logging')

lib = static_library(
    'bmc-services-health-lib',
    ['services_health.cpp', 'checks.cpp', 'metric_writer.cpp'],
    dependencies: [phosphor_logging_dep],
)
lib_dep = declare_dependency(
    link_with: lib,
    include_directories: include_directories('.'),
    dependencies: [phosphor_logging_dep],
)

sdbusplus_dep = dependency('sdbusplus')
systemd_dep = dependency('libsystemd')

executable(
    'bmc-services-health',
    ['main.cpp', 'systemd_dbus.cpp'],
    dependencies: [lib_dep, sdbusplus_dep, systemd_dep],
    install: true,
    install_dir: get_option('bindir'),
)

if get_option('tests').allowed()
    subdir('test')
endif

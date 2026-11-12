project(
    'redfish-client',
    'cpp',
    version: '1.0',
    default_options: ['cpp_std=c++23'],
    meson_version: '>=1.1.1',
)

phosphor_logging_dep = dependency('phosphor-logging')
phosphor_dbus_interfaces_dep = dependency('phosphor-dbus-interfaces')
sdbusplus_dep = dependency('sdbusplus')
nlohmann_json_dep = dependency('nlohmann_json', include_type: 'system')
curl_dep = dependency('libcurl')
boost_dep = dependency('boost', required: true)
dl_dep = dependency('dl')
CLI11_dep = dependency('CLI11', include_type: 'system')

redfish_schema_dep = dependency('redfish-schema-pack', native: true)
dsp_spec_jsondir = ''.join(
    [
        meson.get_external_property('sys_root', '', native: true),
        '/',
        redfish_schema_dep.get_variable('schema_dir'),
    ],
) / 'json-schema'

subdir('redfish-binding')

subdir('configurations')

packagedir = join_paths(
    get_option('prefix'),
    get_option('datadir'),
    meson.project_name(),
)

foreach conf : configurations
    install_data(
        join_paths('configurations', conf),
        install_dir: join_paths(packagedir, 'configurations'),
    )
endforeach

base_deps = [
    CLI11_dep,
    boost_dep,
    curl_dep,
    dl_dep,
    nlohmann_json_dep,
    phosphor_logging_dep,
    phosphor_dbus_interfaces_dep,
    redfish_binding_dep,
    sdbusplus_dep,
]

cpp_args = ['-DBOOST_STACKTRACE_USE_ADDR2LINE', '-ggdb', '-fPIC']

link_args = ['-rdynamic']

srcs = [
    'src/async_http_client.cpp',
    'src/config.cpp',
    'src/persist_map.cpp',
    'src/sensor.cpp',
    'src/sensor_handler.cpp',
    'src/log_service_handler.cpp',
    'src/update_service_handler.cpp',
    'src/redfish_client.cpp',
    'src/log_entry_mapper_registry.cpp',
    'src/log_entry_mapper_utils.cpp',
    'src/cper_mapper.cpp',
    'src/instinct_cper_mapper.cpp',
    'src/instinct_amc_mapper.cpp',
    'src/instinct_gpu_mapper.cpp',
    'src/instinct_thermal_mapper.cpp',
    'src/instinct_power_supply_mapper.cpp',
    'src/hgx_ps_run_pwr_fault_mapper.cpp',
    'src/unhandled_mapper.cpp',
    'src/hgx_thermal_mapper.cpp',
    'src/hgx_leak_detector_mapper.cpp',
    'src/sensor_threshold_mapper.cpp',
    'src/openbmc_mapper.cpp',
    'src/update_mapper.cpp',
    'src/environmental_mapper.cpp',
]

root_inc = include_directories('include')

redfish_client_library = static_library(
    'redfish-client-lib',
    srcs,
    cpp_args: cpp_args,
    link_args: link_args,
    dependencies: base_deps,
    include_directories: root_inc,
)

install_subdir(
    'include/redfish_client',
    install_dir: get_option('includedir'),
    strip_directory: false,
)

executable(
    'redfish-client',
    'src/main.cpp',
    dependencies: base_deps,
    include_directories: root_inc,
    cpp_args: cpp_args,
    link_args: link_args,
    link_with: redfish_client_library,
    install: true,
)

if get_option('tests').allowed()
    subdir('test')
endif

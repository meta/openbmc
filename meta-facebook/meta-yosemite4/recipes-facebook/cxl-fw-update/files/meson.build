project(
  'cxl-fw-update',
  'cpp',
  version: '0.1',
  meson_version: '>=0.57.0',
  license: 'Apache-2.0',
  default_options: [
    'werror=true',
    'warning_level=3',
    'cpp_std=c++23',
  ])


src = [
  'cxl-update.cpp',
  'cci-mctp-update.cpp',
]

executable(
  'cxl-fw-update',
  src,
  dependencies: [dependency('sdbusplus')],
  install: true,
)

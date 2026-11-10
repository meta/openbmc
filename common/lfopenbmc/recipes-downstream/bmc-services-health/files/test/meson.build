gtest_dep = dependency('gtest', main: true, disabler: true, required: false)
if not gtest_dep.found()
    assert(
        not get_option('tests').enabled(),
        'Googletest is required if tests are enabled',
    )
endif

# ptest-meson installs test/test-* (classes/ptest-meson.bbclass)
foreach name : ['services_health']
    test(
        name,
        executable(
            'test-' + name,
            name + '_test.cpp',
            dependencies: [gtest_dep, lib_dep],
        ),
        env: ['LG2_FORCE_STDERR=1'],
    )
endforeach

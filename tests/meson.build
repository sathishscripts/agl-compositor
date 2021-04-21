dep_pixman = dependency('pixman-1', version: '>= 0.25.2')

lib_test_runner = static_library(
	'test-runner',
	'weston-test-runner.c',
	dependencies: [
		dep_wayland_client,
		libweston_dep,
	],
	include_directories: common_inc,
	install: false,
)

dep_test_runner = declare_dependency(
	dependencies: dep_wayland_client,
	link_with: lib_test_runner
)

lib_test_client = static_library(
	'test-client',
	[
		'weston-test-client-helper.c',
		'weston-test-fixture-compositor.c',
		'../shared/os-compatibility.c',
		'../shared/xalloc.c',
		weston_test_client_protocol_h,
		weston_test_protocol_c,
		viewporter_client_protocol_h,
		viewporter_protocol_c,
		agl_shell_client_protocol_h,
		agl_shell_protocol_c,
		xdg_shell_client_protocol_h,
		xdg_shell_protocol_c,
	],
	include_directories: common_inc,
	dependencies: [
		dep_wayland_client,
		deps_libweston,
		dep_libexec_compositor,
		dep_pixman,
		dependency('cairo'),
	],
	install: false,
)
dep_test_client = declare_dependency(
	link_with: lib_test_client,
	sources: [
		viewporter_client_protocol_h,
	],
	dependencies: [
		dep_wayland_client,
		dep_test_runner,
		dep_pixman,
		dependency('libudev', version: '>= 136'),
	]
)

tests = [
    { 'name': 'agl-client-shell' },
]

env_modmap = ''
test_config_h = configuration_data()
test_config_h.set_quoted('WESTON_TEST_REFERENCE_PATH', meson.current_source_dir() + '/reference')
test_config_h.set_quoted('WESTON_MODULE_MAP', env_modmap)
test_config_h.set_quoted('WESTON_DATA_DIR', join_paths(meson.current_source_dir(), '..', 'data'))
message('Test suite plug-in path is ' + libweston_dep.get_pkgconfig_variable('libdir'))
test_config_h.set_quoted('TESTSUITE_PLUGIN_PATH', libweston_dep.get_pkgconfig_variable('libdir') + '/' + 'test-plugin.so')
configure_file(output: 'test-config.h', configuration: test_config_h)

foreach t : tests
	t_name = 'test-' + t.get('name')
	t_sources = t.get('sources', [t.get('name') + '-test.c'])
	t_sources += weston_test_client_protocol_h

	t_deps = [ dep_test_client, libweston_dep ]
	t_deps += t.get('dep_objs', [])

	t_exe = executable(
		t_name,
		t_sources,
		c_args: [
			'-DUNIT_TEST',
			'-DTHIS_TEST_NAME="' + t_name + '"',
		],
		build_by_default: true,
		include_directories: common_inc,
		dependencies: t_deps,
		install: false,
	)

	test(t.get('name'), t_exe, depends: t.get('test_deps', []))
endforeach

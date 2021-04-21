dep_test_client = dependency('libweston-test-10')
dep_test_client_runner = dependency('libweston-test-runner-10')

# FIXME: extra depends should be actually be added here
tests = [
    { 'name': 'agl-client-shell' },
]

foreach t : tests
	t_name = 'test-' + t.get('name')
	t_sources = t.get('sources', [t.get('name') + '-test.c'])
	t_sources += [ agl_shell_client_protocol_h, agl_shell_protocol_c,
		       xdg_shell_client_protocol_h, xdg_shell_protocol_c,
		       agl_screenshooter_client_protocol_h,
		       agl_screenshooter_protocol_c ]

	t_deps = [ dep_test_client, dep_test_client_runner,
		    libweston_dep, dep_libexec_compositor ]
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

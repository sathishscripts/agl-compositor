project('agl-compositor',
  'c',
  version: '0.0.13',
  default_options: [
    'warning_level=3',
    'c_std=gnu99',
  ],
  meson_version: '>= 0.47',
  license: 'MIT/Expat',
)

config_h = configuration_data()

cc = meson.get_compiler('c')
add_project_arguments(
  cc.get_supported_arguments([
    '-Wno-unused-parameter',
    '-Wno-pedantic',
    '-Wextra',
    '-Werror'
  ]),
  language: 'c'
)

add_project_arguments([
    '-DPACKAGE_STRING="agl-compositor @0@"'.format(meson.project_version()),
    '-D_GNU_SOURCE',
    '-D_ALL_SOURCE',
  ],
  language: 'c'
)

optional_libc_funcs = [ 'memfd_create', 'strchrnul' ]
foreach func: optional_libc_funcs
    if cc.has_function(func)
        add_project_arguments('-DHAVE_@0@=1'.format(func.to_upper()), language: 'c')
    endif
endforeach

dep_libsystemd = dependency('libsystemd', required: false)
dep_scanner = dependency('wayland-scanner', native: true)
prog_scanner = find_program(dep_scanner.get_pkgconfig_variable('wayland_scanner'))
dep_wp = dependency('wayland-protocols', version: '>= 1.18')
dir_wp_base = dep_wp.get_pkgconfig_variable('pkgdatadir')

agl_shell_xml = files('protocol/agl-shell.xml')
xdg_shell_xml = join_paths(dir_wp_base, 'stable', 'xdg-shell', 'xdg-shell.xml')

protocols = [
  { 'name': 'agl-shell', 'source': 'internal' },
  { 'name': 'xdg-shell', 'source': 'wp-stable' },
]

foreach proto: protocols
    proto_name = proto['name']
    if proto['source'] == 'internal'
        base_file = proto_name
	xml_path = join_paths('protocol', '@0@.xml'.format(base_file))
    elif proto['source'] == 'wp-stable'
        base_file = proto_name
	xml_path = join_paths(dir_wp_base, 'stable', proto_name, '@0@.xml'.format(base_file))
    else
        base_file = '@0@-unstable-@1@'.format(proto_name, proto['version'])
	xml_path = join_paths(dir_wp_base, 'unstable', proto_name, '@0@.xml'.format(base_file))
    endif

    foreach output_type: [ 'client-header', 'server-header', 'private-code' ]
	if output_type == 'client-header'
	    output_file = '@0@-client-protocol.h'.format(base_file)
	elif output_type == 'server-header'
	    output_file = '@0@-server-protocol.h'.format(base_file)
	else
	    output_file = '@0@-protocol.c'.format(base_file)
	    if dep_scanner.version().version_compare('< 1.14.91')
	        output_type = 'code'
	    endif
	endif

	var_name = output_file.underscorify()
	target = custom_target(
	    '@0@ @1@'.format(base_file, output_type),
	    command: [ prog_scanner, output_type, '@INPUT@', '@OUTPUT@' ],
	    input: xml_path,
	    output: output_file,
	)

        set_variable(var_name, target)
    endforeach
endforeach

# libweston-6 pkg-config file already has 'libweston-6' as prefix but
# agl-compositor uses 'libweston-6' also. This makes use of the prefix
# path as to allow building and installing the compositor locally
prefix_path = get_option('prefix')
message('prefix_path ' + prefix_path)
if not prefix_path.contains('/usr')
  additional_include_dir = include_directories(prefix_path + '/' + 'include')
  local_dep = declare_dependency(include_directories: additional_include_dir)
else
  local_dep = []
endif

deps_libweston = [
  dependency('wayland-server'),
  dependency('libweston-7'),
  dependency('libweston-desktop-7'),
  local_dep,
]

srcs_agl_compositor = [
	'src/main.c',
	'src/desktop.c',
	'src/layout.c',
	'src/shell.c',
	'shared/option-parser.c',
	'shared/os-compatibility.c',
	agl_shell_server_protocol_h,
	agl_shell_protocol_c,
	xdg_shell_protocol_c,
]

if dep_libsystemd.found()
  config_h.set('HAVE_SYSTEMD', 1)

  srcs_agl_compositor += 'src/systemd-notify.c'
  deps_libweston += dep_libsystemd

  message('Found systemd, enabling notify support')
endif

configure_file(output: 'config.h', configuration: config_h)

exe_agl_compositor = executable(
	'agl-compositor',
	srcs_agl_compositor,
	dependencies: deps_libweston,
	install: true
)

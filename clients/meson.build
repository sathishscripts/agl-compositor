dep_wayland_client = dependency('wayland-client', version: '>= 1.17.0')

grpcpp_reflection_dep = cxx.find_library('grpc++_reflection')
protoc = find_program('protoc')
grpc_cpp = find_program('grpc_cpp_plugin')

protoc_gen = generator(protoc, \
                       output : ['@BASENAME@.pb.cc', '@BASENAME@.pb.h'],
                       arguments : ['--proto_path=@CURRENT_SOURCE_DIR@/../protocol',
                         '--cpp_out=@BUILD_DIR@',
                         '@INPUT@'])

generated_protoc_sources = protoc_gen.process('../protocol/agl_shell.proto')

grpc_gen = generator(protoc, \
                     output : ['@BASENAME@.grpc.pb.cc', '@BASENAME@.grpc.pb.h'],
                     arguments : ['--proto_path=@CURRENT_SOURCE_DIR@/../protocol',
                       '--grpc_out=@BUILD_DIR@',
                       '--plugin=protoc-gen-grpc=' + grpc_cpp.path(),
                       '@INPUT@'])
generated_grpc_sources = grpc_gen.process('../protocol/agl_shell.proto')

grpc_deps = [
    dependency('protobuf'),
    dependency('grpc'),
    dependency('grpc++'),
    grpcpp_reflection_dep,
]

clients = [
{
	'basename': 'agl-screenshooter',
	'sources': [
	  'screenshooter.c',
	  '../shared/file-util.c',
	  '../shared/os-compatibility.c',
	  '../shared/xalloc.c',
	  agl_screenshooter_client_protocol_h,
	  agl_screenshooter_protocol_c,
	  xdg_output_unstable_v1_client_protocol_h,
	  xdg_output_unstable_v1_protocol_c,
	],
	'deps_objs' : [ dep_wayland_client ],
	'deps': [ 'cairo' ],
},
{
    'basename': 'agl-shell-grpc-server',
    'sources': [
      'main-grpc.cpp',
      'grpc-async-cb.cpp',
      'shell.cpp',
      generated_protoc_sources,
      generated_grpc_sources,
      agl_shell_client_protocol_h,
      agl_shell_protocol_c,
    ],
    'deps_objs' : [ grpc_deps, dep_wayland_client ],
    'deps' : [],
},
]

foreach t: clients
  t_name = t.get('basename')
  t_deps = t.get('deps_objs', [])

  foreach xdep: t.get('deps', [])
    dep = dependency(xdep, required: false)
    if dep.found()
      t_deps += dep
    endif
  endforeach

  if t_name == 'agl-shell-grpc-server'
    executable(
        t_name, t.get('sources'),
        include_directories: [ common_inc ],
        dependencies: [ t_deps, libweston_dep ],
        install: true,
        install_dir: dir_module_agl_compositor,
    )
  else
    executable(
        t_name, t.get('sources'),
        include_directories: [ common_inc ],
        dependencies: [ t_deps, libweston_dep ],
        install: true,
    )
  endif

  message('Building client ' + t_name)
endforeach

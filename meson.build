project('task-manager', 'cpp', version: '0.1.0',
  default_options: ['cpp_std=c++17', 'warning_level=2', 'default_library=static'])

sdk_sp = subproject('satellite-plugin-sdk')
satellite_common_dep = sdk_sp.get_variable('satellite_common_dep')

nlohmann_json_sp = subproject('nlohmann_json')
json_dep = nlohmann_json_sp.get_variable('nlohmann_json_dep')

zmq_opt = get_option('zmq')
zmq_dep = disabler()
if not zmq_opt.disabled()
  libzmq_sp = subproject('libzmq', required: zmq_opt.enabled())
  zmq_dep = libzmq_sp.get_variable('libzmq_dep')
endif

inc = include_directories('include')

taskmgr_lib = static_library(
  'taskmgr',
  files(
    'src/task-manager/plugin_registry.cpp',
    'src/task-manager/plugin_resolve.cpp',
    'src/task-manager/task_manager.cpp',
    'src/task-manager/config_resolver.cpp',
  ),
  include_directories: inc,
  dependencies: [satellite_common_dep, json_dep],
)

executable('task-manager', 'src/task-manager/main.cpp',
  include_directories: inc, link_with: taskmgr_lib,
  dependencies: zmq_dep.found() ? [satellite_common_dep, json_dep, zmq_dep] : [satellite_common_dep, json_dep],
  cpp_args: zmq_dep.found() ? ['-DMP_HAVE_ZMQ'] : [],
  install: true)

executable('task-client', 'src/task-manager/client.cpp',
  include_directories: inc, link_with: taskmgr_lib,
  dependencies: [satellite_common_dep, json_dep], install: true)

task_zmq_cpp_args = zmq_dep.found() ? ['-DMP_HAVE_ZMQ'] : []
executable('task-zmq-client', 'src/task-manager/zmq_client.cpp',
  include_directories: inc, link_with: taskmgr_lib,
  dependencies: zmq_dep.found() ? [satellite_common_dep, json_dep, zmq_dep] : [satellite_common_dep, json_dep],
  cpp_args: task_zmq_cpp_args, install: true)

project('fastdyn', 'c')
cc = meson.get_compiler('c')

python3 = import('python').find_installation('python3')
python_dep = python3.dependency()

python_versions = ['python3.10', 'python3.11', 'python3.12', 'python3.13', 'python3.14', 'python3.15']
libpython = dependency('', required: false)

foreach v : python_versions
  libpython = cc.find_library(v, required: false)
  if libpython.found()
    break
  endif
endforeach

if not libpython.found()
  error('Required Python library not found (3.10 - 3.15)')
endif

glib = dependency('glib-2.0', version: '>=2.56.0', required: true)
deps = [glib, libpython, python_dep]

# USER RELATED FLAGS. USER CAN ADD CUSTOM LIBS
ENABLE_LIBGZ = get_option('enable_libgz')
if ENABLE_LIBGZ
  libgz_wrapper_dep = declare_dependency(
    include_directories: include_directories('../PRehost/gazebo/fastdyn'),
    link_args: ['-L../PRehost/gazebo/fastdyn/build/lib', '-lgz_wrapper'],
  )
  deps += libgz_wrapper_dep
endif

ENABLE_LIBHW = get_option('enable_libhw')
if ENABLE_LIBHW
libhw_dep = declare_dependency(
  include_directories: include_directories('/root/rooney/libhw/include'),
  link_args: ['-L/root/rooney/libhw/out', '-lhw', '-lstlink']
)
  deps +=libhw_dep
endif

#Based on the configurations, generate config.h
configure_file(
  input : 'config.h.in',
  output : 'config.h',
  configuration : {
    'ENABLE_LIBGZ' : ENABLE_LIBGZ,
    'ENABLE_LIBHW' : ENABLE_LIBHW,
  }
)
#config_inc = include_directories(meson.current_build_dir())


QEMU_DIR = get_option('qemu_path')
qemu_include = include_directories(
  join_paths(QEMU_DIR, 'include', 'qemu'),
  join_paths(QEMU_DIR, '..', 'qemu', 'include')
  
)

# --- Collect all source files
srcs = files(
  'core/core.c',
  'python/python.c',
  'utils/utils.c',
  'virtuals/virtuals.c',
)

if get_option('device_models')
  srcs += files(
  'device_models/dev.c',
  'device_models/passthrough/passthrough.c'
  )
endif

# --- Project include dir
local_include = include_directories('include')


fastdyn_target = shared_module(
  'fastdyn',
  srcs,
  include_directories: [qemu_include, local_include],
  dependencies: deps,
  install: false
)

project('fastdyn', 'c')
cc = meson.get_compiler('c')

# suppress MAVLink packed warnings
add_project_arguments('-Wno-address-of-packed-member', language : 'c')

#glib support
glib = dependency('glib-2.0', version: '>=2.56.0', required: true)
cjson_dep = dependency('libcjson', required: true)
deps = [glib, cjson_dep]
srcs = []

#fuzzer support
libfuzz_wrapper_dep = declare_dependency(
  link_args: ['-L' + meson.current_source_dir() + '/fuzzer/fastdyn_fuzz_lib/target/release', '-lfastdyn_fuzzer']
)
deps += libfuzz_wrapper_dep

#debugging FLAGS
DEBUG_PRINT = get_option('DEBUG_PRINT')

#PYTHON Support
ENABLE_LIBPY = get_option('enable_libpy')
if ENABLE_LIBPY
  python3 = import('python').find_installation('python3')
  python_dep = python3.dependency()

  python_versions = ['python3.10', 'python3.11', 'python3.12', 'python3.13', 'python3.14', 'python3.15']
  libpython = dependency('', required: false)

  srcs += files('device_models/halucinator/halucinator.c')

  foreach v : python_versions
    libpython = cc.find_library(v, required: false)
    if libpython.found()
      break
    endif
  endforeach
  if not libpython.found()
    error('Required Python library not found (3.10 - 3.15)')
  endif

  deps+=[libpython, python_dep]
endif

# USER FLAGS. USER CAN ADD CUSTOM LIBS
ENABLE_LIBGZ = get_option('enable_libgz')
if ENABLE_LIBGZ
  libgz_wrapper_dep = declare_dependency(
    include_directories: include_directories('./courbet/gazebo'),
    link_args: ['-L' + meson.current_source_dir() + '/courbet/gazebo/build/lib', '-lgz_wrapper'],
  )
  deps += libgz_wrapper_dep
endif

ENABLE_LIBHW = get_option('enable_libhw')
if ENABLE_LIBHW
libhw_dep = declare_dependency(
  include_directories: include_directories('../../libhw/include'),
  link_args: ['-L../../libhw/out', '-lhw', '-lstlink']
)
  deps +=libhw_dep
endif

#Based on the configurations, generate config.h
ENABLE_LIBDEV = get_option('device_models')
configure_file(
  input : 'config.h.in',
  output : 'config.h',
  configuration : {
    'ENABLE_LIBGZ' : ENABLE_LIBGZ ? 1 : 0,
    'ENABLE_LIBHW' : ENABLE_LIBHW ? 1 : 0,
    'ENABLE_LIBPY' : ENABLE_LIBPY ? 1 : 0,
    'DEBUG_PRINT' : DEBUG_PRINT ? 1 : 0,
    'ENABLE_LIBDEV' : ENABLE_LIBDEV ? 1:0,
  }
)
#config_inc = include_directories(meson.current_build_dir())


QEMU_DIR = get_option('qemu_path')
qemu_include = include_directories(
  join_paths(QEMU_DIR, 'include', 'qemu'),
  join_paths(QEMU_DIR, 'include')

)

# --- Project include dir
local_include = include_directories('include')

# Find BFD and libiberty
# bfd_dep = cc.find_library('bfd', required: true)

# deps += bfd_dep
# deps += iberty_dep

# --- Collect all source files
srcs += files(
  'core/core.c',
  'python/python.c',
  'utils/utils.c',
  'virtuals/virtuals.c',
  'virtuals/fuzz.c',
  'libs/ring_buffer.c',
  'libs/mavlink_lib.c',
)

if ENABLE_LIBGZ
srcs += files(
  'libs/gcs_receiver.c',

)
endif

if ENABLE_LIBDEV
  libdl_dep = cc.find_library('dl', required: true)
  deps += libdl_dep
  srcs += files(
  'device_models/dev.c',
  'device_models/classic/classic.c',
  )

if ENABLE_LIBHW
  srcs += files(
  'device_models/passthrough/passthrough.c',
  'device_models/elder/elder.c',
  'device_models/elder/device_config.c',
  'device_models/elder/inih/ini.c'
  )
endif

  device_models_inc = include_directories('device_models/elder/inih/')
  local_include = [local_include, device_models_inc]
endif

if ENABLE_LIBGZ
mavlink_include = include_directories('submodules/mavlink_headers/ardupilotmega')

local_include = [local_include, mavlink_include]
endif

fastdyn_target = shared_module(
  'fastdyn',
  srcs,
  include_directories: [qemu_include, local_include],
  dependencies: deps,
  install: false
)

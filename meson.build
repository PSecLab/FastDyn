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

libhw_dep = declare_dependency(
  include_directories: include_directories('/data/libhw/include'),
  link_args: ['-L/data/libhw/out', '-lhw']
)

qemu_include = include_directories(
  '/data/qemu/include/qemu/',
  '/data/qemu/include'
)

# --- Collect all source files
srcs = files(
  'core/core.c',
  'device_models/dev.c',
  'python/python.c',
  'utils/utils.c',
  'virtuals/virtuals.c',
  'device_models/passthrough/passthrough.c'
)

# --- Project include dir
local_include = include_directories('include')


shared_module(
  'fastdyn',
  srcs,
  include_directories: [qemu_include, local_include],
  dependencies: [glib, libpython, libhw_dep, python_dep],
  install: false
)


srcs += files('gazebo.cpp')

# The path to the build/lib needs to be relative to THIS file.
gz_dir = meson.current_source_dir() + '/build/lib'

libgz_wrapper_dep = declare_dependency(
  include_directories: [
    include_directories('physics_engines/gazebo'), # Relative to this subdir Shouldn't be needed.
    phy_inc,
  ],
  link_args: [
    '-L' + gz_dir,
    '-lgz_wrapper',
    '-Wl,-rpath,' + gz_dir
  ],
)

# This appends to the 'deps' list defined in your root meson.build
deps += libgz_wrapper_dep

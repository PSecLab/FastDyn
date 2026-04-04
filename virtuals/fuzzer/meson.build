fuzz_inc = include_directories('.')
local_include = [local_include, fuzz_inc]

libfuzz_wrapper_dep = declare_dependency(
    link_args: ['-L' + meson.current_source_dir() + '/fastdyn_fuzz_lib/target/release', '-lfastdyn_fuzzer']
)
deps += libfuzz_wrapper_dep

subdir('protocol_fuzzers')

srcs += files(
    'fuzz.c',
    'fuzz_bbl.c',
    'fuzz_trace.c',
)
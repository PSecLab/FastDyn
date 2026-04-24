fuzz_inc = include_directories('.')
aflnet_inc = []
aflnet_dir = get_option('aflnet_path')

if ENABLE_AFLNET
    aflnet_inc = include_directories(aflnet_dir)
endif

local_include = [local_include, fuzz_inc, aflnet_inc]

if ENABLE_LIBFUZZ
    deps += declare_dependency(
        link_args: ['-L' + meson.current_source_dir() + '/fastdyn_fuzz_lib/target/release', '-lfastdyn_fuzzer']
    )
endif

if ENABLE_AFLNET
    deps += declare_dependency(
        link_args: ['-L' + aflnet_dir, '-laflnet']
    )
endif

subdir('protocol_fuzzers')
subdir('stateful_fuzzers')

srcs += files(
    'fuzz.c',
    'fuzz_bbl.c',
    'fuzz_trace.c',
)
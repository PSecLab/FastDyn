#!/usr/bin/env bash
# Direct QEMU invocation for the modifier microbench. Skips fastdyn CLI's
# ~500ms Python parse per run so 100 iterations aren't dominated by startup.
#
# Usage:  ./run_bench.sh [iterations]        (default 100)
# Output: one `elapsed_ns=<N>` line per run on stdout.

cd "$(dirname "$0")/../../../../.."   # FastDyn repo root (5 levels up)

ITERS="${1:-100}"
CONF="boardrunner/boardrunner_examples/microbenchmarks/firmware_slicing/modifier_benchmarking/configuration"
KERNEL="boardrunner/boardrunner_examples/microbenchmarks/firmware_slicing/firmware/build/microbench_slice.axf"

# No `timeout`: benchmark_end calls exit(0) in the plugin, so QEMU dies as
# soon as the [BENCH] line is emitted.
for _ in $(seq 1 "$ITERS"); do
    ../qemu/build/qemu-system-arm \
        -machine cortexm,memory-backend=ram0 \
        -cpu cortex-m4 \
        -kernel "$KERNEL" \
        --semihosting --semihosting-config enable=on,target=native \
        -global armv7m.init-nsvtor=0x08000000 \
        -object memory-backend-file,id=ram0,mem-path=../qemu/ws/my_m4_ram3,size=512M,share=on \
        -global cortexm-soc.ram_baseaddr0=0x20000000 \
        -object memory-backend-file,id=ram1,mem-path=../qemu/ws/my_m4_ram,size=512K,share=on \
        -global cortexm-soc.ram_baseaddr1=0x30000000 \
        -global cortexm-soc.ram_backend1=ram1 \
        --plugin "build/libfastdyn.so,dev=$CONF/dev_config.json,virtual=$CONF/virtuals.txt,modifier=$CONF/modifiers.txt,coverage=0,fuzzing=0,edge_coverage=0,twintrace=off,twintrace_binary=None,finline=None" \
        -display none 2>/dev/null \
    | grep -E '^\[BENCH\]'
done

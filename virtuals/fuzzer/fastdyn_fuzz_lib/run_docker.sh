docker run --rm -it \
  -v ${PWD}/../LibAFL-0.15.4:/work/ \
  -v ${PWD}/virtuals/fuzzer/fastdyn_fuzz_lib:/work/fuzzers/fastdyn/fastdyn_fuzz_lib \
  -w /work/fuzzers/fastdyn/fastdyn_fuzz_lib \
  libafl /bin/bash
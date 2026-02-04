docker run --rm -it \
  -v /data/Code/rehosting/LibAFL-0.15.4:/work/ \
  -v /data/Code/rehosting/FastDyn/fuzzer/fastdyn_fuzz_lib:/work/fuzzers/fastdyn/fastdyn_fuzz_lib \
  -w /work/fuzzers/fastdyn/fastdyn_fuzz_lib \
  libafl /bin/bash
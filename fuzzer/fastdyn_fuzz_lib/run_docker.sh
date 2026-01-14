docker run --rm -it \
  -v /home/hammad/Downloads/LibAFL-0.15.3:/work/ \
  -v /home/hammad/work/rehosting/FastDyn/fuzzer/fastdyn_fuzz_lib:/work/fuzzers/fastdyn/fastdyn_fuzz_lib \
  -w /work/fuzzers/fastdyn/fastdyn_fuzz_lib \
  libafl /bin/bash
docker run --rm -it \
  -v /root/rooney/LibAFL-0.15.3:/work/ \
  -v /root/rooney/FastDyn/fuzzer/fastdyn_split:/work/fuzzers/fastdyn/fastdyn_split \
  -w /work/fuzzers/fastdyn/fastdyn_split \
  libafl /bin/bash
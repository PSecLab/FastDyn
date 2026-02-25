export LD_LIBRARY_PATH=../../FastDyn/fuzzer/fastdyn_fuzz_lib/target/release/:$LD_LIBRARY_PATH

./qemu-system-arm --plugin ../../FastDyn/build/libfastdyn.so,dev=classic:0x40000000-0x5FFFFFFF,virtual=../../FastDyn/courbet/rover_fuzz462/unlabeled_conf/virtuals.txt,modifier=../../FastDyn/courbet/rover_fuzz462/unlabeled_conf/modifiers.txt,symbols=../../FastDyn/courbet/bin/ardurover_v462,coverage=1,bbl=1 \
    -d op,in_asm -D qemu.log -machine cortexm,memory-backend=ram0 \
    -monitor telnet:127.0.0.1:5555,server,nowait \
    -gdb tcp::1235 \
    -semihosting --semihosting-config enable=on,target=native \
    -qmp unix:/tmp/qmp-sock,server,nowait \
    -device loader,file=../../FastDyn/courbet/bin/ardurover_v462,addr=0x08004000 \
    -serial stdio -nographic \
    -object memory-backend-file,id=ram0,mem-path=../ws/memory/my_m4_ram3,size=512M,share=on \
    -global cortexm-soc.ram_baseaddr0=0x20000000 -cpu cortex-m4 \
    -global armv7m.init-nsvtor=0x08004000 \
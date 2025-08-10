cp ./tests/gen.c .
make
python3 ./test.py --lib ./gen.so  --trace ../tests/io.log --init-func gpiog_init --read-func gpiog_read --write-func gpiog_write --base-addr 0x40021800

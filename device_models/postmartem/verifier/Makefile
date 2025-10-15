all:
	gcc -g -shared -fPIC -I../../../include/ -I../../../../qemu/include/ -Iapis/include `pkg-config --cflags --libs glib-2.0` ./tests/gen.c ./apis/src/devmodels_apis.c /scratch/Fastdyn/FastDyn/utils/utils.c ./apis/src/i2c_bus.c -o gen.so

verify:
	gcc -g -shared -fPIC -I../../../include/ -I../../../../qemu/include/ `pkg-config --cflags --libs glib-2.0` ./tests/gen.c ./tests/libtest.c -o gen.so

slave:
	gcc -g -shared -fPIC -I../../../include/ -I../../../../qemu/include/ -Iapis/include `pkg-config --cflags --libs glib-2.0` /scratch/Fastdyn/FastDyn/utils/utils.c ./tests/slave.c -o slave_gen.so

clean:
	rm gen.so

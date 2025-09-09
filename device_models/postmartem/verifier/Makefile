all:
	gcc -g -shared -fPIC -I../../../include/ -I../../../../qemu/include/ `pkg-config --cflags --libs glib-2.0` ./gen.c -o gen.so

clean:
	rm gen.so

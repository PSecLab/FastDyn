# FastDyn
## How to run?

1. Make sacrifice for debugging gods so your debugging and rehosting goes smoothly.  
2. Update the path in `meson.build` to your QEMU (**fastdyn**) fork. You need to update this snippet:

   ```meson
   qemu_include = include_directories(
     '/data/qemu/include/qemu/',
     '/data/qemu/include'
   )
   ```

3. Run:

   ```bash
   make
   ```

   The Makefile will set up the build and run `ninja`.

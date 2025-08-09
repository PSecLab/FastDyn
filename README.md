# FastDyn Plugins
## How to run?

1. Make sacrifice for debugging gods so your debugging and rehosting goes smoothly.  

2. Run the following command and pass the `qemu` path along with it like:

   ```bash
   make qemu_path="/home/fastdyn-qemu"
   ```
   if you don't pass the `qemu_path` argument, then, it will use the `../qemu` as the path for the QEMU.
   The Makefile will set up the build and run `ninja`.

3. By default the libraries like `libhw` and `libgz` disabled. To enable them, please go to `Makefile` and change the respective flags.

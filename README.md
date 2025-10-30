# FastDyn Plugins
## How to run?
Install `Fastdyn` as a package using:
   ```bash
   ./setup.sh
   ```
You can pass the arguments using -c for configuration.toml, -m for symbol map file and -o for output dir. Further use:
   ```bash
   fastdyn --help
   ```
to get more information about our great tool.

1. Make sacrifice for debugging gods so your debugging and rehosting goes smoothly.

2. Run the following command and pass the `qemu` path along with it like:

   ```bash
   make qemu_path="/home/fastdyn-qemu"
   ```
   if you don't pass the `qemu_path` argument, then, it will use the `../qemu` as the path for the QEMU.
   The Makefile will set up the build and run `ninja`.
   also,
   #TODO: Update this later to be more efficient
   ```bash
   export LD_LIBRARY_PATH=/home/FastDyn/build:Fastdyn/FastDyn/device_models/postmartem/verifier
   ```

3. By default the libraries like `libhw` and `libgz` disabled. To enable them, please go to `Makefile` and change the respective flags.

### Extras Update the readme later
We expect the `cmsis-svd-data` to be placed for the generator and verifier wherever you are the running the command!
We recommend running the command from the main directory of fastdyn. (Do we need to update this?)
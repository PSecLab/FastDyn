You need to run this command in a separate terminal to run fastdyn:

```bash
/scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/bin/openocd \
  -s /scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/scripts \
  -f interface/cmsis-dap.cfg \
  -f target/max78000.cfg
```

You can pass cmsis-svd available here using the following command:

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/gpio/gpio_config.toml -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd
```
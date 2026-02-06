# Phase 1: Preparation (Before running simulation)
These commands create a 64MB empty binary file and format it as FAT32 so it mimics a fresh SD card.

The model is set up to expect sdcard.img in the directory fastdyn is run from, so run these commands from the same directory

```bash
# 1. Create a blank 64MB file filled with zeros (64MB is a safe size to ensure FAT32 compatibility)
dd if=/dev/zero of=sdcard.img bs=1M count=64
# 2. Format the file as FAT32
# -F 32: Force FAT32 type
# -S 512: Sector size 512 bytes
# -n SDCARD: Volume label
mkfs.fat -F 32 -S 512 -n SDCARD sdcard.img
```
# Phase 2: Run Your Simulation
There are two different models, one is the base model, the other acts as a harness for the fuzzer.
The fuzzer is a simple demonstration of fuzzing blocks of data from the sd card from the model, the application however only checks the size.
The FIFO register is used for multiple purposes, so modifiers are used to write a flag to memory when the firmware is at the point where it reads, the fuzzer checks for the flag before writing data.

# Phase 3: Verification (After simulation finishes)
```bash
# 3. Create a temporary mount point (folder)
sudo mkdir -p /mnt/sdcard_sim

# 4. Mount the image file to that folder
# -o loop tells Linux to treat the file like a block device
sudo mount -o loop sdcard.img /mnt/sdcard_sim

# 5. List the files to see if STM32.TXT exists
ls -l /mnt/sdcard_sim

# 6. Read the content of the file
cat /mnt/sdcard_sim/STM32.TXT

# Expected Output: "This is STM32 working with FatFs"
```bash
# Phase 4: Cleanup
sudo umount /mnt/sdcard_sim

# 8. (Optional) Remove the temporary folder
sudo rmdir /mnt/sdcard_sim
```
Run the following commands:
#TODO:
Add later

The reason for three different firmwares is just because the flash_firmware and passthrough one is compiled with 192.168.7.1 and the stm32 one is compiled with 192.168.8.2

To run the firmware in elder mode, first create tap on your linux machine
```bash
sudo ip tuntap add dev tap0 mode tap user $(whoami)
# 1. Assign the IP address
sudo ip addr add 192.168.8.1/24 dev tap0
# 2. Bring the interface up
sudo ip link set dev tap0 up
#TEST
ip addr show tap0
```
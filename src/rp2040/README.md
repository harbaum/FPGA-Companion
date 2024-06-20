# MiSTeryNano FPGA companion Pico Pico variant

The is the variant of the MiSTeryNano FPGA companion firmware
for the Raspberry Pi Pico (W).

## Building

### Download an install the Pi Pico SDK

### Update the Pico-PIO-USB component

In pico-sdk/lib/tinyusb:

This is supposed to be working, but the Pico-PIO-USB is too old
```
  python3 tools/get_dependencies.py rp2040
```

Instead do:
```
  cd hw/mcu/raspberry_pi
  rm -rf Pico-PIO-USB
  git clone https://github.com/sekigon-gonnoc/Pico-PIO-USB.git
```

### Download the FreeRTOS kernel

```
git clone https://github.com/FreeRTOS/FreeRTOS-Kernel
```

### Download the FatFS

```
git clone https://github.com/abbrev/fatfs.git
rm fatfs/source/ffconf.h
```

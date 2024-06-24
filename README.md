# MiSTeryNano FPGA Companion

The MiSTeryNano FPGA Companion implements support functions for FPGA
based retro computing. While the FPGA typically implements the
hardware of the retro machine itself the FPGA Companion uses a
microcontroller to support modern peripherals like USB keyboard, mice
and SD cards. It also implements an on-screen-display menu to allow
the user to configure the retro machine.

Thhe FPGA Companions replaces the MiSTeryNano firmware that was
formerly part of the [MiSTeryNano
project](https://github.com/harbaum/MiSTeryNano). It is also
used by the [NanoMig](https://github.com/harbaum/nanomig), the
[C64Nano](https://github.com/vossstef/tang_nano_20k_c64) and the
[VIC20Nano](https://github.com/vossstef/VIC20Nano).

## Supported MCUs

Currently the FPGA Companion can be used with the following MCUs:

 - [M0S/BL616](https://wiki.sipeed.com/hardware/en/maixzero/m0s/m0s.html), [build src/bl616](src/bl616), and
 - [Raspberry Pi Pico/RP2040](https://www.raspberrypi.com/documentation/microcontrollers/raspberry-pi-pico.html), [build src/rp2040](src/rp2040)
 - [ESP32 S2/S3](https://www.espressif.com/en/products/socs/esp32-s2), currently under development
 
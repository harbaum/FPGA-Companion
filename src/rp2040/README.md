# MiSTeryNano FPGA companion Pi Pico / RP2040 variant

The is the variant of the MiSTeryNano FPGA companion firmware
for the Raspberry Pi Pico (W).

## Building

### Install the Pi Pico Toolchain
```
 sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
```

### Download an install the Pi Pico SDK

Download the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
and set the ```PICO_SDK_PATH``` environment variable to point to the
SDKs root directory.

```
git clone https://github.com/raspberrypi/pico-sdk --recursive
export PICO_SDK_PATH=<full-path-to-clones-sdk>
```

### Update the TinyUSB component

The TinyUSB inside the Pico SDK needs to be at least version 0.17.0.

To update it go to pico-sdk/lib and install PIO-USB do:

```
cd pico-sdk/lib
mv tinyusb tinyusb.old
git clone https://github.com/hathach/tinyusb.git
cd tinyusb
python3 tools/get_deps.py rp2040
```

### Run cmake and make

To start the build process create a build directory and start the
compilation:

```
cd src/rp2040
mkdir build
cd build
cmake ..
make
```

The resulting file named ```fpga_companion.uf2``` is loaded onto the
Pico as usual. Once successfully booted the Pico's LED will blink.

Additional debug output is sent via UART at 115200 bit/s on GP0

# Pin usage

| Pin | Signal | Description |
|---|---|---|
| GP0  | UART_TX  | Serial debug output |
| GP2  | USB D+ | USB PIO host D+ |
| GP3  | USB D- | USB PIO host D-   |
| GP16 | MISO | SPI data from FPGA |
| GP17 | CSn | SPI chip select to FPGA |
| GP18 | SCK | SPI clock to FPGA |
| GP19 | MOSI | SPI data to FPGA |
| GP22 | IRQn | SPI interrupt from FPGA |

## Using the internal micro USB connector

The RP2040 can use the pins GP2 and GP3 for a USB host port as
depicted below. This is the default configuration for the
FPGA Companion, although it's possible to use the standard
on-board micro USB connector instead.

To do so in the file ```tusb_config.h``` the PIO USB
needs to be disabled by setting the following value
to 0:

```
// change to 0 if using on-board native micro USB
// change to 1 if using pico-pio-usb as host controller for raspberry rp2040
#define CFG_TUH_RPI_PIO_USB   1
```

To use this you need a micro-USB to USB-A-OTG adapter.

# Example wiring

![Tang Nano 20k with Raspberry Pi Pico](pico_tn20k.png)

# Convenient development

The RP2040 is by default somewhat inconvenient to develop for
as it has to be mounted manually as mass-storage to copy the
uf2 file onto it.

It's thus recommanded to use a second Pi-Pico as a SWD programmng
adapter. The details are explained in appendix A of
[Getting started with Raspberry Pi Pico](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf) under section ```Debug with a second Pico```.

The ```CMakeLists.txt``` file already contains a matching target and
with openocd installed a simple ```make flash``` should upload the
new firmware via the second Pico.


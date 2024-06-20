# MiSTeryNano FPGA companion Pico Pico variant

The is the variant of the MiSTeryNano FPGA companion firmware
for the Raspberry Pi Pico (W).

## Building

### Download an install the Pi Pico SDK

Download the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
and set the ```PICO_SDK_PATH``` environment variable to point to the
SDKs root directory.

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

### Run cmake and make

To start the build process create a build directory and start the
compilation:

```
cd src/rp2040
mkdir build
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

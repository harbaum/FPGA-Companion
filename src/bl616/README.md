# MiSTeryNano FPGA companion BL616 variant

The is the variant of the MiSTeryNano FPGA companion firmware
for the BL616 MCU (M0S Dock).

The [instructions in the MiSTerNano repositories](https://github.com/harbaum/MiSTeryNano/tree/main/firmware) mostly apply to this version as well.

## Example wiring

![Tang Nano 20k with M0S Dock](m0s_dock_tn20k.png)  

additional instructions

## Install the Bouffalolab Toolchain (Windows 11)

Install [Git for Windows](https://gitforwindows.org)

Install [cmake for Windows](https://cmake.org/download)

Install Bouffalo RISC-V MCU toolchain

```text
Open Start Search, type “cmd” or Win + R and type “cmd” 

cd %HOMEPATH%
git clone https://github.com/bouffalolab/toolchain_gcc_t-head_windows.git
```

Install Bouffalo SDK with latest CherryUSB stack:  

```text
cd %HOMEPATH%
git clone --recurse-submodules https://github.com/CherryUSB/bouffalo_sdk.git
```

Set Windows SDK Environment Variable:  

Open Start Search, type “env”, and select “Edit the system environment variables”.

```text
BL_SDK_BASE=C:\Users\xyzuser\bouffalo_sdk
```

Set Windows search PATH for Toolchain:  

```text
C:\Users\xyzuser\toolchain_gcc_t-head_windows\bin
C:\Users\xyzuser\bouffalo_sdk\tools\make
C:\Users\xyzuser\bouffalo_sdk\tools\ninja
```

Close shell

```text
exit
```

Open Start Search, type “cmd” or Win + R and type “cmd”  
check individually proper start of each single tool

```text
make -v
cmake -version
ninja --help
riscv64-unknown-elf-gcc -v
```

Download FPGA companion repository

```text
cd %HOMEPATH%/Documents
git clone https://github.com/harbaum/FPGA-Companion.git
cd FPGA-Companion
git submodule init
git submodule update
```

Compile the firmware:  

```text
cd %HOMEPATH%/Documents\fork\FPGA-Companion\src\bl616
make clean
make
```

program the firmware by using [instructions](https://github.com/harbaum/MiSTeryNano/tree/main/firmware)

figure out µC bootloader COM port and use shell command:  
Press Windows + R keyboard shortcut to launch the Windows Run box, type “devmgmt. msc” , and click the OK button

```text
make CHIP=bl616 COMX=COMabc  flash
```

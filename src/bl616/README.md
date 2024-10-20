# MiSTeryNano FPGA companion BL616 variant

The is the variant of the MiSTeryNano FPGA companion firmware
for the BL616 MCU.

The [instructions in the MiSTerNano repositories](https://github.com/harbaum/MiSTeryNano/tree/main/firmware) mostly apply to this version as well.


# Example wiring

![Tang Nano 20k with M0S Dock](m0s_dock_tn20k.png)
<br><br><br><br>
additional instructions
## Install the Bouffalolab Toolchain (Windows 11)

Install [Git for Windows](https://gitforwindows.org)

Install [cmake for Windows](https://cmake.org/download)

Install Bouffalo RISC-V MCU toolchain

```
Open Start Search, type “cmd” or Win + R and type “cmd” 

cd %HOMEPATH%
git clone https://github.com/bouffalolab/toolchain_gcc_t-head_windows.git
```

Install modified Bouffalo SDK:
```
cd %HOMEPATH%
git clone --recurse-submodules https://github.com/harbaum/bouffalo_sdk.git
```

This is a fork of the original Bouffalo SDK with the CherryUSB stack updated to a more recent version<br>
<br>

Set Windows SDK Environment Variable: <br>

Open Start Search, type “env”, and select “Edit the system environment variables”.
```
BL_SDK_BASE=C:\Users\xyzuser\bouffalo_sdk
```

Set Windows search PATH for Toolchain: <br>

```
C:\Users\xyzuser\toolchain_gcc_t-head_windows\bin
C:\Users\xyzuser\bouffalo_sdk\tools\make
C:\Users\xyzuser\bouffalo_sdk\tools\ninja
```

(Optional) Set Windows search PATH for Gowin FPGA Tools: <br>
```
C:\Gowin\Gowin_V1.9.10_x64\IDE\bin
C:\Gowin\Gowin_V1.9.10_x64\Programmer\bin
```

Close shell
```
exit
```

Open Start Search, type “cmd” or Win + R and type “cmd”  
check individually proper start of each single tool
```
make -v
cmake -version
ninja --help
riscv64-unknown-elf-gcc -v
```

Download FPGA companion repository

```
cd %HOMEPATH%/Documents
    If you plan to enhance the fw create a fork and clone from there.
    mkdir fork
    cd fork
    git clone https://github.com/xyzuser/FPGA-Companion.git
git clone https://github.com/harbaum/FPGA-Companion.git
cd FPGA-Companion
git submodule init
git submodule update
```

Compile the firmware:<br>
```
cd %HOMEPATH%/Documents\fork\FPGA-Companion\src\bl616
make clean
make
```

program the firmware by using [instructions](https://github.com/harbaum/MiSTeryNano/tree/main/firmware)

figure out µC bootloader COM port and use shell command:  
Press Windows + R keyboard shortcut to launch the Windows Run box, type “devmgmt. msc” , and click the OK button

```
make CHIP=bl616 COMX=COMabc  flash
```

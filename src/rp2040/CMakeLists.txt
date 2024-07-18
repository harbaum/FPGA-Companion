cmake_minimum_required(VERSION 3.13)

set(PROJECT fpga_companion)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(${PROJECT} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Pull in FreeRTOS 
include(../FreeRTOS-Kernel/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

# u8g2
file(GLOB U8G2_SRC ../u8g2/csrc/*.c)
add_library(u8g2 ${U8G2_SRC})

add_executable(${PROJECT}
        ../main.c
        mcu_hw.c
	../sysctrl.c
	../hidparser.c
	../hid.c
	../sdc.c
	../osd_u8g2.c
	../menu.c
	../inifile.c
	../core.c
	../core_atarist.c
	../core_c64.c
	../core_vic20.c
	../core_amiga.c
	../fatfs/source/ff.c
	../fatfs/source/ffunicode.c
	../u8g2/sys/bitmap/common/u8x8_d_bitmap.c
	../tusb_xinput/xinput_host.c
	../config.c
	../freertos_callbacks.c
	../xml.c
)

family_add_pico_pio_usb(${PROJECT})

target_compile_definitions(${PROJECT} PRIVATE
        PIO_USB_DP_PIN_DEFAULT=2
        )

target_include_directories(${PROJECT} PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}
      ${CMAKE_CURRENT_LIST_DIR}/../fatfs/source
      ${CMAKE_CURRENT_LIST_DIR}/../u8g2/csrc
      ${CMAKE_CURRENT_LIST_DIR}/../tusb_xinput
)

string(APPEND CMAKE_C_FLAGS " -Os -fdata-sections -ffunction-sections -Wno-error=incompatible-pointer-types" )

target_link_libraries(${PROJECT} PRIVATE pico_stdlib FreeRTOS-Kernel FreeRTOS-Kernel-Heap1)
target_link_libraries(${PROJECT} PRIVATE pico_multicore hardware_pio hardware_dma hardware_spi)
target_link_libraries(${PROJECT} PRIVATE tinyusb_host tinyusb_board u8g2)

pico_add_extra_outputs(${PROJECT})
pico_enable_stdio_usb(${PROJECT} 0)
pico_enable_stdio_uart(${PROJECT} 1)

add_custom_target(flash
    COMMAND echo "Flashing ${PROJECT} ..."
    COMMAND openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program ${PROJECT}.elf verify reset exit"
    DEPENDS "${PROJECT}"
    COMMENT "Flash target using openocd"
)

target_link_options(${PROJECT} PRIVATE -Xlinker --print-memory-usage)
target_compile_options(${PROJECT} PRIVATE -Wall -Wextra)

add_custom_target(reset
    COMMAND echo "Reseting ${PROJECT} ..."
    COMMAND openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c init -c reset -c exit
    COMMENT "Reset target using openocd"
)

add_custom_target(term
    COMMAND term.sh /dev/ttyACM0 115200
)

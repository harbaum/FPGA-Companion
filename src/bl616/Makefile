# Makefile for the MiSTeryNano firmware

COMX?=/dev/m0s_debugger
BAUDRATE?=2000000

SDK_DEMO_PATH ?= .
BL_SDK_BASE ?= $(SDK_DEMO_PATH)/../../..

CHIP ?= bl616
# BOARD ?= bl616dk -DCMAKE_C_FLAGS="-DM0S_DOCK=1"
BOARD ?= bl616dk
CROSS_COMPILE ?= riscv64-unknown-elf-

include $(BL_SDK_BASE)/project.build

term:
	term.sh $(COMX) $(BAUDRATE)

FATFS_SRC=$(BL_SDK_BASE)/components/fs/fatfs
FATFS_FILES=$(FATFS_SRC)/ff.c $(FATFS_SRC)/diskio.c $(FATFS_SRC)/ffunicode.c

MXML_SRC=mxml
MXML_FILES=$(MXML_SRC)/mxml-attr.c $(MXML_SRC)/mxml-file.c $(MXML_SRC)/mxml-get.c $(MXML_SRC)/mxml-index.c $(MXML_SRC)/mxml-node.c $(MXML_SRC)/mxml-options.c $(MXML_SRC)/mxml-private.c $(MXML_SRC)/mxml-search.c $(MXML_SRC)/mxml-set.c

SDL_TEST_CFLAGS=-I. -I$(FATFS_SRC) -I$(MXML_SRC) -Iu8g2/csrc `sdl2-config --cflags` -DSDL
SDL_TEST_SRC=u8g2/csrc/*.c u8g2/sys/bitmap/common/*.c u8g2/sys/sdl/common/*.c

sdl_menu_test: sdl_menu_test.c menu.c menu.h fatfs_conf_user.h $(FATFS_FILES) $(MXML_FILES) 
	gcc $(SDL_TEST_CFLAGS) -o sdl_menu_test sdl_menu_test.c menu.c $(SDL_TEST_SRC) $(FATFS_FILES) $(MXML_FILES) `sdl2-config --libs` 

test: sdl_menu_test
	./sdl_menu_test

/*
  sysctrl.h

  MiSTeryNano system control interface
*/

#ifndef SYS_CTRL_H
#define SYS_CTRL_H

#include <stdbool.h>
#include "config.h"
#include "spi.h"

// the MCU can adopt to the core running to e.g.
// change the menu and the keyboard mapping
#define CORE_ID_UNKNOWN  0x00
#define CORE_ID_ATARI_ST 0x01
#define CORE_ID_C64      0x02
#define CORE_ID_VIC20    0x03
#define CORE_ID_AMIGA    0x04

extern unsigned char core_id;

int  sys_status_is_valid(void);
void sys_set_leds(char);
void sys_set_rgb(unsigned long);
unsigned char sys_get_buttons(void);
void sys_set_val(char, uint8_t);
unsigned char sys_irq_ctrl(unsigned char);
void sys_handle_interrupts(unsigned char);
bool sys_wait4fpga(void);

#endif // SYS_CTRL_H

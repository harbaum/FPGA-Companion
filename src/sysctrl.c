/*
  sysctrl.c

  MiSTeryNano system control interface
*/

#include <stdio.h>
#include <stdlib.h>

#include "spi.h"
#include "sysctrl.h"
#include "sdc.h"
#include "osd.h"
#include "inifile.h"
#include "core.h"

#include "debug.h"
#include "config.h"
#include "mcu_hw.h"
#include "at_wifi.h"

unsigned char core_id = 0;

static const char *core_names[] = {
  "<unset>", "Atari ST", "C64", "VIC", "Amiga", "Atari 2600"
};

const char *sys_get_config_name(void) {
  const char *config_xml[] = {
    CARD_MOUNTPOINT "/config.xml",
    CARD_MOUNTPOINT "/atarist.xml",
    CARD_MOUNTPOINT "/c64.xml",
    CARD_MOUNTPOINT "/vic20.xml",
    CARD_MOUNTPOINT "/amiga.xml",
    CARD_MOUNTPOINT "/atari2600.xml"
  };
  return config_xml[core_id];
}

static void sys_begin(unsigned char cmd) {
  mcu_hw_spi_begin();  
  mcu_hw_spi_tx_u08(SPI_TARGET_SYS);
  mcu_hw_spi_tx_u08(cmd);
}  

int sys_status_is_valid(void) {
  sys_begin(SPI_SYS_STATUS);
  mcu_hw_spi_tx_u08(0);
  unsigned char b0 = mcu_hw_spi_tx_u08(0);
  unsigned char b1 = mcu_hw_spi_tx_u08(0);
  core_id = mcu_hw_spi_tx_u08(0);
  unsigned char coldboot = mcu_hw_spi_tx_u08(0);
  mcu_hw_spi_end();  

  if((b0 == 0x5c) && (b1 == 0x42)) {
    sys_debugf("Core ID: %02x", core_id);
    if(core_id < 3) sys_debugf("Core: %s", core_names[core_id]);

    // coldboot status equals core_id on cores not supporting cold
    // boot status
    if(coldboot != core_id) {
      // check colboot status
      sys_debugf("Coldboot status is %02x", coldboot);

    }
  }
  
  return((b0 == 0x5c) && (b1 == 0x42));
}

void sys_set_leds(char leds) {
  sys_begin(SPI_SYS_LEDS);
  mcu_hw_spi_tx_u08(leds);
  mcu_hw_spi_end();  
}

void sys_set_rgb(unsigned long rgb) {
  sys_begin(SPI_SYS_RGB);
  mcu_hw_spi_tx_u08((rgb >> 16) & 0xff); // R
  mcu_hw_spi_tx_u08((rgb >> 8) & 0xff);  // G
  mcu_hw_spi_tx_u08(rgb & 0xff);         // B
  mcu_hw_spi_end();    
}

unsigned char sys_get_buttons(void) {
  unsigned char btns = 0;
  
  sys_begin(SPI_SYS_BUTTONS);
  mcu_hw_spi_tx_u08(0x00);
  btns = mcu_hw_spi_tx_u08(0);
  mcu_hw_spi_end();

  return btns;
}

void sys_set_val(char id, uint8_t value) {
  sys_debugf("SYS set value %c = %d", id, value);
  
  sys_begin(SPI_SYS_SETVAL);   // send value command
  mcu_hw_spi_tx_u08(id);              // value id
  mcu_hw_spi_tx_u08(value);           // value itself
  mcu_hw_spi_end();  
}

unsigned char sys_irq_ctrl(unsigned char ack) {
  sys_begin(SPI_SYS_IRQ_CTRL);
  mcu_hw_spi_tx_u08(ack);
  unsigned char ret = mcu_hw_spi_tx_u08(0);
  mcu_hw_spi_end();  
  return ret;
}

// send a byte for "port" communication which is core specific
// and may be some serial or parallel port which the MCU implements
void sys_port_put(unsigned char byte) {
  sys_begin(SPI_SYS_PORT_IN);
  mcu_hw_spi_tx_u08(byte);
  mcu_hw_spi_end();
}

// read status and byte from port out
static bool sys_port_get(unsigned char *d) {
  sys_begin(SPI_SYS_PORT_OUT);
  mcu_hw_spi_tx_u08(0);
  unsigned char status = mcu_hw_spi_tx_u08(0);
  *d = mcu_hw_spi_tx_u08(0);
  mcu_hw_spi_end();  

  return status & 1;
}

static void sys_handle_event(bool ignore_coldboot) {
  // the FPGAs cold boot flag was set indicating that the
  // FPGA has be reloaded while the MCU was running. Reset
  // the MCU to re-initialize everything and get into a
  // sane state

  // request interrupt source
  sys_begin(SPI_SYS_IRQ_SRC);
  mcu_hw_spi_tx_u08(0);
  unsigned char irq_src = mcu_hw_spi_tx_u08(0);
  mcu_hw_spi_end();
  if(irq_src & 2) {
    // sys_debugf("Port out data request");
    
    // read port data
    unsigned char byte;
    while(sys_port_get(&byte))
      at_wifi_port_byte(byte);
  }
  
  // no irq source given at all means coldboot of a very old core.
  // TODO: That does not work as intended and triggers with port
  // IO on later cores. Disabled for now ...
  if(irq_src & 1 /* || !irq_src */ ) {
    if(ignore_coldboot)
      sys_debugf("FPGA cold boot detected, ignoring for now");
    else {
      sys_debugf("FPGA cold boot detected, reseting MCU ...");
      mcu_hw_reset();
    }
  }
}

void sys_handle_interrupts(unsigned char pending, bool ignore_coldboot) {
  // debugf("IRQ = %02x", pending);

  if(pending & 0x01) // irq 0 = SYSCTRL
    sys_handle_event(ignore_coldboot);

  if(pending & 0x02) // irq 1 = HID
    hid_handle_event();
  
  if(pending & 0x08) // irq 3 = SDC
    sdc_handle_event();
  
  //  if(pending & 0x10) // irq 4 = AUDIO
  //    audio_handle_event();
}

bool sys_wait4fpga(void) {
  sys_debugf("Waiting for FPGA to become ready");
  
  // try to establish connection to FPGA for five seconds. Assume the FPGA is
  // not properly configured after that
  int fpga_ok, timeout = 500;
  do {
    fpga_ok = sys_status_is_valid();
    if(!fpga_ok) {
      vTaskDelay(pdMS_TO_TICKS(10));
      timeout--;
    }
  } while(timeout && !fpga_ok);

  if(timeout) {
    sys_debugf("FPGA ready after %dms!", (500-timeout)*10);

    // core_id is set now, so handle the legacy cores. The
    // new config driven cores will (hopefully) handle this
    // in the init action
    if(core_id != CORE_ID_UNKNOWN)
      sys_set_val('R', 3);  // immediately set reset as the config may change
    
    sys_set_rgb(0x000040);  // blue
    return true;
  }
  
  sys_debugf("FPGA not ready after 5 seconds!");
  // this is basically useless and will only work if the
  // FPGA receives requests but cannot answer them
  sys_set_rgb(0x400000);  // red
  return false;
}

void sys_run_action(config_action_t *action) {
  if(!action) return;
  
  sys_debugf("Running action '%s'", action->name);

  // execute all commands
  for(int i=0;action->commands[i].code != CONFIG_ACTION_COMMAND_IDLE;i++) {
    switch(action->commands[i].code) {
    case CONFIG_ACTION_COMMAND_SET:
      sys_debugf("SET('%c',%d)", action->commands[i].set.id,
		 action->commands[i].set.value);
      sys_set_val(action->commands[i].set.id, action->commands[i].set.value);
      break;
      
    case CONFIG_ACTION_COMMAND_DELAY:
      sys_debugf("DELAY(%d)", action->commands[i].delay.ms);
      vTaskDelay(pdMS_TO_TICKS(action->commands[i].delay.ms));
      break;
      
    case CONFIG_ACTION_COMMAND_LOAD:
      sys_debugf("LOAD %s", action->commands[i].filename);

      // try to read ini file
      if(inifile_read(action->commands[i].filename) != 0)
	// ini file loading failed: set core specific defaults
	core_set_default_images();
      break;
      
    case CONFIG_ACTION_COMMAND_SAVE:
      sys_debugf("SAVE %s", action->commands[i].filename);
      inifile_write(action->commands[i].filename);
      break;
      
    case CONFIG_ACTION_COMMAND_HIDE:
      sys_debugf("HIDE OSD");
      osd_enable(OSD_INVISIBLE);  // hide OSD
      break;
      
    case CONFIG_ACTION_COMMAND_LINK:
      sys_debugf("LINK");
      sys_run_action(action->commands[i].action);
      break;
    }
  }
}

void sys_run_action_by_name(char *name) {
  // check for init action
  config_action_t *action = config_get_action(name);
  if(action) sys_run_action(action);
}

char *sys_get_config(void) {
  char *ret = NULL;
  // (try to) read xml directly from core

  // read the xml two times, one time to determine
  // the size and a second time to actually read it
  
  sys_begin(SPI_SYS_READ_CFG);
  mcu_hw_spi_tx_u08(0);
  mcu_hw_spi_tx_u08(0);

  char c = mcu_hw_spi_tx_u08(0);
  int len;
  for(len=0;len < 8191 && c >= 32 && c != 0xff;len++)
    c = mcu_hw_spi_tx_u08(0);

  mcu_hw_spi_end();  

  sys_debugf("core xml config size: %d", len);
  if(len < 100) return NULL;
  
  // allocate enough space
  ret = malloc(len+1);

  // and read the data into the buffer
  sys_begin(SPI_SYS_READ_CFG);
  mcu_hw_spi_tx_u08(0);
  mcu_hw_spi_tx_u08(0);
  
  int i;
  for(i=0;i<len;i++) ret[i] = mcu_hw_spi_tx_u08(0);
  ret[i] = '\0';
  
  mcu_hw_spi_end();  
  
  return ret;
}

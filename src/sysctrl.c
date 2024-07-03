/*
  sysctrl.c

  MiSTeryNano system control interface
*/

#include <stdio.h>

#include "spi.h"
#include "sysctrl.h"
#include "sdc.h"
#include "osd.h"
#include "inifile.h"
#include "core.h"

#include "debug.h"
#include "config.h"
#include "mcu_hw.h"

unsigned char core_id = 0;

static const char *core_names[] = {
  "<unset>", "Atari ST", "C64", "VIC", "Amiga"
};

const char *sys_get_config_name(void) {
  const char *config_xml[] = {
    CARD_MOUNTPOINT "/config.xml",
    CARD_MOUNTPOINT "/atarist.xml",
    CARD_MOUNTPOINT "/c64.xml",
    CARD_MOUNTPOINT "/vic20.xml",
    CARD_MOUNTPOINT "/amiga.xml"
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

static void sys_handle_event(void) {
  // the FPGAs cold boot flag was set indicating that the
  // FPGA has be reloaded while the MCU was running. Reset
  // the MCU to re-initialize everything and get into a
  // sane state
  
  sys_debugf("FPGA cold boot detected, reseting MCU ...");
  mcu_hw_reset();
}

void sys_handle_interrupts(unsigned char pending) {
  // debugf("IRQ = %02x", pending);

  if(pending & 0x01) // irq 0 = SYSCTRL
    sys_handle_event();

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

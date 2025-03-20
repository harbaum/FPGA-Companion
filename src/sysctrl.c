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

static void sys_port_begin(unsigned char cmd) {
  sys_begin(SPI_SYS_PORT);
  mcu_hw_spi_tx_u08(cmd); 
}

static uint8_t sys_port_tx_free(unsigned char port) {
  sys_port_begin(SPI_SYS_PORT_STATUS);
  // first byte always returns the number of ports implemented by core
  uint8_t ports = mcu_hw_spi_tx_u08(port);

  // the follwing bytes are specific to the requested port
  uint8_t type = mcu_hw_spi_tx_u08(0);
  mcu_hw_spi_tx_u08(0);  // skip rx_available
  uint8_t tx_available = mcu_hw_spi_tx_u08(0);  
  mcu_hw_spi_end();

  // return false if there's no such port
  if(!ports || type != 0)
    return 0;

  // if(tx_available) printf("TX avail: %d\n", tx_available);
  return tx_available;
}
  
// send a byte for "port" communication which is core specific
// and may be some serial or parallel port which the MCU implements
static void sys_port_put(unsigned char port, unsigned char byte) {
  // wait for tx buffer space to become available. This may actually
  // wait forever when no program running in the core actually reads
  // port data
  uint8_t tx_free = sys_port_tx_free(0);
  while(!tx_free) {
    vTaskDelay(pdMS_TO_TICKS(1));
    tx_free = sys_port_tx_free(0);
  }

  // send as many bytes as space in buffer
  sys_port_begin(SPI_SYS_PORT_PUT); // port command: send byte
  mcu_hw_spi_tx_u08(port);
  mcu_hw_spi_tx_u08(byte);
  mcu_hw_spi_end();
}

void sys_port_write(unsigned char port, const unsigned char *ptr, int len) {
  // todo: send longer chunks
  while(len--) sys_port_put(port, *ptr++);
}

bool sys_port_get_status(unsigned char port) {
  static const char *parity_str[] = { "N", "O", "E", "?" }; 
  static const char *stop_str[] = { "1", "1.5", "2", "?" }; 
  struct port_serial_status serial_status;

  sys_port_begin(SPI_SYS_PORT_STATUS);
  // first byte always returns the number of ports implemented by core
  uint8_t ports = mcu_hw_spi_tx_u08(port);

  // the follwing bytes are specific to the requested port
  uint8_t type = mcu_hw_spi_tx_u08(0);
  uint8_t rx_available = mcu_hw_spi_tx_u08(0);
  uint8_t tx_available = mcu_hw_spi_tx_u08(0);
  
  // read further info in case it's a serial port (type == 0)
  if(type == 0) {
    // read additional 32 bit
    uint8_t *ptr = (uint8_t*)&serial_status;
    for(int i=0;i<4;i++) *ptr++ = mcu_hw_spi_tx_u08(0);    
  }

  mcu_hw_spi_end();

  printf("Number of ports: %d\n", ports);

  // return -1 if there's no such port
  if(!ports || type != 0)
    return false;

  printf("Port 0: Type = %d, rx_available = %d, tx_available = %d\n", type, rx_available, tx_available);
  
  if(type == 0) {
    printf("  Serial status:\n");
    printf("  Bitrate:  %d\n", serial_status.bitrate);
    printf("  Databits: %d\n", serial_status.databits);
    printf("  Partity:  %s\n", parity_str[serial_status.parity]);
    printf("  Stopbits: %s\n", stop_str[serial_status.stopbits]);
  }
  
  return true;
}
  
static uint8_t sys_port_rx_available(unsigned char port) {
  sys_port_begin(SPI_SYS_PORT_STATUS);
  // first byte always returns the number of ports implemented by core
  uint8_t ports = mcu_hw_spi_tx_u08(port);

  // the follwing bytes are specific to the requested port
  uint8_t type = mcu_hw_spi_tx_u08(0);
  uint8_t rx_available = mcu_hw_spi_tx_u08(0);
  
  mcu_hw_spi_end();

  // return false if there's no such port
  if(!ports || type != 0)
    return 0;

  return rx_available;
}
  
// read status and byte from port out
static int16_t sys_port_get(unsigned char port) {
  uint8_t rx_avail = sys_port_rx_available(0);
  if(!rx_avail) return -1;           // no such port or no data available

  // TODO: we can actually read more than one byte at a time with this
  sys_port_begin(SPI_SYS_PORT_GET);  // port command: get byte
  mcu_hw_spi_tx_u08(port);
  mcu_hw_spi_tx_u08(1);              // read one byte from fifo
  uint8_t d = mcu_hw_spi_tx_u08(0);  // read last byte without increasing fifo pointer
  mcu_hw_spi_end();
  
  return d;
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
    // read port 0 data for wifi emulation
    int16_t byte = sys_port_get(0);
    while(byte >= 0) {
      at_wifi_port_byte(byte);
      byte = sys_port_get(0);
    }
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

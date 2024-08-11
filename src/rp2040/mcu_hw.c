/*
  mcu_hw.c - MiSTeryNano FPGA companion hardware driver for rp2040
*/

// /opt/pico-sdk/lib/tinyusb/examples/host/hid_controller/src/main.c

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <timers.h>

#include "pico/stdlib.h"

#include <stdio.h>
#include "pio_usb.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#include "../debug.h"
#include "../config.h"
#include "../spi.h"

#include "../mcu_hw.h"

/* ============================================================================================= */
/* ===============                          USB                                   ============== */
/* ============================================================================================= */
#include "tusb.h"
#include "../hid.h"
#include "../hidparser.h"

#include "tusb_option.h"
#ifndef TUSB_VERSION_NUMBER
#error "Cannot determine TinyUSB version!"
#endif

#if TUSB_VERSION_NUMBER < 1700
#error "Please update your TinyUSB installation!"
#endif

static struct {
  uint8_t dev_addr;
  uint8_t instance;
  hid_state_t state;
  hid_report_t rep;
} hid_device[MAX_HID_DEVICES];

static struct {
  uint8_t dev_addr;
  uint8_t instance;
  uint8_t js_index;
  uint8_t state;
} xbox_state[MAX_XBOX_DEVICES];
  
static void pio_usb_task(__attribute__((unused)) void *parms) {
  // mark all hid and xbox entries as unused
  for(int i=0;i<MAX_HID_DEVICES;i++)
    hid_device[i].dev_addr = 0xff;

  for(int i=0;i<MAX_XBOX_DEVICES;i++)
    xbox_state[i].dev_addr = 0xff;
    
  while(1) {
    tuh_task();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
  // Interface protocol (hid_interface_protocol_enum_t)
  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  usb_debugf("[%04x:%04x][%u] HID Interface%u, Protocol = %s",
  	     vid, pid, dev_addr, instance, protocol_str[itf_protocol]);

  // search for a free hid entry
  int idx;
  for(idx=0;idx<MAX_HID_DEVICES && (hid_device[idx].dev_addr != 0xff);idx++);
  if(idx != MAX_HID_DEVICES) {
    usb_debugf("Using HID entry %d", idx);
    
    if(parse_report_descriptor(desc_report, desc_len, &hid_device[idx].rep, NULL)) {
      hid_device[idx].dev_addr = dev_addr;
      hid_device[idx].instance = instance;
      if(hid_device[idx].rep.type == REPORT_TYPE_JOYSTICK)
	hid_device[idx].state.joystick.js_index = hid_allocate_joystick();
    } else
      usb_debugf("ignoring device");
  } else
    usb_debugf("Error, no more free HID entries");
  
  // tuh_hid_report_received_cb() will be invoked when report is available
  if ( !tuh_hid_receive_report(dev_addr, instance) ) 
    usb_debugf("Error: cannot request report");
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  usb_debugf("[%u] HID Interface%u is unmounted", dev_addr, instance);

  // find matching hid report
  for(int idx=0;idx<MAX_HID_DEVICES;idx++) {
    if(hid_device[idx].dev_addr == dev_addr && hid_device[idx].instance == instance) {
      usb_debugf("releasing %d", idx);
      hid_device[idx].dev_addr = 0xff;
      if(hid_device[idx].rep.type == REPORT_TYPE_JOYSTICK)
	hid_release_joystick(hid_device[idx].state.joystick.js_index);
    }
  }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  // usb_debugf("[%u] HID Interface%u", dev_addr, instance);

  // find matching hid report
  for(int idx=0;idx<MAX_HID_DEVICES;idx++)
    if(hid_device[idx].dev_addr == dev_addr && hid_device[idx].instance == instance)     
      hid_parse(&hid_device[idx].rep, &hid_device[idx].state, report, len);
  
  // continue to request to receive report
  if ( !tuh_hid_receive_report(dev_addr, instance) )
    usb_debugf("Error: cannot request report");
}

/* ============================================================================================= */
/* ===============                          SPI                                   ============== */
/* ============================================================================================= */
#include "pico/binary_info.h"
#include "hardware/spi.h"
#include "queue.h"

extern TaskHandle_t com_task_handle;
static SemaphoreHandle_t sem;

static void irq_handler(void) {
  // Disable interrupt. It will be re-enabled by the com task
  gpio_set_irq_enabled(22, GPIO_IRQ_LEVEL_LOW, false);

  if(com_task_handle) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR( com_task_handle, &xHigherPriorityTaskWoken );
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
  }
}

void mcu_hw_spi_init(void) {
  debugf("Initializing SPI");

  sem = xSemaphoreCreateMutex();

  // init SPI at 20Mhz, mode 1
  spi_init(spi_default, 20000000);
  spi_set_format(spi_default, 8, SPI_CPOL_0, SPI_CPHA_1, SPI_MSB_FIRST);
  
  debugf("  MISO = %d", PICO_DEFAULT_SPI_RX_PIN);
  gpio_set_function(PICO_DEFAULT_SPI_RX_PIN, GPIO_FUNC_SPI);
  debugf("  SCK  = %d", PICO_DEFAULT_SPI_SCK_PIN);
  gpio_set_function(PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);
  debugf("  MOSI = %d", PICO_DEFAULT_SPI_TX_PIN);
  gpio_set_function(PICO_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);
  
  // Chip select is active-low, so we'll initialise it to a driven-high state
  debugf("  CSn  = %d", PICO_DEFAULT_SPI_CSN_PIN);
  gpio_init(PICO_DEFAULT_SPI_CSN_PIN);
  gpio_set_dir(PICO_DEFAULT_SPI_CSN_PIN, GPIO_OUT);
  gpio_put(PICO_DEFAULT_SPI_CSN_PIN, 1);

  // The interruput input isn't strictly part of the SPi
  // The interrupt is active low on GP22
  debugf("  IRQn = %d", 22);
  // set handler but not enable yet as the main task may not be ready  
  gpio_add_raw_irq_handler(22, irq_handler);  
}

void mcu_hw_irq_ack(void) {
  static bool first = true;

  if(first) {
    debugf("enable IRQ");
    irq_set_enabled(IO_IRQ_BANK0, true);
    first = false;
  }
  
  // re-enable the interrupt since it was now serviced outside the irq handler
  gpio_set_irq_enabled(22, GPIO_IRQ_LEVEL_LOW, 1); 
}

void mcu_hw_spi_begin() {
  xSemaphoreTake(sem, 0xffffffffUL);      // wait forever
  gpio_put(PICO_DEFAULT_SPI_CSN_PIN, 0);  // Active low
}

void mcu_hw_spi_end() {
  gpio_put(PICO_DEFAULT_SPI_CSN_PIN, 1);
  xSemaphoreGive(sem);
}

unsigned char mcu_hw_spi_tx_u08(unsigned char b) {
  unsigned char retval;
  spi_write_read_blocking(spi_default, &b, &retval, 1);
  return retval;
}

/* ============================================================================================= */
/* ============                        XBOX controllers                          =============== */
/* ============================================================================================= */

void mcu_hw_init(void) {
  // default 125MHz is not appropreate. Sysclock should be multiple of 12MHz.
  set_sys_clock_khz(120000, true);
  
  stdio_init_all();    // ... so stdio can adjust its bit rate

  printf("\r\n\r\n" LOGO "           FPGA Companion for RP2040\r\n\r\n");
  printf("USB D+/D- on GP%d and GP%d\r\n", PIO_USB_DP_PIN_DEFAULT, PIO_USB_DP_PIN_DEFAULT+1);
  
  gpio_init(PICO_DEFAULT_LED_PIN);
  gpio_set_dir(PICO_DEFAULT_LED_PIN, 1);
  gpio_put(PICO_DEFAULT_LED_PIN, !PICO_DEFAULT_LED_PIN_INVERTED);

  mcu_hw_spi_init();

  tuh_init(BOARD_TUH_RHPORT);
  
  TaskHandle_t pio_usb_handle;
  xTaskCreate(pio_usb_task, "usb_task", 2048, NULL, configMAX_PRIORITIES, &pio_usb_handle);
}

#include "xinput_host.h"

usbh_class_driver_t const* usbh_app_driver_get_cb(uint8_t* driver_count){
  *driver_count = 1;
  return &usbh_xinput_driver;
}

void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance, xinputh_interface_t const* xid_itf, __attribute__((unused)) uint16_t len) {
  const xinput_gamepad_t *p = &xid_itf->pad;
  
  if (xid_itf->last_xfer_result == XFER_RESULT_SUCCESS) {
    if (xid_itf->connected && xid_itf->new_pad_data) {

      // find matching hid report
      for(int idx=0;idx<MAX_XBOX_DEVICES;idx++) {
	if(xbox_state[idx].dev_addr == dev_addr && xbox_state[idx].instance == instance) {
      
	  // build new state
	  unsigned char state =
	    ((p->wButtons & XINPUT_GAMEPAD_DPAD_UP   )?0x08:0x00) |
	    ((p->wButtons & XINPUT_GAMEPAD_DPAD_DOWN )?0x04:0x00) |
	    ((p->wButtons & XINPUT_GAMEPAD_DPAD_LEFT )?0x02:0x00) |
	    ((p->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)?0x01:0x00) |
	    ((p->wButtons & 0xf000) >> 8);
	  
	  // submit if state has changed
	  if(state != xbox_state[idx].state) {    
	    usb_debugf("XBOX Joy%d: %02x", xbox_state[idx].js_index, state);
	    
	    mcu_hw_spi_begin();
	    mcu_hw_spi_tx_u08(SPI_TARGET_HID);
	    mcu_hw_spi_tx_u08(SPI_HID_JOYSTICK);
	    mcu_hw_spi_tx_u08(xbox_state[idx].js_index);
	    mcu_hw_spi_tx_u08(state);
	    mcu_hw_spi_end();
	    
	    xbox_state[idx].state = state;
	  }
	}
      }
    }
  }
  tuh_xinput_receive_report(dev_addr, instance);
}

void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance, const xinputh_interface_t *xinput_itf) {
  usb_debugf("xbox mounted %d/%d", dev_addr, instance);

  // search for a free xbox entry
  int idx;
  for(idx=0;idx<MAX_XBOX_DEVICES && (xbox_state[idx].dev_addr != 0xff);idx++);
  if(idx != MAX_XBOX_DEVICES) {
    usb_debugf("Using XBOX entry %d", idx);
    xbox_state[idx].dev_addr = dev_addr;
    xbox_state[idx].instance = instance;
    xbox_state[idx].state = 0xff;    
    xbox_state[idx].js_index = hid_allocate_joystick();
  } else
    usb_debugf("Error, no more free XBOX entries");

  // If this is a Xbox 360 Wireless controller we need to wait for a connection packet
  // on the in pipe before setting LEDs etc. So just start getting data until a controller is connected.
  if (xinput_itf->type == XBOX360_WIRELESS && xinput_itf->connected == false) {
    tuh_xinput_receive_report(dev_addr, instance);
    return;
  }
  tuh_xinput_set_led(dev_addr, instance, 0, true);
  tuh_xinput_set_led(dev_addr, instance, 1, true);
  tuh_xinput_set_rumble(dev_addr, instance, 0, 0, true);
  tuh_xinput_receive_report(dev_addr, instance);
}

void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance) {
  usb_debugf("xbox unmounted %d/%d", dev_addr, instance);

  // find matching hid report
  for(int idx=0;idx<MAX_XBOX_DEVICES;idx++) {
    if(xbox_state[idx].dev_addr == dev_addr && xbox_state[idx].instance == instance) {
      usb_debugf("releasing %d/%d", idx, xbox_state[idx].js_index);
      xbox_state[idx].dev_addr = 0xff;
      hid_release_joystick(xbox_state[idx].js_index);
    }
  }
}

#include "hardware/watchdog.h"

void mcu_hw_reset(void) {
  debugf("HW reset");
  watchdog_reboot(0, 0, 10);

  // *((volatile uint32_t*)(PPB_BASE + 0x0ED0C)) = 0x5FA0004;
  // alternally
  //  watchdog_enable(1, 1);
  //  while(1);
}

/* ============================================================================================= */
/* ======                              FreeRTOS support                                   ====== */
/* ============================================================================================= */

void vApplicationMallocFailedHook( void ) { configASSERT( ( volatile void * ) NULL ); }
void vApplicationStackOverflowHook( TaskHandle_t pxTask, char *pcTaskName ) {
  ( void ) pcTaskName;
  ( void ) pxTask;
  
  /* Run time stack overflow checking is performed if
     configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
     function is called if a stack overflow is detected. */
  
  /* Force an assert. */
  configASSERT( ( volatile void * ) NULL );
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void ) {
  volatile size_t xFreeHeapSpace;
  
  /* This is just a trivial example of an idle hook.  It is called on each
     cycle of the idle task.  It must *NOT* attempt to block.  In this case the
     idle task just queries the amount of FreeRTOS heap that remains.  See the
     memory management section on the http://www.FreeRTOS.org web site for memory
     management options.  If there is a lot of heap memory free then the
     configTOTAL_HEAP_SIZE value in FreeRTOSConfig.h can be reduced to free up
     RAM. */
  xFreeHeapSpace = xPortGetFreeHeapSize();
  
  /* Remove compiler warning about xFreeHeapSpace being set but never used. */
  ( void ) xFreeHeapSpace;
}
/*-----------------------------------------------------------*/

void vApplicationTickHook( void ) { }

static void led_timer(__attribute__((unused)) TimerHandle_t pxTimer) {
  gpio_xor_mask( 1u << PICO_DEFAULT_LED_PIN );
}
  
void mcu_hw_main_loop(void) {
  TimerHandle_t led_timer_handle = xTimerCreate("LED timer", pdMS_TO_TICKS(200), pdTRUE,
						NULL, led_timer);
  xTimerStart(led_timer_handle, 0);
  
  /* Start the tasks and timer running. */  
  vTaskStartScheduler();
  
  /* If all is well, the scheduler will now be running, and the following
     line will never be reached.  If the following line does execute, then
     there was insufficient FreeRTOS heap memory available for the Idle and/or
     timer tasks to be created.  See the memory management section on the
     FreeRTOS web site for more details on the FreeRTOS heap
     http://www.freertos.org/a00111.html. */

  for( ;; );
}

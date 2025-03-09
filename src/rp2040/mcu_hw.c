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
#include "tusb.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#include "../debug.h"
#include "../config.h"
#include "../spi.h"

#include "../mcu_hw.h"

#ifdef WAVESHARE_RP2040_ZERO
#warning "Building for Waveshare RP2040-Zero mini board"

// the waveshare mini does not expose the default spi0 pins, so we need
// to specify them
#define SPI_RX_PIN    4
#define SPI_SCK_PIN   6
#define SPI_TX_PIN    7
#define SPI_CSN_PIN   5
#define SPI_IRQ_PIN   8
#define SPI_BUS  spi0
#define WS2812_PIN    16
#else
#warning "Building for Pi Pico and Pico(W)"

// the regular pi pico uses spi0 by default
#define SPI_RX_PIN   PICO_DEFAULT_SPI_RX_PIN
#define SPI_SCK_PIN  PICO_DEFAULT_SPI_SCK_PIN
#define SPI_TX_PIN   PICO_DEFAULT_SPI_TX_PIN
#define SPI_CSN_PIN  PICO_DEFAULT_SPI_CSN_PIN
#define SPI_IRQ_PIN  22
#define SPI_BUS  spi_default
#endif

#ifdef WS2812_PIN
#include "ws2812.pio.h"
#endif

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

#include "tusb_config.h"
#if defined(WS2812_PIN) && CFG_TUH_RPI_PIO_USB == 1
#error "WS2812B and PIO USB cannot be used simultaneously!"
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
  int16_t state_x;
  int16_t state_y;
  uint8_t state_btn_extra;
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

uint8_t byteScaleAnalog(int16_t xbox_val)
{
  // Scale the xbox value from [-32768, 32767] to [1, 255]
  // Offset by 32768 to get in range [0, 65536], then divide by 256 to get in range [1, 255]
  uint8_t scale_val = (xbox_val + 32768) / 256;
  if (scale_val == 0) return 1;
  return scale_val;
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
      else if(hid_device[idx].rep.type == REPORT_TYPE_MOUSE) {
	// switch mice to report mode
	if(!tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT)) {
	  usb_debugf("Failed to set report mode");
	  hid_device[idx].rep.report_id_present = false;
	}
      }
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
  gpio_set_irq_enabled(SPI_IRQ_PIN, GPIO_IRQ_LEVEL_LOW, false);

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
  spi_init(SPI_BUS, 20000000);
  spi_set_format(SPI_BUS, 8, SPI_CPOL_0, SPI_CPHA_1, SPI_MSB_FIRST);
  
  debugf("  MISO = %d", SPI_RX_PIN);
  gpio_set_function(SPI_RX_PIN, GPIO_FUNC_SPI);
  debugf("  SCK  = %d", SPI_SCK_PIN);
  gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
  debugf("  MOSI = %d", SPI_TX_PIN);
  gpio_set_function(SPI_TX_PIN, GPIO_FUNC_SPI);
  
  // Chip select is active-low, so we'll initialise it to a driven-high state
  debugf("  CSn  = %d", SPI_CSN_PIN);
  gpio_init(SPI_CSN_PIN);
  gpio_set_dir(SPI_CSN_PIN, GPIO_OUT);
  gpio_put(SPI_CSN_PIN, 1);

  // The interruput input isn't strictly part of the SPi
  // The interrupt is active low on GP22
  debugf("  IRQn = %d", SPI_IRQ_PIN);
  // set handler but not enable yet as the main task may not be ready
  gpio_add_raw_irq_handler(SPI_IRQ_PIN, irq_handler);
}

void mcu_hw_irq_ack(void) {
  static bool first = true;

  if(first) {
    debugf("enable IRQ");
    irq_set_enabled(IO_IRQ_BANK0, true);
    first = false;
  }
  
  // re-enable the interrupt since it was now serviced outside the irq handler
  gpio_set_irq_enabled(SPI_IRQ_PIN, GPIO_IRQ_LEVEL_LOW, 1); 
}

void mcu_hw_spi_begin() {
  xSemaphoreTake(sem, 0xffffffffUL);      // wait forever
  gpio_put(SPI_CSN_PIN, 0);  // Active low
}

void mcu_hw_spi_end() {
  gpio_put(SPI_CSN_PIN, 1);
  xSemaphoreGive(sem);
}

unsigned char mcu_hw_spi_tx_u08(unsigned char b) {
  unsigned char retval;
  spi_write_read_blocking(SPI_BUS, &b, &retval, 1);
  return retval;
}

void mcu_hw_init(void) {
  // default 125MHz is not appropreate. Sysclock should be multiple of 12MHz.

#ifdef WAVESHARE_RP2040_ZERO
  // the waveshare mini does not support SWD and we thus use a simpler (slower) UART
  set_sys_clock_khz(240000, true);
  stdio_init_all();    // ... so stdio can adjust its bit rate
  uart_set_baudrate(uart0, 460800);  
#else
  set_sys_clock_khz(120000, true); // required for RPiPico
  stdio_init_all();
  uart_set_baudrate(uart0, 921600);
#endif

  printf("\r\n\r\n" LOGO "           FPGA Companion for RP2040\r\n\r\n");
#if CFG_TUH_RPI_PIO_USB == 0
  printf("Using native USB\r\n");
#else
  printf("USB D+/D- on GP%d and GP%d\r\n", PIO_USB_DP_PIN_DEFAULT, PIO_USB_DP_PIN_DEFAULT+1);
#endif

#ifdef WS2812_PIN
  uint offset = pio_add_program(pio0, &ws2812_program);  
  ws2812_program_init(pio0, 0, offset, WS2812_PIN, 800000, 0);
#else
  gpio_init(PICO_DEFAULT_LED_PIN);
  gpio_set_dir(PICO_DEFAULT_LED_PIN, 1);
  gpio_put(PICO_DEFAULT_LED_PIN, !PICO_DEFAULT_LED_PIN_INVERTED);
#endif
  
  mcu_hw_spi_init();

  tuh_init(BOARD_TUH_RHPORT);
  
  TaskHandle_t pio_usb_handle;
  xTaskCreate(pio_usb_task, "usb_task", 2048, NULL, configMAX_PRIORITIES, &pio_usb_handle);
}

/* ============================================================================================= */
/* ============                        XBOX controllers                          =============== */
/* ============================================================================================= */

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

    // build extra button new state
    unsigned char state_btn_extra =
	    ((p->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER  )?0x01:0x00) |
	    ((p->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER )?0x02:0x00) |
	    ((p->wButtons & XINPUT_GAMEPAD_BACK           )?0x10:0x00) | // Rumblepad 2 / Dual Action compatibility
	    ((p->wButtons & XINPUT_GAMEPAD_START          )?0x20:0x00);

	  // build analog stick x,y state
      int16_t sThumbLX = p->sThumbLX;
      int16_t sThumbLY = p->sThumbLY;
      uint8_t ax = byteScaleAnalog(sThumbLX);
      uint8_t ay = ~byteScaleAnalog(sThumbLY);

    // map analog stick directions to digital
    if(ax > (uint8_t) 0xc0) state |= 0x01;
    if(ax < (uint8_t) 0x40) state |= 0x02;
    if(ay > (uint8_t) 0xc0) state |= 0x04;
    if(ay < (uint8_t) 0x40) state |= 0x08;

    // submit if state has changed
	  if((state != xbox_state[idx].state) ||
      (state_btn_extra != xbox_state[idx].state_btn_extra) ||
      (ax != xbox_state[idx].state_x) ||
      (ay != xbox_state[idx].state_y)) {

      xbox_state[idx].state = state;
      xbox_state[idx].state_btn_extra = state_btn_extra;
      xbox_state[idx].state_x = sThumbLX;
      xbox_state[idx].state_y = sThumbLY;
      usb_debugf("XBOX Joy%d: B %02x EB %02x X %02x Y %02x", xbox_state[idx].js_index, state, state_btn_extra, byteScaleAnalog(ax), byteScaleAnalog(ay));

	    mcu_hw_spi_begin();
	    mcu_hw_spi_tx_u08(SPI_TARGET_HID);
	    mcu_hw_spi_tx_u08(SPI_HID_JOYSTICK);
	    mcu_hw_spi_tx_u08(xbox_state[idx].js_index);
	    mcu_hw_spi_tx_u08(state);
	    mcu_hw_spi_tx_u08(ax); // gamepad analog X
	    mcu_hw_spi_tx_u08(ay); // gamepad analog Y
	    mcu_hw_spi_tx_u08(state_btn_extra); // gamepad extra buttons
	    mcu_hw_spi_end();
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
    xbox_state[idx].state = 0;
    xbox_state[idx].state_btn_extra = 0;
    xbox_state[idx].state_x = 0;
    xbox_state[idx].state_y = 0;
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

#ifdef WS2812_PIN
static void led_timer(__attribute__((unused)) TimerHandle_t pxTimer) {
  static char state = 0;
  pio_sm_put_blocking(pio0, 0, state?0x40000000:0x00000000);  // GRBX
  state = !state;
}
#else
static void led_timer(__attribute__((unused)) TimerHandle_t pxTimer) {
  gpio_xor_mask( 1u << PICO_DEFAULT_LED_PIN );
}
#endif

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

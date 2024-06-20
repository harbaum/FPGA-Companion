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

#include "../debug.h"
#include "../config.h"

#include "../mcu_hw.h"

/* ============================================================================================= */
/* ===============                          USB                                   ============== */
/* ============================================================================================= */
#include "tusb.h"
#include "../hid.h"
#include "../hidparser.h"

static struct {
  uint8_t dev_addr;
  uint8_t instance;
  hid_state_t state;
  hid_report_t rep;
} hid_report[MAX_HID_DEVICES];

static void pio_usb_task(__attribute__((unused)) void *parms) {
  // mark all hid entries as unused
  for(int i=0;i<MAX_HID_DEVICES;i++)
    hid_report[i].dev_addr = 0xff;

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
  for(idx=0;idx<MAX_HID_DEVICES && (hid_report[idx].dev_addr != 0xff);idx++);
  if(idx != MAX_HID_DEVICES) {
    usb_debugf("Using HID entry %d", idx);
    
    parse_report_descriptor(desc_report, desc_len, &hid_report[idx].rep, NULL);
    hid_report[idx].dev_addr = dev_addr;
    hid_report[idx].instance = instance;
  } else
    usb_debugf("Error, no more free HID entries");
  
  // tuh_hid_report_received_cb() will be invoked when report is available
  if ( !tuh_hid_receive_report(dev_addr, instance) ) 
    usb_debugf("Error: cannot request report");
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  usb_debugf("[%u] HID Interface%u is unmounted", dev_addr, instance);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  usb_debugf("[%u] HID Interface%u", dev_addr, instance);

  // find matching hid report
  for(int idx=0;idx<MAX_HID_DEVICES;idx++)
    if(hid_report[idx].dev_addr == dev_addr && hid_report[idx].instance == instance)     
      hid_parse(&hid_report[idx].rep, &hid_report[idx].state, report, len);
  
  // continue to request to receive report
  if ( !tuh_hid_receive_report(dev_addr, instance) )
    usb_debugf("Error: cannot request report");
}

void mcu_hw_launch(void) {  
}

/* ============================================================================================= */
/* ===============                          SPI                                   ============== */
/* ============================================================================================= */
#include "pico/binary_info.h"
#include "hardware/spi.h"
#include "queue.h"

extern TaskHandle_t com_task_handle;
static SemaphoreHandle_t sem;

static void irq_handler(__attribute__((unused)) unsigned int gpio,
			__attribute__((unused)) uint32_t event_mask) {
  // debugf("IRQ %u %lu", gpio, event_mask);
  // Disable interrupt. It will be re-enabled by the com task
  gpio_set_irq_enabled(22, GPIO_IRQ_LEVEL_LOW, false);

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR( com_task_handle, &xHigherPriorityTaskWoken );
  portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
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
  gpio_set_irq_enabled_with_callback(22, GPIO_IRQ_LEVEL_LOW, 1, irq_handler);
}

void mcu_hw_irq_ack(void) {
  // re-enable the interrupt since it was now serviced outside the irq handeler
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
/* ============================================================================================= */

void mcu_hw_init(void) {
  // default 125MHz is not appropreate. Sysclock should be multiple of 12MHz.
  set_sys_clock_khz(120000, true);
  
  stdio_init_all();    // ... so stdio can adjust its bit rate
  
  printf("\r\n\r\n"
	 "=============================================================\r\n"
	 "========      MiSTeryNano FPGA companion for Pico    ========\r\n"
	 "=============================================================\r\n");
  printf("USB D+/D- on GP%d and GP%d\r\n", PIO_USB_DP_PIN_DEFAULT, PIO_USB_DP_PIN_DEFAULT+1);
  
  gpio_init(PICO_DEFAULT_LED_PIN);
  gpio_set_dir(PICO_DEFAULT_LED_PIN, 1);
  gpio_put(PICO_DEFAULT_LED_PIN, !PICO_DEFAULT_LED_PIN_INVERTED);

  mcu_hw_spi_init();

  tuh_init(BOARD_TUH_RHPORT);
  
  TaskHandle_t pio_usb_handle;
  xTaskCreate(pio_usb_task, "usb_task", 2048, NULL, configMAX_PRIORITIES, &pio_usb_handle);
}

void mcu_hw_reset(void) {
  debugf("HW reset");
  sleep_ms(10);   // give uart some time to transmit
  *((volatile uint32_t*)(PPB_BASE + 0x0ED0C)) = 0x5FA0004;

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

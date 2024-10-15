/*
  mcu_hw.c - bl616 hardware driver
*/

#include <FreeRTOS.h>
#include "mem.h"

#include "usbh_core.h"
#include "usbh_hid.h"

#include "../spi.h"
#include "../hid.h"
#include "../sdc.h"
#include "../sysctrl.h"
#include "../debug.h"
#include "../mcu_hw.h"

extern uint32_t __HeapBase;
extern uint32_t __HeapLimit;

#include "bl616_glb.h"

#include "bflb_mtimer.h"
#include "bflb_spi.h"
#include "bflb_dma.h"
#include "bflb_gpio.h"
#include "bflb_wdg.h"
#include "bflb_uart.h"
#include "bflb_clock.h"
#include "bflb_flash.h"
#include "bflb_clock.h"

static struct bflb_device_s *gpio;

/* ============================================================================================= */
/* ===============                          USB                                   ============== */
/* ============================================================================================= */

// usb_host.c

#include <queue.h>
#include <hardware/bl616.h>

#define MAX_REPORT_SIZE   8
#define XBOX_REPORT_SIZE 20

#define STATE_NONE      0 
#define STATE_DETECTED  1 
#define STATE_RUNNING   2
#define STATE_FAILED    3

extern struct bflb_device_s *gpio;

void set_led(int pin, int on) {
  // only M0S dock has those leds
  if(on) bflb_gpio_reset(gpio, pin);
  else   bflb_gpio_set(gpio, pin);
}

static struct usb_config {
  struct xbox_info_S {
    int index;
    int state;
    struct usbh_hid *class;
    uint8_t *buffer;
    struct usb_config *usb;
    SemaphoreHandle_t sem;
    TaskHandle_t task_handle;    
    unsigned char last_state;
    unsigned char js_index;
    int state_btn_extra;
    unsigned char last_state_btn_extra;
#ifdef RATE_CHECK
    TickType_t rate_start;
    unsigned long rate_events;
#endif    
  } xbox_info[CONFIG_USBHOST_MAX_XBOX_CLASS];
    
  struct hid_info_S {
    int index;
    int state;
    struct usbh_hid *class;
    uint8_t *buffer;
    int nbytes;
    hid_report_t report;
    struct usb_config *usb;
    SemaphoreHandle_t sem;
    TaskHandle_t task_handle;    
#ifdef RATE_CHECK
    TickType_t rate_start;
    unsigned long rate_events;
#endif
    hid_state_t hid_state;
  } hid_info[CONFIG_USBHOST_MAX_HID_CLASS];
} usb_config;
  
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t hid_buffer[CONFIG_USBHOST_MAX_HID_CLASS][MAX_REPORT_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t xbox_buffer[CONFIG_USBHOST_MAX_XBOX_CLASS][XBOX_REPORT_SIZE];

void usbh_hid_callback(void *arg, int nbytes) {
  struct hid_info_S *hid = (struct hid_info_S *)arg;

  xSemaphoreGiveFromISR(hid->sem, NULL);
  hid->nbytes = nbytes;
}  

void usbh_xbox_callback(void *arg, int nbytes) {
  struct xbox_info_S *xbox = (struct xbox_info_S *)arg;
  if(nbytes == XBOX_REPORT_SIZE)
    xSemaphoreGiveFromISR(xbox->sem, NULL);
}  

static void usbh_update(struct usb_config *usb) {
  // check for active hid devices
  for(int i=0;i<CONFIG_USBHOST_MAX_HID_CLASS;i++) {
    char *dev_str = "/dev/inputX";
    dev_str[10] = '0' + i;
    usb->hid_info[i].class = (struct usbh_hid *)usbh_find_class_instance(dev_str);
    
    if(usb->hid_info[i].class && usb->hid_info[i].state == STATE_NONE) {
      usb_debugf("NEW HID %d", i);

      usb_debugf("Interval: %d", usb->hid_info[i].class->hport->config.intf[i].altsetting[0].ep[0].ep_desc.bInterval);
	 
      usb_debugf("Interface %d", usb->hid_info[i].class->intf);
      usb_debugf("  class %d", usb->hid_info[i].class->hport->config.intf[i].altsetting[0].intf_desc.bInterfaceClass);
      usb_debugf("  subclass %d", usb->hid_info[i].class->hport->config.intf[i].altsetting[0].intf_desc.bInterfaceSubClass);
      usb_debugf("  protocol %d", usb->hid_info[i].class->hport->config.intf[i].altsetting[0].intf_desc.bInterfaceProtocol);
	
      // parse report descriptor ...
      usb_debugf("report descriptor: %p", usb->hid_info[i].class->report_desc);
      
      if(!parse_report_descriptor(usb->hid_info[i].class->report_desc, 128, &usb->hid_info[i].report, NULL)) {
	usb->hid_info[i].state = STATE_FAILED;   // parsing failed, don't use
	return;
      }
      
      usb->hid_info[i].state = STATE_DETECTED;
    }
    
    else if(!usb->hid_info[i].class && usb->hid_info[i].state != STATE_NONE) {
      usb_debugf("HID LOST %d", i);
      vTaskDelete( usb->hid_info[i].task_handle );
      usb->hid_info[i].state = STATE_NONE;

      if(usb->hid_info[i].report.type == REPORT_TYPE_JOYSTICK) {
	usb_debugf("Joystick %d gone", usb->hid_info[i].hid_state.joystick.js_index);
	hid_release_joystick(usb->hid_info[i].hid_state.joystick.js_index);
      }
    }
  }

  // check for active xbox devices
  for(int i=0;i<CONFIG_USBHOST_MAX_XBOX_CLASS;i++) {
    char *dev_str = "/dev/xboxX";
    dev_str[9] = '0' + i;
    usb->xbox_info[i].class = (struct usbh_hid *)usbh_find_class_instance(dev_str);
    
    if(usb->xbox_info[i].class && usb->xbox_info[i].state == STATE_NONE) {
      usb_debugf("NEW XBOX %d", i);

      usb_debugf("Interval: %d", usb->xbox_info[i].class->hport->config.intf[i].altsetting[0].ep[0].ep_desc.bInterval);
	 
      usb_debugf("Interface %d", usb->xbox_info[i].class->intf);
      usb_debugf("  class %d", usb->xbox_info[i].class->hport->config.intf[i].altsetting[0].intf_desc.bInterfaceClass);
      usb_debugf("  subclass %d", usb->xbox_info[i].class->hport->config.intf[i].altsetting[0].intf_desc.bInterfaceSubClass);
      usb_debugf("  protocol %d", usb->xbox_info[i].class->hport->config.intf[i].altsetting[0].intf_desc.bInterfaceProtocol);
	
      usb->xbox_info[i].state = STATE_DETECTED;
    }
    
    else if(!usb->xbox_info[i].class && usb->xbox_info[i].state != STATE_NONE) {
      usb_debugf("XBOX %d", i);
      vTaskDelete( usb->xbox_info[i].task_handle );
      usb->xbox_info[i].state = STATE_NONE;
      
      usb_debugf("Joystick %d gone", usb->xbox_info[i].js_index);
      hid_release_joystick(usb->xbox_info[i].js_index);
    }
  }

  // check for number of mice and keyboards and update leds
  int mice = 0, keyboards = 0;  
  for(int i=0;i<CONFIG_USBHOST_MAX_HID_CLASS;i++) {
    if(usb->hid_info[i].state == STATE_RUNNING) {
      if(usb->hid_info[i].report.type == REPORT_TYPE_MOUSE)    mice++;
      if(usb->hid_info[i].report.type == REPORT_TYPE_KEYBOARD) keyboards++;      
    }
  }

  extern void set_led(int pin, int on);
  set_led(GPIO_PIN_27, mice);
  set_led(GPIO_PIN_28, keyboards);
}

static void xbox_parse(struct xbox_info_S *xbox) {
#if 0
  USB_LOG_RAW("XBOX%d: ", xbox->index);
  
  // just dump the report
  for (size_t i = 0; i < 20; i++) 
    USB_LOG_RAW("0x%02x ", xbox->buffer[i]);
  USB_LOG_RAW("");
#endif

  // verify length field
  if(xbox->buffer[0] != 0 || xbox->buffer[1] != 20)
    return;

  /*
  needed:
  dpright       0x0001
  dpleft        0x0002
  dpdown        0x0004
  dpup          0x0008
  b             0x0010
  a             0x0020
  y             0x0040
  x             0x0080
  leftshoulder  0x0100
  rightshoulder 0x0200
  back          0x0400
  start         0x0800

  //https://docs.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_gamepad
  DPAD_UP 0x0001
  DPAD_DOWN 0x0002
  DPAD_LEFT 0x0004
  DPAD_RIGHT 0x0008
  START 0x0010
  BACK 0x0020
  LEFT_THUMB 0x0040
  RIGHT_THUMB 0x0080
  //
  LEFT_SHOULDER 0x0100
  RIGHT_SHOULDER 0x0200
  GUIDE 0x0400
  SHARE 0x0800
  A 0x1000
  B 0x2000
  X 0x4000
  Y 0x8000
  */

  // the xbox controller sends the direction bits in exactly the
  // reversed order than we expect ...
  unsigned char state =
    ((xbox->buffer[2] & 0x01)<<3) | ((xbox->buffer[2] & 0x02)<<1) |
    ((xbox->buffer[2] & 0x04)>>1) | ((xbox->buffer[2] & 0x08)>>3) |
    (xbox->buffer[3] & 0xf0);  // A, B, X, Y
  
  unsigned char state_btn_extra = 
    xbox->buffer[2] & 0xf0; //

  // submit if state has changed
  if(state != xbox->last_state || state_btn_extra != xbox->last_state_btn_extra ) {
    
    usb_debugf("XBOX Joy%d: %02x", xbox->js_index, state);
  
    mcu_hw_spi_begin();
    mcu_hw_spi_tx_u08(SPI_TARGET_HID);
    mcu_hw_spi_tx_u08(SPI_HID_JOYSTICK);
    mcu_hw_spi_tx_u08(xbox->js_index);
    mcu_hw_spi_tx_u08(state);
    mcu_hw_spi_tx_u08(0); // gamepad analog X
    mcu_hw_spi_tx_u08(0); // gamepad analog Y
    mcu_hw_spi_tx_u08(state_btn_extra); // gamepad extra buttons
    mcu_hw_spi_end();
    
    xbox->last_state = state;
    xbox->last_state_btn_extra = state_btn_extra;
  }
}

// each HID client gets itws own thread which submits urbs
// and waits for the interrupt to succeed
static void usbh_hid_client_thread(void *argument) {
  struct hid_info_S *hid = (struct hid_info_S *)argument;

  usb_debugf("HID client #%d: thread started", hid->index);

  while(1) {
    int ret = usbh_submit_urb(&hid->class->intin_urb);
    if (ret < 0)
      usb_debugf("HID client #%d: submit failed", hid->index);
    else {
      // Wait for result
      xSemaphoreTake(hid->sem, 0xffffffffUL);
      if(hid->nbytes > 0)
	hid_parse(&hid->report, &hid->hid_state, hid->buffer, hid->nbytes);
      
      hid->nbytes = 0;
    }      

#ifdef RATE_CHECK
    hid->rate_events++;
    if(!(hid->rate_events % 100)) {
     float ms_since_start = (xTaskGetTickCount() - hid->rate_start) * portTICK_PERIOD_MS;
     usb_debugf("Rate = %f events/sec",  1000 * hid->rate_events /  ms_since_start);
    }    
#endif
  }
}

// ... and XBOX clients as well
static void usbh_xbox_client_thread(void *argument) {
  struct xbox_info_S *xbox = (struct xbox_info_S *)argument;

  usb_debugf("XBOX client #%d: thread started", xbox->index);

  while(1) {
    int ret = usbh_submit_urb(&xbox->class->intin_urb);
    if (ret < 0)
      usb_debugf("XBOX client #%d: submit failed", xbox->index);
    else {
      // Wait for result
      xSemaphoreTake(xbox->sem, 0xffffffffUL);
      xbox_parse(xbox);
    }      

#ifdef RATE_CHECK
    xbox->rate_events++;
    if(!(xbox->rate_events % 100)) {
     float ms_since_start = (xTaskGetTickCount() - xbox->rate_start) * portTICK_PERIOD_MS;
     usb_debugf("Rate = %f events/sec",  1000 * xbox->rate_events /  ms_since_start);
    }    
#endif
  }
}

static void usbh_hid_thread(void *argument) {
  usb_debugf("Starting usb host task...");

  struct usb_config *usb = (struct usb_config *)argument;

  // request status (currently only dummy data, will return 0x5c, 0x42)
  // in the long term the core is supposed to return its HID demands
  // (keyboard matrix type, joystick type and number, ...)
  
  mcu_hw_spi_begin();
  mcu_hw_spi_tx_u08(SPI_TARGET_HID);
  mcu_hw_spi_tx_u08(SPI_HID_STATUS);
  mcu_hw_spi_tx_u08(0x00);
  usb_debugf("HID status #0: %02x", mcu_hw_spi_tx_u08(0x00));
  usb_debugf("HID status #1: %02x", mcu_hw_spi_tx_u08(0x00));
  mcu_hw_spi_end();

  while (1) {
    usbh_update(usb);

    for(int i=0;i<CONFIG_USBHOST_MAX_HID_CLASS;i++) {
      if(usb->hid_info[i].state == STATE_DETECTED) {
	usb_debugf("NEW HID device %d", i);
	usb->hid_info[i].state = STATE_RUNNING; 

	if( usb->hid_info[i].report.type == REPORT_TYPE_JOYSTICK ) {	
	  usb->hid_info[i].hid_state.joystick.js_index = hid_allocate_joystick();
	  usb_debugf("  -> joystick %d", usb->hid_info[i].hid_state.joystick.js_index);
	}
	  
#if 0
	// set report protocol 1 if subclass != BOOT_INTF
	// CherryUSB doesn't report the InterfaceSubClass (HID_BOOT_INTF_SUBCLASS)
	// we thus set boot protocol on keyboards
	if( usb->hid_info[i].report.type == REPORT_TYPE_KEYBOARD ) {	
	  // /* 0x0 = boot protocol, 0x1 = report protocol */
	  usb_debugf("setting boot protocol");
	  ret = usbh_hid_set_protocol(usb->hid_info[i].class, HID_PROTOCOL_BOOT);
	  if (ret < 0) {
	    usb_debugf("failed");
	    usb->hid_info[i].state = STATE_FAILED;  // failed
	    continue;
	  }
	}
#endif

	// setup urb
	usbh_int_urb_fill(&usb->hid_info[i].class->intin_urb,
			  usb->hid_info[i].class->hport,
			  usb->hid_info[i].class->intin, usb->hid_info[i].buffer,
			  usb->hid_info[i].report.report_size + (usb->hid_info[i].report.report_id_present ? 1:0),
			  0, usbh_hid_callback, &usb->hid_info[i]);

#ifdef RATE_CHECK
	usb->hid_info[i].rate_start = xTaskGetTickCount();
	usb->hid_info[i].rate_events = 0;
#endif
	
	// start a new thread for the new device
	xTaskCreate(usbh_hid_client_thread, (char *)"hid_task", 1024,
		    &usb->hid_info[i], configMAX_PRIORITIES-3, &usb->hid_info[i].task_handle );
      }
    }
    
    for(int i=0;i<CONFIG_USBHOST_MAX_XBOX_CLASS;i++) {
      if(usb->xbox_info[i].state == STATE_DETECTED) {
	usb_debugf("NEW XBOX device %d", i);
	usb->xbox_info[i].state = STATE_RUNNING; 

	// search for free joystick slot
	usb->xbox_info[i].js_index = hid_allocate_joystick();
	usb_debugf("  -> joystick %d", usb->xbox_info[i].js_index);
	
	// setup urb
	usbh_int_urb_fill(&usb->xbox_info[i].class->intin_urb,
			  usb->xbox_info[i].class->hport,
			  usb->xbox_info[i].class->intin, usb->xbox_info[i].buffer,
			  XBOX_REPORT_SIZE,
			  0, usbh_xbox_callback, &usb->xbox_info[i]);
	
#ifdef RATE_CHECK
	usb->xbox_info[i].rate_start = xTaskGetTickCount();
	usb->xbox_info[i].rate_events = 0;
#endif

	// start a new thread for the new device
	xTaskCreate(usbh_xbox_client_thread, (char *)"xbox_task", 2048,
		    &usb->xbox_info[i], configMAX_PRIORITIES-3, &usb->xbox_info[i].task_handle );
      }
    }

    // this thread only handles new devices and thus doesn't have to run very
    // often
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void usb_host(void) {
  TaskHandle_t usb_handle;

  usb_debugf("init usb hid host");

  usbh_initialize(0, USB_BASE);
  
  // initialize all HID info entries
  for(int i=0;i<CONFIG_USBHOST_MAX_HID_CLASS;i++) {
    usb_config.hid_info[i].index = i;
    usb_config.hid_info[i].state = 0;
    usb_config.hid_info[i].buffer = hid_buffer[i];      
    usb_config.hid_info[i].usb = &usb_config;
    usb_config.hid_info[i].sem = xSemaphoreCreateBinary();
  }
  
  // initialize all XBOX info entries
  for(int i=0;i<CONFIG_USBHOST_MAX_XBOX_CLASS;i++) {
    usb_config.xbox_info[i].index = i;
    usb_config.xbox_info[i].state = 0;
    usb_config.xbox_info[i].buffer = xbox_buffer[i];      
    usb_config.xbox_info[i].usb = &usb_config;
    usb_config.xbox_info[i].sem = xSemaphoreCreateBinary();
  }
  xTaskCreate(usbh_hid_thread, (char *)"usb_task", 2048, &usb_config, configMAX_PRIORITIES-3, &usb_handle);
}

/* ============================================================================================= */
/* ===============                          SPI                                   ============== */
/* ============================================================================================= */

extern TaskHandle_t com_task_handle;
static SemaphoreHandle_t spi_sem;
static struct bflb_device_s *spi_dev;

#define SPI_PIN_CSN   GPIO_PIN_12
#define SPI_PIN_SCK   GPIO_PIN_13
#define SPI_PIN_MISO  GPIO_PIN_10
#define SPI_PIN_MOSI  GPIO_PIN_11
#define SPI_PIN_IRQ   GPIO_PIN_14

void spi_isr(uint8_t pin) {
  if (pin == SPI_PIN_IRQ) {
    // disable further interrupts until thread has processed the current message
    bflb_irq_disable(gpio->irq_num);

    if(com_task_handle) {    
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      vTaskNotifyGiveFromISR( com_task_handle, &xHigherPriorityTaskWoken );
      portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
  }
}

static void mcu_hw_spi_init(void) {
  // when FPGA sets data on rising edge:
  // stable with long cables up to 20Mhz
  // short cables up to 32MHz

  /* spi miso */
  bflb_gpio_init(gpio, SPI_PIN_MISO, GPIO_FUNC_SPI0 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1);
  /* spi mosi */
  bflb_gpio_init(gpio, SPI_PIN_MOSI, GPIO_FUNC_SPI0 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1);
  /* spi clk */
  bflb_gpio_init(gpio, SPI_PIN_SCK, GPIO_FUNC_SPI0 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1);
  /* spi cs */
  bflb_gpio_init(gpio, SPI_PIN_CSN, GPIO_OUTPUT | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1);
  bflb_gpio_set(gpio, SPI_PIN_CSN);

  struct bflb_spi_config_s spi_cfg = {
    .freq = 20000000,   // 20MHz
    .role = SPI_ROLE_MASTER,
    .mode = SPI_MODE1,         // mode 1: idle state low, data sampled on falling edge
    .data_width = SPI_DATA_WIDTH_8BIT,
    .bit_order = SPI_BIT_MSB,
    .byte_order = SPI_BYTE_LSB,
    .tx_fifo_threshold = 0,
    .rx_fifo_threshold = 0,
  };
  
  spi_dev = bflb_device_get_by_name("spi0");
  bflb_spi_init(spi_dev, &spi_cfg);

  bflb_spi_feature_control(spi_dev, SPI_CMD_SET_DATA_WIDTH, SPI_DATA_WIDTH_8BIT);

  // semaphore to access the spi bus
  spi_sem = xSemaphoreCreateMutex();

  /* interrupt input */
  bflb_irq_disable(gpio->irq_num);
  bflb_gpio_init(gpio, SPI_PIN_IRQ, GPIO_INPUT | GPIO_PULLUP | GPIO_SMT_EN);
  bflb_gpio_int_init(gpio, SPI_PIN_IRQ, GPIO_INT_TRIG_MODE_SYNC_LOW_LEVEL);
  bflb_gpio_irq_attach(SPI_PIN_IRQ, spi_isr);
}

// spi may be used by different threads. Thus begin and end are using
// semaphores

void mcu_hw_spi_begin(void) {
  xSemaphoreTake(spi_sem, 0xffffffffUL); // wait forever
  bflb_gpio_reset(gpio, SPI_PIN_CSN);
}

unsigned char mcu_hw_spi_tx_u08(unsigned char b) {
  return bflb_spi_poll_send(spi_dev, b);
}

void mcu_hw_spi_end(void) {
  bflb_gpio_set(gpio, SPI_PIN_CSN);
  xSemaphoreGive(spi_sem);
}

void mcu_hw_irq_ack(void) {
  // re-enable the interrupt since it was now serviced outside the irq handeler
  bflb_irq_enable(gpio->irq_num);   // resume interrupt processing
}

/* ============================================================================================= */
/* ============================================================================================= */

extern void log_start(void);
extern void bl_show_flashinfo(void);
extern void bl_show_log(void);
extern void bflb_uart_set_console(struct bflb_device_s *dev);

// the M0S uses max bitrate as I use another M0S for console debug which can
// deal with 2MBit/s
#define CONSOLE_BAUDRATE 2000000

static struct bflb_device_s *uart0;

static void system_clock_init(void) {
  /* wifipll/audiopll */
  GLB_Power_On_XTAL_And_PLL_CLK(GLB_XTAL_40M, GLB_PLL_WIFIPLL | GLB_PLL_AUPLL);
  GLB_Set_MCU_System_CLK(GLB_MCU_SYS_CLK_TOP_WIFIPLL_320M);
  CPU_Set_MTimer_CLK(ENABLE, BL_MTIMER_SOURCE_CLOCK_MCU_XCLK, Clock_System_Clock_Get(BL_SYSTEM_CLOCK_XCLK) / 1000000 - 1);
}

/* TODO: disabled everything we don't need or use */
static void peripheral_clock_init(void) {
  PERIPHERAL_CLOCK_ADC_DAC_ENABLE();
  PERIPHERAL_CLOCK_SEC_ENABLE();
  PERIPHERAL_CLOCK_DMA0_ENABLE();
  PERIPHERAL_CLOCK_UART0_ENABLE();
  PERIPHERAL_CLOCK_UART1_ENABLE();
  PERIPHERAL_CLOCK_SPI0_ENABLE();
  PERIPHERAL_CLOCK_I2C0_ENABLE();
  PERIPHERAL_CLOCK_PWM0_ENABLE();
  PERIPHERAL_CLOCK_TIMER0_1_WDG_ENABLE();
  PERIPHERAL_CLOCK_IR_ENABLE();
  PERIPHERAL_CLOCK_I2S_ENABLE();
  PERIPHERAL_CLOCK_USB_ENABLE();
  PERIPHERAL_CLOCK_CAN_ENABLE();
  PERIPHERAL_CLOCK_AUDIO_ENABLE();
  PERIPHERAL_CLOCK_CKS_ENABLE();
    
  GLB_Set_UART_CLK(ENABLE, HBN_UART_CLK_XCLK, 0);
  GLB_Set_SPI_CLK(ENABLE, GLB_SPI_CLK_MCU_MUXPLL_160M, 0);
  GLB_Set_DBI_CLK(ENABLE, GLB_SPI_CLK_MCU_MUXPLL_160M, 0);
  GLB_Set_I2C_CLK(ENABLE, GLB_I2C_CLK_XCLK, 0);
  GLB_Set_ADC_CLK(ENABLE, GLB_ADC_CLK_XCLK, 1);
  GLB_Set_DIG_CLK_Sel(GLB_DIG_CLK_XCLK);
  GLB_Set_DIG_512K_CLK(ENABLE, ENABLE, 0x4E);
  GLB_Set_PWM1_IO_Sel(GLB_PWM1_IO_SINGLE_END);
  GLB_Set_IR_CLK(ENABLE, GLB_IR_CLK_SRC_XCLK, 19);
  GLB_Set_CAM_CLK(ENABLE, GLB_CAM_CLK_WIFIPLL_96M, 3);
  
  GLB_Set_PKA_CLK_Sel(GLB_PKA_CLK_MCU_MUXPLL_160M);
  
  GLB_Set_USB_CLK_From_WIFIPLL(1);
  GLB_Swap_MCU_SPI_0_MOSI_With_MISO(0);
}

static void console_init() {
  struct bflb_device_s *gpio;
  
  gpio = bflb_device_get_by_name("gpio");
  
  // M0S Dock has debug uart on default pins 21 and 22
  bflb_gpio_uart_init(gpio, GPIO_PIN_21, GPIO_UART_FUNC_UART0_TX);
  bflb_gpio_uart_init(gpio, GPIO_PIN_22, GPIO_UART_FUNC_UART0_RX);
  
  struct bflb_uart_config_s cfg;
  cfg.baudrate = CONSOLE_BAUDRATE;
  cfg.data_bits = UART_DATA_BITS_8;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.parity = UART_PARITY_NONE;
  cfg.flow_ctrl = 0;
  cfg.tx_fifo_threshold = 7;
  cfg.rx_fifo_threshold = 7;
  
  uart0 = bflb_device_get_by_name("uart0");
  
  bflb_uart_init(uart0, &cfg);
  bflb_uart_set_console(uart0);
}    

// local board_init used as a replacemement for global board_init
static void mn_board_init(void) {
    int ret = -1;
    uintptr_t flag;
    size_t heap_len;

    flag = bflb_irq_save();
#ifndef CONFIG_PSRAM_COPY_CODE
    ret = bflb_flash_init();
#endif
    system_clock_init();
    peripheral_clock_init();
    bflb_irq_initialize();

    console_init();

    heap_len = ((size_t)&__HeapLimit - (size_t)&__HeapBase);
    kmem_init((void *)&__HeapBase, heap_len);

    bl_show_log();
    if (ret != 0) {
        printf("flash init fail!!!\r\n");
    }
    bl_show_flashinfo();

    printf("dynamic memory init success, ocram heap size = %d Kbyte \r\n", ((size_t)&__HeapLimit - (size_t)&__HeapBase) / 1024);

    printf("sig1:%08x\r\n", BL_RD_REG(GLB_BASE, GLB_UART_CFG1));
    printf("sig2:%08x\r\n", BL_RD_REG(GLB_BASE, GLB_UART_CFG2));
    printf("cgen1:%08x\r\n", getreg32(BFLB_GLB_CGEN1_BASE));

    log_start();

#if (defined(CONFIG_LUA) || defined(CONFIG_BFLOG) || defined(CONFIG_FATFS))
    // we use FATFS ... so the RTC should be set ...    
    // rtc = bflb_device_get_by_name("rtc");
#endif
    bflb_irq_restore(flag);
}

void mcu_hw_init(void) {
  mn_board_init();
  gpio = bflb_device_get_by_name("gpio");

  printf("\r\n\r\n" LOGO "           FPGA Companion for BL616\r\n\r\n");

  // init on-board LEDs
  bflb_gpio_init(gpio, GPIO_PIN_27, GPIO_OUTPUT | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_0);
  bflb_gpio_init(gpio, GPIO_PIN_28, GPIO_OUTPUT | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_0);
  
  // both leds off
  bflb_gpio_set(gpio, GPIO_PIN_27);
  bflb_gpio_set(gpio, GPIO_PIN_28);
  
  // button
  bflb_gpio_init(gpio, GPIO_PIN_2, GPIO_INPUT | GPIO_PULLDOWN | GPIO_SMT_EN | GPIO_DRV_0);
 
  mcu_hw_spi_init();
  
  usb_host();
}

void mcu_hw_reset(void) {
  debugf("HW reset");
  
  bflb_mtimer_delay_ms(1000);
  GLB_SW_POR_Reset(); 
}

void mcu_hw_main_loop(void) {
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

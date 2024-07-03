/*
  main.c - MiSTeryNano FPGA Companion Pi Pico variant

*/

#include "../mcu_hw.h"

#include "../config.h"
#include "../sysctrl.h"
#include "../sdc.h"
#include "../osd.h"
#include "../menu.h"
#include "../core.h"
#include "../inifile.h"
#include "../debug.h"
#include "../xml.h"

/*-----------------------------------------------------------*/
/*---            main FPGA communication task            ----*/
/*-----------------------------------------------------------*/

TaskHandle_t com_task_handle;

static void com_task(__attribute__((unused)) void *p ) {
  debugf("Starting main communication task");
  
  // startup FPGA, this will also put the core into reset
  if(sys_wait4fpga()) {
    // FPGA is ready and can be talked to

    // initialitze SD card
    sdc_init();
    
    // try to load a config .xml from sd card. If the core has identified itself,
    // then e.g. atarist.xml will be read. otherwise config.xml
    FIL fil;
    if(f_open(&fil, sys_get_config_name(), FA_OPEN_EXISTING | FA_READ) == FR_OK) {
      config_init();

      UINT br; char c;
      debugf("XML config open");

      // read byte by byte. Slow but that doesn't hurt ...
      FRESULT r = f_read(&fil, &c, 1, &br);
      while(r == FR_OK && br) {
	xml_parse(c);      
	r = f_read(&fil, &c, 1, &br);
      }    
      f_close(&fil);

      config_dump();
    } 

    // process any pending interrupt. Filter out irq 1 which is the
    // FPGA cold boot event which we ignore since we just booted outselves
    sys_handle_interrupts(sys_irq_ctrl(0xff) & 0xfe);
    
    // by default, DB9 interrupts are disabled. Reading
    // the DB9 state enables them. This is what hid_handle_event
    // does.
    hid_handle_event();

    if(!cfg) {
      // finally release FPGA from reset
      sys_set_val('R', 0);
    }
      
    // initialize on-screen-display and menu system
    osd_init();    
    menu_init();

    // open disk images, either defaults set in sdc_init or
    // user configure ones from the ini file
    sdc_mount_defaults();
  }

  debugf("Entering main loop");
  
  for(;;) {
    ulTaskNotifyTake( pdTRUE, portMAX_DELAY);    
    sys_handle_interrupts(sys_irq_ctrl(0xff));
      
    mcu_hw_irq_ack();  // re-enable interrupt
  }
}

#ifdef ESP_PLATFORM
void app_main( void )
#else
int main( void )
#endif
{
  mcu_hw_init();
  
  // run FPGA com thread
  xTaskCreate( com_task, "FPGA Com", 4096, NULL, CONFIG_MAX_PRIORITY-1, &com_task_handle );

  mcu_hw_main_loop();

#ifndef ESP_PLATFORM
  return 0;
#endif
}
/*-----------------------------------------------------------*/


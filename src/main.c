/*
  main.c - MiSTeryNano FPGA Companion Pi Pico variant

*/

#include "../mcu_hw.h"

#include "../sysctrl.h"
#include "../sdc.h"
#include "../osd.h"
#include "../menu.h"
#include "../core.h"
#include "../inifile.h"

/*-----------------------------------------------------------*/
/*---            main FPGA communication task            ----*/
/*-----------------------------------------------------------*/

TaskHandle_t com_task_handle;

static void com_task(__attribute__((unused)) void *p ) {
  // startup FPGA, this will also put the core into reset
  if(sys_wait4fpga()) {
    // FPGA is ready and can be talked to
    
    // initialitze SD card
    sdc_init();

    // process any pending interrupt. Filter out irq 1 which is the
    // FPGA cold boot event which we ignore since we just booted outselves
    sys_handle_interrupts(sys_irq_ctrl(0xff) & 0xfe);
    
    // by default, DB9 interrupts are disabled. Reading
    // the DB9 state enables them. This is what hid_handle_event
    // does.
    hid_handle_event();
    
    // finally release FPGA from reset
    sys_set_val('R', 0);

    // initialize on-screen-display and menu system
    menu_init();
    // open disk images, either defaults set in sdc_init or
    // user configure ones from the ini file
    sdc_mount_defaults();
  }

  for(;;) {
    ulTaskNotifyTake( pdTRUE, portMAX_DELAY);    
    sys_handle_interrupts(sys_irq_ctrl(0xff));
      
    mcu_hw_irq_ack();  // re-enable interrupt
  }
}
  
int main( void ) {
  mcu_hw_init();

  // run FPGA com thread
  xTaskCreate( com_task, "FPGA Com", 2048, NULL, CONFIG_MAX_PRIORITY-1, &com_task_handle );

  mcu_hw_main_loop();

  return 0;
}
/*-----------------------------------------------------------*/


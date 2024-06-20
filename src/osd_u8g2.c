//
// osd_u8g2.c
//

#include "spi.h"
#include "osd.h"
#include "menu.h"
#include "sdc.h"
#include "mcu_hw.h"

#include "debug.h"

u8g2_t u8g2;

static char state;
static uint8_t buf[128*8];  // screen buffer

static const u8x8_display_info_t u8x8_mn_128x64_info =
  { 0, 1, 0, 0, 0, 0, 0, 0, 4000000UL, 1, 0, 0, 0, 16, 8, 0, 0, 128, 64 };

uint8_t u8x8_d_mn_128x64(u8x8_t *u8g2, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  uint8_t x, y, c;
  uint8_t *ptr;

  switch(msg)
  {
    case U8X8_MSG_DISPLAY_SETUP_MEMORY:
      u8x8_d_helper_display_setup_memory(u8g2, &u8x8_mn_128x64_info);
      break;
    case U8X8_MSG_DISPLAY_INIT:
      u8x8_d_helper_display_init(u8g2);
      break;
    case U8X8_MSG_DISPLAY_SET_POWER_SAVE:
      break;
    case U8X8_MSG_DISPLAY_SET_FLIP_MODE:
      break;
    case U8X8_MSG_DISPLAY_SET_CONTRAST:
      break;
    case U8X8_MSG_DISPLAY_DRAW_TILE:
      x = ((u8x8_tile_t *)arg_ptr)->x_pos;
      x *= 8;
      x += u8g2->x_offset;
    
      y = ((u8x8_tile_t *)arg_ptr)->y_pos;
      y *= 8;
    
      do
      {
        c = ((u8x8_tile_t *)arg_ptr)->cnt;
        ptr = ((u8x8_tile_t *)arg_ptr)->tile_ptr;

	mcu_hw_spi_begin();
	
	/* send data */
	mcu_hw_spi_tx_u08(SPI_TARGET_OSD);
	mcu_hw_spi_tx_u08(SPI_OSD_WRITE);           // command byte data
	mcu_hw_spi_tx_u08(((y/8)<<4)+x/8); // tile address

	for(int i=0;i<c*8;i++)
	  mcu_hw_spi_tx_u08(ptr[i]);

	mcu_hw_spi_end();

        arg_int--;
	x+=c*8;
      } while( arg_int > 0 );

      break;

    default:
      return 0;
  }
  return 1;
}

static uint8_t u8x8_d_mn_gpio(U8X8_UNUSED u8x8_t *u8x8, U8X8_UNUSED uint8_t msg, U8X8_UNUSED uint8_t arg_int, U8X8_UNUSED void *arg_ptr) {
  return 1;
}

void u8x8_Setup_mn_128x64(u8x8_t *u8x8) {
  /* setup defaults */
  u8x8_SetupDefaults(u8x8);
  
  /* setup specific callbacks */
  u8x8->display_cb = u8x8_d_mn_128x64;
	
  u8x8->gpio_and_delay_cb = u8x8_d_mn_gpio;

  /* setup display info */
  u8x8_SetupMemory(u8x8);  
}

void osd_enable(char en) {
  osd_debugf("%sable", en?"en":"dis");
  
  state = en;
  
  // show/hide OSD
  mcu_hw_spi_begin();  
  mcu_hw_spi_tx_u08(SPI_TARGET_OSD);
  mcu_hw_spi_tx_u08(SPI_OSD_ENABLE);  // enable/disable command
  mcu_hw_spi_tx_u08(en);    // enable
  mcu_hw_spi_end();  
}

void osd_init(void) {
  // prepare u8g2
  // osd.spi->dev->user_data = osd.buf;
  u8x8_Setup_mn_128x64(u8g2_GetU8x8(&u8g2));
  u8g2_SetupBuffer(&u8g2, buf, 8, u8g2_ll_hvline_vertical_top_lsb, &u8g2_cb_r0);
  
  u8x8_ConnectBitmapToU8x8(u8g2_GetU8x8(&u8g2));
  u8g2_SetFontMode(&u8g2, 1);

  // make sure OSD is initially hidden
  state = OSD_INVISIBLE;
  osd_enable(state);
}

int osd_is_visible(void) {
  return state == OSD_VISIBLE;
}

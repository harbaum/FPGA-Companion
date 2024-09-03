// hid.c

#include "hid.h"
#include "debug.h"
#include "sysctrl.h"
#include "core.h"
#include "osd.h"
#include "menu.h"

#include "mcu_hw.h"

#include <string.h>  // for memcpy

// keep a map of joysticks to be able to report
// them individually
static uint8_t joystick_map = 0;

uint8_t hid_allocate_joystick(void) {
  uint8_t idx;
  for(idx=0;joystick_map & (1<<idx);idx++);
  joystick_map |= (1<<idx);
  usb_debugf("Allocating joystick %d (map = %02x)", idx, joystick_map);
  return idx;
}

void hid_release_joystick(uint8_t idx) {
  joystick_map &= ~(1<<idx);
  usb_debugf("Releasing joystick %d (map = %02x)", idx, joystick_map);
}
  
static void kbd_tx(uint8_t byte) {
  mcu_hw_spi_begin();
  mcu_hw_spi_tx_u08(SPI_TARGET_HID);
  mcu_hw_spi_tx_u08(SPI_HID_KEYBOARD);
  mcu_hw_spi_tx_u08(byte);
  mcu_hw_spi_end();
}

// the c64 core can use the numerical pad on the keyboard to
// emulate a joystick
static void kbd_num2joy(char state, unsigned char code) {
  static unsigned char kbd_joy_state = 0;
  static unsigned char kbd_joy_state_last = 0;
  
  // mapping:
  // keycode 5a = KP 2 = down
  // keycode 5c = KP 4 = left
  // keycode 5e = KP 6 = right
  // keycode 60 = KP 8 = up
  // keycode 62 = KP 0 = fire
  // keycode 63 = KP . and delete = 2nd trigger button
  // keycode 44 = F11 = Restore Key
  // keycode 4b = Page Up = Tape Play Key
  
  if(state == 0)
    // start parsing a new set of keys
    kbd_joy_state = 0;
  else if(state == 1) {
    // collect key/btn states
    if(code == 0x5e) kbd_joy_state |= 0x01;
    if(code == 0x5c) kbd_joy_state |= 0x02;
    if(code == 0x5a) kbd_joy_state |= 0x04;
    if(code == 0x60) kbd_joy_state |= 0x08;
    if(code == 0x62) kbd_joy_state |= 0x10;
    if(code == 0x63) kbd_joy_state |= 0x20;
    if(code == 0x44) kbd_joy_state |= 0x40;
    if(code == 0x4b) kbd_joy_state |= 0x80;
  } else if(state == 2) {
    // submit if state has changed
    if(kbd_joy_state != kbd_joy_state_last) {
      
      usb_debugf("KP Joy: %02x\r\n", kbd_joy_state);
  
      mcu_hw_spi_begin();
      mcu_hw_spi_tx_u08(SPI_TARGET_HID);
      mcu_hw_spi_tx_u08(SPI_HID_JOYSTICK);
      mcu_hw_spi_tx_u08(0x80);  // report this as joystick 0x80 as js0-x are USB joysticks
      mcu_hw_spi_tx_u08(kbd_joy_state);
      mcu_hw_spi_end();
      
      kbd_joy_state_last = kbd_joy_state;
    }
  }
}
  
void kbd_parse(__attribute__((unused)) const hid_report_t *report, struct hid_kbd_state_S *state,
	       const unsigned char *buffer, int nbytes) {
  // we expect boot mode packets which are exactly 8 bytes long
  if(nbytes != 8) return;
  
  // check if modifier have changed
  if((buffer[0] != state->last_report[0]) && !osd_is_visible()) {
    for(int i=0;i<8;i++) {
      if(core_map_modifier_key(i)) {      
	// modifier released?
	if((state->last_report[0] & (1<<i)) && !(buffer[0] & (1<<i)))
	  kbd_tx(0x80 | core_map_modifier_key(i));
	// modifier pressed?
	if(!(state->last_report[0] & (1<<i)) && (buffer[0] & (1<<i)))
	  kbd_tx(core_map_modifier_key(i));
      }
    }
  } 
  
  // prepare for parsing numpad joystick
  if(core_id == CORE_ID_C64||core_id == CORE_ID_VIC20) kbd_num2joy(0, 0);
  
  // check if regular keys have changed
  for(int i=0;i<6;i++) {
    // C64 uses some keys for joystick emulation
    if(core_id == CORE_ID_C64||core_id == CORE_ID_VIC20) kbd_num2joy(1, buffer[2+i]);
    
    if(buffer[2+i] != state->last_report[2+i]) {
      // key released?
      if(state->last_report[2+i] && !osd_is_visible() )
	kbd_tx(0x80 | core_map_key(state->last_report[2+i]));
      
      // key pressed?
      if(buffer[2+i])  {
	static unsigned long msg;
	msg = 0;
	
	// F12 toggles the OSD state. Therefore F12 must never be forwarded
	// to the core and thus must have an empty entry in the keymap. ESC
	// can only close the OSD.

	// Caution: Since the OSD closes on the press event, the following
	// release event will be sent into the core. The core should thus
	// cope with release events that did not have a press event before
	if(buffer[2+i] == 0x45 || (osd_is_visible() && buffer[2+i] == 0x29) )
	  msg = osd_is_visible()?MENU_EVENT_HIDE:MENU_EVENT_SHOW;
	else {
	  if(!osd_is_visible())
	    kbd_tx(core_map_key(buffer[2+i]));
	  else {
	    // check if cursor up/down or space has been pressed
	    if(buffer[2+i] == 0x51) msg = MENU_EVENT_DOWN;      
	    if(buffer[2+i] == 0x52) msg = MENU_EVENT_UP;
	    if(buffer[2+i] == 0x4e) msg = MENU_EVENT_PGDOWN;      
	    if(buffer[2+i] == 0x4b) msg = MENU_EVENT_PGUP;
	    if((buffer[2+i] == 0x2c) || (buffer[2+i] == 0x28))
	      msg = MENU_EVENT_SELECT;
	  }
	}

	// send message to menu task
	if(msg) menu_notify(msg);
      }   
    }
  }
  memcpy(state->last_report, buffer, 8);

  // check if numpad joystick has changed state and send message if so
  if(core_id == CORE_ID_C64||core_id == CORE_ID_VIC20) kbd_num2joy(2, 0);
}

// collect bits from byte stream and assemble them into a signed word
static uint16_t collect_bits(const uint8_t *p, uint16_t offset, uint8_t size, bool is_signed) {
  // mask unused bits of first byte
  uint8_t mask = 0xff << (offset&7);
  uint8_t byte = offset/8;
  uint8_t bits = size;
  uint8_t shift = offset&7;
  
  //  iusb_debugf("0 m:%x by:%d bi=%d sh=%d ->", mask, byte, bits, shift);
  uint16_t rval = (p[byte++] & mask) >> shift;
  mask = 0xff;
  shift = 8-shift;
  bits -= shift;
  
  // first byte already contained more bits than we need
  if(shift > size) {
    // mask unused bits
    rval &= (1<<size)-1;
  } else {
    // further bytes if required
    while(bits) {
      mask = (bits<8)?(0xff>>(8-bits)):0xff;
      rval += (p[byte++] & mask) << shift;
      shift += 8;
      bits -= (bits>8)?8:bits;
    }
  }
  
  if(is_signed) {
    // do sign expansion
    uint16_t sign_bit = 1<<(size-1);
    if(rval & sign_bit) {
      while(sign_bit) {
	rval |= sign_bit;
	sign_bit <<= 1;
      }
    }
  }
  
  return rval;
}

void mouse_parse(const hid_report_t *report, __attribute__((unused)) struct hid_mouse_state_S *state,
		 const unsigned char *buffer, int nbytes) {
  // we expect at least three bytes:
  if(nbytes < 3) return;
  
  // collect info about the two axes
  int a[2];
  for(int i=0;i<2;i++) {  
    bool is_signed = report->joystick_mouse.axis[i].logical.min > 
      report->joystick_mouse.axis[i].logical.max;

    a[i] = collect_bits(buffer, report->joystick_mouse.axis[i].offset, 
			report->joystick_mouse.axis[i].size, is_signed);
  }

  // ... and two buttons
  uint8_t btns = 0;
  for(int i=0;i<2;i++)
    if(buffer[report->joystick_mouse.button[i].byte_offset] & 
       report->joystick_mouse.button[i].bitmask)
      btns |= (1<<i);

  mcu_hw_spi_begin();
  mcu_hw_spi_tx_u08(SPI_TARGET_HID);
  mcu_hw_spi_tx_u08(SPI_HID_MOUSE);
  mcu_hw_spi_tx_u08(btns);
  mcu_hw_spi_tx_u08(a[0]);
  mcu_hw_spi_tx_u08(a[1]);
  mcu_hw_spi_end();
}

void joystick_parse(const hid_report_t *report, struct hid_joystick_state_S *state,
		    const unsigned char *buffer, __attribute__((unused)) int nbytes) {
  //  usb_debugf("joystick: %d %02x %02x %02x %02x", nbytes,
  //  	 buffer[0]&0xff, buffer[1]&0xff, buffer[2]&0xff, buffer[3]&0xff);

  // collect info about the two axes
  int a[2];
  for(int i=0;i<2;i++) {  
    bool is_signed = report->joystick_mouse.axis[i].logical.min > 
      report->joystick_mouse.axis[i].logical.max;
    
    a[i] = collect_bits(buffer, report->joystick_mouse.axis[i].offset, 
			report->joystick_mouse.axis[i].size, is_signed);
  }

  // ... and four buttons
  unsigned char joy = 0;
  for(int i=0;i<4;i++)
    if(buffer[report->joystick_mouse.button[i].byte_offset] & 
       report->joystick_mouse.button[i].bitmask)
      joy |= (0x10<<i);

  // ... and the eight extra buttons
  unsigned char btn_extra = 0;
  for(int i=4;i<12;i++)
    if(buffer[report->joystick_mouse.button[i].byte_offset] & 
      report->joystick_mouse.button[i].bitmask) 
      btn_extra |= (1<<(i-4));

  // map directions to digital
  if(a[0] > 0xc0) joy |= 0x01;
  if(a[0] < 0x40) joy |= 0x02;
  if(a[1] > 0xc0) joy |= 0x04;
  if(a[1] < 0x40) joy |= 0x08;

  int ax = 0;
  int ay = 0;
  ax = a[0];
  ay = a[1];

  if((joy != state->last_state) || 
     (ax != state->last_state_x) || 
     (ay != state->last_state_y) || 
     (btn_extra != state->last_state_btn_extra))  {
    state->last_state = joy;
    state->last_state_x = ax;
    state->last_state_y = ay;
    state->last_state_btn_extra = btn_extra;
    usb_debugf("JOY%d: D %02x A0 %02x A1 %02x B %02x", state->js_index, joy, ax, ay, btn_extra);

    mcu_hw_spi_begin();
    mcu_hw_spi_tx_u08(SPI_TARGET_HID);
    mcu_hw_spi_tx_u08(SPI_HID_JOYSTICK);
    mcu_hw_spi_tx_u08(state->js_index);
    mcu_hw_spi_tx_u08(joy);
    mcu_hw_spi_tx_u08(ax); // e.g. gamepad X
    mcu_hw_spi_tx_u08(ay); // e.g. gamepad Y
    mcu_hw_spi_tx_u08(btn_extra); // e.g. gamepad extra buttons
    mcu_hw_spi_end();
  }
}

void rii_joy_parse(const unsigned char *buffer) {
  unsigned char b = 0;
  if(buffer[0] == 0xcd && buffer[1] == 0x00) b = 0x10;      // cd == play/pause  -> center
  if(buffer[0] == 0xe9 && buffer[1] == 0x00) b = 0x08;      // e9 == V+          -> up
  if(buffer[0] == 0xea && buffer[1] == 0x00) b = 0x04;      // ea == V-          -> down
  if(buffer[0] == 0xb6 && buffer[1] == 0x00) b = 0x02;      // b6 == skip prev   -> left
  if(buffer[0] == 0xb5 && buffer[1] == 0x00) b = 0x01;      // b5 == skip next   -> right

  usb_debugf("RII Joy: %02x %02x", 0, b);
  
  mcu_hw_spi_begin();
  mcu_hw_spi_tx_u08(SPI_TARGET_HID);
  mcu_hw_spi_tx_u08(SPI_HID_JOYSTICK);
  mcu_hw_spi_tx_u08(0);  // Rii joystick always report as joystick 0
  mcu_hw_spi_tx_u08(b);
  mcu_hw_spi_end();
}

void hid_parse(const hid_report_t *report, hid_state_t *state, uint8_t const* data, uint16_t len) {
  //  usb_debugf("hid parse");
  //  hexdump((void*)report, len);

  // the following is a hack for the Rii keyboard/touch combos to use the
  // left top multimedia pad as a joystick. These special keys are sent
  // via the mouse/touchpad part
  if(report->report_id_present &&
     report->type == REPORT_TYPE_MOUSE &&
     len == 3 &&
     data[0] != report->report_id) {
    rii_joy_parse(data+1);
    return;
  }
  
  // check and skip report id if present
  if(report->report_id_present) {
    if(!len || (data[0] != report->report_id))
      return;
    
    // skip report id
    data++; len--;
  }
  
  if(len == report->report_size) {
    if(report->type == REPORT_TYPE_KEYBOARD)
      kbd_parse(report, &state->kbd, data, len);
    
    if(report->type == REPORT_TYPE_MOUSE)
      mouse_parse(report, &state->mouse, data, len);
    
    if(report->type == REPORT_TYPE_JOYSTICK)
      joystick_parse(report, &state->joystick, data, len);
  }
}

// hid event triggered by FPGA
void hid_handle_event(void) {
  mcu_hw_spi_begin();
  mcu_hw_spi_tx_u08(SPI_TARGET_HID);
  mcu_hw_spi_tx_u08(SPI_HID_GET_DB9);
  mcu_hw_spi_tx_u08(0x00);
  uint8_t db9 = mcu_hw_spi_tx_u08(0x00);
  mcu_hw_spi_end();

  debugf("DB9: %02x", db9);
}

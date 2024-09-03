/* hid.h */

#ifndef HID_H
#define HID_H

#include <stdbool.h>
#include "hidparser.h"

struct hid_kbd_state_S {
  unsigned char last_report[8];	
};

struct hid_mouse_state_S {
};

struct hid_joystick_state_S {
  unsigned char last_state;
  unsigned char js_index;
  unsigned char last_state_x;
  unsigned char last_state_y;
};

typedef union {
  struct hid_kbd_state_S kbd;
  struct hid_mouse_state_S mouse;
  struct hid_joystick_state_S joystick;  
} hid_state_t;

void hid_parse(const hid_report_t *report, hid_state_t *state, uint8_t const* data, uint16_t len);

void kbd_parse(const hid_report_t *report, struct hid_kbd_state_S *state, const unsigned char *buffer, int nbytes);
void mouse_parse(const hid_report_t *report, struct hid_mouse_state_S *state, const unsigned char *buffer, int nbytes);
void joystick_parse(const hid_report_t *report, struct hid_joystick_state_S *state, const unsigned char *buffer, int nbytes);

void hid_handle_event(void);

uint8_t hid_allocate_joystick(void);
void hid_release_joystick(uint8_t idx);

#endif // HID_H

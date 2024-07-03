/*
  core_amiga.c

  AMIGA specific menus, keytables etc
*/

#include "core_amiga.h"
#include "sdc.h"
#include "debug.h"

const char * core_amiga_default_images[] = {
  CARD_MOUNTPOINT "/df0.adf",
  CARD_MOUNTPOINT "/df1.adf",
  CARD_MOUNTPOINT "/df2.adf",
  CARD_MOUNTPOINT "/df3.adf",
  NULL
};

// ------------------------------------------------------------------
// -----------------------  Amiga menu ------------------------------
// ------------------------------------------------------------------

static const char main_form_amiga[] =
  "NanoMig,;"                           // main form has no parent
  // --------
  "F,Floppy DF0:,0|adf;"                // fileselector for DF0
  "S,System,1;"                         // System submenu is #1
  "S,Storage,2;"                        // Storage submenu is #2
  "S,Settings,3;"                       // Settings submenu is #3
  "B,Reset,R;";                         // system reset

static const char system_form_amiga[] =
  "System,0|2;"                         // return to form 0, entry 2
  // --------
  "L,Chipset:,OCS-A500|OCS-A1000|ECS,C;"// Chipset
  "L,Chipmem:,512k|1M|1.5M|2M,Y;"       // Chip ram size
  "L,Slowmem:,None|512k|1M|1.5M,X;"     // Slow ram size
  "L,Video:,PAL|NTSC,V;";               // Video Standard

  static const char storage_form_amiga[] =
  "Storage,0|3;"                        // return to form 0, entry 3
  // --------
  "L,Drives:,1|2|3|4,D;"                // Floppy Drives
  "L,Speed:,Normal|Fast,S;"             // Floppy Speed
  "F,Floppy DF0,0|adf;"                 // image selector
  "F,Floppy DF1,1|adf;" 
  "F,Floppy DF2,2|adf;" 
  "F,Floppy DF3,3|adf;"; 

static const char settings_form_amiga[] =
  "Settings,0|4;"                       // return to form 0, entry 4
  // --------
  "L,Scanlines:,None|Dim|Black,L;"      // Video Scanlines
  "L,Filter:,None|Horizontal|Vertical|Hor+Ver,F;"  // Video Filter
  "B,Save settings,S;";

const char *core_amiga_forms[] = {
    main_form_amiga,
    system_form_amiga,
    storage_form_amiga,
    settings_form_amiga
};

menu_legacy_variable_t core_amiga_variables[] = {
  { 'D', { 0 }},    // default one floppy drive
  { 'S', { 1 }},    // default fast floppy
  { 'C', { 2 }},    // default ECS
  { 'V', { 0 }},    // default PAL
  { 'F', { 0 }},    // default no video filter
  { 'L', { 0 }},    // default no scanlines
  { 'Y', { 0 }},    // default 512k chip ram
  { 'X', { 1 }},    // default 512k slow ram
  { '\0',{ 0 }}
};

#define MISS          (0)

const unsigned char core_amiga_keymap[] = {
  MISS,   // 00: NoEvent
  MISS,   // 01: Overrun Error
  MISS,   // 02: POST fail
  MISS,   // 03: ErrorUndefined
 
  // characters
  0x20,   // 04: a
  0x35,   // 05: b
  0x33,   // 06: c
  0x22,   // 07: d
  0x12,   // 08: e
  0x23,   // 09: f
  0x24,   // 0a: g
  0x25,   // 0b: h
  0x17,   // 0c: i
  0x26,   // 0d: j
  0x27,   // 0e: k
  0x28,   // 0f: l
  0x37,   // 10: m
  0x36,   // 11: n
  0x18,   // 12: o
  0x19,   // 13: p
  0x10,   // 14: q
  0x13,   // 15: r
  0x21,   // 16: s
  0x14,   // 17: t
  0x16,   // 18: u
  0x34,   // 19: v
  0x11,   // 1a: w
  0x32,   // 1b: x
  0x15,   // 1c: y
  0x31,   // 1d: z

  // top number key row
  0x01,   // 1e: 1
  0x02,   // 1f: 2
  0x03,   // 20: 3
  0x04,   // 21: 4
  0x05,   // 22: 5
  0x06,   // 23: 6
  0x07,   // 24: 7
  0x08,   // 25: 8
  0x09,   // 26: 9
  0x0a,   // 27: 0
  
  // other keys
  0x44,   // 28: return
  0x45,   // 29: esc
  0x41,   // 2a: backspace
  0x42,   // 2b: tab		  
  0x40,   // 2c: space

  0x0b,   // 2d: -
  0x0c,   // 2e: =
  0x1a,   // 2f: [			  
  0x1b,   // 30: ]
  0x0d,   // 31: backslash 
  0x2b,   // 32: EUR-1
  0x29,   // 33: ;
  0x2a,   // 34: ' 
  0x00,   // 35: `
  0x38,   // 36: ,
  0x39,   // 37: .
  0x3a,   // 38: /
  0x62,   // 39: caps lock


  // function keys
  0x50,   // 3a: F1
  0x51,   // 3b: F2
  0x52,   // 3c: F3
  0x53,   // 3d: F4
  0x54,   // 3e: F5
  0x55,   // 3f: F6
  0x56,   // 40: F7
  0x57,   // 41: F8
  0x58,   // 42: F9
  0x59,   // 43: F10
  MISS,   // 44: F11
  MISS,   // 45: F12

  MISS,   // 46: PrtScr
  MISS,   // 47: Scroll Lock
  MISS,   // 48: Pause
  MISS,   // 49: Insert
  0x5a,   // 4a: Home -> KP-(
  0x5b,   // 4b: PageUp -> KP-)
  0x46,   // 4c: Delete
  0x5f,   // 4d: End -> HELP
  0x67,   // 4e: PageDown -> Right-Amiga
  
  // cursor keys
  0x4e,   // 4f: right
  0x4f,   // 50: left
  0x4d,   // 51: down
  0x4c,   // 52: up
  
  MISS,   // 53: Num Lock

  // keypad
  0x5c,   // 54: KP /
  0x5d,   // 55: KP *
  0x4a,   // 56: KP -
  0x5e,   // 57: KP +
  0x43,   // 58: KP Enter
  0x1d,   // 59: KP 1
  0x1e,   // 5a: KP 2
  0x1f,   // 5b: KP 3
  0x2d,   // 5c: KP 4
  0x2e,   // 5d: KP 5
  0x2f,   // 5e: KP 6
  0x3d,   // 5f: KP 7
  0x3e,   // 60: KP 8
  0x3f,   // 61: KP 9
  0x0f,   // 62: KP 0
  0x3c,   // 63: KP .
  0x2b,   // 64: EUR-2
};  
  
const unsigned char core_amiga_modifier[] = {
  /* usb modifer bits:
     0     1      2    3    4     5      6    7
     LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
  */

  0x63, // ctrl
  0x60, // lshift
  0x64, // lalt
  0x66, // Win
  MISS, // ctrl (right)
  0x61, // rshift
  0x65, // ralt
  0x67  // Menu
};
  

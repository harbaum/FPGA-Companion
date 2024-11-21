/*
  core_vic20.c

  VIC20 specific menus, keytables etc
*/

#include "core_vic20.h"
#include "sdc.h"
#include "debug.h"

const char * core_vic20_default_images[] = {
  CARD_MOUNTPOINT "/disk8.d64",
  CARD_MOUNTPOINT "/vic20crt.crt",
  CARD_MOUNTPOINT "/vic20prg.prg",
  CARD_MOUNTPOINT "/vic20kernal.bin",
  CARD_MOUNTPOINT "/vic20tap.tap",
  NULL
};

// ------------------------------------------------------------------
// ------------------------  VIC20 menu -------------------------------
// ------------------------------------------------------------------

static const char main_form_vic20[] =
  "VIC20Nano,;"                         // main form has no parent
  // --------
  "B,Detach Cartridge & Reset,F;"
  "S,System,1;"                         // System submenu is form 1
  "S,Storage,2;"                        // Storage submenu
  "S,Settings,3;"                       // Settings submenu is form 2
  "B,Reset,R;";                         // system reset

static const char system_form_vic20[] =
  "System,0|2;"                         // return to form 0, entry 2
  // --------
  "L,Joyport:,Retro D9|USB #1 Joy|USB #2 Joy|NumPad|DualShock 2|Mouse|DS2 Paddle|USB #1 Padd|USB #2 Padd|Off,Q;" // Joystick port 1 mapping
  "L,c1541 ROM:,Dolphin DOS|CBM DOS|Speed DOS P|Jiffy DOS,D;"  // c1541 compatibility
  "L,RAM $04 3K:,Off|On,U;"
  "L,RAM $20 8K:,Off|On,X;"
  "L,RAM $40 8K:,Off|On,Y;"
  "L,RAM $60 8K:,Off|On,N;"
  "L,RAM $A0 8K:,Off|On,G;"
  "L,Video Std:,PAL|NTSC,E;"
  "L,Vid. cent:,Off|Both|Horz|Vert,J;"
  "L,CRT write:,Off|On,V;"
  "L,Tape Sound:,Off|On,I;"
  "B,c1541 Reset,Z;"
  "B,Cold Boot,B;"; 

static const char storage_form_vic20[] =
  "Storage,0|3;"                        // return to form 0, entry 3
  // --------
  "F,Floppy 8:,0|d64+g64;"              // fileselector for Disk Drive 8:
  "F,CRT ROM:,1|prg+crt;"               // fileselector for CRT (special VIC20 prg)
  "F,PRG BASIC:,2|prg;"                 // fileselector for PRG
  "F,VIC20 Kernal:,3|bin;"              // fileselector for Kernal ROM
  "F,TAP Tape:,4|tap;"                  // fileselector for TAP
  "L,Disk prot.:,None|8:,P;";           // Enable/Disable Floppy write protection

static const char settings_form_vic20[] =
  "Settings,0|4;"                       // return to form 0, entry 3
  // --------
  "L,Screen:,Normal|Wide,W;"
  "L,Scanlines:,None|25%|50%|75%,S;"
  "L,Volume:,Mute|33%|66%|100%,A;"
  "B,Save settings,S;";

const char *core_vic20_forms[] = {
  main_form_vic20,
  system_form_vic20,
  storage_form_vic20,
  settings_form_vic20
};

menu_legacy_variable_t core_vic20_variables[] = {
  { 'U', { 0 }},    // default 3k, $0400
  { 'X', { 0 }},    // default 8k, $2000
  { 'Y', { 0 }},    // default 8k, $4000
  { 'N', { 0 }},    // default 8k, $6000
  { 'G', { 0 }},    // default 8k, $A000 Cartridge area
  { 'D', { 1 }},    // default c1541 dos = CBM
  { 'S', { 0 }},    // default scanlines = none
  { 'A', { 2 }},    // default volume = 66%
  { 'W', { 0 }},    // default normal (4:3) screen
  { 'P', { 0 }},    // default no floppy write protected
  { 'Q', { 0 }},    // Joystick port 1 mapping = DB9
  { 'E', { 0 }},    // default standard = PAL
  { 'J', { 1 }},    // Screen center = Both
  { 'V', { 1 }},    // Cartridge writable = On
  { 'I', { 1 }},    // default Tape sound = On
  { '\0',{ 0 }}
};

#define MISS          (0)
#define MATRIX(a,b)   (b*8+a)

const unsigned char core_vic20_keymap[] = {
  MISS,         // 00: NoEvent
  MISS,         // 01: Overrun Error
  MISS,         // 02: POST fail
  MISS,         // 03: ErrorUndefined
 
  // characters
  MATRIX( 2,1), // 04: a
  MATRIX( 4,3), // 05: b
  MATRIX( 4,2), // 06: c
  MATRIX( 2,2), // 07: d
  MATRIX( 6,1), // 08: e
  MATRIX( 5,2), // 09: f
  MATRIX( 2,3), // 0a: g
  MATRIX( 5,3), // 0b: h
  MATRIX( 1,4), // 0c: i
  MATRIX( 2,4), // 0d: j
  MATRIX( 5,4), // 0e: k
  MATRIX( 2,5), // 0f: l
  MATRIX( 4,4), // 10: m
  MATRIX( 3,4), // 11: n
  MATRIX( 6,4), // 12: o
  MATRIX( 1,5), // 13: p
  MATRIX( 6,0), // 14: q
  MATRIX( 1,2), // 15: r
  MATRIX( 5,1), // 16: s
  MATRIX( 6,2), // 17: t
  MATRIX( 6,3), // 18: u
  MATRIX( 3,3), // 19: v
  MATRIX( 1,1), // 1a: w
  MATRIX( 3,2), // 1b: x
  MATRIX( 1,3), // 1c: y
  MATRIX( 4,1), // 1d: z

  // top number key row
  MATRIX( 0,0), // 1e: 1
  MATRIX( 7,0), // 1f: 2
  MATRIX( 0,1), // 20: 3
  MATRIX( 7,1), // 21: 4
  MATRIX( 0,2), // 22: 5
  MATRIX( 7,2), // 23: 6
  MATRIX( 0,3), // 24: 7
  MATRIX( 7,3), // 25: 8
  MATRIX( 0,4), // 26: 9
  MATRIX( 7,4), // 27: 0
  
  // other keys
  MATRIX( 1,7), // 28: return
  MATRIX( 3,0), // 29: esc  as run/stop
  MATRIX( 0,7), // 2a: backspace as del
  MATRIX( 3,1), // lshift // 2b: tab
  MATRIX( 4,0), // 2c: space

  MATRIX( 7,5), // 2d: - as -
  MATRIX( 0,5), // 2e: = as +
  MATRIX( 6,5), // 2f: [  as @
  MATRIX( 1,6), // 30: ]  as *
  MATRIX( 0,6), // 31: backslash as pound 
  MATRIX( 0,6), // 32: EUR-1 as pound backup
  MATRIX( 5,5), // 33: ; as :
  MATRIX( 2,6), // 34: ' as ;
  MATRIX( 1,0), // 35: ` as arrow left
  MATRIX( 3,5), // 36: , as ,
  MATRIX( 4,5), // 37: . as .
  MATRIX( 3,6), // 38: / as /
  MATRIX( 5,0), // 39: caps lock as cbm 
  
  // function keys
  MATRIX( 4,7), // 3a: F1
  MATRIX( 4,7), // 3b: F2
  MATRIX( 5,7), // 3c: F3
  MATRIX( 5,7), // 3d: F4
  MATRIX( 6,7), // 3e: F5
  MATRIX( 6,7), // 3f: F6
  MATRIX( 7,7), // 40: F7
  MATRIX( 7,7), // 41: F8
  MATRIX( 6,6), // 42: F9  as arrow up
  MATRIX( 5,6), // 43: F10 as equal
  MATRIX( 3,1), // lshift // 44: F11 as restore
  MISS,         // 45: F12 (OSD)

  MATRIX( 3,1), // lshift // 46: PrtScr
  MATRIX( 3,1), // lshift // 47: Scroll Lock
  MATRIX( 3,1), // lshift // 48: Pause
  MATRIX( 7,6), // 49: Insert as CLR
  MATRIX( 3,1), // lshift // 4a: Home
  MATRIX( 3,1), // lshift // 4b: PageUp
  MATRIX( 7,6), // 4c: Delete as CLR
  MATRIX( 3,1), // lshift // 4d: End
  MATRIX( 3,1), // lshift // 4e: PageDown
  
  // cursor keys
  MATRIX( 2,7), // 4f: right
  MATRIX( 2,7), // 50: left
  MATRIX( 3,7), // 51: down
  MATRIX( 3,7), // 52: up
  MATRIX( 3,1), // lshift // 53: Num Lock

  // keypad
  MATRIX( 3,1), // lshift//  54: KP /
  MATRIX( 3,1), // lshift//  55: KP *
  MATRIX( 3,1), // lshift//  56: KP -
  MATRIX( 3,1), // lshift//  57: KP +
  MATRIX( 3,1), // lshift//  58: KP Enter
  MATRIX( 3,1), // lshift//  59: KP 1
  MATRIX( 3,1), // lshift//  a: KP 2
  MATRIX( 3,1), // lshift//  5b: KP 3
  MATRIX( 3,1), // lshift//  5c: KP 4
  MATRIX( 3,1), // lshift//  5d: KP 5
  MATRIX( 3,1), // lshift//  5e: KP 6
  MATRIX( 3,1), // lshift//  f: KP 7
  MATRIX( 3,1), // lshift//  60: KP 8
  MATRIX( 3,1), // lshift//  61: KP 9
  MATRIX( 3,1), // lshift//  62: KP 0
  MATRIX( 3,1), // lshift//  63: KP .
  MATRIX( 3,1), // lshift//  64: EUR-2
};  
  
const unsigned char core_vic20_modifier[] = {
  /* usb modifer bits:
     0     1      2    3    4     5      6    7
     LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
  */

  MATRIX( 2,0), // ctrl (left)
  MATRIX( 3,1), // lshift
  MATRIX( 5,0), // lalt as CBM 
  MATRIX( 3,1), // lshift        // lgui
  MATRIX( 2,0), // ctrl (right)
  MATRIX( 4,6), // rshift
  MATRIX( 5,0), // ralt as CBM
  MATRIX( 3,1) // lshift         // rgui
};
  

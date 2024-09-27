/*
  core_atari2600.c

  atari 2600 specific menus
*/

#include "core_atari2600.h"
#include "sdc.h"
#include "debug.h"

const char * core_atari2600_default_images[] = {
  CARD_MOUNTPOINT "/atari2600crt.bin",
  NULL
};

static const char main_form_atari2600[] =
  "A2600Nano,;"                           // main form has no parent
  // --------
  "F,Cartridge:,0|bin+F8+F6+FE+E0+3F+F4+P2+FA+CV+UA+E7+F0+32;" // fileselector for ROM
  "S,System,1;"                         // System submenu is form 1
  "S,Storage,2;"                        // Storage submenu
  "S,Settings,3;"                       // Settings submenu is form 2
  "B,Reset,R;";                         // system reset

static const char system_form_atari2600[] =
  "System,0|2;"                         // return to form 0, entry 2
  // --------
  "L,Joyport 1:,Retro D9|USB #1 Joy|USB #2 Joy|NumPad|DualShock 2|Mouse|DS2 Paddle|USB #1 Padd|USB #2 Padd|Off,Q;"
  "L,Joyport 2:,Retro D9|USB #1 Joy|USB #2 Joy|NumPad|DualShock 2|Mouse|DS2 Paddle|USB #1 Padd|USB #2 Padd|Off,J;"
  "L,Invert Paddle:,Off|On,V;"
  "L,Difficulty P1:,A|B,X;"
  "L,Difficulty P2:,A|B,Y;"
  "L,De-comb:,Off|On,C;"
  "L,VBlank:,Original|Regenerate,M;"
  "L,Video mode:,Mono|Color,O;"
  "L,SuperChip:,Auto|Off|On,U;"
  "L,Video Std:,NTSC|PAL,E;"
  "B,Cold Boot,B;"; 

static const char storage_form_atari2600[] =
  "Storage,0|3;"                        // return to form 0, entry 3
  // --------
  "F,Cartridge:,0|bin+F8+F6+FE+E0+3F+F4+P2+FA+CV+UA+E7+F0+32;";  // fileselector 

static const char settings_form_atari2600[] =
  "Settings,0|4;"                       // return to form 0, entry 3
  // --------
  "L,Screen:,Normal|Wide,W;"
  "L,Scanlines:,None|25%|50%|75%,S;"
  "L,Volume:,Mute|33%|66%|100%,A;"
  "B,Save settings,S;";

const char *core_atari2600_forms[] = {
  main_form_atari2600,
  system_form_atari2600,
  storage_form_atari2600,
  settings_form_atari2600
};
// Q J V X Y C M O U E
menu_legacy_variable_t core_atari2600_variables[] = {
  { 'X', { 0 }},    // default Difficulty P1 = A
  { 'Y', { 0 }},    // default Difficulty P2 = A
  { 'V', { 0 }},    // default Invert Paddle = off
  { 'S', { 0 }},    // default scanlines = none
  { 'A', { 2 }},    // default volume = 66%
  { 'W', { 0 }},    // default normal (4:3) screen
  { 'Q', { 1 }},    // Joystick port 1 mapping, USB #1
  { 'J', { 9 }},    // Joystick port 2 mapping, OFF
  { 'E', { 0 }},    // default standard = NTSC
  { 'C', { 0 }},    // default De-comb = off
  { 'M', { 0 }},    // default VBlank = original
  { 'O', { 1 }},    // default Video mode = color
  { 'U', { 0 }},    // default SuperChip = auto
  { '\0',{ 0 }}
};

#define MISS          (0)
#define MATRIX(b,a)   (b*8+a)

const unsigned char core_atari2600_keymap[] = {
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
  MATRIX( 7,4), // 11: n
  MATRIX( 6,4), // 12: o
  MATRIX( 1,5), // 13: p
  MATRIX( 6,7), // 14: q
  MATRIX( 1,2), // 15: r
  MATRIX( 5,1), // 16: s
  MATRIX( 6,2), // 17: t
  MATRIX( 6,3), // 18: u
  MATRIX( 7,3), // 19: v
  MATRIX( 1,1), // 1a: w
  MATRIX( 7,2), // 1b: x
  MATRIX( 1,3), // 1c: y
  MATRIX( 4,1), // 1d: z

  // top number key row
  MATRIX( 0,7), // 1e: 1
  MATRIX( 3,7), // 1f: 2
  MATRIX( 0,1), // 20: 3
  MATRIX( 3,1), // 21: 4
  MATRIX( 0,2), // 22: 5
  MATRIX( 3,2), // 23: 6
  MATRIX( 0,3), // 24: 7
  MATRIX( 3,3), // 25: 8
  MATRIX( 0,4), // 26: 9
  MATRIX( 3,4), // 27: 0
  
  // other keys
  MATRIX( 1,0), // 28: return
  MATRIX( 7,7), // 29: esc  as run/stop
  MATRIX( 0,0), // 2a: backspace as del
  MATRIX( 7,1), // lshift // 2b: tab
  MATRIX( 4,7), // 2c: space

  MATRIX( 3,5), // 2d: - as -
  MATRIX( 0,5), // 2e: = as +
  MATRIX( 6,5), // 2f: [  as @
  MATRIX( 1,6), // 30: ]  as *
  MATRIX( 0,6), // 31: backslash as pound 
  MATRIX( 0,6), // 32: EUR-1 as pound backup
  MATRIX( 5,5), // 33: ; as :
  MATRIX( 2,6), // 34: ' as ;
  MATRIX( 1,7), // 35: ` as arrow left
  MATRIX( 7,5), // 36: , as ,
  MATRIX( 4,5), // 37: . as .
  MATRIX( 7,6), // 38: / as /
  MATRIX( 5,7), // 39: caps lock as cbm 
  
  // function keys
  MATRIX( 4,0), // 3a: F1
  MATRIX( 4,0), // 3b: F2
  MATRIX( 5,0), // 3c: F3
  MATRIX( 5,0), // 3d: F4
  MATRIX( 6,0), // 3e: F5
  MATRIX( 6,0), // 3f: F6
  MATRIX( 3,0), // 40: F7
  MATRIX( 3,0), // 41: F8
  MATRIX( 6,6), // 42: F9  as arrow up
  MATRIX( 5,6), // 43: F10 as equal
  MATRIX( 7,1), // lshift // 44: F11 as restore (NMI) !
  MISS,         // 45: F12 (OSD)

  MATRIX( 7,1), // lshift // 46: PrtScr
  MATRIX( 7,1), // lshift // 47: Scroll Lock
  MATRIX( 7,1), // lshift // 48: Pause
  MATRIX( 3,6), // 49: Insert as CLR
  MATRIX( 7,1), // lshift // 4a: Home
  MATRIX( 7,1), // lshift // 4b: PageUp
  MATRIX( 3,6), // 4c: Delete as CLR
  MATRIX( 7,1), // lshift // 4d: End
  MATRIX( 7,1), // lshift // 4e: PageDown
  
  // cursor keys
  MATRIX( 2,0), // 4f: right
  MATRIX( 2,0), // 50: left
  MATRIX( 7,0), // 51: down
  MATRIX( 7,0), // 52: up
  
  MATRIX( 7,1), // lshift // 53: Num Lock

  // keypad
  MATRIX( 7,1), // lshift // 54: KP /
  MATRIX( 7,1), // lshift // 55: KP *
  MATRIX( 7,1), // lshift // 56: KP -
  MATRIX( 7,1), // lshift // 57: KP +
  MATRIX( 7,1), // lshift // 58: KP Enter
  MATRIX( 7,1), // lshift // 59: KP 1
  MATRIX( 7,1), // lshift // 5a: KP 2
  MATRIX( 7,1), // lshift // 5b: KP 3
  MATRIX( 7,1), // lshift // 5c: KP 4
  MATRIX( 7,1), // lshift // 5d: KP 5
  MATRIX( 7,1), // lshift // 5e: KP 6
  MATRIX( 7,1), // lshift // 5f: KP 7
  MATRIX( 7,1), // lshift // 60: KP 8
  MATRIX( 7,1), // lshift // 61: KP 9
  MATRIX( 7,1), // lshift // 62: KP 0
  MATRIX( 7,1), // lshift // 63: KP .
  MATRIX( 7,1), // lshift // 64: EUR-2
};  
  
const unsigned char core_atari2600_modifier[] = {
  /* usb modifer bits:
     0     1      2    3    4     5      6    7
     LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
  */

  MATRIX( 2,7), // ctrl (left)
  MATRIX( 7,1), // lshift
  MATRIX( 5,7), // lalt as CBM 
  MATRIX( 7,1), // lshift  // lgui
  MATRIX( 2,7), // ctrl (right)
  MATRIX( 4,6), // rshift
  MATRIX( 5,7), // ralt as CBM
  MATRIX( 7,1)  // lshift  // rgui
};

/*
  menu.c - MiSTeryNano menu based in u8g2
*/
  
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ff.h>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <freertos/task.h>
#else
#include <FreeRTOS.h>
#include <timers.h>
#include <task.h>
#endif

#include "sdc.h"
#include "osd.h"
#include "inifile.h"
#include "menu.h"
#include "core.h"
#include "sysctrl.h"
#include "debug.h"

// this is the u8g2_font_helvR08_te with any trailing
// spaces removed
#include "font_helvR08_te.c"

static menu_t menu;

#define MENU_FORM_FSEL           -1

#define MENU_ENTRY_INDEX_ID       0
#define MENU_ENTRY_INDEX_LABEL    1
#define MENU_ENTRY_INDEX_FORM     2
#define MENU_ENTRY_INDEX_OPTIONS  2
#define MENU_ENTRY_INDEX_VARIABLE 3

static void menu_goto_form(int form, int entry) {
  menu.form = form;
  menu.entry = entry;
  menu.entries = -1;
  menu.offset = 0;
}

menu_variable_t *menu_get_vars(void) {
  return menu.vars;
}

void menu_set_value(unsigned char id, unsigned char value) {
  for(int i=0;menu.vars[i].id;i++)
    if(menu.vars[i].id == id)
      menu.vars[i].value = value;
}

// find first occurence of any char in chrs within str
const char *strchrs(const char *str, char *chrs) {
  while(*str) {
    for(unsigned int i=0;i<strlen(chrs);i++)
      if(*str == chrs[i]) return str;
    str++;
  }
  return NULL;
}

// get the n'th substring in colon separated string
static const char *menu_get_str(const char *s, int n) {
  while(n--) {
    s = strchr(s, ',');   // skip n substrings
    if(!s) return NULL;
    s = s + 1;
  }
  return s;
}

// get the n'th char in colon separated string
static char menu_get_chr(const char *s, int n) {
  while(n--) {
    s = strchr(s, ',');   // skip n substrings
    if(!s) return '\0';   // reached and of string?
    s = s + 1;
  }
  return s[0];
}

// get the n'th substring in | separated string in a colon string
static const char *menu_get_substr(const char *s, int n, int m) {
  while(n--) {
    s = strchr(s, ',');   // skip n substrings
    if(!s) return NULL;
    s = s + 1;
  }
  
  while(m--) {
    s = strchr(s, '|');   // skip m subsubstrings
    if(!s) return NULL;
    s = s + 1;
  }
  return s;
}
  
static int menu_get_int(const char *s, int n) {
  const char *str = menu_get_str(s, n);
  if(!str) return(-1);

  // The string may not be 0 terminated, but rather ; or : terminated.
  // This is fine as atoi stops parsing at the first non-digit
  return atoi(str);
}

static int menu_get_subint(const char *s, int n, int m) {
  const char *str = menu_get_substr(s, n, m);
  if(!str) return(-1);
  return atoi(str);
}

static int menu_variable_get(const char *s) {
  char id = menu_get_chr(s, MENU_ENTRY_INDEX_VARIABLE);
  if(!id) return -1;

  for(int i=0;menu.vars[i].id;i++)
    if(menu.vars[i].id == id)
      return menu.vars[i].value;    

  return -1;
}

static void menu_variable_set(const char *s, int val) {
  char id = menu_get_chr(s, MENU_ENTRY_INDEX_VARIABLE);
  if(!id) return;
  
  for(int i=0;menu.vars[i].id;i++) {
    if(menu.vars[i].id == id) {
      menu.vars[i].value = val;

      // also set this in the core
      sys_set_val(id, val);

      if(core_id == CORE_ID_ATARI_ST) {      
	// trigger cold reset if memory, chipset or TOS have been changed a
	// video change will also trigger a reset, but that's handled by
	// the ST itself
	if((id == 'C') || (id == 'M') || (id == 'T')) {
	  sys_set_val('R', 3);
	  sys_set_val('R', 0);
	}
      }
      if(core_id == CORE_ID_C64||core_id == CORE_ID_VIC20){
	// c64 core, trigger core reset if Video mode / PLL changes
	if(id == 'E') {
	  sys_set_val('R', 3);
	  sys_set_val('R', 0); }
	// c64 core, trigger c1541 reset in case DOS ROM changed
	if(id == 'D') {  
	  sys_set_val('Z', 1);
	  sys_set_val('Z', 0); }
      }
      if(core_id == CORE_ID_AMIGA) {      
	// trigger reset if memory or chipset settings changed
	if((id == 'Y') || (id == 'X') || (id == 'C')) {
	  sys_set_val('R', 1);
	  sys_set_val('R', 0);
	}
      }
    }
  }
}
  
static int menu_get_options(const char *s, int n) {
  // get possible number of values
  int num = 1;
  const char *v = menu_get_str(s, n);
  // count all '|' before next ';', ',' or '\0'
  while(*v && *v != ';' && *v != ',') {
    if(*v == '|') num++;
    v++;
  }
  return num;
}

// various 8x8 icons
static const unsigned char icn_right_bits[]  = { 0x00,0x04,0x0c,0x1c,0x3c,0x1c,0x0c,0x04 };
static const unsigned char icn_left_bits[]   = { 0x00,0x20,0x30,0x38,0x3c,0x38,0x30,0x20 };
static const unsigned char icn_floppy_bits[] = { 0xff,0x81,0x83,0x81,0xbd,0xad,0x6d,0x3f };
static const unsigned char icn_empty_bits[] =  { 0xc3,0xe7,0x7e,0x3c,0x3c,0x7e,0xe7,0xc3 };

void u8g2_DrawStrT(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, const char *s) {
  // get length of string
  int n = 0;
  while(s[n] && s[n] != ';' && s[n] != ',' && s[n] != '|') n++;

  // create a 0 terminated copy in the stack
  char buffer[n+1];
  strncpy(buffer, s, n);
  buffer[n] = '\0';
  
  u8g2_DrawStr(u8g2, x, y, buffer);
}

// Draw menu title. Submenu titles are selectable and can be used to return to the
// parent menu.
static void menu_draw_title(const char *s) {
  int x = 1;

  // draw left arrow for submenus
  if(menu.form) {
    u8g2_DrawXBM(&u8g2, 0, 1, 8, 8, icn_left_bits);    
    x = 8;
  }

  // draw title in bold and seperator line
  u8g2_SetFont(&u8g2, u8g2_font_helvB08_tr);
  u8g2_DrawStrT(&u8g2, x, 9, menu_get_str(s, 0));
  u8g2_DrawHLine(&u8g2, 0, 13, u8g2_GetDisplayWidth(&u8g2));

  if(x > 0 && menu.entry == 0)
    u8g2_DrawButtonFrame(&u8g2, 0, 9, U8G2_BTN_INV, u8g2_GetDisplayWidth(&u8g2), 1, 1);
  
  // draw the rest with normal font
  u8g2_SetFont(&u8g2, font_helvR08_te);
}

static void menu_draw_entry(int y, const char *s) {
  const char *buf = menu_get_str(s, MENU_ENTRY_INDEX_LABEL);

  int ypos = 13 + 12 * y;
  int width = u8g2_GetDisplayWidth(&u8g2);

  // all menu entries are a plain text
  u8g2_DrawStrT(&u8g2, 1, ypos, buf);
    
  // prepare highlight
  int hl_x = 0;
  int hl_w = width;
  
  // handle second string for 'L'ist entries
  if(s[0] == 'L') {
    // get variable
    int value = menu_variable_get(s);

    u8g2_DrawStrT(&u8g2, width/2, ypos, 
		  menu_get_substr(s, MENU_ENTRY_INDEX_OPTIONS, value));
		  
    hl_x = width/2;
    hl_w = width/2;
  }
  
  // some entries have a small icon to the right    
  if(s[0] == 'S')
    u8g2_DrawXBM(&u8g2, hl_w-8, ypos-8, 8, 8, icn_right_bits);    
  if(s[0] == 'F') {
    // icon depends if floppy is inserted
    u8g2_DrawXBM(&u8g2, hl_w-9, ypos-8, 8, 8,
	sdc_get_image_name(menu_get_subint(s, 2, 0))?icn_floppy_bits:icn_empty_bits);
  }
  
  if(y+menu.offset == menu.entry)
    u8g2_DrawButtonFrame(&u8g2, hl_x, ypos, U8G2_BTN_INV, hl_w, 1, 1);
}

static const int icon_skip = 10;

static void menu_fs_scroll_entry(sdc_dir_entry_t *entry) {
  int row = menu.entry - menu.offset - 1;
  int y =  13 + 12 * (row+1);
  int width = u8g2_GetDisplayWidth(&u8g2);
  int swid = u8g2_GetStrWidth(&u8g2, entry->name) + 1;

  // fill the area where the scrolling entry would show
  u8g2_SetClipWindow(&u8g2, icon_skip, y-9, width, y+12-9);  
  u8g2_DrawBox(&u8g2, icon_skip, y-9, width-icon_skip, 12);
  u8g2_SetDrawColor(&u8g2, 0);
  
  int scroll = menu.fs_scroll_cur++ - 25;   // 25 means 1 sec delay
  if(menu.fs_scroll_cur > swid-width+icon_skip+50) menu.fs_scroll_cur = 0;
  if(scroll < 0) scroll = 0;
  if(scroll > swid-width+icon_skip) scroll = swid-width+icon_skip;
  
  u8g2_DrawStr(&u8g2, icon_skip-scroll, y, entry->name);      

  // restore previous draw mode
  u8g2_SetDrawColor(&u8g2, 1);
  u8g2_SetMaxClipWindow(&u8g2);
  u8g2_SendBuffer(&u8g2);
}

void menu_timer_enable(bool on);

static void menu_fs_draw_entry(int row, sdc_dir_entry_t *entry) {      
  static const unsigned char folder_icon[] = { 0x70,0x8e,0xff,0x81,0x81,0x81,0x81,0x7e };
  static const unsigned char up_icon[] =     { 0x04,0x0e,0x1f,0x0e,0xfe,0xfe,0xfe,0x00 };
  static const unsigned char empty_icon[] =  { 0xc3,0xe7,0x7e,0x3c,0x3c,0x7e,0xe7,0xc3 };
  
  char str[strlen(entry->name)+1];
  int y =  13 + 12 * (row+1);

  // ignore leading / used by special entries
  if(entry->name[0] == '/') strcpy(str, entry->name+1);
  else                      strcpy(str, entry->name);
  
  int width = u8g2_GetDisplayWidth(&u8g2);
  
  // properly ellipsize string
  int dotlen = u8g2_GetStrWidth(&u8g2, "...");
  if(u8g2_GetStrWidth(&u8g2, str) > width-icon_skip) {
    // the entry is too long to fit the menu.    
    if(menu.entry == row+menu.offset+1) {
      menu.fs_scroll_cur = 0;
      menu.fs_scroll_entry = entry;
      // enable timer, to allow animations
      menu_timer_enable(true);
    }
    
    while(u8g2_GetStrWidth(&u8g2, str) > width-icon_skip-dotlen) str[strlen(str)-1] = 0;
    if(strlen(str) < sizeof(str)-4) strcat(str, "...");
  }
  u8g2_DrawStr(&u8g2, icon_skip, y, str);      
  
  // draw folder icon in front of directories
  if(entry->is_dir)
    u8g2_DrawXBM(&u8g2, 1, y-8, 8, 8,
		 (entry->name[0] == '/')?empty_icon:
		 strcmp(entry->name, "..")?folder_icon:
		 up_icon);

  if(menu.entry == row+menu.offset+1)
    u8g2_DrawButtonFrame(&u8g2, 0, y, U8G2_BTN_INV, width, 1, 1);     
}

// file selector events
#define FSEL_INIT   0
#define FSEL_DRAW   1
#define FSEL_DOWN   2
#define FSEL_UP     3
#define FSEL_SELECT 4

// process file selector events
static void menu_fileselector(int event) {
  static sdc_dir_t *dir = NULL;
  static const char *s;
  static int parent;
  static int drive;
  static const char *exts;
  
  if(event == FSEL_INIT) {
    // init
    s = menu.forms[menu.form];
    for(int i=0;i<menu.entry;i++) s = strchr(s, ';')+1;

    // get extensions
    exts = menu_get_substr(s, 2, 1);

    // scan files
    drive = menu_get_subint(s, 2, 0);
    
    dir = sdc_readdir(drive, NULL, exts);

    menu.entry = 1;               // start by highlighting first file entry
    menu.entries = dir->len + 1;  // incl. title
    menu.offset = 0;
    parent = menu.form;
    menu.form = MENU_FORM_FSEL;

    // try to jump to current file. Get the current image name and path
    char *name = sdc_get_image_name(drive);
    if(name) {
      // try to find name in file list
      for(int i=0;i<dir->len;i++) {
	if(strcmp(dir->files[i].name, name) == 0) {
	  // file found, adjust entry and offset
	  menu.entry = i+1;
	  
	  if(menu.entries > 5 && menu.entry > 3) {
	    if(menu.entry < menu.entries-2) menu.offset = menu.entry - 3;
	    else                              menu.offset = menu.entries-5;
	  }
	}
      }
    }
  } else if(event == FSEL_DRAW) {
    // draw
    menu_draw_title(menu_get_str(s, MENU_ENTRY_INDEX_LABEL));
    
    // draw up to four files
    menu.fs_scroll_entry = NULL;  // assume no scrolling needed
    menu_timer_enable(false);
    
    for(int i=0;i<((dir->len<4)?dir->len:4);i++)
      menu_fs_draw_entry(i, &(dir->files[i+menu.offset]));
  } else if(event == FSEL_SELECT) {

    if(!menu.entry)
      menu_goto_form(parent, 1);
    else {
      sdc_dir_entry_t *entry = &(dir->files[menu.entry - 1]);

      if(entry->is_dir) {
	if(entry->name[0] == '/') {
	  // User selected the "No Disk" entry
	  // Eject it and return to parent menu
	  menu_goto_form(parent, 1);
	  sdc_image_open(drive, NULL);
	} else {	
	  // check if we are going up one dir and try to select the
	  // directory we are coming from
	  char *prev = NULL; 
	  if(strcmp(entry->name, "..") == 0) {
	    prev = strrchr(sdc_get_cwd(drive), '/');
	    if(prev) prev++;
	  }
	  
	  menu.entry = 1;               // start by highlighting '..'
	  menu.offset = 0;
	  dir = sdc_readdir(drive, entry->name, exts);	
	  menu.entries = dir->len + 1;  // incl. title
	  
	  // prev is still valid, since sdc_readdir doesn't free the old string when going
	  // up one directory. Instead it just terminates it in the middle	
	  if(prev) {	
	    // try to find previous dir entry in current dir	  
	    for(int i=0;i<dir->len;i++) {
	      if(dir->files[i].is_dir && strcmp(dir->files[i].name, prev) == 0) {
		// file found, adjust entry and offset
		menu.entry = i+1;
		
		if(menu.entries > 5 && menu.entry > 3) {
		  if(menu.entry < menu.entries-2) menu.offset = menu.entry - 3;
		  else                              menu.offset = menu.entries-5;
		}
	      }
	    }
	  }
	}
      } else {
	// request insertion of this image
	sdc_image_open(drive, entry->name);
	// return to parent form
	menu_goto_form(parent, 1);
      }
    }
  }   
}

static void menu_draw_form(const char *s) {
  u8g2_ClearBuffer(&u8g2);

  // regular entry?
  if(menu.form >= 0) {
    // count menu entries if not done yet
    if(menu.entries < 0) {
      menu.entries = 0;

      for(const char *p = s;*p && strchr(p, ';');p=strchr(p, ';')+1)
	menu.entries++;

      // this is a newly opened form and we just determined the number
      // of menu entries. Therefore, adjust the scroll offset if needed
      if(menu.entries > 5 && menu.entry > 3) {
	if(menu.entry < menu.entries-2) menu.offset = menu.entry - 3;
	else                            menu.offset = menu.entries-5;
      }
    }

    // -------- draw title -----------
    menu_draw_title(s);
    s = strchr(s, ';')+1;

    // ------- draw menu entries ------

    // skip 'offset' entries
    for(int i=0;i<menu.offset;i++)
      s = strchr(s, ';')+1;      // skip to next entry
    
    // walk over menu string
    int y = 1;
    while(*s) {
      menu_draw_entry(y++, s);    
      s = strchr(s, ';')+1;      // skip to next entry
    }
  } else if(menu.form == MENU_FORM_FSEL)
    menu_fileselector(FSEL_DRAW);
  
  u8g2_SendBuffer(&u8g2);
}

static void menu_select(void) {
  if(menu.form == MENU_FORM_FSEL) {
    menu_fileselector(FSEL_SELECT);
    return;
  }
    
  const char *s = menu.forms[menu.form];
  // skip to current entry (incl. title)
  for(int i=0;i<menu.entry;i++) s = strchr(s, ';')+1;
  
  menu_debugf("Selected: %s", s);

  // if the title was selected, then goto parent form
  if(!menu.entry) {
    menu_debugf("parent");
    menu_goto_form(menu_get_subint(s, 1,0), menu_get_subint(s, 1,1));
    return;
  }
  
  switch(*s) {
  case 'F':
    // user has choosen a file selector
    menu_fileselector(FSEL_INIT);
    break;
    
  case 'S':
    // user has choosen a submenu
    menu_goto_form(menu_get_int(s, MENU_ENTRY_INDEX_FORM), 1);
    break;

  case 'L': {
    // user has choosen a selection list
    int value = menu_variable_get(s) + 1;
    int max_value = menu_get_options(s, MENU_ENTRY_INDEX_OPTIONS)-1;
    if(value > max_value) value = 0;    
    menu_variable_set(s, value);
  } break;

  case 'B': {
    // user has choosen a button
    signed char id = menu_get_chr(s, 2);
    
    if(id == 'S')
      inifile_write();

    // normal reset
    if(id == 'R') {    
      sys_set_val('R', 1);
      sys_set_val('R', 0);
      osd_enable(OSD_INVISIBLE);  // hide OSD
    }

    // cold boot
    if(id == 'B') {    
      sys_set_val('R', 3);
      sys_set_val('R', 0);
      osd_enable(OSD_INVISIBLE);  // hide OSD
    }

    // c64 and vic20 core, c1541 reset
    if(id == 'Z') {    
      sys_set_val('Z', 1);
      sys_set_val('Z', 0);
      osd_enable(OSD_INVISIBLE);  // hide OSD
    }
  } break;
	
  default:
    menu_debugf("unknown %c", *s);    
  }
}

static int menu_entry_is_usable(void) {
  // check if the current entry in the menu is actually selectable
  // (currently only the title of the start form is not)

  // not start form? -> ok
  if(menu.form) return 1;

  return (menu.entry == 0)?0:1;
}

static void menu_entry_go(int step) {
  do {
    menu.entry += step;

    // single step wraps top/bottom, paging does not
    if(abs(step) == 1) {    
      if(menu.entry < 0) menu.entry = menu.entries + menu.entry;
      if(menu.entry >= menu.entries) menu.entry = menu.entry - menu.entries;
    } else {
      // limit to top/bottom. Afterwards step 1 in opposite direction to skip unusable entries
      if(menu.entry < 1) { menu.entry = 1; step = 1; }	
      if(menu.entry >= menu.entries) { menu.entry = menu.entries - 1; step = -1; }
    }

    // scrolling needed?
    if(step > 0) {
      if(menu.entries <= 5)                   menu.offset = 0;
      else {
	if(menu.entry <= 3)                   menu.offset = 0;
	else if(menu.entry < menu.entries-2) menu.offset = menu.entry - 3;
	else                                   menu.offset = menu.entries-5;
      }
    }

    if(step < 0) {
      if(menu.entries <= 5)                   menu.offset = 0;
      else {
	if(menu.entry <= 2)                   menu.offset = 0;
	else if(menu.entry < menu.entries-3) menu.offset = menu.entry - 2;
	else                                   menu.offset = menu.entries-5;
      }
    }
    
    // give file selector a chance to adjust scroll
    if(menu.form == MENU_FORM_FSEL)
      menu_fileselector((step>0)?FSEL_DOWN:FSEL_UP);
    
  } while(!menu_entry_is_usable());
}

void menu_do(int event) {
  // -1 is a timer event used to scroll the current file name if it's to long
  // for the OSD
  if(event < 0) {
    if((menu.form == MENU_FORM_FSEL) && (menu.fs_scroll_entry))
      menu_fs_scroll_entry(menu.fs_scroll_entry);
    
    return;
  }
  
  if(event)  {
    if(event == MENU_EVENT_SHOW)   osd_enable(OSD_VISIBLE);
    if(event == MENU_EVENT_HIDE)   osd_enable(OSD_INVISIBLE);
    
    if(event == MENU_EVENT_UP)     menu_entry_go(-1);
    if(event == MENU_EVENT_DOWN)   menu_entry_go( 1);

    if(event == MENU_EVENT_PGUP)   menu_entry_go(-4);
    if(event == MENU_EVENT_PGDOWN) menu_entry_go( 4);

    if(event == MENU_EVENT_SELECT) menu_select();
  }  
  menu_draw_form(menu.forms[menu.form]);
}

TimerHandle_t menu_timer_handle;
// queue to forward key press events from USB to MENU
QueueHandle_t menu_queue = NULL;

void menu_timer_enable(bool on) {
  if(on) xTimerStart(menu_timer_handle, 0);
  else   xTimerStop(menu_timer_handle, 0);
}

// a 25Hz timer that can be activated by the menu whenever animations
// are displayed and which should be updated constantly
static void menu_timer(__attribute__((unused)) TimerHandle_t pxTimer) {
  static long msg = -1;
  xQueueSendToBack(menu_queue, &msg,  ( TickType_t ) 0);
}

static void menu_task(__attribute__((unused)) void *parms) {
  menu_debugf("task running");

  // wait for user events
  while(1) {
    // receive events from usb    
    long cmd;
    xQueueReceive(menu_queue, &cmd, 0xffffffffUL);
    menu_debugf("command %ld", cmd);
    menu_do(cmd);
  }
}

void menu_init(void) {
  menu_debugf("initializing");
  
  memset(&menu, 0, sizeof(menu));

  menu.forms = core_get_forms();
  menu.vars = core_get_variables();

  osd_init();

  // read config etc from sd card
  if(inifile_read() != 0) {
    // reading ini file failed
    
    // set core specific defaults
    core_set_default_images();
  }

  menu_goto_form(0, 1); // first form selected at start
  
  // send initial values for all variables
  for(int i=0;menu.vars[i].id;i++)
    sys_set_val(menu.vars[i].id, menu.vars[i].value);
  
  // release the core's reset, so it can start
  // and cold reset the core, just in case ...
  sys_set_val('R', 3);
  sys_set_val('R', 0);
  
  if(core_id == CORE_ID_C64||core_id == CORE_ID_VIC20) {  // c64 core, c1541 reset at power-up
    sys_set_val('Z', 1);
    sys_set_val('Z', 0);
  }

  // switch MCU controlled leds off
  sys_set_leds(0x00);

  menu_do(0);

  // create a 25 Hz timer that frequently wakes the OSD thread
  // allowing for animations
  menu_timer_handle = xTimerCreate("Menu timer", pdMS_TO_TICKS(40), pdTRUE,
				   NULL, menu_timer);

  // message queue from USB to OSD
  menu_queue = xQueueCreate(10, sizeof( long ) );

  // start a thread for the on screen display    
  xTaskCreate(menu_task, (char *)"menu_task", 2048, NULL, configMAX_PRIORITIES-3, NULL);
}
 
void menu_notify(unsigned long msg) {  
  xQueueSendToBackFromISR(menu_queue, &msg,  ( TickType_t ) 0);
}


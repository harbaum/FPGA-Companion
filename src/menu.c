/*
  menu.c - MiSTeryNano menu based in u8g2

  This version includes the old static MiSTeryNano type of menu
  as we as the new config driven one.

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
#include <freertos/queue.h>
#else
#include <FreeRTOS.h>
#include <timers.h>
#include <task.h>
#include <queue.h>
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

static menu_legacy_t menu;

// some constants for arrangement
// The OSD (currently) is 64 pixel high. To allow for a proper
// box around a text line, it needs to be 12 pixels high. A total
// of five lines is 5*12 = 60 + title seperation line
#define MENU_LINE_Y      13   // y pos of seperator line
#define MENU_ENTRY_H     12   // height of regular menu entries
#define MENU_ENTRY_BASE   9   // font baseline offset


#define MENU_FORM_FSEL           -1

#define MENU_ENTRY_INDEX_ID       0
#define MENU_ENTRY_INDEX_LABEL    1
#define MENU_ENTRY_INDEX_FORM     2
#define MENU_ENTRY_INDEX_OPTIONS  2
#define MENU_ENTRY_INDEX_VARIABLE 3

/* new menu state */
typedef struct {
  int type;
  int selected; 
  int scroll;
  union {
    config_menu_t *menu;  
    config_fsel_t *fsel;
  };
  
  // file selector related
  sdc_dir_t *dir;
} menu_state_t;

static menu_state_t *menu_state = NULL;

/* =========== handling of variables ============= */
static menu_variable_t **variables = NULL;

menu_variable_t **menu_get_variables(void) {
  return variables;
}

static int menu_variable_get(char id) {
  for(int i=0;variables[i];i++)
    if(variables[i]->id == id)
      return variables[i]->value;

  return 0;  
}

static void menu_variable_set(char id, int value) {
  for(int i=0;variables[i];i++) {
    if(variables[i]->id == id) {
      if(variables[i]->value != value) {
	variables[i]->value = value;
	// also set this in the core
	sys_set_val(id, value);
      }
    }
  }
}

static void menu_setup_variable(char id, int value) {
  // menu_debugf("setup variable '%c' = %d", id, value);

  // simply return if variable already exists
  int i;
  for(i=0;variables[i];i++)
    if(variables[i]->id == id)
      return;

  // allocate new entry
  menu_variable_t *variable = malloc(sizeof(menu_variable_t));
  variable->id = id;
  variable->value = value;

  // menu_debugf("new variable index %d", i);
  variables = reallocarray(variables, i+2, sizeof(menu_variable_t*));
  variables[i] = variable;
  variables[i+1] = NULL;
}

static void menu_setup_menu_variables(config_menu_t *menu) {
  config_menu_entry_t *me = menu->entries;
  for(int cnt=0;me[cnt].type != CONFIG_MENU_ENTRY_UNKNOWN;cnt++) {
    if(me[cnt].type == CONFIG_MENU_ENTRY_MENU)
      menu_setup_menu_variables(me[cnt].menu);
    
    if(me[cnt].type == CONFIG_MENU_ENTRY_LIST) {
      // setup variable ...
      menu_setup_variable(me[cnt].list->id, me[cnt].list->def);
      // ... and set in core
      sys_set_val(me[cnt].list->id, me[cnt].list->def);
    }
  }
}

static void menu_setup_variables(void) {
  // variables occur in two places:
  // in the set command used in actions
  // in menu items (currently only in lists as buttons use actions)

  // actually variables should always show up in the init action,
  // otherwise they'd be uninitialited (actually set to zero ...)
  
  // add null pointer as end marker
  variables = malloc(sizeof(menu_variable_t *));
  variables[0] = NULL;
  
  // search for variables in all actions
  for(int a=0;cfg->actions[a];a++)
    // search for set commands
    for(int c=0;cfg->actions[a]->commands[c].code != CONFIG_ACTION_COMMAND_IDLE;c++)
      if(cfg->actions[a]->commands[c].code == CONFIG_ACTION_COMMAND_SET)
	menu_setup_variable(cfg->actions[a]->commands[c].set.id, 0);

  // search through menu tree for lists
  menu_setup_menu_variables(cfg->menu);  
}


static void menu_goto_form(int form, int entry) {
  menu.form = form;
  menu.entry = entry;
  menu.entries = -1;
  menu.offset = 0;
}

menu_legacy_variable_t *menu_get_vars(void) {
  return menu.vars;
}

void menu_set_value(unsigned char id, unsigned char value) {
  if(cfg)
    menu_variable_set(id, value);
  else
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

static int menu_legacy_variable_get(const char *s) {
  char id = menu_get_chr(s, MENU_ENTRY_INDEX_VARIABLE);
  if(!id) return -1;

  for(int i=0;menu.vars[i].id;i++)
    if(menu.vars[i].id == id)
      return menu.vars[i].value;    

  return -1;
}

static void menu_legacy_variable_set(const char *s, int val) {
  char id = menu_get_chr(s, MENU_ENTRY_INDEX_VARIABLE);
  if(!id) return;
  
  for(int i=0;menu.vars[i].id;i++) {
    if(menu.vars[i].id == id) {
      menu.vars[i].value = val;

      // also set this in the core
      sys_set_val(id, val);

#ifdef ENABLE_LEGACY_ATARIST
      if(core_id == CORE_ID_ATARI_ST) {      
	// trigger cold reset if memory, chipset or TOS have been changed a
	// video change will also trigger a reset, but that's handled by
	// the ST itself
	if((id == 'C') || (id == 'M') || (id == 'T')) {
	  sys_set_val('R', 3);
	  sys_set_val('R', 0);
	}
      }
#endif
#ifdef ENABLE_LEGACY_C64
    if(core_id == CORE_ID_C64){
      // c64 core, trigger core reset if Video mode / PLL changes
      if(id == 'E') {
        sys_set_val('R', 3);
        sys_set_val('R', 0); }
      // c64 core, trigger c1541 reset in case DOS ROM changed
      if(id == 'D') {  
        sys_set_val('Z', 1);
        sys_set_val('Z', 0); }
          }
#endif
    if(core_id == CORE_ID_VIC20){
      // c64 core, trigger core reset if Video mode / PLL changes
      if(id == 'E') {
        sys_set_val('R', 3);
        sys_set_val('R', 0); }
      // c64 core, trigger c1541 reset in case DOS ROM changed
      if(id == 'D') {  
        sys_set_val('Z', 1);
        sys_set_val('Z', 0); }
          }
    if(core_id == CORE_ID_VIC20){
  // trigger reset in case memory configuration change
  if(id == 'U' || id == 'X' || id == 'Y' || id == 'N') {
    sys_set_val('R', 3);
    sys_set_val('R', 0); }
  }
#ifdef ENABLE_LEGACY_AMIGA
      if(core_id == CORE_ID_AMIGA) {      
	// trigger reset if memory or chipset settings changed
	if((id == 'Y') || (id == 'X') || (id == 'C')) {
	  sys_set_val('R', 1);
	  sys_set_val('R', 0);
	}
      }
#endif
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
static void menu_legacy_draw_title(const char *s) {
  int x = 1;

  // draw left arrow for submenus
  if(menu.form) {
    u8g2_DrawXBM(&u8g2, 0, 1, 8, 8, icn_left_bits);    
    x = 8;
  }

  // draw title in bold and seperator line
  u8g2_SetFont(&u8g2, u8g2_font_helvB08_tr);
  u8g2_DrawStrT(&u8g2, x, MENU_ENTRY_BASE, menu_get_str(s, 0));
  u8g2_DrawHLine(&u8g2, 0, MENU_LINE_Y, u8g2_GetDisplayWidth(&u8g2));

  if(x > 0 && menu.entry == 0)
    u8g2_DrawButtonFrame(&u8g2, 0, MENU_ENTRY_BASE, U8G2_BTN_INV, u8g2_GetDisplayWidth(&u8g2), 1, 1);
  
  // draw the rest with normal font
  u8g2_SetFont(&u8g2, font_helvR08_te);
}

static void menu_legacy_draw_entry(int y, const char *s) {
  const char *buf = menu_get_str(s, MENU_ENTRY_INDEX_LABEL);

  int ypos = MENU_LINE_Y + MENU_ENTRY_H * y;
  int width = u8g2_GetDisplayWidth(&u8g2);

  // all menu entries are a plain text
  u8g2_DrawStrT(&u8g2, 1, ypos, buf);
    
  // prepare highlight
  int hl_x = 0;
  int hl_w = width;
  
  // handle second string for 'L'ist entries
  if(s[0] == 'L') {
    // get variable
    int value = menu_legacy_variable_get(s);

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
    u8g2_DrawXBM(&u8g2, hl_w-MENU_ENTRY_BASE, ypos-8, 8, 8,
	sdc_get_image_name(menu_get_subint(s, 2, 0))?icn_floppy_bits:icn_empty_bits);
  }
  
  if(y+menu.offset == menu.entry)
    u8g2_DrawButtonFrame(&u8g2, hl_x, ypos, U8G2_BTN_INV, hl_w, 1, 1);
}

#define FS_ICON_WIDTH 10

static void menu_legacy_fs_scroll_entry(sdc_dir_entry_t *entry) {
  int row = menu.entry - menu.offset - 1;
  int y =  MENU_LINE_Y + MENU_ENTRY_H * (row+1);
  int width = u8g2_GetDisplayWidth(&u8g2);
  int swid = u8g2_GetStrWidth(&u8g2, entry->name) + 1;

  // fill the area where the scrolling entry would show
  u8g2_SetClipWindow(&u8g2, FS_ICON_WIDTH, y-MENU_ENTRY_BASE, width, y+MENU_ENTRY_H-MENU_ENTRY_BASE);  
  u8g2_DrawBox(&u8g2, FS_ICON_WIDTH, y-MENU_ENTRY_BASE, width-FS_ICON_WIDTH, MENU_ENTRY_H);
  u8g2_SetDrawColor(&u8g2, 0);
  
  int scroll = menu.fs_scroll_cur++ - 25;   // 25 means 1 sec delay
  if(menu.fs_scroll_cur > swid-width+FS_ICON_WIDTH+50) menu.fs_scroll_cur = 0;
  if(scroll < 0) scroll = 0;
  if(scroll > swid-width+FS_ICON_WIDTH) scroll = swid-width+FS_ICON_WIDTH;
  
  u8g2_DrawStr(&u8g2, FS_ICON_WIDTH-scroll, y, entry->name);      

  // restore previous draw mode
  u8g2_SetDrawColor(&u8g2, 1);
  u8g2_SetMaxClipWindow(&u8g2);
  u8g2_SendBuffer(&u8g2);
}

static int fs_scroll_cur = -1;

static void menu_fs_scroll_entry(void) {
  // no scrolling
  if(fs_scroll_cur < 0) return;
  
  // don't scroll anything else
  if(menu_state->type != CONFIG_MENU_ENTRY_FILESELECTOR) return;
  
  int row = menu_state->selected - 1;
  int y =  MENU_LINE_Y + MENU_ENTRY_H * (row-menu_state->scroll+1);
  int width = u8g2_GetDisplayWidth(&u8g2);

  int swid = u8g2_GetStrWidth(&u8g2, menu_state->dir->files[row].name) + 1;

  // fill the area where the scrolling entry would show
  u8g2_SetClipWindow(&u8g2, FS_ICON_WIDTH, y-MENU_ENTRY_BASE, width, y+MENU_ENTRY_H-MENU_ENTRY_BASE);  
  u8g2_DrawBox(&u8g2, FS_ICON_WIDTH, y-MENU_ENTRY_BASE, width-FS_ICON_WIDTH, MENU_ENTRY_H);
  u8g2_SetDrawColor(&u8g2, 0);

  int scroll = fs_scroll_cur++ - 25;   // 25 means 1 sec delay
  if(fs_scroll_cur > swid-width+FS_ICON_WIDTH+50) fs_scroll_cur = 0;
  if(scroll < 0) scroll = 0;
  if(scroll > swid-width+FS_ICON_WIDTH) scroll = swid-width+FS_ICON_WIDTH;
  
  u8g2_DrawStr(&u8g2, FS_ICON_WIDTH-scroll, y, menu_state->dir->files[row].name);
  
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
  int y =  MENU_LINE_Y + MENU_ENTRY_H * (row+1);

  // ignore leading / used by special entries
  if(entry->name[0] == '/') strcpy(str, entry->name+1);
  else                      strcpy(str, entry->name);
  
  int width = u8g2_GetDisplayWidth(&u8g2);
  
  // properly ellipsize string
  int dotlen = u8g2_GetStrWidth(&u8g2, "...");
  if(u8g2_GetStrWidth(&u8g2, str) > width-FS_ICON_WIDTH) {
    // the entry is too long to fit the menu.    
    if(!cfg) {
      if(menu.entry == row+menu.offset+1) {
	// scroll in legacy menu    
	menu.fs_scroll_cur = 0;
	menu.fs_scroll_entry = entry;
      }
    } else {
      // check if this is the selected file and then enable scrolling
      if(row == menu_state->selected - menu_state->scroll - 1)
	fs_scroll_cur = 0;
    }
    
    // enable timer, to allow animations
    menu_timer_enable(true);
    
    while(u8g2_GetStrWidth(&u8g2, str) > width-FS_ICON_WIDTH-dotlen) str[strlen(str)-1] = 0;
    if(strlen(str) < sizeof(str)-4) strcat(str, "...");
  }
  
  u8g2_DrawStr(&u8g2, FS_ICON_WIDTH, y, str);      
  
  // draw folder icon in front of directories
  if(entry->is_dir)
    u8g2_DrawXBM(&u8g2, 1, y-8, 8, 8,
		 (entry->name[0] == '/')?empty_icon:
		 strcmp(entry->name, "..")?folder_icon:
		 up_icon);

  // frame for legacy entry
  if(!cfg && menu.entry == row+menu.offset+1)
    u8g2_DrawButtonFrame(&u8g2, 0, y, U8G2_BTN_INV, width, 1, 1);
  else if(cfg && menu_state->selected == row+menu_state->scroll+1)
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
    menu_legacy_draw_title(menu_get_str(s, MENU_ENTRY_INDEX_LABEL));
    
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
    menu_legacy_draw_title(s);
    s = strchr(s, ';')+1;

    // ------- draw menu entries ------

    // skip 'offset' entries
    for(int i=0;i<menu.offset;i++)
      s = strchr(s, ';')+1;      // skip to next entry
    
    // walk over menu string
    int y = 1;
    while(*s) {
      menu_legacy_draw_entry(y++, s);    
      s = strchr(s, ';')+1;      // skip to next entry
    }
  } else if(menu.form == MENU_FORM_FSEL)
    menu_fileselector(FSEL_DRAW);
  
  u8g2_SendBuffer(&u8g2);
}

static void menu_legacy_select(void) {
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
    int value = menu_legacy_variable_get(s) + 1;
    int max_value = menu_get_options(s, MENU_ENTRY_INDEX_OPTIONS)-1;
    if(value > max_value) value = 0;    
    menu_legacy_variable_set(s, value);
  } break;

  case 'B': {
    // user has choosen a button
    signed char id = menu_get_chr(s, 2);
    
    if(id == 'S')
      inifile_write(NULL);

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
#ifdef ENABLE_LEGACY_C64
    if(core_id == CORE_ID_C64) {
      // c64 and vic20 core, c1541 reset
      if(id == 'Z') {
        sys_set_val('Z', 1);
        sys_set_val('Z', 0);
        osd_enable(OSD_INVISIBLE);  // hide OSD
      }

      // c64 and vic20 core, detach cartridge
      if(id == 'F') {
        sys_set_val('F', 1);
        sys_set_val('F', 0);
        osd_enable(OSD_INVISIBLE);  // hide OSD
      }
    }
#endif
    if(core_id == CORE_ID_VIC20) {
      // c64 and vic20 core, c1541 reset
      if(id == 'Z') {
        sys_set_val('Z', 1);
        sys_set_val('Z', 0);
        osd_enable(OSD_INVISIBLE);  // hide OSD
      }

      // c64 and vic20 core, detach cartridge
      if(id == 'F') {
        sys_set_val('F', 1);
        sys_set_val('F', 0);
        osd_enable(OSD_INVISIBLE);  // hide OSD
      }
    }
  } break;
	
  default:
    menu_debugf("unknown %c", *s);    
  }
}

static int menu_legacy_entry_is_usable(void) {
  // check if the current entry in the menu is actually selectable
  // (currently only the title of the start form is not)

  // not start form? -> ok
  if(menu.form) return 1;

  return (menu.entry == 0)?0:1;
}

static void menu_legacy_entry_go(int step) {
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
    
  } while(!menu_legacy_entry_is_usable());
}

static void menu_push(void) {
  // count existing state entries as the last one would
  // have the menu pointer being the root menu
  // pointer
  int i=0;
  if(menu_state) {
    while(menu_state[i].menu != cfg->menu) i++;
    i++;
  }

  menu_debugf("stack depth %d", i);
  
  menu_state = reallocarray(menu_state, i+1, sizeof(menu_state_t));
  // move all existing entries up one
  if(i) {
    for(int j=i-1;j>=0;j--) {
      debugf("move state %d to %d", j, j+1);
      menu_state[j+1] = menu_state[j];
    }
  }
}

// a menu has been closed (which sure wasn't the root menu as that
// cannot be close). So remove the lowest state stack entry and move
// all other entries in step down
static void menu_pop(void) {
  // this should never happen ...
  if(!menu_state) return;

  // neither should this as we never really close the
  // root menu
  if(menu_state->menu == cfg->menu) {
    free(menu_state);
    menu_state = NULL;
    return;
  }
    
  // count number of entries
  int i=1; while(menu_state[i-1].menu != cfg->menu) i++;
  menu_debugf("pop stack depth %d", i);

  if(i>10) exit(0);
  
  // move all existing entries down one
  for(int j=0;j<i-1;j++) {
    debugf("move state %d to %d", j+1, j);
    menu_state[j] = menu_state[j+1];
  }  

  menu_state = reallocarray(menu_state, i-1, sizeof(menu_state_t));
}

static int menu_count_entries(void) {
  int entries = 0;

  if(menu_state->type == CONFIG_MENU_ENTRY_MENU)
    while(menu_state->menu->entries[entries].type != CONFIG_MENU_ENTRY_UNKNOWN)
      entries++;
  else if(menu_state->type == CONFIG_MENU_ENTRY_FILESELECTOR)
    entries = menu_state->dir->len;
    
  return entries+1;  // title is also an entry
}

static bool menu_is_root(void) {
  return menu_state->menu == cfg->menu;
}

static int menu_entry_is_usable(void) {
  // not root menu? Then all entries are usable
  if(!menu_is_root()) return 1;

  // in root menu only the title is unusable
  return menu_state->selected != 0;
}

static void menu_entry_go(int step) {
  int entries = menu_count_entries();
  
  do {
    menu_state->selected += step;
    
    // single step wraps top/bottom, paging does not
    if(abs(step) == 1) {    
      if(menu_state->selected < 0) menu_state->selected = entries + menu_state->selected;
      if(menu_state->selected >= entries) menu_state->selected = menu_state->selected - entries;
    } else {
      // limit to top/bottom. Afterwards step 1 in opposite
      // direction to skip unusable entries
      if(menu_state->selected < 1) { menu_state->selected = 1; step = 1; }	
      if(menu_state->selected >= entries) { menu_state->selected = entries - 1; step = -1; }
    }

    // scrolling needed?
    if(step > 0) {
      if(entries <= 5)                            menu_state->scroll = 0;
      else {
	if(menu_state->selected <= 3)             menu_state->scroll = 0;
	else if(menu_state->selected < entries-2) menu_state->scroll = menu_state->selected - 3;
	else                                      menu_state->scroll = entries-5;
      }
    }

    if(step < 0) {
      if(entries <= 5)                            menu_state->scroll = 0;
      else {
	if(menu_state->selected <= 2)             menu_state->scroll = 0;
	else if(menu_state->selected < entries-3) menu_state->scroll = menu_state->selected - 2;
	else                                      menu_state->scroll = entries-5;
      }
    }    
  } while(!menu_entry_is_usable());
}

static void menu_draw_title(const char *s, bool arrow, bool selected) {
  int x = 1;

  // draw left arrow for submenus
  if(arrow) {
    u8g2_DrawXBM(&u8g2, 0, 1, 8, 8, icn_left_bits);    
    x = 8;
  }

  // draw title in bold and seperator line
  u8g2_SetFont(&u8g2, u8g2_font_helvB08_tr);
  u8g2_DrawStr(&u8g2, x, MENU_ENTRY_BASE, menu_get_str(s, 0));
  u8g2_DrawHLine(&u8g2, 0, MENU_LINE_Y, u8g2_GetDisplayWidth(&u8g2));

  if(selected)
    u8g2_DrawButtonFrame(&u8g2, 0, MENU_ENTRY_BASE, U8G2_BTN_INV,
	 u8g2_GetDisplayWidth(&u8g2), 1, 1);
  
  // draw the rest with normal font
  u8g2_SetFont(&u8g2, font_helvR08_te);
}

static char *menuentry_get_label(config_menu_entry_t *entry) {
  if(entry->type == CONFIG_MENU_ENTRY_MENU)
    return entry->menu->label;
  if(entry->type == CONFIG_MENU_ENTRY_FILESELECTOR)
    return entry->fsel->label;
  if(entry->type == CONFIG_MENU_ENTRY_LIST)
    return entry->list->label;
  if(entry->type == CONFIG_MENU_ENTRY_BUTTON)
    return entry->button->label;
  
  return NULL;
}

static int menu_get_list_length(config_menu_entry_t *entry) {
  int len = 0;  
  for(;entry->list->listentries[len];len++);
  return len-1;
}
  
static char *menu_get_listentry(config_menu_entry_t *entry, int value) {
  if(!entry || entry->type != CONFIG_MENU_ENTRY_LIST) return NULL;

  for(int i=0;entry->list->listentries[i];i++)
    if(entry->list->listentries[i]->value == value)
      return entry->list->listentries[i]->label;

  return NULL;
}

static void menu_draw_entry(config_menu_entry_t *entry, int row, bool selected) {
  menu_debugf("row %d: %s '%s'", row,
	      config_menuentry_get_type_str(entry),
	      menuentry_get_label(entry));

  // all menu entries use some kind of label
  char *s = menuentry_get_label(entry);
  int ypos = MENU_LINE_Y+MENU_ENTRY_H + MENU_ENTRY_H * row;
  int width = u8g2_GetDisplayWidth(&u8g2);
  
  // all menu entries are a plain text
  u8g2_DrawStr(&u8g2, 1, ypos, s);
    
  // prepare highlight
  int hl_x = 0;
  int hl_w = width;

  // handle second string for list entries
  if(entry->type == CONFIG_MENU_ENTRY_LIST) {
    // get matching variable
    int value = menu_variable_get(entry->list->id);
    char *str = menu_get_listentry(entry, value);
    if(str) u8g2_DrawStr(&u8g2, width/2, ypos, str);
		  
    hl_x = width/2;
    hl_w = width/2;
  }
  
  // some entries have a small icon to the right    
  if(entry->type == CONFIG_MENU_ENTRY_MENU)
    u8g2_DrawXBM(&u8g2, hl_w-8, ypos-8, 8, 8, icn_right_bits);
  if(entry->type == CONFIG_MENU_ENTRY_FILESELECTOR) {
    // icon depends if floppy is inserted xyz
    u8g2_DrawXBM(&u8g2, hl_w-MENU_ENTRY_BASE, ypos-8, 8, 8,
	 sdc_get_image_name(entry->fsel->index)?icn_floppy_bits:icn_empty_bits);
  }
  
  if(selected)
    u8g2_DrawButtonFrame(&u8g2, hl_x, ypos, U8G2_BTN_INV, hl_w, 1, 1);
}

static int menu_wrap_text(int y_in, const char *msg) {  
  // fetch words until the width is exceeded
  const char *p = msg;
  char *b = NULL;
  int y = y_in;
  
  u8g2_SetFont(&u8g2, font_helvR08_te);
  while(*msg && *p) {
    // search for end of word
    while(*p && *p != ' ') p++;
    
    // allocate substring
    b = realloc(b, p-msg+1);
    strncpy(b, msg, p-msg);
    b[p-msg]='\0';
    
    // check if this is now too long for screen
    if((u8g2_GetStrWidth(&u8g2, b) >  u8g2_GetDisplayWidth(&u8g2))) {
      // cut last word to fit to screen
      while(*p == ' ') p--;
      while(*p != ' ') p--;
      b[p-msg]='\0';

      if(y_in) u8g2_DrawStr(&u8g2, (u8g2_GetDisplayWidth(&u8g2)-u8g2_GetStrWidth(&u8g2, b))/2, y, b);
      y+=11;
      
      msg = ++p;
    }
    while(*p == ' ') p++;
  }
  
  if(y_in) u8g2_DrawStr(&u8g2, (u8g2_GetDisplayWidth(&u8g2)-u8g2_GetStrWidth(&u8g2, b))/2, y, b);
  y+=11;

  free(b);

  return y;
}

// draw a dialog box
void menu_draw_dialog(const char *title,  const char *msg) {
  u8g2_ClearBuffer(&u8g2);

  // MENU_LINE_Y is the height of the title incl line
  int y = (64 - MENU_LINE_Y - menu_wrap_text(0, msg))/2;
  
  u8g2_SetFont(&u8g2, u8g2_font_helvB08_tr);
  
  int width = u8g2_GetDisplayWidth(&u8g2);
  int swid = u8g2_GetStrWidth(&u8g2, title);
 
  // draw title in bold and seperator line
  u8g2_DrawStr(&u8g2, (width-swid)/2, y+MENU_ENTRY_BASE, title);
  u8g2_DrawHLine(&u8g2, (width-swid)/2, y+MENU_ENTRY_H, swid);

  u8g2_SetFont(&u8g2, font_helvR08_te);

  menu_wrap_text(y+23, msg);
  
  u8g2_SendBuffer(&u8g2);
}

void menu_draw(void) {
  // draw a test dialog box
  //  menu_draw_dialog("Title", "This is a rather long text which needs to wrap!");  return;
  
  u8g2_ClearBuffer(&u8g2);
 
  if(menu_state->type == CONFIG_MENU_ENTRY_MENU) {
    // =============== draw a regular menu =================
    menu_debugf("drawing '%s'", menu_state->menu->label);  
    
    // draw the title
    menu_draw_title(menu_state->menu->label, !menu_is_root(), menu_state->selected == 0);

    // draw up to four entries
    config_menu_entry_t *entry = menu_state->menu->entries;
    for(int i=0;i<4 && entry[i].type != CONFIG_MENU_ENTRY_UNKNOWN;i++)
      menu_draw_entry(entry+i+menu_state->scroll, i, menu_state->selected == menu_state->scroll+i+1);    
  } else {
    // =============== draw a fileselector =================    
    menu_debugf("drawing '%s'", menu_state->fsel->label);
    
    menu_draw_title(menu_state->fsel->label, true, menu_state->selected == 0);
    menu_timer_enable(false);
    fs_scroll_cur = -1;

    // draw up to four entries
    for(int i=0;i<4 && i<menu_state->dir->len-menu_state->scroll;i++) {            
      debugf("file %s", menu_state->dir->files[i+menu_state->scroll].name);

      menu_fs_draw_entry(i, &menu_state->dir->files[i+menu_state->scroll]);
    }
  }
    
  u8g2_SendBuffer(&u8g2);
}

void menu_goto(config_menu_t *menu) {
  menu_push();
  
  // prepare menu state ...
  menu_state->menu = menu;
  menu_state->selected = 1;
  menu_state->scroll = 0;
  menu_state->type = CONFIG_MENU_ENTRY_MENU;
}

static void menu_file_selector_open(config_menu_entry_t *entry) {
  menu_push();
  menu_state->fsel = entry->fsel;
  menu_state->selected = 1;
  menu_state->scroll = 0;
  menu_state->type = CONFIG_MENU_ENTRY_FILESELECTOR;
  
  // scan file system
  menu_state->dir = sdc_readdir(entry->fsel->index, NULL, (void*)entry->fsel->ext);
  // try to jump to current file. Get the current image name and path
  char *name = sdc_get_image_name(entry->fsel->index);
  if(name) {
    debugf("trying to jump to %s", name);
    // try to find name in file list
    for(int i=0;i<menu_state->dir->len;i++) {
      if(strcmp(menu_state->dir->files[i].name, name) == 0) {
	debugf("found preset entry %d", i);
	
	// file found, adjust entry and offset
	menu_state->selected = i+1;
	
	if(menu_state->dir->len > 4 && menu_state->selected > 3) {
	  debugf("more than 4 files an selected is > 3");
	  if(menu_state->selected < menu_state->dir->len-1) menu_state->scroll = menu_state->selected - 3;
	  else                                              menu_state->scroll = menu_state->dir->len-4;
	}
      }
    }
  }
  
}

static void menu_fileselector_select(sdc_dir_entry_t *entry) {
  int drive = menu_state->fsel->index;
  debugf("drive %d, file selected '%s'", drive, entry->name);
    
  if(entry->is_dir) {
    if(entry->name[0] == '/') {
      // User selected the "No Disk" entry
      // return to parent form
      menu_pop();
      // Eject
      sdc_image_open(drive, NULL);
    } else {	
      // check if we are going up one dir and try to select the
      // directory we are coming from
      char *prev = NULL; 
      if(strcmp(entry->name, "..") == 0) {
	prev = strrchr(sdc_get_cwd(drive), '/');
	if(prev) prev++;
      }

      menu_state->selected = 1;   // start by highlighting '..'
      menu_state->scroll = 0;
      menu_state->dir = sdc_readdir(drive, entry->name, (void*)menu_state->fsel->ext);	
      
      // prev is still valid, since sdc_readdir doesn't free the old string when going
      // up one directory. Instead it just terminates it in the middle	
      if(prev) {
	menu_debugf("up to %s", prev);
	
	// try to find previous dir entry in current dir	  
	for(int i=0;i<menu_state->dir->len;i++) {
	  if(menu_state->dir->files[i].is_dir && strcmp(menu_state->dir->files[i].name, prev) == 0) {
	    // file found, adjust entry and offset
	    menu_state->selected = i+1;

	    if(menu_state->dir->len > 4 && menu_state->selected > 3) {
	      if(menu_state->selected < menu_state->dir->len - 1) menu_state->scroll = menu_state->selected - 3;
	      else                                                menu_state->scroll = menu_state->selected - 5;
	    }
	  }
	}
      }
    }
  } else {
    // request insertion of this image
    sdc_image_open(drive, entry->name);
    
    // return to parent form
    menu_pop();
  }
}

static void menu_select(void) {
  // if the title was selected, then goto parent form
  if(menu_state->selected == 0) {
    menu_pop();
    return;
  }

  // in fileselector
  if(menu_state->type == CONFIG_MENU_ENTRY_FILESELECTOR) {
    menu_fileselector_select(&menu_state->dir->files[menu_state->selected-1]);
    return;
  }
  
  config_menu_entry_t *entry = menu_state->menu->entries + menu_state->selected - 1;
  menu_debugf("Selected: %s '%s'", config_menuentry_get_type_str(entry), menuentry_get_label(entry));

  switch(entry->type) {
  case CONFIG_MENU_ENTRY_FILESELECTOR:
    // user has choosen a file selector
    menu_file_selector_open(entry);
    break;
    
  case CONFIG_MENU_ENTRY_MENU:
    menu_goto(entry->menu);
    break;

  case CONFIG_MENU_ENTRY_LIST: {
    // user has choosen a selection list
    int value = menu_variable_get(entry->list->id) + 1;
    int max_value = menu_get_list_length(entry);
    if(value > max_value) value = 0;    
    menu_variable_set(entry->list->id, value);

    // check if there's an action connected to changing this
    // list. This e.g. happens when changing system settings is
    // meant to trigger a (cold) boot
    if(entry->list->action)
      sys_run_action(entry->list->action);

  } break;

  case CONFIG_MENU_ENTRY_BUTTON:
    if(entry->button->action)
      sys_run_action(entry->button->action);
    break;
	
  default:
    menu_debugf("unknown %s", config_menuentry_get_type_str(entry));    
  }
}

void menu_do(int event) {
  // -1 is a timer event used to scroll the current file name if it's to long
  // for the OSD
  if(event < 0) {
    if(!cfg) {
      // legacy menu fileselector animation
      if((menu.form == MENU_FORM_FSEL) && (menu.fs_scroll_entry))
	menu_legacy_fs_scroll_entry(menu.fs_scroll_entry);
    } else {
      if(menu_state->type == CONFIG_MENU_ENTRY_FILESELECTOR)
	menu_fs_scroll_entry();
    }
      
    return;
  }
  
  menu_debugf("do %d", event);
  
  if(event)  {
    if(event == MENU_EVENT_SHOW)   osd_enable(OSD_VISIBLE);
    if(event == MENU_EVENT_HIDE)   osd_enable(OSD_INVISIBLE);

    if(!cfg) {    
      if(event == MENU_EVENT_UP)     menu_legacy_entry_go(-1);
      if(event == MENU_EVENT_DOWN)   menu_legacy_entry_go( 1);

      if(event == MENU_EVENT_PGUP)   menu_legacy_entry_go(-4);
      if(event == MENU_EVENT_PGDOWN) menu_legacy_entry_go( 4);

      if(event == MENU_EVENT_SELECT) menu_legacy_select();
    } else {
      if(event == MENU_EVENT_UP)     menu_entry_go(-1);
      if(event == MENU_EVENT_DOWN)   menu_entry_go( 1);

      if(event == MENU_EVENT_PGUP)   menu_entry_go(-4);
      if(event == MENU_EVENT_PGDOWN) menu_entry_go( 4);

      if(event == MENU_EVENT_SELECT) menu_select();
    }
  }
  if(!cfg) menu_draw_form(menu.forms[menu.form]);
  else     menu_draw();
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
  menu_debugf("Initializing");

  // check if a config was loaded. If no, use the legacy menu
  if(!cfg) {  
    menu_debugf("Using legacy menu");
  
    memset(&menu, 0, sizeof(menu));

    menu.forms = core_get_forms();
    menu.vars = core_get_variables();

    // read config etc from sd card
    if(inifile_read(NULL) != 0) {
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
#ifdef ENABLE_LEGACY_C64
    if(core_id == CORE_ID_C64) {  // c1541 reset at power-up
      sys_set_val('F', 0);
      sys_set_val('Z', 1);
      sys_set_val('Z', 0);
    }
#endif
    if(core_id == CORE_ID_VIC20) {  // c1541 reset at power-up
      sys_set_val('F', 0);
      sys_set_val('Z', 1);
      sys_set_val('Z', 0);
    }
    menu_do(0);
  } else {
    // a config was loaded, use that
    menu_debugf("Using configured menu");

    menu_debugf("Setting up variables");
    menu_setup_variables();
    
    menu_debugf("Processing init action");
    sys_run_action_by_name("init");

    menu_goto(cfg->menu);    

    // ready to run core
    sys_run_action_by_name("ready");
  }
    
  // switch MCU controlled leds off
  sys_set_leds(0x00);
    
  // create a 25 Hz timer that frequently wakes the OSD thread
  // allowing for animations
  menu_timer_handle = xTimerCreate("Menu timer", pdMS_TO_TICKS(40), pdTRUE,
				   NULL, menu_timer);
  
  // message queue from USB to OSD
  menu_queue = xQueueCreate(10, sizeof( long ) );
  
  // start a thread for the on screen display    
  xTaskCreate(menu_task, (char *)"menu_task", 4096, NULL, configMAX_PRIORITIES-3, NULL);
}
  
void menu_notify(unsigned long msg) {
  xQueueSendToBackFromISR(menu_queue, &msg,  ( TickType_t ) 0);
}


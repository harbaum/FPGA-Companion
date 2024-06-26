#ifndef MENU_H
#define MENU_H

#include "osd.h"
#include "sdc.h"

#define MENU_EVENT_NONE   0
#define MENU_EVENT_UP     1
#define MENU_EVENT_DOWN   2
#define MENU_EVENT_LEFT   3
#define MENU_EVENT_RIGHT  4
#define MENU_EVENT_SELECT 5
#define MENU_EVENT_SHOW   6
#define MENU_EVENT_HIDE   7
#define MENU_EVENT_PGUP   8
#define MENU_EVENT_PGDOWN 9

// variables
typedef struct {
  const char id;
  union {
    int value;
    void *ptr;
  };
} menu_variable_t;

typedef struct {
  const char **forms;
  menu_variable_t *vars;
  int form;
  int entry;
  int entries;
  int offset;   // scroll offset

  // infos needed to scroll a highlighted fileselector entry
  int fs_scroll_cur;
  sdc_dir_entry_t *fs_scroll_entry;
} menu_t;

void menu_init(void);
menu_variable_t *menu_get_vars(void);
void menu_set_value(unsigned char id, unsigned char value);
void menu_do(int);
void menu_notify(unsigned long msg);

#endif // MENU_H

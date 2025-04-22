/*
  inifile.h
 */

#ifndef INIFILE_H
#define INIFILE_H

#define INIFILE_OPTION_HOTKEY   0   // HID key code
#define INIFILE_OPTION_LED      1   // 0 = blink, 1 = on, 0 = off

int inifile_read(char *);
void inifile_write(char *);
int inifile_option_get(int id);

#endif // INIFILE_H

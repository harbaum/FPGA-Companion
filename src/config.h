/*
  config.h - MiSTeryNano FPGA Companion

 */

#ifndef CONFIG_H
#define CONFIG_H

#ifndef ESP_PLATFORM
#define CONFIG_MAX_PRIORITY          (32)
#else
#define CONFIG_MAX_PRIORITY          (25)
#endif

#define MAX_DRIVES                   (6)
#define MAX_HID_DEVICES              (4)
#define MAX_XBOX_DEVICES             (2)

#endif // CONFIG_H

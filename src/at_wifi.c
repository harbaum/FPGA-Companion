/*
  at_wifi.c

  background process sending and receiving data from FPGA "port" which
  usually us serial/rs232 data. This is parsed to provice an hayes at
  command like interface to e.g. wifi
*/

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

#include "at_wifi.h"

#include <string.h>
#include <stdlib.h>

#include "sysctrl.h"

#include "debug.h"
#include "mcu_hw.h"

static QueueHandle_t rx_queue = NULL;

static void port_putc(unsigned char byte) {
  sys_port_put(byte);
  // waiting 1ms between the bytes roughly equals 9600 bit/s
  vTaskDelay(pdMS_TO_TICKS(1));
}

void at_wifi_puts(const char *str) {
  while(*str) port_putc(*str++);
}

void at_wifi_puts_n(const char *str, int len) {
  while(len--) port_putc(*str++);
}

static void port_line(char *command) {
  if(strcasecmp(command, "athelp") == 0) {
    at_wifi_puts("Supported commands:\r\n");
    at_wifi_puts("  atscan\r\n");
    at_wifi_puts("  atssid <ssid>,<passphrase>\r\n");
    at_wifi_puts("  atd <url>:<port>\r\n");
  } else if(strcasecmp(command, "atscan") == 0) {
    mcu_hw_wifi_scan();
  } else if(strncasecmp(command, "atssid", 6) == 0) {
    // skip to ssid
    char *s = command+6;
    while(*s == ' ') s++;
    // find comma in command
    char *k = s;
    while(*k && *k != ',') k++;
    if(*k == ',') *k++ = '\0';          
    mcu_hw_wifi_connect(s, k);
  } else if(strncasecmp(command, "atd", 3) == 0) {
    char *s = command+3;
    while(*s == ' ') s++;
    // find ':' in command
    char *k = s;
    while(*k && *k != ':') k++;    // skip to port
    if(*k == ':') *k++ = '\0';          
    mcu_hw_tcp_connect(s, atoi(k));
  } else
    at_wifi_puts("Unknown command, try 'athelp'\r\n");
}

// Port implements an "AT" like interface to e.g. be used
// to control a WiFi modem
void at_wifi_port_byte(unsigned char byte) {
  xQueueSendToBack(rx_queue, &byte,  ( TickType_t ) 0);
}
  
static void at_wifi_tx(unsigned char byte) {
  static char cmd[64] = "";  // may include a full server name ...
  
  // try to send via tcp. Handle it locally if it isn't accepted
  if(mcu_hw_tcp_data(byte)) return;
  
  // echo byte back. Echo everything but newlines. In case
  // of cr append a newline  
  if(byte != '\n') {  
    port_putc(byte);
    if(byte == '\r') port_putc('\n');
  }

  // cr will always return into state 0
  if(byte == '\r') {
    port_line(cmd);
    strcpy(cmd, "");
  } else {
    // ignore any newline
    if((byte != '\n') && strlen(cmd) < sizeof(cmd)-1) {
      cmd[strlen(cmd)+1] = '\0';
      cmd[strlen(cmd)] = byte;
    }
  }
}

static void at_wifi_task(__attribute__((unused)) void *parms) {
  menu_debugf("at/wifi task running");

  // wait for user events
  while(1) {
    unsigned char byte;
    if(xQueueReceive(rx_queue, &byte, pdMS_TO_TICKS(1000))) 
      at_wifi_tx(byte);
  }
}

void at_wifi_init(void) {
  debugf("AT WIFI init");

  // start a thread to handle at/wifi io
  rx_queue = xQueueCreate(10, sizeof( unsigned char ) );
  xTaskCreate(at_wifi_task, (char *)"at_wifi_task", 2048, NULL, configMAX_PRIORITIES-10, NULL);
}


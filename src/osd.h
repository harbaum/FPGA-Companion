#ifndef OSD_H
#define OSD_H

#include "spi.h"
#include "u8g2.h"

#define OSD_INVISIBLE  0
#define OSD_VISIBLE    (!OSD_INVISIBLE)

extern u8g2_t u8g2;

void osd_init(void);
void osd_enable(char);
int osd_is_visible(void);

#endif // OSD_H

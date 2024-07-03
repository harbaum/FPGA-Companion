#ifndef CORE_H
#define CORE_H

#include <stdint.h>
#include "menu.h"

void core_set_default_images(void);
uint8_t core_map_key(uint8_t);
uint8_t core_map_modifier_key(uint8_t);
const char **core_get_forms(void);
menu_legacy_variable_t *core_get_variables(void);

#endif // CORE_H

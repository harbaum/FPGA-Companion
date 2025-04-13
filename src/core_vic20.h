//
// core_vic20.h
//

#ifndef CORE_VIC20_H
#define CORE_VIC20_H

#include <stdint.h>
#include "core.h"
#include "menu.h"

#ifdef ENABLE_LEGACY_VIC20
extern const char *core_vic20_default_images[];
extern const unsigned char core_vic20_keymap[];
extern const unsigned char core_vic20_modifier[];
extern const char *core_vic20_forms[];
extern menu_legacy_variable_t core_vic20_variables[];
#endif

#endif // CORE_VIC20_H

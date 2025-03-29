//
// core_c64.h
//

#ifndef CORE_C64_H
#define CORE_C64_H

#include <stdint.h>
#include "core.h"
#include "menu.h"

#ifdef ENABLE_LEGACY_C64
extern const char *core_c64_default_images[];
extern const unsigned char core_c64_keymap[];
extern const unsigned char core_c64_modifier[];
extern const char *core_c64_forms[];
extern menu_legacy_variable_t core_c64_variables[];
#endif

#endif // CORE_C64_H

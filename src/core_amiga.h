//
// core_amiga.h
//

#ifndef CORE_AMIGA_H
#define CORE_AMIGA_H

#include <stdint.h>
#include "menu.h"

#ifdef ENABLE_LEGACY_AMIGA
extern const char *core_amiga_default_images[];
extern const unsigned char core_amiga_keymap[];
extern const unsigned char core_amiga_modifier[];
extern const char *core_amiga_forms[];
extern menu_legacy_variable_t core_amiga_variables[];
#endif

#endif // CORE_AMIGA_H

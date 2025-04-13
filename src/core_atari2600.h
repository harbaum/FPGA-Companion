//
// core_atari2600.h
//

#ifndef CORE_ATARI2600_H
#define CORE_ATARI2600_H

#include <stdint.h>
#include "core.h"
#include "menu.h"

#ifdef ENABLE_LEGACY_ATARI_2600
extern const char *core_atari2600_default_images[];
extern const unsigned char core_atari2600_keymap[];
extern const unsigned char core_atari2600_modifier[];
extern const char *core_atari2600_forms[];
extern menu_legacy_variable_t core_atari2600_variables[];
#endif

#endif // CORE_atari2600_H

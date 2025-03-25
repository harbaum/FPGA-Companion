//
// core_atarist.h
//

#ifndef CORE_ATARIST_H
#define CORE_ATARIST_H

#include <stdint.h>
#include "core.h"
#include "menu.h"

#ifdef ENABLE_LEGACY_ATARIST
extern const char *core_atarist_default_images[];
extern const unsigned char core_atarist_keymap[];
extern const unsigned char core_atarist_modifier[];
extern const char *core_atarist_forms[];
extern menu_legacy_variable_t core_atarist_variables[];
#endif

#endif // CORE_ATARIST_H

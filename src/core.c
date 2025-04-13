/*
  core.c - Support for legacy cores
*/

#include "core.h"
#ifdef ENABLE_LEGACY_ATARIST
#include "core_atarist.h"
#endif
#ifdef ENABLE_LEGACY_C64
#include "core_c64.h"
#endif
#ifdef ENABLE_LEGACY_VIC20
#include "core_vic20.h"
#endif
#ifdef ENABLE_LEGACY_AMIGA
#include "core_amiga.h"
#endif
#ifdef ENABLE_LEGACY_ATARI_2600
#include "core_atari2600.h"
#endif
#include "sysctrl.h"    // for core_id
#include "debug.h"

void core_set_default_images(void) {
  debugf("Setting core specific defaults");
  const char **images = NULL;
  
#ifdef ENABLE_LEGACY_ATARIST
  if(core_id == CORE_ID_ATARI_ST)
    images = core_atarist_default_images;
  else
#endif
#ifdef ENABLE_LEGACY_C64
  if(core_id == CORE_ID_C64)
    images = core_c64_default_images;
  else 
#endif
#ifdef ENABLE_LEGACY_VIC20
  if(core_id == CORE_ID_VIC20)
      images = core_vic20_default_images;
#endif
#ifdef ENABLE_LEGACY_AMIGA
  else if(core_id == CORE_ID_AMIGA)
    images = core_amiga_default_images;
#endif
#ifdef ENABLE_LEGACY_ATARI_2600
else if(core_id == CORE_ID_ATARI_2600)
    images = core_atari2600_default_images;
#endif
   else
    debugf("%s: unsupported core %d", __func__, core_id);

  if(images)
    for(int drive=0;images[drive];drive++)
      sdc_set_default(drive, images[drive]);
}

uint8_t core_map_key(uint8_t code) {
#ifdef ENABLE_LEGACY_ATARIST
  if(core_id == CORE_ID_ATARI_ST)
    return core_atarist_keymap[code];
#endif
#ifdef ENABLE_LEGACY_C64
  if(core_id == CORE_ID_C64)
    return core_c64_keymap[code];
#endif
#ifdef ENABLE_LEGACY_VIC20
  if(core_id == CORE_ID_VIC20)
    return core_vic20_keymap[code];
#endif
#ifdef ENABLE_LEGACY_AMIGA
  if(core_id == CORE_ID_AMIGA)
    return core_amiga_keymap[code];
#endif
#ifdef ENABLE_LEGACY_ATARI_2600
  if(core_id == CORE_ID_ATARI_2600)
    return core_atari2600_keymap[code];
#endif

return code;
}

uint8_t core_map_modifier_key(uint8_t code) {
#ifdef ENABLE_LEGACY_ATARIST
  if(core_id == CORE_ID_ATARI_ST)
    return core_atarist_modifier[code];
#endif
#ifdef ENABLE_LEGACY_C64
  if(core_id == CORE_ID_C64)
    return core_c64_modifier[code];
#endif
#ifdef ENABLE_LEGACY_VIC20
  if(core_id == CORE_ID_VIC20)
    return core_vic20_modifier[code];
#endif
#ifdef ENABLE_LEGACY_AMIGA
  if(core_id == CORE_ID_AMIGA)
    return core_amiga_modifier[code];
#endif
#ifdef ENABLE_LEGACY_ATARI_2600
  if(core_id == CORE_ID_ATARI_2600)
    return core_atari2600_modifier[code];
#endif

    // generic modfier mapping maps the USB modfier keys
  // onto key codes 0x68-0x6f
  return 0x68+code;
}

const char **core_get_forms(void) {
#ifdef ENABLE_LEGACY_ATARIST
  if(core_id == CORE_ID_ATARI_ST)
    return core_atarist_forms;
#endif
#ifdef ENABLE_LEGACY_C64
  if(core_id == CORE_ID_C64)
    return core_c64_forms;
#endif
#ifdef ENABLE_LEGACY_VIC20
  if(core_id == CORE_ID_VIC20)
    return core_vic20_forms;
#endif
#ifdef ENABLE_LEGACY_AMIGA
  if(core_id == CORE_ID_AMIGA)
    return core_amiga_forms;
#endif
#ifdef ENABLE_LEGACY_ATARI_2600
  if(core_id == CORE_ID_ATARI_2600)
    return core_atari2600_forms;
#endif

debugf("%s: unsupported core %d", __func__, core_id);
  return NULL;
}

menu_legacy_variable_t *core_get_variables(void) {
#ifdef ENABLE_LEGACY_ATARIST
  if(core_id == CORE_ID_ATARI_ST)
    return core_atarist_variables;
#endif
#ifdef ENABLE_LEGACY_C64
  if(core_id == CORE_ID_C64)
    return core_c64_variables;
#endif
#ifdef ENABLE_LEGACY_VIC20
  if(core_id == CORE_ID_VIC20)
    return core_vic20_variables;
#endif
#ifdef ENABLE_LEGACY_AMIGA
  if(core_id == CORE_ID_AMIGA)
    return core_amiga_variables;
#endif
#ifdef ENABLE_LEGACY_ATARI_2600
if(core_id == CORE_ID_ATARI_2600)
    return core_atari2600_variables;
#endif

  debugf("%s: unsupported core %d", __func__, core_id);
  return NULL;
}

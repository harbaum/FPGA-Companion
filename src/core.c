/*
  core.c - Support for legacy cores
*/

#include "core.h"
#include "core_atarist.h"
#include "core_c64.h"
#include "core_vic20.h"
#include "core_amiga.h"
#include "sysctrl.h"    // for core_id
#include "debug.h"

void core_set_default_images(void) {
  debugf("Setting core specific defaults");
  const char **images = NULL;
  
  if(core_id == CORE_ID_ATARI_ST)
    images = core_atarist_default_images;
  else if(core_id == CORE_ID_C64)
    images = core_c64_default_images;
  else if(core_id == CORE_ID_VIC20)
    images = core_vic20_default_images;
  else if(core_id == CORE_ID_AMIGA)
    images = core_amiga_default_images;
  else
    debugf("%s: unsupported core %d", __func__, core_id);

  if(images)
    for(int drive=0;images[drive];drive++)
      sdc_set_default(drive, images[drive]);
}

uint8_t core_map_key(uint8_t code) {
  if(core_id == CORE_ID_ATARI_ST)
    return core_atarist_keymap[code];
  if(core_id == CORE_ID_C64)
    return core_c64_keymap[code];
  if(core_id == CORE_ID_VIC20)
    return core_vic20_keymap[code];
  if(core_id == CORE_ID_AMIGA)
    return core_amiga_keymap[code];

  debugf("%s: unsupported core %d", __func__, core_id);
  return 0;
}

uint8_t core_map_modifier_key(uint8_t code) {
  if(core_id == CORE_ID_ATARI_ST)
    return core_atarist_modifier[code];
  if(core_id == CORE_ID_C64)
    return core_c64_modifier[code];
  if(core_id == CORE_ID_VIC20)
    return core_vic20_modifier[code];
  if(core_id == CORE_ID_AMIGA)
    return core_amiga_modifier[code];

  debugf("%s: unsupported core %d", __func__, core_id);
  return 0;
}

const char **core_get_forms(void) {
  if(core_id == CORE_ID_ATARI_ST)
    return core_atarist_forms;
  if(core_id == CORE_ID_C64)
    return core_c64_forms;
  if(core_id == CORE_ID_VIC20)
    return core_vic20_forms;
  if(core_id == CORE_ID_AMIGA)
    return core_amiga_forms;

  debugf("%s: unsupported core %d", __func__, core_id);
  return NULL;
}

menu_variable_t *core_get_variables(void) {
  if(core_id == CORE_ID_ATARI_ST)
    return core_atarist_variables;
  if(core_id == CORE_ID_C64)
    return core_c64_variables;
  if(core_id == CORE_ID_VIC20)
    return core_vic20_variables;
  if(core_id == CORE_ID_AMIGA)
    return core_amiga_variables;

  debugf("%s: unsupported core %d", __func__, core_id);
  return NULL;
}

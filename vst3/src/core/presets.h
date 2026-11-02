// Factory programs: the 40 registrations from the service manual ("some registrations").
#pragma once
#include "params.h"

namespace smk {

struct Preset {
  const char* name;
  double values[P_COUNT];
  // a registration sets every panel control; swell pedal, touch and output settings stay as they are
  bool has(int id) const { return id >= 0 && id <= P_TOUCH_BRASS_BRILL; }
};

int presetCount();
const Preset& preset(int index);

} // namespace smk

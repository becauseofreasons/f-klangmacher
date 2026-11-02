// Processor state: read / write (shared by processor and controller).
// Parameters are stored as (id, value) pairs, so later versions can add parameters and still read old projects.
#pragma once
#include "ids.h"
#include "params.h"
#include "presets.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include <cstdint>

namespace smk {

struct PluginState {
  double values[P_COUNT];
  int32_t program = 0;
  uint32_t serial = 1979;

  // a new instance starts with registration 1 (Piano)
  PluginState() {
    defaultValues(values);
    for (int i = 0; i < P_COUNT; i++) if (preset(0).has(i)) values[i] = preset(0).values[i];
  }

  bool read(Steinberg::IBStream* stream) {
    Steinberg::IBStreamer s(stream, kLittleEndian);
    Steinberg::uint32 magic = 0, version = 0, count = 0;
    if (!s.readInt32u(magic) || magic != kStateMagic) return false;
    if (!s.readInt32u(version) || version < 1) return false;
    if (!s.readInt32u(count) || count > 4096) return false;
    defaultValues(values);
    for (Steinberg::uint32 i = 0; i < count; i++) {
      Steinberg::uint32 id = 0;
      double v = 0;
      if (!s.readInt32u(id) || !s.readDouble(v)) return false;
      if (id < Steinberg::uint32(P_COUNT) && v >= 0 && v <= 1) values[id] = v;
    }
    Steinberg::int32 p = 0;
    if (s.readInt32(p)) program = p;
    Steinberg::uint32 ser = 1979;
    if (s.readInt32u(ser)) serial = ser;
    return true;
  }
  bool write(Steinberg::IBStream* stream) const {
    Steinberg::IBStreamer s(stream, kLittleEndian);
    if (!s.writeInt32u(kStateMagic) || !s.writeInt32u(kStateVersion) || !s.writeInt32u(Steinberg::uint32(P_COUNT)))
      return false;
    for (int i = 0; i < P_COUNT; i++)
      if (!s.writeInt32u(Steinberg::uint32(i)) || !s.writeDouble(values[i])) return false;
    return s.writeInt32(program) && s.writeInt32u(serial);
  }
};

} // namespace smk

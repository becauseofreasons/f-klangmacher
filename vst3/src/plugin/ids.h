#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace smk {

// class ids - never change them, DAW projects refer to them
static const Steinberg::FUID ProcessorUID(0x6E2B91C4, 0x3D7A4F58, 0xB1E0925C, 0x47F3A86D);
static const Steinberg::FUID ControllerUID(0x9A41D7E3, 0x52C84B1F, 0x8E6F03A9, 0xD25B7C14);

// parameter ids 0..P_COUNT-1 = smk::ParamId (core/params.h); extra ids:
enum : Steinberg::Vst::ParamID {
  kParamProgram = 100,          // also the id of the program list
};

constexpr const char* kVendor = "BecauseOfReasons";
constexpr const char* kPluginName = "F. Klangmacher";
constexpr const char* kVersion = "1.0.0";

// processor state (little endian): "SMKR", u32 version, u32 count, count x (u32 id, f64 value),
// i32 program, u32 device serial
constexpr Steinberg::uint32 kStateMagic = 0x524B4D53;   // "SMKR"
constexpr Steinberg::uint32 kStateVersion = 1;

} // namespace smk

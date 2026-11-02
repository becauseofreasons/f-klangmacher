// Parameter table of the emulation: every panel control is one parameter.
// No SDK dependency: used by the core, the plugin and the editor.
// The ids are stored in DAW projects and presets - never renumber them, only append.
#pragma once
#include <cstdint>

namespace smk {

enum ParamId : int {
  // sliders (panel scale 1..10)
  P_MONO_TUNING = 0,  // centre = in tune
  P_MONO_VOLUME,
  P_VIB_DEPTH,
  P_VIB_SPEED,
  P_PORTA_TIME,
  P_VCF_ADSR,
  P_VCF_OSC,
  P_VCF_BIAS,
  P_VCF_EMPH,
  P_ATTACK,
  P_DECAY,
  P_SUSTAIN,
  P_RELEASE,
  P_VCA_ADSR,
  P_VCA_OSC,
  P_POLI_VOLUME,
  P_POLI_BRILL,
  P_STR_VOLUME,
  P_STR_BRILL,
  P_STR_SUSTAIN,
  P_STR_ATTACK,
  // knobs
  P_POLI_PITCH,       // centre = in tune
  P_OSC_MODE,         // Adsr, Release, Attack, Continuous
  // switches
  P_ENV_TRIGGER,      // Off, Trigger, Phaser
  P_LFO_WAVE,         // Square, Triangle
  P_OSC_ON,
  P_PORTA_ON,
  // registers (tabs). Mono and poli registers are mutually exclusive groups -> one list each
  P_MONO_REG,         // 0 = none, 1..12 = Tuba .. El. Guitar
  P_POLI_REG,         // 0 = none, 1..5 = Piano .. Brass Vibrato
  P_FREE,
  P_POLI_IN_MONO,     // "Poli & Strings in Mono"
  P_SPLIT,
  P_STRINGS_8,
  P_STRINGS_4,
  P_TOUCH_MONO_BRILL,
  P_TOUCH_OSCILL,
  P_TOUCH_MONO_GLIDE,
  P_TOUCH_BRASS_BRILL,
  // not on the panel
  P_EXPRESSION,       // swell pedal (MIDI CC 11)
  P_TOUCH,            // second touch of the keyboard (MIDI aftertouch)
  P_OUT_MODE,         // Stereo (Poli left, Mono right) / Mono sum
  P_OUT_LEVEL,
  P_EFT_SENS,         // E.F.T. input sensitivity (rear panel)
  P_SUSTAIN_PEDAL,    // piano / harpsichord / honky tonk sustain switch of the swell pedal (MIDI CC 64)
  P_COUNT
};

enum MonoReg { MR_NONE = 0, MR_TUBA, MR_TROMBONE, MR_TRUMPET, MR_SAX, MR_CLARINET, MR_OBOE, MR_FLUTE, MR_PICCOLO,
               MR_VIOLIN, MR_ACCORDION, MR_EL_BASS, MR_EL_GUITAR, MR_COUNT };
enum PoliReg { PR_NONE = 0, PR_PIANO, PR_HARPSICHORD, PR_HONKY_TONK, PR_BRASS, PR_BRASS_VIBRATO, PR_COUNT };
enum OscMode { OM_ADSR = 0, OM_RELEASE, OM_ATTACK, OM_CONTINUOUS };
enum EnvTrigger { ET_OFF = 0, ET_TRIGGER, ET_PHASER };

enum class Kind : uint8_t { Slider, Knob, List, Toggle };

struct ParamDef {
  int id;
  const char* name;        // ASCII, shown in the DAW
  const char* shortName;
  Kind kind;
  int steps;               // 0 = continuous, else number of values - 1
  double def;              // default, normalized 0..1
  const char* const* labels;
};

const ParamDef& paramDef(int id);
// discrete value from a normalized one (VST3 convention) and back
int toIndex(int id, double norm);
double fromIndex(int id, int index);
// default values of all parameters (normalized)
void defaultValues(double* out /* P_COUNT */);

} // namespace smk

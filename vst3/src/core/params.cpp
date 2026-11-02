#include "params.h"
#include <algorithm>

namespace smk {

namespace {
const char* const OSC_MODES[] = { "Adsr", "Release", "Attack", "Continuous" };
const char* const ENV_TRIG[] = { "Off", "Trigger", "Phaser" };
const char* const WAVES[] = { "Square", "Triangle" };
const char* const ONOFF[] = { "Off", "On" };
const char* const MONO_REGS[] = { "-", "Tuba", "Trombone", "Trumpet", "Sax", "Clarinet", "Oboe", "Flute", "Piccolo",
                                  "Violin", "Accordion", "El. Bass", "El. Guitar" };
const char* const POLI_REGS[] = { "-", "Piano", "Harpsichord", "Honky Tonk", "Brass", "Brass Vibrato" };
const char* const OUT_MODES[] = { "Stereo (Poli L / Mono R)", "Mono" };

const ParamDef DEFS[P_COUNT] = {
  { P_MONO_TUNING, "Mono Tuning", "MTune", Kind::Slider, 0, 0.5, nullptr },
  { P_MONO_VOLUME, "Mono Volume", "MVol", Kind::Slider, 0, 0.7, nullptr },
  { P_VIB_DEPTH, "Vibrato Depth", "VDepth", Kind::Slider, 0, 0.3, nullptr },
  { P_VIB_SPEED, "Vibrato Speed", "VSpeed", Kind::Slider, 0, 0.5, nullptr },
  { P_PORTA_TIME, "Portamento Timer", "PTime", Kind::Slider, 0, 0.4, nullptr },
  { P_VCF_ADSR, "VCF ADSR", "FEnv", Kind::Slider, 0, 0.5, nullptr },
  { P_VCF_OSC, "VCF Osc", "FOsc", Kind::Slider, 0, 0.0, nullptr },
  { P_VCF_BIAS, "VCF Bias", "Bias", Kind::Slider, 0, 0.4, nullptr },
  { P_VCF_EMPH, "VCF Emphasis", "Emph", Kind::Slider, 0, 0.2, nullptr },
  { P_ATTACK, "Attack", "Att", Kind::Slider, 0, 0.1, nullptr },
  { P_DECAY, "Decay", "Dec", Kind::Slider, 0, 0.4, nullptr },
  { P_SUSTAIN, "Sustain", "Sus", Kind::Slider, 0, 0.7, nullptr },
  { P_RELEASE, "Release", "Rel", Kind::Slider, 0, 0.3, nullptr },
  { P_VCA_ADSR, "VCA ADSR", "AEnv", Kind::Slider, 0, 1.0, nullptr },
  { P_VCA_OSC, "VCA Osc", "AOsc", Kind::Slider, 0, 0.0, nullptr },
  { P_POLI_VOLUME, "Poli Volume", "PVol", Kind::Slider, 0, 0.7, nullptr },
  { P_POLI_BRILL, "Poli Brilliance", "PBrill", Kind::Slider, 0, 0.5, nullptr },
  { P_STR_VOLUME, "Strings Volume", "SVol", Kind::Slider, 0, 0.7, nullptr },
  { P_STR_BRILL, "Strings Brilliance", "SBrill", Kind::Slider, 0, 0.5, nullptr },
  { P_STR_SUSTAIN, "Strings Sustain", "SSus", Kind::Slider, 0, 0.4, nullptr },
  { P_STR_ATTACK, "Strings Attack", "SAtt", Kind::Slider, 0, 0.2, nullptr },
  { P_POLI_PITCH, "Poli Pitch", "PPitch", Kind::Knob, 0, 0.5, nullptr },
  { P_OSC_MODE, "Oscillator Mode", "OMode", Kind::List, 3, 1.0, OSC_MODES },
  { P_ENV_TRIGGER, "Env Trigger", "EFT", Kind::List, 2, 0.0, ENV_TRIG },
  { P_LFO_WAVE, "Oscillator Wave", "OWave", Kind::List, 1, 1.0, WAVES },
  { P_OSC_ON, "Oscillator On", "OscOn", Kind::Toggle, 1, 0.0, ONOFF },
  { P_PORTA_ON, "Portamento On", "PortOn", Kind::Toggle, 1, 0.0, ONOFF },
  { P_MONO_REG, "Mono Register", "MReg", Kind::List, MR_COUNT - 1, 0.0, MONO_REGS },
  { P_POLI_REG, "Poli Register", "PReg", Kind::List, PR_COUNT - 1, 0.0, POLI_REGS },
  { P_FREE, "Free", "Free", Kind::Toggle, 1, 0.0, ONOFF },
  { P_POLI_IN_MONO, "Poli & Strings in Mono", "PinM", Kind::Toggle, 1, 0.0, ONOFF },
  { P_SPLIT, "Split Keyboard", "Split", Kind::Toggle, 1, 0.0, ONOFF },
  { P_STRINGS_8, "Strings 8'", "Str8", Kind::Toggle, 1, 0.0, ONOFF },
  { P_STRINGS_4, "Strings 4'", "Str4", Kind::Toggle, 1, 0.0, ONOFF },
  { P_TOUCH_MONO_BRILL, "Touch Mono Brill", "TBrill", Kind::Toggle, 1, 0.0, ONOFF },
  { P_TOUCH_OSCILL, "Touch Oscill", "TOsc", Kind::Toggle, 1, 0.0, ONOFF },
  { P_TOUCH_MONO_GLIDE, "Touch Mono Glide", "TGlide", Kind::Toggle, 1, 0.0, ONOFF },
  { P_TOUCH_BRASS_BRILL, "Touch Brass Brill", "TBBrill", Kind::Toggle, 1, 0.0, ONOFF },
  { P_EXPRESSION, "Swell Pedal", "Swell", Kind::Slider, 0, 1.0, nullptr },
  { P_TOUCH, "Second Touch", "Touch", Kind::Slider, 0, 0.0, nullptr },
  { P_OUT_MODE, "Output Mode", "Out", Kind::List, 1, 0.0, OUT_MODES },
  { P_OUT_LEVEL, "Output Level", "Level", Kind::Slider, 0, 0.7, nullptr },
  { P_EFT_SENS, "E.F.T. Sensitivity", "EftSens", Kind::Slider, 0, 0.5, nullptr },
  { P_SUSTAIN_PEDAL, "Sustain Pedal", "SusPed", Kind::Toggle, 1, 0.0, ONOFF },
};
} // namespace

const ParamDef& paramDef(int id) { return DEFS[std::max(0, std::min(int(P_COUNT) - 1, id))]; }

int toIndex(int id, double norm) {
  int s = paramDef(id).steps;
  if (s <= 0) return 0;
  return std::max(0, std::min(s, int(norm * (s + 1))));
}

double fromIndex(int id, int index) {
  int s = paramDef(id).steps;
  return s > 0 ? double(std::max(0, std::min(s, index))) / s : 0.0;
}

void defaultValues(double* out) {
  for (int i = 0; i < P_COUNT; i++) out[i] = DEFS[i].def;
}

} // namespace smk

#include "synth.h"
#include "sections.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#define SMK_FTZ 1
#elif defined(__aarch64__)
#define SMK_FTZ_ARM 1
#endif

namespace smk {

namespace {
constexpr int CHUNK = 64;            // internal block: parameters and keys are read per chunk
}

Synth::Synth() {
  defaultValues(values);
  dev = std::make_unique<Device>(Device::make(1979));
  polySec = std::make_unique<PolySection>();
  monoSec = std::make_unique<MonoSection>();
  prepare(48000, 512);
}

Synth::~Synth() = default;

void Synth::prepare(double sampleRate, int) {
  fs = sampleRate > 0 ? sampleRate : 48000;
  polySec->prepare(fs);
  monoSec->prepare(fs);
  polySec->setDevice(*dev);
  monoSec->setDevice(*dev);
  reset();
}

void Synth::reset() {
  polySec->reset();
  monoSec->reset();
  routeKeys();
  swell = swellTarget;
}

void Synth::setDevice(uint32_t serial) {
  *dev = Device::make(serial);
  polySec->setDevice(*dev);
  monoSec->setDevice(*dev);
}

void Synth::setParam(int id, double norm) {
  if (id < 0 || id >= P_COUNT) return;
  norm = clamp01(norm);
  bool splitChange = id == P_SPLIT && (norm >= 0.5) != (values[P_SPLIT] >= 0.5);
  values[id] = norm;
  if (splitChange) routeKeys();
}

void Synth::setTouchMidi(float t) { touch = float(clamp01(t)); }

// keys to the sections: mono from the 2nd C; split keyboard: poli the first 29 notes, mono the rest
void Synth::routeKeys() {
  bool split = values[P_SPLIT] >= 0.5;
  for (int k = 0; k < KEYS; k++) {
    bool poly = keyDown[k] && (!split || k < SPLIT_KEYS);
    bool mono = keyDown[k] && k >= MONO_FIRST_KEY && (!split || k >= SPLIT_KEYS);
    polySec->key(k, poly);
    monoSec->key(k, mono);
  }
}

void Synth::noteOn(int note, float) {
  int k = note - LOWEST_KEY;
  if (k < 0 || k >= KEYS || keyDown[k]) return;
  keyDown[k] = true;
  bool split = values[P_SPLIT] >= 0.5;
  if (!split || k < SPLIT_KEYS) polySec->key(k, true);
  if (k >= MONO_FIRST_KEY && (!split || k >= SPLIT_KEYS)) monoSec->key(k, true);
}

void Synth::noteOff(int note) {
  int k = note - LOWEST_KEY;
  if (k < 0 || k >= KEYS || !keyDown[k]) return;
  keyDown[k] = false;
  polySec->key(k, false);
  monoSec->key(k, false);
}

void Synth::allNotesOff() {
  for (bool& d : keyDown) d = false;
  polySec->allOff();
  monoSec->allOff();
}

void Synth::render(float* L, float* R, const float* inL, const float* inR, int n) {
#ifdef SMK_FTZ
  // flush denormals to zero while rendering (decaying filters would otherwise cost a lot of CPU)
  struct Ftz { unsigned old; Ftz() : old(_mm_getcsr()) { _mm_setcsr(old | 0x8040); } ~Ftz() { _mm_setcsr(old); } } ftz;
#elif defined(SMK_FTZ_ARM)
  // Apple Silicon / ARM64: FZ bit (24) of the FPCR register
  struct Ftz {
    uint64_t old;
    Ftz() { __asm__ __volatile__("mrs %0, fpcr" : "=r"(old)); uint64_t v = old | (uint64_t(1) << 24); __asm__ __volatile__("msr fpcr, %0" : : "r"(v)); }
    ~Ftz() { __asm__ __volatile__("msr fpcr, %0" : : "r"(old)); }
  } ftz;
#endif
  float poli[CHUNK], mono[CHUNK], eft[CHUNK];
  // swell pedal (lamp + LDR, TCA 730): the lamp is slow
  double swellCoef = rcCoef(0.06, fs);
  // output level: 0.7 = unity
  double lvTarget = values[P_OUT_LEVEL] / 0.7;
  lvTarget *= lvTarget;
  bool monoOut = toIndex(P_OUT_MODE, values[P_OUT_MODE]) == 1 || !R;
  int pos = 0;
  while (pos < n) {
    int len = std::min(CHUNK, n - pos);
    if (inL) {
      for (int i = 0; i < len; i++) eft[i] = 0.5f * (inL[pos + i] + (inR ? inR[pos + i] : inL[pos + i]));
    }
    polySec->render(values, touch, poli, len);
    monoSec->render(values, touch, poli, inL ? eft : nullptr, mono, len);   // IN POLI (K9.8) for "Poli & Strings in Mono"
    // TCA 730: voltage controlled gain on both channels (VR3/VR4: 4.6 V .. 6.6 V, about 24 dB range)
    swellTarget = std::pow(10.0, -24.0 * (1.0 - values[P_EXPRESSION]) / 20.0);
    for (int i = 0; i < len; i++) {
      swell += swellCoef * (swellTarget - swell);
      level += 0.001 * (lvTarget - level);
      double g = swell * level;
      // output stage limits softly (TCA 730)
      double l = 1.2 * std::tanh(poli[i] * g / 1.2), r = 1.2 * std::tanh(mono[i] * g / 1.2);
      if (monoOut) {
        double m = 0.7071 * (l + r);
        L[pos + i] = float(m);
        if (R) R[pos + i] = float(m);
      } else {
        L[pos + i] = float(l);
        R[pos + i] = float(r);
      }
    }
    pos += len;
  }
}

} // namespace smk

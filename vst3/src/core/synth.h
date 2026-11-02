// The whole instrument: poly section (divide-down organ part, strings with ensemble) and mono
// section (preset synthesizer), mixed like the original: Poli on one channel, Mono on the other.
// No SDK dependency; render() allocates nothing.
#pragma once
#include "params.h"
#include <cstdint>
#include <memory>

namespace smk {

class PolySection;
class MonoSection;
struct Device;

class Synth {
public:
  Synth();
  ~Synth();
  void prepare(double sampleRate, int maxBlock);
  void reset();                                  // silence, envelopes to zero
  void setParam(int id, double norm);            // normalized 0..1 like the plugin parameters
  double param(int id) const { return values[id]; }
  void noteOn(int note, float velocity);
  void noteOff(int note);
  void allNotesOff();
  void setTouchMidi(float t);                    // second touch from MIDI pressure (0..1)
  void setDevice(uint32_t serial);               // component tolerances (0 = ideal parts)
  // L/R output (R may be null: mono sum into L); E.F.T. input may be null
  void render(float* L, float* R, const float* inL, const float* inR, int n);

  // for tests
  const Device& device() const { return *dev; }
  double sampleRate() const { return fs; }
  PolySection& poly() { return *polySec; }
  MonoSection& mono() { return *monoSec; }

  static constexpr int LOWEST_KEY = 36;          // MIDI note of the lowest key (61 keys: 36..96)
  static constexpr int KEYS = 61;

private:
  double fs = 48000;
  double values[P_COUNT];
  std::unique_ptr<Device> dev;
  std::unique_ptr<PolySection> polySec;
  std::unique_ptr<MonoSection> monoSec;
  bool keyDown[KEYS] = {};
  float touch = 0;
  double swell = 1, swellTarget = 1, level = 0.7;
  void routeKeys();
};

} // namespace smk

// One instrument with its own component tolerances. The serial number is the seed; 0 = ideal parts.
// The instrument is "serviced": the tuning trimmers (VR1 poli, VR2 mono) were set by a technician, so only
// a residual of a few cents remains. Everything else (capacitors, resistors, transistors) keeps its spread.
#pragma once
#include <cstdint>

namespace smk {

struct Device {
  uint32_t serial = 0;
  // tuning residual after the service adjustment (cents)
  double poliCents = 0, monoCents = 0;
  // per key of the 61: keyer capacitor / resistor spread (factor ~1)
  double percTau[61], brassTau[61], strTau[61], keyLevel[61];
  // per note of the divider chain: duty cycle deviation of the TOS / divider outputs (0 = 50 %)
  double duty[12];
  // mono section
  double vcfCut = 1, vcfRes = 1, vcaOffset = 0, lfoRate = 1, adsrTime[4] = { 1, 1, 1, 1 }, sustain = 1;
  double sawLevel = 1, glide = 1;
  // poli section
  double poliVcfCut = 1, brassEnv = 1, strAttack = 1, strBrill = 1;
  // ensemble (delay lines)
  double chorusRate[2] = { 1, 1 }, bbdBias[3] = { 0, 0, 0 };

  static Device make(uint32_t serial);
};

} // namespace smk

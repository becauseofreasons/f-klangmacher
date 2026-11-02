// The two sections of the instrument (see synth.h). Internal header of the core.
#pragma once
#include "params.h"
#include "device.h"
#include "dsp.h"
#include <cstdint>

namespace smk {

constexpr int KEYS = 61;
constexpr int MONO_FIRST_KEY = 12;     // mono section: 49 notes from the 2nd C
constexpr int SPLIT_KEYS = 29;         // split keyboard: poli on the first 29 notes

// ---------------------------------------------------------------- poly section
// divide-down generator (TOS + dividers), keyers for percussion (piano, harpsichord, honky tonk),
// brass and strings, prefilters, poli VCF with the brass envelope, strings with delayed attack,
// delay-line ensemble and the strings low pass.
class PolySection {
public:
  void prepare(double fs);
  void setDevice(const Device& d);
  void reset();
  void key(int k, bool down);                     // k = 0..60
  void allOff();
  // vals: all parameters (normalized), touch: second touch 0..1
  // out: poli + strings after IC23 (before the swell); the same signal goes to the mono section
  // when "Poli & Strings in Mono" is on
  void render(const double* vals, float touch, float* out, int n);

  // frequency of MIDI note m from the divider chain (for tests)
  double noteHz(int m) const;

private:
  static constexpr int NOTES = 73;                // C2 (36) .. C8 (108)
  double fs = 48000;
  Device dev;
  bool down[KEYS] = {};
  // generator: one counter per pitch class (in periods of the TOS output), divider stage s = period 2^s
  double topHz[12] = {}, count[12] = {};
  // detector "X" per note (saw obtained by deriving the square): recovery state and coefficient
  double xk[NOTES] = {}, xEndC[NOTES] = {}, xv[NOTES] = {};
  uint32_t xStamp[NOTES] = {}, stamp = 0;         // X is computed once per sample, only for notes in use
  struct KeyCoef { double pAtt, pFastH, pFastR, pSlowH, pSlowR, bAtt, bRel, sAtt, sFast, sSlow; } kc[KEYS] = {};
  void updateCoefs();
  // per key envelopes (magnitudes in volts)
  double pe[KEYS] = {}, be[KEYS] = {}, se[KEYS] = {};
  bool pAttack[KEYS] = {};
  // section state
  bool sincro = false;                            // any poly key held
  double ramp = -7.5;                             // C56: delayed attack of the strings
  double bc8 = -6.8, bc4 = -6.8, bcBrass = -8.25;
  double fF = 0, fH = 0, h2Timer = 0;             // brass envelope (R163/C59, IC2/C63)
  double lfoPh = 0;                               // ensemble oscillators (also brass vibrato)
  double lfoPh2 = 0;
  double logFc = 7.0;                             // poli VCF control (log Hz), 15 ms lag
  double muteP = 0, muteH = 0, muteHonky = 0;     // 12 ms soft switching (Q27/Q28/Q29)
  // filters
  OnePole pianoHp[5], pianoLp[5], brassHp[5], strHp[3], strLp[3], outHp;
  Svf harpHp[5], honkHp[5], pianoSk, vcfA, vcfB, strSk;
  Biquad bbdIn, bbdOut[3];
  // delay lines
  static constexpr int DL = 2048;
  float dl[DL] = {};
  int dlPos = 0;
  double dlDelay[3] = {}, dlTarget[3] = {}, clk = 1;
  uint32_t ctl = 0;
  Smooth sVol, sStrVol, sBrill, sStrBrill, sStrSus, sPitch;
};

// ---------------------------------------------------------------- mono section
// generator (M110 key scanning with priority, 2 MHz VCO, sawtooth with constant amplitude, square),
// 12 preset timbres with wave shapers and prefilters, VCF (2 x Sallen-Key OTA), VCA, ADSR with preset
// networks, LFO with the synchronizing (fade) circuit, portamento, glide (second touch), E.F.T.
class MonoSection {
public:
  void prepare(double fs);
  void setDevice(const Device& d);
  void reset();
  void key(int k, bool down);
  void allOff();
  void render(const double* vals, float touch, const float* polyIn, const float* eftIn, float* out, int n);

  // for tests
  double currentHz() const { return hz; }
  double envValue() const { return env; }
  int priorityKey() const { return curKey; }

private:
  double fs = 48000;
  Device dev;
  bool down[KEYS] = {};
  int curKey = -1, lastKey = -1;
  bool gate = false, retrig = false;
  double hz = 440, targetNote = 0, curNote = 0, glide = 0;
  double portaNext = 0;
  double sawPh = 0, sqPh = 0, accPh = 0;
  double env = 0;
  int envStage = 0;                               // 0 release, 1 attack, 2 decay/sustain
  double decay2 = 0;
  double lfoPh = 0, lfoTri = 0, lfoAmp = 0;
  bool lfoUp = true;
  double attackLine = 0;
  double shaperCap = 0, guitarPulse = 0, bassPulse = 0;
  bool schmitt = false;
  double violinHp = 0;
  double eftEnv = 0, eftAvg = 0;
  bool eftHigh = false;
  Svf vcf1, vcf2;
  double vcfFb = 0;
  Smooth sVol, sCut, sEmph, sVcfEnv, sVcfOsc, sVcaEnv, sVcaOsc, sDepth, sTune;
  struct State {
    OnePole pre1, pre2, vh1, vh2, gHp, polyHp, sineLp, eftHp;
    Biquad bassBp;
    double mixPh1 = 0, mixPh2 = 0;
    int lastReg = -1;
  } st;
  double ripTimer = 0;
  uint32_t ctl = 0;
  bool ctlUpdate = true;
};

} // namespace smk

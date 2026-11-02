// Poly section of the instrument: PA 1017 (2 MHz clock, TOS), PA 874 dividers, PA 867 percussion keyers,
// PA 705/706 brass and strings keyers, PA 876 prefilters / poli VCF / switching, PA 934 delay lines.
// Signal levels are circuit volts; component names follow the service manual.
#include "sections.h"
#include "synth.h"
#include <cmath>
#include <cstring>

namespace smk {

namespace {
// AY-1-0212: divisors of the 12 outputs C#7 .. C8 (index = pitch class, C = C8)
const double TOS_DIV[12] = { 478, 902, 852, 804, 758, 716, 676, 638, 602, 568, 536, 506 };
const int TOP_MIDI[12] = { 108, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107 };
constexpr double CLOCK = 1999360.0;            // A7 = 3520 Hz with divisor 568
constexpr double KEYER_R = 560e3;
const double INV_POW2[8] = { 1, 0.5, 0.25, 0.125, 0.0625, 0.03125, 0.015625, 0.0078125 };

// "X" detector capacitor per note C2..C8 (C14..C74 and the TOS set), tau = 16.5k * C
double detectorC(int m) {
  int k = m - 36;                              // 0 = C2
  static const double tab[] = { 68, 68, 68, 68, 68, 56, 56, 56, 56, 47, 47, 47 };   // octave 2
  if (k < 12) return tab[k] * 1e-9;
  int pc = k % 12;                             // C, C#-E, F-G#, A-B pattern, one step per 4 semitones
  int oct = k / 12;                            // 1 = octave 3 ...
  static const double steps[] = { 47, 39, 33, 27, 22, 18, 15, 12, 10, 8.2, 6.8, 5.6, 4.7, 3.9, 3.3, 2.7, 2.7 };
  int group = pc == 0 ? 0 : pc <= 4 ? 1 : pc <= 8 ? 2 : 3;
  int idx = (oct - 1) * 3 + group;
  if (pc == 0) idx = (oct - 1) * 3;            // C uses the value of the previous A-B group
  return steps[std::min(16, idx)] * 1e-9;
}

// percussion: number of mixed footages and SM tap per key range (PA 867)
int footages(int k) { return k < 24 ? 4 : k < 48 ? 3 : 2; }
double smLevel(int k, bool sustain) {
  static const double sm[4] = { 3.0, 2.3, 2.0, 1.3 }, smSus[4] = { 6.9, 6.4, 6.2, 5.7 };
  int g = k < 24 ? 0 : k < 36 ? 1 : k < 48 ? 2 : 3;
  return (sustain ? smSus[g] : sm[g]) + 0.6;
}
double approach(double x, double target, double tau, double dt) { return target + (x - target) * std::exp(-dt / tau); }

// OUT ENVELOPE static part from the BRILLIANCE slider (Po3a), volts (table of the analysis)
double staticBrill(double x) {
  static const double t[11] = { 0.19, 0.61, 0.93, 1.26, 1.62, 2.06, 2.65, 3.49, 4.83, 7.33, 13.5 };
  x = clamp01(x) * 10;
  int i = std::min(9, int(x));
  return t[i] + (t[i + 1] - t[i]) * (x - i);
}
} // namespace

void PolySection::prepare(double sampleRate) {
  fs = sampleRate;
  reset();
}

void PolySection::setDevice(const Device& d) {
  dev = d;
  updateCoefs();
}

// per key RC coefficients (with the spread of the keyer capacitors)
void PolySection::updateCoefs() {
  const double dt = 1.0 / fs;
  auto a = [&](double tau) { return std::exp(-dt / tau); };
  for (int k = 0; k < KEYS; k++) {
    KeyCoef& c = kc[k];
    double tp = dev.percTau[k], tb = dev.brassTau[k], ts = dev.strTau[k];
    c.pAtt = a((k < 36 ? 270.0 : 1000.0) * 1.8e-6 * tp);
    c.pFastH = a(0.22 * tp); c.pFastR = a(0.040 * tp);
    c.pSlowH = a(6.8 * tp); c.pSlowR = a(1.23 * tp);
    c.bAtt = a(6.8e-3 * tb); c.bRel = a(53.8e-3 * tb);
    c.sAtt = a(6.8e-3 * ts); c.sFast = a(62.8e-3 * ts); c.sSlow = a(3.5 * ts);
  }
}

double PolySection::noteHz(int m) const {
  int pc = ((m % 12) + 12) % 12;
  return topHz[pc] / std::pow(2.0, (TOP_MIDI[pc] - m) / 12);
}

void PolySection::reset() {
  for (int c = 0; c < 12; c++) { count[c] = 0; topHz[c] = CLOCK / TOS_DIV[c]; }
  for (int i = 0; i < NOTES; i++) {
    double f = 440.0 * std::pow(2.0, (36 + i - 69) / 12.0), tau = 16.5e3 * detectorC(36 + i);
    xk[i] = 1.0 / (f * tau);                                       // recovery per period
    xEndC[i] = std::exp(-0.5 * xk[i]);                             // X just before the rising edge
    xv[i] = 12; xStamp[i] = 0;
  }
  stamp = 0;
  updateCoefs();
  for (int k = 0; k < KEYS; k++) { down[k] = false; pe[k] = be[k] = se[k] = 0; pAttack[k] = false; }
  sincro = false;
  ramp = -7.5; bc8 = bc4 = -6.8; bcBrass = -8.25;
  fF = fH = h2Timer = 0;
  lfoPh = lfoPh2 = 0;
  logFc = std::log(1000.0);
  muteP = muteH = muteHonky = 0;
  std::memset(dl, 0, sizeof dl);
  dlPos = 0;
  ctl = 0; clk = 1;
  for (int l = 0; l < 3; l++) dlDelay[l] = dlTarget[l] = 256.0 / 62500.0 * fs;

  // fixed filters (PA 876 DWG 5, PA 934)
  const double pLo[5] = { 39, 41, 105, 139, 300 }, pHi[5] = { 595, 595, 1440, 1830, 2380 };
  for (int s = 0; s < 5; s++) { pianoHp[s].setFc(pLo[s], fs); pianoLp[s].setFc(pHi[s], fs); pianoHp[s].reset(); pianoLp[s].reset(); }
  const double hHp[5] = { 436, 492, 1000, 660, 1200 };
  for (int s = 0; s < 5; s++) {
    harpHp[s].set(hHp[s], 0.6, fs); harpHp[s].reset();
    honkHp[s].set(hHp[s] * 1.6, 0.6, fs); honkHp[s].reset();
  }
  const double bHp[5] = { 28, 70, 160, 360, 800 };
  for (int s = 0; s < 5; s++) { brassHp[s].setFc(bHp[s], fs); brassHp[s].reset(); }
  const double sLo[3] = { 89, 128, 214 }, sHi[3] = { 1210, 1740, 4920 };
  for (int s = 0; s < 3; s++) { strHp[s].setFc(sLo[s], fs); strLp[s].setFc(sHi[s], fs); strHp[s].reset(); strLp[s].reset(); }
  pianoSk.set(2750, 1.1, fs); pianoSk.reset();
  vcfA.reset(); vcfB.reset(); strSk.reset();
  bbdIn.lowpass(std::min(13000.0, fs * 0.45), 0.6, fs); bbdIn.reset();
  for (Biquad& b : bbdOut) { b.lowpass(std::min(12500.0, fs * 0.45), 0.7, fs); b.reset(); }
  outHp.setFc(15, fs); outHp.reset();
  sVol.init(0, fs); sStrVol.init(0, fs); sBrill.init(0, fs); sStrBrill.init(0, fs); sStrSus.init(0, fs); sPitch.init(0.5, fs);
}

void PolySection::key(int k, bool d) {
  if (k < 0 || k >= KEYS || down[k] == d) return;
  down[k] = d;
  if (d) pAttack[k] = true;                     // the key bar step charges C2 through Rp
}

void PolySection::allOff() {
  for (int k = 0; k < KEYS; k++) down[k] = false;
}

void PolySection::render(const double* v, float touch, float* out, int n) {
  const double dt = 1.0 / fs;
  const int reg = toIndex(P_POLI_REG, v[P_POLI_REG]);
  const bool brass = reg == PR_BRASS || reg == PR_BRASS_VIBRATO;
  const bool piano = reg == PR_PIANO || reg == PR_HONKY_TONK;
  const bool harp = reg == PR_HARPSICHORD || reg == PR_HONKY_TONK;
  const bool honky = reg == PR_HONKY_TONK;
  const bool str8 = v[P_STRINGS_8] >= 0.5, str4 = v[P_STRINGS_4] >= 0.5;
  const bool sustainPedal = v[P_SUSTAIN_PEDAL] >= 0.5;
  const bool brassBrill = v[P_TOUCH_BRASS_BRILL] >= 0.5;

  bool any = false;
  for (int k = 0; k < KEYS; k++) if (down[k]) { any = true; break; }
  const bool edge = any && !sincro;              // SINCRO gate-on edge (paraphonic, legato does not retrigger)
  sincro = any;
  if (edge) h2Timer = 0.100;                     // IC2: 100 ms attack pulse

  // strings delayed attack: C56 (1 uF) through R137 10k + Po5 1M log; attack = 0 keeps the strings open
  const bool attackZero = v[P_STR_ATTACK] < 0.02;
  const double tauRamp = (10e3 + 1e6 * logPot(v[P_STR_ATTACK])) * 1e-6 * dev.strAttack;
  if (edge && !attackZero) ramp = -7.5;          // Q17 discharges C56
  // C.S.: 0 V while any key is held, else 0 .. -9.3 V (Po4); 0 when both registers are off
  // per key releases happen at this level (see the strings keyer below)

  const double a12 = 1 - std::exp(-dt / 0.012), a10 = 1 - std::exp(-dt / 0.010), a5 = 1 - std::exp(-dt / 0.005);
  const double aRamp = std::exp(-dt / tauRamp);

  // generator clock: POLI PITCH (+-50 cents), service residual, brass vibrato (FAST OSC of PA 934, ~6 Hz)
  double pitchCents = (sPitch.run(v[P_POLI_PITCH]) - 0.5) * 100.0 + dev.poliCents;

  for (int i = 0; i < n; i++) {
    // ------------------------------------------------ ensemble oscillators (PA 934): 0.6 Hz and 6 Hz
    lfoPh += 0.6 * dev.chorusRate[0] * dt; if (lfoPh >= 1) lfoPh -= 1;
    lfoPh2 += 6.0 * dev.chorusRate[1] * dt; if (lfoPh2 >= 1) lfoPh2 -= 1;
    const bool ctlTick = (ctl++ & 7) == 0;               // control rate: every 8 samples
    if (ctlTick) {
      double fast = std::sin(2 * PI * lfoPh2);
      double cents = pitchCents + (reg == PR_BRASS_VIBRATO ? 15.0 * fast : 0.0);
      clk = std::pow(2.0, cents / 1200.0);
      for (int l = 0; l < 3; l++) {
        double phi = l * (2 * PI / 3);
        double mod = 0.72 * std::sin(2 * PI * lfoPh + phi) + 0.28 * std::sin(2 * PI * lfoPh2 + phi);
        double fclk = 62500.0 + 12500.0 * mod;
        dlTarget[l] = 256.0 / fclk * fs * (1.0 + dev.bbdBias[l]);
      }
    }

    // ------------------------------------------------ dividers: phase per note, detector X per note
    double incTop[12];
    for (int c = 0; c < 12; c++) {
      incTop[c] = topHz[c] * clk * dt;
      count[c] += incTop[c];
      if (count[c] >= 128.0) count[c] -= 128.0;
    }
    auto phaseOf = [&](int m, double& ph, double& inc) {
      int pc = m % 12;
      int s = (TOP_MIDI[pc] - m) / 12;
      double q = count[pc] * INV_POW2[s];
      ph = q - std::floor(q);
      inc = incTop[pc] * INV_POW2[s];
    };
    // detector X of note j (C2 = 0), computed when a keyer needs it:
    // the square's falling edge pulls X to 0.6 V, it recovers to 12 V with tau = 16.5k * C, the rising edge snaps it back
    stamp++;
    auto X = [&](int j) {
      if (xStamp[j] == stamp) return xv[j];
      xStamp[j] = stamp;
      double ph, inc;
      phaseOf(36 + j, ph, inc);
      double x = ph >= 0.5 ? 12.0 - 11.4 * std::exp(-(ph - 0.5) * xk[j]) : 12.0;
      x -= 5.7 * polyBlep(ph < 0.5 ? ph + 0.5 : ph - 0.5, inc);     // drop of 11.4 V at phase 0.5
      x += 5.7 * xEndC[j] * polyBlep(ph, inc);                      // rise at phase 0
      xv[j] = x;
      return x;
    };

    // ------------------------------------------------ keyers
    double P[5] = {}, BR[5] = {}, S[3] = {};
    const double vb8 = bc8, vb4 = bc4, vbB = bcBrass;
    const double cs = (!any && (str8 || str4)) ? -9.3 * sStrSus.y : 0.0;
    const bool percOn = piano || harp;
    for (int k = 0; k < KEYS; k++) {
      const bool d = down[k];
      double& e = pe[k];
      double& b = be[k];
      double& s = se[k];
      if (!d && e < 1e-3 && b < 1e-3 && s < 1e-3) { e = b = s = 0; continue; }
      const KeyCoef& c = kc[k];
      // ---- percussion (PA 867 / PA 902): C2 2.2 uF (+ C1 10 uF while held), SM taps, 1/2^n coincidence pulse
      if (pAttack[k]) {
        e = 11.8 + (e - 11.8) * c.pAtt;
        if (e > 11.7 || !d) pAttack[k] = false;
      } else if (e > 1e-3) {
        double esm = smLevel(k, sustainPedal);
        if (e > esm) e = esm + (e - esm) * (d ? c.pFastH : c.pFastR);
        e *= d ? c.pSlowH : c.pSlowR;
      }
      int nf = footages(k);
      double thr = 0.6 * (nf + 1);
      if (percOn && e > thr) {
        double ph, inc;
        phaseOf(36 + k, ph, inc);
        double w = 1.0 / double(1 << nf);
        double amp = (e - thr) * dev.keyLevel[k];
        double p = ph >= 1.0 - w ? amp : 0.0;
        double tr = ph - (1.0 - w);
        p += 0.5 * amp * polyBlep(tr < 0 ? tr + 1.0 : tr, inc);
        p -= 0.5 * amp * polyBlep(ph, inc);
        P[std::min(4, k / 12)] += p;
      }
      // ---- brass envelope (PA 794: 6k8 / 1 uF attack, 6k8 + 47k release)
      b = d ? 14.4 + (b - 14.4) * c.bAtt : b * c.bRel;
      // ---- strings envelope (10 uF: 680 R attack, 5k6 to C.S., then the slow 560k paths)
      if (d) s = 14.4 + (s - 14.4) * c.sAtt;
      else {
        double floorV = -cs + 0.6;
        if (s > floorV) s = floorV + (s - floorV) * c.sFast;
        s *= c.sSlow;
      }
      // ---- multi-emitter keying: I = max(0, 2 (Vbc - 0.6) + Venv - X) / 560k (per footage)
      if (b > 1.2 && vbB > -8.0) BR[std::min(4, k / 12)] += std::max(0.0, 2 * (vbB - 0.6) + b - X(k));
      if (s > 1.2) {
        if (vb8 > -6.7) S[k < 24 ? 0 : k < 48 ? 1 : 2] += std::max(0.0, 2 * (vb8 - 0.6) + s - X(k));
        if (vb4 > -6.7) S[k < 12 ? 0 : k < 36 ? 1 : 2] += std::max(0.0, 2 * (vb4 - 0.6) + s - X(k + 12));
      }
    }

    // ------------------------------------------------ register switching (soft, 10-12 ms)
    muteP += ((piano ? 1.0 : 0.0) - muteP) * a12;
    muteH += ((harp ? 1.0 : 0.0) - muteH) * a12;
    muteHonky += ((honky ? 1.0 : 0.0) - muteHonky) * a12;
    bcBrass += ((brass ? 0.0 : -8.25) - bcBrass) * a10;
    if (!attackZero) {
      if (any) ramp = -0.5 + (ramp + 0.5) * aRamp;                 // Q24 holds C56 while no key is down
    } else ramp = -0.5 + (ramp + 0.5) * (1 - a10);
    bc8 += ((str8 ? ramp : -6.8) - bc8) * a5;
    bc4 += ((str4 ? ramp : -6.8) - bc4) * a5;

    // ------------------------------------------------ prefilters (strand current -> 22k / 33k pull-ups)
    const double pianoGain[5] = { 2.8, 4.7, 3.5, 4.8, 3.4 };
    const double harpGain[5] = { 1.42, 1.42, 1.42, 1.22, 1.0 };
    double pianoSum = 0, harpSum = 0, honkSum = 0, brassSum = 0;
    for (int s = 0; s < 5; s++) {
      double u = P[s] * 22e3 / KEYER_R;
      double bp = pianoLp[s].lp(pianoHp[s].hp(u));
      pianoSum += bp * pianoGain[s] * 1.1;                 // peak of the 1st order band pass ~0.9
      double hp;
      harpHp[s].run(u, nullptr, &hp);
      harpSum += hp * harpGain[s] * 17.7 / 12.0;
      double hp2;
      honkHp[s].run(u, nullptr, &hp2);
      honkSum += hp2 * harpGain[s] * 0.5 * 17.7 / 12.0;
      brassSum += brassHp[s].hp(BR[s] * 22e3 / KEYER_R) * 4.7;
    }
    pianoSum = pianoSk.run(pianoSum);
    double q30 = pianoSum * muteP * (1 - muteHonky * 0.0)
               - harpSum * muteH * (1 - muteHonky)
               - honkSum * muteHonky
               + brassSum;

    // ------------------------------------------------ OUT ENVELOPE and VCF POLI
    // brass: F (R163/C59, 28 ms, sustain) + H (IC2 pulse 100 ms, C63 20 ms / 79 ms)
    fF = approach(fF, any ? 8.6 : 0.0, 0.028, dt);
    if (h2Timer > 0) { h2Timer -= dt; fH = approach(fH, 12.3, 0.020, dt); }
    else fH = approach(fH, 0.0, 0.079, dt);
    double brill = sBrill.run(v[P_POLI_BRILL]);
    double envV;
    if (brass) {
      double g = 3.75 + 8.35 * brill;                      // IC3 gain with Po3b
      double v3 = 0.0213 * fF + 0.0145 * fH;
      envV = 0.957 * std::max(0.0, g * v3 - 0.6) * dev.brassEnv;
    } else envV = staticBrill(brill);
    if (brassBrill && !piano) envV += std::max(0.0, 0.064 * 14.0 * touch - 0.12);
    // exponential converter (Q33/Q35) with a musical scale (see analysis: literal 21 mV/oct is too steep)
    double fc = 4750.0 * std::pow(2.0, (envV - 2.0) / 0.55) * dev.poliVcfCut;
    fc = clamp(fc, 20.0, 15400.0);
    if (ctlTick) {
      logFc += (std::log(fc) - logFc) * (1 - std::exp(-8 * dt / 0.015));   // C48: 15 ms
      double f0 = std::min(std::exp(logFc), fs * 0.45);
      vcfA.set(f0, 0.745, fs);
      vcfB.set(f0, 0.745, fs);
    }
    double u = 4.0 * std::tanh(q30 / 4.0);                 // OTA input (divider 1/101)
    double vcf = vcfB.run(4.0 * std::tanh(vcfA.run(u) * 1.2 / 4.0));

    // ------------------------------------------------ strings: strand filters, delay lines, IC24
    double sIn = 0;
    const double sGain[3] = { 1.33, 1.34, 1.0 };
    for (int s = 0; s < 3; s++) sIn += strLp[s].lp(strHp[s].hp(S[s] * 33e3 / KEYER_R)) * sGain[s] * 1.25;
    // three TDA1022 lines, clocks modulated 120 degrees apart (50..75 kHz -> 3.4..5.1 ms), 100 % wet
    dl[dlPos] = float(bbdIn.run(sIn));
    double wet = 0;
    for (int l = 0; l < 3; l++) {
      dlDelay[l] += (dlTarget[l] - dlDelay[l]) * 0.125;     // smooth between control ticks
      double rp = dlPos - dlDelay[l];
      while (rp < 0) rp += DL;
      int i0 = int(rp);
      double fr = rp - i0;
      double y0 = dl[(i0 - 1 + DL) % DL], y1 = dl[i0 % DL], y2 = dl[(i0 + 1) % DL], y3 = dl[(i0 + 2) % DL];
      // cubic interpolation
      double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
      double y = ((c3 * fr + c2) * fr + c1) * fr + y1;
      wet += bbdOut[l].run(y);
    }
    dlPos = (dlPos + 1) % DL;
    double kGain = 1.0 + 1.22 * sStrBrill.run(v[P_STR_BRILL]) * dev.strBrill;      // Po6 / R270
    if (ctlTick) strSk.set(4080, 1.0 / (3.0 - kGain), fs);
    double strings = strSk.run(-wet) * kGain;

    // ------------------------------------------------ IC23 mixer: poly x1.0, strings x0.48
    // (the poly path is lifted by 2.5: level balance against strings and mono, absolute levels are not on the sheets)
    double mix = vcf * sVol.run(v[P_POLI_VOLUME]) * 2.5 + strings * sStrVol.run(v[P_STR_VOLUME]) * 0.48;
    sStrSus.run(v[P_STR_SUSTAIN]);
    out[i] = float(outHp.hp(mix) * 0.16);                 // volts -> digital full scale
  }
}

} // namespace smk

// Mono section of the instrument (PA 1017 right half, PA 875 DWG 8/9/10).
// Voltages are those of the circuit (volts, mA); comments name the parts of the service manual.
#include "sections.h"
#include "synth.h"
#include <cmath>

namespace smk {

namespace {
constexpr double C41 = 3.3e-6;                 // ADSR capacitor
constexpr double ADSR_PEAK = 11.66;            // attack ends here (IC24/2 threshold)
constexpr double ADSR_TOP = 12.9;              // attack target (LM324 high - diode)
constexpr double ADSR_FLOOR = 0.6;             // idle output
constexpr double VT_THERMAL = 0.0257;

// footage per register (semitones relative to 8')
const int FOOT[MR_COUNT] = { 0, -24, -12, 0, -12, 0, 0, 0, 12, 0, 12, -24, -12 };

double par(const double* v, int id) { return v[id]; }
int reg(const double* v) { return toIndex(P_MONO_REG, v[P_MONO_REG]); }
bool on(const double* v, int id) { return v[id] >= 0.5; }
double sigm(double x) { return 1.0 / (1.0 + std::exp(-x)); }
// exponential approach of x to target with time constant tau over dt
double approach(double x, double target, double tau, double dt) { return target + (x - target) * std::exp(-dt / tau); }
}

void MonoSection::prepare(double sampleRate) {
  fs = sampleRate;
  reset();
}

void MonoSection::setDevice(const Device& d) { dev = d; }

void MonoSection::reset() {
  for (bool& d : down) d = false;
  curKey = lastKey = -1;
  gate = retrig = false;
  env = ADSR_FLOOR;
  envStage = 0;
  sawPh = sqPh = accPh = 0;
  lfoTri = 0; lfoAmp = 0; lfoUp = true;
  attackLine = 0;
  shaperCap = guitarPulse = bassPulse = 0;
  violinHp = 1;
  eftEnv = eftAvg = 0; eftHigh = false;
  vcf1.reset(); vcf2.reset(); vcfFb = 0;
  sVol.init(0, fs); sCut.init(0, fs); sEmph.init(0, fs); sVcfEnv.init(0, fs); sVcfOsc.init(0, fs);
  sVcaEnv.init(0, fs); sVcaOsc.init(0, fs); sDepth.init(0, fs); sTune.init(0.5, fs);
  ripTimer = 0;
  portaNext = 0;
  curNote = targetNote = 60;
  glide = 0;
  st = State();
}

void MonoSection::key(int k, bool d) {
  if (k < 0 || k >= KEYS) return;
  down[k] = d;
}

void MonoSection::allOff() {
  for (bool& d : down) d = false;
}

// ---------------------------------------------------------------- one sample of the whole section
void MonoSection::render(const double* v, float touch, const float* polyIn, const float* eftIn, float* out, int n) {
  const double dt = 1.0 / fs;
  const int r = reg(v);
  const bool free = on(v, P_FREE);
  const bool preset = !free;
  const bool pluck = r == MR_EL_BASS || r == MR_EL_GUITAR;
  const bool poliIn = on(v, P_POLI_IN_MONO);
  const int eftMode = toIndex(P_ENV_TRIGGER, v[P_ENV_TRIGGER]);
  const bool eftActive = eftMode != ET_OFF && eftIn != nullptr;

  if (r != st.lastReg) {
    st.lastReg = r;
    // first-order prefilters: series R-C into the virtual earth of IC41 (DWG 8)
    const double hpHz[MR_COUNT] = { 10, 151, 321, 983, 13, 59, 1290, 10, 10, 667, 284, 2.7, 10 };
    st.pre1.setFc(hpHz[r], fs);
    st.pre2.setFc(r == MR_EL_GUITAR ? 7.2 : 1.5, fs);       // C27/C29 resp. C6 coupling
    st.bassBp.bandpass(125, 1.04, fs);
    st.polyHp.setFc(10.6, fs);
    st.eftHp.setFc(360, fs);
  }

  // --- M110: high note priority (pin 12 grounded), a new priority key sends C.RIP (re-trigger)
  int top = -1;
  for (int k = KEYS - 1; k >= MONO_FIRST_KEY; k--) if (down[k]) { top = k; break; }
  bool keyHeld = top >= 0;                    // TP low
  if (top >= 0 && top != curKey) {
    if (curKey >= 0) ripTimer = 0.004;        // monostable Q32/Q34: 4 ms "key released"
    curKey = top;
    targetNote = Synth::LOWEST_KEY + top;
    if (!on(v, P_PORTA_ON) || lastKey < 0) curNote = targetNote;
    lastKey = top;
  }
  if (!keyHeld) curKey = -1;

  // --- ADSR parameters (FREE: panel pots; PRESET: resistor networks switched by the register)
  double rAtt, rDec, rRel, vSus, decTarget;
  if (free) {
    rAtt = 1.0 / (1.0 / (1e6 * logPot(v[P_ATTACK]) + 270) + 1.0 / 100e3);      // R151/D35 path in parallel
    rDec = 1e6 * logPot(v[P_DECAY]) + 680;
    rRel = 10e3 + 1e6 * logPot(v[P_RELEASE]);
    vSus = std::max(0.7, 11.81 * v[P_SUSTAIN]);
    decTarget = 0.8;
  } else {
    double ra = 22e3;                                                           // R149
    if (r == MR_TROMBONE) ra = 1.0 / (1 / 22e3 + 1 / 15e3);                       // + R147
    else if (r == MR_TRUMPET || r == MR_CLARINET || r == MR_FLUTE || r == MR_PICCOLO || r == MR_SAX ||
             r == MR_OBOE || r == MR_VIOLIN) ra = 11e3;                          // + R146
    else if (pluck) ra = 330;                                                   // IC27 3-4 short (op-amp limited)
    rAtt = 1.0 / (1.0 / (ra + 270) + 1.0 / 100e3);
    rDec = 270e3 + 680;
    rRel = pluck ? 2.21e6 : 10e3;
    vSus = pluck ? 0.0 : (r == MR_ACCORDION || r == MR_OBOE || r == MR_VIOLIN || r == MR_NONE) ? 15.0 : 8.86;
    decTarget = 1.3;
  }
  rAtt *= dev.adsrTime[0]; rDec *= dev.adsrTime[1]; rRel *= dev.adsrTime[3];
  vSus *= dev.sustain;

  // --- LFO speed (Po22 1M log rheostat, integrator 1.1 uF)
  double lfoHz = 0.356 / ((1e6 * logPot(v[P_VIB_SPEED]) + 3.9e3) * 1.1e-6) * dev.lfoRate;
  const bool lfoSquare = toIndex(P_LFO_WAVE, v[P_LFO_WAVE]) == 0;
  const int oscMode = toIndex(P_OSC_MODE, v[P_OSC_MODE]);
  const bool oscOn = on(v, P_OSC_ON);

  // --- footage and pitch
  const int foot = FOOT[r];
  const double portaStep = 4.0 * (1.2e3 + 220e3 * v[P_PORTA_TIME]) * 47e-9;   // per semitone (M110 scanning)
  const double tuneCents = (sTune.y - 0.5) * 100.0 + dev.monoCents;

  // RC coefficients of this block
  const double aAtt = std::exp(-dt / (rAtt * C41)), aLine = std::exp(-dt / 0.1), aLfo = 1 - std::exp(-dt / 0.009),
               aGlide = 1 - std::exp(-dt / (0.035 * dev.glide));

  // EFT sensitivity
  const double eftGain = 1.0 + 100e3 * v[P_EFT_SENS] / 2.2e3;

  for (int i = 0; i < n; i++) {
    // ---------------------------------------------------- portamento: semitone steps
    if (curNote != targetNote) {
      portaNext -= dt;
      if (portaNext <= 0) {
        curNote += targetNote > curNote ? 1 : -1;
        portaNext = portaStep;
      }
    }
    // ---------------------------------------------------- gate, C.RIP, E.F.T.
    double eftSig = 0;
    bool eftTrig = false;
    if (eftActive) {
      double x = eftIn[i] * 2.0 * eftGain;                 // 1.0 full scale = 2 V from the instrument
      x = 13.0 * std::tanh(x / 13.0);                      // IC42 output swing
      eftSig = x;
      // detector: high pass ~360 Hz, gain 5..10, half wave, leaky integrator (22 ms charge, 270 ms leak, 6.2 V)
      double h = st.eftHp.hp(x) * 7.0;
      h = std::min(h, 4.0);
      double rect = h > 0 ? h : 0;
      eftEnv += dt * (rect / 0.022 - eftEnv / 0.270);
      eftEnv = clamp(eftEnv, 0.0, 6.2);
      eftAvg += (eftEnv - eftAvg) * (dt / 0.010);          // IC44c 10 ms
      double ref = eftAvg + 0.323;
      if (eftHigh) eftHigh = eftEnv > ref - 0.06; else eftHigh = eftEnv > ref + 0.06;
      eftTrig = eftHigh;
    }
    bool g = keyHeld || (eftActive && (eftMode == ET_PHASER || eftTrig));
    if (ripTimer > 0) { ripTimer -= dt; g = false; }
    // ---------------------------------------------------- ADSR (C41)
    if (g && !gate) envStage = 1;                          // C39 edge: attack flip-flop set
    if (!g) envStage = 0;
    gate = g;
    if (envStage == 1) {
      env = ADSR_TOP + (env - ADSR_TOP) * aAtt;
      if (env >= ADSR_PEAK) envStage = 2;
    } else if (envStage == 2) {
      // comparator IC24/4 holds C41 at the sustain voltage; plucked registers: double decay paths
      double gsum = 0, isum = 0;                           // conductances and currents (towards targets)
      if (!pluck || free) {
        if (env > vSus) { gsum += 1 / rDec; isum += decTarget / rDec; }
      }
      if (pluck) {
        if (env > 7.96) { gsum += 1 / 155e3; isum += 7.96 / 155e3; }                  // path A (R158)
        if (preset) { gsum += 1 / 2.2e6; isum += 1.3 / 2.2e6; }                       // path B (R157)
        if (r == MR_EL_GUITAR && env > 6.6) { gsum += 1 / 74e3; isum += 6.6 / 74e3; } // path C (R87)
      }
      if (gsum > 0) {
        double tgt = isum / gsum;
        double nv = approach(env, tgt, C41 / gsum, dt);
        if (!pluck || free) nv = std::max(nv, std::min(env, vSus));
        env = nv;
      }
      if (env < vSus && !pluck) env = std::min(vSus, approach(env, ADSR_TOP, 100e3 * C41, dt));   // creeps up via R151
    } else {
      double gsum = 1 / rRel, isum = ADSR_FLOOR / rRel;
      if (pluck && preset) {
        gsum += 1 / 2.2e6; isum += 1.3 / 2.2e6;
        if (env > 7.96) { gsum += 1 / 155e3; isum += 7.96 / 155e3; }
      }
      env = approach(env, isum / gsum, C41 / gsum, dt);
    }
    const bool attackHigh = envStage == 1;                 // LINEA ATTACK
    attackLine = attackHigh ? 1.0 : attackLine * aLine;

    // ---------------------------------------------------- LFO with the synchronizing circuit
    double ctrl;                                           // control voltage at D89
    switch (oscMode) {
      case OM_CONTINUOUS: ctrl = 15.0; if (on(v, P_TOUCH_OSCILL) && touch < 0.6) ctrl = 0; break;
      case OM_ATTACK: ctrl = attackLine * 13.5; break;
      case OM_RELEASE: ctrl = keyHeld ? 0.0 : 12.0; break;
      default: ctrl = env; break;
    }
    if (!oscOn) ctrl = 0;
    double vbq = std::max(0.0, 0.68 * (ctrl - 0.6));
    double ampTarget = clamp01((1.83 * vbq + 2.45 - 5.5) / (20.0 - 5.5));
    lfoAmp += (ampTarget - lfoAmp) * aLfo;
    {
      double stepT = 4.0 * lfoHz * dt;                     // triangle -1..1
      if (lfoUp) { lfoTri += stepT; if (lfoTri >= 1) { lfoTri = 2 - lfoTri; lfoUp = false; } }
      else { lfoTri -= stepT; if (lfoTri <= -1) { lfoTri = -2 - lfoTri; lfoUp = true; } }
    }
    double tri = 6.0 * lfoTri * lfoAmp;                    // +-6 V
    double sq = (lfoUp ? 6.3 : -6.3) * lfoAmp;
    double lfoBus = lfoSquare ? sq : tri;
    double vib = tri * 0.252 * sDepth.run(v[P_VIB_DEPTH]); // +-1.5 V into K1.12

    // ---------------------------------------------------- generator (2 MHz VCO, M110 divider)
    double touchGlide = on(v, P_TOUCH_MONO_GLIDE) ? clamp01((touch - 0.1) / 0.8) : 0.0;
    glide += (touchGlide - glide) * aGlide;
    sTune.run(v[P_MONO_TUNING]);
    if ((ctl++ & 7) == 0) {                                // control rate: pitch and filter every 8 samples
      double note = curNote + foot;
      double ideal = 440.0 * std::pow(2.0, (note - 69) / 12.0);
      double N = std::max(16.0, std::round(2.0e6 / ideal));  // integer division of the clock
      double clockCents = tuneCents + vib * 30.0 + glide * 200.0;
      hz = 2.0e6 * std::pow(2.0, clockCents / 1200.0) / N;
      ctlUpdate = true;
    }
    double inc = std::min(0.45, hz * dt);

    // saw: up ramp 0..4 V with fast reset (C15, Q12), polyBLEP
    sawPh += inc;
    if (sawPh >= 1) sawPh -= 1;
    double saw = 4.0 * dev.sawLevel * sawPh - 2.0 * dev.sawLevel * polyBlep(sawPh, inc);
    // the 741 buffer slews the reset (~8 us)
    shaperCap += (saw - shaperCap) * (1 - std::exp(-dt / 4e-6));
    saw = shaperCap;

    // ---------------------------------------------------- timbre (DWG 8) into the mixer IC41
    double x = 0;
    switch (r) {
      case MR_TUBA: x = st.pre1.hp(0.563 * saw) * 0.947; break;
      case MR_TROMBONE: x = st.pre1.hp(0.563 * saw) * 0.947; break;
      case MR_TRUMPET: x = st.pre1.hp(0.563 * saw) * 1.36; break;
      case MR_VIOLIN: {
        // diode shunts D10-D15 open after key down (C23), giving a sharp start
        double tau = keyHeld ? 0.12 : 0.185;
        violinHp = approach(violinHp, keyHeld ? 0.0 : 1.0, tau, dt);
        double rsh = 6e3 / std::max(1e-4, violinHp);
        double f1 = 1.0 / (2 * PI * (3.74e3 + rsh) * 4.7e-9);
        double f2 = 1.0 / (2 * PI * (rsh + 3e3) * 10e-9);
        st.vh1.setFc(f1, fs); st.vh2.setFc(f2, fs);
        double y = st.vh1.hp(0.205 * saw) * (rsh / (rsh + 3.74e3));
        y = st.vh2.hp(y) * (rsh / (rsh + 3e3));
        x = st.pre1.hp(y) * 0.964;
        break;
      }
      case MR_SAX: case MR_OBOE: {
        // Schmitt trigger Q44/Q48 on the saw: high above 0.61 V until the reset; 0 / 0.78 V
        const double d0 = 0.61 / 4.0;
        double p = sawPh < d0 ? 0.0 : 0.78;
        p += 0.78 * polyBlep(std::fmod(sawPh - d0 + 1.0, 1.0), inc) * 0.5;
        p -= 0.78 * polyBlep(sawPh, inc) * 0.5;
        x = r == MR_SAX ? st.pre1.hp(p) * 1.47 : st.pre1.hp(p) * 0.667;
        break;
      }
      case MR_CLARINET: {
        // CL: M110 square through R21/R22 (level not printed; about 0.45 Vpp gives the level of the other registers)
        double pp = sawPh;
        double s = pp < 0.5 ? 0.45 : 0.0;
        s += 0.225 * polyBlep(pp, inc) - 0.225 * polyBlep(std::fmod(pp + 0.5, 1.0), inc);
        x = st.pre1.hp(s) * 3.7;
        break;
      }
      case MR_FLUTE: case MR_PICCOLO: {
        // Q43 folds the saw into a triangle (2 Vpp), 4-diode sine shaper on IC33, low pass 3.4 kHz
        double tri2 = saw < 2.0 ? 4.0 - saw : saw;
        double ac = st.pre2.hp(tri2 - 3.0);                 // C6 coupling
        double sine = 1.45 * std::tanh(ac * 1.25);
        st.sineLp.setFc(3390, fs);
        x = st.sineLp.lp(sine) * 0.455 * 2.0;
        break;
      }
      case MR_ACCORDION: {
        // IC29 square at 4', CD4027 /2 (8') and /4 (16'), IC40 pulse modulated by the delay-line oscillators
        accPh += inc * 0.25;
        if (accPh >= 1) accPh -= 1;
        auto sqr = [&](double ph, double dti) {
          double s = ph < 0.5 ? 1.0 : 0.0;
          s += 0.5 * polyBlep(ph, dti) - 0.5 * polyBlep(std::fmod(ph + 0.5, 1.0), dti);
          return s;
        };
        double p16 = accPh, p8 = std::fmod(accPh * 2.0, 1.0), p4 = std::fmod(accPh * 4.0, 1.0);
        st.mixPh1 += 0.6 * dt; st.mixPh2 += 6.0 * dt;
        if (st.mixPh1 >= 1) st.mixPh1 -= 1;
        if (st.mixPh2 >= 1) st.mixPh2 -= 1;
        double mix = 0.72 * std::sin(2 * PI * st.mixPh1) + 0.28 * std::sin(2 * PI * st.mixPh2);
        double duty = clamp(0.44 + 0.06 * mix, 0.1, 0.9);   // ramp (10.8 V) against 6 V + MIX OSC
        double pw = p8 < 1 - duty ? 0.0 : 1.0;
        pw += 0.5 * polyBlep(std::fmod(p8 - (1 - duty) + 1.0, 1.0), inc * 0.5) - 0.5 * polyBlep(p8, inc * 0.5);
        double node = 0.22 * sqr(p4, inc) + 0.41 * sqr(p8, inc * 0.5) + 0.41 * sqr(p16, inc * 0.25) + 0.67 * pw;
        x = st.pre1.hp(node) * 1.0;
        break;
      }
      case MR_EL_BASS: case MR_EL_GUITAR: {
        // IC29 Schmitt: high while saw < P; P = 0.175 Vout (+1.4 V from C10 at the attack, El. Bass)
        if (attackHigh) bassPulse = 1.4; else bassPulse *= std::exp(-dt / 0.26);
        double thr = 2.36 + (r == MR_EL_BASS ? bassPulse : 0.0);
        double d0 = clamp(thr / 4.0, 0.05, 0.98);
        double p = sawPh < d0 ? 4.3 : 0.0;
        p += 2.15 * polyBlep(sawPh, inc) - 2.15 * polyBlep(std::fmod(sawPh - d0 + 1.0, 1.0), inc);
        if (r == MR_EL_BASS) {
          x = st.bassBp.run(p) * 1.87 * 0.37;               // IC30 band pass 125 Hz, Q 1.04
        } else {
          // IC31/IC32: OTA high pass, 540 Hz at the attack, falling to 0 in about 0.3 s (C31, R94)
          if (attackHigh) guitarPulse = 0.76; else guitarPulse *= std::exp(-dt / 0.1);
          double iabc = std::max(0.0, guitarPulse - 0.033);   // mA
          double fc = 2.0 * 19.2e-3 * iabc / (101.0 * 82e-9) / (2 * PI);
          double y = st.pre2.hp(p);                          // C27 / C29 coupling
          if (fc > 1.0) { st.gHp.setFc(fc, fs); y = st.gHp.hp(y); } else st.gHp.lp(y);
          x = y * 0.256;
        }
        break;
      }
      default: break;
    }
    if (poliIn && polyIn) x += st.polyHp.hp(polyIn[i]) * 1.47;   // C22 / R75
    if (eftActive) x += eftSig * 0.5;                          // AP V.C.F.

    // ---------------------------------------------------- VCF driver (IC17, lin/exp converter, VK)
    double vk = 0.00119 * hz;                                  // K1.11, linear in frequency
    double adsrLine = (preset && (r == MR_OBOE || r == MR_VIOLIN)) ? 15.0 : env;
    double vplus = 0.01168 * 0.82 * adsrLine * sVcfEnv.run(v[P_VCF_ADSR]) + 0.01168 * lfoBus * sVcfOsc.run(v[P_VCF_OSC]);
    double bias = sCut.run(v[P_VCF_BIAS]);
    if (free) vplus += 0.00958 * 12.4 * bias;
    else if (r == MR_ACCORDION || r == MR_TRUMPET) vplus += 0.035 * 0.00958 * 15.0;            // R226/R229 via R225: about +0.7 octave
    double vt = -10.0 * touch;                                 // second touch photocell (negative with pressure)
    double v17 = 23.7 * vplus - (on(v, P_TOUCH_MONO_BRILL) ? 0.041 * vt : 0.0);
    v17 = clamp(v17, -11.0, 11.0);
    double vb = 0.106 * v17 - 0.11;                           // VR2: lowest Tuba note "only slightly audible"
    double i3 = 1.25e-3 + 0.93e-3 * vk;
    double i21 = i3 * sigm(vb / VT_THERMAL);
    double iabc = std::min(0.77e-3, i21 / 4.0) * dev.vcfCut;
    double fc = 18.8 * iabc * 1e6;
    // emphasis: k = 1.8 x slider in FREE, fixed 1.0 for El. Guitar in preset mode
    double k = free ? 1.78 * sEmph.run(v[P_VCF_EMPH]) * dev.vcfRes : (r == MR_EL_GUITAR ? 1.0 : 0.0);
    k = std::min(k, 1.79);

    // ---------------------------------------------------- VCF: 2 x OTA Sallen-Key (Q = 0.745), 2x oversampled
    double y = 0;
    const double fs2 = 2 * fs;
    if (ctlUpdate) {
      double fcc = clamp(fc, 8.0, 0.42 * fs2);
      vcf1.set(fcc, 0.745, fs2);
      vcf2.set(fcc, 0.745, fs2);
      ctlUpdate = false;
    }
    for (int os = 0; os < 2; os++) {
      double u = x - k * vcfFb;
      u = 6.2 * std::tanh(u / 6.2);                          // OTA input stage (divider 1/121, 2 VT)
      double a = vcf1.run(u);
      a = 6.2 * std::tanh(a / 6.2);
      double b = vcf2.run(a);
      vcfFb = b;
      y += b;
    }
    y *= 0.5;

    // ---------------------------------------------------- VCA (IC16, exponential driver Q24-Q26)
    double v19 = 0.82 * env * sVcaEnv.run(v[P_VCA_ADSR]) + 0.18 * lfoBus * sVcaOsc.run(v[P_VCA_OSC]);
    auto i2 = [](double vv) { double raw = 1e-3 * std::exp((0.0246 * vv - 0.13) / VT_THERMAL); return raw / (1.0 + raw / 2.5e-3); };
    double irest = i2(0.82 * ADSR_FLOOR * v[P_VCA_ADSR]);    // R255 / VR3 null the output at rest
    double gain = (i2(v19) - irest) / 1.74e-3 + dev.vcaOffset;
    bool muted = r == MR_NONE && !poliIn && !eftActive;      // CM: no mono register selected
    if (muted) gain = 0;
    double o = y * gain * sVol.run(v[P_MONO_VOLUME]);
    out[i] = float(o * 0.22);                                // volts -> digital full scale
  }
}

} // namespace smk

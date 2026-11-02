// Tests of the sound model without a DAW: pitch of every footage, levels, routing, all 40 registrations,
// CPU load. Optionally writes one WAV per registration.
//   smk_test [wav folder]
#include "synth.h"
#include "sections.h"
#include "presets.h"
#include "params.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace smk;

static const double FS = 48000;
static int failures = 0;
static void check(bool ok, const std::string& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) failures++; }

struct Take { std::vector<float> l, r; };

static double pitchNear(const std::vector<float>& x, double t0, double t1, double f0, double range = 120) {
  size_t s = size_t(t0 * FS), e = std::min(x.size(), size_t(t1 * FS));
  double best = 0, bestF = 0;
  for (int c = -int(range); c <= int(range); c++) {
    double f = f0 * std::pow(2.0, c / 1200.0), w = 2 * M_PI * f / FS, k = 2 * std::cos(w), s1 = 0, s2 = 0;
    for (size_t i = s; i < e; i++) {
      double win = 0.5 - 0.5 * std::cos(2 * M_PI * double(i - s) / double(e - s));
      double s0 = x[i] * win + k * s1 - s2; s2 = s1; s1 = s0;
    }
    double m = s1 * s1 + s2 * s2 - k * s1 * s2;
    if (m > best) { best = m; bestF = f; }
  }
  return bestF;
}
static double rms(const std::vector<float>& x, double t0, double t1) {
  double q = 0; size_t s = size_t(t0 * FS), e = std::min(x.size(), size_t(t1 * FS));
  for (size_t i = s; i < e; i++) q += double(x[i]) * x[i];
  return e > s ? std::sqrt(q / double(e - s)) : 0;
}
static double peak(const std::vector<float>& x) { double p = 0; for (float v : x) p = std::max(p, double(std::fabs(v))); return p; }
static bool finite(const std::vector<float>& x) { for (float v : x) if (!std::isfinite(v)) return false; return true; }
static double cents(double f, double ref) { return 1200 * std::log2(f / ref); }

static void writeWav(const std::string& path, const Take& t) {
  std::ofstream f(path, std::ios::binary);
  auto u32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
  auto u16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
  uint32_t n = uint32_t(t.l.size());
  f.write("RIFF", 4); u32(36 + n * 4); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2);
  u32(uint32_t(FS)); u32(uint32_t(FS * 4)); u16(4); u16(16); f.write("data", 4); u32(n * 4);
  for (uint32_t i = 0; i < n; i++)
    for (float v : { t.l[i], t.r[i] }) { int s = int(std::lround(std::max(-1.0f, std::min(1.0f, v)) * 32767)); u16(uint16_t(int16_t(s))); }
}

// plays a list of (time, note, on) events and records
struct Ev { double t; int note; bool on; };
static Take play(Synth& s, const std::vector<Ev>& evs, double len) {
  Take t;
  size_t n = size_t(len * FS);
  t.l.assign(n, 0); t.r.assign(n, 0);
  size_t pos = 0, ei = 0;
  while (pos < n) {
    size_t next = n;
    if (ei < evs.size()) next = std::min(next, size_t(evs[ei].t * FS));
    next = std::min(next, pos + 256);
    if (next > pos) { s.render(t.l.data() + pos, t.r.data() + pos, nullptr, nullptr, int(next - pos)); pos = next; }
    while (ei < evs.size() && size_t(evs[ei].t * FS) <= pos) {
      if (evs[ei].on) s.noteOn(evs[ei].note, 0.8f); else s.noteOff(evs[ei].note);
      ei++;
    }
  }
  return t;
}

static void load(Synth& s, int presetIndex) {
  double v[P_COUNT];
  defaultValues(v);
  for (int i = 0; i < P_COUNT; i++) s.setParam(i, v[i]);
  if (presetIndex >= 0) for (int i = 0; i < P_COUNT; i++) if (preset(presetIndex).has(i)) s.setParam(i, preset(presetIndex).values[i]);
  s.allNotesOff();
  s.reset();
}

// a registration with only one mono register, no vibrato
static void monoOnly(Synth& s, int reg) {
  load(s, -1);
  s.setParam(P_MONO_REG, fromIndex(P_MONO_REG, reg));
  s.setParam(P_MONO_VOLUME, 0.9);
  s.setParam(P_VCA_ADSR, 0.9);
  s.setParam(P_VCF_ADSR, 0.35);
}

int main(int argc, char** argv) {
  std::string wavDir = argc > 1 ? argv[1] : "";
  Synth s;
  s.prepare(FS, 512);
  s.setDevice(0);
  s.reset();

  std::printf("Tonhöhe (ideale Bauteile):\n");
  // footage of each mono register: Tuba 32', Trombone 16', Trumpet 8', Piccolo 4' ...
  struct Foot { int reg; const char* name; double feet; };
  const Foot feet[] = { { MR_TUBA, "Tuba", 32 }, { MR_TROMBONE, "Trombone", 16 }, { MR_TRUMPET, "Trumpet", 8 },
                        { MR_SAX, "Sax", 16 }, { MR_CLARINET, "Clarinet", 8 }, { MR_OBOE, "Oboe", 8 },
                        { MR_FLUTE, "Flute", 8 }, { MR_PICCOLO, "Piccolo", 4 }, { MR_VIOLIN, "Violin", 8 },
                        { MR_ACCORDION, "Accordion", 4 }, { MR_EL_BASS, "El. Bass", 32 }, { MR_EL_GUITAR, "El. Guitar", 16 } };
  for (const Foot& f : feet) {
    monoOnly(s, f.reg);
    Take t = play(s, { { 0.05, 69, true }, { 0.9, 69, false } }, 1.2);
    double ref = 440.0 * 8.0 / f.feet;
    double hz = pitchNear(t.r, 0.25, 0.85, ref);
    char b[160];
    std::snprintf(b, sizeof b, "%-10s A4 = %7.2f Hz (%+.1f Cent zu %.1f Hz), Pegel %.3f, links %.4f", f.name, hz, cents(hz, ref), ref,
                  rms(t.r, 0.25, 0.85), rms(t.l, 0.25, 0.85));
    check(std::fabs(cents(hz, ref)) < 8 && rms(t.r, 0.25, 0.85) > 0.01 && rms(t.l, 0.25, 0.85) < 1e-4, b);
  }
  {
    load(s, 0);   // Piano
    Take t = play(s, { { 0.05, 60, true }, { 0.4, 60, false }, { 0.5, 96, true }, { 0.9, 96, false }, { 1.0, 36, true }, { 1.6, 36, false } }, 1.8);
    double a = pitchNear(t.l, 0.06, 0.38, 261.626), b = pitchNear(t.l, 0.51, 0.85, 2093.0), c = pitchNear(t.l, 1.05, 1.55, 65.41);
    char m[200];
    std::snprintf(m, sizeof m, "Piano C4 %.2f Hz (%+.1f), C7 %.1f Hz (%+.1f), C2 %.2f Hz (%+.1f Cent)", a, cents(a, 261.626), b,
                  cents(b, 2093.0), c, cents(c, 65.41));
    check(std::fabs(cents(a, 261.626)) < 5 && std::fabs(cents(b, 2093.0)) < 5 && std::fabs(cents(c, 65.41)) < 5, m);
    check(rms(t.r, 0.06, 0.38) < 1e-4, "Poli nur auf dem linken Kanal");
  }

  std::printf("Mono-Tastenumfang und Split:\n");
  {
    monoOnly(s, MR_TRUMPET);
    Take t = play(s, { { 0.05, 40, true }, { 0.4, 40, false }, { 0.5, 48, true }, { 0.9, 48, false } }, 1.0);
    check(rms(t.r, 0.1, 0.4) < 1e-4 && rms(t.r, 0.6, 0.9) > 0.01, "Mono spielt erst ab dem 2. C (MIDI 48)");
    load(s, 27);  // Split Keyboard 3: Violin + Piano
    Take u = play(s, { { 0.05, 72, true }, { 0.5, 72, false }, { 0.6, 60, true }, { 1.1, 60, false } }, 1.3);
    std::printf("  (Split C5: links %.6f rechts %.4f)\n", rms(u.l, 0.1, 0.45), rms(u.r, 0.1, 0.45));
    check(rms(u.r, 0.1, 0.45) > 0.01 && rms(u.l, 0.1, 0.45) < 1e-4, "Split: C5 nur Mono");
    check(rms(u.l, 0.65, 1.05) > 0.01, "Split: C4 (untere 29 Tasten) Poli");
  }

  std::printf("Spielhilfen:\n");
  {
    // portamento: semitone steps (M110 scanning), 12 steps take a while
    load(s, 22);                                   // 23 Portamento (Piccolo)
    std::vector<float> l(64), r(64);
    s.noteOn(60, 0.8f);
    for (int i = 0; i < 100; i++) s.render(l.data(), r.data(), nullptr, nullptr, 64);
    double h0 = s.mono().currentHz();
    s.noteOn(72, 0.8f);
    for (int i = 0; i < 10; i++) s.render(l.data(), r.data(), nullptr, nullptr, 64);
    double hMid = s.mono().currentHz();
    for (int i = 0; i < 400; i++) s.render(l.data(), r.data(), nullptr, nullptr, 64);
    double h1 = s.mono().currentHz();
    char m[160];
    std::snprintf(m, sizeof m, "Portamento: %.0f Hz -> nach 13 ms %.0f Hz -> %.0f Hz", h0, hMid, h1);
    check(std::fabs(cents(h1, h0) - 1200) < 5 && cents(hMid, h0) > 50 && cents(hMid, h0) < 1150, m);
    // glide: second touch raises the mono pitch by a whole tone
    load(s, 21);                                   // 22 El. Guitar with Glide
    s.noteOn(69, 0.8f);
    for (int i = 0; i < 100; i++) s.render(l.data(), r.data(), nullptr, nullptr, 64);
    double g0 = s.mono().currentHz();
    s.setTouchMidi(1.0f);
    for (int i = 0; i < 300; i++) s.render(l.data(), r.data(), nullptr, nullptr, 64);
    double g1 = s.mono().currentHz();
    s.setTouchMidi(0.0f);
    std::snprintf(m, sizeof m, "Glide (Aftertouch): %+.0f Cent", cents(g1, g0));
    check(std::fabs(cents(g1, g0) - 200) < 10, m);
    // E.F.T. trigger: an external burst opens the envelope without a key
    load(s, 11);                                   // 12 Trumpet
    s.setParam(P_ENV_TRIGGER, fromIndex(P_ENV_TRIGGER, ET_TRIGGER));
    std::vector<float> in(64), ol(64), orr(64);
    double eftOut = 0, before = 0;
    for (int b = 0; b < 1500; b++) {
      for (int i = 0; i < 64; i++) { int t = b * 64 + i; in[i] = (t > 24000 && t < 48000) ? float(0.3 * std::sin(t * 0.06)) : 0.0f; }
      s.render(ol.data(), orr.data(), in.data(), in.data(), 64);
      for (float x : orr) { if (b < 370) before += x * x; else if (b < 750) eftOut += x * x; }
    }
    check(eftOut > 100 * (before + 1e-12) && eftOut > 1e-3, "E.F.T. Trigger: externes Signal startet die Hüllkurve");
    // sustain pedal (swell pedal switch): longer piano release
    auto pianoTail = [&](bool ped) {
      load(s, 0);
      s.setParam(P_SUSTAIN_PEDAL, ped ? 1.0 : 0.0);
      Take t = play(s, { { 0.05, 60, true }, { 0.3, 60, false } }, 1.2);
      return rms(t.l, 0.6, 1.1);
    };
    double t0 = pianoTail(false), t1 = pianoTail(true);
    std::snprintf(m, sizeof m, "Sustain-Pedal: Ausklang %.4f -> %.4f", t0, t1);
    check(t1 > 4 * t0, m);
    // vibrato (Violin registration: LFO continuous, OSCILL touch tab -> vibrato on the second touch)
    load(s, 17);
    s.noteOn(69, 0.8f);
    s.setTouchMidi(1.0f);
    double lo = 1e9, hi = 0;
    for (int i = 0; i < 1500; i++) {
      s.render(l.data(), r.data(), nullptr, nullptr, 64);
      if (i > 300) { lo = std::min(lo, s.mono().currentHz()); hi = std::max(hi, s.mono().currentHz()); }
    }
    s.setTouchMidi(0.0f);
    std::snprintf(m, sizeof m, "Vibrato (Violin, mit Aftertouch): +-%.0f Cent", cents(hi, lo) / 2);
    check(cents(hi, lo) > 10 && cents(hi, lo) < 120, m);
  }

  std::printf("Bauteilstreuung:\n");
  for (uint32_t ser : { 1979u, 4711u, 123u }) {
    s.setDevice(ser);
    monoOnly(s, MR_TRUMPET);
    Take t = play(s, { { 0.05, 69, true }, { 0.9, 69, false } }, 1.0);
    double hz = pitchNear(t.r, 0.25, 0.85, 440);
    load(s, 0);
    Take u = play(s, { { 0.05, 69, true }, { 0.5, 69, false } }, 0.6);
    double hp = pitchNear(u.l, 0.06, 0.45, 440);
    char m[160];
    std::snprintf(m, sizeof m, "Gerät %u: Mono %+.1f Cent, Poli %+.1f Cent", ser, cents(hz, 440), cents(hp, 440));
    check(std::fabs(cents(hz, 440)) < 6 && std::fabs(cents(hp, 440)) < 6, m);
  }
  s.setDevice(0);

  std::printf("Alle Registrierungen:\n");
  const std::vector<Ev> phrase = { { 0.05, 60, true }, { 0.05, 64, true }, { 0.05, 67, true }, { 0.8, 72, true }, { 1.4, 72, false },
                                   { 1.5, 76, true }, { 2.2, 76, false }, { 2.4, 60, false }, { 2.4, 64, false }, { 2.4, 67, false },
                                   { 2.6, 48, true }, { 3.2, 48, false } };
  for (int i = 0; i < presetCount(); i++) {
    load(s, i);
    s.reset();
    Take t = play(s, phrase, 4.2);
    double pk = std::max(peak(t.l), peak(t.r)), lv = std::max(rms(t.l, 0.1, 2.4), rms(t.r, 0.1, 2.4));
    char m[200];
    std::snprintf(m, sizeof m, "%2d %-44s Spitze %.2f, Pegel L %.3f R %.3f", i + 1, preset(i).name, pk, rms(t.l, 0.1, 2.4), rms(t.r, 0.1, 2.4));
    check(finite(t.l) && finite(t.r) && pk < 1.0 && lv > 0.01, m);
    if (!wavDir.empty()) {
      char fn[64];
      std::snprintf(fn, sizeof fn, "/reg%02d.wav", i + 1);
      writeWav(wavDir + fn, t);
    }
  }

  std::printf("Rechenzeit:\n");
  {
    load(s, 7);   // Strings and Piano
    s.setParam(P_MONO_REG, fromIndex(P_MONO_REG, MR_TRUMPET));
    std::vector<float> l(512), r(512);
    for (int n = 48; n < 72; n += 2) s.noteOn(n, 0.8f);
    auto t0 = std::chrono::steady_clock::now();
    int blocks = int(10 * FS / 512);
    for (int b = 0; b < blocks; b++) s.render(l.data(), r.data(), nullptr, nullptr, 512);
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    char m[120];
    std::snprintf(m, sizeof m, "10 s mit 12 Tasten (Strings, Piano, Mono): %.2f s Rechenzeit = %.1f %% CPU", sec, sec * 10);
    check(sec < 2.5, m);
  }
  std::printf(failures ? "FEHLER: %d Prüfungen\n" : "alle Prüfungen ok\n", failures);
  return failures ? 1 : 0;
}

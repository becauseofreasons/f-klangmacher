// End-to-end test of the built plugin: loads FKlangmacher.vst3 like a DAW does, selects registrations,
// plays notes, checks pitch, channel routing (Poli left, Mono right), feedback to the host and the state.
//   smk_hosttest <FKlangmacher.vst3> [out.wav]
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static const double FS = 48000;
static const int BLOCK = 512;
static int failures = 0;
static void check(bool ok, const char* what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) failures++; }

// strongest frequency within +-range cents around f0 (Goertzel, 1 cent steps)
static double pitchNear(const std::vector<float>& x, double t0, double t1, double f0, double range = 150) {
  size_t s = size_t(t0 * FS), e = size_t(t1 * FS);
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
  double q = 0; size_t s = size_t(t0 * FS), e = size_t(t1 * FS);
  for (size_t i = s; i < e; i++) q += double(x[i]) * x[i];
  return std::sqrt(q / double(e - s));
}
static double cents(double f, double ref) { return 1200 * std::log2(f / ref); }

static void writeWav(const char* path, const std::vector<float>& l, const std::vector<float>& r) {
  std::ofstream f(path, std::ios::binary);
  auto u32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
  auto u16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
  uint32_t n = uint32_t(l.size());
  f.write("RIFF", 4); u32(36 + n * 4); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2);
  u32(uint32_t(FS)); u32(uint32_t(FS * 4)); u16(4); u16(16); f.write("data", 4); u32(n * 4);
  for (uint32_t i = 0; i < n; i++)
    for (float v : { l[i], r[i] }) { int s = int(std::lround(std::max(-1.0f, std::min(1.0f, v)) * 32767)); u16(uint16_t(int16_t(s))); }
}

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "smk_hosttest <FKlangmacher.vst3> [out.wav]\n"); return 2; }
  std::string err;
  auto module = VST3::Hosting::Module::create(argv[1], err);
  if (!module) { std::fprintf(stderr, "Modul nicht geladen: %s\n", err.c_str()); return 1; }
  HostApplication host;
  PluginContextFactory::instance().setPluginContext(&host);
  auto factory = module->getFactory();
  VST3::Hosting::ClassInfo info;
  bool found = false;
  for (auto& ci : factory.classInfos()) if (ci.category() == kVstAudioEffectClass) { info = ci; found = true; break; }
  if (!found) { std::fprintf(stderr, "keine Audio-Klasse\n"); return 1; }
  std::printf("%s (%s, %s) %s\n", info.name().c_str(), info.vendor().c_str(), info.subCategoriesString().c_str(), info.version().c_str());

  auto provider = owned(new PlugProvider(factory, info, true));
  if (!provider->initialize()) { std::fprintf(stderr, "PlugProvider fehlgeschlagen\n"); return 1; }
  IPtr<IComponent> comp = provider->getComponentPtr();
  IPtr<IEditController> ctrl = provider->getControllerPtr();
  FUnknownPtr<IAudioProcessor> proc(comp);
  check(comp && ctrl && proc, "Komponente, Controller und Prozessor geladen");
  {
    MemoryStream st;
    comp->getState(&st);
    st.seek(0, IBStream::kIBSeekSet, nullptr);
    ctrl->setComponentState(&st);
  }
  int32 nParams = ctrl->getParameterCount();
  std::printf("  %d Parameter\n", nParams);
  check(nParams == 44 + 1, "44 Bedienelemente/Einstellungen + Programm");
  FUnknownPtr<IMidiMapping> mm(ctrl);
  ParamID mid = 0;
  check(mm && mm->getMidiControllerAssignment(0, 0, kAfterTouch, mid) == kResultTrue && mid == 39, "Aftertouch -> Second Touch");
  check(mm && mm->getMidiControllerAssignment(0, 0, kCtrlExpression, mid) == kResultTrue && mid == 38, "CC 11 -> Schwellerpedal");

  ProcessSetup setup{ kRealtime, kSample32, BLOCK, FS };
  check(proc->setupProcessing(setup) == kResultOk, "setupProcessing");
  comp->activateBus(kAudio, kOutput, 0, true);
  comp->activateBus(kEvent, kInput, 0, true);
  comp->setActive(true);
  proc->setProcessing(true);

  HostProcessData data;
  data.prepare(*comp, BLOCK, kSample32);
  EventList events;
  ParameterChanges inCh(64), outCh(64);
  data.inputEvents = &events;
  data.inputParameterChanges = &inCh;
  data.outputParameterChanges = &outCh;
  data.numSamples = BLOCK;

  const double P = 1.0 / 39;                       // program list: index / 39
  std::vector<float> outL, outR;
  std::map<ParamID, std::vector<std::pair<double, double>>> feedback;
  struct Ev { double t; int kind; int a; double v; };      // kind 0 note on, 1 note off, 2 param
  std::vector<Ev> plan = {
    { 0.50, 2, 100, 11 * P },                                 // 12 Trumpet
    { 1.00, 0, 69, 0.8 }, { 2.00, 1, 69, 0 },
    { 2.50, 2, 100, 0 * P },                                  // 1 Piano
    { 3.00, 0, 60, 0.8 }, { 3.60, 1, 60, 0 },
    { 4.00, 2, 100, 5 * P },                                  // 6 Strings 1
    { 4.50, 0, 60, 0.8 }, { 4.50, 0, 64, 0.8 }, { 4.50, 0, 67, 0.8 },
    { 6.00, 1, 60, 0 }, { 6.00, 1, 64, 0 }, { 6.00, 1, 67, 0 },
  };
  size_t pi = 0;
  const double end = 7.5;
  for (int64_t pos = 0; pos < int64_t(end * FS); pos += BLOCK) {
    events.clear();
    inCh.clearQueue();
    outCh.clearQueue();
    double t0 = pos / FS, t1 = (pos + BLOCK) / FS;
    while (pi < plan.size() && plan[pi].t < t1) {
      const Ev& ev = plan[pi++];
      int32 off = int32((ev.t - t0) * FS);
      if (ev.kind <= 1) {
        Event e{};
        e.busIndex = 0; e.sampleOffset = off; e.flags = Event::kIsLive;
        if (ev.kind == 0) { e.type = Event::kNoteOnEvent; e.noteOn.pitch = int16(ev.a); e.noteOn.velocity = float(ev.v); e.noteOn.noteId = -1; }
        else { e.type = Event::kNoteOffEvent; e.noteOff.pitch = int16(ev.a); e.noteOff.velocity = 0.5f; e.noteOff.noteId = -1; }
        events.addEvent(e);
      } else {
        int32 idx;
        if (IParamValueQueue* q = inCh.addParameterData(ParamID(ev.a), idx)) q->addPoint(off, ev.v, idx);
        ctrl->setParamNormalized(ParamID(ev.a), ev.v);
      }
    }
    if (proc->process(data) != kResultOk) { check(false, "process"); break; }
    float* L = data.outputs[0].channelBuffers32[0];
    float* R = data.outputs[0].channelBuffers32[1];
    for (int i = 0; i < BLOCK; i++) { outL.push_back(L[i]); outR.push_back(R[i]); }
    for (int32 i = 0; i < outCh.getParameterCount(); i++) {
      IParamValueQueue* q = outCh.getParameterData(i);
      int32 o; ParamValue v;
      if (q && q->getPoint(0, o, v) == kResultTrue) feedback[q->getParameterId()].push_back({ t1, v });
    }
  }
  proc->setProcessing(false);

  std::printf("Klang:\n");
  double silent = rms(outL, 0.1, 0.9) + rms(outR, 0.1, 0.9);
  double trR = rms(outR, 1.2, 1.9), trL = rms(outL, 1.2, 1.9);
  std::printf("  still %.5f, Trumpet rechts %.4f links %.4f\n", silent, trR, trL);
  check(trR > 0.02 && trR > 20 * silent, "Trumpet hörbar (Mono, rechter Kanal)");
  check(trL < trR * 0.05, "Mono-Teil nicht auf dem linken Kanal");
  double f1 = pitchNear(outR, 1.3, 1.9, 440.0);
  std::printf("  Trumpet A4: %.2f Hz = %+.1f Cent\n", f1, cents(f1, 440.0));
  check(std::fabs(cents(f1, 440.0)) < 12, "Tonhöhe A4 (vorgestimmt, wenige Cent)");
  double piL = rms(outL, 3.02, 3.3), piR = rms(outR, 3.02, 3.3);
  std::printf("  Piano links %.4f rechts %.4f\n", piL, piR);
  check(piL > 0.02 && piR < piL * 0.05, "Piano auf dem linken Kanal (Poli)");
  double f2 = pitchNear(outL, 3.02, 3.4, 261.626);
  std::printf("  Piano C4: %.2f Hz = %+.1f Cent\n", f2, cents(f2, 261.626));
  check(std::fabs(cents(f2, 261.626)) < 12, "Tonhöhe C4 Poli");
  double st1 = rms(outL, 5.0, 5.9), st2 = rms(outL, 6.8, 7.4);
  std::printf("  Strings %.4f, nach Loslassen (Sustain) %.4f\n", st1, st2);
  check(st1 > 0.02, "Strings-Akkord hörbar");
  check(st2 < st1, "Strings klingen aus");

  std::printf("Rückmeldung an die DAW:\n");
  auto last = [&](ParamID id) { return feedback[id].empty() ? -1.0 : feedback[id].back().second; };
  check(!feedback[27].empty(), "Programmwahl meldet die Register");
  check(std::fabs(last(33) - 1.0) < 1e-9 && std::fabs(last(32) - 1.0) < 1e-9, "zuletzt Strings 8' und 4' an");
  check(feedback.count(100) == 0, "Programmwechsel vom Host wird nicht zurückgemeldet");

  std::printf("Zustand:\n");
  MemoryStream st;
  check(comp->getState(&st) == kResultOk, "getState");
  int64 size = 0; st.tell(&size);
  check(size == 12 + 44 * 12 + 8, "Größe des Zustands");
  st.seek(0, IBStream::kIBSeekSet, nullptr);
  check(comp->setState(&st) == kResultOk, "setState");
  st.seek(0, IBStream::kIBSeekSet, nullptr);
  check(ctrl->setComponentState(&st) == kResultOk, "Controller liest den Zustand");
  check(std::fabs(ctrl->getParamNormalized(32) - 1.0) < 1e-9, "Controller zeigt Strings 8' an");

  comp->setActive(false);
  data.unprepare();
  provider = nullptr;
  PluginContextFactory::instance().setPluginContext(nullptr);
  if (argc > 2) { writeWav(argv[2], outL, outR); std::printf("geschrieben: %s\n", argv[2]); }
  std::printf(failures ? "FEHLER: %d Prüfungen\n" : "alle Prüfungen ok\n", failures);
  return failures ? 1 : 0;
}

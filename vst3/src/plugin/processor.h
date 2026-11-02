#pragma once
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "synth.h"
#include "state.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace smk {

class Processor : public Steinberg::Vst::AudioEffect {
public:
  Processor();
  ~Processor() override;
  static Steinberg::FUnknown* createInstance(void*) { return (Steinberg::Vst::IAudioProcessor*)new Processor; }

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                   Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
  std::unique_ptr<Synth> synth;
  std::vector<float> zeros;
  double sampleRate = 48000;
  PluginState cur;                 // audio thread copy (values, program, serial)
  float polyPressure[128] = {};
  float channelPressure = 0;
  // state exchange with the host thread (the audio thread only uses try_lock)
  std::mutex mtx;
  PluginState shared;              // latest copy for getState
  PluginState pending;
  bool hasPending = false;
  std::atomic<uint32_t> pendingSerial{ 0 };

  void applyParam(int id, double v);
  void applyProgram(int index, Steinberg::Vst::IParameterChanges* out);
  void handleParams(Steinberg::Vst::IParameterChanges* in, Steinberg::Vst::IParameterChanges* out);
  void handleEvent(const Steinberg::Vst::Event& e);
  void updateTouch();
};

} // namespace smk

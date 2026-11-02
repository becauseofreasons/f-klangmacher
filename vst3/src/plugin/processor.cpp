#include "processor.h"
#include "ids.h"
#include "presets.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include <algorithm>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace smk {

Processor::Processor() {
  setControllerClass(ControllerUID);
  synth = std::make_unique<Synth>();
  synth->prepare(48000, 4096);
  for (int i = 0; i < P_COUNT; i++) synth->setParam(i, cur.values[i]);
  synth->setDevice(cur.serial);
  synth->reset();
  shared = cur;
  zeros.assign(4096, 0.0f);
}

Processor::~Processor() = default;

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
  tresult r = AudioEffect::initialize(context);
  if (r != kResultOk) return r;
  addAudioOutput(STR16("Out"), SpeakerArr::kStereo);
  // E.F.T. input (envelope follower trigger): side chain, off until the host connects it
  addAudioInput(STR16("E.F.T. In"), SpeakerArr::kStereo, BusTypes::kAux, 0);
  addEventInput(STR16("MIDI In"), 16);
  return kResultOk;
}

// messages from the controller (host thread)
tresult PLUGIN_API Processor::notify(IMessage* message) {
  if (!message) return kInvalidArgument;
  if (FIDStringsEqual(message->getMessageID(), "device")) {
    int64 serial = 0;
    if (message->getAttributes()->getInt("serial", serial) == kResultOk) pendingSerial = uint32_t(serial) | 0x80000000u;
    return kResultOk;
  }
  return AudioEffect::notify(message);
}

tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                 SpeakerArrangement* outputs, int32 numOuts) {
  if (numOuts != 1) return kResultFalse;
  if (outputs[0] != SpeakerArr::kStereo && outputs[0] != SpeakerArr::kMono) return kResultFalse;
  if (numIns > 1) return kResultFalse;
  if (numIns == 1 && inputs[0] != SpeakerArr::kStereo && inputs[0] != SpeakerArr::kMono) return kResultFalse;
  return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize) {
  return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
  std::lock_guard<std::mutex> lock(mtx);        // inactive: the audio thread does not run
  sampleRate = setup.sampleRate > 0 ? setup.sampleRate : 48000;
  int32 maxBlock = std::max<int32>(setup.maxSamplesPerBlock, 32);
  synth->prepare(sampleRate, maxBlock);
  zeros.assign(size_t(maxBlock), 0.0f);
  return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Processor::setActive(TBool state) {
  if (state) {
    std::lock_guard<std::mutex> lock(mtx);
    synth->allNotesOff();
  }
  return AudioEffect::setActive(state);
}

void Processor::applyParam(int id, double v) {
  if (id < 0 || id >= P_COUNT) return;
  cur.values[id] = v;
  synth->setParam(id, v);
}

// program selected (DAW program list or editor): all panel values of the registration
void Processor::applyProgram(int index, IParameterChanges* out) {
  if (index < 0 || index >= presetCount()) return;
  cur.program = index;
  const Preset& p = preset(index);
  for (int i = 0; i < P_COUNT; i++) {
    if (!p.has(i)) continue;
    applyParam(i, p.values[i]);
    if (out) {
      int32 idx = 0;
      if (IParamValueQueue* q = out->addParameterData(ParamID(i), idx)) q->addPoint(0, p.values[i], idx);
    }
  }
}

void Processor::handleParams(IParameterChanges* in, IParameterChanges* out) {
  if (!in) return;
  int32 n = in->getParameterCount();
  for (int32 i = 0; i < n; i++) {
    IParamValueQueue* q = in->getParameterData(i);
    if (!q || q->getPointCount() <= 0) continue;
    int32 offset;
    ParamValue v;
    if (q->getPoint(q->getPointCount() - 1, offset, v) != kResultTrue) continue;
    ParamID id = q->getParameterId();
    if (id == kParamProgram) {
      int idx = std::min(presetCount() - 1, int(v * presetCount()));
      if (idx != cur.program) applyProgram(idx, out);      // the same value again (project load) changes nothing
    } else if (id < ParamID(P_COUNT)) {
      applyParam(int(id), v);
    }
  }
}

void Processor::updateTouch() {
  float t = channelPressure;
  for (float p : polyPressure) t = std::max(t, p);
  synth->setTouchMidi(t);
}

void Processor::handleEvent(const Event& e) {
  switch (e.type) {
    case Event::kNoteOnEvent:
      if (e.noteOn.velocity <= 0) synth->noteOff(e.noteOn.pitch);
      else synth->noteOn(e.noteOn.pitch, e.noteOn.velocity);
      break;
    case Event::kNoteOffEvent:
      synth->noteOff(e.noteOff.pitch);
      if (e.noteOff.pitch >= 0 && e.noteOff.pitch < 128 && polyPressure[e.noteOff.pitch] > 0) {
        polyPressure[e.noteOff.pitch] = 0;
        updateTouch();
      }
      break;
    case Event::kPolyPressureEvent:
      if (e.polyPressure.pitch >= 0 && e.polyPressure.pitch < 128) {
        polyPressure[e.polyPressure.pitch] = e.polyPressure.pressure;
        updateTouch();
      }
      break;
    default: break;
  }
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
  // state from the host thread (project loaded)
  if (mtx.try_lock()) {
    if (hasPending) {
      for (int i = 0; i < P_COUNT; i++) applyParam(i, pending.values[i]);
      cur.program = pending.program;
      if (pending.serial != cur.serial) { cur.serial = pending.serial; synth->setDevice(cur.serial); }
      hasPending = false;
    }
    shared = cur;
    mtx.unlock();
  }
  uint32_t ps = pendingSerial.exchange(0);
  if (ps) { cur.serial = ps & 0x7FFFFFFFu; synth->setDevice(cur.serial); }

  handleParams(data.inputParameterChanges, data.outputParameterChanges);
  // channel pressure arrives as parameter P_TOUCH (IMidiMapping) - also used for the second touch
  channelPressure = float(cur.values[P_TOUCH]);
  updateTouch();

  int32 n = data.numSamples;
  if (n <= 0) return kResultOk;
  float* outL = nullptr;
  float* outR = nullptr;
  if (data.numOutputs > 0 && data.outputs[0].channelBuffers32) {
    AudioBusBuffers& bus = data.outputs[0];
    outL = bus.channelBuffers32[0];
    outR = bus.numChannels > 1 ? bus.channelBuffers32[1] : nullptr;
  }
  if (!outL) return kResultOk;
  const float* inL = nullptr;
  const float* inR = nullptr;
  if (data.numInputs > 0 && data.inputs[0].channelBuffers32 && data.inputs[0].numChannels > 0) {
    inL = data.inputs[0].channelBuffers32[0];
    inR = data.inputs[0].numChannels > 1 ? data.inputs[0].channelBuffers32[1] : inL;
  }

  int32 pos = 0;
  auto renderTo = [&](int32 end) {
    while (pos < end) {
      int32 len = std::min<int32>(end - pos, int32(zeros.size()));
      synth->render(outL + pos, outR ? outR + pos : nullptr, inL ? inL + pos : nullptr, inR ? inR + pos : nullptr, len);
      pos += len;
    }
  };
  if (IEventList* ev = data.inputEvents) {
    int32 count = ev->getEventCount();
    for (int32 i = 0; i < count; i++) {
      Event e;
      if (ev->getEvent(i, e) != kResultOk) continue;
      renderTo(std::max<int32>(pos, std::min<int32>(e.sampleOffset, n)));
      handleEvent(e);
    }
  }
  renderTo(n);
  AudioBusBuffers& bus = data.outputs[0];
  for (int32 c = 2; c < bus.numChannels; c++)
    if (bus.channelBuffers32[c]) std::memcpy(bus.channelBuffers32[c], outL, size_t(n) * sizeof(float));
  bus.silenceFlags = 0;
  return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* stream) {
  PluginState st;
  if (!st.read(stream)) return kResultFalse;
  std::lock_guard<std::mutex> lock(mtx);
  pending = st;
  hasPending = true;
  shared = st;
  return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* stream) {
  PluginState st;
  {
    std::lock_guard<std::mutex> lock(mtx);
    st = hasPending ? pending : shared;
  }
  return st.write(stream) ? kResultOk : kResultFalse;
}

} // namespace smk

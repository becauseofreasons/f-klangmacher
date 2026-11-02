#include "controller.h"
#include "ids.h"
#include "state.h"
#include "params.h"
#include "presets.h"
#include "editor.h"
#include "uisettings.h"
#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/base/ustring.h"
#include "base/source/fstring.h"
#include <algorithm>
#include <cmath>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace smk {

namespace {
void toU16(const std::string& s, String128 out) { UString(out, 128).fromAscii(s.c_str()); }

// slider shown on the panel scale 0..10
class PanelParameter : public RangeParameter {
public:
  PanelParameter(const TChar* title, ParamID id, double def, bool centred)
  : RangeParameter(title, id, nullptr, 0, 1, def, 0, ParameterInfo::kCanAutomate), centred(centred) {}
  void toString(ParamValue norm, String128 string) const SMTG_OVERRIDE {
    char b[32];
    if (centred) snprintf(b, sizeof b, "%+.0f %%", (norm - 0.5) * 200);
    else snprintf(b, sizeof b, "%.1f", norm * 10);
    UString(string, 128).fromAscii(b);
  }
  bool fromString(const TChar* string, ParamValue& norm) const SMTG_OVERRIDE {
    String s(string);
    double v = 0;
    if (!s.scanFloat(v)) return false;
    norm = centred ? v / 200.0 + 0.5 : v / 10.0;
    norm = std::max(0.0, std::min(1.0, norm));
    return true;
  }
private:
  bool centred;
};
} // namespace

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
  tresult r = EditControllerEx1::initialize(context);
  if (r != kResultOk) return r;

  addUnit(new Unit(STR16("F. Klangmacher"), kRootUnitId, kNoParentUnitId, kParamProgram));
  auto* list = new ProgramList(STR16("Registrations"), kParamProgram, kRootUnitId);
  for (int i = 0; i < presetCount(); i++) {
    String128 name;
    toU16(std::to_string(i + 1) + " " + preset(i).name, name);
    list->addProgram(name);
  }
  addProgramList(list);
  Parameter* pp = list->getParameter();
  pp->getInfo().flags |= ParameterInfo::kIsProgramChange;
  parameters.addParameter(pp);

  for (int i = 0; i < P_COUNT; i++) {
    const ParamDef& d = paramDef(i);
    String128 title, shortTitle;
    toU16(d.name, title);
    toU16(d.shortName, shortTitle);
    Parameter* p = nullptr;
    if (d.kind == Kind::List || d.kind == Kind::Toggle) {
      auto* sp = new StringListParameter(title, ParamID(i), nullptr, ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
      for (int v = 0; v <= d.steps; v++) {
        String128 u;
        toU16(d.labels[v], u);
        sp->appendString(u);
      }
      sp->setNormalized(d.def);
      sp->getInfo().defaultNormalizedValue = d.def;
      p = sp;
    } else {
      p = new PanelParameter(title, ParamID(i), d.def, i == P_MONO_TUNING || i == P_POLI_PITCH);
    }
    UString(p->getInfo().shortTitle, 128).assign(shortTitle);
    parameters.addParameter(p);
  }
  applyProgramValues(0);              // same start as the processor: registration 1
  zoom = loadDefaultZoom();
  return kResultOk;
}

namespace {
constexpr uint32 kUiStateMagic = 0x55524B53;     // "SKRU"
}

tresult PLUGIN_API Controller::setState(IBStream* stream) {
  IBStreamer s(stream, kLittleEndian);
  uint32 magic = 0;
  int32 percent = 0;
  if (!s.readInt32u(magic) || magic != kUiStateMagic || !s.readInt32(percent)) return kResultFalse;
  if (percent > 0) zoom = snapZoom(percent / 100.0);
  return kResultOk;
}

tresult PLUGIN_API Controller::getState(IBStream* stream) {
  IBStreamer s(stream, kLittleEndian);
  return s.writeInt32u(kUiStateMagic) && s.writeInt32(int32(std::lround(zoom * 100))) ? kResultOk : kResultFalse;
}

void Controller::setUiZoom(double z) {
  zoom = snapZoom(z);
  saveDefaultZoom(zoom);
}

// project loaded: parameter values from the processor state
tresult PLUGIN_API Controller::setComponentState(IBStream* stream) {
  PluginState st;
  if (!st.read(stream)) return kResultFalse;
  for (int i = 0; i < P_COUNT; i++) EditControllerEx1::setParamNormalized(ParamID(i), st.values[i]);
  if (presetCount() > 1)
    EditControllerEx1::setParamNormalized(kParamProgram, double(std::max(0, st.program)) / (presetCount() - 1));
  serial = st.serial;
  return kResultOk;
}

int Controller::currentProgram() const {
  int n = presetCount();
  return std::min(n - 1, int(const_cast<Controller*>(this)->getParamNormalized(kParamProgram) * n));
}

void Controller::applyProgramValues(int index) {
  if (index < 0 || index >= presetCount()) return;
  const Preset& p = preset(index);
  for (int i = 0; i < P_COUNT; i++)
    if (p.has(i)) EditControllerEx1::setParamNormalized(ParamID(i), p.values[i]);
}

// program chosen in the DAW: show the registration's values (the processor applies the same table)
tresult PLUGIN_API Controller::setParamNormalized(ParamID tag, ParamValue value) {
  if (tag == kParamProgram && !applyingProgram) {
    int before = currentProgram();
    tresult r = EditControllerEx1::setParamNormalized(tag, value);
    int now = currentProgram();
    if (now != before) applyProgramValues(now);
    return r;
  }
  return EditControllerEx1::setParamNormalized(tag, value);
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
  if (FIDStringsEqual(name, ViewType::kEditor)) return new Editor(this);
  return nullptr;
}

void Controller::beginGesture(int id) { beginEdit(ParamID(id)); }
void Controller::performGesture(int id, double norm) {
  EditControllerEx1::setParamNormalized(ParamID(id), norm);
  performEdit(ParamID(id), norm);
}
void Controller::endGesture(int id) { endEdit(ParamID(id)); }
void Controller::edit(int id, double norm) {
  beginGesture(id);
  performGesture(id, norm);
  endGesture(id);
}

void Controller::selectProgram(int index) {
  int n = presetCount();
  if (index < 0 || index >= n) return;
  double v = n > 1 ? double(index) / (n - 1) : 0;
  applyingProgram = true;
  beginEdit(kParamProgram);
  EditControllerEx1::setParamNormalized(kParamProgram, v);
  performEdit(kParamProgram, v);
  endEdit(kParamProgram);
  applyingProgram = false;
  applyProgramValues(index);
}

void Controller::newDevice(uint32_t s) {
  serial = s;
  if (auto msg = owned(allocateMessage())) {
    msg->setMessageID("device");
    msg->getAttributes()->setInt("serial", int64(s));
    sendMessage(msg);
  }
}

tresult PLUGIN_API Controller::getMidiControllerAssignment(int32 busIndex, int16 channel, CtrlNumber cc, ParamID& id) {
  (void)channel;
  if (busIndex != 0) return kResultFalse;
  switch (cc) {
    case kCtrlExpression: id = P_EXPRESSION; return kResultTrue;
    case kAfterTouch: id = P_TOUCH; return kResultTrue;
    case kCtrlModWheel: id = P_TOUCH; return kResultTrue;
    case kCtrlVolume: id = P_OUT_LEVEL; return kResultTrue;
    case kCtrlSustainOnOff: id = P_SUSTAIN_PEDAL; return kResultTrue;
    default: return kResultFalse;
  }
}

} // namespace smk

#pragma once
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

namespace smk {

class Controller : public Steinberg::Vst::EditControllerEx1, public Steinberg::Vst::IMidiMapping {
public:
  static Steinberg::FUnknown* createInstance(void*) { return (Steinberg::Vst::IEditController*)new Controller; }

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;
  // controller state: editor size of this project
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
  Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
  // IMidiMapping: CC 11 = swell pedal, aftertouch and CC 1 = second touch, CC 7 = output level, CC 64 = sustain
  Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                            Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                            Steinberg::Vst::ParamID& id) SMTG_OVERRIDE;

  // for the editor
  void edit(int id, double norm);                 // begin + perform + end
  void beginGesture(int id);
  void performGesture(int id, double norm);
  void endGesture(int id);
  void selectProgram(int index);
  void newDevice(uint32_t serial);
  uint32_t deviceSerial() const { return serial; }
  int currentProgram() const;
  double uiZoom() const { return zoom; }
  void setUiZoom(double z);                       // also the default for new instances

  OBJ_METHODS(Controller, EditControllerEx1)
  DEFINE_INTERFACES
    DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
  END_DEFINE_INTERFACES(EditControllerEx1)
  REFCOUNT_METHODS(EditControllerEx1)

private:
  uint32_t serial = 1979;
  double zoom = 1.0;
  bool applyingProgram = false;
  void applyProgramValues(int index);
};

} // namespace smk

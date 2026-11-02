#pragma once
#include "public.sdk/source/vst/vstguieditor.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "vstgui/lib/cvstguitimer.h"
#include "../gui/panel.h"

namespace smk {

class Controller;

// Size = panel size x user zoom (75 .. 200 %) x display scale of the host (Windows HiDPI).
class Editor : public Steinberg::Vst::VSTGUIEditor, public Steinberg::IPlugViewContentScaleSupport, public PanelHost {
public:
  explicit Editor(Controller* c);
  bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
  void PLUGIN_API close() SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) SMTG_OVERRIDE;
  bool beforeSizeChange(const VSTGUI::CRect& newSize, const VSTGUI::CRect& oldSize) SMTG_OVERRIDE;

  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) SMTG_OVERRIDE;
  DELEGATE_REFCOUNT(Steinberg::Vst::VSTGUIEditor)

  // PanelHost
  double value(int id) override;
  void beginEdit(int id) override;
  void performEdit(int id, double norm) override;
  void endEdit(int id) override;
  int program() override;
  void selectProgram(int index) override;
  uint32_t serial() override;
  void newDevice(uint32_t serial) override;
  double zoom() override { return userZoom; }
  void setZoom(double z) override;

private:
  Controller* ctl;
  PanelView* panel = nullptr;
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer;
  double userZoom = 1.0, hostScale = 1.0;
  bool quietResize = false;          // frame size changes that need no request to the host
  double scale() const { return userZoom * hostScale; }
  bool applyScale();
};

} // namespace smk

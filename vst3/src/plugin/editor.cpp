#include "editor.h"
#include "controller.h"
#include "linux_runloop.h"
#include "ids.h"
#include "uisettings.h"
#include "vstgui/lib/cframe.h"
#include <cmath>
#include <cstring>

using namespace VSTGUI;
using namespace Steinberg;

namespace smk {

Editor::Editor(Controller* c) : VSTGUIEditor(c), ctl(c) {
  userZoom = ctl->uiZoom();
  ViewRect r(0, 0, int32(std::lround(PanelView::W * scale())), int32(std::lround(PanelView::H * scale())));
  setRect(r);
}

tresult PLUGIN_API Editor::queryInterface(const TUID iid, void** obj) {
  QUERY_INTERFACE(iid, obj, IPlugViewContentScaleSupport::iid, IPlugViewContentScaleSupport)
  return VSTGUIEditor::queryInterface(iid, obj);
}

// zoom of the frame; while open, the frame asks the host for the new window size (beforeSizeChange)
bool Editor::applyScale() {
  if (!frame) return true;
  return frame->setZoom(scale());
}

bool Editor::beforeSizeChange(const CRect& newSize, const CRect&) {
  if (quietResize || !plugFrame) return true;
  ViewRect vr(0, 0, int32(std::lround(newSize.getWidth())), int32(std::lround(newSize.getHeight())));
  quietResize = true;                // the host answers with onSize -> frame->setSize
  bool ok = plugFrame->resizeView(this, &vr) == kResultTrue;
  quietResize = false;
  return ok;
}

void Editor::setZoom(double z) {
  double old = userZoom;
  userZoom = snapZoom(z);
  if (userZoom == old) return;
  if (!applyScale()) { userZoom = old; return; }      // host refused the new size
  ctl->setUiZoom(userZoom);
}

tresult PLUGIN_API Editor::setContentScaleFactor(ScaleFactor factor) {
  if (!(factor > 0.25 && factor < 8)) return kResultFalse;
  double old = hostScale;
  hostScale = factor;
  if (!applyScale()) { hostScale = old; return kResultFalse; }
  return kResultOk;
}

bool PLUGIN_API Editor::open(void* parent, const PlatformType& platformType) {
  if (frame) return false;
  frame = new CFrame(CRect(0, 0, PanelView::W, PanelView::H), this);
  frame->setTransparency(false);
  panel = new PanelView(*this);
  frame->addView(panel);
  quietResize = true;                 // attached() reports the size to the host
  frame->setZoom(scale());
  quietResize = false;
  bool ok;
#if SMTG_OS_LINUX
  VSTGUI::X11::FrameConfig config;
  config.runLoop = makeOwned<HostRunLoop>(plugFrame);
  ok = frame->open(parent, platformType, &config);
#else
  ok = frame->open(parent, platformType);
#endif
  if (!ok) { frame->forget(); frame = nullptr; panel = nullptr; return false; }
  timer = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { if (panel) panel->refresh(); }, 40, true);
  return true;
}

void PLUGIN_API Editor::close() {
  if (timer) { timer->stop(); timer = nullptr; }
  if (frame) {
    frame->close();          // releases the views
    frame = nullptr;
  }
  panel = nullptr;
}

tresult PLUGIN_API Editor::isPlatformTypeSupported(FIDString type) {
#if SMTG_OS_LINUX
  if (strcmp(type, kPlatformTypeX11EmbedWindowID) != 0) return kInvalidArgument;     // X11 only (also under Xwayland)
#endif
  return VSTGUIEditor::isPlatformTypeSupported(type);
}

double Editor::value(int id) { return ctl->getParamNormalized(Vst::ParamID(id)); }
void Editor::beginEdit(int id) { ctl->beginGesture(id); }
void Editor::performEdit(int id, double norm) { ctl->performGesture(id, norm); }
void Editor::endEdit(int id) { ctl->endGesture(id); }
int Editor::program() { return ctl->currentProgram(); }
void Editor::selectProgram(int index) { ctl->selectProgram(index); }
uint32_t Editor::serial() { return ctl->deviceSerial(); }
void Editor::newDevice(uint32_t s) { ctl->newDevice(s); }

} // namespace smk

// Renders the editor without a window into PNG files (layout check).
//   smk_uishot <output folder> [scale]
#include "../src/gui/panel.h"
#include "params.h"
#include "presets.h"
#include "vstgui/lib/vstguiinit.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/platform/iplatformbitmap.h"
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
#include <fstream>
#include <string>

using namespace VSTGUI;
using namespace smk;

struct StubHost : PanelHost {
  double vals[P_COUNT];
  int prog = 0;
  uint32_t ser = 1979;
  StubHost() { defaultValues(vals); }
  double value(int id) override { return vals[id]; }
  void beginEdit(int) override {}
  void performEdit(int id, double v) override { vals[id] = v; }
  void endEdit(int) override {}
  int program() override { return prog; }
  void selectProgram(int i) override {
    prog = i;
    for (int k = 0; k < P_COUNT; k++) if (preset(i).has(k)) vals[k] = preset(i).values[k];
  }
  uint32_t serial() override { return ser; }
  void newDevice(uint32_t s) override { ser = s; }
  double z = 1.0;
  double zoom() override { return z; }
  void setZoom(double v) override { z = v; }
};

static double gScale = 1.0;
static void render(PanelView& v, const std::string& path) {
  // scale by a transform (a backing scale factor would only enlarge the canvas with Cairo)
  auto ctx = COffscreenContext::create(CPoint(PanelView::W * gScale, PanelView::H * gScale), 1.0);
  if (!ctx) { std::printf("kein Offscreen-Zeichnen auf diesem System (%s)\n", path.c_str()); return; }
  ctx->beginDraw();
  {
    CDrawContext::Transform t(*ctx, CGraphicsTransform().scale(gScale, gScale));
    v.draw(ctx);
  }
  ctx->endDraw();
  auto png = getPlatformFactory().createBitmapMemoryPNGRepresentation(ctx->getBitmap()->getPlatformBitmap());
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
  std::printf("geschrieben: %s (%zu Byte)\n", path.c_str(), png.size());
}

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : ".";
  if (argc > 2) gScale = std::atof(argv[2]);
#ifdef _WIN32
  VSTGUI::init(GetModuleHandle(nullptr));
#else
  VSTGUI::init(nullptr);
#endif
  {
    StubHost host;
    auto* view = new PanelView(host);
    host.selectProgram(30);            // Free Tab 3: many controls in use
    view->refresh();
    render(*view, out + "/ui_free3.png");
    host.selectProgram(26);            // Split Keyboard 2
    view->refresh();
    render(*view, out + "/ui_split2.png");
    view->forget();
  }
  VSTGUI::exit();
  return 0;
}

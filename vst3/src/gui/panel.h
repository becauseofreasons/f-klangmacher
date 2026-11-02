// The editor: the front panel of the F. Klangmacher without the keyboard, drawn in code.
// It talks to the outside only through PanelHost, so it also runs without the plugin
// (test/uishot.cpp renders it into a PNG).
#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/ccolor.h"
#include "params.h"
#include <cstdint>
#include <string>
#include <vector>

namespace smk {

struct PanelHost {
  virtual ~PanelHost() = default;
  virtual double value(int id) = 0;                 // normalized
  virtual void beginEdit(int id) = 0;
  virtual void performEdit(int id, double norm) = 0;
  virtual void endEdit(int id) = 0;
  virtual int program() = 0;
  virtual void selectProgram(int index) = 0;
  virtual uint32_t serial() = 0;
  virtual void newDevice(uint32_t serial) = 0;      // 0 = ideal parts
  virtual double zoom() = 0;                        // editor size: 0.75, 1, 1.5, 2
  virtual void setZoom(double z) = 0;
};

class PanelView : public VSTGUI::CView {
public:
  static constexpr double W = 1600, H = 348;
  explicit PanelView(PanelHost& host);
  void refresh();                                    // timer: redraw when values changed
  void draw(VSTGUI::CDrawContext* c) override;
  void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
  void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
  void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
  void onMouseCancelEvent(VSTGUI::MouseCancelEvent& e) override;
  void onMouseWheelEvent(VSTGUI::MouseWheelEvent& e) override;

  enum class T : uint8_t { Slider, Centre, Toggle, Switch3, Wave, Rotary, Knob, Tab, Info };
  struct Ctl {
    T type;
    int id;              // parameter (Tab: see tabKind)
    double x, y;         // centre (Slider: centre of the track top)
    int tabKind = 0;     // Tab: 0 = radio (id = list parameter, val = index), 1 = toggle, 2 = cancel mono, 3 = cancel poli
    int val = 0;
    VSTGUI::CColor cap;
    std::string text;    // tab caption ("\n" = two lines)
  };

private:
  PanelHost& host;
  std::vector<Ctl> ctls;
  double shown[P_COUNT] = {};
  int shownProgram = -1;
  uint32_t shownSerial = 0;
  int drag = -1;                 // index in ctls
  double dragStartY = 0, dragStartX = 0, dragStartV = 0;
  bool dragFine = false;

  void build();
  int hit(const VSTGUI::CPoint& p) const;
  VSTGUI::CRect area(const Ctl& c) const;
  double v(int id) const { return shown[id]; }
  void set(int id, double norm);                    // single edit (begin/perform/end)
  void pressTab(const Ctl& c);
  bool tabLit(const Ctl& c) const;
  void showMenu(const VSTGUI::CPoint& where);

  void drawBackground(VSTGUI::CDrawContext* c);
  void drawSlider(VSTGUI::CDrawContext* c, const Ctl& k);
  void drawToggle(VSTGUI::CDrawContext* c, const Ctl& k);
  void drawRotary(VSTGUI::CDrawContext* c, const Ctl& k);
  void drawKnob(VSTGUI::CDrawContext* c, const Ctl& k);
  void drawTab(VSTGUI::CDrawContext* c, const Ctl& k);
  void drawInfo(VSTGUI::CDrawContext* c, const Ctl& k);
};

} // namespace smk

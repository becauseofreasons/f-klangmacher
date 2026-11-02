#include "panel.h"
#include "theme.h"
#include "presets.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <random>

using namespace VSTGUI;

namespace smk {

namespace {
using theme::rgb;

// vertical layout
constexpr double PANEL_T = 18, PANEL_B = 226;           // cream control panel
constexpr double TILE_T = 23, TILE_B = 216;             // function group tiles
constexpr double HEAD_T = 26, HEAD_B = 39;              // coloured header of a tile
constexpr double LABEL_Y = 47;                          // control names under the header
constexpr double FOOT_Y = 205;                          // small print at the bottom of a tile
constexpr double TRACK_T = 58, TRACK_B = 190;           // slider travel (scale 10 .. 0)
constexpr double STRIP_B = 332;                         // tab strip bottom
constexpr double TAB_T = 240, TAB_H = 25, TAB_W = 26;
constexpr double CAPTION = 9.5;                         // tab captions (font size)
constexpr double LABEL = 9;                             // control names (font size)
constexpr double CHEEK = 34;
constexpr double CAP_W = 20, CAP_H = 12;

CColor mix(const CColor& a, const CColor& b, double t) {
  t = std::max(0.0, std::min(1.0, t));
  return CColor(uint8_t(a.red + (b.red - a.red) * t), uint8_t(a.green + (b.green - a.green) * t),
                uint8_t(a.blue + (b.blue - a.blue) * t), uint8_t(a.alpha + (b.alpha - a.alpha) * t));
}
double gMinLine = 0;                                     // at least one screen pixel (set per draw from the zoom)
void line(CDrawContext* c, double x0, double y0, double x1, double y1, const CColor& col, double w = 1) {
  c->setFrameColor(col);
  c->setLineWidth(std::max(w, gMinLine));
  c->drawLine(CPoint(x0, y0), CPoint(x1, y1));
}
void fillRect(CDrawContext* c, const CRect& r, const CColor& col) {
  c->setFillColor(col);
  c->drawRect(r, kDrawFilled);
}
void roundRect(CDrawContext* c, const CRect& r, double rad, const CColor* fill, const CColor* frame, double w = 1) {
  auto p = VSTGUI::owned(c->createGraphicsPath());
  if (!p) { if (fill) fillRect(c, r, *fill); return; }
  p->addRoundRect(r, rad);
  if (fill) { c->setFillColor(*fill); c->drawGraphicsPath(p, CDrawContext::kPathFilled); }
  if (frame) { c->setFrameColor(*frame); c->setLineWidth(std::max(w, gMinLine)); c->drawGraphicsPath(p, CDrawContext::kPathStroked); }
}
void fillRound(CDrawContext* c, const CRect& r, double rad, const CColor& col) { roundRect(c, r, rad, &col, nullptr); }
void vGradient(CDrawContext* c, const CRect& r, const CColor& top, const CColor& bottom, double rad = 0) {
  auto p = VSTGUI::owned(c->createGraphicsPath());
  auto g = VSTGUI::owned(CGradient::create(0, 1, top, bottom));
  if (!p || !g) { fillRect(c, r, mix(top, bottom, 0.5)); return; }
  if (rad > 0) p->addRoundRect(r, rad); else p->addRect(r);
  c->fillLinearGradient(p, *g, CPoint(r.left, r.top), CPoint(r.left, r.bottom));
}
void hGradient(CDrawContext* c, const CRect& r, const CColor& left, const CColor& right) {
  auto p = VSTGUI::owned(c->createGraphicsPath());
  auto g = VSTGUI::owned(CGradient::create(0, 1, left, right));
  if (!p || !g) { fillRect(c, r, mix(left, right, 0.5)); return; }
  p->addRect(r);
  c->fillLinearGradient(p, *g, CPoint(r.left, r.top), CPoint(r.right, r.top));
}
void disc(CDrawContext* c, double x, double y, double r, const CColor& col) {
  c->setFillColor(col);
  c->drawEllipse(CRect(x - r, y - r, x + r, y + r), kDrawFilled);
}
// printed text (condensed)
void text(CDrawContext* c, const std::string& s, double cx, double cy, double size, const CColor& col = theme::ink,
          bool bold = true, CHoriTxtAlign al = kCenterText, double squeeze = theme::kSqueeze) {
  CRect r(cx - 80, cy - size, cx + 80, cy + size);
  if (al == kLeftText) r = CRect(cx, cy - size, cx + 200, cy + size);
  if (al == kRightText) r = CRect(cx - 200, cy - size, cx, cy + size);
  theme::label(c, s, r, col, size, bold, al, false, squeeze);
}
// arc as a polyline (the Direct2D arc of Wine is unreliable); angles in degrees, 0 = right, clockwise
void arcLine(CDrawContext* c, double cx, double cy, double r, double a0, double a1, const CColor& col, double w = 1) {
  int n = std::max(4, int(std::fabs(a1 - a0) / 6));
  for (int i = 0; i < n; i++) {
    double t0 = (a0 + (a1 - a0) * i / n) * M_PI / 180, t1 = (a0 + (a1 - a0) * (i + 1) / n) * M_PI / 180;
    line(c, cx + r * std::cos(t0), cy + r * std::sin(t0), cx + r * std::cos(t1), cy + r * std::sin(t1), col, w);
  }
}

double textW(CDrawContext* c, const std::string& t, double size, bool bold = true, double squeeze = theme::kSqueeze) {
  c->setFont(theme::font(size, bold));
  double w = c->getStringWidth(t.c_str());
  return (w > 0 ? w : t.size() * size * 0.66) * squeeze;
}

// function group: tile with a coloured header and the control names below it
struct Lbl { const char* t; double x; };
void groupTile(CDrawContext* c, double x0, double x1, const std::string& title, const CColor& col, const std::vector<Lbl>& labels) {
  CRect t(x0, TILE_T, x1, TILE_B);
  CColor shade = rgb(0x6a, 0x4a, 0x2a, 40);
  fillRound(c, CRect(t).offset(0, 1.5), 7, shade);
  vGradient(c, t, mix(theme::tile, theme::cream, 0.35), theme::tile, 7);
  roundRect(c, t, 7, nullptr, &theme::tileEdge, 1);
  CRect h(x0 + 3, HEAD_T, x1 - 3, HEAD_B);
  vGradient(c, h, mix(col, rgb(255, 255, 255), 0.12), col, 4);
  if (!title.empty()) text(c, title, (x0 + x1) / 2, (HEAD_T + HEAD_B) / 2 + 0.5, 10.5, theme::cream);
  for (const Lbl& l : labels) text(c, l.t, l.x, LABEL_Y, LABEL);
}

// slider scale: ticks on the left of the slot (long at 0, 5, 10), numbers 0 / 5 / 10 for the first slider of a group
void scale(CDrawContext* c, double x, bool numbers = false) {
  for (int i = 0; i <= 10; i++) {
    double y = TRACK_B - i * (TRACK_B - TRACK_T) / 10;
    bool major = i % 5 == 0;
    line(c, x - (major ? 13 : 10), y, x - 5, y, major ? theme::ink : theme::inkDim, major ? 1.3 : 1);
    if (numbers && major) text(c, std::to_string(i), x - 15, y + 0.5, 8.5, theme::inkDim, true, kRightText, 1);
  }
}

const char* const MONO_TABS[] = { "TUBA", "TROMBONE", "TRUMPET", "SAX", "CLARINET", "OBOE", "FLUTE", "PICCOLO",
                                  "VIOLIN", "ACCORDION", "EL BASS", "EL GUITAR" };
} // namespace

PanelView::PanelView(PanelHost& h) : CView(CRect(0, 0, W, H)), host(h) {
  build();
  for (int i = 0; i < P_COUNT; i++) shown[i] = host.value(i);
  shownProgram = host.program();
  shownSerial = host.serial();
}

// x positions of the controls (panel coordinates)
namespace L {
constexpr double ENV0 = 46, ENV = 118, ENV1 = 134, MONO0 = 139, TUNE = 164, VOL = 208, MONO1 = 229;
constexpr double DEPTH = 262, SPEED = 300, WAVE = 354, ROT = 408, OSCSW = 486, OSC0 = 234, OSC1 = 524;
constexpr double PORTA0 = 530, TIMER = 556, PORTSW = 604, PORTA1 = 630;
constexpr double VCF0 = 636, VCF[4] = { 660, 698, 736, 774 }, VCF1 = 800;
constexpr double ADSR0 = 806, ADSR[4] = { 830, 870, 910, 950 }, ADSR1 = 976;
constexpr double VCA0 = 982, VCA[2] = { 1004, 1042 }, VCA1 = 1064;
constexpr double POLI0 = 1070, POLI[2] = { 1094, 1140 }, POLI1 = 1172;
constexpr double STR0 = 1178, STR[4] = { 1202, 1246, 1288, 1328 }, STR1 = 1352;
constexpr double LOGO0 = 1358, LOGO1 = 1480, PITCH0 = 1486, PITCH = 1521, PITCH1 = 1556;
constexpr double TABS = 92, TSTEP = 46;           // mono tabs
constexpr double FREE = 662, PINM = 724, SPLIT = 786, CMONO = 852, CPOLI = 898;
constexpr double PIANO = 966, PSTEP = 50, STR8 = 1230, STR4 = 1272, TOUCH = 1340;
} // namespace L

void PanelView::build() {
  auto add = [&](T t, int id, double x, double y) { Ctl k; k.type = t; k.id = id; k.x = x; k.y = y; ctls.push_back(k); return &ctls.back(); };
  add(T::Switch3, P_ENV_TRIGGER, L::ENV, 124);
  add(T::Centre, P_MONO_TUNING, L::TUNE, 0);
  add(T::Slider, P_MONO_VOLUME, L::VOL, 0);
  add(T::Slider, P_VIB_DEPTH, L::DEPTH, 0);
  add(T::Slider, P_VIB_SPEED, L::SPEED, 0);
  add(T::Wave, P_LFO_WAVE, L::WAVE, 124);
  add(T::Rotary, P_OSC_MODE, L::ROT, 132);
  add(T::Toggle, P_OSC_ON, L::OSCSW, 124);
  add(T::Slider, P_PORTA_TIME, L::TIMER, 0);
  add(T::Toggle, P_PORTA_ON, L::PORTSW, 124);
  const int vcfIds[4] = { P_VCF_ADSR, P_VCF_OSC, P_VCF_BIAS, P_VCF_EMPH }, adsrIds[4] = { P_ATTACK, P_DECAY, P_SUSTAIN, P_RELEASE },
            strIds[4] = { P_STR_VOLUME, P_STR_BRILL, P_STR_SUSTAIN, P_STR_ATTACK };
  for (int i = 0; i < 4; i++) { add(T::Slider, vcfIds[i], L::VCF[i], 0); add(T::Slider, adsrIds[i], L::ADSR[i], 0); add(T::Slider, strIds[i], L::STR[i], 0); }
  add(T::Slider, P_VCA_ADSR, L::VCA[0], 0);
  add(T::Slider, P_VCA_OSC, L::VCA[1], 0);
  add(T::Slider, P_POLI_VOLUME, L::POLI[0], 0);
  add(T::Slider, P_POLI_BRILL, L::POLI[1], 0);
  add(T::Knob, P_POLI_PITCH, L::PITCH, 132);
  add(T::Info, -1, (L::LOGO0 + L::LOGO1) / 2, 120);

  auto tab = [&](double x, int kind, int id, int val, CColor cap, const std::string& s) {
    Ctl* k = add(T::Tab, id, x, TAB_T);
    k->tabKind = kind; k->val = val; k->cap = cap; k->text = s;
  };
  const CColor cOrange = rgb(0xe2, 0x80, 0x44), cMustard = rgb(0xe3, 0xb2, 0x47), cAvocado = rgb(0x9c, 0xab, 0x52),
               cTeal = rgb(0x52, 0x94, 0x8c), cCream = rgb(0xec, 0xe1, 0xc6), cRust = rgb(0xc4, 0x5c, 0x35);
  for (int i = 0; i < 12; i++) {
    CColor col = i < 6 ? cOrange : i < 8 ? cMustard : i < 10 ? cAvocado : cTeal;
    tab(L::TABS + L::TSTEP * i, 0, P_MONO_REG, i + 1, col, MONO_TABS[i]);
  }
  tab(L::FREE, 1, P_FREE, 0, cCream, "FREE");
  tab(L::PINM, 1, P_POLI_IN_MONO, 0, cCream, "POLI & STRINGS\nIN MONO");
  tab(L::SPLIT, 1, P_SPLIT, 0, cCream, "SPLIT\nKEYBOARD");
  tab(L::CMONO, 2, P_MONO_REG, 0, cRust, "MONO");
  tab(L::CPOLI, 3, P_POLI_REG, 0, cRust, "POLI");
  const char* poli[5] = { "PIANO", "HARPSICHORD", "HONKY\nTONK", "BRASS", "BRASS\nVIBRATO" };
  const CColor pc[5] = { cTeal, cTeal, cTeal, cOrange, cCream };
  for (int i = 0; i < 5; i++) tab(L::PIANO + L::PSTEP * i, 0, P_POLI_REG, i + 1, pc[i], poli[i]);
  tab(L::STR8, 1, P_STRINGS_8, 0, cMustard, "8'");
  tab(L::STR4, 1, P_STRINGS_4, 0, cMustard, "4'");
  const char* touch[4] = { "MONO\nBRILL", "OSCILL", "MONO\nGLIDE", "BRASS\nBRILL" };
  const int touchIds[4] = { P_TOUCH_MONO_BRILL, P_TOUCH_OSCILL, P_TOUCH_MONO_GLIDE, P_TOUCH_BRASS_BRILL };
  const CColor grey = cAvocado;
  for (int i = 0; i < 4; i++) tab(L::TOUCH + L::TSTEP * i, 1, touchIds[i], 0, grey, touch[i]);
}

// ------------------------------------------------------------------ geometry and values
CRect PanelView::area(const Ctl& k) const {
  switch (k.type) {
    case T::Slider: case T::Centre: return CRect(k.x - 13, TRACK_T - 10, k.x + 13, TRACK_B + 10);
    case T::Toggle: case T::Wave: return CRect(k.x - 12, k.y - 26, k.x + 12, k.y + 26);
    case T::Switch3: return CRect(k.x - 66, k.y - 34, k.x + 12, k.y + 34);
    case T::Rotary: return CRect(k.x - 42, k.y - 46, k.x + 46, k.y + 26);
    case T::Knob: return CRect(k.x - 22, k.y - 24, k.x + 22, k.y + 24);
    case T::Tab: return CRect(k.x - TAB_W / 2 - 3, TAB_T - 12, k.x + TAB_W / 2 + 3, TAB_T + TAB_H + 2);
    case T::Info: return CRect(L::LOGO0 + 8, 72, L::LOGO1 - 8, 172);
  }
  return CRect();
}

int PanelView::hit(const CPoint& p) const {
  for (size_t i = 0; i < ctls.size(); i++)
    if (area(ctls[i]).pointInside(p)) return int(i);
  return -1;
}

void PanelView::set(int id, double norm) {
  norm = std::max(0.0, std::min(1.0, norm));
  host.beginEdit(id);
  host.performEdit(id, norm);
  host.endEdit(id);
  shown[id] = norm;
  invalid();
}

bool PanelView::tabLit(const Ctl& k) const {
  switch (k.tabKind) {
    case 0: {
      int idx = toIndex(k.id, v(k.id));
      // BRASS VIBRATO is an add-on of BRASS on the instrument: both lamps are lit
      return idx == k.val || (k.id == P_POLI_REG && k.val == PR_BRASS && idx == PR_BRASS_VIBRATO);
    }
    case 1: return v(k.id) >= 0.5;
    case 2: return toIndex(P_MONO_REG, v(P_MONO_REG)) == 0;                      // CANCEL lamp: nothing selected
    case 3: return toIndex(P_POLI_REG, v(P_POLI_REG)) == 0;
  }
  return false;
}

void PanelView::pressTab(const Ctl& k) {
  switch (k.tabKind) {
    case 0: {                                                   // touch switches: one register of the group
      int idx = toIndex(k.id, v(k.id)), val = k.val;
      if (k.id == P_POLI_REG && val == PR_BRASS_VIBRATO && idx == PR_BRASS_VIBRATO) val = PR_BRASS;   // vibrato off
      else if (k.id == P_POLI_REG && val == PR_BRASS && idx == PR_BRASS_VIBRATO) val = PR_BRASS_VIBRATO;
      set(k.id, fromIndex(k.id, val));
      break;
    }
    case 1: set(k.id, v(k.id) >= 0.5 ? 0.0 : 1.0); break;
    case 2: set(P_MONO_REG, 0.0); break;                        // CANCEL resets the SAS 570 S chips
    case 3: set(P_POLI_REG, 0.0); break;
  }
}

void PanelView::refresh() {
  bool dirty = false;
  for (int i = 0; i < P_COUNT; i++) {
    double x = host.value(i);
    if (std::fabs(x - shown[i]) > 1e-6) { shown[i] = x; dirty = true; }
  }
  int p = host.program();
  if (p != shownProgram) { shownProgram = p; dirty = true; }
  uint32_t s = host.serial();
  if (s != shownSerial) { shownSerial = s; dirty = true; }
  if (dirty) invalid();
}

// ------------------------------------------------------------------ mouse
void PanelView::onMouseDownEvent(MouseDownEvent& e) {
  if (e.buttonState.isRight()) { showMenu(e.mousePosition); e.consumed = true; return; }   // right click: the same menu
  if (!e.buttonState.isLeft()) return;
  int i = hit(e.mousePosition);
  if (i < 0) return;
  const Ctl& k = ctls[size_t(i)];
  e.consumed = true;
  switch (k.type) {
    case T::Slider: case T::Centre: case T::Knob: {
      if (e.clickCount >= 2 || e.modifiers.has(ModifierKey::Control)) { set(k.id, paramDef(k.id).def); return; }
      drag = i;
      dragStartY = e.mousePosition.y;
      dragStartX = e.mousePosition.x;
      dragStartV = v(k.id);
      dragFine = e.modifiers.has(ModifierKey::Shift);
      // click on the slot outside the cap: jump there (like grabbing the cap)
      if (k.type != T::Knob) {
        double capY = TRACK_B - v(k.id) * (TRACK_B - TRACK_T);
        if (std::fabs(e.mousePosition.y - capY) > CAP_H) {
          dragStartV = std::max(0.0, std::min(1.0, (TRACK_B - e.mousePosition.y) / (TRACK_B - TRACK_T)));
        }
      }
      host.beginEdit(k.id);
      shown[k.id] = dragStartV;
      host.performEdit(k.id, dragStartV);
      invalid();
      return;
    }
    case T::Toggle: set(k.id, v(k.id) >= 0.5 ? 0.0 : 1.0); return;
    case T::Wave: set(k.id, v(k.id) >= 0.5 ? 0.0 : 1.0); return;
    case T::Switch3: {
      double dy = e.mousePosition.y - k.y;
      int pos = dy < -8 ? 0 : dy > 8 ? 2 : 1;                   // Off top, Trigger middle, Phaser bottom
      set(k.id, fromIndex(k.id, pos));
      return;
    }
    case T::Rotary: {
      // positions on the dial: Adsr (left), Release (upper left), Attack (upper right), Continuous (right)
      double dx = e.mousePosition.x - k.x, dy = e.mousePosition.y - k.y;
      int pos;
      if (dx * dx + dy * dy < 15 * 15) pos = (toIndex(k.id, v(k.id)) + 1) % 4;      // on the knob: next position
      else {
        double a = std::atan2(-dy, dx) * 180 / M_PI;           // 0 = right, 90 = up
        pos = a > 135 || a < -90 ? OM_ADSR : a > 90 ? OM_RELEASE : a > 45 ? OM_ATTACK : OM_CONTINUOUS;
      }
      set(k.id, fromIndex(k.id, pos));
      return;
    }
    case T::Tab: pressTab(k); return;
    case T::Info: showMenu(e.mousePosition); return;
  }
}

void PanelView::onMouseMoveEvent(MouseMoveEvent& e) {
  if (drag < 0) return;
  const Ctl& k = ctls[size_t(drag)];
  bool fine = e.modifiers.has(ModifierKey::Shift);
  if (fine != dragFine) {        // switching fine mode: continue from here
    dragFine = fine; dragStartY = e.mousePosition.y; dragStartX = e.mousePosition.x; dragStartV = v(k.id);
  }
  double range = k.type == T::Knob ? 160 : (TRACK_B - TRACK_T);
  double d = (dragStartY - e.mousePosition.y) / range;
  if (k.type == T::Knob) d += (e.mousePosition.x - dragStartX) / range;
  if (fine) d *= 0.1;
  double nv = std::max(0.0, std::min(1.0, dragStartV + d));
  if (std::fabs(nv - shown[k.id]) > 1e-7) {
    shown[k.id] = nv;
    host.performEdit(k.id, nv);
    invalid();
  }
  e.consumed = true;
}

void PanelView::onMouseUpEvent(MouseUpEvent& e) {
  if (drag < 0) return;
  host.endEdit(ctls[size_t(drag)].id);
  drag = -1;
  e.consumed = true;
}

void PanelView::onMouseCancelEvent(MouseCancelEvent& e) {
  if (drag >= 0) host.endEdit(ctls[size_t(drag)].id);
  drag = -1;
  e.consumed = true;
}

void PanelView::onMouseWheelEvent(MouseWheelEvent& e) {
  int i = hit(e.mousePosition);
  if (i < 0) return;
  const Ctl& k = ctls[size_t(i)];
  if (k.type != T::Slider && k.type != T::Centre && k.type != T::Knob) return;
  double step = e.modifiers.has(ModifierKey::Shift) ? 0.002 : 0.02;
  set(k.id, v(k.id) + (e.deltaY > 0 ? step : -step));
  e.consumed = true;
}

void PanelView::showMenu(const CPoint& where) {
  CFrame* f = getFrame();
  if (!f) return;
  auto menu = makeOwned<COptionMenu>(CRect(where.x, where.y, where.x + 1, where.y + 1), nullptr, -1, nullptr, nullptr,
                                     COptionMenu::kPopupStyle | COptionMenu::kMultipleCheckStyle);
  // one action per menu entry (separators and disabled lines have none)
  auto actions = std::make_shared<std::vector<std::function<void()>>>();
  auto entry = [&](const std::string& title, bool checked, std::function<void()> fn) {
    menu->addEntry(UTF8String(title.c_str()), -1, fn ? (checked ? CMenuItem::kChecked : 0) : CMenuItem::kDisabled);
    actions->push_back(std::move(fn));
  };
  auto separator = [&] { menu->addSeparator(); actions->push_back(nullptr); };

  for (int i = 0; i < presetCount(); i++)
    entry(std::to_string(i + 1) + "  " + preset(i).name, i == shownProgram, [this, i] { host.selectProgram(i); });
  separator();
  uint32_t ser = host.serial();
  entry(ser ? "Ger\xC3\xA4t Nr. " + std::to_string(ser) + " (Bauteilstreuung)" : std::string("Ideale Bauteile"), false, nullptr);
  entry("Anderes Ger\xC3\xA4t w\xC3\xBCrfeln", false, [this] {
    std::random_device rd;
    host.newDevice(1000 + rd() % 9000);
  });
  entry("Ideale Bauteile", ser == 0, [this] { host.newDevice(0); });
  separator();
  bool mono = toIndex(P_OUT_MODE, v(P_OUT_MODE)) == 1;
  entry("Ausgang: Poli links / Mono rechts", !mono, [this] { set(P_OUT_MODE, 0.0); });
  entry("Ausgang: Mono (Summe)", mono, [this] { set(P_OUT_MODE, 1.0); });
  separator();
  double z = host.zoom();
  for (double step : { 0.75, 1.0, 1.5, 2.0 })
    entry("Gr\xC3\xB6\xC3\x9F" "e " + std::to_string(int(std::lround(step * 100))) + " %", std::fabs(z - step) < 0.01,
          [this, step] { host.setZoom(step); });

  menu->popup(f, where, [this, actions](COptionMenu* m) {
    int r = int(m->getLastResult());
    if (r < 0 || r >= int(actions->size()) || !(*actions)[size_t(r)]) return;
    (*actions)[size_t(r)]();
    refresh();
    invalid();
  });
}

// ------------------------------------------------------------------ drawing
void PanelView::draw(CDrawContext* c) {
  c->setDrawMode(kAntiAliasing | kNonIntegralMode);
  double zoom = c->getCurrentTransform().m11 * c->getScaleFactor();
  gMinLine = zoom > 0 ? 1.0 / zoom : 0;
  drawBackground(c);
  for (const Ctl& k : ctls) {
    switch (k.type) {
      case T::Slider: case T::Centre: drawSlider(c, k); break;
      case T::Toggle: case T::Wave: case T::Switch3: drawToggle(c, k); break;
      case T::Rotary: drawRotary(c, k); break;
      case T::Knob: drawKnob(c, k); break;
      case T::Tab: drawTab(c, k); break;
      case T::Info: drawInfo(c, k); break;
    }
  }
  setDirty(false);
}

namespace {
// colour of the function group a control belongs to (slider index line)
CColor groupColor(double x) {
  if (x < L::ENV1) return theme::rust;
  if (x < L::MONO1) return theme::orange;
  if (x < L::OSC1) return theme::mustard;
  if (x < L::PORTA1) return theme::avocado;
  if (x < L::VCF1) return theme::teal;
  if (x < L::ADSR1) return theme::rust;
  if (x < L::VCA1) return theme::orange;
  if (x < L::POLI1) return theme::mustard;
  if (x < L::STR1) return theme::avocado;
  return theme::teal;
}
// tile background colour at height y (for cut-outs over the tile gradient)
CColor tileAt(double y) {
  return mix(mix(theme::tile, theme::cream, 0.35), theme::tile, (y - TILE_T) / (TILE_B - TILE_T));
}
} // namespace

void PanelView::drawBackground(CDrawContext* c) {
  using namespace theme;
  // trim: top and bottom rails with rally stripes, side cheeks
  vGradient(c, CRect(0, 0, W, PANEL_T), trimLight, trim);
  vGradient(c, CRect(0, STRIP_B, W, H), trim, trimDark);
  auto stripes = [&](double y) {
    const CColor cols[3] = { orange, mustard, rust };
    for (int i = 0; i < 3; i++) fillRect(c, CRect(CHEEK, y + i * 3.5, W - CHEEK, y + i * 3.5 + 2.5), cols[i]);
  };
  stripes(4.5);
  stripes(STRIP_B + 3.5);
  // cream control panel and the dark tab strip
  vGradient(c, CRect(CHEEK, PANEL_T, W - CHEEK, PANEL_B), cream, creamDark);
  line(c, CHEEK, PANEL_T + 0.5, W - CHEEK, PANEL_T + 0.5, rgb(0x1a, 0x10, 0x08, 140), 1);
  vGradient(c, CRect(CHEEK, PANEL_B, W - CHEEK, STRIP_B), rgb(0x48, 0x2e, 0x1d), rgb(0x2a, 0x1a, 0x10));
  line(c, CHEEK, PANEL_B + 0.5, W - CHEEK, PANEL_B + 0.5, rgb(0x14, 0x0c, 0x06), 1.5);
  line(c, CHEEK, STRIP_B - 0.5, W - CHEEK, STRIP_B - 0.5, rgb(0x14, 0x0c, 0x06), 1);
  for (double sx : { CHEEK + 14, W - CHEEK - 14 }) {                 // brass screws of the tab strip
    disc(c, sx, PANEL_B + 21, 3.5, rgb(0xb9, 0x97, 0x5c));
    line(c, sx - 2.2, PANEL_B + 21, sx + 2.2, PANEL_B + 21, rgb(0x5a, 0x40, 0x20), 1);
  }
  for (int side = 0; side < 2; side++) {
    CRect r = side ? CRect(W - CHEEK, 0, W, H) : CRect(0, 0, CHEEK, H);
    if (side) hGradient(c, r, trimLight, trimDark);
    else hGradient(c, r, trimDark, trimLight);
    double edge = side ? r.left + 0.5 : r.right - 0.5;
    line(c, edge, 0, edge, H, rgb(0x14, 0x0c, 0x06), 1);
    double sx = side ? r.left + 9 : r.right - 11;                     // thin orange pin stripe on the cheeks
    fillRect(c, CRect(sx, 0, sx + 2, H), rgb(0xd9, 0x6b, 0x2b, 150));
  }

  // function groups
  groupTile(c, L::ENV0, L::ENV1, "ENV TRIGGER", rust, {});
  groupTile(c, L::MONO0, L::MONO1, "MONO", orange, { { "TUNING", L::TUNE }, { "VOLUME", L::VOL + 1 } });
  scale(c, L::TUNE);
  text(c, "#", L::TUNE + 13, 64, 12, ink, true, kCenterText, 1);
  text(c, "b", L::TUNE + 13, 188, 12, ink, true, kCenterText, 1);
  scale(c, L::VOL, true);
  groupTile(c, L::OSC0, L::OSC1, "OSCILLATOR", mustard, { { "DEPTH", L::DEPTH }, { "SPEED", L::SPEED } });
  scale(c, L::DEPTH, true); scale(c, L::SPEED);
  text(c, "VIBRATO", (L::DEPTH + L::SPEED) / 2 - 4, FOOT_Y, 8.5, inkDim);
  groupTile(c, L::PORTA0, L::PORTA1, "PORTAMENTO", avocado, { { "TIMER", L::TIMER } });
  scale(c, L::TIMER, true);
  groupTile(c, L::VCF0, L::VCF1, "VCF", teal,
            { { "ADSR", L::VCF[0] }, { "OSC", L::VCF[1] }, { "BIAS", L::VCF[2] }, { "EMPHASIS", L::VCF[3] + 1 } });
  for (int i = 0; i < 4; i++) scale(c, L::VCF[i], i == 0);
  groupTile(c, L::ADSR0, L::ADSR1, "ADSR", rust,
            { { "ATTACK", L::ADSR[0] }, { "DECAY", L::ADSR[1] }, { "SUSTAIN", L::ADSR[2] }, { "RELEASE", L::ADSR[3] + 1 } });
  for (int i = 0; i < 4; i++) {
    scale(c, L::ADSR[i], i == 0);
    // envelope icon under each slider, its own segment highlighted
    const double pts[5][2] = { { 0, 0 }, { 5, -8 }, { 9, -4.5 }, { 14, -4.5 }, { 18, 0 } };
    double x = L::ADSR[i] - 9, y = FOOT_Y + 4;
    for (int s = 0; s < 4; s++)
      line(c, x + pts[s][0], y + pts[s][1], x + pts[s + 1][0], y + pts[s + 1][1], s == i ? rust : inkDim, s == i ? 1.8 : 1);
  }
  groupTile(c, L::VCA0, L::VCA1, "VCA", orange, { { "ADSR", L::VCA[0] }, { "OSC", L::VCA[1] } });
  scale(c, L::VCA[0], true); scale(c, L::VCA[1]);
  groupTile(c, L::POLI0, L::POLI1, "POLI", mustard, { { "VOLUME", L::POLI[0] }, { "BRILLIANCE", L::POLI[1] + 1 } });
  scale(c, L::POLI[0], true); scale(c, L::POLI[1]);
  groupTile(c, L::STR0, L::STR1, "STRINGS", avocado,
            { { "VOLUME", L::STR[0] }, { "BRILLIANCE", L::STR[1] }, { "SUSTAIN", L::STR[2] + 1 }, { "ATTACK", L::STR[3] + 2 } });
  for (int i = 0; i < 4; i++) scale(c, L::STR[i], i == 0);
  groupTile(c, L::PITCH0, L::PITCH1, "POLI PITCH", teal, {});

  // name plate: sunset emblem, name, maker
  {
    CRect t(L::LOGO0, TILE_T, L::LOGO1, TILE_B);
    CColor shade = rgb(0x6a, 0x4a, 0x2a, 40);
    fillRound(c, CRect(t).offset(0, 1.5), 7, shade);
    vGradient(c, t, mix(tile, cream, 0.35), tile, 7);
    roundRect(c, t, 7, nullptr, &tileEdge, 1);
    double cx = L::LOGO0 + 21, cy = 45, r = 13;
    vGradient(c, CRect(cx - r, cy - r, cx + r, cy + r), mustard, rust, r);
    for (int i = 0; i < 3; i++) {                                     // horizontal cuts through the lower half
      double y = cy + 1 + i * 4.2, h = 1.2 + i * 0.7;
      fillRect(c, CRect(cx - r - 1, y, cx + r + 1, y + h), tileAt(y));
    }
    theme::label(c, "F.", CRect(cx + r + 5, 29, L::LOGO1 - 4, 50), rust, 21, true, kLeftText, true, 0.95);
    theme::label(c, "Klangmacher", CRect(cx + r + 6, 50, L::LOGO1 - 4, 64), ink, 12.5, true, kLeftText, true, 0.86);
    text(c, "BecauseOfReasons", (L::LOGO0 + L::LOGO1) / 2, FOOT_Y, 9, inkDim, true, kCenterText, 0.9);
  }

  // tab strip: lamps are above the tabs; group bars below the captions
  auto groupBar = [&](double x0, double x1, const std::string& name, const CColor& col, const CColor& txt) {
    double y = 304, tw = textW(c, name, 10) + 16, mid = (x0 + x1) / 2;
    line(c, x0, y, x1, y, col, 2);
    fillRound(c, CRect(x0 - 1, y - 4, x0 + 2, y + 4), 1, col);
    fillRound(c, CRect(x1 - 2, y - 4, x1 + 1, y + 4), 1, col);
    fillRound(c, CRect(mid - tw / 2, y - 7, mid + tw / 2, y + 7), 7, col);
    text(c, name, mid, y + 0.5, 10, txt);
  };
  groupBar(L::TABS - 18, L::TABS + 11 * L::TSTEP + 18, "MONO", printDim, ink);
  groupBar(L::CMONO - 18, L::CPOLI + 18, "CANCEL", rust, cream);
  groupBar(L::PIANO - 18, L::PIANO + 4 * L::PSTEP + 18, "POLI", teal, cream);
  groupBar(L::STR8 - 18, L::STR4 + 18, "STRINGS", mustard, trimDark);
  groupBar(L::TOUCH - 18, L::TOUCH + 3 * L::TSTEP + 18, "TOUCH CONTROL", avocado, cream);
  line(c, L::PINM - 16, 304, L::PINM + 16, 304, printDim, 2);
  {
    // range of the mono keys
    double y = 322, x0 = L::TABS + 6 * L::TSTEP - 18, x1 = W - CHEEK - 10, mid = (x0 + x1) / 2;
    double tw = textW(c, "MONO KEYS", 9) + 12;
    line(c, x0, y - 4, x0, y, printDim, 1);
    line(c, x0, y, mid - tw / 2, y, printDim, 1);
    line(c, mid + tw / 2, y, x1, y, printDim, 1);
    line(c, x1, y - 4, x1, y, printDim, 1);
    text(c, "MONO KEYS", mid, y + 0.5, 9, printDim);
  }
}

void PanelView::drawSlider(CDrawContext* c, const Ctl& k) {
  double x = k.x;
  fillRound(c, CRect(x - 2.5, TRACK_T - 6, x + 2.5, TRACK_B + 6), 2.5, theme::slot);
  line(c, x + 3, TRACK_T - 4, x + 3, TRACK_B + 4, rgb(255, 255, 255, 110), 1);
  // cap: brown block with an index line in the colour of its group
  double y = TRACK_B - v(k.id) * (TRACK_B - TRACK_T);
  CRect cap(x - CAP_W / 2, y - CAP_H / 2, x + CAP_W / 2, y + CAP_H / 2);
  fillRound(c, CRect(cap).offset(1, 2.5), 3, rgb(0x40, 0x28, 0x10, 90));
  vGradient(c, cap, theme::capTop, theme::capBottom, 2.5);
  CColor fr = rgb(0x1a, 0x0f, 0x08);
  roundRect(c, cap, 2.5, nullptr, &fr, 0.8);
  line(c, cap.left + 2, cap.top + 1.2, cap.right - 2, cap.top + 1.2, rgb(255, 255, 255, 60), 1);
  line(c, cap.left + 2, y + 0.5, cap.right - 2, y + 0.5, mix(groupColor(x), rgb(255, 255, 255), 0.25), 2);
}

void PanelView::drawToggle(CDrawContext* c, const Ctl& k) {
  const CColor& o = theme::ink;
  double x = k.x, y = k.y;
  int pos;                                   // lever direction: -1 up, 0 middle, 1 down
  if (k.type == T::Switch3) {
    pos = toIndex(k.id, v(k.id)) - 1;
    const char* names[3] = { "Off", "Trigger", "Phaser" };
    for (int i = 0; i < 3; i++) {
      double ly = y - 22 + 22 * i;
      text(c, names[i], x - 62, ly + 0.5, 10, o, true, kLeftText);
      line(c, x - (i == 1 ? 16 : 22), ly, x - 4, ly, theme::inkDim);
    }
    line(c, x - 4, y - 22, x - 4, y + 22, theme::inkDim);
  } else if (k.type == T::Wave) {
    pos = v(k.id) >= 0.5 ? 1 : -1;           // square up, triangle down
    double lx = x - 18;
    auto p = VSTGUI::owned(c->createGraphicsPath());
    if (p) {
      double sy = y - 14;
      p->beginSubpath(CPoint(lx - 8, sy + 3)); p->addLine(CPoint(lx - 8, sy - 3)); p->addLine(CPoint(lx - 3, sy - 3));
      p->addLine(CPoint(lx - 3, sy + 3)); p->addLine(CPoint(lx + 2, sy + 3)); p->addLine(CPoint(lx + 2, sy - 3));
      p->addLine(CPoint(lx + 6, sy - 3));
      double ty = y + 14;
      p->beginSubpath(CPoint(lx - 8, ty + 3)); p->addLine(CPoint(lx - 5, ty - 3)); p->addLine(CPoint(lx - 1, ty + 3));
      p->addLine(CPoint(lx + 3, ty - 3)); p->addLine(CPoint(lx + 6, ty + 3));
      c->setFrameColor(o); c->setLineWidth(1.4);
      c->drawGraphicsPath(p, CDrawContext::kPathStroked);
    }
    line(c, lx + 8, y - 14, x + 4, y - 14, theme::inkDim); line(c, x + 4, y - 14, x + 4, y + 14, theme::inkDim);
    line(c, lx + 8, y + 14, x + 4, y + 14, theme::inkDim);
  } else {
    pos = v(k.id) >= 0.5 ? -1 : 1;           // On up, Off down
    text(c, "On", x + 10, y - 21.5, 10, o, true, kLeftText);
    text(c, "Off", x + 10, y + 22.5, 10, o, true, kLeftText);
    line(c, x + 2, y - 22, x + 9, y - 22, theme::inkDim); line(c, x + 2, y - 22, x + 2, y + 22, theme::inkDim);
    line(c, x + 2, y + 22, x + 9, y + 22, theme::inkDim);
  }
  // bat handle switch on a brown plate
  fillRound(c, CRect(x - 8, y - 15, x + 8, y + 15), 3, rgb(0x3a, 0x24, 0x16));
  disc(c, x, y, 6, rgb(0x24, 0x16, 0x0d));
  disc(c, x, y, 4.5, rgb(0x9a, 0x96, 0x8e));
  double tipY = y + pos * 12;
  c->setLineStyle(kLineSolid);
  line(c, x, y, x, tipY, theme::chromeDark, 4.5);
  line(c, x, y, x, tipY, theme::chrome, 2.5);
  disc(c, x, tipY, 3.5, theme::chrome);
  disc(c, x - 0.8, tipY - 1.2, 1.3, rgb(255, 255, 255, 170));
}

namespace {
// brown bakelite knob with a cream pointer (angle in degrees, 0 = right, counter-clockwise)
void bakeliteKnob(CDrawContext* c, double x, double y, double r, double angle) {
  disc(c, x + 1, y + 2, r + 1, rgb(0x40, 0x28, 0x10, 90));
  disc(c, x, y, r, rgb(0x26, 0x17, 0x0e));
  for (int i = 0; i < 24; i++) {                                     // knurled skirt
    double a = i * M_PI / 12;
    line(c, x + std::cos(a) * (r - 3), y + std::sin(a) * (r - 3), x + std::cos(a) * r, y + std::sin(a) * r,
         rgb(0x55, 0x38, 0x24), 1);
  }
  vGradient(c, CRect(x - r + 4, y - r + 4, x + r - 4, y + r - 4), theme::capTop, theme::capBottom, r - 4);
  double a = angle * M_PI / 180;
  line(c, x + std::cos(a) * 3, y - std::sin(a) * 3, x + std::cos(a) * (r - 1.5), y - std::sin(a) * (r - 1.5),
       theme::cream, 2.2);
}
} // namespace

void PanelView::drawRotary(CDrawContext* c, const Ctl& k) {
  const CColor& o = theme::ink;
  double x = k.x, y = k.y;
  text(c, "Release", x - 16, y - 40, 9.5, o, true, kRightText);
  text(c, "Attack", x + 16, y - 40, 9.5, o, true, kLeftText);
  text(c, "Adsr", x - 24, y - 28, 9.5, o, true, kRightText);
  text(c, "Continuous", x + 24, y - 28, 9.5, o, true, kLeftText);
  const CColor& d = theme::inkDim;
  line(c, x - 14, y - 40, x - 6, y - 40, d); line(c, x - 6, y - 40, x - 6, y - 22, d);
  line(c, x + 14, y - 40, x + 6, y - 40, d); line(c, x + 6, y - 40, x + 6, y - 22, d);
  line(c, x - 22, y - 28, x - 16, y - 28, d); line(c, x - 16, y - 28, x - 16, y - 16, d);
  line(c, x + 22, y - 28, x + 16, y - 28, d); line(c, x + 16, y - 28, x + 16, y - 16, d);
  const double ang[4] = { 205, 120, 60, -25 };            // Adsr, Release, Attack, Continuous
  int idx = toIndex(k.id, v(k.id));
  for (int i = 0; i < 4; i++) {                           // position dots, the active one in the group colour
    double a = ang[i] * M_PI / 180;
    disc(c, x + std::cos(a) * 21, y - std::sin(a) * 21, i == idx ? 2.6 : 1.8, i == idx ? theme::mustard : theme::inkDim);
  }
  bakeliteKnob(c, x, y, 16, ang[idx]);
}

void PanelView::drawKnob(CDrawContext* c, const Ctl& k) {
  double x = k.x, y = k.y;
  arcLine(c, x, y, 21, 205, 335, theme::inkDim, 1.3);
  line(c, x, y - 24, x, y - 18, theme::ink, 1.3);
  text(c, "b", x - 24, y + 2, 12, theme::ink, true, kCenterText, 1);
  text(c, "#", x + 24, y + 2, 12, theme::ink, true, kCenterText, 1);
  bakeliteKnob(c, x, y, 13, 90 - (v(k.id) - 0.5) * 270);
}

void PanelView::drawTab(CDrawContext* c, const Ctl& k) {
  double x = k.x;
  CRect r(x - TAB_W / 2, TAB_T, x + TAB_W / 2, TAB_T + TAB_H);
  bool lit = tabLit(k);
  // amber lamp above the tab
  CRect lamp(x - 5, TAB_T - 10, x + 5, TAB_T - 5);
  if (lit) fillRound(c, lamp.inset(-3, -3), 5, rgb(0xff, 0xa8, 0x30, 70));
  fillRound(c, lamp, 1.5, lit ? theme::lampOn : theme::lampOff);
  if (lit) line(c, lamp.left + 2, lamp.top + 1.3, lamp.right - 2, lamp.top + 1.3, rgb(0xff, 0xec, 0xc0), 1);
  // rocker cap in its opening
  fillRound(c, r.inset(-2, -2), 3, rgb(0x14, 0x0c, 0x06));
  CRect face(r.left, r.top, r.right, r.bottom - 7), front(r.left, r.bottom - 7, r.right, r.bottom);
  vGradient(c, front, mix(k.cap, rgb(0, 0, 0), 0.25), mix(k.cap, rgb(0, 0, 0), 0.5), 2);
  vGradient(c, face, mix(k.cap, rgb(255, 255, 255), 0.3), k.cap, 2);
  line(c, r.left + 1, r.bottom - 7, r.right - 1, r.bottom - 7, mix(k.cap, rgb(0, 0, 0), 0.55), 1);
  // caption below (one or two lines)
  std::string s = k.text, s2;
  size_t nl = s.find('\n');
  if (nl != std::string::npos) { s2 = s.substr(nl + 1); s = s.substr(0, nl); }
  double size = CAPTION, maxW = 60;
  for (const std::string& t : { s, s2 }) {
    double w = t.empty() ? 0 : textW(c, t, CAPTION);
    if (w > maxW) size = std::min(size, CAPTION * maxW / w);
  }
  double ty = TAB_T + TAB_H + 11;
  text(c, s, x, ty, size, theme::print, true);
  if (!s2.empty()) text(c, s2, x, ty + size + 1.5, size, theme::print, true);
}

void PanelView::drawInfo(CDrawContext* c, const Ctl&) {
  // amber display in the name plate: registration, device, size (click: menu)
  CRect w = area(Ctl{ T::Info, -1, 0, 0 });
  CColor edge = rgb(0x14, 0x0c, 0x06);
  roundRect(c, w, 5, &theme::display, &edge, 1.5);
  line(c, w.left + 4, w.top + 2.5, w.right - 4, w.top + 2.5, rgb(0, 0, 0, 120), 1.5);
  int p = shownProgram;
  std::string name = p >= 0 && p < presetCount() ? preset(p).name : "";
  CColor dim = rgb(0xff, 0xb4, 0x2e, 130);
  double cx = w.getCenter().x;
  text(c, "REGISTRATION", cx, w.top + 15, 8.5, dim);
  if (!name.empty()) {
    std::string up;
    for (char ch : name) up += char(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
    text(c, std::to_string(p + 1), cx, w.top + 36, 17, theme::lampOn);
    double size = 11.5, maxW = w.getWidth() - 10, tw = textW(c, up, size);
    if (tw > maxW) size *= maxW / tw;
    text(c, up, cx, w.top + 60, size, theme::lampOn);
  }
  std::string dev = shownSerial ? "NR. " + std::to_string(shownSerial) : std::string("IDEAL");
  text(c, dev + "  \xC2\xB7  " + std::to_string(int(std::lround(host.zoom() * 100))) + " %", cx, w.bottom - 13, 8.5, dim);
}

} // namespace smk

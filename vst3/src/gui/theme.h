// Colours and fonts of the panel (own 70s design: cream panel, brown trim, orange / mustard / rust stripes)
#pragma once
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicstransform.h"
#include <string>

namespace smk {
namespace theme {

using VSTGUI::CColor;
inline CColor rgb(int r, int g, int b, int a = 255) { return CColor(uint8_t(r), uint8_t(g), uint8_t(b), uint8_t(a)); }

// frame: dark brown trim with rally stripes
const CColor trim = rgb(0x3e, 0x26, 0x18);
const CColor trimDark = rgb(0x22, 0x14, 0x0c);
const CColor trimLight = rgb(0x5c, 0x3b, 0x26);
// control panel: cream with darker tiles per function group
const CColor cream = rgb(0xf1, 0xe7, 0xcd);
const CColor creamDark = rgb(0xe4, 0xd8, 0xb8);
const CColor tile = rgb(0xe6, 0xd9, 0xb7);
const CColor tileEdge = rgb(0xbf, 0xab, 0x80);
const CColor ink = rgb(0x3a, 0x24, 0x16);            // print on the cream panel
const CColor inkDim = rgb(0x86, 0x69, 0x48);
const CColor print = rgb(0xf0, 0xe2, 0xbf);          // print on the dark tab strip
const CColor printDim = rgb(0xb3, 0x9c, 0x76);
// 70s accents
const CColor rust = rgb(0xb3, 0x47, 0x22);
const CColor orange = rgb(0xd9, 0x6b, 0x2b);
const CColor mustard = rgb(0xc9, 0x93, 0x22);
const CColor avocado = rgb(0x76, 0x86, 0x31);
const CColor teal = rgb(0x2e, 0x72, 0x6d);
// controls
const CColor slot = rgb(0x2a, 0x19, 0x0f);
const CColor capTop = rgb(0x6a, 0x47, 0x30);
const CColor capBottom = rgb(0x2c, 0x1a, 0x10);
const CColor chrome = rgb(0xd6, 0xd2, 0xc8);
const CColor chromeDark = rgb(0x70, 0x6a, 0x60);
const CColor lampOn = rgb(0xff, 0xb4, 0x2e);         // amber lamps and display
const CColor lampOff = rgb(0x6e, 0x4a, 0x1e);
const CColor display = rgb(0x1e, 0x13, 0x0c);

inline VSTGUI::SharedPointer<VSTGUI::CFontDesc> font(double size, bool bold = false, bool italic = false) {
#if defined(_WIN32) || defined(__APPLE__)
  const char* family = "Arial";
#else
  const char* family = "Liberation Sans";
#endif
  int style = (bold ? VSTGUI::kBoldFace : 0) | (italic ? VSTGUI::kItalicFace : 0);
  return VSTGUI::makeOwned<VSTGUI::CFontDesc>(family, size, style);
}

// The panel print uses a condensed typeface: text is squeezed horizontally (squeeze < 1).
constexpr double kSqueeze = 0.82;

inline void label(VSTGUI::CDrawContext* c, const std::string& s, const VSTGUI::CRect& r, const CColor& col, double size,
                  bool bold = true, VSTGUI::CHoriTxtAlign align = VSTGUI::kCenterText, bool italic = false,
                  double squeeze = 1.0) {
  c->setFont(font(size, bold, italic));
  c->setFontColor(col);
  if (squeeze == 1.0) { c->drawString(s.c_str(), r, align, true); return; }
  double ax = align == VSTGUI::kLeftText ? r.left : align == VSTGUI::kRightText ? r.right : r.getCenter().x;
  VSTGUI::CDrawContext::Transform t(*c, VSTGUI::CGraphicsTransform(squeeze, 0, 0, 1, ax * (1 - squeeze), 0));
  VSTGUI::CRect wide(ax + (r.left - ax) / squeeze, r.top, ax + (r.right - ax) / squeeze, r.bottom);
  c->drawString(s.c_str(), wide, align, true);
}

} // namespace theme
} // namespace smk

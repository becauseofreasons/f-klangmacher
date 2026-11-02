// Editor size: the allowed zoom steps and the user-wide default for new instances
// (a small text file: %APPDATA%\FKlangmacher\settings.txt, ~/Library/Application Support/FKlangmacher/settings.txt
// on macOS, ~/.config/FKlangmacher/settings.txt on Linux).
// A project stores its own size in the controller state.
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#ifdef _WIN32
#include <direct.h>
#include <wchar.h>
#else
#include <sys/stat.h>
#endif

namespace smk {

constexpr double kZoomSteps[] = { 0.75, 1.0, 1.5, 2.0 };
constexpr int kZoomCount = 4;

inline double snapZoom(double z) {
  double best = 1.0;
  for (double s : kZoomSteps)
    if (std::fabs(s - z) < std::fabs(best - z)) best = s;
  return best;
}

namespace detail {
#ifdef _WIN32
inline std::wstring settingsDir() {
  const wchar_t* app = _wgetenv(L"APPDATA");
  return app && *app ? std::wstring(app) + L"\\FKlangmacher" : std::wstring();
}
inline FILE* openSettings(bool write) {
  std::wstring dir = settingsDir();
  if (dir.empty()) return nullptr;
  if (write) _wmkdir(dir.c_str());
  return _wfopen((dir + L"\\settings.txt").c_str(), write ? L"w" : L"r");
}
#else
inline std::string settingsDir() {
#ifdef __APPLE__
  const char* home = std::getenv("HOME");
  return home && *home ? std::string(home) + "/Library/Application Support/FKlangmacher" : std::string();
#endif
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) return std::string(xdg) + "/FKlangmacher";
  const char* home = std::getenv("HOME");
  return home && *home ? std::string(home) + "/.config/FKlangmacher" : std::string();
}
inline FILE* openSettings(bool write) {
  std::string dir = settingsDir();
  if (dir.empty()) return nullptr;
  if (write) {
    mkdir((dir.substr(0, dir.rfind('/'))).c_str(), 0755);
    mkdir(dir.c_str(), 0755);
  }
  return std::fopen((dir + "/settings.txt").c_str(), write ? "w" : "r");
}
#endif
} // namespace detail

inline double loadDefaultZoom() {
  double z = 1.0;
  if (FILE* f = detail::openSettings(false)) {
    int percent = 0;                       // whole percent: no decimal point, no locale trouble
    if (std::fscanf(f, "zoom=%d", &percent) == 1 && percent > 0) z = snapZoom(percent / 100.0);
    std::fclose(f);
  }
  return z;
}

inline void saveDefaultZoom(double z) {
  if (FILE* f = detail::openSettings(true)) {
    std::fprintf(f, "zoom=%d\n", int(std::lround(snapZoom(z) * 100)));
    std::fclose(f);
  }
}

} // namespace smk

// Small DSP building blocks for the circuit models (header only, no allocation).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace smk {

constexpr double PI = 3.14159265358979323846;

inline double clamp(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }
inline double clamp01(double x) { return clamp(x, 0.0, 1.0); }
// coefficient of an RC low pass for time constant tau (s): y += a * (x - y)
inline double rcCoef(double tau, double fs) { return tau <= 0 ? 1.0 : 1.0 - std::exp(-1.0 / (tau * fs)); }
// audio-taper (log) potentiometer: wiper fraction for a knob position 0..1 (10 % at half travel)
inline double logPot(double x) { x = clamp01(x); return (std::pow(10.0, 2.0 * x) - 1.0) / 99.0; }
// fast soft clipper (OTA / transistor stages)
inline double softClip(double x) { return x / std::sqrt(1.0 + x * x); }

// PolyBLEP residual for a step at phase t (0..1) with phase increment dt
inline double polyBlep(double t, double dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
  if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
  return 0.0;
}

// one pole low pass / high pass (topology preserving, cut-off can change every sample)
struct OnePole {
  double s = 0, g = 0;
  void setFc(double fc, double fs) { double w = std::tan(PI * clamp(fc, 1.0, fs * 0.45) / fs); g = w / (1 + w); }
  double lp(double x) { double v = (x - s) * g; double y = v + s; s = y + v; return y; }
  double hp(double x) { return x - lp(x); }
  void reset(double v = 0) { s = v; }
};

// state variable filter (Simper / TPT), 2 poles
struct Svf {
  double ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1.4142;
  void set(double fc, double q, double fs) {
    double g = std::tan(PI * clamp(fc, 5.0, fs * 0.45) / fs);
    k = 1.0 / std::max(0.05, q);
    a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
  }
  // returns low pass, band pass in bp, high pass in hp
  double run(double x, double* bp = nullptr, double* hp = nullptr) {
    double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
    if (bp) *bp = v1;
    if (hp) *hp = x - k * v1 - v2;
    return v2;
  }
  void reset() { ic1 = ic2 = 0; }
};

// biquad (RBJ cookbook), for fixed passive / active filters
struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void lowpass(double fc, double q, double fs) { design(fc, q, fs, 0); }
  void highpass(double fc, double q, double fs) { design(fc, q, fs, 1); }
  void bandpass(double fc, double q, double fs) { design(fc, q, fs, 2); }
  void design(double fc, double q, double fs, int type) {
    double w = 2 * PI * clamp(fc, 5.0, fs * 0.45) / fs, cs = std::cos(w), al = std::sin(w) / (2 * std::max(0.05, q));
    double a0 = 1 + al;
    if (type == 0) { b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = b0; }
    else if (type == 1) { b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = b0; }
    else { b0 = al; b1 = 0; b2 = -al; }
    a1 = -2 * cs; a2 = 1 - al;
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
  }
  double run(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
  void reset() { z1 = z2 = 0; }
};

// parameter smoothing (about 10 ms)
struct Smooth {
  double y = 0, a = 0.002;
  void init(double v, double fs, double tau = 0.01) { y = v; a = rcCoef(tau, fs); }
  double run(double target) { y += a * (target - y); return y; }
};

// deterministic random numbers (component tolerances): mulberry32
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed) {}
  uint32_t next() { uint32_t t = (s += 0x6D2B79F5u); t = (t ^ (t >> 15)) * (t | 1u); t ^= t + (t ^ (t >> 7)) * (t | 61u); return t ^ (t >> 14); }
  double uni() { return next() / 4294967296.0; }                     // 0..1
  double sym() { return uni() * 2.0 - 1.0; }                         // -1..1
  // value with tolerance tol (e.g. 0.05 = 5 %), roughly normal (sum of 3 uniforms)
  double tol(double tol) { return 1.0 + tol * (sym() + sym() + sym()) / 3.0 * 1.7; }
};

} // namespace smk

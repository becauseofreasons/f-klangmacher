#include "device.h"
#include "dsp.h"

namespace smk {

Device Device::make(uint32_t serial) {
  Device d;
  d.serial = serial;
  for (int k = 0; k < 61; k++) d.percTau[k] = d.brassTau[k] = d.strTau[k] = d.keyLevel[k] = 1;
  for (double& x : d.duty) x = 0;
  if (serial == 0) return d;

  Rng r(serial * 2654435761u + 12345u);
  // trimmed by the technician: the remaining error is what you can still hear as a slow beat
  d.poliCents = r.sym() * 1.5;
  d.monoCents = r.sym() * 2.5;
  // electrolytic keyer capacitors (10 uF / 2.2 uF / 1 uF, -20/+50 % in the seventies, here a bit kinder)
  for (int k = 0; k < 61; k++) {
    d.percTau[k] = r.tol(0.12);
    d.brassTau[k] = r.tol(0.12);
    d.strTau[k] = r.tol(0.12);
    d.keyLevel[k] = r.tol(0.04);
  }
  for (double& x : d.duty) x = r.sym() * 0.01;
  d.vcfCut = r.tol(0.06);
  d.vcfRes = r.tol(0.05);
  d.vcaOffset = r.sym() * 0.004;           // VR4 offset trim residual: a very small click
  d.lfoRate = r.tol(0.08);
  for (double& x : d.adsrTime) x = r.tol(0.08);
  d.sustain = r.tol(0.03);
  d.sawLevel = r.tol(0.03);
  d.glide = r.tol(0.05);
  d.poliVcfCut = r.tol(0.06);
  d.brassEnv = r.tol(0.08);
  d.strAttack = r.tol(0.08);
  d.strBrill = r.tol(0.05);
  d.chorusRate[0] = r.tol(0.03);           // trimmed to 1.67 s / 167 ms (VR1/VR2 of PA 934)
  d.chorusRate[1] = r.tol(0.03);
  for (double& x : d.bbdBias) x = r.sym() * 0.02;
  return d;
}

} // namespace smk

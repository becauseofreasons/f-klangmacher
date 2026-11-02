#include "presets.h"

namespace smk {

namespace {
const Preset PRESETS[] = {
#include "gen/presets.inc"
};
constexpr int N = int(sizeof PRESETS / sizeof PRESETS[0]);
} // namespace

int presetCount() { return N; }
const Preset& preset(int index) { return PRESETS[index < 0 ? 0 : index >= N ? N - 1 : index]; }

} // namespace smk

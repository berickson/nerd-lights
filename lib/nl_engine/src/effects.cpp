// Effects, ported from pattern-engine.reference.js (redesign handoff).
// Pixel-moving effects are applied in engine.cpp's sample-position step.
#include "nl_internal.h"

namespace nl {

static const nl_param_def MOTION_PARAMS[] = {
  {"speed", "Speed", -60, 60, 1, "px/s", 10},
  {"swing", "Swing", 0, 120, 1, "px", 0},
  {"smooth", "Anti-aliasing", 0, 100, 1, "%", 100},
};

#define COUNT(a) (uint8_t)(sizeof(a) / sizeof(a[0]))

const EffectDef EFFECTS[NL_FX_COUNT] = {
  {"Motion", "Movement", "Scrolls any pattern along the strip, or swings it back and forth.",
   F_MOVES, MOTION_PARAMS, COUNT(MOTION_PARAMS), false, {0, 0, 0}},
};

}  // namespace nl

// Engine internals shared by engine.cpp, bases.cpp and effects.cpp.
#pragma once
#include "nl_engine.h"
#include "nl_math.h"

namespace nl {

enum {
  F_NO_COLORS = 1,   // uses its own colors (Rainbow, Chomp, Sorting, Traffic)
  F_STATEFUL = 2,    // keeps state that advances in NL_STEP_MS steps
  F_WARN = 4,        // photosensitivity warning (Strobe)
  F_MOVES = 8        // effect remaps the sample position (Motion, Mirror, Earthquake)
};

struct Program;

struct BaseDef {
  const char *name;
  const char *group;
  const char *desc;
  uint8_t flags;
  const nl_param_def *params;
  uint8_t n_params;
  // Stateless bases: the color at Q16 position xq.
  Col (*sample)(const Program &p, int64_t xq, int64_t t_ms);
  // Frame bases: state size, setup, one step, and drawing the current state.
  uint32_t (*state_bytes)(const Program &p);
  void (*init)(Program &p);
  void (*step)(Program &p);
  void (*frame)(const Program &p, Col *out);
};

struct EffectDef {
  const char *name;
  const char *group;
  const char *desc;
  uint8_t flags;
  const nl_param_def *params;
  uint8_t n_params;
  bool has_default_color;
  nl_rgb default_color;
};

extern const BaseDef BASES[NL_BASE_COUNT];
extern const EffectDef EFFECTS[NL_FX_COUNT];

struct Program {
  nl_config cfg;          // clamped copy
  uint16_t n;
  Col colors[NL_MAX_COLORS];
  uint8_t n_colors;       // at least 1: no colors means white
  const BaseDef *base;
  uint32_t seed;          // stateful base seed, from the zone
  Rng rng;
  void *state;
  uint32_t state_size;
  Col *frame;             // n entries, frame bases only
  bool started;
  int64_t last_step_ms;
};

inline float bp(const Program &p, int i) { return p.cfg.params[i]; }
inline const Col &pal(const Program &p, int j) {
  int L = p.n_colors;
  return p.colors[((j % L) + L) % L];
}
// Add light to a pixel, clamped per channel, at a rounded position (spec put()).
void put(Col *a, int n, float pos, const Col &c, float k = 1.0f);

}  // namespace nl

struct nl_program : nl::Program {};

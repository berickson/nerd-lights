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
struct FxRun;

// Per-frame values a base or effect works out once in prepare() and reads per pixel.
struct Scratch {
  int64_t i[6];
  float f[24];
};

struct BaseDef {
  const char *name;
  const char *group;
  const char *desc;
  uint8_t flags;
  const nl_param_def *params;
  uint8_t n_params;
  // Per frame, before sampling (optional).
  void (*prepare)(Program &p);
  // Stateless bases: the color at Q16 position xq.
  Col (*sample)(const Program &p, int64_t xq);
  // Optional fast path: count samples from x0q, one pixel apart.
  void (*row)(const Program &p, int64_t x0q, int count, Col *out);
  // Frame bases: draw the whole frame (from state, or from the time alone).
  void (*frame)(Program &p, Col *out);
  // Stateful bases: state size, setup, and one NL_STEP_MS step.
  uint32_t (*state_bytes)(const Program &p);
  void (*init)(Program &p);
  void (*step)(Program &p);
};

struct EffectDef {
  const char *name;
  const char *group;
  const char *desc;
  uint8_t flags;
  const nl_param_def *params;
  uint8_t n_params;
  const char *default_color;   // "#rrggbb", or null for the pattern's colors
  // Per frame (optional).
  void (*prepare)(const Program &p, FxRun &f);
  // Per pixel, after the base: change v.
  void (*apply)(const Program &p, const FxRun &f, int i, Col &v);
  // Frame effects draw their own frame, composited over the pattern by brightness.
  void (*frame)(const Program &p, FxRun &f, Col *out);
  uint32_t (*state_bytes)(const Program &p, const FxRun &f);
  void (*init)(Program &p, FxRun &f);
  void (*step)(Program &p, FxRun &f);
};

extern const BaseDef BASES[NL_BASE_COUNT];
extern const EffectDef EFFECTS[NL_FX_COUNT];

struct FxRun {
  const EffectDef *def;
  const nl_effect_config *cfg;
  uint32_t seed;          // from type and instance number, not the zone, so
                          // matching zones stay in step
  int64_t t_ms;           // frame time plus this instance's offset
  Col colors[NL_MAX_COLORS];
  uint8_t n_colors;       // raw count; 0 means the pattern has no colors
  Rng rng;
  void *state;
  Col *frame;
  Scratch s;
};

struct Program {
  nl_config cfg;          // clamped copy
  uint16_t n;
  Col colors[NL_MAX_COLORS];
  uint8_t n_colors;       // at least 1: no colors means white
  uint8_t raw_colors;     // the count the zone really has
  const BaseDef *base;
  uint32_t seed;          // stateful base seed, from the zone
  Rng rng;
  void *state;
  uint32_t bytes;         // everything allocated, for nl_program_bytes
  Col *frame;             // n entries, frame bases only
  Col *samples;           // n + 1 entries
  FxRun fx[NL_MAX_EFFECTS];
  uint8_t n_fx;           // effects that are on, in stack order
  bool stateful;          // base or any effect steps
  bool started;
  int64_t last_step_ms;
  int64_t t_ms;           // the frame being rendered
  int32_t utc_offset_s;   // for Binary clock
  float out_gamma;        // nl_program_set_output; 0 until set, meaning 1
  bool bright_after;
  Scratch s;
};

inline float bp(const Program &p, int i) { return p.cfg.params[i]; }
inline float fp(const FxRun &f, int i) { return f.cfg->params[i]; }
inline int bpi(const Program &p, int i) { return roundi(p.cfg.params[i]); }
inline int fpi(const FxRun &f, int i) { return roundi(f.cfg->params[i]); }
inline const Col &pal(const Program &p, int64_t j) {
  int L = p.n_colors;
  return p.colors[(int)(((j % L) + L) % L)];
}
inline Col fpal(const FxRun &f, int64_t j) {
  if (!f.n_colors) return col(255, 255, 255);
  int L = f.n_colors;
  return f.colors[(int)(((j % L) + L) % L)];
}
// Add light to a pixel, clamped per channel, at a rounded position (spec put()).
void put(Col *a, int n, float pos, const Col &c, float k = 1.0f);
// Value noise at a Q16 coordinate (spec vn()).
float value_noise(int64_t c);

}  // namespace nl

struct nl_program : nl::Program {};

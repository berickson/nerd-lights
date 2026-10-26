// Nerd Lights pattern engine: one library for the controller firmware and the
// WebAssembly web preview. Given the same config, seed and times, every target
// produces the same bytes. See docs/active/pattern-engine-plan.md (web repo).
//
// Rules that keep it deterministic:
// - hashes and random numbers are integer (nl_math.h), never libm or rand()
// - time is integer milliseconds; large times are reduced with integer math
//   before any float sees them
// - stateful patterns advance in fixed 20 ms steps, independent of frame rate
// - float only, with our own sin/exp2/log2, built with -ffp-contract=off
// - nothing here reads a clock or allocates while rendering
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NL_MAX_COLORS 8
#define NL_MAX_EFFECTS 8
#define NL_MAX_PARAMS 6
#define NL_MAX_PIXELS 1000
#define NL_STEP_MS 20

enum nl_base_id {
  NL_BASE_NORMAL = 0,
  NL_BASE_FIRE,
  NL_BASE_TETRIS,
  NL_BASE_COUNT
};

enum nl_effect_id {
  NL_FX_MOTION = 0,
  NL_FX_COUNT
};

// Where an effect takes its color from.
enum nl_effect_color {
  NL_COLOR_DEFAULT = 0,   // the effect's own default (or pattern colors if it has none)
  NL_COLOR_PATTERN = 1,   // the zone's colors
  NL_COLOR_CUSTOM = 2     // nl_effect_config.color
};

typedef struct { uint8_t r, g, b; } nl_rgb;

typedef struct {
  uint16_t type;          // nl_effect_id
  uint16_t instance;      // stable copy number ("Ghost 2"), feeds its seed
  uint8_t on;
  uint8_t color_mode;     // nl_effect_color
  nl_rgb color;
  float params[NL_MAX_PARAMS];  // in the order of the effect's param table
} nl_effect_config;

typedef struct {
  uint16_t base;          // nl_base_id
  float params[NL_MAX_PARAMS];  // in the order of the base's param table
  nl_rgb colors[NL_MAX_COLORS];
  uint8_t n_colors;
  nl_effect_config effects[NL_MAX_EFFECTS];
  uint8_t n_effects;
  uint8_t brightness;     // 1-100
  uint32_t seed;          // per zone; the host passes the zone id
} nl_config;

typedef struct {
  const char *key;
  const char *label;
  float min, max, step;
  const char *unit;
  float def;
} nl_param_def;

typedef struct nl_program nl_program;

// Fill cfg with a base's defaults: its params, no colors, no effects, 100%.
void nl_config_defaults(nl_config *cfg, uint16_t base);
// Set an effect's params to its defaults.
void nl_effect_defaults(nl_effect_config *fx, uint16_t type);

// Copies and clamps cfg; out-of-range params are clamped, NaN takes the default.
// Returns null when n_px is 0 or above NL_MAX_PIXELS, or memory runs out.
nl_program *nl_program_create(const nl_config *cfg, uint16_t n_px);
void nl_program_free(nl_program *p);
// Bytes of state the program holds, for heap budgeting.
uint32_t nl_program_bytes(const nl_program *p);

// Render the frame at t_ms into rgb (n_px * 3 bytes, r g b order).
// Stateful patterns start at the first rendered time and step to t_ms. A jump of
// more than NL_MAX_CATCHUP_MS (or backwards) restarts the stepping clock at
// t_ms without replaying, which is the stutter when the clock switches to NTP.
#define NL_MAX_CATCHUP_MS 1000
void nl_program_render(nl_program *p, int64_t t_ms, uint8_t *rgb);

// Runs the golden vectors (nl_selftest.cpp) and writes one line per vector,
// "name crc32", into out. Returns the length written.
int nl_selftest(char *out, int out_size);

#ifdef __cplusplus
}
#endif

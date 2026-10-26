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

// Ids are stable: they feed seeds and golden vectors. Add new ones at the end.
enum nl_base_id {
  NL_BASE_NORMAL = 0,
  NL_BASE_FIRE,
  NL_BASE_TETRIS,
  NL_BASE_RAINBOW,
  NL_BASE_METEOR,
  NL_BASE_TWINKLE,
  NL_BASE_CONFETTI,
  NL_BASE_BREATHE,
  NL_BASE_EXPLOSION,
  NL_BASE_STROBE,
  NL_BASE_CANDLES,
  NL_BASE_SNAKE,
  NL_BASE_PONG,
  NL_BASE_CHOMP,
  NL_BASE_SORTING,
  NL_BASE_AUTOMATON,
  NL_BASE_HOURGLASS,
  NL_BASE_BINARY_CLOCK,
  NL_BASE_LAVA_LAMP,
  NL_BASE_AURORA,
  NL_BASE_GRADIENT,
  NL_BASE_CHASE,
  NL_BASE_FLICKER,
  NL_BASE_LIGHTNING,
  NL_BASE_COUNT
};

enum nl_effect_id {
  NL_FX_MOTION = 0,
  NL_FX_MIRROR,
  NL_FX_SHIMMER,
  NL_FX_SPARKLE,
  NL_FX_CANDLE,
  NL_FX_PULSE,
  NL_FX_HEARTBEAT,
  NL_FX_WAVE,
  NL_FX_RAINBOW_SWEEP,
  NL_FX_FIREFLIES,
  NL_FX_FIREWORKS,
  NL_FX_RAIN,
  NL_FX_TRAFFIC,
  NL_FX_DATA_STREAM,
  NL_FX_BOUNCING_BALLS,
  NL_FX_LASER_DUEL,
  NL_FX_LIGHTNING,
  NL_FX_SNOWFALL,
  NL_FX_COMET,
  NL_FX_GHOST,
  NL_FX_SPOOKY_EYES,
  NL_FX_BROWNOUT,
  NL_FX_GLITCH,
  NL_FX_EARTHQUAKE,
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

// Output stage. The engine works in screen-like values (what the web preview
// draws as-is). LEDs give off light in proportion to the value, so controllers
// set gamma 2.2: the final float values go through a 4096-step gamma table to
// 16-bit light, which rounds to a byte once. brightness_after_gamma scales that
// light by the brightness (49% = 49% of the light) instead of scaling values
// before gamma (even steps by eye). Defaults: gamma 1 (linear), before.
void nl_program_set_output(nl_program *p, float gamma, uint8_t brightness_after_gamma);

// Local time offset from UTC in seconds, for Binary clock. Hosts update it when
// daylight saving changes. Default 0.
void nl_program_set_utc_offset(nl_program *p, int32_t seconds);

// Every base and effect with its group, description, flags and params, as JSON:
// {"bases":[{"id","name","group","desc","stateful","ownColors","warn","params":[...]}],
//  "effects":[{..., "moves", "tints", "color":"#rrggbb"|null, ...}]}, in display order.
// Writes up to out_size bytes (always terminated) and returns the full length,
// so a caller can retry with a bigger buffer.
int nl_engine_describe(char *out, int out_size);

// Names and parameter tables, for UIs and console commands.
const char *nl_base_name(uint16_t base);
uint8_t nl_base_flags(uint16_t base);          // 2 = stateful, as in nl_internal.h
uint8_t nl_base_param_count(uint16_t base);
const nl_param_def *nl_base_param(uint16_t base, uint8_t i);
const char *nl_effect_name(uint16_t type);
uint8_t nl_effect_param_count(uint16_t type);
const nl_param_def *nl_effect_param(uint16_t type, uint8_t i);

// Setters, so hosts without the struct layout (JavaScript) can fill a config.
uint32_t nl_config_size(void);
void nl_config_set_param(nl_config *cfg, uint8_t i, float v);
void nl_config_set_colors(nl_config *cfg, uint8_t n);
void nl_config_set_color(nl_config *cfg, uint8_t j, uint8_t r, uint8_t g, uint8_t b);
void nl_config_set_brightness(nl_config *cfg, uint8_t brightness);
void nl_config_set_seed(nl_config *cfg, uint32_t seed);
// Appends an effect with its defaults; returns its index, or -1 when full.
int nl_config_add_effect(nl_config *cfg, uint16_t type);
void nl_config_set_effect_param(nl_config *cfg, uint8_t k, uint8_t i, float v);
void nl_config_set_effect_instance(nl_config *cfg, uint8_t k, uint16_t instance);
// mode is an nl_effect_color; the color only matters for NL_COLOR_CUSTOM.
void nl_config_set_effect_color(nl_config *cfg, uint8_t k, uint8_t mode, uint8_t r, uint8_t g, uint8_t b);

// A zone's run of pixels on one strip (see layout.cpp).
typedef struct {
  uint16_t start_pixel;   // first physical pixel on the strip, 0-based
  uint16_t light_count;
  uint16_t zone_offset;   // chain: the zone pixel this piece starts at
  uint8_t reversed;
} nl_piece;

// Copy a rendered zone (zone_len pixels, rgb) onto a strip through its pieces.
// copy != 0: every piece shows the zone from its start; else they chain.
void nl_layout_apply(const uint8_t *zone_rgb, uint16_t zone_len, uint8_t copy,
                     const nl_piece *pieces, int n_pieces,
                     uint8_t *strip_rgb, uint16_t strip_len);

// Runs the golden vectors (nl_selftest.cpp) and writes one line per vector,
// "name crc32", into out. Returns the length written.
int nl_selftest(char *out, int out_size);

#ifdef __cplusplus
}
#endif

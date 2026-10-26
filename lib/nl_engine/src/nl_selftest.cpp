// Golden vectors. Every target (ESP32, WebAssembly in node and the browser) runs
// these and prints "name crc32"; test/engine/golden.txt holds the expected
// lines. A vector renders `frames` frames from t0, frame_ms apart, and the CRC
// covers every byte of every frame.
#include <stdio.h>
#include <stdlib.h>
#include "nl_internal.h"

using namespace nl;

static const int64_t T_2026 = 1793000000000LL;  // 2026-10-26
static const int64_t T_2100 = 4102444800000LL;  // 2100-01-01

struct Vector {
  const char *name;
  uint16_t n;
  int64_t t0;
  int frames;
  int frame_ms;
  int jump_at;        // frame index that jumps ahead by jump_ms (0: none)
  int jump_ms;
  void (*setup)(nl_config &c);
  int32_t utc_offset_s;
  float gamma;            // 0: no nl_program_set_output (linear)
  uint8_t bright_after;
};

static void colors(nl_config &c, int count) {
  static const nl_rgb PAL[] = {
    {0x00, 0xff, 0xff}, {0xff, 0xb8, 0x00}, {0xff, 0x00, 0xff}, {0x00, 0x10, 0xff},
    {0x00, 0xff, 0x00}, {0xff, 0x5a, 0x5a}, {0xff, 0x00, 0x00}, {0xff, 0x8a, 0x2a},
  };
  c.n_colors = (uint8_t)count;
  for (int j = 0; j < count; j++) c.colors[j] = PAL[j];
}

static nl_effect_config &motion(nl_config &c, float speed, float swing, float smooth) {
  nl_effect_config &fx = c.effects[c.n_effects++];
  nl_effect_defaults(&fx, NL_FX_MOTION);
  fx.params[0] = speed;
  fx.params[1] = swing;
  fx.params[2] = smooth;
  return fx;
}

static const Vector VECTORS[] = {
  {"normal_36", 36, T_2026, 30, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_NORMAL); colors(c, 3); }},
  {"normal_gap_motion_1000", 1000, T_2026, 30, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_NORMAL); colors(c, 4);
     c.params[0] = 7; c.params[1] = 3; c.params[2] = 40; motion(c, 10, 0, 100); }},
  {"normal_motion_neg_300", 300, T_2026, 60, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_NORMAL); colors(c, 2); motion(c, -37, 0, 50); }},
  {"normal_motion_nearest_300", 300, T_2026, 60, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_NORMAL); colors(c, 5); c.params[2] = 100; motion(c, 23, 0, 0); }},
  {"normal_swing_300", 300, T_2026, 60, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_NORMAL); colors(c, 3); motion(c, 25, 30, 100); }},
  {"normal_2100_300", 300, T_2100, 30, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_NORMAL); colors(c, 8); c.brightness = 37; motion(c, 60, 0, 100); }},
  {"normal_1px_nocolor", 1, T_2026, 10, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_NORMAL); }},
  {"fire_36", 36, T_2026, 60, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_FIRE); colors(c, 3); }},
  {"fire_one_color_300", 300, T_2026, 30, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_FIRE); colors(c, 1); c.params[0] = 90; c.params[1] = 100; }},
  {"fire_motion_1000", 1000, T_2026, 30, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_FIRE); colors(c, 4); motion(c, -15, 0, 100); }},
  {"fire_2100_300", 300, T_2100, 30, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_FIRE); colors(c, 2); c.params[0] = 0; c.params[1] = 1; }},
  {"tetris_36", 36, T_2026, 1800, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_TETRIS); colors(c, 3); c.seed = 75; }},
  {"tetris_fast_300", 300, T_2026, 600, 100, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_TETRIS); colors(c, 6); c.seed = 76;
     c.params[0] = 150; c.params[1] = 20; c.params[2] = 1; }},
  {"tetris_jump_36", 36, T_2026, 600, 33, 300, 5000, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_TETRIS); colors(c, 2); c.seed = 77; motion(c, 7, 0, 100); }},
  {"tetris_1px", 1, T_2026, 300, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_TETRIS); c.seed = 78; }},
  // Phase 2 special cases (the loops in nl_selftest add one vector per base and effect)
  {"stack_fire_300", 300, T_2026, 120, 250, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_FIRE); colors(c, 3); c.brightness = 60; c.seed = 80;
     nl_config_add_effect(&c, NL_FX_MIRROR); motion(c, 12, 0, 100);
     nl_config_add_effect(&c, NL_FX_SHIMMER); nl_config_add_effect(&c, NL_FX_SPARKLE);
     nl_config_add_effect(&c, NL_FX_FIREWORKS); nl_config_add_effect(&c, NL_FX_BOUNCING_BALLS);
     nl_config_add_effect(&c, NL_FX_EARTHQUAKE); nl_config_add_effect(&c, NL_FX_LIGHTNING); }},
  {"ghost_twice_100", 100, T_2026, 120, 250, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_RAINBOW);
     int a = nl_config_add_effect(&c, NL_FX_GHOST), b = nl_config_add_effect(&c, NL_FX_GHOST);
     nl_config_set_effect_instance(&c, a, 1); nl_config_set_effect_instance(&c, b, 2); }},
  {"color_modes_100", 100, T_2026, 240, 250, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_TWINKLE); colors(c, 2);
     int a = nl_config_add_effect(&c, NL_FX_LIGHTNING), b = nl_config_add_effect(&c, NL_FX_SPARKLE);
     nl_config_set_effect_color(&c, a, NL_COLOR_CUSTOM, 255, 0, 128);
     nl_config_set_effect_color(&c, b, NL_COLOR_PATTERN, 0, 0, 0); }},
  {"binary_clock_tz_36", 36, T_2026, 20, 1000, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_BINARY_CLOCK); colors(c, 3); }, -7 * 3600},
  {"aurora_nocolor_100", 100, T_2026, 30, 250, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_AURORA); }},
  {"candles_spaced_100", 100, T_2026, 30, 250, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_CANDLES); c.params[0] = 4; c.params[2] = 20; }},
  {"candles_warmth_20_36", 36, T_2026, 30, 250, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_CANDLES); colors(c, 2); c.params[4] = 20; }},
  {"pong_5px", 5, T_2026, 60, 100, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_PONG); colors(c, 2); }},
  {"sorting_1000", 1000, T_2026, 40, 500, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_SORTING); c.seed = 81; c.params[0] = 60; }},
  {"automaton_rule110_300", 300, T_2026, 120, 250, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_AUTOMATON); colors(c, 2); c.params[0] = 110; c.params[1] = 30; c.seed = 82; }},
  // The original firmware patterns (Phase 2b)
  {"twinkle_background_300", 300, T_2026, 120, 100, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_TWINKLE); colors(c, 4); c.params[0] = 30; }},
  {"explosion_cap_1000", 1000, T_2026, 20, 100, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_EXPLOSION); colors(c, 3); c.params[0] = 50; }},
  {"meteor_reverse_300", 300, T_2026, 60, 100, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_METEOR); colors(c, 3); c.params[0] = -250; c.params[1] = 30; c.params[2] = 0.5f; }},
  {"lightning_storm_100", 100, T_2026, 600, 33, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_LIGHTNING); colors(c, 2); c.params[0] = 100; c.seed = 83; }},
  {"twinkle_short_9", 9, T_2026, 60, 250, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_TWINKLE); colors(c, 1); }},
  {"explosion_short_9", 9, T_2026, 60, 250, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_EXPLOSION); colors(c, 1); }},
  {"strobe_one_color_10", 10, T_2026, 60, 50, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_STROBE); colors(c, 1); c.params[0] = 5; }},
  // Output gamma, as the controllers run it
  {"gamma_before_lava_100", 100, T_2026, 60, 250, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_LAVA_LAMP); colors(c, 2); c.brightness = 49; }, 0, 2.2f, 0},
  {"gamma_after_breathe_36", 36, T_2026, 60, 100, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_BREATHE); colors(c, 1); c.brightness = 49; }, 0, 2.2f, 1},
  {"linear_after_fire_36", 36, T_2026, 30, 100, 0, 0, [](nl_config &c) {
     nl_config_defaults(&c, NL_BASE_FIRE); colors(c, 3); c.brightness = 30; }, 0, 1.0f, 1},
};

// "Rain & ripples" -> "rain_ripples"
static void slug(char *out, int size, const char *prefix, const char *name, int n) {
  int len = snprintf(out, size, "%s", prefix);
  bool sep = false;
  for (const char *c = name; *c && len < size - 8; c++) {
    char ch = *c;
    if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
    bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
    if (!alnum) { sep = true; continue; }
    if (sep && len > (int)strlen(prefix)) out[len++] = '_';
    sep = false;
    out[len++] = ch;
  }
  snprintf(out + len, size - len, "_%d", n);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, int len) {
  static uint32_t table[256];
  if (!table[1])
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
      table[i] = c;
    }
  for (int i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
  return crc;
}

// One vector: render frames and print "name crc32".
static int run_vector(const char *name, const nl_config &c, uint16_t n, int64_t t0, int frames,
                      int frame_ms, int jump_at, int jump_ms, int32_t utc_offset_s, uint8_t *rgb,
                      char *out, int room, float gamma = 0.0f, uint8_t bright_after = 0) {
  nl_program *p = nl_program_create(&c, n);
  if (!p) return snprintf(out, room, "%s create-failed\n", name);
  nl_program_set_utc_offset(p, utc_offset_s);
  if (gamma > 0.0f) nl_program_set_output(p, gamma, bright_after);
  uint32_t crc = 0xffffffffu;
  int64_t t = t0;
  for (int f = 0; f < frames; f++) {
    if (jump_at && f == jump_at) t += jump_ms;
    nl_program_render(p, t, rgb);
    crc = crc32_update(crc, rgb, n * 3);
    t += frame_ms;
  }
  nl_program_free(p);
  return snprintf(out, room, "%s %08x\n", name, (unsigned)~crc);
}

extern "C" int nl_selftest(char *out, int out_size) {
  int used = 0;
  uint8_t *rgb = (uint8_t *)malloc(NL_MAX_PIXELS * 3);
  if (!rgb) return snprintf(out, out_size, "ERROR out of memory\n");
  auto add = [&](int w) {
    if (w < 0 || w >= out_size - used) return false;
    used += w;
    return true;
  };
  bool room = true;
  for (const Vector &v : VECTORS) {
    nl_config c;
    v.setup(c);
    room = add(run_vector(v.name, c, v.n, v.t0, v.frames, v.frame_ms, v.jump_at, v.jump_ms,
                          v.utc_offset_s, rgb, out + used, out_size - used, v.gamma, v.bright_after));
    if (!room) break;
  }
  // Zone pieces onto a strip: one zone, 50 px, rendered with motion and laid
  // out three ways on a 60 px strip.
  {
    nl_config c;
    nl_config_defaults(&c, NL_BASE_NORMAL);
    colors(c, 3);
    motion(c, 9, 0, 100);
    static const nl_piece chain[] = {{0, 20, 0, 0}, {25, 30, 20, 1}};
    static const nl_piece copy[] = {{0, 20, 0, 1}, {30, 30, 0, 0}};
    static const nl_piece clipped[] = {{50, 20, 40, 0}};   // runs past the zone and the strip
    const struct { const char *name; const nl_piece *p; int n; uint8_t copy; } cases[] = {
      {"layout_chain_60", chain, 2, 0}, {"layout_copy_60", copy, 2, 1}, {"layout_clipped_60", clipped, 1, 0}};
    uint8_t *strip = (uint8_t *)malloc(60 * 3);
    nl_program *p = nl_program_create(&c, 50);
    for (int k = 0; room && strip && p && k < 3; k++) {
      uint32_t crc = 0xffffffffu;
      for (int f = 0; f < 30; f++) {
        nl_program_render(p, T_2026 + f * 100, rgb);
        memset(strip, 0, 60 * 3);
        nl_layout_apply(rgb, 50, cases[k].copy, cases[k].p, cases[k].n, strip, 60);
        crc = crc32_update(crc, strip, 60 * 3);
      }
      room = add(snprintf(out + used, out_size - used, "%s %08x\n", cases[k].name, (unsigned)~crc));
    }
    nl_program_free(p);
    free(strip);
  }
  // Every base with its defaults and three colors, 60 s at 2 fps.
  for (uint16_t b = NL_BASE_RAINBOW; room && b < NL_BASE_COUNT; b++) {
    nl_config c;
    nl_config_defaults(&c, b);
    colors(c, 3);
    c.seed = 90 + b;
    char name[48];
    slug(name, sizeof name, "base_", nl_base_name(b), 36);
    room = add(run_vector(name, c, 36, T_2026, 120, 500, 0, 0, 0, rgb, out + used, out_size - used));
  }
  // Every effect with its defaults over striped Normal, 60 s at 2 fps.
  for (uint16_t e = NL_FX_MIRROR; room && e < NL_FX_COUNT; e++) {
    nl_config c;
    nl_config_defaults(&c, NL_BASE_NORMAL);
    colors(c, 3);
    c.seed = 120 + e;
    nl_config_add_effect(&c, e);
    char name[48];
    slug(name, sizeof name, "fx_", nl_effect_name(e), 36);
    room = add(run_vector(name, c, 36, T_2026, 120, 500, 0, 0, 0, rgb, out + used, out_size - used));
  }
  free(rgb);
  return used;
}

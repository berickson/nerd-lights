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
};

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, int len) {
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return crc;
}

extern "C" int nl_selftest(char *out, int out_size) {
  int used = 0;
  uint8_t *rgb = (uint8_t *)malloc(NL_MAX_PIXELS * 3);
  if (!rgb) return snprintf(out, out_size, "ERROR out of memory\n");
  for (const Vector &v : VECTORS) {
    nl_config c;
    v.setup(c);
    nl_program *p = nl_program_create(&c, v.n);
    bool ok = p != nullptr;
    uint32_t crc = 0xffffffffu;
    if (ok) {
      int64_t t = v.t0;
      for (int f = 0; f < v.frames; f++) {
        if (v.jump_at && f == v.jump_at) t += v.jump_ms;
        nl_program_render(p, t, rgb);
        crc = crc32_update(crc, rgb, v.n * 3);
        t += v.frame_ms;
      }
      nl_program_free(p);
    }
    int w = snprintf(out + used, out_size - used, ok ? "%s %08x\n" : "%s create-failed\n",
                     v.name, (unsigned)~crc);
    if (w < 0 || w >= out_size - used) break;
    used += w;
  }
  free(rgb);
  return used;
}

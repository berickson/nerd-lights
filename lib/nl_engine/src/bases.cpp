// Base patterns, ported from pattern-engine.reference.js (redesign handoff).
// Stateless bases hash without the zone seed, so two zones showing the same look
// at the same time are in step. Stateful bases use the zone seed.
#include <stdlib.h>
#include "nl_internal.h"

namespace nl {

void put(Col *a, int n, float pos, const Col &c, float k) {
  int i = ftoi_floor(pos + 0.5f);
  if (i < 0 || i >= n || !(k > 0.0f)) return;
  Col &o = a[i];
  o.r = minf(255.0f, o.r + c.r * k);
  o.g = minf(255.0f, o.g + c.g * k);
  o.b = minf(255.0f, o.b + c.b * k);
}

static const uint32_t NOISE_SEED = 0x6e6f6973u;

// Value noise at a Q16 coordinate (spec vn()).
static float value_noise(int64_t c) {
  uint32_t i = (uint32_t)q_floor(c);
  float f = q_frac(c);
  float u = f * f * (3.0f - 2.0f * f);
  return hash01(NOISE_SEED, i) * (1.0f - u) + hash01(NOISE_SEED, i + 1) * u;
}

// --- Normal ---

static const nl_param_def NORMAL_PARAMS[] = {
  {"band", "Band width", 1, 40, 1, "px", 6},
  {"gap", "Dark gap", 0, 20, 1, "px", 0},
  {"blend", "Blend", 0, 100, 1, "%", 60},
};

static Col normal_sample(const Program &p, int64_t xq, int64_t) {
  int L = p.n_colors;
  int64_t bw = (int64_t)bp(p, 0), g = (int64_t)bp(p, 1), cell = bw + g;
  float b = bp(p, 2) / 100.0f;
  int64_t pos = floor_mod(xq, cell * L * Q16);
  int idx = (int)(pos / (cell * Q16));
  if (idx > L - 1) idx = L - 1;
  float w = (float)(pos - idx * cell * Q16) * (1.0f / 65536.0f);
  float bwf = (float)bw;
  if (g > 0) {
    if (w >= bwf) return col(0, 0, 0);
    float e = b * bwf / 2.0f;
    float k = e > 0.0f ? minf(1.0f, minf(w / e, (bwf - w) / e)) : 1.0f;
    return scale(p.colors[idx], k);
  }
  float k = b > 0.0f ? maxf(0.0f, (w / bwf - (1.0f - b)) / b) : 0.0f;
  k = k * k * (3.0f - 2.0f * k);
  return mix(p.colors[idx], p.colors[(idx + 1) % L], k);
}

// --- Fire ---

static const nl_param_def FIRE_PARAMS[] = {
  {"heat", "Heat", 0, 100, 1, "%", 60},
  {"speed", "Pace", 1, 100, 1, "", 40},
};

static Col fire_sample(const Program &p, int64_t xq, int64_t t_ms) {
  int64_t pace = (int64_t)bp(p, 1);
  int64_t c1 = floor_div(xq * 12, 100) + scale_q16(t_ms, pace, 60000);
  int64_t c2 = floor_div(xq * 40, 100) - scale_q16(t_ms, pace, 15000);
  float h = powf_(value_noise(c1) * 0.6f + value_noise(c2) * 0.4f,
                  1.8f - bp(p, 0) / 100.0f * 1.2f);
  // Ramp black -> colors (-> pale yellow when only one color).
  Col S[NL_MAX_COLORS + 2];
  int len = 0;
  S[len++] = col(0, 0, 0);
  for (int j = 0; j < p.n_colors; j++) S[len++] = p.colors[j];
  if (p.n_colors == 1) S[len++] = mix(p.colors[0], col(255, 255, 220), 0.6f);
  h = clampf(h, 0.0f, 0.999f) * (float)(len - 1);
  int a = ftoi_floor(h);
  return mix(S[a], S[a + 1], h - (float)a);
}

// --- Tetris ---
// Pixel 0 is the bottom of the well. Blocks drop from the far end and stack;
// when the stack nears the top it flashes and clears.

static const nl_param_def TETRIS_PARAMS[] = {
  {"speed", "Drop speed", 5, 150, 1, "px/s", 50},
  {"minSize", "Smallest block", 1, 20, 1, "px", 2},
  {"maxSize", "Largest block", 1, 20, 1, "px", 8},
};

struct TetrisState {
  int32_t acc;            // thousandths of a pixel of drop owed
  int32_t clear_ms;       // > 0 while the full stack flashes
  uint32_t k;             // next color index
  int32_t stack_len;
  bool has_blk;
  int32_t blk_pos, blk_len;
  uint8_t blk_c;
  uint8_t stack[1];       // stack_capacity entries: color index << 1 | dim
};

static int tetris_lo(const Program &p) {
  float a = bp(p, 1), b = bp(p, 2);
  int lo = ftoi_floor(minf(a, b) + 0.5f);
  return lo < 1 ? 1 : lo;
}
static int tetris_hi(const Program &p) {
  float a = bp(p, 1), b = bp(p, 2);
  int hi = ftoi_floor(maxf(a, b) + 0.5f), lo = tetris_lo(p);
  return hi < lo ? lo : hi;
}

static uint32_t tetris_bytes(const Program &p) {
  return (uint32_t)sizeof(TetrisState) + p.n + 20;
}

static void tetris_new_block(Program &p, TetrisState &s) {
  int lo = tetris_lo(p), hi = tetris_hi(p);
  s.has_blk = true;
  s.blk_pos = p.n;
  s.blk_len = lo + (int32_t)p.rng.below((uint32_t)(hi - lo + 1));
  s.blk_c = (uint8_t)(s.k++ % p.n_colors);
}

static void tetris_init(Program &p) {
  TetrisState &s = *(TetrisState *)p.state;
  tetris_new_block(p, s);
}

static void tetris_step(Program &p) {
  TetrisState &s = *(TetrisState *)p.state;
  if (s.clear_ms > 0) {
    s.clear_ms -= NL_STEP_MS;
    if (s.clear_ms <= 0) s.stack_len = 0;
    return;
  }
  if (!s.has_blk) tetris_new_block(p, s);
  s.acc += (int32_t)(bp(p, 0) * NL_STEP_MS + 0.5f);
  int hi = tetris_hi(p);
  for (int moves = 0; s.acc >= 1000 && moves < 60; moves++) {
    s.acc -= 1000;
    if (!s.has_blk) continue;
    s.blk_pos -= 1;
    if (s.blk_pos <= s.stack_len) {
      for (int j = 0; j < s.blk_len; j++)
        s.stack[s.stack_len++] = (uint8_t)(s.blk_c << 1 | (j == 0 && s.blk_len > 1 ? 1 : 0));
      if (s.stack_len >= p.n - hi) {
        s.has_blk = false;
        s.clear_ms = 1200;
      } else {
        tetris_new_block(p, s);
      }
    }
  }
}

static Col stack_color(const Program &p, uint8_t code) {
  Col c = p.colors[code >> 1];
  return (code & 1) ? scale(c, 0.55f) : c;
}

static void tetris_frame(const Program &p, Col *a) {
  const TetrisState &s = *(const TetrisState *)p.state;
  int n = p.n;
  if (s.clear_ms > 0) {
    bool on = ((s.clear_ms * 8) / 1000) % 2 == 0;
    for (int i = 0; i < s.stack_len; i++)
      put(a, n, (float)i, on ? col(255, 255, 255) : stack_color(p, s.stack[i]));
    return;
  }
  for (int i = 0; i < s.stack_len; i++) put(a, n, (float)i, stack_color(p, s.stack[i]));
  if (s.has_blk) {
    Col c = p.colors[s.blk_c];
    for (int j = 0; j < s.blk_len; j++)
      put(a, n, (float)(s.blk_pos + j), (j == 0 && s.blk_len > 1) ? scale(c, 0.55f) : c);
  }
}

#define COUNT(a) (uint8_t)(sizeof(a) / sizeof(a[0]))

const BaseDef BASES[NL_BASE_COUNT] = {
  {"Normal", "Classic",
   "Repeating bands of your colors - space and blend them. Add the Motion effect to move them.",
   0, NORMAL_PARAMS, COUNT(NORMAL_PARAMS), normal_sample, nullptr, nullptr, nullptr, nullptr},
  {"Fire", "Classic", "Licking flames built from your colors.",
   0, FIRE_PARAMS, COUNT(FIRE_PARAMS), fire_sample, nullptr, nullptr, nullptr, nullptr},
  {"Tetris", "Games",
   "Pixel 0 is the bottom of the well. Blocks drop in from the far end and stack up; "
   "when the stack reaches the top it flashes and clears.",
   F_STATEFUL, TETRIS_PARAMS, COUNT(TETRIS_PARAMS), nullptr, tetris_bytes, tetris_init,
   tetris_step, tetris_frame},
};

}  // namespace nl

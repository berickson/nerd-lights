// Base patterns, ported from pattern-engine.reference.js (redesign handoff).
// Stateless bases hash without the zone seed, so two zones showing the same look
// at the same time are in step. Stateful bases use the zone seed.
//
// Time: the spec's float seconds t become integer milliseconds T, split with
// phase(), window() and rad_q32() before any float sees them.
#include <stdlib.h>
#include "nl_internal.h"

namespace nl {

void put(Col *a, int n, float pos, const Col &c, float k) {
  int i = roundi(pos);
  if (i < 0 || i >= n || !(k > 0.0f)) return;
  Col &o = a[i];
  o.r = minf(255.0f, o.r + c.r * k);
  o.g = minf(255.0f, o.g + c.g * k);
  o.b = minf(255.0f, o.b + c.b * k);
}

static const uint32_t NOISE_SEED = 0x6e6f6973u;
static const uint32_t BASE_SEED = 0x62617365u;

float value_noise(int64_t c) {
  uint32_t i = (uint32_t)q_floor(c);
  float f = q_frac(c);
  float u = f * f * (3.0f - 2.0f * f);
  return hash01(NOISE_SEED, i) * (1.0f - u) + hash01(NOISE_SEED, i + 1) * u;
}

static inline float hb(int64_t a, uint32_t salt) { return hh(BASE_SEED, a, salt); }
static inline float hb2(int64_t a, int64_t b, uint32_t salt) {
  return unit(hash32(hash32(BASE_SEED, (uint32_t)a, salt), (uint32_t)b));
}
static inline int64_t q16(float x) { return (int64_t)(x * 65536.0f); }
static inline int pixel_of(int64_t xq) { return (int)q_floor(xq + Q16 / 2); }  // Math.round(x)
static const Col BLACK = {0, 0, 0};
static const Col WHITE = {255, 255, 255};

#define COUNT(a) (uint8_t)(sizeof(a) / sizeof(a[0]))

// ============================================================ Classic

// --- Normal ---
static const nl_param_def NORMAL_PARAMS[] = {
  {"band", "Band width", 1, 40, 1, "px", 6},
  {"gap", "Dark gap", 0, 20, 1, "px", 0},
  {"blend", "Blend", 0, 100, 1, "%", 60},
};

// Color at offset w (Q16) into band idx.
static Col normal_at(const Program &p, int idx, int32_t wq) {
  int L = p.n_colors;
  float bwf = bp(p, 0), b = bp(p, 2) / 100.0f;
  float w = (float)wq * (1.0f / 65536.0f);
  if (bp(p, 1) > 0.0f) {
    if (w >= bwf) return BLACK;
    float e = b * bwf / 2.0f;
    float k = e > 0.0f ? minf(1.0f, minf(w / e, (bwf - w) / e)) : 1.0f;
    return scale(p.colors[idx], k);
  }
  float k = b > 0.0f ? maxf(0.0f, (w / bwf - (1.0f - b)) / b) : 0.0f;
  k = k * k * (3.0f - 2.0f * k);
  return mix(p.colors[idx], p.colors[(idx + 1) % L], k);
}

static Col normal_sample(const Program &p, int64_t xq) {
  int64_t cellq = (int64_t)(bpi(p, 0) + bpi(p, 1)) * Q16;
  int64_t pos = floor_mod(xq, cellq * p.n_colors);
  int idx = (int)(pos / cellq);
  return normal_at(p, idx, (int32_t)(pos - idx * cellq));
}

static void normal_row(const Program &p, int64_t x0q, int count, Col *out) {
  int32_t cellq = (bpi(p, 0) + bpi(p, 1)) * (int32_t)Q16;
  int64_t pos = floor_mod(x0q, (int64_t)cellq * p.n_colors);
  int idx = (int)(pos / cellq);
  int32_t wq = (int32_t)(pos - (int64_t)idx * cellq);
  for (int k = 0; k < count; k++) {
    out[k] = normal_at(p, idx, wq);
    wq += (int32_t)Q16;
    while (wq >= cellq) {
      wq -= cellq;
      if (++idx == p.n_colors) idx = 0;
    }
  }
}

// --- Rainbow ---
static const nl_param_def RAINBOW_PARAMS[] = {
  {"length", "Rainbow length", 10, 600, 1, "px", 100},
};

static Col rainbow_sample(const Program &p, int64_t xq) {
  int64_t lenq = (int64_t)bpi(p, 0) * Q16;
  return hsv((float)floor_mod(xq, lenq) / (float)lenq);
}

static void rainbow_row(const Program &p, int64_t x0q, int count, Col *out) {
  int64_t lenq = (int64_t)bpi(p, 0) * Q16;
  int64_t pos = floor_mod(x0q, lenq);
  for (int k = 0; k < count; k++) {
    out[k] = hsv((float)pos / (float)lenq);
    pos += Q16;
    if (pos >= lenq) pos -= lenq;
  }
}

// --- Fire ---
static const nl_param_def FIRE_PARAMS[] = {
  {"heat", "Heat", 0, 100, 1, "%", 60},
  {"speed", "Pace", 1, 100, 1, "", 40},
};

static void fire_prepare(Program &p) {
  int64_t pace = bpi(p, 1);
  p.s.i[0] = scale_q16(p.t_ms, pace, 60000);
  p.s.i[1] = scale_q16(p.t_ms, pace, 15000);
  p.s.f[0] = 1.8f - bp(p, 0) / 100.0f * 1.2f;
}

// Ramp black -> colors (-> pale yellow when only one color), at h.
static Col fire_color(const Program &p, float h) {
  int L = p.n_colors, len = L + (L == 1 ? 2 : 1);
  h = clampf(h, 0.0f, 0.999f) * (float)(len - 1);
  int a = ftoi_floor(h);
  Col pale = mix(p.colors[0], col(255, 255, 220), 0.6f);
  Col ca = a == 0 ? BLACK : p.colors[a - 1];
  Col cb = a + 1 <= L ? p.colors[a] : pale;
  return mix(ca, cb, h - (float)a);
}

static Col fire_at(const Program &p, int64_t c1, int64_t c2) {
  float h = powf_(value_noise(c1) * 0.6f + value_noise(c2) * 0.4f, p.s.f[0]);
  return fire_color(p, h);
}

static Col fire_sample(const Program &p, int64_t xq) {
  return fire_at(p, floor_div(xq * 12, 100) + p.s.i[0], floor_div(xq * 40, 100) - p.s.i[1]);
}

// Same values as fire_sample, stepping x*12/100 and x*40/100 without dividing.
static void fire_row(const Program &p, int64_t x0q, int count, Col *out) {
  int64_t q1 = floor_div(x0q * 12, 100), r1 = x0q * 12 - q1 * 100;
  int64_t q2 = floor_div(x0q * 40, 100), r2 = x0q * 40 - q2 * 100;
  for (int k = 0; k < count; k++) {
    out[k] = fire_at(p, q1 + p.s.i[0], q2 - p.s.i[1]);
    q1 += 7864; r1 += 32;            // 65536 * 12 = 786432 = 7864 * 100 + 32
    if (r1 >= 100) { r1 -= 100; q1++; }
    q2 += 26214; r2 += 40;           // 65536 * 40 = 2621440 = 26214 * 100 + 40
    if (r2 >= 100) { r2 -= 100; q2++; }
  }
}

// --- Meteor ---
static const nl_param_def METEOR_PARAMS[] = {
  {"speed", "Speed", 1, 100, 1, "px/s", 20},
  {"length", "Length", 1, 200, 1, "px", 50},
  {"tail", "Tail fade", 0, 5, 0.5f, "", 2},
};

static void meteor_prepare(Program &p) {
  int64_t span = (int64_t)(p.n + bpi(p, 1)) * Q16;
  int64_t tr = scale_q16(p.t_ms, bpi(p, 0), 1000);
  int64_t cy = floor_div(tr, span);
  p.s.i[0] = tr - cy * span;
  p.s.i[1] = cy;
}

static Col meteor_sample(const Program &p, int64_t xq) {
  float len = (float)bpi(p, 1);
  float d = (float)(p.s.i[0] - xq) * (1.0f / 65536.0f);
  if (d < 0.0f || d > len) return BLACK;
  Col c = mix(pal(p, p.s.i[1]), pal(p, p.s.i[1] + 1), d / len);
  return scale(c, powf_(1.0f - d / len, bp(p, 2)));
}

// --- Twinkle ---
static const nl_param_def TWINKLE_PARAMS[] = {
  {"density", "Density", 1, 100, 1, "%", 40},
  {"speed", "Pace", 1, 100, 1, "", 50},
};

static void twinkle_prepare(Program &p) {
  Phase ph = phase(p.t_ms, 12 + bpi(p, 1), 120);   // t * (0.1 + speed / 120)
  p.s.i[0] = ph.k;
  p.s.f[0] = ph.f;
}

static Col twinkle_sample(const Program &p, int64_t xq) {
  int i = pixel_of(xq);
  float ph = hb(i, 1) + p.s.f[0];
  int64_t cy = p.s.i[0] + (ph >= 1.0f ? 1 : 0);
  float f = ph >= 1.0f ? ph - 1.0f : ph;
  bool lit = hb2(i, cy, 2) < bp(p, 0) / 100.0f;
  float k = lit ? sin_pi(f) : 0.0f;
  int idx = ftoi_floor(hb2(i, cy, 3) * (float)p.n_colors);
  return scale(p.colors[idx], 0.05f + 0.95f * k * k);
}

// --- Confetti ---
static const nl_param_def CONFETTI_PARAMS[] = {
  {"density", "Density", 1, 100, 1, "%", 40},
  {"speed", "Pace", 1, 100, 1, "", 50},
};

static Col confetti_sample(const Program &p, int64_t xq) {
  int i = pixel_of(xq);
  // t * (0.2 + speed / 50) * (0.5 + hsh), with the per-pixel factor in thousandths
  int64_t mi = 500 + ftoi_floor(hb(i, 4) * 1000.0f);
  int64_t a = p.t_ms * (10 + bpi(p, 1)) * mi, d = 50LL * 1000 * 1000;
  int64_t k = floor_div(a, d);
  float ph = (float)(a - k * d) / (float)d + hb(i, 5);
  int64_t cy = k + (ph >= 1.0f ? 1 : 0);
  float f = ph >= 1.0f ? ph - 1.0f : ph;
  bool lit = hb2(i, cy, 6) < bp(p, 0) / 100.0f;
  int idx = ftoi_floor(hb2(i, cy, 7) * (float)p.n_colors);
  return scale(p.colors[idx], lit ? (1.0f - f) * (1.0f - f) : 0.0f);
}

// --- Breathe ---
static const nl_param_def BREATHE_PARAMS[] = {
  {"period", "Period", 1, 20, 1, "s", 4},
};

static void breathe_prepare(Program &p) {
  int64_t period = (int64_t)bpi(p, 0) * 1000;
  p.s.i[0] = floor_div(p.t_ms, period);
  p.s.f[0] = 0.06f + 0.94f * (0.5f - 0.5f * cos_turns(turns_q32(p.t_ms, 1, period)));
}

static Col breathe_sample(const Program &p, int64_t) {
  return scale(pal(p, p.s.i[0]), p.s.f[0]);
}

// --- Explosion ---
static const nl_param_def EXPLOSION_PARAMS[] = {
  {"interval", "Interval", 1, 10, 1, "s", 3},
};

static void explosion_prepare(Program &p) {
  Phase ph = phase(p.t_ms, 1, bpi(p, 0));
  p.s.i[0] = ph.k;
  p.s.i[1] = q16((0.2f + 0.6f * hb(ph.k, 8)) * (float)p.n);
  p.s.f[0] = ph.f;
}

static Col explosion_sample(const Program &p, int64_t xq) {
  int64_t dq = xq - p.s.i[1];
  float d = (float)(dq < 0 ? -dq : dq) * (1.0f / 65536.0f);
  float f = p.s.f[0], r = f * (float)p.n * 0.5f;
  float k = expf_(-(d - r) * (d - r) / 6.0f) * (1.0f - f) + (d < r ? 0.12f * (1.0f - f) : 0.0f);
  return scale(pal(p, p.s.i[0]), minf(1.0f, k));
}

// --- Strobe ---
static const nl_param_def STROBE_PARAMS[] = {
  {"rate", "Rate", 1, 20, 1, "Hz", 8},
};

static void strobe_prepare(Program &p) {
  Phase ph = phase(p.t_ms, bpi(p, 0), 1);
  p.s.i[0] = ph.k;
  p.s.f[0] = ph.f < 0.25f ? 1.0f : 0.0f;
}

static Col strobe_sample(const Program &p, int64_t) {
  return scale(pal(p, p.s.i[0]), p.s.f[0]);
}

// ============================================================ Scenic

// --- Candles ---
static const nl_param_def CANDLES_PARAMS[] = {
  {"spacing", "Spacing", 1, 10, 1, "px", 1},
  {"flicker", "Flicker", 0, 100, 1, "%", 55},
  {"wind", "Drafts", 0, 20, 1, "/min", 6},
  {"glow", "Glare", 0, 100, 1, "%", 35},
};

static void candles_frame(Program &p, Col *a) {
  const int n = p.n, sp = bpi(p, 0);
  const float fl = bp(p, 1) / 100.0f;
  const Col warm = col(255, 138, 42);
  const Col *C = p.raw_colors ? p.colors : &warm;
  const int CL = p.raw_colors ? p.raw_colors : 1;
  const int64_t T = p.t_ms;
  Window gw = window(T, 3000);
  float gl = gw.lt, gust = 0.0f;
  if (hb(gw.w, 10) < bp(p, 2) * 3.0f / 60.0f && gl < 1.4f)
    gust = sin_pi(gl / 1.4f) * (0.6f + 0.4f * hb(gw.w, 11));
  bool dir = hb(gw.w, 12) < 0.5f;
  float gx = dir ? (gl / 1.4f) * n * 1.4f - n * 0.2f : n * 1.2f - (gl / 1.4f) * n * 1.4f;
  int64_t t1 = scale_q16(T, 19, 10000), t2 = scale_q16(T, 67, 10000), t3 = scale_q16(T, 16, 1000);
  uint32_t ph23 = rad_q32(T, 23, 1);
  float gh = bp(p, 3) / 100.0f * 0.3f;
  for (int i = 0; i < n; i += sp) {
    const Col &c = C[(i / sp) % CL];
    float sd = (float)i * 3.7f + hb(i, 13) * 50.0f, size = 0.78f + 0.22f * hb(i, 14);
    float k = 1.0f - fl * (0.32f * value_noise(t1 + q16(sd)) + 0.22f * value_noise(t2 + q16(sd * 1.7f)) +
                           0.14f * value_noise(t3 + q16(sd * 0.3f)));
    if (gust > 0.0f) {
      float e = ((float)i - gx) / ((float)n * 0.25f);
      k *= 1.0f - gust * expf_(-e * e) * 0.75f * (0.55f + 0.45f * sin_turns(ph23 + rad_to_q32(sd)));
    }
    k = maxf(0.04f, k * size);
    Col cc = col(c.r * k, c.g * powf_(k, 1.7f), c.b * powf_(k, 2.4f));
    put(a, n, (float)i, cc);
    if (gh > 0.0f && sp > 1)
      for (int d = 1; d <= 3 && d <= sp - 1; d++) {
        put(a, n, (float)(i - d), cc, gh / (float)(d * d));
        put(a, n, (float)(i + d), cc, gh / (float)(d * d));
      }
  }
}

// --- Lava lamp ---
static const nl_param_def LAVA_PARAMS[] = {
  {"blobs", "Blobs", 1, 8, 1, "", 4},
  {"size", "Blob size", 2, 40, 1, "px", 8},
  {"speed", "Pace", 1, 100, 1, "", 30},
};

static void lava_frame(Program &p, Col *a) {
  const int n = p.n, nb = bpi(p, 0);
  const int64_t T = p.t_ms, pace = bpi(p, 2);
  Col bg = scale(pal(p, 1), 0.18f), fg = pal(p, 0);
  float bx[8], br[8];
  for (int j = 0; j < nb; j++) {
    int64_t rm = 300 + ftoi_floor(hb(j, 30) * 500.0f);   // 0.3 + 0.5 * hsh, thousandths
    bx[j] = n * (0.5f + 0.45f * sin_turns(rad_q32(T, pace * rm, 100000) + rad_to_q32(j * 2.1f)));
    br[j] = bp(p, 1) * (0.7f + 0.6f * hb(j, 31)) *
            (1.0f + 0.3f * sin_turns(rad_q32(T, pace, 100) + rad_to_q32((float)j)));
  }
  for (int i = 0; i < n; i++) {
    float f = 0.0f;
    for (int j = 0; j < nb; j++) {
      float d = (float)i - bx[j];
      f += br[j] * br[j] / (d * d + 1.0f);
    }
    a[i] = mix(bg, fg, clampf((f - 0.6f) / 0.8f, 0.0f, 1.0f));
  }
}

// --- Aurora ---
static const nl_param_def AURORA_PARAMS[] = {
  {"speed", "Pace", 1, 100, 1, "", 30},
};

static void aurora_frame(Program &p, Col *a) {
  Col C[NL_MAX_COLORS];
  int nc;
  if (p.raw_colors > 1) {
    nc = p.raw_colors;
    for (int j = 0; j < nc; j++) C[j] = p.colors[j];
  } else if (p.raw_colors == 1) {
    nc = 2; C[0] = p.colors[0]; C[1] = scale(p.colors[0], 0.5f);
  } else {
    nc = 3; C[0] = col(0, 255, 120); C[1] = col(0, 180, 255); C[2] = col(160, 0, 255);
  }
  const int64_t T = p.t_ms, pace = bpi(p, 0);
  int64_t t1 = scale_q16(T, pace * 5, 1000000);   // t * s * 0.5
  int64_t t2 = scale_q16(T, pace * 8, 1000000);   // t * s * 0.8
  int64_t t3 = scale_q16(T, pace * 2, 1000000);   // t * s * 0.2
  for (int i = 0; i < p.n; i++) {
    int64_t iq = (int64_t)i * Q16;
    float b = minf(1.0f, powf_(value_noise(iq * 6 / 100 + t1) *
                               value_noise(iq * 17 / 100 - t2 + 50 * Q16), 1.2f) * 1.8f);
    a[i] = scale(ramp(C, nc, value_noise(iq * 3 / 100 + t3 + 100 * Q16)), b);
  }
}

// ============================================================ Games

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
  uint8_t stack[1];       // n + 20 entries: color index << 1 | dim
};

static int tetris_lo(const Program &p) {
  int lo = roundi(minf(bp(p, 1), bp(p, 2)));
  return lo < 1 ? 1 : lo;
}
static int tetris_hi(const Program &p) {
  int hi = roundi(maxf(bp(p, 1), bp(p, 2))), lo = tetris_lo(p);
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

static void tetris_frame(Program &p, Col *a) {
  const TetrisState &s = *(const TetrisState *)p.state;
  int n = p.n;
  if (s.clear_ms > 0) {
    bool on = ((s.clear_ms * 8) / 1000) % 2 == 0;
    for (int i = 0; i < s.stack_len; i++)
      put(a, n, (float)i, on ? WHITE : stack_color(p, s.stack[i]));
    return;
  }
  for (int i = 0; i < s.stack_len; i++) put(a, n, (float)i, stack_color(p, s.stack[i]));
  if (s.has_blk) {
    Col c = p.colors[s.blk_c];
    for (int j = 0; j < s.blk_len; j++)
      put(a, n, (float)(s.blk_pos + j), (j == 0 && s.blk_len > 1) ? scale(c, 0.55f) : c);
  }
}

// --- Snake ---
static const nl_param_def SNAKE_PARAMS[] = {
  {"speed", "Speed", 5, 150, 1, "px/s", 40},
};

static const int SNAKE_HIST = 600;

struct SnakeState {
  int32_t acc, len, food, dead_ms;
  int32_t start, count;   // ring buffer of head positions, oldest first
  int16_t hist[SNAKE_HIST];
};

static uint32_t snake_bytes(const Program &) { return sizeof(SnakeState); }

static int snake_at(const SnakeState &s, int j) { return s.hist[(s.start + j) % SNAKE_HIST]; }

static void snake_push(SnakeState &s, int v) {
  if (s.count == SNAKE_HIST) {
    s.start = (s.start + 1) % SNAKE_HIST;
    s.count--;
  }
  s.hist[(s.start + s.count) % SNAKE_HIST] = (int16_t)v;
  s.count++;
}

static void snake_init(Program &p) {
  SnakeState &s = *(SnakeState *)p.state;
  s.start = 0; s.count = 0;
  snake_push(s, p.n >> 1);
  s.len = 3;
  s.food = (int32_t)p.rng.below(p.n);
}

static void snake_step(Program &p) {
  SnakeState &s = *(SnakeState *)p.state;
  if (s.dead_ms > 0) {
    s.dead_ms -= NL_STEP_MS;
    if (s.dead_ms <= 0) {
      int last = snake_at(s, s.count - 1);
      s.start = 0; s.count = 0;
      snake_push(s, last);
      s.len = 3;
    }
    return;
  }
  s.acc += bpi(p, 0) * NL_STEP_MS;
  for (int moves = 0; s.acc >= 1000 && moves < 60; moves++) {
    s.acc -= 1000;
    int h = snake_at(s, s.count - 1);
    int nh = h + (s.food > h ? 1 : s.food < h ? -1 : 1);
    snake_push(s, nh);
    if (nh == s.food) {
      int grow = (p.n + 12) / 24;              // Math.round(n / 24)
      s.len += grow > 2 ? grow : 2;
      s.food = (int32_t)p.rng.below(p.n);
      if (s.len * 100 > p.n * 55) s.dead_ms = 1200;
    }
  }
}

static void snake_frame(Program &p, Col *a) {
  const SnakeState &s = *(const SnakeState *)p.state;
  const int n = p.n;
  int bl = s.len < s.count ? s.len : s.count, first = s.count - bl;
  if (s.dead_ms > 0) {
    if (((s.dead_ms * 6) / 1000) % 2 == 0)
      for (int j = 0; j < bl; j++) {
        int i = snake_at(s, first + j);
        if (i >= 0 && i < n) a[i] = col(255, 40, 40);
      }
    return;
  }
  Col c0 = pal(p, 0), c1 = pal(p, 1);
  for (int j = 0; j < bl; j++) {
    int i = snake_at(s, first + j);
    if (i < 0 || i >= n) continue;
    float k = (float)(j + 1) / (float)bl;
    a[i] = scale(mix(c1, c0, k), 0.25f + 0.75f * k);
  }
  if (sin_turns(rad_q32(p.t_ms, 12, 1)) > -0.3f && s.food >= 0 && s.food < n) a[s.food] = pal(p, 2);
}

// --- Pong ---
static const nl_param_def PONG_PARAMS[] = {
  {"speed", "Ball speed", 10, 150, 1, "px/s", 70},
  {"paddle", "Paddle size", 1, 10, 1, "px", 3},
};

static void pong_frame(Program &p, Col *a) {
  const int n = p.n, ps = bpi(p, 1) < 1 ? 1 : bpi(p, 1);
  int span = n - 2 * ps;
  if (span < 1) span = 1;
  Phase ph = phase(p.t_ms, bpi(p, 0), span);   // one crossing takes span / speed s
  bool fwd = floor_mod(ph.k, 2) == 0, miss = hb(ph.k, 20) < 0.2f;
  for (int j = 0; j < ps; j++) {
    put(a, n, (float)j, pal(p, 0));
    put(a, n, (float)(n - 1 - j), pal(p, 1));
  }
  float x = (float)ps + ph.f * (float)span;
  if (!fwd) x = (float)(n - 1) - x;
  if (miss && ph.f > 0.9f) {
    float fl = (ph.f - 0.9f) / 0.1f;
    if (ftoi_floor(fl * 6.0f) % 2 == 0) {
      float lim = maxf(6.0f, (float)n * 0.15f);
      for (int j = 0; (float)j < lim; j++) put(a, n, (float)(fwd ? n - 1 - j : j), col(255, 0, 0), 0.9f);
    }
  } else {
    put(a, n, x, WHITE);
  }
}

// --- Chomp ---
static const nl_param_def CHOMP_PARAMS[] = {
  {"speed", "Speed", 5, 100, 1, "px/s", 30},
};

static void chomp_frame(Program &p, Col *a) {
  const int n = p.n;
  const float v = (float)bpi(p, 0);
  const int64_t lapq = (int64_t)(n + 40) * Q16;
  float x = (float)floor_mod(scale_q16(p.t_ms, bpi(p, 0), 1000), lapq) * (1.0f / 65536.0f) - 15.0f;
  int pel = (n * 6 + 5) / 10;                     // Math.round(n * 0.6)
  float fr = x > (float)pel ? (x - (float)pel) / v : -1.0f;
  bool scared = fr >= 0.0f && fr < 4.0f;
  bool blink = floor_mod(floor_div(p.t_ms * 8, 1000), 2) == 1;
  int from = -ftoi_floor(-(x + 1.0f));            // ceil(x + 1)
  for (int i = from < 0 ? 0 : from; i < n; i++)
    if (i % 3 == 0 && i != pel) put(a, n, (float)i, col(110, 95, 70));
  if ((float)pel > x) put(a, n, (float)pel, col(255, 200, 150), 0.5f + 0.5f * sin_turns(rad_q32(p.t_ms, 12, 1)));
  static const Col gc[4] = {{255, 0, 0}, {255, 120, 200}, {0, 230, 255}, {255, 160, 0}};
  for (int j = 0; j < 4; j++) {
    float home = (float)pel - 8.0f - (float)(j * 5);
    float gx = scared ? home - fr * v * 0.5f
                      : minf(x - 8.0f - (float)(j * 5), fr >= 4.0f ? home - 4.0f * v * 0.5f + (fr - 4.0f) * v : 1e9f);
    Col c = scared ? ((fr > 3.0f && blink) ? WHITE : col(30, 40, 255)) : gc[j];
    put(a, n, gx, c);
    put(a, n, gx - 1.0f, c, 0.6f);
  }
  Col y = col(255, 220, 0);
  put(a, n, x, y);
  put(a, n, x - 1.0f, y);
  if (blink) put(a, n, x + 1.0f, y);
}

// ============================================================ Nerdy

// --- Sorting ---
static const nl_param_def SORTING_PARAMS[] = {
  {"speed", "Steps", 1, 60, 1, "/s", 12},
};

enum { SORT_SHUFFLE, SORT_SORT, SORT_HOLD };

struct SortingState {
  int32_t acc, hold_ms, par, clean, phase;
  uint16_t arr[1];        // n entries
};

static uint32_t sorting_bytes(const Program &p) { return sizeof(SortingState) + 2u * p.n; }

static void sorting_init(Program &p) {
  SortingState &s = *(SortingState *)p.state;
  for (int i = 0; i < p.n; i++) s.arr[i] = (uint16_t)i;
  s.phase = SORT_SHUFFLE;
}

static void sorting_step(Program &p) {
  SortingState &s = *(SortingState *)p.state;
  const int n = p.n;
  if (s.phase == SORT_HOLD) {
    s.hold_ms -= NL_STEP_MS;
    if (s.hold_ms <= 0) s.phase = SORT_SHUFFLE;
  }
  if (s.phase == SORT_SHUFFLE) {
    for (int i = n - 1; i > 0; i--) {
      int j = (int)p.rng.below((uint32_t)(i + 1));
      uint16_t t = s.arr[i]; s.arr[i] = s.arr[j]; s.arr[j] = t;
    }
    s.phase = SORT_SORT;
    s.hold_ms = 800;
  }
  if (s.phase != SORT_SORT) return;
  if (s.hold_ms > 0) {
    s.hold_ms -= NL_STEP_MS;
    return;
  }
  s.acc += bpi(p, 0) * NL_STEP_MS;
  for (int moves = 0; s.acc >= 1000 && moves < 60; moves++) {
    s.acc -= 1000;
    if (s.phase != SORT_SORT) continue;
    bool swapped = false;
    for (int i = s.par; i < n - 1; i += 2)
      if (s.arr[i] > s.arr[i + 1]) {
        uint16_t t = s.arr[i]; s.arr[i] = s.arr[i + 1]; s.arr[i + 1] = t;
        swapped = true;
      }
    s.par = 1 - s.par;
    s.clean = swapped ? 0 : s.clean + 1;
    if (s.clean >= 2) {
      s.phase = SORT_HOLD;
      s.hold_ms = 2500;
      s.clean = 0;
    }
  }
}

static void sorting_frame(Program &p, Col *a) {
  const SortingState &s = *(const SortingState *)p.state;
  for (int i = 0; i < p.n; i++) a[i] = hsv((float)s.arr[i] / (float)p.n * 0.85f);
}

// --- Automaton ---
static const nl_param_def AUTOMATON_PARAMS[] = {
  {"rule", "Rule", 0, 255, 1, "", 30},
  {"speed", "Generations", 1, 30, 1, "/s", 8},
};

struct AutomatonState {
  int32_t acc, gen;
  uint8_t cells[1];       // row[n], age[n], next[n]
};

static uint32_t automaton_bytes(const Program &p) { return sizeof(AutomatonState) + 3u * p.n; }

static void automaton_init(Program &p) {
  AutomatonState &s = *(AutomatonState *)p.state;
  s.cells[p.n >> 1] = 1;
}

static void automaton_step(Program &p) {
  AutomatonState &s = *(AutomatonState *)p.state;
  const int n = p.n, rule = bpi(p, 0);
  uint8_t *row = s.cells, *age = s.cells + n, *nr = s.cells + 2 * n;
  s.acc += bpi(p, 1) * NL_STEP_MS;
  for (int moves = 0; s.acc >= 1000 && moves < 60; moves++) {
    s.acc -= 1000;
    int alive = 0;
    for (int i = 0; i < n; i++) {
      int l = row[(i - 1 + n) % n], c = row[i], r = row[(i + 1) % n];
      nr[i] = (uint8_t)((rule >> (l << 2 | c << 1 | r)) & 1);
      alive += nr[i];
    }
    for (int i = 0; i < n; i++) {
      age[i] = nr[i] ? (row[i] ? (uint8_t)(age[i] < 250 ? age[i] + 1 : 250) : 0) : 0;
      row[i] = nr[i];
    }
    s.gen++;
    if (alive == 0 || alive == n || s.gen > 240) {
      for (int i = 0; i < n; i++) row[i] = (uint8_t)(p.rng.next() >> 31);
      s.gen = 0;
    }
  }
}

static void automaton_frame(Program &p, Col *a) {
  const AutomatonState &s = *(const AutomatonState *)p.state;
  const uint8_t *row = s.cells, *age = s.cells + p.n;
  Col c0 = pal(p, 0), c1 = pal(p, 1);
  for (int i = 0; i < p.n; i++)
    a[i] = row[i] ? mix(c1, c0, minf(1.0f, (float)age[i] / 4.0f)) : BLACK;
}

// --- Hourglass ---
static const nl_param_def HOURGLASS_PARAMS[] = {
  {"time", "Duration", 5, 120, 1, "s", 20},
};

static void hourglass_frame(Program &p, Col *a) {
  const int n = p.n, half = n >> 1;
  Phase ph = phase(p.t_ms, 1, bpi(p, 0));
  Col sand = pal(p, 0);
  int fallen = ftoi_floor(ph.f * (float)half), top = half - fallen;
  for (int i = 0; i < fallen; i++) a[i] = sand;
  for (int i = 0; i < top; i++) a[half + i] = sand;
  put(a, n, (float)half, col(40, 44, 52), 0.6f);
  if (top > 0) {
    int64_t mq = (int64_t)(half - fallen) * Q16;
    int64_t tq = scale_q16(p.t_ms, (int64_t)n * 6, 10000);   // t * n * 0.6
    for (int j = 0; j < 3; j++) {
      int64_t off = (int64_t)j * (half - fallen) * Q16 / 3;
      float y = (float)half - (float)floor_mod(tq + off, mq) * (1.0f / 65536.0f);
      put(a, n, y, sand, 0.8f);
    }
  }
  if (floor_mod(ph.k, 2) == 1)
    for (int i = 0, j = n - 1; i < j; i++, j--) {
      Col t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

// --- Binary clock ---
static void binary_clock_frame(Program &p, Col *a) {
  const int n = p.n, gap = 3;
  int64_t sod = floor_mod(floor_div(p.t_ms, 1000) + p.utc_offset_s, 86400);
  int v[3] = {(int)(sod / 3600), (int)(sod / 60 % 60), (int)(sod % 60)};
  int bw = (int)floor_div(n - gap * 2, 18);
  if (bw < 1) bw = 1;
  int x = (int)floor_div(n - (bw * 18 + gap * 2), 2);
  for (int g = 0; g < 3; g++) {
    Col c = pal(p, g);
    for (int b = 5; b >= 0; b--) {
      bool on = (v[g] >> b) & 1;
      for (int j = 0; j < bw - (bw > 2 ? 1 : 0); j++)
        if (x + j >= 0 && x + j < n) a[x + j] = on ? c : scale(c, 0.08f);
      x += bw;
    }
    x += gap;
  }
}

// ============================================================ table

#define STATELESS(sample, row, prepare) prepare, sample, row, nullptr, nullptr, nullptr, nullptr
#define FRAME(frame) nullptr, nullptr, nullptr, frame, nullptr, nullptr, nullptr
#define STATEFUL(frame, bytes, init, step) nullptr, nullptr, nullptr, frame, bytes, init, step

const BaseDef BASES[NL_BASE_COUNT] = {
  {"Normal", "Classic",
   "Repeating bands of your colors - space and blend them. Add the Motion effect to move them.",
   0, NORMAL_PARAMS, COUNT(NORMAL_PARAMS), STATELESS(normal_sample, normal_row, nullptr)},
  {"Fire", "Classic", "Licking flames built from your colors.",
   0, FIRE_PARAMS, COUNT(FIRE_PARAMS), STATELESS(fire_sample, fire_row, fire_prepare)},
  {"Tetris", "Games",
   "Pixel 0 is the bottom of the well. Blocks drop in from the far end and stack up; "
   "when the stack reaches the top it flashes and clears.",
   F_STATEFUL, TETRIS_PARAMS, COUNT(TETRIS_PARAMS),
   STATEFUL(tetris_frame, tetris_bytes, tetris_init, tetris_step)},
  {"Rainbow", "Classic", "The full spectrum. Add Motion to scroll it.",
   F_NO_COLORS, RAINBOW_PARAMS, COUNT(RAINBOW_PARAMS), STATELESS(rainbow_sample, rainbow_row, nullptr)},
  {"Meteor", "Classic", "A streak with a fading tail.",
   0, METEOR_PARAMS, COUNT(METEOR_PARAMS), STATELESS(meteor_sample, nullptr, meteor_prepare)},
  {"Twinkle", "Classic", "Lights gently fade in and out.",
   0, TWINKLE_PARAMS, COUNT(TWINKLE_PARAMS), STATELESS(twinkle_sample, nullptr, twinkle_prepare)},
  {"Confetti", "Classic", "Quick random pops of color.",
   0, CONFETTI_PARAMS, COUNT(CONFETTI_PARAMS), STATELESS(confetti_sample, nullptr, nullptr)},
  {"Breathe", "Classic", "Whole zone fades through your colors.",
   0, BREATHE_PARAMS, COUNT(BREATHE_PARAMS), STATELESS(breathe_sample, nullptr, breathe_prepare)},
  {"Explosion", "Classic", "Bursts expand from random points.",
   0, EXPLOSION_PARAMS, COUNT(EXPLOSION_PARAMS), STATELESS(explosion_sample, nullptr, explosion_prepare)},
  {"Strobe", "Classic",
   "Hard flashes, cycling colors. Fast flashing can trigger seizures in people with photosensitive epilepsy.",
   F_WARN, STROBE_PARAMS, COUNT(STROBE_PARAMS), STATELESS(strobe_sample, nullptr, strobe_prepare)},
  {"Candles", "Scenic",
   "Every pixel (or every few) is its own candle: independent flicker, warmer when it dips, a soft "
   "glare around each flame, and the odd draft rolling through.",
   0, CANDLES_PARAMS, COUNT(CANDLES_PARAMS), FRAME(candles_frame)},
  {"Snake", "Games",
   "The snake slides back and forth hunting the blinking food pixel and grows with every bite. "
   "When it gets too long it flashes and starts over.",
   F_STATEFUL, SNAKE_PARAMS, COUNT(SNAKE_PARAMS),
   STATEFUL(snake_frame, snake_bytes, snake_init, snake_step)},
  {"Pong", "Games",
   "A paddle guards each end of the strip and they rally a ball between them. Every so often one "
   "misses and its end flashes red.",
   0, PONG_PARAMS, COUNT(PONG_PARAMS), FRAME(pong_frame)},
  {"Chomp", "Games",
   "A dot-eater munches down the strip with four ghosts on its tail. It grabs a power pellet past "
   "halfway and the ghosts turn blue and flee.",
   F_NO_COLORS, CHOMP_PARAMS, COUNT(CHOMP_PARAMS), FRAME(chomp_frame)},
  {"Sorting", "Nerdy",
   "A rainbow gets scrambled, then sorted back into order live with an odd-even sort, one round of "
   "swaps per step. Holds, then reshuffles.",
   F_STATEFUL | F_NO_COLORS, SORTING_PARAMS, COUNT(SORTING_PARAMS),
   STATEFUL(sorting_frame, sorting_bytes, sorting_init, sorting_step)},
  {"Automaton", "Nerdy",
   "Each pixel lives or dies based on itself and its two neighbors (an elementary cellular "
   "automaton). Try rule 30 for chaos, 90 for fractals, 110 for the Turing-complete one.",
   F_STATEFUL, AUTOMATON_PARAMS, COUNT(AUTOMATON_PARAMS),
   STATEFUL(automaton_frame, automaton_bytes, automaton_init, automaton_step)},
  {"Hourglass", "Nerdy",
   "Pixel 0 is the bottom. The upper half starts full of sand, grains trickle through the middle "
   "and pile up below, then it flips.",
   0, HOURGLASS_PARAMS, COUNT(HOURGLASS_PARAMS), FRAME(hourglass_frame)},
  {"Binary clock", "Nerdy",
   "The real time in binary: hours, minutes and seconds as three 6-bit groups, most significant "
   "bit first. A lit block is a 1.",
   0, nullptr, 0, FRAME(binary_clock_frame)},
  {"Lava lamp", "Scenic",
   "Soft blobs of your first color drift up and down through your second, merging and splitting.",
   0, LAVA_PARAMS, COUNT(LAVA_PARAMS), FRAME(lava_frame)},
  {"Aurora", "Scenic", "Slow, shifting curtains of color, like the northern lights.",
   0, AURORA_PARAMS, COUNT(AURORA_PARAMS), FRAME(aurora_frame)},
};

}  // namespace nl

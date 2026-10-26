// Effects, ported from pattern-engine.reference.js (redesign handoff).
// Pixel-moving effects (Motion, Mirror, Earthquake) are applied in engine.cpp's
// sample-position step; their prepare() leaves the shift in s.i[0].
// Each instance hashes with its own seed (type + instance number) and runs at
// t + an offset from that seed, so copies of an effect do not run in step.
#include <stdlib.h>
#include "nl_internal.h"

namespace nl {

static inline float hf(const FxRun &f, int64_t a, uint32_t salt) { return hh(f.seed, a, salt); }
static inline float hf2(const FxRun &f, int64_t a, int64_t b, uint32_t salt) {
  return unit(hash32(hash32(f.seed, (uint32_t)a, salt), (uint32_t)b));
}
static inline int64_t q16(float x) { return (int64_t)(x * 65536.0f); }
static const Col BLACK = {0, 0, 0};
static const Col WHITE = {255, 255, 255};

#define COUNT(a) (uint8_t)(sizeof(a) / sizeof(a[0]))

// ============================================================ Movement

static const nl_param_def MOTION_PARAMS[] = {
  {"speed", "Speed", -60, 60, 1, "px/s", 10},
  {"swing", "Swing", 0, 120, 1, "px", 0},
  {"smooth", "Anti-aliasing", 0, 100, 1, "%", 100},
};

static const uint64_t INV_PI_Q32 = 1367130551u;  // 2^32 / pi

// x -= swing > 0 ? w * sin(t * speed * 2 / w) : t * speed
static void motion_prepare(const Program &p, FxRun &f) {
  int64_t speed = fpi(f, 0), w = fpi(f, 1), shift;
  if (w <= 0) {
    shift = scale_q16(p.t_ms, speed, 1000);
  } else {
    // phase in turns is t_ms * speed / (1000 * w * pi)
    int64_t a = p.t_ms * speed, d = 1000 * w;
    int64_t q = floor_div(a, d), r = a - q * d;
    uint32_t ph = (uint32_t)((uint64_t)q * INV_PI_Q32 + ((uint64_t)r * INV_PI_Q32) / (uint64_t)d);
    shift = (int64_t)((float)w * sin_turns(ph) * 65536.0f);
  }
  f.s.i[0] = -shift;
  f.s.f[0] = fp(f, 2) / 100.0f;
}

static void earthquake_prepare(const Program &p, FxRun &f) {
  Window wd = window(p.t_ms, 1600);
  f.s.i[0] = 0;
  if (hf(f, wd.w, 1) < fp(f, 0) * 1.6f / 60.0f && wd.lt < 0.8f) {
    float k = 1.0f - wd.lt / 0.8f;
    float jitter = hf(f, floor_div(p.t_ms * 30, 1000), 2) - 0.5f;
    f.s.i[0] = q16(fp(f, 1) * (sinf_(wd.lt * 48.0f) * k + jitter * k * 0.8f));
  }
}

static const nl_param_def EARTHQUAKE_PARAMS[] = {
  {"freq", "Quakes", 1, 30, 1, "/min", 6},
  {"mag", "Magnitude", 1, 30, 1, "px", 8},
};

// ============================================================ Atmosphere

// --- Shimmer ---
static const nl_param_def SHIMMER_PARAMS[] = {
  {"amount", "Amount", 0, 100, 1, "%", 40},
  {"speed", "Pace", 1, 100, 1, "", 50},
};

static void shimmer_prepare(const Program &, FxRun &f) {
  f.s.i[0] = rad_q32(f.t_ms, fpi(f, 1), 4);   // t * s * 3, s = speed / 12
  f.s.i[1] = rad_q32(f.t_ms, fpi(f, 1), 6);   // t * s * 2
  f.s.f[0] = fp(f, 0) / 100.0f;
}

static void shimmer_apply(const Program &, const FxRun &f, int i, Col &v) {
  uint32_t a = (uint32_t)f.s.i[0] + rad_to_q32((float)i * 1.9f + hf(f, i, 3) * 6.0f);
  uint32_t b = rad_to_q32((float)i * 0.7f) - (uint32_t)f.s.i[1];
  float x = 0.5f + 0.5f * sin_turns(a) * sin_turns(b);
  v = scale(v, 1.0f - f.s.f[0] * x * x);
}

// --- Sparkle ---
static const nl_param_def SPARKLE_PARAMS[] = {
  {"density", "Density", 1, 100, 1, "%", 30},
};

static void sparkle_prepare(const Program &, FxRun &f) {
  Phase ph = phase(f.t_ms, 16, 10);   // t * 1.6
  f.s.i[0] = ph.k;
  f.s.f[0] = ph.f;
}

static void sparkle_apply(const Program &, const FxRun &f, int i, Col &v) {
  float ph = hf(f, i, 4) + f.s.f[0];
  int64_t cy = f.s.i[0] + (ph >= 1.0f ? 1 : 0);
  float fr = ph >= 1.0f ? ph - 1.0f : ph;
  if (hf2(f, i, cy, 5) < fp(f, 0) / 300.0f) {
    float k = 1.0f - fr;
    v = mix(v, f.colors[0], k * k * k);
  }
}

// --- Candle ---
static const nl_param_def CANDLE_PARAMS[] = {
  {"amount", "Amount", 0, 100, 1, "%", 45},
};

static void candle_prepare(const Program &, FxRun &f) {
  f.s.i[0] = scale_q16(f.t_ms, 7, 1000);
  f.s.i[1] = scale_q16(f.t_ms, 19, 1000);
  f.s.f[0] = fp(f, 0) / 100.0f;
}

static void candle_apply(const Program &, const FxRun &f, int i, Col &v) {
  float a = f.s.f[0];
  int64_t iq = (int64_t)i * Q16;
  float k = 1.0f - a * (0.6f * value_noise(f.s.i[0] + iq * 35 / 100) +
                        0.4f * value_noise(f.s.i[1] + iq * 13 / 10));
  float m = maxc(v) / 255.0f;
  v = scale(mix(v, scale(f.colors[0], m), a * 0.3f), k);
}

// --- Pulse ---
static const nl_param_def PULSE_PARAMS[] = {
  {"depth", "Depth", 0, 100, 1, "%", 50},
  {"period", "Period", 1, 20, 0.5f, "s", 4},
};

static void pulse_prepare(const Program &, FxRun &f) {
  int64_t period = roundi(fp(f, 1) * 1000.0f);
  f.s.f[0] = 1.0f - fp(f, 0) / 100.0f * (0.5f - 0.5f * cos_turns(turns_q32(f.t_ms, 1, period)));
}

static void scale_apply(const Program &, const FxRun &f, int, Col &v) { v = scale(v, f.s.f[0]); }

// --- Heartbeat ---
static const nl_param_def HEARTBEAT_PARAMS[] = {
  {"bpm", "Rate", 40, 160, 1, "bpm", 70},
  {"depth", "Depth", 0, 100, 1, "%", 70},
};

static float bump(float ph, float c, float w) {
  float d = (ph - c) / w;
  return expf_(-d * d);
}

static void heartbeat_prepare(const Program &, FxRun &f) {
  float ph = (float)turns_q32(f.t_ms, fpi(f, 0), 60000) * (1.0f / 4294967296.0f);
  float beat = minf(1.0f, bump(ph, 0.05f, 0.05f) + 0.75f * bump(ph, 0.25f, 0.06f));
  f.s.f[0] = 1.0f - fp(f, 1) / 100.0f * (1.0f - beat);
}

// --- Wave ---
static const nl_param_def WAVE_PARAMS[] = {
  {"width", "Width", 4, 120, 1, "px", 30},
  {"speed", "Speed", -60, 60, 1, "px/s", 25},
};

static void wave_prepare(const Program &p, FxRun &f) {
  int64_t w = fpi(f, 0);
  int64_t pos = floor_mod(scale_q16(f.t_ms, fpi(f, 1), 1000), (p.n + 2 * w) * Q16) - w * Q16;
  f.s.f[0] = (float)pos * (1.0f / 65536.0f);
  f.s.f[1] = (float)w / 2.0f;
}

static void wave_apply(const Program &, const FxRun &f, int i, Col &v) {
  float d = ((float)i - f.s.f[0]) / f.s.f[1];
  v = scale(v, 0.2f + 0.8f * expf_(-d * d));
}

// --- Rainbow sweep ---
static const nl_param_def RAINBOW_SWEEP_PARAMS[] = {
  {"freq", "Sweeps", 1, 12, 1, "/min", 4},
};

static void rainbow_sweep_prepare(const Program &p, FxRun &f) {
  Window wd = window(f.t_ms, 5000);
  f.s.i[0] = hf(f, wd.w, 6) < fp(f, 0) * 5.0f / 60.0f;
  f.s.f[0] = wd.lt * (float)p.n * 0.6f;
  f.s.f[1] = (float)p.n * 0.35f;
}

static void rainbow_sweep_apply(const Program &, const FxRun &f, int i, Col &v) {
  if (!f.s.i[0]) return;
  float d = f.s.f[0] - (float)i, bw = f.s.f[1];
  if (d >= 0.0f && d < bw) v = mix(v, hsv(d / bw), sin_pi(d / bw) * 0.9f);
}

// ============================================================ Scenes (frame effects)

// --- Fireflies ---
static const nl_param_def FIREFLIES_PARAMS[] = {
  {"count", "Fireflies", 1, 30, 1, "", 10},
};

static void fireflies_frame(const Program &p, FxRun &f, Col *a) {
  const int n = p.n;
  Col c = f.n_colors ? fpal(f, 0) : col(200, 255, 60);
  int64_t tn = scale_q16(f.t_ms, 15, 100000);   // t * 0.15
  for (int j = 0; j < fpi(f, 0); j++) {
    float x = (float)n * value_noise(tn + (int64_t)j * 137 * Q16 / 10);
    int64_t period = 2000 + ftoi_floor(hf(f, j, 7) * 3000.0f);
    float ph = (float)floor_mod(f.t_ms, period) / (float)period + hf(f, j, 8);
    if (ph >= 1.0f) ph -= 1.0f;
    float k = ph < 0.25f ? sin_pi(ph / 0.25f) : 0.0f;
    put(a, n, x, c, k);
    put(a, n, x - 1.0f, c, k * 0.25f);
    put(a, n, x + 1.0f, c, k * 0.25f);
  }
}

// Events every 60 / rate s: the first event to draw, and seconds since event k.
static int64_t events_from(const FxRun &f, int64_t rate, int64_t back) {
  return floor_div(f.t_ms * rate, 60000) - back;
}
static float since_event(const FxRun &f, int64_t rate, int64_t k) {
  return (float)(f.t_ms * rate - k * 60000) / (float)(1000 * rate);
}

// --- Fireworks ---
static const nl_param_def FIREWORKS_PARAMS[] = {
  {"rate", "Shows", 5, 60, 1, "/min", 24},
};

static void fireworks_frame(const Program &p, FxRun &f, Col *a) {
  const int n = p.n;
  const int64_t rate = fpi(f, 0), K = floor_div(f.t_ms * rate, 60000);
  const float g = (float)n * 0.5f;
  uint32_t ph30 = rad_q32(f.t_ms, 30, 1);
  for (int64_t k = events_from(f, rate, (rate * 16 + 299) / 300); k <= K; k++) {
    float tt = since_event(f, rate, k);
    if (tt < 0.0f) continue;
    float h = (float)n * (0.45f + 0.4f * hf(f, k, 9));
    Col c = fpal(f, k);
    if (tt < 1.0f) {
      float y = h * (1.0f - (1.0f - tt) * (1.0f - tt));
      put(a, n, y, col(255, 200, 120));
      put(a, n, y - 1.0f, col(255, 120, 40), 0.4f);
      put(a, n, y - 2.0f, col(255, 80, 0), 0.15f);
      continue;
    }
    float bt = tt - 1.0f;
    if (bt > 2.2f) continue;
    float fd = 1.0f - bt / 2.2f;
    if (bt < 0.08f)
      for (int j = -3; j <= 3; j++) put(a, n, h + (float)j, WHITE);
    for (int j = 0; j < 16; j++) {
      float sv = (hf2(f, k, j, 10) - 0.5f) * (float)n * 0.7f;
      put(a, n, h + sv * bt - 0.5f * g * bt * bt, mix(c, WHITE, fd * fd * 0.5f),
          fd * fd * (0.6f + 0.4f * sin_turns(ph30 + rad_to_q32((float)j))));
    }
  }
}

// --- Rain & ripples ---
static const nl_param_def RAIN_PARAMS[] = {
  {"rate", "Drops", 5, 200, 1, "/min", 60},
};

static void rain_frame(const Program &p, FxRun &f, Col *a) {
  const int n = p.n;
  const int64_t rate = fpi(f, 0), K = floor_div(f.t_ms * rate, 60000);
  Col bg = scale(fpal(f, 1), 0.06f);
  for (int i = 0; i < n; i++) a[i] = bg;
  for (int64_t k = events_from(f, rate, (rate + 29) / 30); k <= K; k++) {
    float tt = since_event(f, rate, k);
    if (tt < 0.0f || tt > 2.0f) continue;
    float x = hf(f, k, 11) * (float)n, r = tt * (float)n * 0.12f, fd = 1.0f - tt / 2.0f;
    Col c = fpal(f, k);
    if (tt < 0.1f) put(a, n, x, WHITE);
    put(a, n, x - r, c, fd);
    put(a, n, x + r, c, fd);
    put(a, n, x - r * 0.6f, c, fd * 0.4f);
    put(a, n, x + r * 0.6f, c, fd * 0.4f);
  }
}

// --- Traffic ---
static const nl_param_def TRAFFIC_PARAMS[] = {
  {"cars", "Cars", 1, 20, 1, "", 8},
  {"speed", "Speed", 5, 120, 1, "px/s", 40},
};

static void traffic_frame(const Program &p, FxRun &f, Col *a) {
  const int n = p.n;
  for (int j = 0; j < fpi(f, 0); j++) {
    int dir = j % 2 ? 1 : -1;
    int64_t vm = 600 + ftoi_floor(hf(f, j, 12) * 600.0f);   // 0.6 + 0.6 * hsh, thousandths
    int64_t pos = scale_q16(f.t_ms, fpi(f, 1) * vm * dir, 1000000) + q16(hf(f, j, 13) * (float)n * 2.0f);
    float x = (float)floor_mod(pos, (int64_t)(n + 10) * Q16) * (1.0f / 65536.0f) - 5.0f;
    Col c = dir > 0 ? col(255, 20, 0) : col(255, 240, 210);
    put(a, n, x, c);
    put(a, n, x + 2.0f, c);
    if (dir < 0) {
      put(a, n, x - 1.0f, c, 0.25f);
      put(a, n, x - 2.0f, c, 0.1f);
    }
  }
}

// --- Data stream ---
static const nl_param_def DATA_STREAM_PARAMS[] = {
  {"packets", "Packets", 1, 16, 1, "", 8},
  {"speed", "Speed", 5, 150, 1, "px/s", 50},
};

static void data_stream_frame(const Program &p, FxRun &f, Col *a) {
  const int n = p.n;
  Col bg = scale(fpal(f, 0), 0.05f);
  for (int i = 0; i < n; i++) a[i] = bg;
  for (int j = 0; j < fpi(f, 0); j++) {
    int64_t vm = 500 + ftoi_floor(hf(f, j, 14) * 1000.0f);   // 0.5 + hsh, thousandths
    int len = 2 + ftoi_floor(hf(f, j, 15) * 6.0f);
    int64_t pos = scale_q16(f.t_ms, fpi(f, 1) * vm, 1000000) + q16(hf(f, j, 16) * (float)n * 3.0f);
    float x = (float)floor_mod(pos, (int64_t)(n + len * 2) * Q16) * (1.0f / 65536.0f) - (float)len;
    Col c = fpal(f, j);
    for (int q = 0; q < len; q++) put(a, n, x - (float)q, c, q == 0 ? 1.0f : 0.5f);
  }
}

// --- Bouncing balls ---
static const nl_param_def BALLS_PARAMS[] = {
  {"balls", "Balls", 1, 8, 1, "", 3},
  {"bounce", "Bounciness", 50, 95, 1, "%", 80},
};

struct Ball { float h, v; int c; };

static uint32_t balls_bytes(const Program &, const FxRun &f) { return sizeof(Ball) * fpi(f, 0); }

static void balls_init(Program &p, FxRun &f) {
  Ball *b = (Ball *)f.state;
  int N = fpi(f, 0);
  for (int j = 0; j < N; j++) {
    b[j].h = (float)(p.n - 1) * (1.0f - (float)j / ((float)N * 1.5f));
    b[j].v = 0.0f;
    b[j].c = j;
  }
}

static void balls_step(Program &p, FxRun &f) {
  Ball *b = (Ball *)f.state;
  const float g = (float)p.n * 1.4f, dt = NL_STEP_MS / 1000.0f;
  for (int j = 0; j < fpi(f, 0); j++) {
    b[j].v -= g * dt;
    b[j].h += b[j].v * dt;
    if (b[j].h <= 0.0f) {
      b[j].h = 0.0f;
      b[j].v = -b[j].v * fp(f, 1) / 100.0f;
      if (b[j].v < g * 0.1f)
        b[j].v = sqrtf_(2.0f * g * (float)(p.n - 1)) * (0.8f + 0.2f * f.rng.next01());
    }
  }
}

static void balls_frame(const Program &p, FxRun &f, Col *a) {
  const Ball *b = (const Ball *)f.state;
  for (int j = 0; j < fpi(f, 0); j++) {
    Col c = fpal(f, b[j].c);
    put(a, p.n, b[j].h, c);
    put(a, p.n, b[j].h + (b[j].v > 0.0f ? -1.0f : 1.0f), c, 0.3f);
  }
}

// --- Laser duel ---
static const nl_param_def LASER_PARAMS[] = {
  {"speed", "Bolt speed", 20, 200, 1, "px/s", 100},
  {"rate", "Volleys", 6, 120, 1, "/min", 40},
};

static void laser_frame(const Program &p, FxRun &f, Col *a) {
  const int n = p.n;
  const float v = (float)fpi(f, 0);
  const int64_t rate = fpi(f, 1), K = floor_div(f.t_ms * rate, 60000);
  const float P = 60.0f / (float)rate;
  Col cl = fpal(f, 0), cr = fpal(f, 1);
  for (int64_t k = events_from(f, rate, (rate + 19) / 20); k <= K; k++) {
    float tt = since_event(f, rate, k) - hf(f, k, 17) * P * 0.3f;
    float off = (hf(f, k, 18) - 0.5f) * 0.5f * (float)n / v;
    if (tt < 0.0f) continue;
    float tm = ((float)(n - 1) + off * v) / (2.0f * v);
    if (tt < tm) {
      for (int j = 0; j < 4; j++) put(a, n, tt * v - (float)j, cl, 1.0f - (float)j / 4.0f);
      if (tt >= off)
        for (int j = 0; j < 4; j++) put(a, n, (float)(n - 1) - (tt - off) * v + (float)j, cr, 1.0f - (float)j / 4.0f);
      continue;
    }
    float bt = tt - tm, mx = tm * v;
    if (bt >= 1.2f) continue;
    float fd = 1.0f - bt / 1.2f;
    put(a, n, mx, WHITE, fd);
    for (int j = 0; j < 12; j++) {
      float sv = (hf2(f, k, j, 19) - 0.5f) * v;
      put(a, n, mx + sv * bt, mix(cl, cr, hf2(f, j, k, 20)), fd * fd);
    }
  }
}

// ============================================================ Weather

// --- Lightning ---
static const nl_param_def LIGHTNING_PARAMS[] = {
  {"freq", "Strikes", 1, 27, 1, "/min", 10},
  {"power", "Intensity", 0, 100, 1, "%", 90},
};

static void lightning_prepare(const Program &p, FxRun &f) {
  Window wd = window(f.t_ms, 2200);
  float lt = wd.lt - hf(f, wd.w, 21) * 1.2f, k = 0.0f;
  if (hf(f, wd.w, 22) < fp(f, 0) * 2.2f / 60.0f && lt > 0.0f) {
    if (lt < 0.07f) k = 1.0f;
    else if (lt > 0.16f && lt < 0.22f) k = 0.7f;
    else if (lt > 0.3f && lt < 0.55f) k = 1.0f - (lt - 0.3f) / 0.25f;
  }
  f.s.f[0] = k * fp(f, 1) / 100.0f;
  f.s.f[1] = hf(f, wd.w, 23) * (float)p.n;
  f.s.f[2] = (float)p.n * 0.5f;
}

static void lightning_apply(const Program &, const FxRun &f, int i, Col &v) {
  if (!(f.s.f[0] > 0.0f)) return;
  float loc = expf_(-absf((float)i - f.s.f[1]) / f.s.f[2]);
  v = mix(v, f.colors[0], minf(1.0f, f.s.f[0] * loc));
}

// --- Snowfall ---
static const nl_param_def SNOWFALL_PARAMS[] = {
  {"density", "Density", 5, 100, 1, "%", 40},
  {"speed", "Pace", 1, 100, 1, "", 50},
};

static void snowfall_prepare(const Program &p, FxRun &f) {
  const int n = p.n;
  int pile = ftoi_floor((float)floor_mod(f.t_ms, 40000) / 40000.0f * (float)n * 0.15f);
  int flakes = roundi(fp(f, 0) / 5.0f);
  int64_t mq = (int64_t)(n - pile) * Q16;
  uint32_t ph2 = rad_q32(f.t_ms, 2, 1);
  for (int j = 0; j < flakes; j++) {
    int64_t sm = 4000 + ftoi_floor(hf(f, j, 24) * 6000.0f);   // 4 + 6 * hsh, thousandths
    int64_t y = floor_mod(scale_q16(f.t_ms, sm * fpi(f, 1), 50000000) + q16(hf(f, j, 25) * (float)n), mq);
    f.s.f[j] = (float)n - (float)y * (1.0f / 65536.0f) + sin_turns(ph2 + rad_to_q32((float)j)) * 0.8f;
  }
  f.s.i[0] = pile;
  f.s.i[1] = flakes;
}

static void snowfall_apply(const Program &, const FxRun &f, int i, Col &v) {
  if (i < f.s.i[0]) {
    v = mix(v, f.colors[0], 0.85f);
    return;
  }
  for (int j = 0; j < f.s.i[1]; j++) {
    float d = absf((float)i - f.s.f[j]);
    if (d < 0.9f) v = mix(v, f.colors[0], 0.9f * (1.0f - d / 0.9f));
  }
}

// --- Comet ---
static const nl_param_def COMET_PARAMS[] = {
  {"freq", "Comets", 1, 17, 1, "/min", 6},
  {"speed", "Speed", 20, 400, 1, "px/s", 150},
};

static void comet_prepare(const Program &p, FxRun &f) {
  Window wd = window(f.t_ms, 3500);
  f.s.i[0] = hf(f, wd.w, 26) < fp(f, 0) * 3.5f / 60.0f;
  float dir = hf(f, wd.w, 27) < 0.5f ? 1.0f : -1.0f, tr = wd.lt * fp(f, 1);
  f.s.f[0] = dir > 0.0f ? tr : (float)p.n - tr;
  f.s.f[1] = dir;
  f.s.f[2] = (float)p.n * 0.25f;
}

static void comet_apply(const Program &, const FxRun &f, int i, Col &v) {
  if (!f.s.i[0]) return;
  float d = (f.s.f[0] - (float)i) * f.s.f[1], L = f.s.f[2];
  if (d >= 0.0f && d < L)
    v = mix(v, d < 1.5f ? mix(f.colors[0], WHITE, 0.6f) : f.colors[0], powf_(1.0f - d / L, 2.5f));
}

// ============================================================ Spooky

// --- Ghost ---
static const nl_param_def GHOST_PARAMS[] = {
  {"speed", "Speed", 2, 60, 1, "px/s", 12},
  {"size", "Size", 2, 40, 1, "px", 10},
};

static void ghost_prepare(const Program &p, FxRun &f) {
  int64_t w = fpi(f, 1);
  int64_t pos = floor_mod(scale_q16(f.t_ms, fpi(f, 0), 1000), (p.n + 4 * w) * Q16) - 2 * w * Q16;
  f.s.f[0] = (float)pos * (1.0f / 65536.0f);
  f.s.f[1] = (float)w;
}

static void ghost_apply(const Program &, const FxRun &f, int i, Col &v) {
  float d = ((float)i - f.s.f[0]) / f.s.f[1], e = absf(d) - 1.3f;
  v = mix(v, BLACK, expf_(-d * d) * 0.95f);
  v = mix(v, f.colors[0], 0.35f * expf_(-e * e * 3.0f) * 0.15f);
}

// --- Spooky eyes ---
static const nl_param_def EYES_PARAMS[] = {
  {"freq", "Appearances", 1, 40, 1, "/min", 12},
};

static void eyes_prepare(const Program &p, FxRun &f) {
  for (int j = 0; j < 3; j++) {
    int64_t P = 4000 + 1300 * j, oP = (370 * j * P + 500) / 1000;   // offset o * P, o = 0.37 j
    int64_t w = floor_div(f.t_ms + oP, P);
    float lt = (float)(f.t_ms + oP - w * P) * 0.001f;
    bool show = !(hf2(f, w, j, 28) > fp(f, 0) / 3.0f * (float)P / 60000.0f || lt > 2.2f);
    f.s.i[j] = show ? ftoi_floor(hf2(f, w, j, 29) * (float)(p.n - 8)) + 3 : INT32_MIN;
    f.s.f[j] = (lt > 1.2f && lt < 1.35f) ? 0.0f : minf(1.0f, minf(lt * 4.0f, (2.2f - lt) * 4.0f));
  }
}

static void eyes_apply(const Program &, const FxRun &f, int i, Col &v) {
  for (int j = 0; j < 3; j++) {
    int64_t x = f.s.i[j];
    if (x == INT32_MIN) continue;
    if (i == x || i == x + 3) v = mix(scale(v, 0.3f), f.colors[0], f.s.f[j]);
    else if (i > x - 3 && i < x + 6) v = scale(v, 0.3f);
  }
}

// --- Brownout ---
static const nl_param_def BROWNOUT_PARAMS[] = {
  {"freq", "Brownouts", 1, 10, 1, "/min", 2},
  {"depth", "Depth", 10, 100, 1, "%", 80},
};

static void brownout_prepare(const Program &, FxRun &f) {
  Window wd = window(f.t_ms, 6000);
  float D = fp(f, 1) / 100.0f, lt = wd.lt;
  f.s.i[0] = hf(f, wd.w, 30) < fp(f, 0) * 6.0f / 60.0f && lt < 2.0f;
  float k;
  if (lt < 0.3f) k = 1.0f - lt / 0.3f * D;
  else if (lt < 1.3f) k = 1.0f - D * (0.7f + 0.3f * hf(f, floor_div(f.t_ms * 15, 1000), 31));
  else k = 1.0f - D * (1.0f - (lt - 1.3f) / 0.7f);
  f.s.f[0] = k;
}

static void brownout_apply(const Program &, const FxRun &f, int, Col &v) {
  if (!f.s.i[0]) return;
  float k = f.s.f[0];
  v = col(v.r * k, v.g * k * (0.8f + 0.2f * k), v.b * k * (0.5f + 0.5f * k));
}

// --- Glitch ---
static const nl_param_def GLITCH_PARAMS[] = {
  {"freq", "Glitches", 1, 120, 1, "/min", 20},
};

static void glitch_prepare(const Program &p, FxRun &f) {
  int64_t w = floor_div(f.t_ms * 7, 1000);
  f.s.i[0] = hf(f, w, 32) < fp(f, 0) / 420.0f;
  f.s.i[1] = hf(f, w, 35) < 0.5f;
  f.s.f[0] = hf(f, w, 33) * (float)p.n;
  f.s.f[1] = 3.0f + hf(f, w, 34) * (float)p.n * 0.25f;
}

static void glitch_apply(const Program &, const FxRun &f, int i, Col &v) {
  if (!f.s.i[0] || (float)i < f.s.f[0] || (float)i >= f.s.f[0] + f.s.f[1]) return;
  v = f.s.i[1] ? BLACK : col(v.b, v.r, v.g);
}

// ============================================================ table

#define APPLY(prepare, apply) prepare, apply, nullptr, nullptr, nullptr, nullptr
#define FRAME_FX(frame) nullptr, nullptr, frame, nullptr, nullptr, nullptr
#define MOVES(prepare) prepare, nullptr, nullptr, nullptr, nullptr, nullptr

const EffectDef EFFECTS[NL_FX_COUNT] = {
  {"Motion", "Movement", "Scrolls any pattern along the strip, or swings it back and forth.",
   F_MOVES, MOTION_PARAMS, COUNT(MOTION_PARAMS), nullptr, MOVES(motion_prepare)},
  {"Mirror", "Movement", "Reflect the pattern from the center.",
   F_MOVES, nullptr, 0, nullptr, MOVES(nullptr)},
  {"Shimmer", "Atmosphere", "Fine ripples of light across the strip.",
   0, SHIMMER_PARAMS, COUNT(SHIMMER_PARAMS), nullptr, APPLY(shimmer_prepare, shimmer_apply)},
  {"Sparkle", "Atmosphere", "Random glints of light.",
   0, SPARKLE_PARAMS, COUNT(SPARKLE_PARAMS), "#ffffff", APPLY(sparkle_prepare, sparkle_apply)},
  {"Candle", "Atmosphere", "Warm, uneven flicker.",
   0, CANDLE_PARAMS, COUNT(CANDLE_PARAMS), "#ff8a2a", APPLY(candle_prepare, candle_apply)},
  {"Pulse", "Atmosphere", "The whole strip breathes in brightness.",
   0, PULSE_PARAMS, COUNT(PULSE_PARAMS), nullptr, APPLY(pulse_prepare, scale_apply)},
  {"Heartbeat", "Atmosphere", "A lub-dub double pulse.",
   0, HEARTBEAT_PARAMS, COUNT(HEARTBEAT_PARAMS), nullptr, APPLY(heartbeat_prepare, scale_apply)},
  {"Wave", "Atmosphere", "A bright band sweeps along.",
   0, WAVE_PARAMS, COUNT(WAVE_PARAMS), nullptr, APPLY(wave_prepare, wave_apply)},
  {"Rainbow sweep", "Atmosphere", "An occasional rainbow wipe passes over.",
   0, RAINBOW_SWEEP_PARAMS, COUNT(RAINBOW_SWEEP_PARAMS), nullptr,
   APPLY(rainbow_sweep_prepare, rainbow_sweep_apply)},
  {"Fireflies", "Scenes", "Tiny lights wander and blink, each on its own rhythm.",
   0, FIREFLIES_PARAMS, COUNT(FIREFLIES_PARAMS), "#c8ff3c", FRAME_FX(fireflies_frame)},
  {"Fireworks", "Scenes", "Rockets climb from pixel 0, burst, and their sparks rain down.",
   0, FIREWORKS_PARAMS, COUNT(FIREWORKS_PARAMS), nullptr, FRAME_FX(fireworks_frame)},
  {"Rain & ripples", "Scenes", "Drops land at random spots and ripples spread both ways.",
   0, RAIN_PARAMS, COUNT(RAIN_PARAMS), "#00ffff", FRAME_FX(rain_frame)},
  {"Traffic", "Scenes", "Headlights one way, taillights the other.",
   F_NO_COLORS, TRAFFIC_PARAMS, COUNT(TRAFFIC_PARAMS), nullptr, FRAME_FX(traffic_frame)},
  {"Data stream", "Scenes", "Packets of different sizes race along the strip.",
   0, DATA_STREAM_PARAMS, COUNT(DATA_STREAM_PARAMS), nullptr, FRAME_FX(data_stream_frame)},
  {"Bouncing balls", "Scenes", "Balls bounce on pixel 0 under real gravity.",
   F_STATEFUL, BALLS_PARAMS, COUNT(BALLS_PARAMS), nullptr,
   nullptr, nullptr, balls_frame, balls_bytes, balls_init, balls_step},
  {"Laser duel", "Scenes", "Bolts fire from both ends and collide in sparks.",
   0, LASER_PARAMS, COUNT(LASER_PARAMS), nullptr, FRAME_FX(laser_frame)},
  {"Lightning", "Weather", "Occasional bright storm flashes.",
   0, LIGHTNING_PARAMS, COUNT(LIGHTNING_PARAMS), "#ffffff", APPLY(lightning_prepare, lightning_apply)},
  {"Snowfall", "Weather", "Flakes drift down and pile up at pixel 0.",
   0, SNOWFALL_PARAMS, COUNT(SNOWFALL_PARAMS), "#ffffff", APPLY(snowfall_prepare, snowfall_apply)},
  {"Comet", "Weather", "Every so often a comet streaks across.",
   0, COMET_PARAMS, COUNT(COMET_PARAMS), "#00ffff", APPLY(comet_prepare, comet_apply)},
  {"Ghost", "Spooky", "A dark shape drifts along the strip, swallowing the light.",
   0, GHOST_PARAMS, COUNT(GHOST_PARAMS), "#aabbff", APPLY(ghost_prepare, ghost_apply)},
  {"Spooky eyes", "Spooky", "Pairs of glowing eyes open in the dark, blink, and vanish.",
   0, EYES_PARAMS, COUNT(EYES_PARAMS), "#ff0000", APPLY(eyes_prepare, eyes_apply)},
  {"Brownout", "Spooky", "The power sags, sputters, then recovers.",
   0, BROWNOUT_PARAMS, COUNT(BROWNOUT_PARAMS), nullptr, APPLY(brownout_prepare, brownout_apply)},
  {"Glitch", "Spooky", "Digital dropouts and color swaps.",
   0, GLITCH_PARAMS, COUNT(GLITCH_PARAMS), nullptr, APPLY(glitch_prepare, glitch_apply)},
  {"Earthquake", "Spooky", "Every so often the lights shake.",
   F_MOVES, EARTHQUAKE_PARAMS, COUNT(EARTHQUAKE_PARAMS), nullptr, MOVES(earthquake_prepare)},
};

}  // namespace nl

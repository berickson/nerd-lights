// Deterministic math for the pattern engine. Integer hashing and random numbers,
// time reduction in integers, and float functions built only from + - * / and
// bit operations, so the ESP32 and WebAssembly round every step the same way.
#pragma once
#include <stdint.h>
#include <string.h>

namespace nl {

// --- hashing and random numbers ---

// lowbias32 (Chris Wellons): a good 32-bit integer mixer.
inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352du;
  x ^= x >> 15; x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}
inline uint32_t hash32(uint32_t a, uint32_t b) { return hash32(a ^ hash32(b + 0x9e3779b9u)); }
inline uint32_t hash32(uint32_t a, uint32_t b, uint32_t c) { return hash32(hash32(a, b), c); }

// Top 24 bits as a float in [0, 1), exact in float.
inline float unit(uint32_t h) { return (float)(h >> 8) * (1.0f / 16777216.0f); }
inline float hash01(uint32_t seed, uint32_t k) { return unit(hash32(seed, k)); }
// Hash of (seed, a, salt) in [0, 1). Each call site in a pattern uses its own salt,
// standing in for the spec's hsh(x * 1.7), hsh(x + 5) and so on.
inline float hh(uint32_t seed, int64_t a, uint32_t salt) {
  return unit(hash32(seed, (uint32_t)a, salt));
}

// PCG32 (O'Neill), for stateful patterns in place of Math.random().
struct Rng {
  uint64_t state = 0, inc = 1;
  void seed(uint32_t s) {
    state = 0; inc = ((uint64_t)hash32(s, 0x51ed) << 1) | 1u;
    next(); state += hash32(s); next();
  }
  uint32_t next() {
    uint64_t old = state;
    state = old * 6364136223846793005ull + inc;
    uint32_t xs = (uint32_t)(((old >> 18) ^ old) >> 27);
    uint32_t rot = (uint32_t)(old >> 59);
    return (xs >> rot) | (xs << ((32 - rot) & 31));
  }
  // Uniform in [0, n), n > 0.
  uint32_t below(uint32_t n) { return (uint32_t)(((uint64_t)next() * n) >> 32); }
  float next01() { return unit(next()); }
};

// --- integer helpers ---

inline int64_t floor_div(int64_t a, int64_t b) {
  int64_t q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}
inline int64_t floor_mod(int64_t a, int64_t b) { return a - floor_div(a, b) * b; }

// Q16 fixed-point positions: 65536 per pixel. Used only for positions and
// noise coordinates, where values can be far larger than a float holds exactly.
const int64_t Q16 = 65536;

// t_ms * num / den, as Q16, without overflowing for times in the year 2100+.
inline int64_t scale_q16(int64_t t_ms, int64_t num, int64_t den) {
  int64_t a = t_ms * num;
  int64_t q = floor_div(a, den), r = a - q * den;
  return q * Q16 + (r * Q16) / den;
}

// Fractional turns of t_ms * num / den, as Q32 (wraps every whole turn).
inline uint32_t turns_q32(int64_t t_ms, int64_t num, int64_t den) {
  int64_t a = t_ms * num;
  int64_t r = floor_mod(a, den);
  return (uint32_t)(((uint64_t)r << 32) / (uint64_t)den);
}

// Q16 position to integer pixel (floor) and fraction. Arithmetic shift is floor
// on both GCC and clang targets.
inline int64_t q_floor(int64_t q) { return q >> 16; }
inline float q_frac(int64_t q) { return (float)(int32_t)(q & 0xffff) * (1.0f / 65536.0f); }

// t_s * num / den split into whole cycles and the fraction of the current one.
struct Phase { int64_t k; float f; };
inline Phase phase(int64_t t_ms, int64_t num, int64_t den) {
  int64_t a = t_ms * num, d = den * 1000;
  int64_t k = floor_div(a, d);
  Phase p = {k, (float)(a - k * d) / (float)d};
  return p;
}

// Window of length period_ms holding t_ms: its index, and seconds into it.
struct Window { int64_t w; float lt; };
inline Window window(int64_t t_ms, int64_t period_ms) {
  int64_t w = floor_div(t_ms, period_ms);
  Window r = {w, (float)(t_ms - w * period_ms) * 0.001f};
  return r;
}

// Phase (Q32 turns) of the angle t_s * num / den radians.
const uint64_t INV_2PI_Q32 = 683565276u;  // 2^32 / (2 pi)
inline uint32_t rad_q32(int64_t t_ms, int64_t num, int64_t den) {
  int64_t a = t_ms * num, d = den * 1000;
  int64_t q = floor_div(a, d), r = a - q * d;
  return (uint32_t)((uint64_t)q * INV_2PI_Q32 + ((uint64_t)r * INV_2PI_Q32) / (uint64_t)d);
}

// --- float functions ---

inline int32_t ftoi_floor(float x) {
  int32_t i = (int32_t)x;               // truncates toward zero
  return (x < (float)i) ? i - 1 : i;
}
inline float floorf_(float x) { return (float)ftoi_floor(x); }
inline float absf(float x) { return x < 0.0f ? -x : x; }
// JS Math.round: halves round up.
inline int32_t roundi(float x) { return ftoi_floor(x + 0.5f); }
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }

// sin of a phase in turns (Q32: 2^32 = one full turn).
float sin_turns(uint32_t phase);
// sin of x radians, for small |x| (a few thousand at most).
float sinf_(float x);
float exp2f_(float x);
float log2f_(float x);
// x^y for x >= 0.
float powf_(float x, float y);
float expf_(float x);
float sqrtf_(float x);
// Small angle in radians to Q32 turns, for adding to a time phase.
inline uint32_t rad_to_q32(float rad) {
  float turns = rad * 0.159154943092f;
  return (uint32_t)(int64_t)((turns - floorf_(turns)) * 4294967296.0f);
}
inline float cos_turns(uint32_t phase) { return sin_turns(phase + 0x40000000u); }
// sin(f * pi) for f in [0, 1].
inline float sin_pi(float f) { return sin_turns((uint32_t)(int64_t)(f * 2147483648.0f)); }

// --- color ---

struct Col { float r, g, b; };
inline Col col(float r, float g, float b) { Col c = {r, g, b}; return c; }
inline Col mix(Col a, Col c, float k) {
  return col(a.r + (c.r - a.r) * k, a.g + (c.g - a.g) * k, a.b + (c.b - a.b) * k);
}
inline Col scale(Col a, float k) { return col(a.r * k, a.g * k, a.b * k); }
inline float maxc(const Col &c) { return maxf(c.r, maxf(c.g, c.b)); }
// Fully saturated hue, h in turns (spec hsv()).
Col hsv(float h);
// Blend along a list of colors, h in [0, 1] (spec ramp()).
Col ramp(const Col *s, int n, float h);

}  // namespace nl

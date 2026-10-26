#include "nl_math.h"

namespace nl {

// sin(x) for x in [0, pi/2], Taylor to x^9 (error under 4e-6 at pi/2).
static float sin_quarter(float x) {
  float x2 = x * x;
  return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f +
         x2 * (-1.0f / 5040.0f + x2 * (1.0f / 362880.0f)))));
}

float sin_turns(uint32_t phase) {
  const float half_pi = 1.57079632679f;
  uint32_t quadrant = phase >> 30;
  uint32_t low = phase & 0x3fffffffu;
  // Count from the near end of the quadrant, so cos never needs a subtraction in float.
  uint32_t part = (quadrant & 1) ? (0x40000000u - low) : low;
  float s = sin_quarter((float)part * (half_pi / 1073741824.0f));
  return (quadrant & 2) ? -s : s;
}

float sinf_(float x) {
  float turns = x * 0.159154943092f;  // 1 / (2 pi)
  float frac = turns - floorf_(turns);
  return sin_turns((uint32_t)(int64_t)(frac * 4294967296.0f));
}

float exp2f_(float x) {
  if (x < -126.0f) return 0.0f;
  if (x > 127.0f) x = 127.0f;
  int32_t i = ftoi_floor(x);
  float f = x - (float)i;
  // 2^f on [0, 1), minimax fit (error about 2e-7).
  float p = 1.0f + f * (0.693147182f + f * (0.240226507f + f * (0.0555041087f +
            f * (0.00961812911f + f * 0.00133335581f))));
  uint32_t bits = (uint32_t)(i + 127) << 23;
  float scale;
  memcpy(&scale, &bits, 4);
  return p * scale;
}

float log2f_(float x) {
  if (!(x >= 1.17549435e-38f)) return -126.0f;  // also catches NaN and x <= 0
  uint32_t bits;
  memcpy(&bits, &x, 4);
  int32_t e = (int32_t)((bits >> 23) & 0xff) - 127;
  bits = (bits & 0x007fffffu) | 0x3f800000u;   // mantissa in [1, 2)
  float m;
  memcpy(&m, &bits, 4);
  // log2(m) = 2/ln2 * atanh(s), s = (m-1)/(m+1) <= 1/3
  float s = (m - 1.0f) / (m + 1.0f);
  float s2 = s * s;
  float a = s * (1.0f + s2 * (1.0f / 3.0f + s2 * (1.0f / 5.0f + s2 * (1.0f / 7.0f +
            s2 * (1.0f / 9.0f)))));
  return (float)e + a * 2.88539008178f;
}

float powf_(float x, float y) {
  if (y == 0.0f) return 1.0f;
  if (x <= 0.0f) return 0.0f;
  if (x == 1.0f) return 1.0f;
  return exp2f_(y * log2f_(x));
}

float expf_(float x) { return exp2f_(x * 1.44269504089f); }

float sqrtf_(float x) { return x <= 0.0f ? 0.0f : exp2f_(0.5f * log2f_(x)); }

Col hsv(float h) {
  h = (h - floorf_(h)) * 6.0f;
  int i = ftoi_floor(h);
  float f = h - (float)i, q = 1.0f - f;
  switch (i % 6) {
    case 0: return col(255, f * 255, 0);
    case 1: return col(q * 255, 255, 0);
    case 2: return col(0, 255, f * 255);
    case 3: return col(0, q * 255, 255);
    case 4: return col(f * 255, 0, 255);
    default: return col(255, 0, q * 255);
  }
}

Col ramp(const Col *s, int n, float h) {
  if (n == 1) return s[0];
  h = clampf(h, 0.0f, 0.999f) * (float)(n - 1);
  int a = ftoi_floor(h);
  return mix(s[a], s[a + 1], h - (float)a);
}

}  // namespace nl

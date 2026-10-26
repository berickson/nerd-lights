// Render pipeline (from the redesign spec):
//   1. Pixel-moving effects remap the sample position, in stack order.
//   2. The base is sampled there; Motion's anti-aliasing blends floor(x) and
//      floor(x)+1. Frame bases draw their whole frame first and are looked up.
//   3. Other effects apply top to bottom (none yet).
//   4. Brightness multiplies, and each channel rounds to a byte.
#include <new>
#include <stdlib.h>
#include "nl_internal.h"

using namespace nl;

static const uint64_t INV_PI_Q32 = 1367130551u;  // 2^32 / pi

static float clamp_param(float v, const nl_param_def &d) {
  if (!(v == v)) return d.def;  // NaN
  v = clampf(v, d.min, d.max);
  if (d.step > 0.0f) v = d.min + floorf_((v - d.min) / d.step + 0.5f) * d.step;
  return v > d.max ? d.max : v;
}

extern "C" void nl_config_defaults(nl_config *cfg, uint16_t base) {
  memset(cfg, 0, sizeof(*cfg));
  if (base >= NL_BASE_COUNT) base = NL_BASE_NORMAL;
  cfg->base = base;
  const BaseDef &b = BASES[base];
  for (int i = 0; i < b.n_params; i++) cfg->params[i] = b.params[i].def;
  cfg->brightness = 100;
}

extern "C" void nl_effect_defaults(nl_effect_config *fx, uint16_t type) {
  memset(fx, 0, sizeof(*fx));
  if (type >= NL_FX_COUNT) type = NL_FX_MOTION;
  fx->type = type;
  fx->on = 1;
  const EffectDef &e = EFFECTS[type];
  for (int i = 0; i < e.n_params; i++) fx->params[i] = e.params[i].def;
}

extern "C" nl_program *nl_program_create(const nl_config *in, uint16_t n_px) {
  if (n_px == 0 || n_px > NL_MAX_PIXELS) return nullptr;
  void *mem = calloc(1, sizeof(nl_program));
  if (!mem) return nullptr;
  nl_program *pp = new (mem) nl_program();
  Program &p = *pp;
  p.cfg = *in;
  p.n = n_px;
  if (p.cfg.base >= NL_BASE_COUNT) p.cfg.base = NL_BASE_NORMAL;
  p.base = &BASES[p.cfg.base];
  for (int i = 0; i < NL_MAX_PARAMS; i++)
    p.cfg.params[i] = i < p.base->n_params ? clamp_param(p.cfg.params[i], p.base->params[i]) : 0.0f;
  if (p.cfg.n_colors > NL_MAX_COLORS) p.cfg.n_colors = NL_MAX_COLORS;
  if (p.cfg.n_effects > NL_MAX_EFFECTS) p.cfg.n_effects = NL_MAX_EFFECTS;
  if (p.cfg.brightness < 1) p.cfg.brightness = 1;
  if (p.cfg.brightness > 100) p.cfg.brightness = 100;
  for (int k = 0; k < p.cfg.n_effects; k++) {
    nl_effect_config &fx = p.cfg.effects[k];
    if (fx.type >= NL_FX_COUNT) { fx.on = 0; continue; }
    const EffectDef &e = EFFECTS[fx.type];
    for (int i = 0; i < NL_MAX_PARAMS; i++)
      fx.params[i] = i < e.n_params ? clamp_param(fx.params[i], e.params[i]) : 0.0f;
  }
  p.n_colors = p.cfg.n_colors;
  for (int j = 0; j < p.n_colors; j++)
    p.colors[j] = col(p.cfg.colors[j].r, p.cfg.colors[j].g, p.cfg.colors[j].b);
  if (p.n_colors == 0) {
    p.colors[0] = col(255, 255, 255);
    p.n_colors = 1;
  }
  p.seed = hash32(p.cfg.seed, 0xba5e0000u + p.cfg.base);
  p.rng.seed(p.seed);
  if (p.base->frame) {
    p.state_size = p.base->state_bytes(p);
    p.state = calloc(1, p.state_size);
    p.frame = (Col *)calloc(p.n, sizeof(Col));
    if (!p.state || !p.frame) {
      nl_program_free(pp);
      return nullptr;
    }
    p.base->init(p);
  }
  return pp;
}

extern "C" void nl_program_free(nl_program *p) {
  if (!p) return;
  free(p->state);
  free(p->frame);
  p->~nl_program();
  free(p);
}

extern "C" uint32_t nl_program_bytes(const nl_program *p) {
  return (uint32_t)sizeof(nl_program) + p->state_size + (p->frame ? p->n * (uint32_t)sizeof(Col) : 0);
}

static void advance(Program &p, int64_t t_ms) {
  if (!p.started || t_ms < p.last_step_ms || t_ms - p.last_step_ms > NL_MAX_CATCHUP_MS) {
    p.started = true;
    p.last_step_ms = t_ms;
    return;
  }
  while (t_ms - p.last_step_ms >= NL_STEP_MS) {
    p.base->step(p);
    p.last_step_ms += NL_STEP_MS;
  }
}

// Motion's shift of the sample position at t_ms, Q16 pixels.
static int64_t motion_shift(const nl_effect_config &fx, int64_t t_ms) {
  int64_t speed = (int64_t)fx.params[0], w = (int64_t)fx.params[1];
  if (w <= 0) return scale_q16(t_ms, speed, 1000);
  // w * sin(t * speed * 2 / w): phase in turns is t_ms * speed / (1000 * w * pi)
  int64_t a = t_ms * speed, d = 1000 * w;
  int64_t q = floor_div(a, d), r = a - q * d;
  uint32_t phase = (uint32_t)((uint64_t)q * INV_PI_Q32 + ((uint64_t)r * INV_PI_Q32) / (uint64_t)d);
  return (int64_t)((float)w * sin_turns(phase) * 65536.0f);
}

extern "C" void nl_program_render(nl_program *pp, int64_t t_ms, uint8_t *rgb) {
  Program &p = *pp;
  const int n = p.n;
  if (p.base->frame) {
    advance(p, t_ms);
    for (int i = 0; i < n; i++) p.frame[i] = col(0, 0, 0);
    p.base->frame(p, p.frame);
  }

  int64_t shift = 0;
  float aa = -1.0f;
  for (int k = 0; k < p.cfg.n_effects; k++) {
    const nl_effect_config &fx = p.cfg.effects[k];
    if (!fx.on) continue;
    if (fx.type == NL_FX_MOTION) {
      shift += motion_shift(fx, t_ms);
      aa = fx.params[2] / 100.0f;
    }
  }

  const float bright = (float)p.cfg.brightness / 100.0f;
  auto sample = [&](int64_t xq) -> Col {
    if (p.frame) return p.frame[floor_mod(q_floor(xq + Q16 / 2), n)];
    return p.base->sample(p, xq, t_ms);
  };
  for (int i = 0; i < n; i++) {
    int64_t xq = (int64_t)i * Q16 - shift;
    Col v;
    if (aa >= 0.0f) {
      int64_t x0 = q_floor(xq) * Q16;
      float fr = q_frac(xq);
      float ff = fr * aa + (fr >= 0.5f ? 1.0f : 0.0f) * (1.0f - aa);
      v = mix(sample(x0), sample(x0 + Q16), ff);
    } else {
      v = sample(xq);
    }
    uint8_t *o = rgb + i * 3;
    o[0] = (uint8_t)ftoi_floor(clampf(v.r * bright, 0.0f, 255.0f) + 0.5f);
    o[1] = (uint8_t)ftoi_floor(clampf(v.g * bright, 0.0f, 255.0f) + 0.5f);
    o[2] = (uint8_t)ftoi_floor(clampf(v.b * bright, 0.0f, 255.0f) + 0.5f);
  }
}

extern "C" const char *nl_base_name(uint16_t b) { return b < NL_BASE_COUNT ? BASES[b].name : nullptr; }
extern "C" uint8_t nl_base_flags(uint16_t b) { return b < NL_BASE_COUNT ? BASES[b].flags : 0; }
extern "C" uint8_t nl_base_param_count(uint16_t b) { return b < NL_BASE_COUNT ? BASES[b].n_params : 0; }
extern "C" const nl_param_def *nl_base_param(uint16_t b, uint8_t i) {
  return b < NL_BASE_COUNT && i < BASES[b].n_params ? &BASES[b].params[i] : nullptr;
}
extern "C" const char *nl_effect_name(uint16_t t) { return t < NL_FX_COUNT ? EFFECTS[t].name : nullptr; }
extern "C" uint8_t nl_effect_param_count(uint16_t t) { return t < NL_FX_COUNT ? EFFECTS[t].n_params : 0; }
extern "C" const nl_param_def *nl_effect_param(uint16_t t, uint8_t i) {
  return t < NL_FX_COUNT && i < EFFECTS[t].n_params ? &EFFECTS[t].params[i] : nullptr;
}

extern "C" uint32_t nl_config_size(void) { return sizeof(nl_config); }
extern "C" void nl_config_set_param(nl_config *c, uint8_t i, float v) {
  if (i < NL_MAX_PARAMS) c->params[i] = v;
}
extern "C" void nl_config_set_colors(nl_config *c, uint8_t n) {
  c->n_colors = n > NL_MAX_COLORS ? NL_MAX_COLORS : n;
}
extern "C" void nl_config_set_color(nl_config *c, uint8_t j, uint8_t r, uint8_t g, uint8_t b) {
  if (j < NL_MAX_COLORS) c->colors[j] = nl_rgb{r, g, b};
}
extern "C" void nl_config_set_brightness(nl_config *c, uint8_t v) { c->brightness = v; }
extern "C" void nl_config_set_seed(nl_config *c, uint32_t seed) { c->seed = seed; }
extern "C" int nl_config_add_effect(nl_config *c, uint16_t type) {
  if (c->n_effects >= NL_MAX_EFFECTS) return -1;
  nl_effect_defaults(&c->effects[c->n_effects], type);
  return c->n_effects++;
}
extern "C" void nl_config_set_effect_param(nl_config *c, uint8_t k, uint8_t i, float v) {
  if (k < c->n_effects && i < NL_MAX_PARAMS) c->effects[k].params[i] = v;
}

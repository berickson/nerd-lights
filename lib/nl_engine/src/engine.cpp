// Render pipeline (from the redesign spec):
//   1. Pixel-moving effects remap the sample position, in stack order.
//   2. The base is sampled there; Motion's anti-aliasing blends floor(x) and
//      floor(x)+1. Frame bases draw their whole frame first and are looked up.
//   3. Other effects apply top to bottom; frame effects composite over the
//      pattern by their own brightness.
//   4. Brightness multiplies, and each channel rounds to a byte.
//
// Without Mirror every pixel moves by the same amount, so a whole row of base
// samples is taken at once (n + 1 of them with anti-aliasing, shared by
// neighbors) and bases can step through it without dividing per pixel.
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include "nl_internal.h"

using namespace nl;

static float clamp_param(float v, const nl_param_def &d) {
  if (!(v == v)) return d.def;  // NaN
  v = clampf(v, d.min, d.max);
  if (d.step > 0.0f) v = d.min + floorf_((v - d.min) / d.step + 0.5f) * d.step;
  return v > d.max ? d.max : v;
}

static nl_rgb parse_hex(const char *h) {
  uint32_t v = (uint32_t)strtoul(h + 1, nullptr, 16);
  return nl_rgb{(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
}

static Col to_col(const nl_rgb &c) { return col(c.r, c.g, c.b); }

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

static void *alloc(Program &p, uint32_t bytes) {
  p.bytes += bytes;
  return calloc(1, bytes);
}

extern "C" nl_program *nl_program_create(const nl_config *in, uint16_t n_px) {
  if (n_px == 0 || n_px > NL_MAX_PIXELS) return nullptr;
  void *mem = calloc(1, sizeof(nl_program));
  if (!mem) return nullptr;
  nl_program *pp = new (mem) nl_program();
  Program &p = *pp;
  p.bytes = sizeof(nl_program);
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
  p.raw_colors = p.n_colors = p.cfg.n_colors;
  for (int j = 0; j < p.n_colors; j++) p.colors[j] = to_col(p.cfg.colors[j]);
  if (p.n_colors == 0) {
    p.colors[0] = col(255, 255, 255);
    p.n_colors = 1;
  }

  bool ok = true;
  p.seed = hash32(p.cfg.seed, 0xba5e0000u + p.cfg.base);
  p.rng.seed(p.seed);
  p.samples = (Col *)alloc(p, (p.n + 1) * (uint32_t)sizeof(Col));
  ok = ok && p.samples;
  if (p.base->frame) {
    p.frame = (Col *)alloc(p, p.n * (uint32_t)sizeof(Col));
    ok = ok && p.frame;
  }
  if (ok && p.base->state_bytes) {
    p.state = alloc(p, p.base->state_bytes(p));
    ok = ok && p.state;
    if (ok) p.base->init(p);
  }
  p.stateful = p.base->step != nullptr;

  for (int k = 0; ok && k < p.cfg.n_effects; k++) {
    nl_effect_config &cfg = p.cfg.effects[k];
    if (cfg.type >= NL_FX_COUNT || !cfg.on) continue;
    const EffectDef &e = EFFECTS[cfg.type];
    for (int i = 0; i < NL_MAX_PARAMS; i++)
      cfg.params[i] = i < e.n_params ? clamp_param(cfg.params[i], e.params[i]) : 0.0f;
    FxRun &f = p.fx[p.n_fx++];
    f.def = &e;
    f.cfg = &cfg;
    f.seed = hash32(0xeffec700u + cfg.type, cfg.instance);
    if (cfg.color_mode == NL_COLOR_CUSTOM) {
      f.colors[0] = to_col(cfg.color);
      f.n_colors = 1;
    } else if (cfg.color_mode == NL_COLOR_DEFAULT && e.default_color) {
      f.colors[0] = to_col(parse_hex(e.default_color));
      f.n_colors = 1;
    } else {
      for (int j = 0; j < p.raw_colors; j++) f.colors[j] = p.colors[j];
      f.n_colors = p.raw_colors;
    }
    if (!f.n_colors) f.colors[0] = col(255, 255, 255);
    if (e.frame) {
      f.frame = (Col *)alloc(p, p.n * (uint32_t)sizeof(Col));
      ok = ok && f.frame;
    }
    if (ok && e.state_bytes) {
      f.rng.seed(hash32(p.cfg.seed, f.seed));
      f.state = alloc(p, e.state_bytes(p, f));
      ok = ok && f.state;
      if (ok) e.init(p, f);
    }
    if (e.step) p.stateful = true;
  }
  if (!ok) {
    nl_program_free(pp);
    return nullptr;
  }
  return pp;
}

extern "C" void nl_program_free(nl_program *p) {
  if (!p) return;
  free(p->state);
  free(p->frame);
  free(p->samples);
  for (int k = 0; k < p->n_fx; k++) {
    free(p->fx[k].frame);
    free(p->fx[k].state);
  }
  p->~nl_program();
  free(p);
}

extern "C" uint32_t nl_program_bytes(const nl_program *p) { return p->bytes; }

extern "C" void nl_program_set_utc_offset(nl_program *p, int32_t seconds) { p->utc_offset_s = seconds; }

static void advance(Program &p, int64_t t_ms) {
  if (!p.started || t_ms < p.last_step_ms || t_ms - p.last_step_ms > NL_MAX_CATCHUP_MS) {
    p.started = true;
    p.last_step_ms = t_ms;
    return;
  }
  while (t_ms - p.last_step_ms >= NL_STEP_MS) {
    if (p.base->step) p.base->step(p);
    for (int k = 0; k < p.n_fx; k++)
      if (p.fx[k].def->step) p.fx[k].def->step(p, p.fx[k]);
    p.last_step_ms += NL_STEP_MS;
  }
}

static Col sample_one(const Program &p, int64_t xq) {
  if (p.frame) return p.frame[floor_mod(q_floor(xq + Q16 / 2), p.n)];
  return p.base->sample(p, xq);
}

// count samples from x0q, one pixel apart.
static void sample_row(const Program &p, int64_t x0q, int count, Col *out) {
  if (p.base->row) {
    p.base->row(p, x0q, count, out);
  } else if (p.frame) {
    int idx = (int)floor_mod(q_floor(x0q + Q16 / 2), p.n);
    for (int k = 0; k < count; k++) {
      out[k] = p.frame[idx];
      if (++idx == p.n) idx = 0;
    }
  } else {
    for (int k = 0; k < count; k++) out[k] = p.base->sample(p, x0q + (int64_t)k * Q16);
  }
}

static float aa_weight(float fr, float aa) {
  return fr * aa + (fr >= 0.5f ? 1.0f : 0.0f) * (1.0f - aa);
}

extern "C" void nl_program_render(nl_program *pp, int64_t t_ms, uint8_t *rgb) {
  Program &p = *pp;
  const int n = p.n;
  p.t_ms = t_ms;
  if (p.stateful) advance(p, t_ms);
  if (p.base->prepare) p.base->prepare(p);
  if (p.frame) {
    for (int i = 0; i < n; i++) p.frame[i] = col(0, 0, 0);
    p.base->frame(p, p.frame);
  }

  int64_t delta = 0;
  float aa = -1.0f;
  bool mirror = false;
  for (int k = 0; k < p.n_fx; k++) {
    FxRun &f = p.fx[k];
    bool moves = f.def->flags & F_MOVES;
    f.t_ms = moves ? t_ms : t_ms + (int64_t)(f.seed % 97000u);
    if (f.def->prepare) f.def->prepare(p, f);
    if (f.frame) {
      for (int i = 0; i < n; i++) f.frame[i] = col(0, 0, 0);
      f.def->frame(p, f, f.frame);
    }
    if (!moves) continue;
    if (f.cfg->type == NL_FX_MIRROR) {
      mirror = true;
    } else {
      delta += f.s.i[0];
      if (f.cfg->type == NL_FX_MOTION) aa = f.s.f[0];
    }
  }

  Col *S = p.samples;
  if (!mirror) {
    if (aa >= 0.0f) {
      float ff = aa_weight(q_frac(delta), aa);
      sample_row(p, q_floor(delta) * Q16, n + 1, S);
      for (int i = 0; i < n; i++) S[i] = mix(S[i], S[i + 1], ff);
    } else {
      sample_row(p, delta, n, S);
    }
  } else {
    const int64_t half = (int64_t)n * Q16 / 2;
    for (int i = 0; i < n; i++) {
      int64_t xq = (int64_t)i * Q16;
      for (int k = 0; k < p.n_fx; k++) {
        const FxRun &f = p.fx[k];
        if (!(f.def->flags & F_MOVES)) continue;
        if (f.cfg->type == NL_FX_MIRROR) {
          int64_t d = xq - half;
          xq = half - (d < 0 ? -d : d);
        } else {
          xq += f.s.i[0];
        }
      }
      if (aa >= 0.0f) {
        int64_t x0 = q_floor(xq) * Q16;
        S[i] = mix(sample_one(p, x0), sample_one(p, x0 + Q16), aa_weight(q_frac(xq), aa));
      } else {
        S[i] = sample_one(p, xq);
      }
    }
  }

  const float bright = (float)p.cfg.brightness / 100.0f;
  for (int i = 0; i < n; i++) {
    Col v = S[i];
    for (int k = 0; k < p.n_fx; k++) {
      const FxRun &f = p.fx[k];
      if (f.frame) {
        const Col &o = f.frame[i];
        float al = minf(1.0f, maxc(o) / 255.0f);
        v = col(v.r * (1.0f - al) + o.r, v.g * (1.0f - al) + o.g, v.b * (1.0f - al) + o.b);
      } else if (f.def->apply) {
        f.def->apply(p, f, i, v);
      }
    }
    uint8_t *o = rgb + i * 3;
    o[0] = (uint8_t)ftoi_floor(clampf(v.r * bright, 0.0f, 255.0f) + 0.5f);
    o[1] = (uint8_t)ftoi_floor(clampf(v.g * bright, 0.0f, 255.0f) + 0.5f);
    o[2] = (uint8_t)ftoi_floor(clampf(v.b * bright, 0.0f, 255.0f) + 0.5f);
  }
}

// --- describe ---

namespace {
struct Writer {
  char *out;
  int size, len;
  void raw(const char *s) {
    for (; *s; s++, len++)
      if (len < size - 1) out[len] = *s;
  }
  void str(const char *s) {
    raw("\"");
    for (; s && *s; s++) {
      char c[3] = {*s, 0, 0};
      if (*s == '"' || *s == '\\') { c[0] = '\\'; c[1] = *s; }
      raw(c);
    }
    raw("\"");
  }
  void num(float v) {
    char b[24];
    snprintf(b, sizeof b, "%g", (double)v);
    raw(b);
  }
  void key(const char *k) { str(k); raw(":"); }
  void params(const nl_param_def *ps, int count) {
    raw("\"params\":[");
    for (int i = 0; i < count; i++) {
      const nl_param_def &d = ps[i];
      if (i) raw(",");
      raw("{"); key("key"); str(d.key); raw(","); key("label"); str(d.label);
      raw(","); key("min"); num(d.min); raw(","); key("max"); num(d.max);
      raw(","); key("step"); num(d.step); raw(","); key("unit"); str(d.unit);
      raw(","); key("def"); num(d.def); raw("}");
    }
    raw("]");
  }
};

// Display order: the handoff's groups, Classic / Games / Nerdy / Scenic.
const uint16_t BASE_ORDER[NL_BASE_COUNT] = {
  NL_BASE_NORMAL, NL_BASE_GRADIENT, NL_BASE_RAINBOW, NL_BASE_FIRE, NL_BASE_METEOR,
  NL_BASE_TWINKLE, NL_BASE_CONFETTI, NL_BASE_FLICKER, NL_BASE_BREATHE, NL_BASE_EXPLOSION,
  NL_BASE_STROBE, NL_BASE_CHASE, NL_BASE_LIGHTNING,
  NL_BASE_TETRIS, NL_BASE_SNAKE, NL_BASE_PONG, NL_BASE_CHOMP,
  NL_BASE_SORTING, NL_BASE_AUTOMATON, NL_BASE_HOURGLASS, NL_BASE_BINARY_CLOCK,
  NL_BASE_CANDLES, NL_BASE_LAVA_LAMP, NL_BASE_AURORA,
};
}  // namespace

extern "C" int nl_engine_describe(char *out, int out_size) {
  Writer w = {out, out_size, 0};
  w.raw("{\"bases\":[");
  for (int k = 0; k < NL_BASE_COUNT; k++) {
    uint16_t id = BASE_ORDER[k];
    const BaseDef &b = BASES[id];
    char ids[8];
    snprintf(ids, sizeof ids, "%u", id);
    if (k) w.raw(",");
    w.raw("{"); w.key("id"); w.raw(ids); w.raw(","); w.key("name"); w.str(b.name);
    w.raw(","); w.key("group"); w.str(b.group); w.raw(","); w.key("desc"); w.str(b.desc);
    w.raw(","); w.key("stateful"); w.raw(b.flags & F_STATEFUL ? "true" : "false");
    w.raw(","); w.key("ownColors"); w.raw(b.flags & F_NO_COLORS ? "true" : "false");
    w.raw(","); w.key("warn"); w.raw(b.flags & F_WARN ? "true" : "false");
    w.raw(","); w.params(b.params, b.n_params); w.raw("}");
  }
  w.raw("],\"effects\":[");
  for (int id = 0; id < NL_FX_COUNT; id++) {
    const EffectDef &e = EFFECTS[id];
    char ids[8];
    snprintf(ids, sizeof ids, "%d", id);
    if (id) w.raw(",");
    w.raw("{"); w.key("id"); w.raw(ids); w.raw(","); w.key("name"); w.str(e.name);
    w.raw(","); w.key("group"); w.str(e.group); w.raw(","); w.key("desc"); w.str(e.desc);
    w.raw(","); w.key("stateful"); w.raw(e.flags & F_STATEFUL ? "true" : "false");
    w.raw(","); w.key("ownColors"); w.raw(e.flags & F_NO_COLORS ? "true" : "false");
    w.raw(","); w.key("moves"); w.raw(e.flags & F_MOVES ? "true" : "false");
    w.raw(","); w.key("color");
    if (e.default_color) w.str(e.default_color); else w.raw("null");
    w.raw(","); w.params(e.params, e.n_params); w.raw("}");
  }
  w.raw("]}");
  if (out_size > 0) out[w.len < out_size ? w.len : out_size - 1] = 0;
  return w.len;
}

// --- names, params and setters ---

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
extern "C" void nl_config_set_effect_instance(nl_config *c, uint8_t k, uint16_t instance) {
  if (k < c->n_effects) c->effects[k].instance = instance;
}
extern "C" void nl_config_set_effect_color(nl_config *c, uint8_t k, uint8_t mode, uint8_t r, uint8_t g, uint8_t b) {
  if (k >= c->n_effects) return;
  c->effects[k].color_mode = mode;
  c->effects[k].color = nl_rgb{r, g, b};
}

// core.c - see core.h
#include "core.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

media_kind_t media_kind(const char *f)
{
    const char *dot = f ? strrchr(f, '.') : NULL;
    if (!dot || dot == f) return MEDIA_NONE;
    char e[6] = {0};
    for (int i = 0; i < 5 && dot[1 + i]; i++) e[i] = (char)tolower((unsigned char)dot[1 + i]);
    if (strlen(dot + 1) > 4) return MEDIA_NONE;
    if (!strcmp(e, "jpg") || !strcmp(e, "jpeg")) return MEDIA_IMAGE;
    if (!strcmp(e, "amj")) return MEDIA_ANIM;
    return MEDIA_NONE;
}

bool amj_parse_header(const uint8_t *b, size_t len, uint32_t file_size, amj_info_t *out)
{
    if (!b || len < AMJ_HEADER || memcmp(b, "AMJ1", 4)) return false;
    amj_info_t i = {.width = rd16(b + 4), .height = rd16(b + 6), .frames = rd32(b + 8)};
    if (i.width == 0 || i.height == 0 || i.width > 1024 || i.height > 1024) return false;
    if (i.frames == 0 || i.frames > AMJ_MAX_FRAMES) return false;
    if ((uint64_t)AMJ_HEADER + (uint64_t)i.frames * AMJ_ENTRY > file_size) return false;
    if (out) *out = i;
    return true;
}

bool amj_entry(const uint8_t *index, uint32_t i, uint32_t file_size, amj_frame_t *out)
{
    const uint8_t *p = index + (size_t)i * AMJ_ENTRY;
    amj_frame_t f = {.offset = rd32(p), .size = rd32(p + 4), .delay_ms = rd32(p + 8)};
    if (f.size < 4 || f.offset < AMJ_HEADER || (uint64_t)f.offset + f.size > file_size) return false;
    if (f.delay_ms < 10) f.delay_ms = 10;        // like browsers: "0" means as fast as reasonable
    if (f.delay_ms > 60000) f.delay_ms = 60000;
    *out = f;
    return true;
}

bool media_safe_name(const char *in, char *out, size_t n)
{
    size_t o = 0, max = n - 1 < MEDIA_NAME_MAX ? n - 1 : MEDIA_NAME_MAX;
    if (!in) in = "";
    // a final ".ext" (1-5 letters or digits) is not part of the name
    size_t end = strlen(in);
    const char *dot = strrchr(in, '.');
    if (dot && end - (size_t)(dot - in) >= 2 && end - (size_t)(dot - in) <= 6) {
        bool ext = true;
        for (const char *p = dot + 1; *p; p++) ext &= isalnum((unsigned char)*p) != 0;
        if (ext) end = (size_t)(dot - in);
    }
    bool sep = false;
    for (size_t i = 0; i < end && o < max; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c) || c == '-' || c == '_') {
            if (sep && o > 0 && o < max) out[o++] = '_';
            if (o < max) out[o++] = (char)c;
            sep = false;
        } else {
            sep = true;   // spaces, dots, slashes, accents, symbols -> one "_"
        }
    }
    out[o] = 0;
    return o > 0;
}

static void parse_ver(const char *s, int v[3])
{
    v[0] = v[1] = v[2] = 0;
    if (*s == 'v' || *s == 'V') s++;
    for (int i = 0; i < 3 && *s >= '0' && *s <= '9'; i++) {
        v[i] = (int)strtol(s, (char **)&s, 10);
        if (*s != '.') break;
        s++;
    }
}

int version_compare(const char *a, const char *b)
{
    int x[3], y[3];
    parse_ver(a, x);
    parse_ver(b, y);
    for (int i = 0; i < 3; i++)
        if (x[i] != y[i]) return x[i] > y[i] ? 1 : -1;
    return 0;
}

bool night_active(int h, int from, int to)
{
    if (from == to) return false;
    if (from < to) return h >= from && h < to;
    return h >= from || h < to;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

bool query_param(const char *q, const char *key, char *out, size_t n)
{
    size_t kl = strlen(key);
    while (q && *q) {
        const char *amp = strchr(q, '&');
        size_t seg = amp ? (size_t)(amp - q) : strlen(q);
        if (seg > kl && !strncmp(q, key, kl) && q[kl] == '=') {
            size_t o = 0;
            for (size_t i = kl + 1; i < seg && o + 1 < n; i++) {
                char c = q[i];
                if (c == '+') c = ' ';
                else if (c == '%' && i + 2 < seg && hexval(q[i + 1]) >= 0 && hexval(q[i + 2]) >= 0) {
                    c = (char)(hexval(q[i + 1]) << 4 | hexval(q[i + 2]));
                    i += 2;
                }
                out[o++] = c;
            }
            out[o] = 0;
            return true;
        }
        q = amp ? amp + 1 : NULL;
    }
    if (n) out[0] = 0;
    return false;
}

int shuffle_next(int cur, int count, uint32_t *seed)
{
    if (count <= 1) return 0;
    *seed = *seed * 1664525u + 1013904223u;
    int n = (int)((*seed >> 8) % (uint32_t)(count - 1));
    return n >= cur ? n + 1 : n;
}

/* ---------------- dice ---------------- */

dice_spec_t dice_clamp(dice_spec_t s)
{
    if (s.count < 1) s.count = 1;
    if (s.count > DICE_MAX) s.count = DICE_MAX;
    if (s.sides < 2) s.sides = 2;
    if (s.sides > 100) s.sides = 100;
    if (s.mod < -99) s.mod = -99;
    if (s.mod > 99) s.mod = 99;
    if (s.mode >= DICE_MODES) s.mode = DICE_NORMAL;
    if (s.drop_lowest >= s.count) s.drop_lowest = 0;
    return s;
}

int dice_one(int sides, dice_rng_t rng, void *ctx)
{
    // reject the top values that would make some faces more likely
    uint32_t limit = UINT32_MAX - (UINT32_MAX % (uint32_t)sides);
    uint32_t v;
    do v = rng(ctx); while (v >= limit);
    return (int)(v % (uint32_t)sides) + 1;
}

bool dice_is_dh(const dice_spec_t *s) { return s->mode >= DICE_DH && s->mode < DICE_MODES; }

// Daggerheart: Hope and Fear d12; the higher one names the outcome, doubles are a critical
static void dh_roll(const dice_spec_t *s, dice_rng_t rng, void *ctx, dice_result_t *r)
{
    r->hope = (uint8_t)dice_one(12, rng, ctx);
    r->fear = (uint8_t)dice_one(12, rng, ctx);
    r->val[0] = r->hope;
    r->val[1] = r->fear;
    r->kept[0] = r->kept[1] = 1;
    r->n = 2;
    r->total = r->hope + r->fear + s->mod;
    if (s->mode != DICE_DH) {
        r->d6 = (uint8_t)dice_one(6, rng, ctx);
        r->val[2] = r->d6;
        r->kept[2] = 1;
        r->n = 3;
        r->total += s->mode == DICE_DH_ADV ? r->d6 : -r->d6;
    }
    r->dh = r->hope == r->fear ? DH_CRIT : r->hope > r->fear ? DH_HOPE : DH_FEAR;
    r->crit = r->dh == DH_CRIT;
}

void dice_roll(const dice_spec_t *sp, dice_rng_t rng, void *ctx, dice_result_t *r)
{
    dice_spec_t s = dice_clamp(*sp);
    memset(r, 0, sizeof(*r));
    r->dh = DH_NONE;
    if (dice_is_dh(&s)) { dh_roll(&s, rng, ctx, r); return; }
    bool pair = s.mode != DICE_NORMAL;
    for (int i = 0; i < s.count; i++) {
        int a = dice_one(s.sides, rng, ctx);
        r->val[r->n] = (uint8_t)a;
        r->kept[r->n++] = 1;
        if (pair) {
            int b = dice_one(s.sides, rng, ctx);
            bool keep_b = s.mode == DICE_ADV ? b > a : b < a;
            r->val[r->n] = (uint8_t)b;
            r->kept[r->n] = keep_b;
            if (keep_b) r->kept[r->n - 1] = 0;
            r->n++;
        }
    }
    // drop the lowest kept dice
    for (int d = 0; d < s.drop_lowest; d++) {
        int lo = -1;
        for (int i = 0; i < r->n; i++)
            if (r->kept[i] && (lo < 0 || r->val[i] < r->val[lo])) lo = i;
        if (lo >= 0) r->kept[lo] = 0;
    }
    r->total = s.mod;
    for (int i = 0; i < r->n; i++) {
        if (!r->kept[i]) continue;
        r->total += r->val[i];
        if (s.sides == 20 && r->val[i] == 20) r->crit = true;
        if (s.sides == 20 && r->val[i] == 1) r->fumble = true;
    }
    if (r->crit) r->fumble = false;   // a 20 among the kept dice wins
}

void dice_format(const dice_spec_t *sp, char *b, size_t n)
{
    dice_spec_t s = dice_clamp(*sp);
    if (dice_is_dh(&s)) {
        int o = snprintf(b, n, "2d12%s", s.mode == DICE_DH_ADV ? "+d6" : s.mode == DICE_DH_DIS ? "-d6" : "");
        if (s.mod && o >= 0 && (size_t)o < n) snprintf(b + o, n - o, "%+d", s.mod);
        return;
    }
    int o = s.count > 1 ? snprintf(b, n, "%dd%d", s.count, s.sides) : snprintf(b, n, "d%d", s.sides);
    if (s.drop_lowest && o >= 0 && (size_t)o < n) o += snprintf(b + o, n - o, "kh%d", s.count - s.drop_lowest);
    if (s.mod && o >= 0 && (size_t)o < n) snprintf(b + o, n - o, "%+d", s.mod);
}

void dice_breakdown(const dice_result_t *r, char *b, size_t n)
{
    size_t o = 0;
    b[0] = 0;
    for (int i = 0; i < r->n && o + 8 < n; i++)
        o += snprintf(b + o, n - o, r->kept[i] ? "%s%d" : "%s(%d)", i ? " " : "", r->val[i]);
}

int dice_ability_mod(int score)
{
    int d = score - 10;
    return d >= 0 ? d / 2 : -((-d + 1) / 2);
}

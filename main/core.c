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

int battery_percent_from_volts(float v)
{
    static const float tab[][2] = {
        {4.15f, 100}, {4.05f, 90}, {3.95f, 78}, {3.85f, 62}, {3.78f, 50},
        {3.72f, 38}, {3.66f, 25}, {3.58f, 12}, {3.45f, 4}, {3.30f, 0},
    };
    if (v >= tab[0][0]) return 100;
    for (int i = 1; i < 10; i++)
        if (v >= tab[i][0]) {
            float t = (v - tab[i][0]) / (tab[i - 1][0] - tab[i][0]);
            return (int)(tab[i][1] + t * (tab[i - 1][1] - tab[i][1]) + 0.5f);
        }
    return 0;
}

int battery_percent_pick(int gauge, float v, bool charging, bool *estimated)
{
    bool was = estimated && *estimated;
    bool est_used;
    int est = battery_percent_from_volts(v), r;
    if (v <= 0) { r = gauge; est_used = false; }   // no reading: nothing better to offer
    else if (gauge < 0 || gauge > 100) { r = est; est_used = true; }
    // while charging the voltage reads high (4.1-4.2 V even half full): only a gauge stuck at
    // zero is replaced then
    else if (charging) { est_used = gauge <= 2 && est > 10; r = est_used ? est : gauge; }
    else if (gauge <= 5 && est >= 15) { r = est; est_used = true; }   // a gauge stuck near zero
    else {
        // hysteresis: the gauge takes over again only once it is close to the voltage estimate
        int d = abs(gauge - est);
        est_used = was ? d > 15 : d > 35;
        r = est_used ? est : gauge;
    }
    if (estimated) *estimated = est_used;
    return r;
}

int shuffle_next(int cur, int count, uint32_t *seed)
{
    if (count <= 1) return 0;
    *seed = *seed * 1664525u + 1013904223u;
    int n = (int)((*seed >> 8) % (uint32_t)(count - 1));
    return n >= cur ? n + 1 : n;
}

bool media_filter_ok(int kind, int filter)
{
    if (filter == MEDIA_FILTER_IMAGES) return kind == MEDIA_IMAGE;
    if (filter == MEDIA_FILTER_ANIM) return kind == MEDIA_ANIM;
    return kind == MEDIA_IMAGE || kind == MEDIA_ANIM;
}

int media_filter_count(const uint8_t *kinds, int n, int filter)
{
    int c = 0;
    for (int i = 0; i < n; i++) c += media_filter_ok(kinds[i], filter);
    return c;
}

int media_pick_first(const uint8_t *kinds, int n, int start, int filter)
{
    if (n <= 0) return -1;
    if (start < 0 || start >= n) start = 0;
    for (int k = 0; k < n; k++) {
        int i = (start + k) % n;
        if (media_filter_ok(kinds[i], filter)) return i;
    }
    return -1;
}

int media_pick_next(const uint8_t *kinds, int n, int cur, int filter, bool shuffle, uint32_t *seed)
{
    int m = media_filter_count(kinds, n, filter);
    if (!m) return -1;
    if (cur < 0 || cur >= n) cur = n - 1;
    if (!shuffle || m == 1) return media_pick_first(kinds, n, (cur + 1) % n, filter);
    // position of cur among the matching media (-1: it does not match)
    int pos = -1, j = 0;
    for (int i = 0; i < n; i++)
        if (media_filter_ok(kinds[i], filter)) { if (i == cur) pos = j; j++; }
    int pick;
    if (pos >= 0) pick = shuffle_next(pos, m, seed);
    else { *seed = *seed * 1664525u + 1013904223u; pick = (int)((*seed >> 8) % (uint32_t)m); }
    for (int i = 0, k = 0; i < n; i++)
        if (media_filter_ok(kinds[i], filter) && k++ == pick) return i;
    return -1;
}

/* ---------------- asset packs ---------------- */

bool pack_name_ok(const char *n)
{
    size_t len = n ? strlen(n) : 0;
    if (!len || len > PACK_NAME_MAX || n[0] == '/' || n[0] == '.' || n[len - 1] == '/') return false;
    for (size_t i = 0; i < len; i++) {
        char c = n[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/';
        if (!ok) return false;
        if (c == '/' && (n[i + 1] == '/' || n[i + 1] == '.')) return false;   // no "//", no hidden or ".." parts
    }
    return true;
}

enum { PK_MAGIC, PK_NLEN, PK_NAME, PK_SIZE, PK_DATA, PK_END, PK_BAD };

void pack_reader_init(pack_reader_t *r) { memset(r, 0, sizeof(*r)); r->st = PK_MAGIC; }

bool pack_feed(pack_reader_t *r, const uint8_t *b, size_t n, const pack_cb_t *cb, void *ctx)
{
    size_t i = 0;
    while (i < n) {
        switch (r->st) {
        case PK_MAGIC:
            r->hdr[r->hn++] = b[i++];
            if (r->hn == 4) {
                if (memcmp(r->hdr, "AMP1", 4)) { r->st = PK_BAD; return false; }
                r->hn = 0; r->st = PK_NLEN;
            }
            break;
        case PK_NLEN:
            r->hdr[r->hn++] = b[i++];
            if (r->hn == 2) {
                r->nlen = rd16(r->hdr);
                r->hn = 0;
                if (r->nlen == 0) { r->st = PK_END; r->done = true; break; }
                if (r->nlen > PACK_NAME_MAX) { r->st = PK_BAD; return false; }
                r->st = PK_NAME;
            }
            break;
        case PK_NAME:
            r->name[r->hn++] = (char)b[i++];
            if (r->hn == r->nlen) {
                r->name[r->hn] = 0;
                r->hn = 0;
                if (!pack_name_ok(r->name)) { r->st = PK_BAD; return false; }
                r->st = PK_SIZE;
            }
            break;
        case PK_SIZE:
            r->hdr[r->hn++] = b[i++];
            if (r->hn == 4) {
                r->left = rd32(r->hdr);
                r->hn = 0;
                if (!cb->open(ctx, r->name, r->left)) { r->st = PK_BAD; return false; }
                r->st = PK_DATA;
                if (!r->left) { if (!cb->close(ctx)) { r->st = PK_BAD; return false; } r->files++; r->st = PK_NLEN; }
            }
            break;
        case PK_DATA: {
            size_t k = n - i < r->left ? n - i : r->left;
            if (!cb->write(ctx, b + i, k)) { r->st = PK_BAD; return false; }
            i += k;
            r->left -= (uint32_t)k;
            if (!r->left) {
                if (!cb->close(ctx)) { r->st = PK_BAD; return false; }
                r->files++;
                r->st = PK_NLEN;
            }
            break;
        }
        case PK_END: return false;   // bytes after the end marker
        default: return false;
        }
    }
    return true;
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

/* ---------------- calendar ---------------- */

#define Y_MIN 1900
#define Y_MAX 2199

bool date_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int date_days_in_month(int y, int m)
{
    static const uint8_t n[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 30;
    return m == 2 && date_leap(y) ? 29 : n[m - 1];
}

date_t date_clamp(date_t d)
{
    if (d.y < Y_MIN) d = (date_t){Y_MIN, 1, 1};
    if (d.y > Y_MAX) d = (date_t){Y_MAX, 12, 31};
    if (d.m < 1) d.m = 1;
    if (d.m > 12) d.m = 12;
    if (d.d < 1) d.d = 1;
    int n = date_days_in_month(d.y, d.m);
    if (d.d > n) d.d = (uint8_t)n;
    return d;
}

// days from civil (H. Hinnant's algorithm)
int32_t date_to_days(date_t d)
{
    int y = d.y - (d.m <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (d.m + (d.m > 2 ? -3 : 9)) + 2) / 5 + d.d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

date_t date_from_days(int32_t z)
{
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int d = doy - (153 * mp + 2) / 5 + 1;
    int m = mp < 10 ? mp + 3 : mp - 9;
    return (date_t){(int16_t)(y + (m <= 2)), (uint8_t)m, (uint8_t)d};
}

int date_weekday(date_t d)
{
    int32_t w = (date_to_days(d) + 3) % 7;   // 1970-01-01 was a Thursday
    return w < 0 ? w + 7 : w;
}

int date_yday(date_t d) { return date_to_days(d) - date_to_days((date_t){d.y, 1, 1}) + 1; }

int date_iso_week(date_t d)
{
    // the week belongs to the year of its Thursday
    date_t th = date_from_days(date_to_days(d) - date_weekday(d) + 3);
    return (date_yday(th) - 1) / 7 + 1;
}

date_t date_add_days(date_t d, int n) { return date_clamp(date_from_days(date_to_days(d) + n)); }

date_t date_add_months(date_t d, int n)
{
    int m = d.y * 12 + (d.m - 1) + n;
    d.y = (int16_t)(m / 12);
    d.m = (uint8_t)(m % 12 + 1);
    return date_clamp(d);
}

date_t date_add_years(date_t d, int n)
{
    d.y = (int16_t)(d.y + n);
    return date_clamp(d);
}

date_t date_easter(int y)
{
    // anonymous Gregorian algorithm (Meeus/Jones/Butcher)
    int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30, i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451;
    int month = (h + l - 7 * m + 114) / 31, day = (h + l - 7 * m + 114) % 31 + 1;
    return (date_t){(int16_t)y, (uint8_t)month, (uint8_t)day};
}

bool date_holiday_it(date_t d)
{
    static const uint8_t fixed[][2] = {
        {1, 1}, {1, 6}, {4, 25}, {5, 1}, {6, 2}, {8, 15}, {11, 1}, {12, 8}, {12, 25}, {12, 26},
    };
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++)
        if (d.m == fixed[i][0] && d.d == fixed[i][1]) return true;
    if (d.y >= 2026 && d.m == 10 && d.d == 4) return true;   // national holiday again from 2026
    int32_t e = date_to_days(date_easter(d.y)), t = date_to_days(d);
    return t == e || t == e + 1;   // Easter and the Monday after
}

date_t date_next_holiday_it(date_t d)
{
    for (int i = 1; i <= 400; i++) {
        date_t n = date_add_days(d, i);
        if (date_holiday_it(n)) return n;
    }
    return d;
}

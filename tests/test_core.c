// Native tests of main/core.c:  cd tests && make
#include "core.h"
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static void test_kind(void)
{
    CHECK(media_kind("a.jpg") == MEDIA_IMAGE);
    CHECK(media_kind("A.JPEG") == MEDIA_IMAGE);
    CHECK(media_kind("clip.amj") == MEDIA_ANIM);
    CHECK(media_kind("clip.AMJ") == MEDIA_ANIM);
    CHECK(media_kind("x.png") == MEDIA_NONE);
    CHECK(media_kind("noext") == MEDIA_NONE);
    CHECK(media_kind(".jpg") == MEDIA_NONE);
    CHECK(media_kind("a.jpgxx") == MEDIA_NONE);
    CHECK(media_kind(NULL) == MEDIA_NONE);
}

static void test_amj(void)
{
    uint8_t b[16 + 2 * 12] = "AMJ1";
    b[4] = 466 & 0xFF; b[5] = 466 >> 8; b[6] = 466 & 0xFF; b[7] = 466 >> 8;
    put32(b + 8, 2);
    put32(b + 16, 40); put32(b + 20, 100); put32(b + 24, 66);
    put32(b + 28, 140); put32(b + 32, 60); put32(b + 36, 0);
    amj_info_t i;
    CHECK(amj_parse_header(b, sizeof(b), 200, &i));
    CHECK(i.width == 466 && i.height == 466 && i.frames == 2);
    CHECK(!amj_parse_header(b, sizeof(b), 30, &i));       // index does not fit the file
    CHECK(!amj_parse_header(b, 10, 200, &i));             // short buffer
    amj_frame_t f;
    CHECK(amj_entry(b + 16, 0, 200, &f) && f.offset == 40 && f.size == 100 && f.delay_ms == 66);
    CHECK(amj_entry(b + 16, 1, 200, &f) && f.delay_ms == 10);   // 0 ms -> 10 ms
    CHECK(!amj_entry(b + 16, 1, 150, &f));                      // frame past the end
    b[0] = 'X';
    CHECK(!amj_parse_header(b, sizeof(b), 200, &i));
    b[0] = 'A';
    put32(b + 8, 0);
    CHECK(!amj_parse_header(b, sizeof(b), 200, &i));
}

static void test_name(void)
{
    char o[48];
    CHECK(media_safe_name("My Photo.jpg", o, sizeof(o)) && !strcmp(o, "My_Photo"));
    CHECK(media_safe_name("a.b.c.gif", o, sizeof(o)) && !strcmp(o, "a_b_c"));
    CHECK(media_safe_name("caff\xc3\xa8 & amici", o, sizeof(o)) && !strcmp(o, "caff_amici"));
    CHECK(media_safe_name("../../etc/passwd", o, sizeof(o)) && !strcmp(o, "etc_passwd"));
    CHECK(!media_safe_name("...", o, sizeof(o)) && o[0] == 0);
    CHECK(!media_safe_name("", o, sizeof(o)));
    char longn[100];
    memset(longn, 'x', 99); longn[99] = 0;
    CHECK(media_safe_name(longn, o, sizeof(o)) && strlen(o) == MEDIA_NAME_MAX);
    char tiny[4];
    CHECK(media_safe_name("abcdef", tiny, sizeof(tiny)) && !strcmp(tiny, "abc"));
}

static void test_misc(void)
{
    CHECK(version_compare("0.1.10", "0.1.9") > 0);
    CHECK(version_compare("v0.2.0", "0.1.99") > 0);
    CHECK(version_compare("0.1.5", "0.1.5-3-gabc") == 0);
    CHECK(version_compare("0.1.4", "0.1.5") < 0);
    CHECK(night_active(23, 23, 7) && night_active(3, 23, 7) && !night_active(7, 23, 7) && !night_active(12, 23, 7));
    CHECK(night_active(14, 13, 15) && !night_active(15, 13, 15));
    CHECK(!night_active(5, 5, 5));
    char v[32];
    CHECK(query_param("name=My%20cat&kind=thumb", "name", v, sizeof(v)) && !strcmp(v, "My cat"));
    CHECK(query_param("name=My%20cat&kind=thumb", "kind", v, sizeof(v)) && !strcmp(v, "thumb"));
    CHECK(!query_param("name=x", "kin", v, sizeof(v)));
    CHECK(!query_param("names=x", "name", v, sizeof(v)));
    CHECK(query_param("a=1+2&b=%2", "b", v, sizeof(v)) && !strcmp(v, "%2"));
    CHECK(query_param("a=1+2", "a", v, sizeof(v)) && !strcmp(v, "1 2"));
    uint32_t seed = 1;
    for (int i = 0; i < 200; i++) {
        int n = shuffle_next(i % 5, 5, &seed);
        CHECK(n >= 0 && n < 5 && n != i % 5);
    }
    CHECK(shuffle_next(0, 1, &seed) == 0);
}

/* ---- dice ---- */
typedef struct { const uint32_t *v; int n, i; } seq_t;   // fixed sequence of "random" words
static uint32_t seq_rng(void *c) { seq_t *s = c; return s->v[s->i++ % s->n]; }
static uint32_t lcg_rng(void *c) { uint32_t *x = c; *x = *x * 1664525u + 1013904223u; return *x; }

static void test_dice(void)
{
    // dice_one: value % sides + 1, and the biased top range is rejected
    uint32_t v1[] = {0, 19, 20, UINT32_MAX, 5};
    seq_t q = {v1, 5, 0};
    CHECK(dice_one(20, seq_rng, &q) == 1);
    CHECK(dice_one(20, seq_rng, &q) == 20);
    CHECK(dice_one(20, seq_rng, &q) == 1);
    CHECK(dice_one(20, seq_rng, &q) == 6);   // UINT32_MAX skipped, then 5 -> 6
    // fairness: 120000 d6 rolls, every face 20000 +- 3 %
    uint32_t x = 7;
    int hist[7] = {0};
    for (int i = 0; i < 120000; i++) hist[dice_one(6, lcg_rng, &x)]++;
    for (int f = 1; f <= 6; f++) CHECK(hist[f] > 19400 && hist[f] < 20600);

    dice_result_t r;
    // advantage keeps the higher, disadvantage the lower
    uint32_t v2[] = {4, 16};   // -> 5 and 17
    seq_t a = {v2, 2, 0};
    dice_spec_t s = {1, 20, 2, DICE_ADV, 0};
    dice_roll(&s, seq_rng, &a, &r);
    CHECK(r.n == 2 && r.val[0] == 5 && r.val[1] == 17 && !r.kept[0] && r.kept[1] && r.total == 19);
    a.i = 0;
    s.mode = DICE_DIS;
    dice_roll(&s, seq_rng, &a, &r);
    CHECK(r.kept[0] && !r.kept[1] && r.total == 7);
    // natural 20 / natural 1
    uint32_t v3[] = {19};
    seq_t c = {v3, 1, 0};
    s = (dice_spec_t){1, 20, -5, DICE_NORMAL, 0};
    dice_roll(&s, seq_rng, &c, &r);
    CHECK(r.crit && !r.fumble && r.total == 15);
    uint32_t v4[] = {0};
    seq_t f = {v4, 1, 0};
    dice_roll(&s, seq_rng, &f, &r);
    CHECK(r.fumble && !r.crit && r.total == -4);
    // a d6 showing 1 or 6 is no crit
    s.sides = 6;
    f.i = 0;
    dice_roll(&s, seq_rng, &f, &r);
    CHECK(!r.fumble && !r.crit);
    // 4d6 drop the lowest
    uint32_t v5[] = {2, 0, 5, 3};   // 3 1 6 4 -> 13
    seq_t d = {v5, 4, 0};
    s = (dice_spec_t){4, 6, 0, DICE_NORMAL, 1};
    dice_roll(&s, seq_rng, &d, &r);
    CHECK(r.total == 13 && !r.kept[1] && r.kept[0] && r.kept[2] && r.kept[3]);
    char b[64];
    dice_breakdown(&r, b, sizeof(b));
    CHECK(!strcmp(b, "3 (1) 6 4"));
    dice_format(&s, b, sizeof(b));
    CHECK(!strcmp(b, "4d6kh3"));
    s = (dice_spec_t){2, 20, 3, 0, 0};
    dice_format(&s, b, sizeof(b));
    CHECK(!strcmp(b, "2d20+3"));
    s = (dice_spec_t){1, 100, -1, 0, 0};
    dice_format(&s, b, sizeof(b));
    CHECK(!strcmp(b, "d100-1"));
    // clamping and the biggest roll
    s = dice_clamp((dice_spec_t){0, 1, 0, 9, 3});
    CHECK(s.count == 1 && s.sides == 2 && s.mode == 0 && s.drop_lowest == 0);
    s = (dice_spec_t){50, 20, 0, DICE_ADV, 0};
    x = 3;
    dice_roll(&s, lcg_rng, &x, &r);
    CHECK(r.n == 2 * DICE_MAX);
    int kept = 0;
    for (int i = 0; i < r.n; i++) kept += r.kept[i];
    CHECK(kept == DICE_MAX && r.total >= 20 && r.total <= 400);
    dice_breakdown(&r, b, 16);   // short buffer: stays terminated
    CHECK(strlen(b) < 16);
    CHECK(dice_ability_mod(10) == 0 && dice_ability_mod(18) == 4 && dice_ability_mod(9) == -1 &&
          dice_ability_mod(8) == -1 && dice_ability_mod(3) == -4 && dice_ability_mod(20) == 5);
}

static void test_daggerheart(void)
{
    dice_result_t r;
    char b[32];
    // hope 8, fear 3 (values % 12 + 1): with Hope
    uint32_t v1[] = {7, 2};
    seq_t a = {v1, 2, 0};
    dice_spec_t s = {1, 20, 2, DICE_DH, 0};
    CHECK(dice_is_dh(&s));
    dice_roll(&s, seq_rng, &a, &r);
    CHECK(r.hope == 8 && r.fear == 3 && r.dh == DH_HOPE && !r.crit && r.total == 13 && r.n == 2);
    // fear wins
    uint32_t v2[] = {0, 10};
    seq_t b2 = {v2, 2, 0};
    dice_roll(&s, seq_rng, &b2, &r);
    CHECK(r.dh == DH_FEAR && r.total == 1 + 11 + 2);
    // doubles: critical
    uint32_t v3[] = {5, 5};
    seq_t c = {v3, 2, 0};
    dice_roll(&s, seq_rng, &c, &r);
    CHECK(r.dh == DH_CRIT && r.crit && r.hope == 6 && r.fear == 6);
    // advantage adds a d6, disadvantage takes it away
    uint32_t v4[] = {7, 2, 3};   // 8, 3, d6 = 4
    seq_t d = {v4, 3, 0};
    s.mode = DICE_DH_ADV;
    dice_roll(&s, seq_rng, &d, &r);
    CHECK(r.d6 == 4 && r.n == 3 && r.total == 8 + 3 + 4 + 2);
    d.i = 0;
    s.mode = DICE_DH_DIS;
    dice_roll(&s, seq_rng, &d, &r);
    CHECK(r.total == 8 + 3 - 4 + 2 && r.dh == DH_HOPE);
    dice_format(&s, b, sizeof(b));
    CHECK(!strcmp(b, "2d12-d6+2"));
    s.mode = DICE_DH;
    s.mod = 0;
    dice_format(&s, b, sizeof(b));
    CHECK(!strcmp(b, "2d12"));
    // a normal roll says "no Daggerheart"
    s = (dice_spec_t){1, 20, 0, DICE_NORMAL, 0};
    dice_roll(&s, seq_rng, &d, &r);
    CHECK(r.dh == DH_NONE && !dice_is_dh(&s));
    s.mode = 9;
    CHECK(dice_clamp(s).mode == DICE_NORMAL);
}

static void test_battery(void)
{
    CHECK(battery_percent_from_volts(4.20f) == 100);
    CHECK(battery_percent_from_volts(4.09f) == 94);
    CHECK(battery_percent_from_volts(3.78f) == 50);
    CHECK(battery_percent_from_volts(3.30f) == 0 && battery_percent_from_volts(3.0f) == 0);
    CHECK(battery_percent_from_volts(0) == 0);
    for (float v = 3.2f; v < 4.25f; v += 0.01f)   // never goes down while the voltage goes up
        CHECK(battery_percent_from_volts(v + 0.01f) >= battery_percent_from_volts(v));
    CHECK(battery_percent_pick(0, 4.09f, false, NULL) == 94);    // new battery, gauge not learned yet
    CHECK(battery_percent_pick(-1, 3.78f, false, NULL) == 50);   // no gauge reading
    CHECK(battery_percent_pick(85, 4.09f, false, NULL) == 85);   // gauge plausible: the gauge wins
    CHECK(battery_percent_pick(40, 4.15f, true, NULL) == 40);    // charging: high voltage is expected
    CHECK(battery_percent_pick(5, 3.50f, false, NULL) == 5);     // really low: the gauge wins
    CHECK(battery_percent_pick(70, 0, false, NULL) == 70);       // no voltage
    CHECK(battery_percent_pick(0, 4.12f, true, NULL) == 97);     // charging, gauge stuck at zero
    bool e = false;
    CHECK(battery_percent_pick(85, 4.09f, false, &e) == 85 && !e);   // the gauge: not estimated
    CHECK(battery_percent_pick(0, 4.09f, false, &e) == 94 && e);     // the estimate: flagged
    CHECK(battery_percent_pick(70, 4.09f, false, &e) == 94 && e);    // 24 apart: still the estimate
    CHECK(battery_percent_pick(82, 4.09f, false, &e) == 82 && !e);   // within 15: the gauge again
    e = false;
    CHECK(battery_percent_pick(70, 4.09f, false, &e) == 70 && !e);   // 24 apart from the gauge: kept
    e = true;
    CHECK(battery_percent_pick(40, 4.15f, true, &e) == 40 && !e);    // charging: the gauge
    CHECK(battery_percent_pick(-1, 3.78f, false, &e) == 50 && e);    // no gauge: estimated
    CHECK(battery_percent_pick(70, 0, false, &e) == 70 && !e);       // no voltage
}

int main(void)
{
    test_battery();
    test_daggerheart();
    test_dice();
    test_kind();
    test_amj();
    test_name();
    test_misc();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}

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

int main(void)
{
    test_kind();
    test_amj();
    test_name();
    test_misc();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}

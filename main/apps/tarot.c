// tarot.c - tarot cards in the neon style, from the "tarot" asset pack on the microSD
// (tools/packs/tarot.py draws them; packs.c downloads the pack the first time).
//
// Deck: the back of the cards. Tap or shake: draw (one card, or three for past / present /
// future: swipe up / down switches). A card may come out reversed (long press on the deck
// turns reversed cards on / off). On a card: tap for its meaning; swipe right or long press
// goes back. In the spread: tap a card to see it, tap elsewhere for the reading (each position
// with its card, upright or reversed, and the meaning of the side that came out). BOOT leaves.
#include "ui.h"
#include "board.h"
#include "core.h"
#include "media.h"
#include "packs.h"
#include "power.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_random.h"

#define PACK      "tarot"
#define PACK_VER  "1"      // bump together with tools/packs/tarot.py when the cards change
#define N_CARDS   78
#define THUMB     128
#define FIELDS    7        // file | name it | name en | upright it | en | reversed it | en

typedef enum { ST_DOWNLOAD, ST_DECK, ST_CARD, ST_SPREAD } state_t;
typedef struct { int card; bool rev; } drawn_t;

static const char *const roman[22] = {"0", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI",
                                      "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX", "XXI"};
static const int spread_xy[3][2] = {{108, 250}, {233, 168}, {358, 250}};
static const int spread_lbl[3] = {S_TAROT_PAST, S_TAROT_PRESENT, S_TAROT_FUTURE};

static char *text_buf;
static char *field[N_CARDS][FIELDS];
static bool text_ok;

static uint16_t *full, *thumb[3];          // never freed: the images point at them
static lv_image_dsc_t full_dsc, thumb_dsc[3];
static lv_obj_t *img, *thumb_img[3], *thumb_lbl[3], *thumb_rev[3], *top_l, *hint_l, *dl_icon, *dl_title, *dl_text;
static lv_timer_t *shake_t;
static state_t st;
static drawn_t drawn[3];
static int shown = -1;                      // drawn[] index on the full image (-1: the back)
static bool from_spread, keep;

static uint32_t rng(void *c) { (void)c; return esp_random(); }

/* ---------------- files ---------------- */

static bool load_text(void)
{
    if (text_ok) return true;
    char p[96];
    packs_path(PACK, "cards.txt", p, sizeof(p));
    int len = 0;
    uint8_t *b = media_read_file(p, &len, 64 * 1024);
    if (!b) return false;
    free(text_buf);
    text_buf = malloc(len + 1);   // with the terminating 0
    if (!text_buf) { free(b); return false; }
    memcpy(text_buf, b, len);
    free(b);
    text_buf[len] = 0;
    int n = 0;
    for (char *line = strtok(text_buf, "\n"); line && n < N_CARDS; line = strtok(NULL, "\n")) {
        if (line[0] == '#') continue;
        char *f = line;
        int k = 0;
        for (; k < FIELDS; k++) {
            field[n][k] = f;
            char *bar = strchr(f, '|');
            if (!bar) { k++; break; }
            *bar = 0;
            f = bar + 1;
        }
        if (k == FIELDS) n++;
    }
    text_ok = n == N_CARDS;
    return text_ok;
}

static const char *card_name(int c) { return text_ok ? field[c][g_set.lang == LANG_IT ? 1 : 2] : ""; }

static void dsc_init(lv_image_dsc_t *d, uint16_t *buf, int sz)
{
    *d = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = sz, .h = sz, .stride = sz * 2},
        .data_size = sz * sz * 2,
        .data = (const uint8_t *)buf,
    };
}

// a reversed card is the same picture turned by 180 degrees
static void turn(uint16_t *b, int n)
{
    for (int i = 0, j = n - 1; i < j; i++, j--) { uint16_t t = b[i]; b[i] = b[j]; b[j] = t; }
}

static bool load_image(const char *file, uint16_t *out, int sz, bool rev)
{
    char p[96];
    packs_path(PACK, file, p, sizeof(p));
    int len = 0;
    power_boost_begin();
    uint8_t *j = media_read_file(p, &len, 512 * 1024);
    bool ok = j && media_decode(j, len, out, sz, sz);
    free(j);
    if (ok && rev) turn(out, sz * sz);
    power_boost_end();
    if (!ok) memset(out, 0, sz * sz * 2);
    return ok;
}

static void card_file(int c, bool small, char *b, int n) { snprintf(b, n, "%s/%02d.jpg", small ? "thumbs" : "cards", c); }

/* ---------------- states ---------------- */

static void fade_cb(void *o, int32_t v) { lv_obj_set_style_image_opa(o, (lv_opa_t)v, 0); }

static void fade_in(lv_obj_t *o)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_duration(&a, 260);
    lv_anim_set_exec_cb(&a, fade_cb);
    lv_anim_start(&a);
}

static void show(state_t s)
{
    st = s;
    bool dl = s == ST_DOWNLOAD;
    lv_obj_update_flag(dl_icon, LV_OBJ_FLAG_HIDDEN, !dl);
    lv_obj_update_flag(dl_title, LV_OBJ_FLAG_HIDDEN, !dl);
    lv_obj_update_flag(dl_text, LV_OBJ_FLAG_HIDDEN, !dl);
    lv_obj_update_flag(img, LV_OBJ_FLAG_HIDDEN, !(s == ST_DECK || s == ST_CARD));
    for (int k = 0; k < 3; k++) {
        lv_obj_update_flag(thumb_img[k], LV_OBJ_FLAG_HIDDEN, s != ST_SPREAD);
        lv_obj_update_flag(thumb_lbl[k], LV_OBJ_FLAG_HIDDEN, s != ST_SPREAD);
        lv_obj_update_flag(thumb_rev[k], LV_OBJ_FLAG_HIDDEN, !(s == ST_SPREAD && drawn[k].rev));
    }
    // a reversed card is upside down: an upright word says so
    bool rev_card = s == ST_CARD && shown >= 0 && drawn[shown].rev;
    lv_obj_update_flag(top_l, LV_OBJ_FLAG_HIDDEN, s != ST_DECK);
    lv_obj_update_flag(hint_l, LV_OBJ_FLAG_HIDDEN, !(s == ST_DECK || s == ST_SPREAD || rev_card));
    lv_obj_set_style_text_color(hint_l, rev_card ? ui_accent() : C_TEXT, 0);
    if (s == ST_DECK) {
        ui_set_text(top_l, T(g_set.tarot_three ? S_TAROT_THREE : S_TAROT_ONE));
        ui_set_text(hint_l, T(S_TAROT_HINT));
    } else if (s == ST_SPREAD) {
        ui_set_text(hint_l, T(S_TAROT_READ_HINT));
    } else if (rev_card) {
        ui_set_text(hint_l, T(S_TAROT_REVERSED));
    }
    lv_obj_align(hint_l, LV_ALIGN_CENTER, 0, 150);
}

static void show_back(void)
{
    shown = -1;
    load_image("back.jpg", full, SCR_W, false);
    lv_obj_invalidate(img);
    show(ST_DECK);
}

static void show_card(int k)
{
    char f[24];
    card_file(drawn[k].card, false, f, sizeof(f));
    if (!load_image(f, full, SCR_W, drawn[k].rev)) ui_toast(T(S_TAROT_BROKEN));
    shown = k;
    show(ST_CARD);
    lv_obj_invalidate(img);
    fade_in(img);
}

static void draw_cards(void)
{
    int n = g_set.tarot_three ? 3 : 1;
    for (int k = 0; k < n; k++) {
        bool again;
        do {   // three different cards
            drawn[k].card = dice_one(N_CARDS, rng, NULL) - 1;
            again = false;
            for (int j = 0; j < k; j++) again |= drawn[j].card == drawn[k].card;
        } while (again);
        drawn[k].rev = g_set.tarot_reversed && (esp_random() & 1);
    }
    input_mark_activity();
    if (n == 1) { from_spread = false; show_card(0); return; }
    for (int k = 0; k < 3; k++) {
        char f[24];
        card_file(drawn[k].card, true, f, sizeof(f));
        load_image(f, thumb[k], THUMB, drawn[k].rev);
        lv_obj_invalidate(thumb_img[k]);
    }
    show(ST_SPREAD);
    for (int k = 0; k < 3; k++) fade_in(thumb_img[k]);
}

static void update_download(void)
{
    char b[96];
    pack_job_t j = packs_job();
    if (j == PACKJ_RUNNING) snprintf(b, sizeof(b), T(S_TAROT_DL_RUN), packs_progress());
    else if (j == PACKJ_FAILED) snprintf(b, sizeof(b), "%s\n%s", packs_error(), T(S_TAROT_DL_RETRY));
    else snprintf(b, sizeof(b), "%s", T(S_TAROT_DL_TEXT));
    ui_set_text(dl_text, b);
}

static bool ready(void) { return packs_ready(PACK, PACK_VER) && load_text(); }

/* ---------------- shake ---------------- */

static void shake_cb(lv_timer_t *t)
{
    static uint32_t first, cooldown;
    static int hits;
    vec3_t a;
    if (st == ST_DOWNLOAD || !board_imu_accel(&a)) return;
    uint32_t now = lv_tick_get();
    if ((int32_t)(now - cooldown) < 0) return;
    float g = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    if (fabsf(g - 1.0f) < 0.9f) return;
    if (!hits || now - first > 600) { hits = 1; first = now; return; }
    if (++hits >= 2) { hits = 0; cooldown = now + 1500; draw_cards(); }
}

/* ---------------- screen ---------------- */

static void enter(lv_obj_t *root, void *arg)
{
    if (!full) { full = media_frame_alloc(SCR_W, SCR_H); dsc_init(&full_dsc, full, SCR_W); }
    for (int k = 0; k < 3; k++)
        if (!thumb[k]) { thumb[k] = media_frame_alloc(THUMB, THUMB); dsc_init(&thumb_dsc[k], thumb[k], THUMB); }
    if (!full || !thumb[0] || !thumb[1] || !thumb[2]) { ui_toast(T(S_ERR_MEMORY)); return; }

    img = lv_image_create(root);
    lv_image_set_src(img, &full_dsc);
    lv_obj_set_pos(img, 0, 0);
    for (int k = 0; k < 3; k++) {
        thumb_img[k] = lv_image_create(root);
        lv_image_set_src(thumb_img[k], &thumb_dsc[k]);
        lv_obj_set_pos(thumb_img[k], spread_xy[k][0] - THUMB / 2, spread_xy[k][1] - THUMB / 2);
        thumb_lbl[k] = ui_label(root, &font_xs, C_DIM, T(spread_lbl[k]));
        lv_obj_align(thumb_lbl[k], LV_ALIGN_TOP_MID, spread_xy[k][0] - SCR_C, spread_xy[k][1] + THUMB / 2 + 6);
        thumb_rev[k] = ui_label(root, &font_xs, ui_accent(), T(S_TAROT_REV_SHORT));
        lv_obj_align(thumb_rev[k], LV_ALIGN_TOP_MID, spread_xy[k][0] - SCR_C, spread_xy[k][1] + THUMB / 2 + 26);
    }
    top_l = ui_label(root, &font_xs, C_TEXT, "");
    lv_obj_align(top_l, LV_ALIGN_CENTER, 0, -150);
    hint_l = ui_label(root, &font_xs, C_TEXT, "");
    lv_obj_set_style_text_align(hint_l, LV_TEXT_ALIGN_CENTER, 0);
    for (int k = 0; k < 2; k++) {   // black pills: readable over the back of the cards
        lv_obj_t *l = k ? hint_l : top_l;
        lv_obj_set_style_bg_color(l, C_BG, 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(l, 12, 0);
        lv_obj_set_style_pad_ver(l, 4, 0);
        lv_obj_set_style_radius(l, 14, 0);
    }
    dl_icon = ui_label(root, &font_icon, ui_accent(), IC_DOWNLOAD);
    lv_obj_align(dl_icon, LV_ALIGN_CENTER, 0, -100);
    dl_title = ui_label(root, &font_m, C_TEXT, T(S_TAROT_DL_TITLE));
    lv_obj_align(dl_title, LV_ALIGN_CENTER, 0, -24);
    dl_text = ui_label(root, &font_s, C_DIM, "");
    lv_obj_set_style_text_align(dl_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(dl_text, 360);
    lv_label_set_long_mode(dl_text, LV_LABEL_LONG_WRAP);
    lv_obj_align(dl_text, LV_ALIGN_CENTER, 0, 52);
    shake_t = lv_timer_create(shake_cb, 40, NULL);

    if (!ready()) { update_download(); show(ST_DOWNLOAD); return; }
    if (keep && st == ST_SPREAD) {   // back from the reading: the previews are still there
        keep = false;
        show(ST_SPREAD);
        return;
    }
    if (keep && st == ST_CARD && shown >= 0) {   // back from the meaning page
        keep = false;
        int k = shown;
        char f[24];
        card_file(drawn[k].card, false, f, sizeof(f));
        load_image(f, full, SCR_W, drawn[k].rev);
        show(ST_CARD);
        return;
    }
    keep = false;
    show_back();
}

static void leave(void)
{
    if (shake_t) { lv_timer_delete(shake_t); shake_t = NULL; }
    lv_anim_delete(img, fade_cb);
    for (int k = 0; k < 3; k++) lv_anim_delete(thumb_img[k], fade_cb);
}

static void tick(void)
{
    if (st != ST_DOWNLOAD) return;
    update_download();
    if (packs_job() == PACKJ_DONE && ready()) show_back();
}

static void back_from_card(void)
{
    if (from_spread) show(ST_SPREAD);
    else show_back();
}

static bool event(ev_t e, int x, int y)
{
    switch (st) {
    case ST_DOWNLOAD:
        if (e == EV_TAP && packs_job() != PACKJ_RUNNING) { packs_install(PACK, PACK_VER); update_download(); }
        return e == EV_TAP;
    case ST_DECK:
        if (e == EV_TAP) { draw_cards(); return true; }
        if (e == EV_SWIPE_UP || e == EV_SWIPE_DOWN) {
            g_set.tarot_three = !g_set.tarot_three;
            settings_mark();
            show(ST_DECK);
            return true;
        }
        if (e == EV_LONG_PRESS) {
            g_set.tarot_reversed = !g_set.tarot_reversed;
            settings_mark();
            ui_toast(T(g_set.tarot_reversed ? S_TAROT_REV_ON : S_TAROT_REV_OFF));
            return true;
        }
        return false;
    case ST_CARD:
        if (e == EV_TAP) { keep = true; ui_push(&scr_tarot_meaning, &drawn[shown]); return true; }
        if (e == EV_SWIPE_RIGHT || e == EV_LONG_PRESS) { back_from_card(); return true; }
        return false;
    case ST_SPREAD:
        if (e == EV_TAP) {
            for (int k = 0; k < 3; k++) {
                int dx = x - spread_xy[k][0], dy = y - spread_xy[k][1];
                if (dx * dx + dy * dy <= (THUMB / 2 + 12) * (THUMB / 2 + 12)) { from_spread = true; show_card(k); return true; }
            }
            keep = true;
            ui_push(&scr_tarot_reading, drawn);
            return true;
        }
        if (e == EV_SWIPE_RIGHT || e == EV_LONG_PRESS) { show_back(); return true; }
        return false;
    }
    return false;
}

const screen_t scr_tarot = {"tarot", enter, leave, event, tick, SCR_NO_STATUS};

/* ---------------- the meaning of a card ---------------- */

static void m_enter(lv_obj_t *root, void *arg)
{
    const drawn_t *d = arg;
    int c = d->card, it = g_set.lang == LANG_IT;
    lv_obj_t *num = ui_label(root, &font_xs, C_DIM, c < 22 ? roman[c] : "");
    lv_obj_align(num, LV_ALIGN_CENTER, 0, -150);
    lv_obj_t *name = ui_label(root, &font_m, C_TEXT, card_name(c));
    lv_obj_align(name, LV_ALIGN_CENTER, 0, -112);
    // the side the card fell on comes first and lit, the other one dimmed below
    for (int k = 0; k < 2; k++) {
        bool rev = k == 0 ? d->rev : !d->rev;
        lv_obj_t *h = ui_label(root, &font_xs, k == 0 ? ui_accent() : C_FAINT, T(rev ? S_TAROT_REVERSED : S_TAROT_UPRIGHT));
        lv_obj_align(h, LV_ALIGN_CENTER, 0, k == 0 ? -58 : 44);
        lv_obj_t *t = ui_label(root, k == 0 ? &font_s : &font_xs, k == 0 ? C_TEXT : C_DIM, text_ok ? field[c][rev ? (it ? 5 : 6) : (it ? 3 : 4)] : "");
        lv_obj_set_width(t, k == 0 ? 340 : 300);
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, SCR_C + (k == 0 ? -38 : 62));
    }
}

static bool m_event(ev_t e, int x, int y)
{
    if (e == EV_TAP) { ui_pop(); return true; }
    return false;
}

const screen_t scr_tarot_meaning = {"tarot-meaning", m_enter, NULL, m_event, NULL, SCR_NO_STATUS};

/* ---------------- the reading of a three-card spread ---------------- */

static void r_enter(lv_obj_t *root, void *arg)
{
    const drawn_t *d = arg;
    int it = g_set.lang == LANG_IT;
    for (int k = 0; k < 3; k++) {
        int y0 = -150 + k * 100, c = d[k].card;
        lv_obj_t *pos = ui_label(root, &font_xs, ui_accent(), T(spread_lbl[k]));
        lv_obj_align(pos, LV_ALIGN_CENTER, 0, y0);
        char b[96];
        if (d[k].rev) snprintf(b, sizeof(b), "%s, %s", card_name(c), T(S_TAROT_REV_SHORT));
        else snprintf(b, sizeof(b), "%s", card_name(c));
        lv_obj_t *name = ui_label(root, &font_s, C_TEXT, b);
        ui_one_line(name, 330);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(name, LV_ALIGN_CENTER, 0, y0 + 26);
        lv_obj_t *t = ui_label(root, &font_xs, C_DIM, text_ok ? field[c][d[k].rev ? (it ? 5 : 6) : (it ? 3 : 4)] : "");
        ui_one_line(t, k == 0 ? 330 : 360);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(t, LV_ALIGN_CENTER, 0, y0 + 52);
    }
}

const screen_t scr_tarot_reading = {"tarot-reading", r_enter, NULL, m_event, NULL, SCR_NO_STATUS};

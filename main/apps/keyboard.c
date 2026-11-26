// keyboard.c - a QWERTY keyboard laid out inside the circle (rows get narrower towards the
// bottom). Tap a key; swipe left deletes; swipe right / BOOT cancels.
#include "ui.h"
#include "keyboard.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define KW 40
#define KH 46
#define GAP 3
#define MAXKEYS 40

#define PAGES 3
static const char *pages[PAGES][3] = {
    {"qwertyuiop", "asdfghjkl", "zxcvbnm"},
    {"1234567890", "-_.@#&*!?/", "()$%+=,"},
    {"[]{}<>^~|\\", "\";:'`", ""},
};
static const char *page_names[PAGES] = {"abc", "123", "#+="};

typedef struct { lv_obj_t *o, *l; char c; int kind; } key_t;   // kind: 0 char, 1 shift, 2 del, 3 page, 4 space, 5 ok
static key_t keys[MAXKEYS];
static int nkeys, page;
static bool shift;
static lv_obj_t *field, *title_l, *flash;
static uint32_t flash_until;
static char text[72];
static kb_arg_t *ka;

static key_t *add_key(lv_obj_t *root, int x, int y, int w, const char *label, char c, int kind)
{
    if (nkeys >= MAXKEYS) return NULL;
    key_t *k = &keys[nkeys++];
    k->o = lv_obj_create(root);
    lv_obj_remove_style_all(k->o);
    lv_obj_set_size(k->o, w, KH);
    lv_obj_set_pos(k->o, x, y);
    lv_obj_set_style_radius(k->o, 12, 0);
    lv_obj_set_style_bg_color(k->o, kind == 5 ? ui_accent() : C_CARD, 0);
    lv_obj_set_style_bg_opa(k->o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(k->o, LV_OBJ_FLAG_SCROLLABLE);
    k->l = ui_label(k->o, kind >= 3 && kind != 4 ? &font_s : &font_m, kind == 5 ? C_BG : C_TEXT, label);
    lv_obj_center(k->l);
    k->c = c;
    k->kind = kind;
    return k;
}

static void row(lv_obj_t *root, const char *chars, int y, bool with_shift)
{
    int n = strlen(chars), side = with_shift ? 2 : 0;
    int wide = 52;
    int total = n * (KW + GAP) - GAP + (side ? 2 * (wide + GAP) : 0);
    int x = SCR_C - total / 2;
    if (with_shift) {
        if (page == 0) add_key(root, x, y, wide, shift ? "ABC" : "abc", 0, 1);   // shift
        x += wide + GAP;
    }
    for (int i = 0; i < n; i++) {
        char s[2] = {chars[i], 0};
        if (shift && !page) s[0] = (char)toupper((unsigned char)s[0]);
        add_key(root, x, y, KW, s, chars[i], 0);
        x += KW + GAP;
    }
    if (with_shift) add_key(root, x, y, wide, IC_LEFT, 0, 2);
}

static void field_update(void)
{
    const char *t = text;
    int len = strlen(text);
    if (len > 16) t = text + len - 16;   // the end of what was typed stays visible
    char b[80];
    snprintf(b, sizeof(b), "%s%s|", len > 16 ? "\xE2\x80\xA6" : "", t);
    lv_label_set_text(field, b);
}

static void build(lv_obj_t *root)
{
    nkeys = 0;
    title_l = ui_label(root, &font_s, ui_accent(), ka->title);
    lv_obj_set_width(title_l, 300);
    lv_label_set_long_mode(title_l, LV_LABEL_LONG_DOT);
    lv_obj_align(title_l, LV_ALIGN_TOP_MID, 0, 56);
    field = ui_label(root, &font_m, C_TEXT, "");
    lv_obj_align(field, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_t *line = lv_obj_create(root);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 300, 2);
    lv_obj_set_style_bg_color(line, ui_accent_dark(), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 140);
    int y0 = 160;
    row(root, pages[page][0], y0, false);
    row(root, pages[page][1], y0 + KH + GAP, false);
    row(root, pages[page][2], y0 + 2 * (KH + GAP), true);
    int y = y0 + 3 * (KH + GAP);
    add_key(root, SCR_C - 168, y, 80, page_names[(page + 1) % PAGES], 0, 3);
    add_key(root, SCR_C - 84, y, 168, "", ' ', 4);
    add_key(root, SCR_C + 88, y, 80, IC_CHECK, 0, 5);
    flash = NULL;
    field_update();
}

static lv_obj_t *root_obj;

static void enter(lv_obj_t *root, void *arg)
{
    if (arg != ka) {   // a new keyboard (not a rebuild after shift / page)
        ka = arg;
        strlcpy(text, ka->initial ? ka->initial : "", sizeof(text));
        page = 0;
        shift = false;
    }
    root_obj = root;
    build(root);
}

static void relayout(void)
{
    lv_obj_clean(root_obj);
    build(root_obj);
}

static void press(key_t *k)
{
    int len = strlen(text);
    switch (k->kind) {
    case 0: case 4:
        if (len < (int)sizeof(text) - 1 && len < ka->maxlen) {
            text[len] = shift && !page && k->kind == 0 ? (char)toupper((unsigned char)k->c) : k->c;
            text[len + 1] = 0;
        }
        if (shift) { shift = false; relayout(); return; }
        break;
    case 1: shift = !shift; relayout(); return;
    case 2: if (len) text[len - 1] = 0; break;
    case 3: page = (page + 1) % PAGES; shift = false; relayout(); return;
    case 5: {
        kb_arg_t *a = ka;
        ka = NULL;
        ui_pop();
        if (a->done) a->done(text, a->user);
        return;
    }
    }
    field_update();
    lv_obj_set_style_bg_color(k->o, ui_accent_dark(), 0);
    flash = k->o;
    flash_until = lv_tick_get() + 140;
}

static bool event(ev_t e, int x, int y)
{
    if (e == EV_TAP) {
        for (int i = 0; i < nkeys; i++) {
            lv_area_t a;
            lv_obj_get_coords(keys[i].o, &a);
            if (x >= a.x1 - GAP && x <= a.x2 + GAP && y >= a.y1 - GAP && y <= a.y2 + GAP) { press(&keys[i]); return true; }
        }
        return true;
    }
    if (e == EV_SWIPE_LEFT) {
        int len = strlen(text);
        if (len) { text[len - 1] = 0; field_update(); }
        return true;
    }
    if (e == EV_SWIPE_RIGHT || e == EV_BOOT_CLICK) {
        kb_arg_t *a = ka;
        ka = NULL;
        ui_pop();
        if (a && a->done) a->done(NULL, a->user);
        return true;
    }
    return e == EV_SWIPE_UP || e == EV_SWIPE_DOWN || e == EV_DIAL_CW || e == EV_DIAL_CCW || e == EV_LONG_PRESS;
}

static void tick(void)
{
    if (flash && (int32_t)(lv_tick_get() - flash_until) > 0) {
        lv_obj_set_style_bg_color(flash, C_CARD, 0);
        flash = NULL;
    }
}

const screen_t scr_keyboard = {"keyboard", enter, NULL, event, tick, SCR_NO_STATUS | SCR_NO_PULL};

void keyboard_open(kb_arg_t *a) { ka = NULL; ui_push(&scr_keyboard, a); }

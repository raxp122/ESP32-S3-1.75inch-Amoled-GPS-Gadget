// gallery.c - the media on the microSD as a carousel of round previews.
// Bezel / swipe up-down-left: move. Tap: show it (pendant). Double tap: slideshow from here.
// Long press: actions (show, slideshow, delete).
#include "ui.h"
#include "media.h"
#include "sd.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

#define BIG    256
#define SMALL  110

static int sel;
static uint32_t seen_gen = UINT32_MAX;
static lv_obj_t *ring, *img, *side[2], *side_img[2], *name_l, *idx_l, *kind_l, *empty_l;
static uint16_t *buf[3];          // thumbnails of sel-1, sel, sel+1
static int buf_idx[3] = {-1, -1, -1};
static lv_image_dsc_t dsc[3];

static void dsc_init(int k)
{
    dsc[k] = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = THUMB_SIZE, .h = THUMB_SIZE,
                   .stride = THUMB_SIZE * 2},
        .data_size = THUMB_SIZE * THUMB_SIZE * 2,
        .data = (const uint8_t *)buf[k],
    };
}

// thumbnail of media i in slot k (decoded once, kept while it stays a neighbour)
static bool load(int k, int i)
{
    if (buf_idx[k] == i) return true;
    if (!buf[k]) { buf[k] = media_frame_alloc(THUMB_SIZE, THUMB_SIZE); if (!buf[k]) return false; dsc_init(k); }
    // maybe another slot already has it
    for (int j = 0; j < 3; j++)
        if (j != k && buf_idx[j] == i && buf[j]) {
            memcpy(buf[k], buf[j], THUMB_SIZE * THUMB_SIZE * 2);
            buf_idx[k] = i;
            return true;
        }
    buf_idx[k] = -1;
    if (!media_thumb(i, buf[k])) { memset(buf[k], 0, THUMB_SIZE * THUMB_SIZE * 2); return false; }
    buf_idx[k] = i;
    return true;
}

static lv_obj_t *round_img(lv_obj_t *parent, int d, lv_obj_t **img_out)
{
    lv_obj_t *c = ui_circle(parent, d, C_CARD);
    lv_obj_set_style_clip_corner(c, true, 0);
    lv_obj_t *im = lv_image_create(c);
    lv_obj_center(im);
    lv_image_set_scale(im, 256 * d / THUMB_SIZE);
    *img_out = im;
    return c;
}

static void show(void)
{
    int n = media_count();
    bool none = n == 0;
    lv_obj_update_flag(ring, LV_OBJ_FLAG_HIDDEN, none);
    lv_obj_update_flag(name_l, LV_OBJ_FLAG_HIDDEN, none);
    lv_obj_update_flag(idx_l, LV_OBJ_FLAG_HIDDEN, none);
    lv_obj_update_flag(empty_l, LV_OBJ_FLAG_HIDDEN, !none);
    for (int s = 0; s < 2; s++) lv_obj_add_flag(side[s], LV_OBJ_FLAG_HIDDEN);
    if (none) {
        lv_label_set_text(empty_l, sd_ok() ? T(S_GALLERY_EMPTY) : T(S_GALLERY_NO_SD));
        return;
    }
    if (sel >= n) sel = n - 1;
    if (sel < 0) sel = 0;
    load(1, sel);
    lv_image_set_src(img, &dsc[1]);
    lv_obj_invalidate(img);
    for (int s = 0; s < 2; s++) {
        int i = sel + (s ? 1 : -1);
        if (i < 0 || i >= n) continue;
        load(s ? 2 : 0, i);
        lv_image_set_src(side_img[s], &dsc[s ? 2 : 0]);
        lv_obj_invalidate(side_img[s]);
        lv_obj_remove_flag(side[s], LV_OBJ_FLAG_HIDDEN);
    }
    const media_item_t *m = media_get(sel);
    char b[64];
    media_display_name(m, b, sizeof(b));
    lv_label_set_text(name_l, b);
    snprintf(b, sizeof(b), "%s   %d / %d", m->kind == MEDIA_ANIM ? IC_FILM : IC_IMAGE, sel + 1, n);
    lv_label_set_text(idx_l, b);
}

static void enter(lv_obj_t *root, void *arg)
{
    if (seen_gen != media_generation()) {
        media_scan();
        seen_gen = media_generation();
        for (int k = 0; k < 3; k++) buf_idx[k] = -1;
    }
    for (int s = 0; s < 2; s++) {
        side[s] = round_img(root, SMALL, &side_img[s]);
        lv_obj_set_style_opa(side[s], LV_OPA_50, 0);
        lv_obj_align(side[s], LV_ALIGN_CENTER, s ? 186 : -186, -6);
    }
    ring = round_img(root, BIG, &img);
    lv_obj_set_style_border_color(ring, ui_accent(), 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_set_style_border_post(ring, true, 0);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, -6);
    idx_l = ui_label(root, &font_s, C_DIM, "");
    lv_obj_align(idx_l, LV_ALIGN_TOP_MID, 0, 52);
    kind_l = idx_l;   // type icon and position share the line at the top
    name_l = ui_label(root, &font_m, C_TEXT, "");
    ui_one_line(name_l, 290);
    lv_obj_align(name_l, LV_ALIGN_CENTER, 0, 156);
    empty_l = ui_label(root, &font_m, C_DIM, "");
    lv_obj_set_width(empty_l, 330);
    lv_label_set_long_mode(empty_l, LV_LABEL_LONG_WRAP);
    lv_obj_center(empty_l);
    show();
}

// The three preview buffers (3 x 115 KB of PSRAM) stay allocated: the old screen is still
// drawn during the fade to the next one, freeing them here would crash it.
static void leave(void) {}

static void move(int d)
{
    int n = media_count();
    if (!n || sel + d < 0 || sel + d >= n) return;
    sel += d;
    show();
}

/* ---- actions (long press) ---- */

static void act_show(void) { ui_pop(); pendant_start(sel, false); }
static void act_slideshow(void) { ui_pop(); pendant_start(sel, true); }
static void act_delete(void)
{
    bool ok = media_delete(sel);
    seen_gen = media_generation();
    for (int k = 0; k < 3; k++) buf_idx[k] = -1;
    ui_pop();
    ui_toast(T(ok ? S_DELETED : S_DELETE_FAILED));
}
static const menu_item_t act_items[] = {
    {IC_EYE, S_ACT_SHOW, NULL, NULL, act_show},
    {IC_PLAY, S_ACT_SLIDESHOW, NULL, NULL, act_slideshow},
    {IC_TRASH, S_ACT_DELETE, NULL, NULL, act_delete, .confirm = true},
};
static menu_t act_menu = {S_APP_GALLERY, act_items, 3, 0};

static bool event(ev_t e, int x, int y)
{
    int n = media_count();
    switch (e) {
    case EV_DIAL_CW: case EV_SWIPE_UP: case EV_SWIPE_LEFT: move(1); return true;
    case EV_DIAL_CCW: case EV_SWIPE_DOWN: move(-1); return true;
    case EV_TAP:
        if (!n) return true;
        if (x < 110) move(-1);
        else if (x > SCR_W - 110) move(1);
        else pendant_start(sel, false);
        return true;
    case EV_DOUBLE_TAP:
        if (n) pendant_start(sel, true);
        return true;
    case EV_LONG_PRESS:
        if (n) { act_menu.sel = 0; ui_push(&scr_menu, &act_menu); }
        return true;
    default: return false;
    }
}

static void tick(void)
{
    // a new upload arrived while the gallery is open
    if (seen_gen != media_generation()) {
        media_scan();
        seen_gen = media_generation();
        for (int k = 0; k < 3; k++) buf_idx[k] = -1;
        show();
    }
}

const screen_t scr_gallery = {"gallery", enter, leave, event, tick, SCR_DTAP};

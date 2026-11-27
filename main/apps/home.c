// home.c - the launcher: apps travel along the edge of the screen, the selected one at
// 12 o'clock. At 6 o'clock there is a "horizon" line: apps sliding towards it fade out and new
// ones come in from the other side, so the list can be as long as needed.
// At the end of the list the ring either goes on from the start (loop) or stops, leaving that
// side of the circle empty (Settings > Screen > Home list).
//
// At 6 o'clock, between the two strokes of the horizon, a fixed shortcut: one app chosen in
// Settings > Screen > Shortcut (Settings by default). It stays in the ring too.
//
// The ring follows the finger: sliding clockwise along the edge moves the icons clockwise.
// No LVGL transforms (they force extra layers: slow, and they tear on this panel); the discs
// change size directly.
#include "ui.h"
#include "media.h"
#include "sd.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "board.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

typedef struct { const char *icon; int name; const screen_t *scr; } app_t;
static const app_t apps[] = {
    {IC_GEM, S_APP_PENDANT, NULL},
    {IC_IMAGE, S_APP_GALLERY, &scr_gallery},
    {IC_UPLOAD, S_APP_UPLOAD, &scr_upload},
    {IC_DICE_D20, S_APP_DICE, &scr_dice},
    {IC_CLOCK, S_APP_CLOCK, &scr_clock},
    {IC_COG, S_APP_SETTINGS, &scr_settings},
    {IC_INFO, S_APP_INFO, &scr_info},
    {IC_CALENDAR, S_APP_CALENDAR, &scr_calendar},   // new apps at the end: the shortcut setting keeps its index
};
#define N_APPS   (int)(sizeof(apps) / sizeof(apps[0]))
#define RING_R   166
#define D_BIG    112      // disc at the top
#define D_SMALL  70       // discs along the ring
#define STEP     40.0f    // degrees between two apps
#define HORIZON  3.5f     // offset (in apps) where an app fades out, before the shortcut at the bottom
#define D_SHORT  62       // the shortcut at 6 o'clock
#define POOL     9        // discs in use: offsets -4..+4

static int sel;            // virtual index: in loop mode it can be any integer
static float pos;          // animated position, same units
static lv_obj_t *disc[POOL], *ico[POOL], *line_l, *line_r, *time_l, *name_l, *sub_l, *short_d, *short_i;

static bool loop_mode(void) { return g_set.home_loop; }

int home_app_count(void) { return N_APPS; }
const char *home_app_name(int i) { return T(apps[((i % N_APPS) + N_APPS) % N_APPS].name); }
static int shortcut(void) { return g_set.home_shortcut < N_APPS ? g_set.home_shortcut : HOME_SHORTCUT_DEFAULT; }
static int app_of(int v) { return ((v % N_APPS) + N_APPS) % N_APPS; }

// half of the visible offsets: in loop mode an app never shows twice at the same time
static float reach(void)
{
    float r = HORIZON;
    if (loop_mode() && (N_APPS - 1) / 2.0f + 0.5f < r) r = (N_APPS - 1) / 2.0f + 0.5f;
    return r;
}

static void layout(void)
{
    int base = (int)floorf(pos);
    float rr = reach();
    for (int k = 0; k < POOL; k++) lv_obj_add_flag(disc[k], LV_OBJ_FLAG_HIDDEN);
    for (int v = base - 4; v <= base + 5; v++) {
        float d = v - pos;
        if (fabsf(d) >= rr) continue;
        if (!loop_mode() && (v < 0 || v >= N_APPS)) continue;   // past the end: empty
        int k = ((v % POOL) + POOL) % POOL;   // the same disc follows the same app while it moves
        int a = app_of(v);
        // the next app sits counter-clockwise: turning clockwise brings it to the top
        float ang = (-90.0f - d * STEP) * 0.0174533f;
        float t = 1.0f - fabsf(d);
        if (t < 0) t = 0;
        int size = D_SMALL + (int)((D_BIG - D_SMALL) * t);
        size &= ~1;
        int x = SCR_C + (int)lroundf(RING_R * cosf(ang)) - size / 2;
        int y = SCR_C + (int)lroundf(RING_R * sinf(ang)) - size / 2;
        // fading towards the horizon line
        float fade = (rr - fabsf(d)) / 0.9f;
        lv_opa_t opa = fade >= 1 ? LV_OPA_COVER : fade <= 0 ? LV_OPA_TRANSP : (lv_opa_t)(255 * fade);
        lv_obj_t *o = disc[k];
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(o, size, size);
        lv_obj_set_pos(o, x, y);
        // fade with the opacity of background, border and icon (a whole-object opacity would
        // need an extra layer per disc)
        lv_obj_set_style_bg_opa(o, opa, 0);
        lv_obj_set_style_border_opa(o, (lv_opa_t)(opa * t), 0);
        lv_obj_set_style_text_opa(ico[k], opa, 0);
        lv_obj_set_style_bg_color(o, lv_color_mix(ui_accent_dark(), C_CARD, (uint8_t)(255 * t)), 0);
        ui_set_text(ico[k], apps[a].icon);
        lv_obj_set_style_text_font(ico[k], t > 0.5f ? &font_l : &font_m, 0);
        lv_obj_set_style_text_color(ico[k], lv_color_mix(ui_accent(), C_DIM, (uint8_t)(255 * t)), 0);
    }
}

static void anim_pos(void *v, int32_t x) { pos = x / 256.0f; layout(); }

static void sub_text(char *b, int n)
{
    b[0] = 0;
    switch (app_of(sel)) {
    case 0:
        if (!sd_ok()) snprintf(b, n, "%s", T(S_NO_SD));
        else if (!media_count()) snprintf(b, n, "%s", T(S_NO_MEDIA_SHORT));
        else snprintf(b, n, "%s", T(g_set.disp_slideshow ? S_SLIDESHOW : S_SINGLE_MEDIA));
        break;
    case 1:
        if (!sd_ok()) snprintf(b, n, "%s", T(S_NO_SD));
        else snprintf(b, n, T(S_N_MEDIA), media_count());
        break;
    case 2:
        if (wifi_mgr_state() == WIFI_CONNECTED) snprintf(b, n, IC_WIFI "  %s", g_set.wifi_ssid);
        else snprintf(b, n, "%s", T(S_VIA_HOTSPOT));
        break;
    case 3: dice_format(&g_set.dice, b, n); break;
    case 4: {
        time_t t = time(NULL);
        struct tm lt;
        localtime_r(&t, &lt);
        if (t > 1735689600) strftime(b, n, "%d.%m.%Y", &lt);
        break;
    }
    case 6: ui_battery_icon(b, n); break;
    default: break;
    }
}

static void refresh_text(void)
{
    char t[16], s[64];
    ui_time_str(t, sizeof(t));
    ui_set_text(time_l, t);
    ui_set_text(name_l, T(apps[app_of(sel)].name));
    sub_text(s, sizeof(s));
    ui_set_text(sub_l, s);
}

static void go(int d)
{
    int to = sel + d;
    if (!loop_mode() && (to < 0 || to >= N_APPS)) return;   // stop mode: the end of the list
    sel = to;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &pos);
    lv_anim_set_values(&a, (int32_t)lroundf(pos * 256), sel * 256);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_pos);
    lv_anim_start(&a);
    refresh_text();
}

static void open_index(int i)
{
    const app_t *a = &apps[i];
    if (!a->scr) { pendant_resume(); return; }
    ui_push(a->scr, NULL);
}

static void open_app(void) { open_index(app_of(sel)); }

static lv_obj_t *tick_mark(lv_obj_t *root, float deg)
{
    // the horizon: two short strokes on the ring at the bottom, where the apps disappear
    lv_obj_t *o = lv_obj_create(root);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 4, 26);
    lv_obj_set_style_radius(o, 2, 0);
    lv_obj_set_style_bg_color(o, C_FAINT, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    float a = deg * 0.0174533f;
    lv_obj_set_pos(o, SCR_C + (int)(RING_R * cosf(a)) - 2, SCR_C + (int)(RING_R * sinf(a)) - 13);
    return o;
}

static void enter(lv_obj_t *root, void *arg)
{
    if (!loop_mode()) {   // back from loop mode: a real index again
        sel = app_of(sel);
    }
    pos = sel;
    line_l = tick_mark(root, 90 - 17);
    line_r = tick_mark(root, 90 + 17);
    short_d = ui_circle(root, D_SHORT, C_CARD);
    lv_obj_set_style_border_color(short_d, C_FAINT, 0);
    lv_obj_set_style_border_width(short_d, 2, 0);
    lv_obj_set_pos(short_d, SCR_C - D_SHORT / 2, SCR_C + RING_R - D_SHORT / 2);
    short_i = ui_label(short_d, &font_m, ui_accent(), apps[shortcut()].icon);
    lv_obj_center(short_i);
    for (int k = 0; k < POOL; k++) {
        disc[k] = ui_circle(root, D_SMALL, C_CARD);
        lv_obj_set_style_border_color(disc[k], ui_accent(), 0);
        lv_obj_set_style_border_width(disc[k], 3, 0);
        ico[k] = ui_label(disc[k], &font_m, C_DIM, "");
        lv_obj_center(ico[k]);
    }
    time_l = ui_label(root, &font_m, C_DIM, "");
    lv_obj_align(time_l, LV_ALIGN_CENTER, 0, -52);
    name_l = ui_label(root, &font_l, C_TEXT, "");
    lv_obj_align(name_l, LV_ALIGN_CENTER, 0, 4);
    sub_l = ui_label(root, &font_s, ui_accent(), "");
    ui_one_line(sub_l, 230);
    lv_obj_align(sub_l, LV_ALIGN_CENTER, 0, 56);
    layout();
    refresh_text();
}

static bool event(ev_t e, int x, int y)
{
    switch (e) {
    // follow the finger: clockwise brings the app on the left (counter-clockwise side) up
    case EV_DIAL_CW: go(1); return true;
    case EV_DIAL_CCW: go(-1); return true;
    // a swipe across the top drags the ring with it: left -> the app on the right comes up
    case EV_SWIPE_LEFT: go(-1); return true;
    case EV_SWIPE_RIGHT: go(1); return true;
    case EV_SWIPE_UP: go(1); return true;
    case EV_SWIPE_DOWN: go(-1); return true;
    case EV_TAP: {
        int dx = x - SCR_C, dy = y - SCR_C;
        if (dx * dx + dy * dy < 120 * 120) { open_app(); return true; }
        int sx = x - SCR_C, sy = y - (SCR_C + RING_R);   // the shortcut at the bottom
        if (sx * sx + sy * sy < (D_SHORT / 2 + 12) * (D_SHORT / 2 + 12)) { open_index(shortcut()); return true; }
        // a tap on a disc: bring it up (or open it if it is already there)
        for (int v = sel - 4; v <= sel + 4; v++) {
            lv_obj_t *o = disc[((v % POOL) + POOL) % POOL];
            if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) continue;
            lv_area_t a;
            lv_obj_get_coords(o, &a);
            if (x >= a.x1 - 8 && x <= a.x2 + 8 && y >= a.y1 - 8 && y <= a.y2 + 8) {
                if (v == sel) open_app();
                else go(v - sel);
                return true;
            }
        }
        return true;
    }
    case EV_BOOT_CLICK: pendant_resume(); return true;
    case EV_BOOT_LONG: return true;
    default: return false;
    }
}

static void tick(void) { refresh_text(); }

static void leave(void)
{
    lv_anim_delete(&pos, NULL);   // the animation would move deleted objects
    pos = sel;
}

const screen_t scr_home = {"home", enter, leave, event, tick, SCR_NO_STATUS};

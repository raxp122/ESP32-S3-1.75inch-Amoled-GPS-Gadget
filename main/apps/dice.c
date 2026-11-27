// dice.c - dice for D&D (and any other tabletop RPG).
//
// Main screen: the die in the middle, the result in big digits, the die types on the lower
// edge. Tap (or shake the pendant) rolls; the bezel changes the die; swipe up / down changes
// the number of dice; swipe left cycles normal / advantage / disadvantage; long press opens
// the options (modifier, saved rolls, ability scores, history...).
#include "ui.h"
#include "board.h"
#include "core.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_random.h"

#define C_GOLD lv_color_hex(0xFFD54A)
#define HIST_MAX 30
#define DIE_R 100

static const uint8_t types[] = {4, 6, 8, 10, 12, 20, 100};
#define N_TYPES (int)(sizeof(types) / sizeof(types[0]))

typedef struct { dice_spec_t spec; dice_result_t res; } hist_t;
static hist_t hist[HIST_MAX];
static int hist_n;                 // entries used (newest at hist[0])

static lv_obj_t *die, *outline, *inner, *num_l, *expr_l, *mode_l, *break_l, *type_l[N_TYPES];
static lv_point_precise_t pts_out[12], pts_in[8];
static lv_timer_t *anim_t, *shake_t;
static int anim_step;
static dice_result_t last;
static dice_spec_t last_spec;   // the roll of `last` (options may change the current one)
static bool have_last;

static uint32_t rng(void *ctx) { (void)ctx; return esp_random(); }

static int type_index(void)
{
    for (int i = 0; i < N_TYPES; i++)
        if (types[i] == g_set.dice.sides) return i;
    return 5;   // d20
}

static void fix_type(void) { g_set.dice.sides = types[type_index()]; }

/* ---------------- drawing ---------------- */

// the outline of the die: a polygon per type (d20 with its inner triangle)
static void shape(void)
{
    int s = g_set.dice.sides, n, start = -90;
    float sx = 1, sy = 1;
    switch (s) {
    case 4: n = 3; sy = 1.08f; break;
    case 6: n = 4; start = -45; break;
    case 8: n = 4; sx = 0.86f; break;
    case 10: n = 4; sx = 0.92f; sy = 1.0f; break;
    case 12: n = 5; break;
    case 20: n = 6; break;
    default: n = 10; break;   // d100
    }
    for (int i = 0; i <= n; i++) {
        float a = (start + 360.0f * i / n) * 0.0174533f;
        float r = DIE_R;
        if (s == 10 && (i % 4 == 2)) r = DIE_R * 0.8f;   // d10: shorter bottom tip
        pts_out[i].x = DIE_R + r * sx * cosf(a);
        pts_out[i].y = DIE_R + r * sy * sinf(a) + (s == 4 ? 12 : 0);
    }
    lv_line_set_points(outline, pts_out, n + 1);
    if (s == 20) {   // the facing triangle of a d20
        for (int i = 0; i <= 3; i++) {
            float a = (-90 + 120.0f * i) * 0.0174533f;
            pts_in[i].x = DIE_R + DIE_R * 0.56f * cosf(a + 3.14159f);
            pts_in[i].y = DIE_R + DIE_R * 0.56f * sinf(a + 3.14159f);
        }
        lv_line_set_points(inner, pts_in, 4);
        lv_obj_remove_flag(inner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(inner, LV_OBJ_FLAG_HIDDEN);
    }
}

static void color(lv_color_t c)
{
    lv_obj_set_style_line_color(outline, c, 0);
    lv_obj_set_style_line_color(inner, c, 0);
    lv_obj_set_style_text_color(num_l, c, 0);
}

static void header(void)
{
    char b[32];
    dice_format(&g_set.dice, b, sizeof(b));
    ui_set_text(expr_l, b);
    static const int modes[] = {-1, S_DICE_ADV, S_DICE_DIS};
    if (!have_last || !(last.crit || last.fumble))
        ui_set_text(mode_l, g_set.dice.mode ? T(modes[g_set.dice.mode]) : "");
    for (int i = 0; i < N_TYPES; i++) {
        bool sel = i == type_index();
        lv_obj_set_style_text_color(type_l[i], sel ? ui_accent() : C_DIM, 0);
        lv_obj_set_style_text_font(type_l[i], sel ? &font_m : &font_s, 0);
    }
}

// a new spec: forget the last result on screen
static void changed(void)
{
    have_last = false;
    char b[8];
    snprintf(b, sizeof(b), "%d", g_set.dice.sides);
    lv_label_set_text(num_l, b);
    lv_obj_set_style_text_opa(num_l, LV_OPA_30, 0);
    color(ui_accent());
    lv_label_set_text(break_l, T(S_DICE_HINT));
    lv_obj_set_style_text_color(break_l, C_DIM, 0);
    shape();
    header();
    settings_mark();
}

static void show_result(void)
{
    char b[96];
    snprintf(b, sizeof(b), "%d", last.total);
    lv_label_set_text(num_l, b);
    lv_obj_set_style_text_opa(num_l, LV_OPA_COVER, 0);
    lv_obj_set_style_translate_x(die, 0, 0);
    lv_obj_set_style_translate_y(die, 0, 0);
    color(last.crit ? C_GOLD : last.fumble ? C_WARN : C_TEXT);
    lv_obj_set_style_line_color(outline, last.crit ? C_GOLD : last.fumble ? C_WARN : ui_accent(), 0);
    lv_obj_set_style_line_color(inner, last.crit ? C_GOLD : last.fumble ? C_WARN : ui_accent(), 0);
    // one die: nothing to break down, unless advantage shows the discarded one
    if (last.n > 1) dice_breakdown(&last, b, sizeof(b));
    else b[0] = 0;
    if (g_set.dice.mod && last.n >= 1) {
        size_t o = strlen(b);
        snprintf(b + o, sizeof(b) - o, "%s%+d", o ? "  " : "", g_set.dice.mod);
    }
    lv_label_set_text(break_l, b);
    lv_obj_set_style_text_color(break_l, C_DIM, 0);
    if (last.crit) { ui_set_text(mode_l, T(S_DICE_CRIT)); lv_obj_set_style_text_color(mode_l, C_GOLD, 0); }
    else if (last.fumble) { ui_set_text(mode_l, T(S_DICE_FUMBLE)); lv_obj_set_style_text_color(mode_l, C_WARN, 0); }
    else { lv_obj_set_style_text_color(mode_l, C_DIM, 0); header(); }
}

/* ---------------- rolling ---------------- */

static void anim_cb(lv_timer_t *t)
{
    anim_step++;
    const int steps = 14;
    if (anim_step >= steps) {
        lv_timer_delete(anim_t);
        anim_t = NULL;
        show_result();
        return;
    }
    // rattling: the die jumps around less and less while random faces flash by (a plain
    // offset, no LVGL transform: those need extra layers and tear on this panel)
    float p = anim_step / (float)steps, amp = 14 * (1 - p);
    lv_obj_set_style_translate_x(die, (int)(amp * sinf(anim_step * 2.3f)), 0);
    lv_obj_set_style_translate_y(die, (int)(-amp * fabsf(cosf(anim_step * 1.7f))), 0);
    char b[8];
    int lo = g_set.dice.count + g_set.dice.mod, hi = g_set.dice.count * g_set.dice.sides + g_set.dice.mod;
    snprintf(b, sizeof(b), "%d", lo + (int)(esp_random() % (uint32_t)(hi - lo + 1)));
    lv_label_set_text(num_l, b);
}

static void roll(void)
{
    if (anim_t) return;
    dice_roll(&g_set.dice, rng, NULL, &last);
    have_last = true;
    memmove(&hist[1], &hist[0], sizeof(hist_t) * (HIST_MAX - 1));
    hist[0] = (hist_t){g_set.dice, last};
    last_spec = g_set.dice;
    if (hist_n < HIST_MAX) hist_n++;
    lv_obj_set_style_text_opa(num_l, LV_OPA_COVER, 0);
    color(C_TEXT);
    lv_obj_set_style_line_color(outline, ui_accent(), 0);
    ui_set_text(break_l, "");
    anim_step = 0;
    anim_t = lv_timer_create(anim_cb, 45, NULL);
}

// shake: two strong jolts within 0.6 s
static void shake_cb(lv_timer_t *t)
{
    static uint32_t first, cooldown;
    static int hits;
    vec3_t a;
    if (!g_set.dice_shake || !board_imu_accel(&a)) return;
    uint32_t now = lv_tick_get();
    if ((int32_t)(now - cooldown) < 0) return;
    float g = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    if (fabsf(g - 1.0f) < 0.9f) return;
    if (!hits || now - first > 600) { hits = 1; first = now; return; }
    if (++hits >= 2) {
        hits = 0;
        cooldown = now + 1500;
        input_mark_activity();
        roll();
    }
}

/* ---------------- options (long press) ---------------- */

static void v_count(char *b, int n) { snprintf(b, n, "%d", g_set.dice.count); }
static void a_count(int d) { g_set.dice.count = g_set.dice.count + d < 1 ? 1 : g_set.dice.count + d > DICE_MAX ? DICE_MAX : g_set.dice.count + d; settings_mark(); }
static void v_mod(char *b, int n) { snprintf(b, n, "%+d", g_set.dice.mod); }
static void a_mod(int d) { int m = g_set.dice.mod + d; g_set.dice.mod = m < -20 ? -20 : m > 30 ? 30 : m; settings_mark(); }
static const int mode_names[] = {S_DICE_NORMAL, S_DICE_ADV, S_DICE_DIS};
static void v_mode(char *b, int n) { snprintf(b, n, "%s", T(mode_names[g_set.dice.mode % 3])); }
static void a_mode(int d) { g_set.dice.mode = (g_set.dice.mode + d + 3) % 3; settings_mark(); }
static void v_shake(char *b, int n) { snprintf(b, n, "%s", T(g_set.dice_shake ? S_ON : S_OFF)); }
static void s_shake(void) { g_set.dice_shake = !g_set.dice_shake; settings_mark(); }
static void v_cur(char *b, int n) { dice_format(&g_set.dice, b, n); }
static void s_save(void)
{
    int free_i = -1;
    for (int i = 0; i < DICE_FAVS; i++) {
        dice_spec_t *f = &g_set.dice_fav[i];
        if (f->count && !memcmp(f, &g_set.dice, sizeof(dice_spec_t))) { ui_toast(T(S_DICE_ALREADY)); return; }
        if (!f->count && free_i < 0) free_i = i;
    }
    if (free_i < 0) {   // full: the oldest (last) goes
        memmove(&g_set.dice_fav[1], &g_set.dice_fav[0], sizeof(dice_spec_t) * (DICE_FAVS - 1));
        free_i = 0;
    } else if (free_i > 0) {   // newest first
        memmove(&g_set.dice_fav[1], &g_set.dice_fav[0], sizeof(dice_spec_t) * free_i);
        free_i = 0;
    }
    g_set.dice_fav[free_i] = g_set.dice;
    settings_save();
    ui_toast(T(S_DICE_SAVED));
}
static void s_reset(void) { g_set.dice = (dice_spec_t){1, 20, 0, DICE_NORMAL, 0}; have_last = false; settings_mark(); ui_pop(); }

static const menu_item_t opt_items[] = {
    {IC_DICE, S_DICE_COUNT, NULL, v_count, NULL, a_count},
    {IC_PLUS, S_DICE_MOD, NULL, v_mod, NULL, a_mod},
    {IC_DICE_D20, S_DICE_MODE, NULL, v_mode, NULL, a_mode},
    {IC_STAR, S_DICE_FAVS, NULL, NULL, NULL, NULL, &scr_dice_favs},
    {IC_SAVE, S_DICE_SAVE, NULL, v_cur, s_save},
    {IC_DICE, S_DICE_STATS, NULL, NULL, NULL, NULL, &scr_dice_stats},
    {IC_HISTORY, S_DICE_HISTORY, NULL, NULL, NULL, NULL, &scr_dice_hist},
    {IC_WALK, S_DICE_SHAKE, NULL, v_shake, s_shake},
    {IC_REFRESH, S_DICE_RESET, NULL, NULL, s_reset},
};
static menu_t opt_menu = {S_APP_DICE, opt_items, sizeof(opt_items) / sizeof(opt_items[0]), 0};

/* ---------------- main screen ---------------- */

static void enter(lv_obj_t *root, void *arg)
{
    g_set.dice = dice_clamp(g_set.dice);
    fix_type();   // a die we do not draw (saved by hand, damaged settings): back to d20
    expr_l = ui_label(root, &font_m, ui_accent(), "");
    lv_obj_align(expr_l, LV_ALIGN_TOP_MID, 0, 34);
    mode_l = ui_label(root, &font_s, C_DIM, "");
    lv_obj_align(mode_l, LV_ALIGN_TOP_MID, 0, 74);

    die = lv_obj_create(root);
    lv_obj_remove_style_all(die);
    lv_obj_set_size(die, DIE_R * 2 + 1, DIE_R * 2 + 26);
    lv_obj_align(die, LV_ALIGN_CENTER, 0, -6);
    lv_obj_remove_flag(die, LV_OBJ_FLAG_SCROLLABLE);
    outline = lv_line_create(die);
    inner = lv_line_create(die);
    for (int k = 0; k < 2; k++) {
        lv_obj_t *l = k ? inner : outline;
        lv_obj_set_style_line_width(l, k ? 2 : 4, 0);
        lv_obj_set_style_line_rounded(l, true, 0);
        lv_obj_set_style_line_opa(l, k ? LV_OPA_50 : LV_OPA_COVER, 0);
    }
    num_l = ui_label(root, &font_xl, C_TEXT, "");
    lv_obj_align(num_l, LV_ALIGN_CENTER, 0, -4);
    break_l = ui_label(root, &font_s, C_DIM, "");
    ui_one_line(break_l, 250);
    lv_obj_align(break_l, LV_ALIGN_CENTER, 0, 112);

    // die types along the lower edge, from 7 to 5 o'clock
    for (int i = 0; i < N_TYPES; i++) {
        char b[8];
        snprintf(b, sizeof(b), "d%d", types[i]);
        type_l[i] = ui_label(root, &font_s, C_DIM, b);
        float a = (138 - i * 16) * 0.0174533f;
        lv_obj_align(type_l[i], LV_ALIGN_CENTER, (int)(196 * cosf(a)), (int)(196 * sinf(a)));
    }
    shake_t = lv_timer_create(shake_cb, 40, NULL);
    if (have_last && !memcmp(&last_spec, &g_set.dice, sizeof(dice_spec_t))) { shape(); header(); show_result(); }
    else changed();
}

static void leave(void)
{
    if (anim_t) { lv_timer_delete(anim_t); anim_t = NULL; }
    if (shake_t) { lv_timer_delete(shake_t); shake_t = NULL; }
    settings_flush();
}

static void set_type(int i)
{
    g_set.dice.sides = types[(i + N_TYPES) % N_TYPES];
    if (g_set.dice.sides != 20 && g_set.dice.mode) g_set.dice.mode = DICE_NORMAL;   // adv/dis: a d20 thing
    changed();
}

static bool event(ev_t e, int x, int y)
{
    if (anim_t && e != EV_BOOT_CLICK && e != EV_BOOT_LONG) return true;   // let the die land
    switch (e) {
    case EV_TAP:
        for (int i = 0; i < N_TYPES; i++) {   // tap on a die type
            lv_area_t a;
            lv_obj_get_coords(type_l[i], &a);
            if (x >= a.x1 - 14 && x <= a.x2 + 14 && y >= a.y1 - 14 && y <= a.y2 + 14) { set_type(i); return true; }
        }
        roll();
        return true;
    // the types sit along the lower edge with d4 on the left: a clockwise slide moves right to
    // left down there, so the selection follows the finger towards the smaller dice
    case EV_DIAL_CW: set_type(type_index() - 1); return true;
    case EV_DIAL_CCW: set_type(type_index() + 1); return true;
    case EV_SWIPE_UP: if (g_set.dice.count < DICE_MAX) { g_set.dice.count++; changed(); } return true;
    case EV_SWIPE_DOWN: if (g_set.dice.count > 1) { g_set.dice.count--; changed(); } return true;
    case EV_SWIPE_LEFT:
        g_set.dice.mode = (g_set.dice.mode + 1) % 3;
        if (g_set.dice.mode && g_set.dice.sides != 20) g_set.dice.sides = 20;
        changed();
        return true;
    case EV_LONG_PRESS: opt_menu.sel = 0; ui_push(&scr_menu, &opt_menu); return true;
    default: return false;
    }
}

const screen_t scr_dice = {"dice", enter, leave, event, NULL, SCR_NO_STATUS};

/* ---------------- saved rolls ---------------- */

static menu_item_t fav_items[DICE_FAVS];
static char fav_text[DICE_FAVS][24];
static menu_t fav_menu = {S_DICE_FAVS, fav_items, 0, 0};
static int fav_map[DICE_FAVS];
static lv_obj_t *fav_empty;

static void fav_pick(void)
{
    g_set.dice = dice_clamp(g_set.dice_fav[fav_map[fav_menu.sel]]);
    fix_type();
    have_last = false;
    settings_mark();
    ui_pop();   // the list
    ui_pop();   // the options
}

static void fav_enter(lv_obj_t *root, void *arg)
{
    static const char *mode_tag[] = {"", "  ADV", "  DIS"};
    int n = 0;
    for (int i = 0; i < DICE_FAVS; i++) {
        dice_spec_t *f = &g_set.dice_fav[i];
        if (!f->count) continue;
        char b[20];
        dice_format(f, b, sizeof(b));
        snprintf(fav_text[n], sizeof(fav_text[n]), "%s%s", b, mode_tag[f->mode % 3]);
        fav_items[n] = (menu_item_t){.icon = IC_STAR, .str = -1, .text = fav_text[n], .select = fav_pick};
        fav_map[n++] = i;
    }
    fav_menu.count = n;
    if (!n) {
        fav_empty = ui_label(root, &font_m, C_DIM, T(S_DICE_NO_FAVS));
        lv_obj_set_width(fav_empty, 320);
        lv_label_set_long_mode(fav_empty, LV_LABEL_LONG_WRAP);
        lv_obj_center(fav_empty);
        return;
    }
    scr_menu.enter(root, &fav_menu);
}
static void fav_leave(void) { if (fav_menu.count) scr_menu.leave(); }
static bool fav_event(ev_t e, int x, int y) { return fav_menu.count && scr_menu.event(e, x, y); }
static void fav_tick(void) { if (fav_menu.count) scr_menu.tick(); }
const screen_t scr_dice_favs = {"dice-favs", fav_enter, fav_leave, fav_event, fav_tick, 0};

/* ---------------- history ---------------- */

static menu_item_t h_items[HIST_MAX];
static char h_text[HIST_MAX][24];
static menu_t h_menu = {S_DICE_HISTORY, h_items, 0, 0};

static void h_value(char *b, int n)
{
    const hist_t *h = &hist[h_menu.sel];
    char d[64];
    dice_breakdown(&h->res, d, sizeof(d));
    snprintf(b, n, "%s%s", h->res.crit ? T(S_DICE_CRIT) : h->res.fumble ? T(S_DICE_FUMBLE) : "", h->res.n > 1 ? d : "");
}

static void h_enter(lv_obj_t *root, void *arg)
{
    static const char *mode_tag[] = {"", " ADV", " DIS"};
    for (int i = 0; i < hist_n; i++) {
        char b[16];
        dice_format(&hist[i].spec, b, sizeof(b));
        snprintf(h_text[i], sizeof(h_text[i]), "%d  \xC2\xB7  %s%s", hist[i].res.total, b, mode_tag[hist[i].spec.mode % 3]);
        h_items[i] = (menu_item_t){.icon = i ? IC_HISTORY : IC_DICE_D20, .str = -1, .text = h_text[i], .value = h_value};
    }
    h_menu.count = hist_n;
    h_menu.sel = 0;
    if (!hist_n) {
        lv_obj_t *l = ui_label(root, &font_m, C_DIM, T(S_DICE_NO_HISTORY));
        lv_obj_center(l);
        return;
    }
    scr_menu.enter(root, &h_menu);
}
static void h_leave(void) { if (h_menu.count) scr_menu.leave(); }
static bool h_event(ev_t e, int x, int y) { return h_menu.count && scr_menu.event(e, x, y); }
static void h_tick(void) { if (h_menu.count) scr_menu.tick(); }
const screen_t scr_dice_hist = {"dice-history", h_enter, h_leave, h_event, h_tick, 0};

/* ---------------- ability scores: 4d6 drop the lowest, six times ---------------- */

static int scores[6];
static bool scores_done;
static lv_obj_t *sc_num[6], *sc_mod[6], *sc_total;

static void stats_roll(void)
{
    dice_spec_t s = {4, 6, 0, DICE_NORMAL, 1};
    for (int i = 0; i < 6; i++) {
        dice_result_t r;
        dice_roll(&s, rng, NULL, &r);
        scores[i] = r.total;
    }
    scores_done = true;
}

static void stats_show(void)
{
    int sum = 0;
    for (int i = 0; i < 6; i++) {
        char b[12];
        snprintf(b, sizeof(b), "%d", scores[i]);
        lv_label_set_text(sc_num[i], b);
        snprintf(b, sizeof(b), "%+d", dice_ability_mod(scores[i]));
        lv_label_set_text(sc_mod[i], b);
        lv_obj_set_style_text_color(sc_num[i], scores[i] >= 16 ? C_GOLD : scores[i] <= 7 ? C_WARN : C_TEXT, 0);
        sum += scores[i];
    }
    char b[48];
    snprintf(b, sizeof(b), T(S_DICE_TOTAL), sum);
    lv_label_set_text(sc_total, b);
}

static void stats_enter(lv_obj_t *root, void *arg)
{
    if (!scores_done) stats_roll();
    lv_obj_t *t = ui_label(root, &font_s, ui_accent(), T(S_DICE_STATS));
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -34);
    sc_total = ui_label(root, &font_m, C_TEXT, "");
    lv_obj_align(sc_total, LV_ALIGN_CENTER, 0, 2);
    lv_obj_t *h = ui_label(root, &font_s, C_DIM, T(S_DICE_REROLL));
    lv_obj_align(h, LV_ALIGN_CENTER, 0, 38);
    for (int i = 0; i < 6; i++) {
        lv_obj_t *d = ui_circle(root, 96, C_CARD);
        lv_obj_set_style_border_color(d, ui_accent_dark(), 0);
        lv_obj_set_style_border_width(d, 2, 0);
        float a = (-90 + i * 60) * 0.0174533f;
        lv_obj_align(d, LV_ALIGN_CENTER, (int)(160 * cosf(a)), (int)(160 * sinf(a)));
        sc_num[i] = ui_label(d, &font_l, C_TEXT, "");
        lv_obj_align(sc_num[i], LV_ALIGN_CENTER, 0, -10);
        sc_mod[i] = ui_label(d, &font_s, C_DIM, "");
        lv_obj_align(sc_mod[i], LV_ALIGN_CENTER, 0, 26);
    }
    stats_show();
}

static bool stats_event(ev_t e, int x, int y)
{
    if (e == EV_TAP || e == EV_DIAL_CW || e == EV_DIAL_CCW) { stats_roll(); stats_show(); return true; }
    return false;
}

const screen_t scr_dice_stats = {"dice-stats", stats_enter, NULL, stats_event, NULL, SCR_NO_STATUS};

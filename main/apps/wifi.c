// wifi.c - Wi-Fi menu, network list (scan + on-screen keyboard) and setup from the phone
#include "ui.h"
#include "keyboard.h"
#include "ota.h"
#include "settings.h"
#include "web.h"
#include "wifi_mgr.h"
#include <stdio.h>
#include <string.h>

/* ---------------- menu ---------------- */

static void v_onoff(char *b, int n)
{
    if (!g_set.wifi_on) { snprintf(b, n, "%s", T(S_OFF)); return; }
    switch (wifi_mgr_state()) {
    case WIFI_CONNECTED: snprintf(b, n, "%s  ·  %s", T(S_CONNECTED), wifi_mgr_ip()); break;
    case WIFI_CONNECTING: snprintf(b, n, "%s", T(S_CONNECTING)); break;
    default: snprintf(b, n, "%s", T(wifi_mgr_conn_failed() ? S_WIFI_FAILED : S_NOT_CONNECTED)); break;
    }
}
static void s_onoff(void)
{
    g_set.wifi_on = !g_set.wifi_on;
    settings_save();
    wifi_mgr_apply();
    if (g_set.wifi_on) ota_auto_start();
}
static void v_net(char *b, int n) { snprintf(b, n, "%s", g_set.wifi_ssid[0] ? g_set.wifi_ssid : T(S_NO_NETWORK_SET)); }
static void s_forget(void) { wifi_mgr_forget(); ui_toast(T(S_WIFI_FORGOTTEN)); }

static const menu_item_t wifi_items[] = {
    {IC_WIFI, S_WIFI, NULL, v_onoff, s_onoff},
    {IC_SLIDERS, S_WIFI_NETWORK, NULL, v_net, NULL, NULL, &scr_wifi_scan},
    {IC_MOBILE, S_WIFI_PHONE, NULL, NULL, NULL, NULL, &scr_wifi_portal},
    {IC_TRASH, S_WIFI_FORGET, NULL, NULL, s_forget, .confirm = true},
};
static menu_t wifi_menu = {S_WIFI, wifi_items, 4, 0};

MENU_SCREEN(scr_wifi, "wifi", &wifi_menu);

/* ---------------- scan ---------------- */

#define MAX_AP 20
static wifi_ap_t aps[MAX_AP];
static menu_item_t ap_items[MAX_AP];
static char ap_vals[MAX_AP][24];
static menu_t ap_menu = {S_WIFI_NETWORK, ap_items, 0, 0};
static uint32_t scan_gen;
static bool have_list;
static lv_obj_t *wait_l;
static char chosen[33];
static kb_arg_t kb;

static void on_password(const char *pass, void *user)
{
    if (!pass) return;   // cancelled: back to the list
    wifi_mgr_connect(chosen, pass);
    char t[80];
    snprintf(t, sizeof(t), T(S_CONNECTING_TO), chosen);
    ui_toast(t);
    ui_pop();            // leave the list too: the Wi-Fi menu shows the state
}

static void pick(void *arg)
{
    int i = (int)(intptr_t)arg;
    strlcpy(chosen, aps[i].ssid, sizeof(chosen));
    if (!aps[i].auth) { on_password("", NULL); return; }
    kb = (kb_arg_t){.title = chosen, .initial = !strcmp(chosen, g_set.wifi_ssid) ? g_set.wifi_pass : "", .maxlen = 63,
                    .done = on_password};
    keyboard_open(&kb);
}

static void ap_value(char *b, int n) { snprintf(b, n, "%s", ap_vals[ap_menu.sel < MAX_AP ? ap_menu.sel : 0]); }

static void build_list(void)
{
    int n = wifi_mgr_scan_results(aps, MAX_AP);
    for (int i = 0; i < n; i++) {
        int bars = aps[i].rssi > -55 ? 4 : aps[i].rssi > -65 ? 3 : aps[i].rssi > -75 ? 2 : 1;
        // bars as bullets (3 bytes each in UTF-8)
        snprintf(ap_vals[i], sizeof(ap_vals[i]), "%s%.*s", aps[i].auth ? IC_LOCK "  " : "", bars * 3,
                 "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");
        ap_items[i] = (menu_item_t){.icon = IC_WIFI, .str = -1, .text = aps[i].ssid, .value = ap_value};
    }
    ap_menu.count = n;
    if (ap_menu.sel >= n) ap_menu.sel = 0;
    have_list = n > 0;
}

// the menu item has no "pick with argument": select() reads the current selection
static void select_current(void) { pick((void *)(intptr_t)ap_menu.sel); }

static void scan_enter(lv_obj_t *root, void *arg)
{
    if (!have_list) {
        if (!wifi_mgr_scan_busy()) wifi_mgr_scan_start();
        scan_gen = wifi_mgr_scan_gen();
        wait_l = ui_label(root, &font_m, C_DIM, T(S_SEARCHING));
        lv_obj_center(wait_l);
        return;
    }
    for (int i = 0; i < ap_menu.count; i++) ap_items[i].select = select_current;
    scr_menu.enter(root, &ap_menu);
}

static void scan_leave(void)
{
    if (have_list) scr_menu.leave();
    wait_l = NULL;
    if (ui_closing()) have_list = false;   // next time: a fresh scan
}

static bool scan_event(ev_t e, int x, int y)
{
    if (!have_list) return false;
    if (e == EV_LONG_PRESS) { have_list = false; ui_rebuild(); return true; }   // scan again
    return scr_menu.event(e, x, y);
}

static void scan_tick(void)
{
    if (have_list) { scr_menu.tick(); return; }
    if (wifi_mgr_scan_gen() != scan_gen && !wifi_mgr_scan_busy()) {
        build_list();
        if (have_list) ui_rebuild();
        else if (wait_l) ui_set_text(wait_l, T(S_NO_NETWORKS));
    }
}

const screen_t scr_wifi_scan = {"wifi-scan", scan_enter, scan_leave, scan_event, scan_tick, 0};

/* ---------------- from the phone ---------------- */

static lv_obj_t *qr, *p_hint, *p_state;
static char p_ssid[33];
static bool p_sent;

static void portal_enter(lv_obj_t *root, void *arg)
{
    if (web_mode() != WEB_WIFI) {
        wifi_mgr_ap_start();
        web_start(WEB_WIFI);
        p_sent = false;
    }
    lv_obj_t *box = lv_obj_create(root);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 186, 186);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 18, 0);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, -58);
    qr = lv_qrcode_create(box);
    lv_qrcode_set_size(qr, 160);
    lv_obj_center(qr);
    char w[80];
    snprintf(w, sizeof(w), "WIFI:T:nopass;S:%s;;", wifi_mgr_ap_ssid());
    lv_qrcode_update(qr, w, strlen(w));
    char h[96];
    snprintf(h, sizeof(h), T(S_PORTAL_HINT), wifi_mgr_ap_ssid());
    p_hint = ui_label(root, &font_s, C_DIM, h);
    lv_obj_set_width(p_hint, 320);
    lv_label_set_long_mode(p_hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(p_hint, LV_ALIGN_CENTER, 0, 96);
    p_state = ui_label(root, &font_s, C_OK, "");
    lv_obj_set_width(p_state, 300);
    lv_label_set_long_mode(p_state, LV_LABEL_LONG_DOT);
    lv_obj_align(p_state, LV_ALIGN_CENTER, 0, 160);
}

static void portal_tick(void)
{
    char pass[65];
    if (web_take_wifi(p_ssid, sizeof(p_ssid), pass, sizeof(pass))) {
        wifi_mgr_connect(p_ssid, pass);
        p_sent = true;
    }
    char s[80] = "";
    if (p_sent) {
        if (wifi_mgr_state() == WIFI_CONNECTED) snprintf(s, sizeof(s), IC_CHECK "  %s  ·  %s", p_ssid, wifi_mgr_ip());
        else if (wifi_mgr_conn_failed()) snprintf(s, sizeof(s), IC_WARN "  %s", T(S_WIFI_FAILED));
        else snprintf(s, sizeof(s), T(S_CONNECTING_TO), p_ssid);
    } else if (wifi_mgr_ap_clients() > 0) {
        snprintf(s, sizeof(s), IC_MOBILE "  %s", T(S_UPLOAD_CONNECTED));
    }
    if (p_state) ui_set_text(p_state, s);
}

static void portal_leave(void)
{
    qr = p_hint = p_state = NULL;
    if (!ui_closing()) return;
    web_stop();
    wifi_mgr_ap_stop();
    if (p_sent && wifi_mgr_state() == WIFI_CONNECTED) ota_auto_start();
}

const screen_t scr_wifi_portal = {"wifi-portal", portal_enter, portal_leave, NULL, portal_tick, SCR_NO_SLEEP};

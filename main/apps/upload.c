// upload.c - "Upload": shows the address (QR) and the PIN of the upload page. On the home
// Wi-Fi the page is at the board's IP; without Wi-Fi the board opens its own hotspot and the
// QR first joins it. The server runs only while this screen is open.
#include "ui.h"
#include "media.h"
#include "sd.h"
#include "web.h"
#include "wifi_mgr.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *qr, *qr_box, *url_l, *pin_l, *state_l, *hint_l;
static bool own_ap, qr_is_url;
static char url[48], qr_data[96];

static void set_qr(const char *data)
{
    if (!strcmp(qr_data, data)) return;
    strlcpy(qr_data, data, sizeof(qr_data));
    lv_qrcode_update(qr, qr_data, strlen(qr_data));
}

static void update(void)
{
    // hotspot: first the QR that joins the network, then (someone joined) the page
    if (own_ap && !qr_is_url && wifi_mgr_ap_clients() > 0) qr_is_url = true;
    if (own_ap && !qr_is_url) {
        char w[80];
        snprintf(w, sizeof(w), "WIFI:T:nopass;S:%s;;", wifi_mgr_ap_ssid());
        set_qr(w);
        char h[96];
        snprintf(h, sizeof(h), T(S_UPLOAD_JOIN), wifi_mgr_ap_ssid());
        ui_set_text(hint_l, h);
    } else {
        set_qr(url);
        ui_set_text(hint_l, T(S_UPLOAD_OPEN));
    }
    char s[96] = "", t[64];
    if (web_progress() >= 0) {
        snprintf(s, sizeof(s), IC_DOWNLOAD "  %d%%", web_progress());
    } else if (web_uploads()) {
        snprintf(t, sizeof(t), T(S_UPLOAD_COUNT), web_uploads());
        snprintf(s, sizeof(s), IC_CHECK "  %s", t);
    } else if (web_client_seen()) {
        snprintf(s, sizeof(s), IC_MOBILE "  %s", T(S_UPLOAD_CONNECTED));
    }
    ui_set_text(state_l, s);
}

static void enter(lv_obj_t *root, void *arg)
{
    lv_obj_t *t = ui_label(root, &font_s, ui_accent(), T(S_APP_UPLOAD));
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 50);
    if (!sd_ok()) {
        lv_obj_t *l = ui_label(root, &font_m, C_DIM, T(S_GALLERY_NO_SD));
        lv_obj_set_width(l, 320);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_center(l);
        return;
    }
    if (web_mode() != WEB_UPLOAD) {
        own_ap = wifi_mgr_state() != WIFI_CONNECTED;
        if (own_ap) wifi_mgr_ap_start();
        web_start(WEB_UPLOAD);
        qr_is_url = false;
    }
    snprintf(url, sizeof(url), "http://%s/", own_ap ? WIFI_AP_IP : wifi_mgr_ip());

    qr_box = lv_obj_create(root);
    lv_obj_remove_style_all(qr_box);
    lv_obj_set_size(qr_box, 186, 186);
    lv_obj_set_style_bg_color(qr_box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qr_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qr_box, 18, 0);
    lv_obj_align(qr_box, LV_ALIGN_CENTER, 0, -50);
    qr = lv_qrcode_create(qr_box);
    lv_qrcode_set_size(qr, 160);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_obj_center(qr);
    qr_data[0] = 0;

    hint_l = ui_label(root, &font_s, C_DIM, "");
    lv_obj_set_width(hint_l, 330);
    lv_label_set_long_mode(hint_l, LV_LABEL_LONG_DOT);
    lv_obj_align(hint_l, LV_ALIGN_CENTER, 0, 62);
    url_l = ui_label(root, &font_s, ui_accent(), url + 7);   // without "http://"
    lv_obj_align(url_l, LV_ALIGN_CENTER, 0, 92);
    char p[16];
    snprintf(p, sizeof(p), "PIN  %s", web_pin());
    pin_l = ui_label(root, &font_l, C_TEXT, p);
    lv_obj_set_style_text_letter_space(pin_l, 4, 0);
    lv_obj_align(pin_l, LV_ALIGN_CENTER, 0, 136);
    state_l = ui_label(root, &font_s, C_OK, "");
    lv_obj_align(state_l, LV_ALIGN_CENTER, 0, 180);
    update();
}

static void tick(void) { if (qr) update(); }

static void leave(void)
{
    qr = NULL;
    if (!ui_closing()) return;   // only covered (quick panel): keep the server
    web_stop();
    if (own_ap) wifi_mgr_ap_stop();
    own_ap = false;
}

const screen_t scr_upload = {"upload", enter, leave, NULL, tick, SCR_NO_SLEEP};

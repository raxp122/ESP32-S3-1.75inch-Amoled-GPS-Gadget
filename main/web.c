// web.c - HTTP server for the upload page (main/www/upload.html) and the Wi-Fi setup page
// (main/www/wifi.html). The pages do all the heavy work in the browser: cropping to the
// circle, converting GIFs and videos to AMJ (core.h), making the thumbnails. The device only
// stores files.
//
// API (upload mode; X-Pin header on every call except /api/info):
//   GET  /api/info                      {"name","version","sd","free","total","count","size","thumb"}
//   POST /api/auth                      200 / 403
//   GET  /api/list                      [{"f":"x.jpg","k":1,"s":123}, ...]
//   GET  /api/thumb?f=x.jpg             the 240 px preview (JPEG)
//   POST /api/upload?f=x.jpg&t=media    raw body -> /sdcard/amulet/x.jpg   (t=thumb -> .thumb/x.jpg)
//   POST /api/delete?f=x.jpg
// Wi-Fi mode:
//   GET  /api/scan                      [{"s":"ssid","r":-60,"l":1}, ...]
//   POST /api/wifi                      body "s=<ssid>&p=<password>" (urlencoded)
#include "web.h"
#include "core.h"
#include "media.h"
#include "ota.h"
#include "sd.h"
#include "wifi_mgr.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "web";

extern const char upload_html_start[] asm("_binary_upload_html_start");
extern const char wifi_html_start[] asm("_binary_wifi_html_start");

static httpd_handle_t srv;
static web_mode_t mode;
static char pin[8];
static volatile int uploads, progress = -1, bad_pins;
static volatile bool seen;
static char current[64];
static char wifi_s[33], wifi_p[65];
static volatile bool wifi_pending;

web_mode_t web_mode(void) { return mode; }
const char *web_pin(void) { return pin; }
int web_uploads(void) { return uploads; }
int web_progress(void) { return progress; }
const char *web_current(void) { return current; }
bool web_client_seen(void) { return seen; }

bool web_take_wifi(char *s, int sn, char *p, int pn)
{
    if (!wifi_pending) return false;
    strlcpy(s, wifi_s, sn);
    strlcpy(p, wifi_p, pn);
    wifi_pending = false;
    return true;
}

/* ---------------- helpers ---------------- */

static esp_err_t send_json(httpd_req_t *r, const char *json)
{
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, json);
}

static esp_err_t fail(httpd_req_t *r, const char *status, const char *msg)
{
    httpd_resp_set_status(r, status);
    httpd_resp_set_type(r, "text/plain");
    return httpd_resp_sendstr(r, msg);
}

// X-Pin check, with a brake against guessing: after 20 wrong PINs nothing is accepted any
// more until the screen is opened again (new PIN)
static bool authorised(httpd_req_t *r)
{
    char p[8] = "";
    if (bad_pins < 20 && httpd_req_get_hdr_value_str(r, "X-Pin", p, sizeof(p)) == ESP_OK && !strcmp(p, pin)) return true;
    if (bad_pins < 1000) bad_pins++;
    vTaskDelay(pdMS_TO_TICKS(400 * (bad_pins > 5 ? 5 : bad_pins)));
    fail(r, "403 Forbidden", "PIN");
    return false;
}

static bool get_query(httpd_req_t *r, const char *key, char *out, int n)
{
    char q[160];
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK) return false;
    return query_param(q, key, out, n);
}

// "My file.jpg" -> "My_file.jpg" with the right extension, false if not acceptable
static bool clean_file(const char *in, char *out, int n)
{
    media_kind_t k = media_kind(in);
    if (k == MEDIA_NONE) return false;
    char base[MEDIA_NAME_MAX + 1];
    if (!media_safe_name(in, base, sizeof(base))) return false;
    snprintf(out, n, "%s.%s", base, k == MEDIA_ANIM ? "amj" : "jpg");
    return true;
}

static void json_str(char *dst, int n, const char *s)   // appends "s" escaped
{
    int o = strlen(dst);
    if (o < n - 1) dst[o++] = '"';
    for (; *s && o < n - 3; s++) {
        if (*s == '"' || *s == '\\') dst[o++] = '\\';
        dst[o++] = (unsigned char)*s < 0x20 ? ' ' : *s;
    }
    if (o < n - 1) dst[o++] = '"';
    dst[o] = 0;
}

/* ---------------- pages ---------------- */

static esp_err_t h_page(httpd_req_t *r)
{
    seen = true;
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, mode == WEB_WIFI ? wifi_html_start : upload_html_start);
}

// anything else: phones probing for a captive portal (generate_204, hotspot-detect...) get
// sent to the page, so the hotspot opens it by itself
static esp_err_t h_any(httpd_req_t *r)
{
    if (!wifi_mgr_ap_on()) return httpd_resp_send_404(r);
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "http://" WIFI_AP_IP "/");
    return httpd_resp_send(r, NULL, 0);
}

static esp_err_t h_info(httpd_req_t *r)
{
    uint64_t total = 0, free_b = 0;
    bool sd = sd_space(&total, &free_b);
    char j[256];
    snprintf(j, sizeof(j),
             "{\"name\":\"AmuletOS\",\"version\":\"%s\",\"mode\":\"%s\",\"sd\":%s,\"free\":%llu,\"total\":%llu,"
             "\"count\":%d,\"size\":466,\"thumb\":%d}",
             ota_current(), mode == WEB_WIFI ? "wifi" : "upload", sd ? "true" : "false",
             (unsigned long long)free_b, (unsigned long long)total, media_count(), THUMB_SIZE);
    return send_json(r, j);
}

static esp_err_t h_auth(httpd_req_t *r)
{
    if (!authorised(r)) return ESP_OK;
    bad_pins = 0;
    return send_json(r, "{\"ok\":true}");
}

static esp_err_t h_list(httpd_req_t *r)
{
    if (!authorised(r)) return ESP_OK;
    httpd_resp_set_type(r, "application/json");
    httpd_resp_sendstr_chunk(r, "[");
    int n = media_count();
    for (int i = 0; i < n; i++) {
        const media_item_t *m = media_get(i);
        if (!m) break;
        char e[160] = "{\"f\":";
        json_str(e, sizeof(e), m->file);
        int l = strlen(e);
        snprintf(e + l, sizeof(e) - l, ",\"k\":%d,\"s\":%lu}%s", m->kind, (unsigned long)m->size, i < n - 1 ? "," : "");
        httpd_resp_sendstr_chunk(r, e);
    }
    httpd_resp_sendstr_chunk(r, "]");
    return httpd_resp_sendstr_chunk(r, NULL);
}

static esp_err_t h_thumb(httpd_req_t *r)
{
    if (!authorised(r)) return ESP_OK;
    char f[64], file[64], path[128];
    if (!get_query(r, "f", f, sizeof(f)) || !clean_file(f, file, sizeof(file))) return fail(r, "400 Bad Request", "name");
    char *dot = strrchr(file, '.');
    *dot = 0;
    snprintf(path, sizeof(path), THUMB_DIR "/%s.jpg", file);
    FILE *fp = fopen(path, "rb");
    if (!fp) return fail(r, "404 Not Found", "no thumbnail");
    httpd_resp_set_type(r, "image/jpeg");
    httpd_resp_set_hdr(r, "Cache-Control", "max-age=60");
    char *b = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    size_t n;
    while (b && (n = fread(b, 1, 8192, fp)) > 0)
        if (httpd_resp_send_chunk(r, b, n) != ESP_OK) break;
    fclose(fp);
    heap_caps_free(b);
    return httpd_resp_send_chunk(r, NULL, 0);
}

static esp_err_t h_upload(httpd_req_t *r)
{
    if (!authorised(r)) return ESP_OK;
    if (!sd_ok()) return fail(r, "507 Insufficient Storage", "no microSD");
    char f[64], t[12] = "media", file[64], path[128], tmp[128];
    if (!get_query(r, "f", f, sizeof(f)) || !clean_file(f, file, sizeof(file))) return fail(r, "400 Bad Request", "name");
    get_query(r, "t", t, sizeof(t));
    bool thumb = !strcmp(t, "thumb");
    if (thumb) {
        char *dot = strrchr(file, '.');
        *dot = 0;
        snprintf(path, sizeof(path), THUMB_DIR "/%s.jpg", file);
        snprintf(tmp, sizeof(tmp), THUMB_DIR "/upload.tmp");
    } else {
        snprintf(path, sizeof(path), MEDIA_DIR "/%s", file);
        snprintf(tmp, sizeof(tmp), MEDIA_DIR "/upload.tmp");
    }
    uint64_t total = 0, free_b = 0;
    sd_space(&total, &free_b);
    if ((uint64_t)r->content_len + 1024 * 1024 > free_b) return fail(r, "507 Insufficient Storage", "card full");

    FILE *fp = fopen(tmp, "wb");
    if (!fp) return fail(r, "500 Internal Server Error", "cannot write");
    const int BUF = 32 * 1024;
    char *b = heap_caps_malloc(BUF, MALLOC_CAP_SPIRAM);
    if (!b) { fclose(fp); return fail(r, "500 Internal Server Error", "memory"); }
    if (!thumb) { strlcpy(current, file, sizeof(current)); progress = 0; }
    size_t left = r->content_len, got = 0;
    bool ok = true;
    while (left > 0) {
        int n = httpd_req_recv(r, b, left < (size_t)BUF ? left : BUF);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0 || fwrite(b, 1, n, fp) != (size_t)n) { ok = false; break; }
        left -= n;
        got += n;
        if (!thumb && r->content_len) progress = (int)(got * 100 / r->content_len);
    }
    heap_caps_free(b);
    fclose(fp);
    if (!ok) {
        remove(tmp);
        progress = -1;
        return fail(r, "500 Internal Server Error", "transfer interrupted");
    }
    remove(path);
    if (rename(tmp, path) != 0) { remove(tmp); progress = -1; return fail(r, "500 Internal Server Error", "rename"); }
    if (!thumb) {
        uploads++;
        progress = -1;
        media_scan();   // the gallery sees it right away
    } else {
        media_touch();  // the gallery reloads its previews
    }
    ESP_LOGI(TAG, "received %s (%u bytes)", path, (unsigned)got);
    return send_json(r, "{\"ok\":true}");
}

static esp_err_t h_delete(httpd_req_t *r)
{
    if (!authorised(r)) return ESP_OK;
    char f[64], file[64];
    if (!get_query(r, "f", f, sizeof(f)) || !clean_file(f, file, sizeof(file))) return fail(r, "400 Bad Request", "name");
    int i = media_find(file);
    if (i < 0 || !media_delete(i)) return fail(r, "404 Not Found", "not found");
    return send_json(r, "{\"ok\":true}");
}

static esp_err_t h_scan(httpd_req_t *r)
{
    // results of the scan started with the page (and refreshed by every call)
    wifi_ap_t aps[24];
    int n = wifi_mgr_scan_results(aps, 24);
    if (!wifi_mgr_scan_busy()) wifi_mgr_scan_start();
    httpd_resp_set_type(r, "application/json");
    httpd_resp_sendstr_chunk(r, "[");
    for (int i = 0; i < n; i++) {
        char e[120] = "{\"s\":";
        json_str(e, sizeof(e), aps[i].ssid);
        int l = strlen(e);
        snprintf(e + l, sizeof(e) - l, ",\"r\":%d,\"l\":%d}%s", aps[i].rssi, aps[i].auth ? 1 : 0, i < n - 1 ? "," : "");
        httpd_resp_sendstr_chunk(r, e);
    }
    httpd_resp_sendstr_chunk(r, "]");
    return httpd_resp_sendstr_chunk(r, NULL);
}

static esp_err_t h_wifi(httpd_req_t *r)
{
    char body[200];
    int len = r->content_len < sizeof(body) - 1 ? r->content_len : sizeof(body) - 1, got = 0;
    while (got < len) {
        int n = httpd_req_recv(r, body + got, len - got);
        if (n <= 0) return fail(r, "400 Bad Request", "body");
        got += n;
    }
    body[got] = 0;
    char s[33], p[65];
    if (!query_param(body, "s", s, sizeof(s)) || !s[0]) return fail(r, "400 Bad Request", "ssid");
    query_param(body, "p", p, sizeof(p));
    strlcpy(wifi_s, s, sizeof(wifi_s));
    strlcpy(wifi_p, p, sizeof(wifi_p));
    wifi_pending = true;
    return send_json(r, "{\"ok\":true}");
}

/* ---------------- start / stop ---------------- */

static void reg(const char *uri, httpd_method_t m, esp_err_t (*h)(httpd_req_t *))
{
    httpd_uri_t u = {.uri = uri, .method = m, .handler = h};
    httpd_register_uri_handler(srv, &u);
}

void web_start(web_mode_t m)
{
    if (srv) web_stop();
    mode = m;
    snprintf(pin, sizeof(pin), "%04u", (unsigned)(esp_random() % 10000));
    uploads = 0;
    progress = -1;
    bad_pins = 0;
    seen = false;
    current[0] = 0;
    wifi_pending = false;
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.max_uri_handlers = 16;
    c.uri_match_fn = httpd_uri_match_wildcard;
    c.stack_size = 8192;
    c.recv_wait_timeout = 20;
    c.send_wait_timeout = 20;
    c.lru_purge_enable = true;
    c.max_open_sockets = 6;
    if (httpd_start(&srv, &c) != ESP_OK) { srv = NULL; mode = WEB_OFF; ESP_LOGE(TAG, "server not started"); return; }
    reg("/", HTTP_GET, h_page);
    reg("/api/info", HTTP_GET, h_info);
    if (m == WEB_UPLOAD) {
        reg("/api/auth", HTTP_POST, h_auth);
        reg("/api/list", HTTP_GET, h_list);
        reg("/api/thumb", HTTP_GET, h_thumb);
        reg("/api/upload", HTTP_POST, h_upload);
        reg("/api/delete", HTTP_POST, h_delete);
    } else {
        reg("/api/scan", HTTP_GET, h_scan);
        reg("/api/wifi", HTTP_POST, h_wifi);
        wifi_mgr_scan_start();
    }
    reg("/*", HTTP_GET, h_any);
    ESP_LOGI(TAG, "server on (%s)", m == WEB_WIFI ? "wifi setup" : "upload");
}

void web_stop(void)
{
    if (srv) httpd_stop(srv);
    srv = NULL;
    mode = WEB_OFF;
    progress = -1;
}

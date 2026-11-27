// wifi_mgr.c - see wifi_mgr.h. Reconnection logic adapted from raxp122/ESP32-Developement.
#include "wifi_mgr.h"
#include "settings.h"
#include "board.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "wifi";

static esp_netif_t *sta_netif, *ap_netif;
static bool started, ap_on;
static wifi_mode_t cur_mode = WIFI_MODE_NULL;
static volatile wifi_state_t state = WIFI_OFF;
static char ip_str[16], ap_ssid[24];
static volatile bool scanning, attempt, conn_fail, self_disc, synced, dns_run;
static volatile int ap_clients;
static int conn_try, retry_ms = 1000, scan_users;
static volatile uint32_t scan_gen;
static wifi_ap_t aps[24];
static int ap_n;
static SemaphoreHandle_t mtx;
static esp_timer_handle_t reconnect_timer;
static bool sntp_started;

static volatile bool suspended;
static bool want_sta(void) { return g_set.wifi_on && g_set.wifi_ssid[0] && !suspended; }
static void update_mode(void);

static void sta_connect(void)
{
    esp_timer_stop(reconnect_timer);
    attempt = true;
    if (esp_wifi_connect() != ESP_OK) {
        attempt = false;
        esp_timer_start_once(reconnect_timer, (uint64_t)retry_ms * 1000);
    }
}

static void own_disconnect(void)
{
    self_disc = state == WIFI_CONNECTED || attempt;
    esp_wifi_disconnect();
}

/* ---------------- NTP ---------------- */

static void time_cb(struct timeval *tv)
{
    synced = true;
    struct tm utc;
    gmtime_r(&tv->tv_sec, &utc);
    bool ok = board_rtc_write(&utc);
    ESP_LOGI(TAG, "time synchronised%s", ok ? " and saved in the RTC" : "");
}

static void sntp_kick(void)
{
    if (!sntp_started) {
        esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        c.sync_cb = time_cb;
        esp_netif_sntp_init(&c);
        sntp_started = true;
    } else {
        esp_netif_sntp_start();
    }
}

bool wifi_mgr_time_synced(void) { return synced; }

/* ---------------- station ---------------- */

static void apply_sta_config(void)
{
    wifi_config_t c = {0};
    strlcpy((char *)c.sta.ssid, g_set.wifi_ssid, sizeof(c.sta.ssid));
    strlcpy((char *)c.sta.password, g_set.wifi_pass, sizeof(c.sta.password));
    c.sta.threshold.authmode = g_set.wifi_pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    c.sta.pmf_cfg.capable = true;
    wifi_config_t cur;
    // writing the same config while connected drops the connection
    if (started && esp_wifi_get_config(WIFI_IF_STA, &cur) == ESP_OK &&
        !strncmp((char *)cur.sta.ssid, (char *)c.sta.ssid, sizeof(c.sta.ssid)) &&
        !strncmp((char *)cur.sta.password, (char *)c.sta.password, sizeof(c.sta.password)))
        return;
    esp_wifi_set_config(WIFI_IF_STA, &c);
}

static void reconnect_cb(void *arg)
{
    if (!started || !want_sta() || state == WIFI_CONNECTED) return;
    if (conn_fail) { conn_fail = false; conn_try = 0; retry_ms = 1000; state = WIFI_CONNECTING; }
    if (scanning) { esp_timer_start_once(reconnect_timer, 1500 * 1000); return; }
    sta_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (want_sta()) { state = WIFI_CONNECTING; sta_connect(); }
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *d = data;
            if (self_disc && d && d->reason == WIFI_REASON_ASSOC_LEAVE) { self_disc = false; break; }
            self_disc = false;
            attempt = false;
            ip_str[0] = 0;
            if (!started || !want_sta()) { state = g_set.wifi_on ? WIFI_NO_NETWORK : WIFI_OFF; break; }
            int r = d ? d->reason : 0;
            bool auth_err = r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || r == WIFI_REASON_HANDSHAKE_TIMEOUT ||
                            r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_AUTH_EXPIRE ||
                            r == WIFI_REASON_CONNECTION_FAIL || r == WIFI_REASON_NO_AP_FOUND;
            esp_timer_stop(reconnect_timer);
            if (auth_err && ++conn_try >= 3) {
                // stop hammering; try again now and then (the network may come back)
                conn_fail = true;
                state = WIFI_NO_NETWORK;
                esp_timer_start_once(reconnect_timer, 60ULL * 1000 * 1000);
                break;
            }
            state = WIFI_CONNECTING;
            esp_timer_start_once(reconnect_timer, (uint64_t)retry_ms * 1000);
            retry_ms = retry_ms * 2 > 30000 ? 30000 : retry_ms * 2;
            break;
        }
        case WIFI_EVENT_SCAN_DONE: {
            scanning = false;
            uint16_t n = 32;
            wifi_ap_record_t *rec = calloc(n, sizeof(*rec));
            if (rec && esp_wifi_scan_get_ap_records(&n, rec) == ESP_OK) {
                xSemaphoreTake(mtx, portMAX_DELAY);
                ap_n = 0;
                for (int i = 0; i < n && ap_n < (int)(sizeof(aps) / sizeof(aps[0])); i++) {
                    if (!rec[i].ssid[0]) continue;
                    bool dup = false;   // the same network on several access points: once
                    for (int k = 0; k < ap_n; k++) dup |= !strcmp(aps[k].ssid, (char *)rec[i].ssid);
                    if (dup) continue;
                    wifi_ap_t *a = &aps[ap_n++];
                    strlcpy(a->ssid, (char *)rec[i].ssid, sizeof(a->ssid));
                    a->rssi = rec[i].rssi;
                    a->auth = rec[i].authmode;
                }
                xSemaphoreGive(mtx);
            } else {
                esp_wifi_clear_ap_list();
            }
            free(rec);
            scan_gen++;
            if (scan_users > 0) scan_users--;
            update_mode();
            // a connection attempt interrupted by the scan starts again
            if (started && want_sta() && state != WIFI_CONNECTED && !attempt) { state = WIFI_CONNECTING; sta_connect(); }
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED: ap_clients++; break;
        case WIFI_EVENT_AP_STADISCONNECTED: if (ap_clients > 0) ap_clients--; break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&e->ip_info.ip));
        state = WIFI_CONNECTED;
        attempt = false;
        retry_ms = 1000;
        conn_fail = false;
        conn_try = 0;
        esp_netif_dns_info_t dns = {0};
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(1, 1, 1, 1);
        esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_FALLBACK, &dns);
        sntp_kick();
        ESP_LOGI(TAG, "connected, IP %s", ip_str);
    }
}

static void update_mode(void)
{
    bool need = (g_set.wifi_on && !suspended) || ap_on || scan_users > 0;
    if (!need) {
        if (started) { esp_wifi_stop(); started = false; cur_mode = WIFI_MODE_NULL; }
        state = WIFI_OFF;
        attempt = scanning = false;
        ip_str[0] = 0;
        return;
    }
    wifi_mode_t mode = ap_on ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    if (mode != cur_mode) { esp_wifi_set_mode(mode); cur_mode = mode; }
    if (ap_on) {
        wifi_config_t ap = {0};
        strlcpy((char *)ap.ap.ssid, ap_ssid, sizeof(ap.ap.ssid));
        ap.ap.ssid_len = strlen(ap_ssid);
        ap.ap.channel = 6;
        ap.ap.max_connection = 4;
        ap.ap.authmode = WIFI_AUTH_OPEN;
        esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    if (want_sta()) apply_sta_config();
    if (!started) {
        state = want_sta() ? WIFI_CONNECTING : (g_set.wifi_on ? WIFI_NO_NETWORK : WIFI_OFF);
        esp_wifi_start();
        started = true;
        return;
    }
    if (want_sta()) {
        if (state != WIFI_CONNECTED && state != WIFI_CONNECTING) { state = WIFI_CONNECTING; sta_connect(); }
    } else {
        if (state == WIFI_CONNECTED || state == WIFI_CONNECTING) own_disconnect();
        state = g_set.wifi_on ? WIFI_NO_NETWORK : WIFI_OFF;
    }
}

void wifi_mgr_init(void)
{
    mtx = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    const esp_timer_create_args_t ta = {.callback = reconnect_cb, .name = "wifi_rc"};
    esp_timer_create(&ta, &reconnect_timer);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ap_ssid, sizeof(ap_ssid), "AmuletOS-%02X%02X", mac[4], mac[5]);
    esp_netif_set_hostname(sta_netif, "amulet");
    update_mode();
}

void wifi_mgr_apply(void)
{
    if (started && want_sta()) {
        own_disconnect();
        state = WIFI_NO_NETWORK;
        retry_ms = 1000;
        conn_fail = false;
        conn_try = 0;
    }
    update_mode();
}

void wifi_mgr_set_suspend(bool s)
{
    if (s == suspended) return;
    suspended = s;
    if (!mtx) return;   // not started yet: wifi_mgr_init() applies it
    ESP_LOGI(TAG, "radio %s", s ? "paused (battery saving)" : "back on");
    if (!s) { retry_ms = 1000; conn_fail = false; conn_try = 0; }
    update_mode();
}

bool wifi_mgr_connect(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || strlen(pass) > 64) return false;
    strlcpy(g_set.wifi_ssid, ssid, sizeof(g_set.wifi_ssid));
    strlcpy(g_set.wifi_pass, pass, sizeof(g_set.wifi_pass));
    g_set.wifi_on = 1;
    suspended = false;
    settings_save();
    if (scanning) { esp_wifi_scan_stop(); scanning = false; }
    if (started) own_disconnect();
    retry_ms = 1000;
    conn_fail = false;
    conn_try = 0;
    update_mode();
    apply_sta_config();
    state = WIFI_CONNECTING;
    sta_connect();
    return true;
}

void wifi_mgr_forget(void)
{
    g_set.wifi_ssid[0] = 0;
    g_set.wifi_pass[0] = 0;
    settings_save();
    if (started) own_disconnect();
    update_mode();
}

void wifi_mgr_reconnect(void)
{
    if (!started || !want_sta() || scanning) return;
    own_disconnect();
    retry_ms = 1000;
    conn_fail = false;
    conn_try = 0;
    ip_str[0] = 0;
    state = WIFI_CONNECTING;
    sta_connect();
}

wifi_state_t wifi_mgr_state(void) { return state; }
bool wifi_mgr_conn_failed(void) { return conn_fail; }
const char *wifi_mgr_ip(void) { return ip_str; }

int wifi_mgr_rssi(void)
{
    wifi_ap_record_t r;
    if (state == WIFI_CONNECTED && esp_wifi_sta_get_ap_info(&r) == ESP_OK) return r.rssi;
    return 0;
}

/* ---------------- scan ---------------- */

void wifi_mgr_scan_start(void)
{
    if (scanning) return;
    scan_users++;
    update_mode();
    if (attempt) { esp_timer_stop(reconnect_timer); own_disconnect(); attempt = false; }
    wifi_scan_config_t sc = {.show_hidden = false};
    if (esp_wifi_scan_start(&sc, false) == ESP_OK) scanning = true;
    else { scan_users--; scan_gen++; }
}

bool wifi_mgr_scan_busy(void) { return scanning; }
uint32_t wifi_mgr_scan_gen(void) { return scan_gen; }

int wifi_mgr_scan_results(wifi_ap_t *out, int max)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    int n = ap_n < max ? ap_n : max;
    memcpy(out, aps, n * sizeof(wifi_ap_t));
    xSemaphoreGive(mtx);
    // strongest first
    for (int i = 1; i < n; i++)
        for (int k = i; k > 0 && out[k].rssi > out[k - 1].rssi; k--) {
            wifi_ap_t t = out[k]; out[k] = out[k - 1]; out[k - 1] = t;
        }
    return n;
}

/* ---------------- hotspot + captive DNS ---------------- */

static void dns_task(void *arg)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY)};
    struct timeval to = {.tv_sec = 0, .tv_usec = 300000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) { close(s); vTaskDelete(NULL); }
    uint8_t buf[512];
    while (dns_run) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n < 12) continue;
        int q = 12;
        while (q < n && buf[q]) q += buf[q] + 1;
        q += 5;   // terminator + QTYPE + QCLASS
        if (q > n || q + 16 > (int)sizeof(buf)) continue;
        bool type_a = buf[q - 4] == 0 && buf[q - 3] == 1;
        buf[2] = 0x81; buf[3] = 0x80;              // response, no error
        buf[4] = 0; buf[5] = 1;                    // 1 question
        buf[6] = 0; buf[7] = type_a ? 1 : 0;       // 1 answer (only for A)
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        int len = q;
        if (type_a) {
            const uint8_t ans[16] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1};
            memcpy(buf + q, ans, 16);
            len += 16;
        }
        sendto(s, buf, len, 0, (struct sockaddr *)&from, fl);
    }
    close(s);
    vTaskDelete(NULL);
}

void wifi_mgr_ap_start(void)
{
    if (ap_on) return;
    ap_on = true;
    ap_clients = 0;
    update_mode();
    dns_run = true;
    xTaskCreate(dns_task, "dns", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "hotspot %s on", ap_ssid);
}

void wifi_mgr_ap_stop(void)
{
    if (!ap_on) return;
    dns_run = false;
    ap_on = false;
    update_mode();
}

bool wifi_mgr_ap_on(void) { return ap_on; }
const char *wifi_mgr_ap_ssid(void) { return ap_ssid; }
int wifi_mgr_ap_clients(void) { return ap_clients; }

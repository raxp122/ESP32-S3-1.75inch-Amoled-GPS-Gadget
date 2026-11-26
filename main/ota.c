// ota.c - every push to main publishes a release (.github/workflows/build.yml) with
//   version.json      {"version":"0.1.N"}
//   amulet-app.bin    the application, written into the free OTA slot
// With the bootloader rollback on, a new firmware that never reaches ota_mark_valid()
// (crash, reboot loop) is replaced by the previous one at the next restart.
// Settings and media are never touched: NVS is at the end of the flash, media on the microSD.
#include "ota.h"
#include "core.h"
#include "i18n.h"
#include "settings.h"
#include "ui.h"
#include "wifi_mgr.h"
#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

#define URL_VERSION "https://github.com/" OTA_REPO "/releases/latest/download/version.json"
#define URL_APP     "https://github.com/" OTA_REPO "/releases/latest/download/amulet-app.bin"

static volatile ota_state_t state = OTA_IDLE;
static volatile int progress;
static bool busy;
static char latest[32], err_msg[80];
static volatile bool auto_on, auto_done;

ota_state_t ota_state(void) { return state; }
int ota_progress(void) { return progress; }
const char *ota_latest(void) { return latest; }
const char *ota_error(void) { return err_msg; }
const char *ota_current(void) { return esp_app_get_description()->version; }

static void fail(const char *msg)
{
    strlcpy(err_msg, msg, sizeof(err_msg));
    state = OTA_ERROR;
    ESP_LOGW(TAG, "%s", msg);
}

void ota_mark_valid(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware %s confirmed", ota_current());
    }
}

typedef struct { char buf[256]; int len; } body_t;

static esp_err_t on_http(esp_http_client_event_t *e)
{
    body_t *b = e->user_data;
    // GitHub redirects: only the body of the final 200 answer counts
    if (e->event_id == HTTP_EVENT_ON_DATA && esp_http_client_get_status_code(e->client) == 200) {
        int n = e->data_len;
        if (b->len + n > (int)sizeof(b->buf) - 1) n = sizeof(b->buf) - 1 - b->len;
        if (n > 0) { memcpy(b->buf + b->len, e->data, n); b->len += n; }
    }
    return ESP_OK;
}

static bool fetch_latest(void)
{
    body_t body = {0};
    esp_http_client_config_t c = {
        .url = URL_VERSION,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 4096,       // GitHub redirect URLs are long
        .buffer_size_tx = 4096,
        .event_handler = on_http,
        .user_data = &body,
    };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) { fail(T(S_ERR_MEMORY)); return false; }
    esp_err_t r = esp_http_client_perform(h);
    int status = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    if (r != ESP_OK) { fail(T(S_ERR_GITHUB)); return false; }
    if (status == 404) { fail(T(S_ERR_NO_RELEASE)); return false; }
    if (status != 200) { snprintf(err_msg, sizeof(err_msg), "HTTP %d", status); state = OTA_ERROR; return false; }
    body.buf[body.len] = 0;
    const char *k = strstr(body.buf, "\"version\"");
    const char *q = k ? strchr(k + 9, '"') : NULL;
    const char *e = q ? strchr(q + 1, '"') : NULL;
    if (!e || e - q - 1 <= 0 || e - q - 1 >= (int)sizeof(latest)) { fail("version.json?"); return false; }
    memcpy(latest, q + 1, e - q - 1);
    latest[e - q - 1] = 0;
    return true;
}

static void do_check(void)
{
    state = OTA_CHECKING;
    if (wifi_mgr_state() != WIFI_CONNECTED) { fail(T(S_ERR_NO_INTERNET)); return; }
    // right after connecting the first request can fail (DNS, clock not set for TLS yet)
    if (!fetch_latest()) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        state = OTA_CHECKING;
        if (!fetch_latest()) return;
    }
    state = version_compare(latest, ota_current()) > 0 ? OTA_AVAILABLE : OTA_UP_TO_DATE;
    ESP_LOGI(TAG, "running %s, latest %s", ota_current(), latest);
}

static void do_install(void)
{
    if (wifi_mgr_state() != WIFI_CONNECTED) { fail(T(S_ERR_NO_INTERNET)); return; }
    state = OTA_DOWNLOADING;
    progress = 0;
    esp_http_client_config_t http = {
        .url = URL_APP,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t cfg = {.http_config = &http};
    esp_https_ota_handle_t h = NULL;
    esp_app_desc_t d;
    esp_err_t r = ESP_FAIL;
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (attempt > 1) vTaskDelay(pdMS_TO_TICKS(2000));
        h = NULL;
        r = esp_https_ota_begin(&cfg, &h);
        if (r != ESP_OK) continue;
        r = esp_https_ota_get_img_desc(h, &d);
        if (r == ESP_OK) break;
        esp_https_ota_abort(h);
        h = NULL;
    }
    if (r != ESP_OK) { fail(T(S_ERR_DOWNLOAD)); return; }
    // safety: it must really be a firmware of this project
    if (strncmp(d.project_name, esp_app_get_description()->project_name, sizeof(d.project_name))) {
        esp_https_ota_abort(h);
        fail(T(S_ERR_WRONG_FW));
        return;
    }
    int total = esp_https_ota_get_image_size(h);
    while ((r = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int got = esp_https_ota_get_image_len_read(h);
        if (total > 0) progress = got * 100 / total;
    }
    if (r != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
        esp_https_ota_abort(h);
        fail(T(S_ERR_DOWNLOAD));
        return;
    }
    if (esp_https_ota_finish(h) != ESP_OK) { fail(T(S_ERR_WRONG_FW)); return; }
    progress = 100;
    state = OTA_DONE;
    ESP_LOGI(TAG, "installed %s: restarting", d.version);
    vTaskDelay(pdMS_TO_TICKS(2500));
    esp_restart();
}

static void do_auto(void)
{
    while (wifi_mgr_state() != WIFI_CONNECTED) {
        if (!g_set.wifi_on) { auto_on = false; return; }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    vTaskDelay(pdMS_TO_TICKS(15000));   // let NTP and the rest of the start finish
    auto_on = false;
    if (wifi_mgr_state() != WIFI_CONNECTED) return;
    auto_done = true;
    if (state != OTA_IDLE || __atomic_exchange_n(&busy, true, __ATOMIC_ACQ_REL)) return;
    do_check();
    __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
    if (state == OTA_AVAILABLE) {
        char t[96];
        snprintf(t, sizeof(t), T(S_OTA_TOAST), latest);
        ui_toast_async(t);
    } else if (state == OTA_ERROR) {
        state = OTA_IDLE;   // a failed automatic check is not shown as an error
    }
}

typedef enum { JOB_CHECK, JOB_INSTALL, JOB_AUTO } job_t;

static void task(void *arg)
{
    job_t j = (job_t)(intptr_t)arg;
    if (j == JOB_CHECK) do_check();
    else if (j == JOB_INSTALL) do_install();
    else do_auto();
    if (j != JOB_AUTO) __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
    vTaskDelete(NULL);
}

static void run(job_t j)
{
    if (j != JOB_AUTO && __atomic_exchange_n(&busy, true, __ATOMIC_ACQ_REL)) return;
    if (xTaskCreatePinnedToCore(task, "ota", 8192, (void *)(intptr_t)j, 3, NULL, 0) != pdPASS) {
        if (j != JOB_AUTO) __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
        fail(T(S_ERR_MEMORY));
    }
}

void ota_check(void) { run(JOB_CHECK); }
void ota_install(void) { run(JOB_INSTALL); }
void ota_auto_start(void)
{
    if (!g_set.ota_auto || auto_on || auto_done) return;
    auto_on = true;
    run(JOB_AUTO);
}

// packs.c - see packs.h
#include "packs.h"
#include "core.h"
#include "i18n.h"
#include "ota.h"
#include "power.h"
#include "wifi_mgr.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "packs";

static volatile pack_job_t job = PACKJ_IDLE;
static volatile int progress;
static char err[80];
static char job_name[24], job_ver[16];

pack_job_t packs_job(void) { return job; }
int packs_progress(void) { return progress; }
const char *packs_error(void) { return err; }

void packs_path(const char *name, const char *file, char *out, int n)
{
    snprintf(out, n, PACKS_DIR "/%s/%s", name, file);
}

bool packs_ready(const char *name, const char *version)
{
    char p[96], v[16] = {0};
    packs_path(name, ".version", p, sizeof(p));
    FILE *f = fopen(p, "r");
    if (!f) return false;
    size_t n = fread(v, 1, sizeof(v) - 1, f);
    fclose(f);
    v[n] = 0;
    char *nl = strpbrk(v, "\r\n");
    if (nl) *nl = 0;
    return !strcmp(v, version);
}

/* ---------------- unpacking ---------------- */

typedef struct { char base[64]; FILE *f; } unpack_t;

// mkdir -p for the folders of a file path
static void make_dirs(char *path)
{
    for (char *p = path + strlen(SD_MOUNT) + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        mkdir(path, 0775);
        *p = '/';
    }
}

static bool u_open(void *c, const char *name, uint32_t size)
{
    unpack_t *u = c;
    char p[160];
    snprintf(p, sizeof(p), "%s/%s", u->base, name);
    make_dirs(p);
    u->f = fopen(p, "wb");
    if (!u->f) ESP_LOGW(TAG, "cannot write %s (%d)", p, errno);
    return u->f != NULL;
}

static bool u_write(void *c, const uint8_t *d, size_t n)
{
    unpack_t *u = c;
    return fwrite(d, 1, n, u->f) == n;
}

static bool u_close(void *c)
{
    unpack_t *u = c;
    bool ok = fclose(u->f) == 0;
    u->f = NULL;
    return ok;
}

static const pack_cb_t UNPACK = {u_open, u_write, u_close};

static void fail(const char *m)
{
    strlcpy(err, m, sizeof(err));
    ESP_LOGW(TAG, "%s: %s", job_name, m);
    job = PACKJ_FAILED;
}

// GitHub answers with redirects to its storage: follow them by hand to read the body ourselves
static esp_http_client_handle_t open_url(const char *url, int64_t *len)
{
    esp_http_client_config_t c = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) return NULL;
    for (int hop = 0; hop < 6; hop++) {
        if (esp_http_client_open(h, 0) != ESP_OK) break;
        *len = esp_http_client_fetch_headers(h);
        int st = esp_http_client_get_status_code(h);
        if (st == 200) return h;
        if (st >= 301 && st <= 308) {
            esp_http_client_flush_response(h, NULL);
            esp_http_client_set_redirection(h);
            esp_http_client_close(h);
            continue;
        }
        ESP_LOGW(TAG, "HTTP %d", st);
        break;
    }
    esp_http_client_cleanup(h);
    return NULL;
}

static void install(void)
{
    if (!sd_ok()) { fail(T(S_NO_SD)); return; }
    if (wifi_mgr_state() != WIFI_CONNECTED) { fail(T(S_ERR_NO_INTERNET)); return; }
    char url[160];
    snprintf(url, sizeof(url), "https://github.com/" OTA_REPO "/releases/latest/download/%s.amp", job_name);
    int64_t total = 0;
    esp_http_client_handle_t h = NULL;
    for (int attempt = 0; attempt < 3 && !h; attempt++) {
        if (attempt) vTaskDelay(pdMS_TO_TICKS(2000));
        h = open_url(url, &total);
    }
    if (!h) { fail(T(S_ERR_DOWNLOAD)); return; }
    unpack_t u = {0};
    mkdir(PACKS_DIR, 0775);
    snprintf(u.base, sizeof(u.base), PACKS_DIR "/%s", job_name);
    mkdir(u.base, 0775);
    char vp[96];
    packs_path(job_name, ".version", vp, sizeof(vp));
    remove(vp);   // not ready until the whole pack is in
    pack_reader_t r;
    pack_reader_init(&r);
    uint8_t *buf = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    bool ok = buf != NULL;
    int64_t got = 0;
    while (ok && !r.done) {
        int n = esp_http_client_read(h, (char *)buf, 8192);
        if (n <= 0) { ok = false; break; }
        got += n;
        if (total > 0) progress = (int)(got * 100 / total);
        ok = pack_feed(&r, buf, n, &UNPACK, &u);
    }
    if (u.f) fclose(u.f);
    heap_caps_free(buf);
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    if (!ok || !r.done) { fail(T(S_ERR_DOWNLOAD)); return; }
    FILE *f = fopen(vp, "w");
    if (!f || fputs(job_ver, f) < 0) { if (f) fclose(f); fail(T(S_ERR_DOWNLOAD)); return; }
    fclose(f);
    ESP_LOGI(TAG, "%s %s: %u files", job_name, job_ver, (unsigned)r.files);
    progress = 100;
    job = PACKJ_DONE;
}

static void task(void *arg)
{
    power_hold(true);
    power_boost_begin();
    install();
    power_boost_end();
    power_hold(false);
    vTaskDelete(NULL);
}

void packs_install(const char *name, const char *version)
{
    if (job == PACKJ_RUNNING) return;
    strlcpy(job_name, name, sizeof(job_name));
    strlcpy(job_ver, version, sizeof(job_ver));
    progress = 0;
    err[0] = 0;
    job = PACKJ_RUNNING;
    if (xTaskCreatePinnedToCore(task, "packs", 8192, NULL, 3, NULL, 0) != pdPASS) fail(T(S_ERR_MEMORY));
}

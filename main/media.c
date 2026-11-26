// media.c - media list on the microSD and JPEG decoding (esp_new_jpeg, SIMD on the ESP32-S3)
#include "media.h"
#include "sd.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_jpeg_dec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "media";
static media_item_t *items;
static int count;
static volatile uint32_t gen;
static SemaphoreHandle_t mtx;

static void lock(void) { if (!mtx) mtx = xSemaphoreCreateRecursiveMutex(); xSemaphoreTakeRecursive(mtx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGiveRecursive(mtx); }

uint32_t media_generation(void) { return gen; }
void media_touch(void) { gen++; }

static int cmp_items(const void *a, const void *b)
{
    const media_item_t *x = a, *y = b;
    if (x->mtime != y->mtime) return x->mtime > y->mtime ? -1 : 1;   // newest first
    return strcasecmp(x->file, y->file);
}

int media_scan(void)
{
    lock();
    if (!items) items = heap_caps_calloc(MEDIA_MAX, sizeof(media_item_t), MALLOC_CAP_SPIRAM);
    count = 0;
    DIR *d = sd_ok() && items ? opendir(MEDIA_DIR) : NULL;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && count < MEDIA_MAX) {
            if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(items[0].file)) continue;
            media_kind_t k = media_kind(e->d_name);
            if (k == MEDIA_NONE) continue;
            char path[96];
            snprintf(path, sizeof(path), MEDIA_DIR "/%s", e->d_name);
            struct stat st;
            if (stat(path, &st) != 0 || st.st_size < 4) continue;
            media_item_t *m = &items[count++];
            strlcpy(m->file, e->d_name, sizeof(m->file));
            m->kind = k;
            m->size = st.st_size;
            m->mtime = st.st_mtime;
        }
        closedir(d);
        qsort(items, count, sizeof(media_item_t), cmp_items);
    }
    gen++;
    int n = count;
    unlock();
    ESP_LOGI(TAG, "%d media on the card", n);
    return n;
}

int media_count(void) { return count; }
const media_item_t *media_get(int i) { return i >= 0 && i < count ? &items[i] : NULL; }

int media_find(const char *file)
{
    for (int i = 0; i < count; i++)
        if (!strcmp(items[i].file, file)) return i;
    return -1;
}

void media_display_name(const media_item_t *m, char *out, int n)
{
    strlcpy(out, m->file, n);
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;
    for (char *p = out; *p; p++) if (*p == '_') *p = ' ';
}

bool media_delete(int i)
{
    lock();
    const media_item_t *m = media_get(i);
    bool ok = false;
    if (m) {
        char path[128], base[64];
        snprintf(path, sizeof(path), MEDIA_DIR "/%s", m->file);
        ok = remove(path) == 0;
        strlcpy(base, m->file, sizeof(base));
        char *dot = strrchr(base, '.');
        if (dot) *dot = 0;
        snprintf(path, sizeof(path), THUMB_DIR "/%s.jpg", base);
        remove(path);
        media_scan();
    }
    unlock();
    return ok;
}

/* ---------------- decoding ---------------- */

uint16_t *media_frame_alloc(int w, int h)
{
    return heap_caps_aligned_calloc(16, 1, (size_t)w * h * 2, MALLOC_CAP_SPIRAM);
}

static uint16_t *tmp_buf;   // for images of another size
static int tmp_len;

bool media_decode(const uint8_t *jpg, int len, uint16_t *out, int out_w, int out_h)
{
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpeg_dec_handle_t dec = NULL;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) return false;
    jpeg_dec_io_t io = {.inbuf = (uint8_t *)jpg, .inbuf_len = len};
    jpeg_dec_header_info_t info;
    bool ok = false;
    if (jpeg_dec_parse_header(dec, &io, &info) != JPEG_ERR_OK) goto done;
    if (info.width == out_w && info.height == out_h) {
        io.outbuf = (uint8_t *)out;
        ok = jpeg_dec_process(dec, &io) == JPEG_ERR_OK;
        goto done;
    }
    int need = 0;
    if (jpeg_dec_get_outbuf_len(dec, &need) != JPEG_ERR_OK || need <= 0 || need > 1024 * 1024 * 3) goto done;
    lock();   // tmp_buf is shared by the player and the gallery
    if (need > tmp_len) {
        heap_caps_free(tmp_buf);
        tmp_buf = heap_caps_aligned_alloc(16, need, MALLOC_CAP_SPIRAM);
        tmp_len = tmp_buf ? need : 0;
        if (!tmp_buf) { unlock(); goto done; }
    }
    io.outbuf = (uint8_t *)tmp_buf;
    if (jpeg_dec_process(dec, &io) != JPEG_ERR_OK) { unlock(); goto done; }
    // centre: crop what does not fit, black around what is smaller
    memset(out, 0, (size_t)out_w * out_h * 2);
    int ox = (out_w - info.width) / 2, oy = (out_h - info.height) / 2;
    for (int y = 0; y < out_h; y++) {
        int sy = y - oy;
        if (sy < 0 || sy >= info.height) continue;
        int x0 = ox > 0 ? ox : 0, sx0 = ox > 0 ? 0 : -ox;
        int w = info.width - sx0;
        if (x0 + w > out_w) w = out_w - x0;
        if (w > 0) memcpy(out + y * out_w + x0, tmp_buf + sy * info.width + sx0, w * 2);
    }
    unlock();
    ok = true;
done:
    jpeg_dec_close(dec);
    return ok;
}

uint8_t *media_read_file(const char *path, int *len, int max_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = n > 0 && n <= max_len ? heap_caps_malloc(n, MALLOC_CAP_SPIRAM) : NULL;
    if (b && fread(b, 1, n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *len = (int)n;
    return b;
}

// first frame of an AMJ (or the whole JPEG) into PSRAM
static uint8_t *first_jpeg(const media_item_t *m, int *len)
{
    char path[96];
    snprintf(path, sizeof(path), MEDIA_DIR "/%s", m->file);
    if (m->kind == MEDIA_IMAGE) return media_read_file(path, len, 2 * 1024 * 1024);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t hdr[AMJ_HEADER + AMJ_ENTRY];
    uint8_t *b = NULL;
    amj_info_t info;
    amj_frame_t fr;
    if (fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr) && amj_parse_header(hdr, sizeof(hdr), m->size, &info) &&
        amj_entry(hdr + AMJ_HEADER, 0, m->size, &fr) && fr.size < 1024 * 1024 &&
        (b = heap_caps_malloc(fr.size, MALLOC_CAP_SPIRAM)) && fseek(f, fr.offset, SEEK_SET) == 0 &&
        fread(b, 1, fr.size, f) == fr.size) {
        *len = fr.size;
    } else {
        free(b);
        b = NULL;
    }
    fclose(f);
    return b;
}

bool media_thumb(int i, uint16_t *out)
{
    const media_item_t *m = media_get(i);
    if (!m) return false;
    char base[64], path[128];
    strlcpy(base, m->file, sizeof(base));
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;
    snprintf(path, sizeof(path), THUMB_DIR "/%s.jpg", base);
    int len = 0;
    uint8_t *j = media_read_file(path, &len, 512 * 1024);
    if (j) {
        bool ok = media_decode(j, len, out, THUMB_SIZE, THUMB_SIZE);
        free(j);
        if (ok) return true;
    }
    // no thumbnail (file copied by hand): decode the media and shrink it
    j = first_jpeg(m, &len);
    if (!j) return false;
    static uint16_t *big;
    if (!big) big = media_frame_alloc(466, 466);
    bool ok = big && media_decode(j, len, big, 466, 466);
    free(j);
    if (!ok) return false;
    for (int y = 0; y < THUMB_SIZE; y++)
        for (int x = 0; x < THUMB_SIZE; x++)
            out[y * THUMB_SIZE + x] = big[(y * 466 / THUMB_SIZE) * 466 + x * 466 / THUMB_SIZE];
    return true;
}

// display.c - CO5300 466x466 AMOLED over QSPI, LVGL task and software rotation.
// Init sequence, 6-column offset and brightness as in the Waveshare BSP. The CO5300 accepts
// partial windows (CASET/RASET): LVGL draws into a full frame in PSRAM (DIRECT mode) and only
// the changed areas are sent, aligned to even coordinates as the controller requires.
#include "display.h"
#include "board.h"
#include "power.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_timer.h"
#include "esp_log.h"

#define HOST        SPI3_HOST
#define W           LCD_W
#define H           LCD_H
#define CHUNK_ROWS  24
#define CHUNK_PX    (W * CHUNK_ROWS)

static const char *TAG = "display";
static esp_lcd_panel_io_handle_t io;
static SemaphoreHandle_t done_sem, lv_mux;
// The panel is written by the LVGL task and by the media player: one at a time (pixels,
// windows and brightness commands must not interleave)
static SemaphoreHandle_t panel_mux;
static uint16_t *dma_buf[2];
static int dma_idx;
static lv_display_t *disp;
static uint8_t *lv_fb;
static volatile bool dark, direct;
static bool task_on;
static int rot;

typedef struct { uint8_t cmd; uint8_t data[4]; uint8_t len; uint16_t delay_ms; } init_cmd_t;
static const init_cmd_t init_cmds[] = {
    {0xFE, {0x20}, 1, 0},
    {0x19, {0x10}, 1, 0},
    {0x1C, {0xA0}, 1, 0},
    {0xFE, {0x00}, 1, 0},
    {0xC4, {0x80}, 1, 0},
    {0x3A, {0x55}, 1, 0},          // RGB565
    {0x35, {0x00}, 1, 0},          // TE on
    {0x53, {0x20}, 1, 0},
    {0x51, {0x00}, 1, 0},          // brightness 0 until the first frame is ready
    {0x63, {0xFF}, 1, 0},
    {0x2A, {0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, {0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, {0}, 0, 600},           // sleep out
    {0x29, {0}, 0, 0},             // display on
};

static esp_err_t cmd(uint8_t c, const void *data, size_t len)
{
    return esp_lcd_panel_io_tx_param(io, (0x02 << 24) | (c << 8), data, len);
}

static bool on_done(esp_lcd_panel_io_handle_t h, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(done_sem, &woken);
    return woken == pdTRUE;
}

static void window(int x1, int y1, int x2, int y2)
{
    x1 += LCD_X_GAP;
    x2 += LCD_X_GAP;
    uint8_t c[4] = {x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF};
    uint8_t r[4] = {y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF};
    cmd(0x2A, c, 4);
    cmd(0x2B, r, 4);
}

// logical (upright) pixel -> physical panel pixel, for clockwise rotation rot*90
static inline void l2p(int lx, int ly, int *px, int *py)
{
    switch (rot) {
    case 1:  *px = W - 1 - ly; *py = lx; break;
    case 2:  *px = W - 1 - lx; *py = H - 1 - ly; break;
    case 3:  *px = ly; *py = H - 1 - lx; break;
    default: *px = lx; *py = ly; break;
    }
}

// physical -> logical (inverse of l2p)
static inline void p2l(int px, int py, int *lx, int *ly)
{
    switch (rot) {
    case 1:  *lx = py; *ly = W - 1 - px; break;
    case 2:  *lx = W - 1 - px; *ly = H - 1 - py; break;
    case 3:  *lx = H - 1 - py; *ly = px; break;
    default: *lx = px; *ly = py; break;
    }
}

void display_map_touch(int *x, int *y)
{
    int lx, ly;
    p2l(*x, *y, &lx, &ly);
    *x = lx;
    *y = ly;
}

// Sends the logical area (x1..x2, y1..y2) of a full logical frame (W x H). swap: the source
// is little-endian (LVGL) and the panel wants big-endian. Two DMA buffers: one is filled
// while the other is on the wire.
static void send_area_locked(const uint16_t *frame, int x1, int y1, int x2, int y2, bool swap);

static void send_area(const uint16_t *frame, int x1, int y1, int x2, int y2, bool swap)
{
    xSemaphoreTakeRecursive(panel_mux, portMAX_DELAY);
    send_area_locked(frame, x1, y1, x2, y2, swap);
    xSemaphoreGiveRecursive(panel_mux);
}

static void send_area_locked(const uint16_t *frame, int x1, int y1, int x2, int y2, bool swap)
{
    int ax, ay, bx, by;
    l2p(x1, y1, &ax, &ay);
    l2p(x2, y2, &bx, &by);
    int px1 = ax < bx ? ax : bx, px2 = ax < bx ? bx : ax;
    int py1 = ay < by ? ay : by, py2 = ay < by ? by : ay;
    int w = px2 - px1 + 1;
    int rows = CHUNK_PX / w;
    if (rows < 1) rows = 1;
    for (int y = py1; y <= py2; y += rows) {
        int n = y + rows - 1 > py2 ? py2 - y + 1 : rows;
        uint16_t *d = dma_buf[dma_idx];
        for (int r = 0; r < n; r++) {
            int py = y + r;
            if (rot == 0) {
                const uint16_t *s = frame + py * W + px1;
                if (swap) for (int i = 0; i < w; i++) d[i] = (uint16_t)((s[i] << 8) | (s[i] >> 8));
                else memcpy(d, s, w * 2);
            } else {
                for (int i = 0; i < w; i++) {
                    int lx, ly;
                    p2l(px1 + i, py, &lx, &ly);
                    uint16_t v = frame[ly * W + lx];
                    d[i] = swap ? (uint16_t)((v << 8) | (v >> 8)) : v;
                }
            }
            d += w;
        }
        // the previous transfer must be finished before the window changes
        if (xSemaphoreTake(done_sem, pdMS_TO_TICKS(200)) != pdTRUE) { xSemaphoreGive(done_sem); return; }
        window(px1, y, px2, y + n - 1);
        if (esp_lcd_panel_io_tx_color(io, (0x32 << 24) | (0x2C << 8), dma_buf[dma_idx], w * n * 2) != ESP_OK) {
            xSemaphoreGive(done_sem);
            return;
        }
        dma_idx ^= 1;
    }
    // wait for the last chunk: LVGL may draw again right after flush_ready
    if (xSemaphoreTake(done_sem, pdMS_TO_TICKS(200)) == pdTRUE) xSemaphoreGive(done_sem);
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    if (!dark && !direct) send_area((const uint16_t *)px, a->x1, a->y1, a->x2, a->y2, true);
    lv_display_flush_ready(d);
}

// the CO5300 wants windows starting on even and ending on odd coordinates (W is even, so
// this survives every rotation)
static void rounder_cb(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
    if (a->x2 > W - 1) a->x2 = W - 1;
    if (a->y2 > H - 1) a->y2 = H - 1;
}

// LVGL time straight from the system timer: a periodic 2 ms tick interrupt would wake the
// chip from light sleep 500 times a second
static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void display_init(int rotation)
{
    rot = rotation & 3;
    lv_mux = xSemaphoreCreateRecursiveMutex();
    panel_mux = xSemaphoreCreateRecursiveMutex();
    done_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(done_sem);

    gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_LCD_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_PCLK,
        .data0_io_num = PIN_LCD_D0,
        .data1_io_num = PIN_LCD_D1,
        .data2_io_num = PIN_LCD_D2,
        .data3_io_num = PIN_LCD_D3,
        .max_transfer_sz = CHUNK_PX * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t ioc = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_done,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(HOST, &ioc, &io));

    gpio_set_level(PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_LCD_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(150));
    for (size_t i = 0; i < sizeof(init_cmds) / sizeof(init_cmds[0]); i++) {
        if (cmd(init_cmds[i].cmd, init_cmds[i].len ? init_cmds[i].data : NULL, init_cmds[i].len) != ESP_OK)
            ESP_LOGW(TAG, "command 0x%02X not sent", init_cmds[i].cmd);
        if (init_cmds[i].delay_ms) vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }

    for (int i = 0; i < 2; i++) dma_buf[i] = heap_caps_malloc(CHUNK_PX * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_fb = heap_caps_calloc(1, W * H * 2, MALLOC_CAP_SPIRAM);
    assert(dma_buf[0] && dma_buf[1] && lv_fb);

    lv_init();
    disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_set_buffers(disp, lv_fb, NULL, W * H * 2, LV_DISPLAY_RENDER_MODE_DIRECT);

    lv_tick_set_cb(tick_ms);
}

int display_rotation(void) { return rot; }

void display_set_rotation(int r)
{
    display_lock();
    rot = r & 3;
    if (!direct) lv_obj_invalidate(lv_screen_active());
    display_unlock();
}

static void lvgl_refresh(bool on)
{
    lv_timer_t *t = lv_display_get_refr_timer(disp);
    if (on) {
        lv_timer_resume(t);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(disp);
    } else {
        lv_timer_pause(t);
    }
}

void display_set_brightness(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    bool to_sleep = false;
    display_lock();
    if (dark && pct > 0) {
        // panel out of sleep (it needs ~120 ms), then the current frame, then the light
        xSemaphoreTakeRecursive(panel_mux, portMAX_DELAY);
        cmd(0x11, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
        cmd(0x29, NULL, 0);
        xSemaphoreGiveRecursive(panel_mux);
        dark = false;
        if (task_on && !direct) lvgl_refresh(true);
    } else if (!dark && pct == 0) {
        dark = true;
        to_sleep = true;
        if (task_on && !direct) lvgl_refresh(false);
    }
    display_unlock();
    uint8_t v = (uint8_t)(pct * 255 / 100);
    xSemaphoreTakeRecursive(panel_mux, portMAX_DELAY);
    cmd(0x51, &v, 1);
    if (to_sleep) {   // screen off: display off + sleep in, the panel draws almost nothing
        cmd(0x28, NULL, 0);
        cmd(0x10, NULL, 0);
    }
    xSemaphoreGiveRecursive(panel_mux);
}

bool display_is_dark(void) { return dark; }

void display_direct_begin(void)
{
    display_lock();
    if (!direct) {
        direct = true;
        if (task_on) lv_timer_pause(lv_display_get_refr_timer(disp));
    }
    display_unlock();
}

void display_direct_push(const uint16_t *frame)
{
    if (!direct || dark) return;
    send_area(frame, 0, 0, W - 1, H - 1, true);
}

void display_direct_end(void)
{
    display_lock();
    if (direct) {
        direct = false;
        if (task_on && !dark) lvgl_refresh(true);
    }
    display_unlock();
}

bool display_direct_on(void) { return direct; }

void display_lock(void) { xSemaphoreTakeRecursive(lv_mux, portMAX_DELAY); }
void display_unlock(void) { xSemaphoreGiveRecursive(lv_mux); }

static void lvgl_task(void *arg)
{
    for (;;) {
        display_lock();
        power_boost_begin();   // full speed only while LVGL works
        uint32_t wait = lv_timer_handler();
        power_boost_end();
        display_unlock();
        if (wait < 5) wait = 5;
        if (wait > (dark ? 200u : 30u)) wait = dark ? 200 : 30;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

void display_start_task(void)
{
    task_on = true;
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 12 * 1024, NULL, 4, NULL, 1);
}

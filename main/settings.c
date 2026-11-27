// settings.c - NVS storage. The NVS partition sits at the end of the flash (partitions.csv),
// where neither the USB image nor an OTA update ever writes.
#include "settings.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

settings_t g_set;
static bool dirty;

void settings_mark(void) { dirty = true; }
void settings_flush(void) { if (dirty) settings_save(); }

static void defaults(settings_t *s)
{
    memset(s, 0, sizeof(*s));
    s->brightness = 60;
    s->rotation = 0;
    s->accent = 0;
    s->lang = 0;
    s->sleep_s = 30;
    s->wifi_on = 1;
    s->ota_auto = 1;
    s->tz = 0;
    s->disp_brightness = 60;
    s->disp_interval_s = 30;
    s->disp_shuffle = 0;
    s->disp_off_min = 0;
    s->motion_wake = 1;
    s->night_on = 0;
    s->night_from_h = 23;
    s->night_to_h = 7;
    s->night_mode = 0;
    s->disp_slideshow = 1;
    s->dice = (dice_spec_t){1, 20, 0, DICE_NORMAL, 0};
    s->dice_shake = 1;
    s->home_loop = 1;
    s->power_save = 1;
    s->wifi_save = 1;
    s->battery_mah = 500;
    s->battery_care = 0;
}

void settings_load(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    defaults(&g_set);
    nvs_handle_t h;
    if (nvs_open("amulet", NVS_READONLY, &h) != ESP_OK) return;
    size_t len = 0;
    if (nvs_get_blob(h, "set", NULL, &len) == ESP_OK && len > 0) {
        uint8_t buf[sizeof(settings_t) + 64];
        if (len > sizeof(buf)) len = sizeof(buf);
        if (nvs_get_blob(h, "set", buf, &len) == ESP_OK)
            memcpy(&g_set, buf, len < sizeof(g_set) ? len : sizeof(g_set));
    }
    nvs_close(h);
    // sanity: values out of range from a damaged blob
    if (g_set.brightness < 5 || g_set.brightness > 100) g_set.brightness = 70;
    if (g_set.disp_brightness < 5 || g_set.disp_brightness > 100) g_set.disp_brightness = 70;
    g_set.rotation &= 3;
    g_set.wifi_ssid[32] = 0;
    g_set.wifi_pass[64] = 0;
    g_set.disp_last[sizeof(g_set.disp_last) - 1] = 0;
    if (g_set.night_from_h > 23) g_set.night_from_h = 23;
    if (g_set.night_to_h > 23) g_set.night_to_h = 7;
    if (g_set.disp_interval_s < 3) g_set.disp_interval_s = 30;
    g_set.dice = dice_clamp(g_set.dice);
    if (g_set.battery_mah < 100 || g_set.battery_mah > 5000) g_set.battery_mah = 500;
}

void settings_save(void)
{
    nvs_handle_t h;
    dirty = false;
    if (nvs_open("amulet", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "set", &g_set, sizeof(g_set));
    nvs_commit(h);
    nvs_close(h);
}

void settings_reset(void)
{
    // the Wi-Fi network stays: resetting the look must not cut the updates off
    settings_t keep = g_set;
    defaults(&g_set);
    memcpy(g_set.wifi_ssid, keep.wifi_ssid, sizeof(keep.wifi_ssid));
    memcpy(g_set.wifi_pass, keep.wifi_pass, sizeof(keep.wifi_pass));
    g_set.wifi_on = keep.wifi_on;
    settings_save();
}

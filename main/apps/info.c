// info.c - battery, storage, network and firmware, as a read-only round menu
#include "ui.h"
#include "board.h"
#include "media.h"
#include "ota.h"
#include "sd.h"
#include "settings.h"
#include "wifi_mgr.h"
#include <stdio.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"

static void v_battery(char *b, int n)
{
    int p = board_battery_percent();
    float v = board_battery_volts();
    if (p < 0) snprintf(b, n, "%s", T(S_NO_BATTERY));
    else snprintf(b, n, "%d%%  ·  %.2f V%s", p, v, board_charging() ? "  " IC_BOLT : "");
}
static void v_power(char *b, int n) { snprintf(b, n, "%s", T(board_usb_power() ? S_POWER_USB : S_POWER_BATTERY)); }
static void v_sd(char *b, int n)
{
    uint64_t t = 0, f = 0;
    if (!sd_space(&t, &f)) { snprintf(b, n, "%s", T(S_NO_SD)); return; }
    snprintf(b, n, T(S_SD_FREE), f / 1e9, t / 1e9);
}
static void v_media(char *b, int n) { snprintf(b, n, T(S_N_MEDIA), media_count()); }
static void v_version(char *b, int n) { snprintf(b, n, "%s", ota_current()); }
static void v_wifi(char *b, int n)
{
    if (wifi_mgr_state() == WIFI_CONNECTED) snprintf(b, n, "%s  ·  %d dBm", g_set.wifi_ssid, wifi_mgr_rssi());
    else snprintf(b, n, "%s", T(wifi_mgr_state() == WIFI_OFF ? S_OFF : S_NOT_CONNECTED));
}
static void v_ip(char *b, int n) { snprintf(b, n, "%s", wifi_mgr_ip()[0] ? wifi_mgr_ip() : "-"); }
static void v_mem(char *b, int n)
{
    snprintf(b, n, "%u KB  ·  PSRAM %u KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}
static void v_uptime(char *b, int n)
{
    int64_t s = esp_timer_get_time() / 1000000;
    snprintf(b, n, "%lldh %02lldm", s / 3600, (s / 60) % 60);
}

static const menu_item_t items[] = {
    {IC_BAT_FULL, S_INFO_BATTERY, NULL, v_battery},
    {IC_BOLT, S_INFO_POWER, NULL, v_power},
    {IC_SD, S_INFO_SD, NULL, v_sd},
    {IC_IMAGE, S_APP_GALLERY, NULL, v_media},
    {IC_WIFI, S_WIFI, NULL, v_wifi},
    {IC_GLOBE, S_INFO_IP, NULL, v_ip},
    {IC_GEM, S_INFO_VERSION, NULL, v_version},
    {IC_CHIP, S_INFO_MEMORY, NULL, v_mem},
    {IC_HOURGLASS, S_INFO_UPTIME, NULL, v_uptime},
};
static menu_t info_menu = {S_APP_INFO, items, sizeof(items) / sizeof(items[0]), 0};

MENU_SCREEN(scr_info, "info", &info_menu);

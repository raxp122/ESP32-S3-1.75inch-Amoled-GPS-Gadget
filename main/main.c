// main.c - AmuletOS start-up
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "board.h"
#include "display.h"
#include "media.h"
#include "ota.h"
#include "sd.h"
#include "settings.h"
#include "ui.h"
#include "wifi_mgr.h"

static const char *TAG = "main";

// the RTC keeps UTC: set the system clock from it (NTP corrects it once Wi-Fi is up)
static void time_from_rtc(void)
{
    struct tm utc;
    if (board_rtc_get(&utc) != RTC_OK) { ESP_LOGW(TAG, "RTC time not valid"); return; }
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t t = mktime(&utc);
    struct timeval tv = {.tv_sec = t};
    settimeofday(&tv, NULL);
}

void app_main(void)
{
    board_init();
    settings_load();
    time_from_rtc();
    board_gps_enable(g_set.gps_on);
    sd_mount();
    media_scan();

    display_init(g_set.rotation);
    display_lock();
    ui_init();            // also sets the time zone
    display_unlock();
    display_start_task();
    vTaskDelay(pdMS_TO_TICKS(120));   // light up after the first frame
    ui_apply_brightness();

    wifi_mgr_init();
    ota_mark_valid();     // we got here: the firmware works, no rollback
    ota_auto_start();
    ESP_LOGI(TAG, "AmuletOS %s ready", ota_current());
}

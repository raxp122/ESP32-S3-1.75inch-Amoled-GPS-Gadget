// ota.h - firmware updates from the GitHub releases of this repository.
// Check and download run in their own task; the UI reads the state.
#pragma once
#include <stdbool.h>

#define OTA_REPO "raxp122/ESP32-S3-1.75inch-Amoled-GPS-Gadget"

typedef enum {
    OTA_IDLE,
    OTA_CHECKING,
    OTA_UP_TO_DATE,
    OTA_AVAILABLE,     // ota_latest() is newer
    OTA_DOWNLOADING,   // ota_progress() 0-100
    OTA_DONE,          // installed: the board restarts by itself
    OTA_ERROR,         // ota_error()
} ota_state_t;

void        ota_mark_valid(void);    // the new firmware works: cancel the automatic rollback
void        ota_check(void);
void        ota_install(void);
void        ota_auto_start(void);    // one check as soon as internet is there (if enabled)
ota_state_t ota_state(void);
int         ota_progress(void);
const char *ota_current(void);
const char *ota_latest(void);
const char *ota_error(void);

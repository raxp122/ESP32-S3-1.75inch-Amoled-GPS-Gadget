// sd.c - microSD on SDMMC (1 bit) mounted at /sdcard
#include "sd.h"
#include "board.h"
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

static sdmmc_card_t *card;

bool sd_mount(void)
{
    if (card) return true;
    esp_vfs_fat_sdmmc_mount_config_t mc = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 32 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = PIN_SD_CLK;
    slot.cmd = PIN_SD_CMD;
    slot.d0 = PIN_SD_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    if (esp_vfs_fat_sdmmc_mount(SD_MOUNT, &host, &slot, &mc, &card) != ESP_OK) {
        card = NULL;
        ESP_LOGW("sd", "microSD not mounted (missing or not FAT32)");
        return false;
    }
    mkdir(SD_MOUNT "/amulet", 0775);
    mkdir(SD_MOUNT "/amulet/.thumb", 0775);
    return true;
}

bool sd_ok(void) { return card != NULL; }

bool sd_space(uint64_t *total, uint64_t *free_b)
{
    if (!card) return false;
    return esp_vfs_fat_info(SD_MOUNT, total, free_b) == ESP_OK;
}

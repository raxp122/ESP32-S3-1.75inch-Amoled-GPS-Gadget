// sd.h - microSD (SDMMC 1-bit) mounted at /sdcard
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SD_MOUNT "/sdcard"
bool sd_mount(void);
bool sd_ok(void);
bool sd_space(uint64_t *total, uint64_t *free_bytes);

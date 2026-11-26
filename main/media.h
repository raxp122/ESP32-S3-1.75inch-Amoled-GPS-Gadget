// media.h - the media library on the microSD (format in core.h) and JPEG decoding.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "core.h"

#define MEDIA_DIR   "/sdcard/amulet"
#define THUMB_DIR   "/sdcard/amulet/.thumb"
#define THUMB_SIZE  240
#define MEDIA_MAX   1000

typedef struct {
    char     file[MEDIA_NAME_MAX + 6];   // "name.jpg" / "name.amj"
    uint8_t  kind;                       // media_kind_t
    uint32_t size;
    int64_t  mtime;
} media_item_t;

int  media_scan(void);                   // rereads the folder (newest first), returns the count
int  media_count(void);
const media_item_t *media_get(int i);    // NULL if out of range
int  media_find(const char *file);       // -1 if not there
bool media_delete(int i);
void media_display_name(const media_item_t *m, char *out, int n);   // without extension
uint32_t media_generation(void);         // grows on every change (uploads, deletions)
void media_touch(void);                  // something changed on the card: bump the generation

// RGB565 little-endian buffers, 16-byte aligned, in PSRAM
uint16_t *media_frame_alloc(int w, int h);
// Decodes a JPEG into out (out_w x out_h): an image of another size is centred (cropped or
// with black around).
bool media_decode(const uint8_t *jpg, int len, uint16_t *out, int out_w, int out_h);
// Whole file into PSRAM (free with free()); max_len guards against huge files
uint8_t *media_read_file(const char *path, int *len, int max_len);
// Gallery preview of item i into out (THUMB_SIZE^2): the thumbnail, else a decoded frame
bool media_thumb(int i, uint16_t *out);

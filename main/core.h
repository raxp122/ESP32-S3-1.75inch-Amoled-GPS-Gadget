// core.h - pure logic without hardware or ESP-IDF, covered by the native tests in tests/.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- media files ----
 * On the microSD, folder /amulet:
 *   <name>.jpg          still image, 466x466 JPEG (already cropped to the circle by the uploader)
 *   <name>.amj          animation (GIF or video converted by the uploader), see below
 *   .thumb/<name>.jpg   240x240 preview for the gallery
 *
 * AMJ ("Amulet Motion JPEG"), little-endian:
 *   0   "AMJ1"
 *   4   u16 width, u16 height
 *   8   u32 frame count
 *   12  u32 reserved (0)
 *   16  frame index: count x { u32 offset, u32 size, u32 delay_ms }
 *   ... JPEG frames (offsets are from the start of the file)
 */
typedef enum { MEDIA_NONE = 0, MEDIA_IMAGE, MEDIA_ANIM } media_kind_t;

#define AMJ_HEADER     16
#define AMJ_ENTRY      12
#define AMJ_MAX_FRAMES 4000
#define MEDIA_NAME_MAX 40     // without extension

typedef struct { uint32_t offset, size, delay_ms; } amj_frame_t;
typedef struct { uint16_t width, height; uint32_t frames; } amj_info_t;

media_kind_t media_kind(const char *filename);            // from the extension (case-insensitive)
// header check: false if not AMJ, if the count is out of range or the index does not fit in file_size
bool amj_parse_header(const uint8_t *buf, size_t len, uint32_t file_size, amj_info_t *out);
// entry i of the index (buf points at the index, i.e. file offset AMJ_HEADER)
bool amj_entry(const uint8_t *index, uint32_t i, uint32_t file_size, amj_frame_t *out);

// Turns any name into a safe FAT file name base: [A-Za-z0-9_-], at most MEDIA_NAME_MAX chars.
// Returns false (and an empty name) if nothing usable is left.
bool media_safe_name(const char *in, char *out, size_t n);

/* ---- misc ---- */
// "0.1.7", "v0.1.7-3-gabc": > 0 if a is newer than b
int  version_compare(const char *a, const char *b);
// night window [from, to) in hours, wrapping over midnight (23 -> 7). from == to: never.
bool night_active(int hour, int from_h, int to_h);
// URL query parameter (decoded): false if missing
bool query_param(const char *query, const char *key, char *out, size_t n);
// next index of a shuffled slideshow (never the current one when count > 1)
int  shuffle_next(int cur, int count, uint32_t *seed);

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
// LiPo charge (0-100) estimated from the voltage at rest; 0 for 0 V (no battery)
int  battery_percent_from_volts(float v);
// The percentage to show: the PMU fuel gauge, unless it is missing (-1) or far from the voltage
// estimate (a new battery the gauge has not learned yet, e.g. 0 % at 4.09 V): then the estimate.
// *estimated (in/out, may be NULL): in, whether the last pick was the estimate (the gauge must
// then come within 15 points to take over again); out, whether this one is.
int  battery_percent_pick(int gauge, float volts, bool charging, bool *estimated);
// next index of a shuffled slideshow (never the current one when count > 1)
int  shuffle_next(int cur, int count, uint32_t *seed);

// what pendant mode plays: everything, still images only, animations (GIF / video) only
enum { MEDIA_FILTER_ALL = 0, MEDIA_FILTER_IMAGES, MEDIA_FILTER_ANIM, MEDIA_FILTERS };
bool media_filter_ok(int kind, int filter);
int  media_filter_count(const uint8_t *kinds, int n, int filter);
// the media to start with: `start` if it passes the filter, else the next one that does; -1 if none
int  media_pick_first(const uint8_t *kinds, int n, int start, int filter);
// the media after `cur` among those that pass the filter (in order, or shuffled: never cur
// itself when there is another one); -1 if none passes
int  media_pick_next(const uint8_t *kinds, int n, int cur, int filter, bool shuffle, uint32_t *seed);

/* ---- calendar ----
 * Gregorian dates, years 1900-2199. Weekday 0 = Monday ... 6 = Sunday. */
typedef struct { int16_t y; uint8_t m, d; } date_t;   // m 1-12, d 1-31

bool    date_leap(int y);
int     date_days_in_month(int y, int m);
int     date_weekday(date_t d);
int     date_yday(date_t d);                  // 1 = 1 January
int     date_iso_week(date_t d);              // 1-53
int32_t date_to_days(date_t d);               // days since 1970-01-01
date_t  date_from_days(int32_t days);
date_t  date_add_days(date_t d, int n);
date_t  date_add_months(date_t d, int n);     // the day is clamped (31 Jan + 1 = 28/29 Feb)
date_t  date_add_years(date_t d, int n);      // 29 Feb -> 28 Feb
date_t  date_clamp(date_t d);                 // into the supported range, valid day
date_t  date_easter(int y);
// Italian national non-working day (Sundays are not counted here)
bool    date_holiday_it(date_t d);
date_t  date_next_holiday_it(date_t d);       // the first one after d

/* ---- dice ----
 * A roll: count dice with `sides` faces, plus a modifier. Advantage / disadvantage roll every
 * die twice and keep the higher / lower one (for 1d20 it is the D&D rule). drop_lowest drops
 * that many of the lowest kept dice (ability scores: 4d6, drop the lowest). */
#define DICE_MAX 20
// DICE_DH*: Daggerheart duality roll, 2d12 Hope and Fear (count and sides are ignored), plus
// a d6 added (advantage) or taken away (disadvantage), plus the modifier
enum { DICE_NORMAL = 0, DICE_ADV, DICE_DIS, DICE_DH, DICE_DH_ADV, DICE_DH_DIS, DICE_MODES };
enum { DH_NONE = -1, DH_HOPE = 0, DH_FEAR, DH_CRIT };

typedef struct {
    uint8_t count;        // 1..DICE_MAX
    uint8_t sides;        // 2..100
    int8_t  mod;          // -99..99
    uint8_t mode;         // DICE_NORMAL / DICE_ADV / DICE_DIS
    uint8_t drop_lowest;  // < count
} dice_spec_t;

typedef struct {
    uint8_t n;                       // dice in val[] (count, or 2 x count with adv/dis)
    uint8_t val[DICE_MAX * 2];
    uint8_t kept[DICE_MAX * 2];      // 1 = counts in the total
    int     total;
    bool    crit, fumble;            // d20: a kept natural 20 / natural 1; Daggerheart: crit = doubles
    int8_t  dh;                      // Daggerheart: DH_HOPE / DH_FEAR / DH_CRIT, else DH_NONE
    uint8_t hope, fear, d6;          // Daggerheart dice (d6 = 0 without advantage/disadvantage)
} dice_result_t;

typedef uint32_t (*dice_rng_t)(void *ctx);   // 32 random bits

dice_spec_t dice_clamp(dice_spec_t s);
// uniform 1..sides without modulo bias
int  dice_one(int sides, dice_rng_t rng, void *ctx);
void dice_roll(const dice_spec_t *s, dice_rng_t rng, void *ctx, dice_result_t *r);
// "2d20+3", "d20 ADV" style is up to the UI: this writes "2d20+3" / "d100" / "4d6-1"
void dice_format(const dice_spec_t *s, char *buf, size_t n);
// the kept / dropped dice as text: "14 9" with dropped ones in parentheses: "14 (9)"
void dice_breakdown(const dice_result_t *r, char *buf, size_t n);
int  dice_ability_mod(int score);   // D&D: (score - 10) / 2, rounded down
bool dice_is_dh(const dice_spec_t *s);

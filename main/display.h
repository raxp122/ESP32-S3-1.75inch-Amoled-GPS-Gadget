// display.h - round AMOLED (CO5300) + LVGL 9. Rotation (0/90/180/270) is applied while
// sending pixels to the panel, so LVGL and the media player always work upright.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#define SCR_W 466
#define SCR_H 466
#define SCR_C (SCR_W / 2)   // centre of the circle

void display_init(int rotation);          // rotation: 0..3 = 0/90/180/270 degrees clockwise
void display_set_rotation(int rotation);
int  display_rotation(void);
void display_set_brightness(int percent); // 0 = dark: LVGL stops drawing
bool display_is_dark(void);
void display_lock(void);                  // LVGL is not thread safe: take this from other tasks
void display_unlock(void);
void display_start_task(void);

// Physical touch coordinates -> logical (upright) coordinates
void display_map_touch(int *x, int *y);

// Direct mode (media player): LVGL stops refreshing and full frames (SCR_W x SCR_H, RGB565
// little-endian like LVGL, so an lv_image can show the same buffer) go straight to the panel.
// display_direct_end() gives the screen back to LVGL and redraws it.
void display_direct_begin(void);
void display_direct_push(const uint16_t *frame);
void display_direct_end(void);
bool display_direct_on(void);

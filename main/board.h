// board.h - pins and on-board chips of the Waveshare ESP32-S3-Touch-AMOLED-1.75 (also -B / -G):
// round AMOLED 466x466 (CO5300, QSPI), touch CST9217, PMU AXP2101, IO expander TCA9554,
// RTC PCF85063, IMU QMI8658, microSD (SDMMC 1-bit). All I2C chips share one bus (15/14).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "driver/i2c_master.h"

#define PIN_I2C_SDA     15
#define PIN_I2C_SCL     14
#define PIN_LCD_CS      12
#define PIN_LCD_PCLK    38
#define PIN_LCD_D0      4
#define PIN_LCD_D1      5
#define PIN_LCD_D2      6
#define PIN_LCD_D3      7
#define PIN_LCD_RST     39
#define PIN_TP_RST      40
#define PIN_TP_INT      11
#define PIN_BTN_BOOT    0
#define PIN_SD_CLK      2
#define PIN_SD_CMD      1
#define PIN_SD_D0       3

#define LCD_W           466
#define LCD_H           466
#define LCD_X_GAP       6      // the controller has 6 extra columns on the left

#define ADDR_TOUCH      0x5A
#define ADDR_AXP2101    0x34
#define ADDR_TCA9554    0x20
#define ADDR_RTC        0x51
#define ADDR_IMU        0x6B

#define EXIO_PWR        4      // SYS_OUT: high while PWR is pressed
#define EXIO_GPS_RST    7      // -G only: GPS reset (high = running)

typedef struct { float x, y, z; } vec3_t;

void board_init(void);
i2c_master_dev_handle_t board_touch_dev(void);
bool board_touch_recover(void);        // resets bus and touch controller after I2C errors

void  board_power_off(void);           // AXP2101 cuts all rails (on USB the board may restart)
float board_battery_volts(void);       // 0 when no battery
int   board_battery_percent(void);     // fuel gauge of the AXP2101 (-1 if unknown)
bool  board_battery_present(void);
void  board_gauge_reset(void);          // the fuel gauge starts over from the voltage (new battery)
bool  board_charging(void);
bool  board_usb_power(void);           // VBUS present

typedef enum { RTC_OK, RTC_NO_CHIP, RTC_STOPPED, RTC_UNSET } rtc_status_t;
rtc_status_t board_rtc_get(struct tm *utc);
bool  board_rtc_write(const struct tm *utc);

bool  board_imu_ok(void);
bool  board_imu_accel(vec3_t *g);      // in g

bool  board_btn_boot(void);            // true = pressed
bool  board_btn_pwr(void);

void  board_gps_enable(bool on);       // holds the GPS of the -G version in reset when off

// Charger and protection for the battery in use (mAh). care: stop charging at 4.1 V (about
// 90 %) instead of 4.2 V, kinder to the cell. The PMU also switches the board off on its own
// below 3.2 V, under the firmware's own low-battery shutdown.
void  board_battery_setup(int capacity_mah, bool care);

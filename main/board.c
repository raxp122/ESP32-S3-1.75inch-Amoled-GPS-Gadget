// board.c - I2C bus and on-board chips. Register sequences follow the Waveshare BSP and the
// ones proven on the same board in raxp122/ESP32-Developement.
#include "board.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "board";

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t dev_tca, dev_rtc, dev_imu, dev_touch, dev_axp;
static bool imu_ok;

static esp_err_t reg_write(i2c_master_dev_handle_t d, uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(d, b, 2, 50);
}

static esp_err_t reg_read(i2c_master_dev_handle_t d, uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(d, &reg, 1, buf, n, 50);
}

static i2c_master_dev_handle_t add_dev(uint8_t addr)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 300000,
    };
    i2c_master_dev_handle_t h = NULL;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &cfg, &h));
    return h;
}

// A chip left half-way through a transfer by a software reset can hold SDA low forever:
// up to 9 clock pulses let it finish the byte, then a STOP frees the bus.
static void bus_unstick(int sda, int scl)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    gpio_set_level(sda, 1);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(10);
    if (gpio_get_level(sda)) return;
    for (int n = 0; n < 9 && !gpio_get_level(sda); n++) {
        gpio_set_level(scl, 0);
        esp_rom_delay_us(10);
        gpio_set_level(scl, 1);
        esp_rom_delay_us(10);
    }
    gpio_set_level(scl, 0);
    esp_rom_delay_us(10);
    gpio_set_level(sda, 0);
    esp_rom_delay_us(10);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(10);
    gpio_set_level(sda, 1);
    esp_rom_delay_us(10);
    ESP_LOGW(TAG, "I2C bus was stuck, released");
}

/* ---------------- AXP2101 ---------------- */

static uint8_t axp_get(uint8_t reg)
{
    uint8_t v = 0;
    reg_read(dev_axp, reg, &v, 1);
    return v;
}

static void axp_init(void)
{
    reg_write(dev_axp, 0x30, axp_get(0x30) | 0x01);   // battery voltage ADC on
    reg_write(dev_axp, 0x18, axp_get(0x18) | 0x08);   // fuel gauge on
    // PWR held: the PMU's own forced power-off only after 10 s. The firmware shuts down
    // cleanly at 5 s (see input.c), the 10 s one is the last resort if the firmware hangs.
    reg_write(dev_axp, 0x27, axp_get(0x27) | 0x0C);
}

/* ---------------- TCA9554 ---------------- */

static void tca_init(void)
{
    // everything input except the GPS reset (-G version)
    uint8_t out = 0xFF, cfg = 0xFF;
    reg_read(dev_tca, 0x01, &out, 1);
    reg_read(dev_tca, 0x03, &cfg, 1);
    cfg &= ~(1 << EXIO_GPS_RST);
    if (reg_write(dev_tca, 0x01, out) != ESP_OK || reg_write(dev_tca, 0x03, cfg) != ESP_OK)
        ESP_LOGW(TAG, "TCA9554 not responding");
}

void board_gps_enable(bool on)
{
    uint8_t out = 0xFF;
    if (reg_read(dev_tca, 0x01, &out, 1) != ESP_OK) return;
    out = on ? out | (1 << EXIO_GPS_RST) : out & ~(1 << EXIO_GPS_RST);
    reg_write(dev_tca, 0x01, out);
}

/* ---------------- IMU ---------------- */

static void imu_init(void)
{
    uint8_t id = 0;
    if (reg_read(dev_imu, 0x00, &id, 1) != ESP_OK || id != 0x05) {
        ESP_LOGW(TAG, "QMI8658 not found (id=0x%02x)", id);
        return;
    }
    reg_write(dev_imu, 0x02, 0x40);   // CTRL1: register auto-increment
    reg_write(dev_imu, 0x03, 0x15);   // CTRL2: +-4 g
    reg_write(dev_imu, 0x08, 0x01);   // CTRL7: accelerometer on
    imu_ok = true;
}

void board_init(void)
{
    bus_unstick(PIN_I2C_SDA, PIN_I2C_SCL);
    i2c_master_bus_config_t bc = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = PIN_I2C_SCL,
        .sda_io_num = PIN_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &bus));
    dev_axp = add_dev(ADDR_AXP2101);
    dev_tca = add_dev(ADDR_TCA9554);
    dev_rtc = add_dev(ADDR_RTC);
    dev_imu = add_dev(ADDR_IMU);
    dev_touch = add_dev(ADDR_TOUCH);
    axp_init();
    tca_init();

    gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_TP_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    imu_init();
    gpio_config_t io = {.pin_bit_mask = 1ULL << PIN_BTN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&io);
}

i2c_master_dev_handle_t board_touch_dev(void) { return dev_touch; }

bool board_touch_recover(void)
{
    i2c_master_bus_reset(bus);
    gpio_set_level(PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGW(TAG, "touch: bus reset and controller restarted");
    return true;
}

void board_power_off(void) { reg_write(dev_axp, 0x10, axp_get(0x10) | 0x01); }

bool board_charging(void) { return ((axp_get(0x01) >> 5) & 0x03) == 0x01; }
bool board_usb_power(void) { return (axp_get(0x00) >> 5) & 1; }

float board_battery_volts(void)
{
    if (!(axp_get(0x00) & 0x08)) return 0;   // no battery
    uint8_t r[2] = {0};
    if (reg_read(dev_axp, 0x34, r, 2) != ESP_OK) return 0;
    return (((r[0] & 0x3F) << 8) | r[1]) / 1000.0f;
}

int board_battery_percent(void)
{
    if (!(axp_get(0x00) & 0x08)) return -1;
    uint8_t p = axp_get(0xA4);
    return p <= 100 ? p : -1;
}

/* ---------------- RTC PCF85063 (keeps UTC) ---------------- */

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

rtc_status_t board_rtc_get(struct tm *t)
{
    uint8_t r[7];
    if (reg_read(dev_rtc, 0x04, r, 7) != ESP_OK) return RTC_NO_CHIP;
    if (r[0] & 0x80) return RTC_STOPPED;   // oscillator stopped: time not reliable
    memset(t, 0, sizeof(*t));
    t->tm_sec = bcd2bin(r[0] & 0x7F);
    t->tm_min = bcd2bin(r[1] & 0x7F);
    t->tm_hour = bcd2bin(r[2] & 0x3F);
    t->tm_mday = bcd2bin(r[3] & 0x3F);
    t->tm_mon = bcd2bin(r[5] & 0x1F) - 1;
    t->tm_year = bcd2bin(r[6]) + 100;
    return t->tm_year >= 125 ? RTC_OK : RTC_UNSET;
}

bool board_rtc_write(const struct tm *t)
{
    uint8_t b[8] = {
        0x04,
        bin2bcd(t->tm_sec), bin2bcd(t->tm_min), bin2bcd(t->tm_hour),
        bin2bcd(t->tm_mday), (uint8_t)t->tm_wday, bin2bcd(t->tm_mon + 1),
        bin2bcd((t->tm_year - 100) % 100),
    };
    for (int tries = 0; tries < 3; tries++) {
        if (i2c_master_transmit(dev_rtc, b, sizeof(b), 50) != ESP_OK) continue;
        struct tm r;
        if (board_rtc_get(&r) == RTC_OK && r.tm_mday == t->tm_mday && r.tm_year == t->tm_year) return true;
    }
    return false;
}

/* ---------------- IMU / buttons ---------------- */

bool board_imu_ok(void) { return imu_ok; }

bool board_imu_accel(vec3_t *g)
{
    if (!imu_ok) return false;
    uint8_t r[6];
    if (reg_read(dev_imu, 0x35, r, 6) != ESP_OK) return false;
    const float k = 1.0f / 8192.0f;   // +-4 g
    g->x = (int16_t)(r[0] | r[1] << 8) * k;
    g->y = (int16_t)(r[2] | r[3] << 8) * k;
    g->z = (int16_t)(r[4] | r[5] << 8) * k;
    return true;
}

bool board_btn_boot(void) { return gpio_get_level(PIN_BTN_BOOT) == 0; }

bool board_btn_pwr(void)
{
    uint8_t in = 0;
    if (reg_read(dev_tca, 0x00, &in, 1) != ESP_OK) return false;
    return (in >> EXIO_PWR) & 1;
}

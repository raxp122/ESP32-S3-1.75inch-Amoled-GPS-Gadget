#pragma once
// simulator: no power management (power.c sees an error and does without)
typedef int esp_err_t;
typedef void *esp_pm_lock_handle_t;
typedef enum { ESP_PM_CPU_FREQ_MAX, ESP_PM_APB_FREQ_MAX, ESP_PM_NO_LIGHT_SLEEP } esp_pm_lock_type_t;
typedef struct { int max_freq_mhz, min_freq_mhz; int light_sleep_enable; } esp_pm_config_t;
#define ESP_OK 0
static inline esp_err_t esp_pm_configure(const void *c) { return -1; }
static inline esp_err_t esp_pm_lock_create(esp_pm_lock_type_t t, int a, const char *n, esp_pm_lock_handle_t *h) { return -1; }
static inline esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t h) { return 0; }
static inline esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t h) { return 0; }

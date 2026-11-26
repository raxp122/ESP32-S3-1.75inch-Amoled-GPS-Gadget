// wifi_mgr.h - Wi-Fi: home network (STA), scan, own hotspot with captive DNS, NTP time
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { WIFI_OFF, WIFI_NO_NETWORK, WIFI_CONNECTING, WIFI_CONNECTED } wifi_state_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t auth;      // 0 = open
} wifi_ap_t;

void wifi_mgr_init(void);
void wifi_mgr_apply(void);                       // after changing g_set.wifi_on
bool wifi_mgr_connect(const char *ssid, const char *pass);   // saves and connects
void wifi_mgr_forget(void);
void wifi_mgr_reconnect(void);
wifi_state_t wifi_mgr_state(void);
bool wifi_mgr_conn_failed(void);                 // wrong password / network not found
const char *wifi_mgr_ip(void);
int  wifi_mgr_rssi(void);

void wifi_mgr_scan_start(void);                  // powers the radio if needed
bool wifi_mgr_scan_busy(void);
uint32_t wifi_mgr_scan_gen(void);                // grows after every finished scan
int  wifi_mgr_scan_results(wifi_ap_t *out, int max);

// Hotspot "AmuletOS-XXXX" (open) with a DNS that answers 192.168.4.1 to every name, so
// phones open the page by themselves. The home connection stays up meanwhile.
void wifi_mgr_ap_start(void);
void wifi_mgr_ap_stop(void);
bool wifi_mgr_ap_on(void);
const char *wifi_mgr_ap_ssid(void);
int  wifi_mgr_ap_clients(void);
#define WIFI_AP_IP "192.168.4.1"

bool wifi_mgr_time_synced(void);

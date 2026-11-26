// web.h - the device's web pages: media upload (PIN protected) and Wi-Fi setup.
// Only running while the matching screen is open.
#pragma once
#include <stdbool.h>

typedef enum { WEB_OFF = 0, WEB_UPLOAD, WEB_WIFI } web_mode_t;

void web_start(web_mode_t mode);
void web_stop(void);
web_mode_t web_mode(void);
const char *web_pin(void);              // 4 digits, new at every start

// upload progress (for the screen)
int  web_uploads(void);                 // finished uploads since the start
int  web_progress(void);                // 0-100 of the file being received, -1 = none
const char *web_current(void);          // its name
bool web_client_seen(void);             // a browser has opened the page

// Wi-Fi setup: credentials sent by the phone (once)
bool web_take_wifi(char *ssid, int ssid_n, char *pass, int pass_n);

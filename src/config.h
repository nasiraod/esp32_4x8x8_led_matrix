#pragma once
#include <Arduino.h>
#include <MD_MAX72xx.h>

// ---------------------------------------------------------------------------
// Panel hardware
//
// WIRING (VSPI, via level shifter): DIN=GPIO23 (MOSI), CLK=GPIO18 (SCK),
// CS=GPIO5, VCC=5V. MISO unused - the MAX7219 never talks back.
//
// ORIENTATION: this panel's DIN enters its LEFTMOST module, while MD_MAX72XX
// assumes column 0 is on the right. DR0CR0RR0_HW therefore renders a full-width
// mirror, and PA_FLIP_LR cancels it. PA_FLIP_LR must stay on in BOTH static and
// scrolling modes - it is the only control over the reverseBuf() path, which
// fires on (FLIP_LR XOR SCROLL_RIGHT). That is also why scrolling uses
// PA_SCROLL_RIGHT and static text uses PA_PRINT.
// ---------------------------------------------------------------------------
#define HARDWARE_TYPE   MD_MAX72XX::DR0CR0RR0_HW
#define MAX_DEVICES     4
#define CS_PIN          5
#define PANEL_COLUMNS   (8 * MAX_DEVICES)

// ---------------------------------------------------------------------------
// Networking
// ---------------------------------------------------------------------------
#define TCP_PORT                23
#define TCP_MAX_CLIENTS         4

#define AP_PASSWORD             "ledpanel123"   // >= 8 chars, WPA2 minimum
#define WIFI_CONNECT_TIMEOUT_MS 30000UL         // spec: 30s to associate
#define WIFI_DROP_GRACE_MS      30000UL         // let auto-reconnect try first
#define PORTAL_RESCAN_MS        20000UL         // portal keeps hunting saved SSID

#define NVS_NAMESPACE           "ledpanel"


// Default POSIX timezone: Eastern (Toronto). Change at runtime with `tz <spec>`.
#define DEFAULT_TZ              "EST5EDT,M3.2.0,M11.1.0"
// Seconds fit now: the compact 4x7 digit font renders HH:MM:SS in 29 columns.
#define DEFAULT_CLOCK_FORMAT    "%H:%M:%S"

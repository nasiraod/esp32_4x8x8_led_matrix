#pragma once
#include <Arduino.h>

enum NetState : uint8_t {
  NET_CONNECTING,   // STA associating, 30s budget
  NET_CONNECTED,    // STA up
  NET_PORTAL        // AP_STA: config page served, saved SSID still hunted for
};

namespace Net {

void begin();
void tick();

NetState    state();
const char *stateName();
bool        isConnected();

String ip();            // STA address when connected, else AP address
String ssid();          // saved SSID (may be empty)
String apSsid();

void setCreds(const String &ssid, const String &pass);   // saves + tries at once
void clearCreds();
void forcePortal();

void   setTz(const String &tz);   // POSIX TZ spec, persisted to NVS
String tz();

}  // namespace Net

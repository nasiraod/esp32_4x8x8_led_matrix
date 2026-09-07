#include "otamgr.h"
#include "config.h"
#include "display_mgr.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include "esp_task_wdt.h"

namespace Ota {

static Preferences prefs;
static bool     g_begun    = false;   // ArduinoOTA started (needs WiFi)
static bool     g_active   = false;   // an update is being written
static bool     g_trusted  = false;   // this image has survived OTA_TRUST_MS
static uint32_t g_bootAt   = 0;
static int      g_lastPct  = -1;

// Display state to put back if an update fails (a success reboots anyway).
static TextFont   g_saveFont;
static ScrollPref g_saveScroll;
static DispMode   g_saveMode;
static String     g_saveMsg;

// ---------------------------------------------------------------------------

static void showPct(unsigned pct) {
  if ((int)pct == g_lastPct) return;            // only redraw on a real change
  g_lastPct = (int)pct;

  char buf[16];
  snprintf(buf, sizeof(buf), "OTA %u%%", pct);
  Display::setMessage(buf);
  Display::tick();                              // paint it now, not next loop
  esp_task_wdt_reset();                         // flash writes block for a while
}

// A freshly written image is not trusted until it has run for OTA_TRUST_MS.
// The marker is set before rebooting into it.
static void armPending() { prefs.putUChar("pending", 1); }

// Boot-time rollback. If the marker is still set, the previous boot never
// reached the trust point.
//
// Only roll back when the reset reason says something went wrong - a person
// power-cycling the panel 10s after a good update should not trigger it.
static void rollbackIfNeeded() {
  if (!prefs.getUChar("pending", 0)) return;

  const esp_reset_reason_t r = esp_reset_reason();
  const bool crashed = (r == ESP_RST_PANIC) || (r == ESP_RST_INT_WDT) ||
                       (r == ESP_RST_TASK_WDT) || (r == ESP_RST_WDT) ||
                       (r == ESP_RST_BROWNOUT);

  if (!crashed) {
    Serial.printf("[ota] pending image, reset reason=%d looks deliberate - keeping it\n", (int)r);
    return;                                     // stay pending, keep counting
  }

  const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
  if (other && esp_ota_set_boot_partition(other) == ESP_OK) {
    prefs.putUChar("pending", 0);
    Serial.printf("[ota] new image crashed (reason=%d) - rolling back to %s\n",
                  (int)r, other->label);
    Serial.flush();
    delay(200);
    ESP.restart();
  } else {
    prefs.putUChar("pending", 0);               // nothing valid to go back to
    Serial.println(F("[ota] crash after update, but no valid previous image"));
  }
}

// ---------------------------------------------------------------------------

void uploadBegin() {
  g_active  = true;
  g_lastPct = -1;
  Serial.println(F("[ota] update starting"));

  g_saveFont   = Display::textFont();
  g_saveScroll = Display::scroll();
  g_saveMode   = Display::mode();
  g_saveMsg    = Display::message();

  Display::setScreen(true);                     // so progress is visible
  Display::setMode(MODE_TEXT);

  // "OTA 45%" is 37 columns in the stock font, so it would auto-scroll - and
  // because the string changes every percent, each rebuild reset the scroll to
  // off-screen and it never appeared at all. The narrow font fits the whole
  // label ("OTA 100%" is 30 of 32 columns), and scrolling is forced off so any
  // future label truncates rather than vanishing.
  Display::setTextFont(FONT_NARROW);
  Display::setScroll(SCROLL_OFF);

  showPct(0);
}

void uploadProgress(size_t done, size_t total) {
  if (total) showPct((unsigned)((done * 100) / total));
  else       esp_task_wdt_reset();
}

void uploadEnd(bool ok) {
  g_active = false;
  Display::setMessage(ok ? "OTA OK" : "OTA FAIL");
  Display::tick();
  Serial.printf("[ota] update %s\n", ok ? "succeeded" : "FAILED");
  if (ok) armPending();                         // trust it only once it runs
}

bool inProgress() { return g_active; }

// ---------------------------------------------------------------------------

void begin() {
  prefs.begin("ota", false);
  g_bootAt = millis();

  const esp_partition_t *run = esp_ota_get_running_partition();
  Serial.printf("[ota] running from %s\n", run ? run->label : "?");

  rollbackIfNeeded();
}

static void startArduinoOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() { uploadBegin(); });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    uploadProgress(done, total);
  });
  ArduinoOTA.onEnd([]() { uploadEnd(true); });
  ArduinoOTA.onError([](ota_error_t) { uploadEnd(false); });

  ArduinoOTA.begin();
  Serial.printf("[ota] ArduinoOTA ready on %s.local:3232\n", OTA_HOSTNAME);
}

void tick() {
  if (!g_begun && WiFi.status() == WL_CONNECTED) {
    startArduinoOTA();
    g_begun = true;
  }
  if (g_begun) ArduinoOTA.handle();

  // Survived long enough: clear the marker and cancel any bootloader rollback.
  if (!g_trusted && (millis() - g_bootAt) > OTA_TRUST_MS) {
    g_trusted = true;
    if (prefs.getUChar("pending", 0)) {
      prefs.putUChar("pending", 0);
      Serial.println(F("[ota] image survived - marked good"));
    }
    esp_ota_mark_app_valid_cancel_rollback();
  }
}

}  // namespace Ota

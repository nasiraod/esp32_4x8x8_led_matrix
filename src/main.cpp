#include <Arduino.h>
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "config.h"
#include "display_mgr.h"
#include "netmgr.h"
#include "tcpsrv.h"
#include "commands.h"
#include "otamgr.h"
#include "sleepsched.h"
#include "buildinfo.h"

// ---------------------------------------------------------------------------
// 4x MAX7219 panel on an ESP32, controlled over TCP and USB serial.
//
//   Modes    : text / stopwatch / clock
//   Control  : TCP port 23 and USB serial (115200) - one shared parser, so a
//              command behaves identically on either. Send `help` for the list.
//   Wi-Fi    : starts an AP config portal when it has no saved network, or
//              falls back to one after 30s failing to associate or 30s of a
//              dropped link, while still hunting for the saved SSID.
//
// BLE was removed deliberately. On this board the Bluetooth controller hangs
// the system at startup and shifts the UART baud by exactly 26/40 - the ESP32
// crystal ratio - with or without WiFi running. WiFi alone is rock solid, so
// TCP carries the remote-control role instead.
// ---------------------------------------------------------------------------

static String serialBuf;

// ---------------------------------------------------------------------------
// Health instrumentation. Kept in place because it is what finally made the
// failures legible: a heartbeat that stops tells us loop() died, and a monitor
// task pinned to the OTHER core reports which subsystem it died in - or, by
// staying silent itself, proves the fault was below FreeRTOS.
// ---------------------------------------------------------------------------
#define STAGE_MAGIC 0xB0FFA5A5

RTC_NOINIT_ATTR static uint32_t g_stageMagic;
RTC_NOINIT_ATTR static uint32_t g_lastStage;
RTC_NOINIT_ATTR static uint32_t g_lastUptime;

static volatile uint32_t g_loopCount = 0;
static volatile uint32_t g_stageNow  = 0;

static const char *stageName(uint32_t s) {
  switch (s) {
    case 1:  return "Net::tick";
    case 2:  return "Tcp::tick";
    case 3:  return "Display::tick";
    case 4:  return "Ota::tick";
    case 5:  return "Sleep::tick";
    case 6:  return "serial-read";
    default: return "(none)";
  }
}

static inline void stage(uint32_t s) {
  g_stageNow   = s;
  g_lastStage  = s;
  g_lastUptime = millis();
}

static void monitorTask(void *) {
  uint32_t lastSeen = 0, stuckMs = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (g_loopCount != lastSeen) {
      lastSeen = g_loopCount;
      if (stuckMs) {
        Serial.printf("[mon] loop RECOVERED after %lums\n", (unsigned long)stuckMs);
        stuckMs = 0;
      }
      continue;
    }
    stuckMs += 500;
    if (stuckMs >= 2000 && (stuckMs % 2000) == 0) {
      Serial.printf("[mon] loop STUCK %lums in stage=%s heap=%u\n",
                    (unsigned long)stuckMs, stageName(g_stageNow),
                    (unsigned)ESP.getFreeHeap());
      Serial.flush();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("LEDPanel starting (WiFi + serial, no BLE)"));
  Serial.print(F("[build] ")); Serial.println(buildId());

  Serial.printf("[clk] boot xtal=%uMHz cpu=%uMHz apb=%u\n",
                (unsigned)getXtalFrequencyMhz(), (unsigned)getCpuFrequencyMhz(),
                (unsigned)getApbFrequency());

  Serial.printf("[rst] reason=%d", (int)esp_reset_reason());
  if (g_stageMagic == STAGE_MAGIC) {
    Serial.printf(" last_stage=%s at up=%lums\n", stageName(g_lastStage),
                  (unsigned long)g_lastUptime);
  } else {
    Serial.println(" (no prior stage - cold boot)");
  }
  g_stageMagic = STAGE_MAGIC;
  g_lastStage  = 0;

  // The Arduino core already owns the task watchdog, so init() is expected to
  // fail and reconfigure() is the call that matters. Log both - an earlier
  // version silently armed nothing while I believed otherwise.
  esp_task_wdt_config_t wdt = {};
  wdt.timeout_ms     = 10000;
  wdt.idle_core_mask = 0;
  wdt.trigger_panic  = true;
  esp_err_t eInit = esp_task_wdt_init(&wdt);
  esp_err_t eCfg  = (eInit == ESP_ERR_INVALID_STATE) ? esp_task_wdt_reconfigure(&wdt) : eInit;
  esp_err_t eAdd  = esp_task_wdt_add(NULL);
  Serial.printf("[wdt] init=%d reconfigure=%d add=%d (0=OK)\n",
                (int)eInit, (int)eCfg, (int)eAdd);

  xTaskCreatePinnedToCore(monitorTask, "mon", 3072, nullptr, 5, nullptr,
                          xPortGetCoreID() == 0 ? 1 : 0);

  Display::begin();
  Display::setStatusMessage("BOOT");

  Net::begin();     // decides AP portal vs STA from saved credentials
  Tcp::begin();
  Ota::begin();     // rollback check; ArduinoOTA starts once WiFi is up
  Sleep::begin();   // load the saved off/dim schedule

  Serial.printf("TCP port %d. Type 'help'.\n", TCP_PORT);
}

void loop() {
  // Loop timing, to decide empirically whether moving the display into its own
  // task is worth the concurrency risk. What matters is not the average but the
  // WORST iteration: the display steps every 40ms, so any single pass longer
  // than that shows up as visible scroll stutter.
  static uint32_t nextBeat = 0, loopsThisWindow = 0, worstUs = 0;
  const uint32_t  iterStart = micros();

  if ((int32_t)(millis() - nextBeat) >= 0) {
    nextBeat = millis() + 2000;
    Serial.printf("[hb] up=%lus heap=%u apb=%u wifi=%s loops/s=%lu worst=%luus\n",
                  (unsigned long)(millis() / 1000),
                  (unsigned)ESP.getFreeHeap(), (unsigned)getApbFrequency(),
                  Net::stateName(),
                  (unsigned long)(loopsThisWindow / 2),
                  (unsigned long)worstUs);
    loopsThisWindow = 0;
    worstUs         = 0;
  }
  loopsThisWindow++;

  // Read-modify-write rather than ++: C++20 deprecates increment on a
  // volatile-qualified object. Aligned 32-bit accesses are atomic on Xtensa,
  // and monitorTask only checks whether this changed, so a torn value would
  // be harmless anyway.
  g_loopCount = g_loopCount + 1;   // watched by monitorTask on the other core
  esp_task_wdt_reset();

  stage(1); Net::tick();
  stage(2); Tcp::tick();
  stage(3); Display::tick();
  stage(4); Ota::tick();
  stage(5); Sleep::tick();
  stage(6);

  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuf.length()) {
        const String reply = Cmd::handle(serialBuf);
        if (reply.length()) Serial.println(reply);
        serialBuf = "";
      }
    } else if (serialBuf.length() < 512) {
      serialBuf += c;
    }
  }

  const uint32_t iterUs = micros() - iterStart;
  if (iterUs > worstUs) worstUs = iterUs;
}

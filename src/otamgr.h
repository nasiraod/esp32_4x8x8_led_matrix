#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Over-the-air firmware updates.
//
// Two routes, both landing here:
//   ArduinoOTA push   pio run -t ota          (espota, port 3232)
//   HTTP upload       POST /update            (browser form or curl)
//
// Both show progress on the panel, feed the task watchdog while flash writes
// block, and arm the rollback marker before rebooting.
// ---------------------------------------------------------------------------

namespace Ota {

void begin();      // rollback check + arm; ArduinoOTA starts once WiFi is up
void tick();       // service ArduinoOTA, and trust the image once it survives

bool inProgress(); // true while an update is being written

// Shared hooks, also used by the HTTP upload handler in netmgr.cpp.
void uploadBegin();
void uploadProgress(size_t done, size_t total);
void uploadEnd(bool ok);

}  // namespace Ota

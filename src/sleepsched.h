#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Sleep schedule - turn the panel off (or dim it) between two times of day.
//
// Completes the `screen off` feature: rather than remembering to darken the
// panel at bedtime, it does it itself. Needs NTP time; does nothing until the
// clock is valid.
//
// EDGE-TRIGGERED, deliberately. The schedule acts only when the window is
// entered or left, so a manual `screen on` or `bright 9` in the middle of the
// night sticks instead of being fought every minute.
// ---------------------------------------------------------------------------

namespace Sleep {

void begin();     // load the saved schedule from NVS
void tick();      // evaluate on minute boundaries

bool   enabled();
String describe();          // "23:00-07:00 dim=2", "23:00-07:00 off", "disabled"

// startMin/endMin are minutes past midnight. dim < 0 means blank the screen;
// dim 0-15 means keep it lit at that brightness instead.
bool set(int startMin, int endMin, int dim);
void disable();

// "HH:MM" -> minutes past midnight, or -1 if malformed.
int parseHHMM(const String &s);

}  // namespace Sleep

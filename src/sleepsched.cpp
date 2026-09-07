#include "sleepsched.h"
#include "config.h"
#include "display_mgr.h"
#include <Preferences.h>
#include <time.h>

namespace Sleep {

static Preferences prefs;
static bool     g_on     = false;   // schedule active
static int      g_start  = 0;       // minutes past midnight
static int      g_end    = 0;
static int      g_dim    = -1;      // <0 = blank the screen, else brightness
static int8_t   g_inWin  = -1;      // last known state; -1 = not yet evaluated
static uint8_t  g_savedBright = 5;  // brightness to restore when waking

// What we ACTUALLY did on entering, so leaving can undo exactly that. Undoing
// based on the current config instead is wrong when the schedule is changed
// mid-window: switching off-mode to dim-mode then disabling used to restore
// brightness without ever turning the screen back on, leaving it dark.
#define APPLIED_NOTHING (-2)
static int8_t   g_applied = APPLIED_NOTHING;
static uint32_t g_nextEval   = 0;

// ---------------------------------------------------------------------------

int parseHHMM(const String &s) {
  const int c = s.indexOf(':');
  if (c < 1 || c > 2) return -1;
  const int h = s.substring(0, c).toInt();
  const int m = s.substring(c + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  if (s.length() < (size_t)c + 2) return -1;
  return h * 60 + m;
}

static String fmt(int mins) {
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", mins / 60, mins % 60);
  return String(b);
}

// Handles windows that wrap midnight, which is the normal case for sleeping.
static bool insideWindow(int nowMin) {
  if (g_start == g_end) return false;             // zero-length: never
  if (g_start < g_end)  return nowMin >= g_start && nowMin < g_end;
  return nowMin >= g_start || nowMin < g_end;     // wraps midnight
}

static void enterSleep() {
  if (g_applied != APPLIED_NOTHING) return;       // already applied
  if (g_dim >= 0) {
    g_savedBright = Display::intensity();
    Display::setIntensity((uint8_t)g_dim);
    Serial.print(F("[sleep] entering window - dimming to "));
    Serial.println(g_dim);
  } else {
    Display::setScreen(false);
    Serial.println(F("[sleep] entering window - screen off"));
  }
  g_applied = (int8_t)g_dim;                      // remember what we actually did
}

static void leaveSleep() {
  if (g_applied == APPLIED_NOTHING) return;       // nothing to undo
  if (g_applied >= 0) {
    Display::setIntensity(g_savedBright);
    Serial.print(F("[sleep] leaving window - brightness back to "));
    Serial.println(g_savedBright);
  } else {
    Display::setScreen(true);
    Serial.println(F("[sleep] leaving window - screen on"));
  }
  g_applied = APPLIED_NOTHING;
}

// ---------------------------------------------------------------------------

void begin() {
  prefs.begin("sleep", false);
  g_on    = prefs.getUChar("on", 0) != 0;
  g_start = prefs.getUShort("start", 0);
  g_end   = prefs.getUShort("end", 0);
  g_dim   = (int)prefs.getChar("dim", -1);
  if (g_on) Serial.printf("[sleep] schedule %s\n", describe().c_str());
}

void tick() {
  if (!g_on) return;

  const uint32_t now = millis();
  if ((int32_t)(now - g_nextEval) < 0) return;
  g_nextEval = now + 10000;                       // 10s is plenty for minutes

  const time_t t = time(nullptr);
  if (t < 100000) return;                         // NTP has not landed yet

  struct tm tmv;
  localtime_r(&t, &tmv);
  const bool in = insideWindow(tmv.tm_hour * 60 + tmv.tm_min);

  if (g_inWin < 0) {                              // first evaluation this boot
    g_inWin = in ? 1 : 0;
    if (in) enterSleep();                         // booted inside the window
    return;
  }
  if (in && g_inWin == 0) { g_inWin = 1; enterSleep(); }
  else if (!in && g_inWin == 1) { g_inWin = 0; leaveSleep(); }
}

bool enabled() { return g_on; }

String describe() {
  if (!g_on) return String("disabled");
  String s = fmt(g_start) + "-" + fmt(g_end);
  s += (g_inWin == 1) ? " [asleep]" : (g_inWin == 0 ? " [awake]" : " [pending]");
  s += (g_dim >= 0) ? (" dim=" + String(g_dim)) : String(" off");
  return s;
}

bool set(int startMin, int endMin, int dim) {
  if (startMin < 0 || endMin < 0 || startMin == endMin) return false;
  if (dim > 15) dim = 15;

  leaveSleep();                                   // undo the old settings first

  g_on    = true;
  g_start = startMin;
  g_end   = endMin;
  g_dim   = dim;
  g_inWin = -1;                                   // re-evaluate immediately
  g_nextEval = millis();

  prefs.putUChar("on", 1);
  prefs.putUShort("start", (uint16_t)startMin);
  prefs.putUShort("end", (uint16_t)endMin);
  prefs.putChar("dim", (int8_t)dim);
  return true;
}

void disable() {
  // If we are currently asleep, wake up rather than leaving it dark.
  if (g_on && g_inWin == 1) leaveSleep();
  g_on    = false;
  g_inWin = -1;
  prefs.putUChar("on", 0);
}

}  // namespace Sleep

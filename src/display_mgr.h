#pragma once
#include <Arduino.h>

enum DispMode : uint8_t { MODE_TEXT, MODE_STOPWATCH, MODE_CLOCK, MODE_TIMER };

// AUTO = scroll only when the message is wider than the panel.
// ON/OFF force it, and apply to MODE_TEXT only - stopwatch and clock always
// use AUTO, because their content changes constantly.
enum ScrollPref : uint8_t { SCROLL_AUTO, SCROLL_ON, SCROLL_OFF };

enum Justify : uint8_t { JUST_LEFT, JUST_CENTER, JUST_RIGHT };

// Text font.
//   normal = the library 5x7 font: full ASCII, lowercase, descenders
//   narrow = hand-drawn 3x8: caps full height, lowercase on a 5-row x-height,
//            plus digits. ~34% narrower than normal.
// A message containing anything outside the narrow set falls back to normal.
//
// There was a third option, "big" (5x8 digits), but it only differed from
// normal for all-digit messages and was indistinguishable otherwise, so it was
// removed. Tall digits still appear automatically in clock and stopwatch modes.
enum TextFont : uint8_t { FONT_NORMAL, FONT_NARROW };

// Digit font for the clock, independent of the layout.
//   stock = the library 5x7 font with its double-dot colon (rows 0-6)
//   big   = the 8-row digits, ~14% taller
// These are mutually exclusive with the hmbar seconds bar, which needs row 7:
// choosing "big" on hmbar gives taller digits and no bar.
enum ClockFont : uint8_t { CFONT_STOCK, CFONT_BIG };

// Clock layouts. 32 columns cannot hold six readable digits (5.3 columns each
// including separators), so each style trades something different away.
enum ClockStyle : uint8_t {
  CLK_HM_BAR,     // HH:MM full font + bottom row fills over the minute
  CLK_HM_BLINK,   // HH:MM full font, colon blinks each second
  CLK_MS,         // MM:SS full font - seconds as digits, hours dropped
  CLK_HMS,        // HH:MM:SS in narrow 3x7 digits with real spacing
  CLK_CUSTOM      // user strftime string, full font, scrolls if too wide
};

namespace Display {

void begin();
void tick();

void     setMode(DispMode m);
DispMode mode();
const char *modeName();

void   setMessage(const String &s);
String message();
bool   messageWasSet();

// Status text shown only while the user has not set their own message, so
// network state never clobbers what someone deliberately put on the panel.
void setStatusMessage(const String &s);

void       setScroll(ScrollPref p);
ScrollPref scroll();
const char *scrollName();
bool       scrollingNow();

void        setTextFont(TextFont f);
TextFont    textFont();
const char *textFontName();

void        setJustify(Justify j);
Justify     justify();
const char *justifyName();

void     setSpeed(uint16_t ms);
uint16_t speed();

void    setIntensity(uint8_t v);
uint8_t intensity();

void   setClockFormat(const String &f);   // also switches to CLK_CUSTOM
String clockFormat();

void        setClockFont(ClockFont f);    // persisted to NVS
ClockFont   clockFont();
const char *clockFontName();

void        setClockStyle(ClockStyle s);  // persisted to NVS
ClockStyle  clockStyle();
const char *clockStyleName();

// Countdown timer. Duration is set once; start/stop/reset behave like the
// stopwatch, and the display shows DONE (blinking briefly) when it reaches zero.
void     timerSet(uint32_t ms);
void     timerStart();
void     timerStop();
void     timerToggle();
void     timerReset();          // back to the full duration, stopped
bool     timerRunning();
bool     timerFinished();
uint32_t timerRemainingMs();
uint32_t timerDurationMs();

void     swStart();
void     swStop();
void     swToggle();
void     swReset();
bool     swRunning();
uint32_t swElapsedMs();

// Screen power. Uses the MAX7219's SHUTDOWN register rather than intensity 0,
// which still glows faintly. Persisted, so it stays off across a reboot.
void setScreen(bool on);
bool screen();

// 180-degree rotation, for mounting the panel upside down. Distinct from
// setMirror(), which compensates for this panel's chain wiring and is not a
// user-facing setting. Persisted to NVS.
void setFlip(bool on);
bool flip();

// Runtime panel tuning, so orientation can be corrected without a reflash.
void    setMirror(bool on);
bool    mirror();
void    setHwType(uint8_t idx);   // 0-7, indexes the DRxCRxRRx module types
uint8_t hwType();

uint16_t messageColumns();        // rendered width of the current content

}  // namespace Display

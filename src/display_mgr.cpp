#include "display_mgr.h"
#include "config.h"
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <time.h>
#include <Preferences.h>

namespace Display {

// ---------------------------------------------------------------------------
// Why this drives MD_MAX72XX directly instead of MD_Parola
//
// This panel's DIN enters its LEFTMOST module while MD_MAX72XX assumes column 0
// is on the right, so DR0CR0RR0_HW renders a clean full-width mirror of the
// intended image. CR/RR only flip pixels WITHIN an 8x8 module (HW_ROW/HW_COL
// are mod-8), so no module type can undo a module-order reversal.
//
// MD_Parola's PA_FLIP_LR can supply that mirror for static text, but its own
// header says it does not work with the SCROLL_* effects - and it doesn't: a
// message that fits in 32 columns scrolls fine, while anything longer streams
// character-by-character through a path the flip does not cover, and comes out
// as noise.
//
// So we own the rendering: build the message into a column array and blit a
// 32-column window ourselves. "Left" then means physical column 0 by
// construction, and message length is irrelevant - scrolling is just an offset.
//
// Note the mirror defaults to OFF: writing MD_MAX72XX's column buffer directly
// lands column p where we want it with no inversion. The mirroring that
// PA_FLIP_LR was compensating for lived in Parola's text layer, not in this
// column mapping - measured on the panel, not assumed. `mirror on|off` retunes
// it at runtime if a differently-wired panel ever needs the other convention.
// ---------------------------------------------------------------------------

static const MD_MAX72XX::moduleType_t HW_TYPES[8] = {
  MD_MAX72XX::DR0CR0RR0_HW, MD_MAX72XX::DR0CR0RR1_HW,
  MD_MAX72XX::DR0CR1RR0_HW, MD_MAX72XX::DR0CR1RR1_HW,
  MD_MAX72XX::DR1CR0RR0_HW, MD_MAX72XX::DR1CR0RR1_HW,
  MD_MAX72XX::DR1CR1RR0_HW, MD_MAX72XX::DR1CR1RR1_HW
};
#define DEFAULT_HW_INDEX 0      // DR0CR0RR0_HW - verified for this panel

static MD_MAX72XX mx = MD_MAX72XX(HW_TYPES[DEFAULT_HW_INDEX], CS_PIN, MAX_DEVICES);

#define MAX_MSG_COLS 1024
static uint8_t  g_cols[MAX_MSG_COLS];
static uint16_t g_colCount = 0;

static String     g_message    = "READY";
static String     g_status     = "";
static bool       g_messageSet = false;
static DispMode   g_mode       = MODE_TEXT;
static ScrollPref g_scroll     = SCROLL_AUTO;
static Justify    g_just       = JUST_LEFT;
static uint16_t   g_speed      = 40;
static uint8_t    g_intensity  = 5;
static String     g_clockFmt   = DEFAULT_CLOCK_FORMAT;
static ClockStyle g_clockStyle = CLK_HM_BAR;
static TextFont   g_textFont   = FONT_NORMAL;
static ClockFont  g_clockFont  = CFONT_BIG;
static uint32_t   g_nextBar    = 0;
static Preferences g_prefs;
static bool       g_mirror     = false;   // measured: no inversion needed here
static bool       g_flip       = false;   // user 180-degree rotation
static bool       g_screen     = true;    // false = MAX7219 in shutdown
static uint8_t    g_hwIndex    = DEFAULT_HW_INDEX;

static String   g_built      = "\x01";   // message the column array was built from
static bool     g_scrolling  = false;
static int32_t  g_offset     = 0;
static bool     g_dirty      = true;
static uint32_t g_nextStep   = 0;
static uint32_t g_nextEval   = 0;

// ---------------------------------------------------------------------------

static String formatStopwatch() {
  const uint32_t ms  = swElapsedMs();
  const uint32_t sec = ms / 1000;
  char out[16];
  if (sec < 100) snprintf(out, sizeof(out), "%lu.%02lu", (unsigned long)sec,
                          (unsigned long)((ms % 1000) / 10));
  else           snprintf(out, sizeof(out), "%lu:%02lu", (unsigned long)(sec / 60),
                          (unsigned long)(sec % 60));
  return String(out);
}

static String formatClock() {
  const time_t now = time(nullptr);
  if (now < 100000) return String("--:--");    // NTP has not landed yet
  struct tm tmv;
  localtime_r(&now, &tmv);
  char out[64];

  switch (g_clockStyle) {
    case CLK_HM_BLINK:
      // Colon blanks on odd seconds - the classic way to show a clock ticking
      // without spending columns on second digits.
      strftime(out, sizeof(out), (tmv.tm_sec & 1) ? "%H %M" : "%H:%M", &tmv);
      break;
    case CLK_MS:   strftime(out, sizeof(out), "%M:%S", &tmv);    break;
    case CLK_HMS:  strftime(out, sizeof(out), "%H:%M:%S", &tmv); break;
    case CLK_CUSTOM: strftime(out, sizeof(out), g_clockFmt.c_str(), &tmv); break;
    default:       strftime(out, sizeof(out), "%H:%M", &tmv);    break;  // HM_BAR
  }
  return String(out);
}

static String currentText() {
  switch (g_mode) {
    case MODE_STOPWATCH: return formatStopwatch();
    case MODE_CLOCK:     return formatClock();
    default:             return (g_messageSet || g_status.isEmpty()) ? g_message : g_status;
  }
}

// ---------------------------------------------------------------------------
// Compact 4x7 digits, so HH:MM:SS fits the panel without scrolling.
//
// The stock font's digits are 5 wide + a 1-column gap, which puts HH:MM:SS at
// 41 columns against a 32-column panel. These are 4 wide with the colon taking
// a single column and absorbing its neighbouring gaps, giving 29 columns.
//
// Bit 0 is the TOP row - same convention as the library font (its '0' is
// 0x3e,0x51,0x49,0x45,0x3e, where 0x3e is rows 1-5) and as setPoint(), which
// does bitSet(dig[column], row).
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Full-height 8-row digit fonts for clock and stopwatch.
//
// The stock font is only 7 rows because row 7 is reserved for DESCENDERS -
// 'g','j','p','q','y' all set bit 7. Text therefore keeps the stock font.
// Digits have no descenders, so numeric displays can use the whole 8 rows and
// come out ~14% taller.
//
// Bit 0 = top row, bit 7 = bottom, matching the library font and setPoint().
// Stored flat with a stride, so one renderer serves both widths.
// ---------------------------------------------------------------------------

// 5 wide - for the 4-digit layouts (HH:MM, MM:SS) and the stopwatch.
static const uint8_t DIGIT5[10 * 5] = {
  0x7E,0x81,0x81,0x81,0x7E,   // 0
  0x00,0x82,0xFF,0x80,0x00,   // 1
  0xC2,0xA1,0x91,0x89,0x86,   // 2
  0x42,0x81,0x89,0x89,0x76,   // 3
  0x18,0x14,0x12,0xFF,0x10,   // 4
  0x4F,0x89,0x89,0x89,0x71,   // 5
  0x7C,0x8A,0x89,0x89,0x70,   // 6
  0x01,0xC1,0x31,0x0D,0x03,   // 7
  0x76,0x89,0x89,0x89,0x76,   // 8
  0x0E,0x91,0x91,0x91,0x7E,   // 9
};

// 3 wide - the only way six digits plus two colons fit 32 columns.
static const uint8_t DIGIT3[10 * 3] = {
  0xFF,0x81,0xFF,   // 0
  0x82,0xFF,0x80,   // 1
  0xF9,0x89,0x8F,   // 2
  0x89,0x89,0xFF,   // 3
  0x0F,0x08,0xFF,   // 4
  0x8F,0x89,0xF9,   // 5
  0xFF,0x89,0xF9,   // 6
  0x01,0x01,0xFF,   // 7
  0xFF,0x89,0xFF,   // 8
  0x8F,0x89,0xFF,   // 9
};

#define GLYPH_COLON 0x24      // dots on rows 2 and 5, balanced for 8 rows
#define GLYPH_DOT   0x80      // row 7

// 3x8 UPPERCASE alphabet, so "narrow" works on real text and not just digits.
// The library has exactly one complete ASCII font, so letters at any other size
// have to be drawn by hand. Uppercase only - at 3 columns there is no room for
// lowercase to be distinguishable, so narrow messages are upper-cased.
//
// Every glyph is 3 wide + 1 gap, so N characters span 4N-1 columns:
// "HELLO" = 19 columns against 29 in the stock font.
static const uint8_t LETTER3[26 * 3] = {
  0xFF,0x09,0xFF,   // A
  0xFF,0x89,0x76,   // B
  0xFF,0x81,0x81,   // C
  0xFF,0x81,0x7E,   // D
  0xFF,0x89,0x89,   // E
  0xFF,0x09,0x09,   // F
  0xFF,0x81,0xF9,   // G
  0xFF,0x08,0xFF,   // H
  0x81,0xFF,0x81,   // I
  0xC0,0x80,0xFF,   // J
  0xFF,0x08,0xF7,   // K
  0xFF,0x80,0x80,   // L
  0xFF,0x06,0xFF,   // M
  0xFF,0x0C,0xFF,   // N
  0xFF,0x81,0xFF,   // O
  0xFF,0x09,0x0F,   // P
  0xFF,0xC1,0xBF,   // Q
  0xFF,0x09,0xEF,   // R
  0x8F,0x89,0xF9,   // S
  0x01,0xFF,0x01,   // T
  0xFF,0x80,0xFF,   // U
  0x7F,0x80,0x7F,   // V
  0xFF,0x60,0xFF,   // W
  0xF7,0x08,0xF7,   // X
  0x07,0xF8,0x07,   // Y
  0xE1,0x99,0x87,   // Z
};

// Lowercase, 3 wide. Caps keep the full 8 rows; lowercase sits on a 5-row
// x-height (rows 3-7) so the two are clearly distinguishable, with ascenders on
// b d f h k l t reaching up to row 1.
//
// No descenders: row 7 is already the last row, so g j p q y sit on the
// baseline. Normal for a font this small, and it lets caps stay full height.
static const uint8_t LOWER3[26 * 3] = {
  0xE8,0xA8,0xF0,   // a
  0xFE,0x88,0x70,   // b
  0xF8,0x88,0x88,   // c
  0x70,0x88,0xFE,   // d
  0xF8,0xA8,0xB8,   // e
  0x08,0xFE,0x0A,   // f
  0x38,0xA8,0xF8,   // g
  0xFE,0x08,0xF0,   // h
  0x00,0xFA,0x00,   // i
  0x80,0x80,0xFA,   // j
  0xFE,0x30,0xC8,   // k
  0x82,0xFE,0x80,   // l
  0xF8,0x18,0xF8,   // m
  0xF8,0x08,0xF0,   // n
  0xF8,0x88,0xF8,   // o
  0xF8,0x28,0x10,   // p
  0x10,0x28,0xF8,   // q
  0xF8,0x08,0x10,   // r
  0xB8,0xA8,0xE8,   // s
  0x08,0xFE,0x88,   // t
  0xF8,0x80,0xF8,   // u
  0x78,0x80,0x78,   // v
  0xF8,0xC0,0xF8,   // w
  0xD8,0x20,0xD8,   // x
  0x38,0xA0,0xF8,   // y
  0xC8,0xA8,0x98,   // z
};

// Punctuation, 3 wide like everything else. Worth covering broadly: a single
// unsupported character makes the WHOLE message fall back to the stock font,
// which is 7 rows and so leaves the bottom row blank - a jarring change for
// something as ordinary as a "!".
static const char PUNCT_CHARS[] = ":.-!?,'\"()/\\+=*#@;_<>%&";
static const uint8_t PUNCT3[][3] = {
  {0x00,0x24,0x00},   // :
  {0x00,0x80,0x00},   // .
  {0x18,0x18,0x18},   // -
  {0x00,0xBF,0x00},   // !
  {0x03,0x59,0x0F},   // ?
  {0x80,0xC0,0x00},   // ,
  {0x00,0x03,0x00},   // '
  {0x03,0x00,0x03},   // "
  {0x00,0x7E,0x81},   // (
  {0x81,0x7E,0x00},   // )
  {0xE0,0x3C,0x07},   // /
  {0x07,0x3C,0xE0},   // backslash
  {0x10,0x7C,0x10},   // +
  {0x28,0x28,0x28},   // =
  {0x14,0x08,0x14},   // *
  {0x24,0x7E,0x24},   // #
  {0x7E,0x99,0xB9},   // @
  {0x00,0xC4,0x00},   // ;
  {0x80,0x80,0x80},   // _
  {0x18,0x24,0x42},   // <
  {0x42,0x24,0x18},   // >
  {0x23,0xDB,0xC4},   // %
  {0x76,0x89,0x60},   // &
};

// One lookup for every narrow glyph; nullptr means "not in this font".
static const uint8_t *narrowGlyph(char c) {
  if (c >= '0' && c <= '9') return DIGIT3  + (size_t)(c - '0') * 3;
  if (c >= 'A' && c <= 'Z') return LETTER3 + (size_t)(c - 'A') * 3;
  if (c >= 'a' && c <= 'z') return LOWER3  + (size_t)(c - 'a') * 3;
  for (uint8_t i = 0; PUNCT_CHARS[i]; i++)
    if (PUNCT_CHARS[i] == c) return PUNCT3[i];
  return nullptr;
}

static bool narrowRenderable(const String &s) {
  if (s.isEmpty()) return false;
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == ' ' || narrowGlyph(c)) continue;
    return false;                            // fall back to the stock font
  }
  return true;
}

static bool buildNarrowText(const String &s) {
  if (!narrowRenderable(s)) return false;
  if ((uint16_t)(s.length() * 4) > MAX_MSG_COLS) return false;

  g_colCount = 0;
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];

    const uint8_t *g = narrowGlyph(c);

    if (g) { for (uint8_t k = 0; k < 3; k++) g_cols[g_colCount++] = g[k]; }
    else   { g_cols[g_colCount++] = 0; g_cols[g_colCount++] = 0;          }  // space

    if (i + 1 < s.length()) g_cols[g_colCount++] = 0;   // 1-column gap
  }
  g_built = s;
  return true;
}

// Only digits and the separators can use these fonts; anything else falls back
// to the stock font.
static bool digitsRenderable(const String &s) {
  if (s.isEmpty()) return false;
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (!((c >= '0' && c <= '9') || c == ':' || c == '.' || c == ' ')) return false;
  }
  return true;
}

// Column cost, so the caller can pick the widest font that still fits.
static uint16_t digitsWidth(const String &s, uint8_t w) {
  uint16_t n = 0;
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == ':' || c == '.') { n += 3; continue; }   // blank | glyph | blank
    if (c == ' ')             { n += 2; continue; }
    n += w;
    const char nx = (i + 1 < s.length()) ? s[i + 1] : '\0';
    if (nx && nx != ':' && nx != '.') n += 1;         // separators bring spacing
  }
  return n;
}

static void buildDigits(const String &s, uint8_t w) {
  const uint8_t *tbl = (w == 3) ? DIGIT3 : DIGIT5;
  g_colCount = 0;

  for (size_t i = 0; i < s.length() && g_colCount + w + 2 < MAX_MSG_COLS; i++) {
    const char c = s[i];

    if (c == ':' || c == '.') {
      g_cols[g_colCount++] = 0;
      g_cols[g_colCount++] = (c == ':') ? GLYPH_COLON : GLYPH_DOT;
      g_cols[g_colCount++] = 0;
      continue;
    }
    if (c == ' ') { g_cols[g_colCount++] = 0; g_cols[g_colCount++] = 0; continue; }

    const uint8_t *g = tbl + (size_t)(c - '0') * w;
    for (uint8_t k = 0; k < w; k++) g_cols[g_colCount++] = g[k];

    const char nx = (i + 1 < s.length()) ? s[i + 1] : '\0';
    if (nx && nx != ':' && nx != '.') g_cols[g_colCount++] = 0;
  }
  g_built = s;
}

// Pick the widest digit font that fits; false if none do.
static bool buildBigDigits(const String &s, uint8_t preferred) {
  if (!digitsRenderable(s)) return false;
  if (digitsWidth(s, preferred) <= PANEL_COLUMNS) { buildDigits(s, preferred); return true; }
  if (preferred != 3 && digitsWidth(s, 3) <= PANEL_COLUMNS) { buildDigits(s, 3); return true; }
  return false;
}

// Rasterise the string into g_cols using the library font.
static void build(const String &s) {
  // HH:MM:SS has no stock option - six 5-wide digits need 41 columns - so it is
  // always the narrow set. Every other layout follows the clock font setting;
  // "stock" simply falls through to the library font below.
  if (g_mode == MODE_CLOCK && g_clockStyle != CLK_CUSTOM) {
    if (g_clockStyle == CLK_HMS) {
      if (buildBigDigits(s, 3)) return;
    } else if (g_clockFont == CFONT_BIG) {
      if (buildBigDigits(s, 5)) return;
    }
  }
  if (g_mode == MODE_STOPWATCH) {
    if (buildBigDigits(s, 5)) return;
  }
  // Text may opt into the tall digit fonts, but only a numeric message can use
  // them - letters exist solely in the stock font.
  if (g_mode == MODE_TEXT && g_textFont == FONT_NARROW) {
    if (buildNarrowText(s)) return;          // letters included
  }

  g_colCount = 0;
  uint8_t cbuf[16];

  for (size_t i = 0; i < s.length() && g_colCount < MAX_MSG_COLS; i++) {
    const uint8_t n = mx.getChar((uint8_t)s[i], sizeof(cbuf), cbuf);
    for (uint8_t c = 0; c < n && g_colCount < MAX_MSG_COLS; c++)
      g_cols[g_colCount++] = cbuf[c];
    if (g_colCount < MAX_MSG_COLS) g_cols[g_colCount++] = 0x00;   // 1-col gap
  }
  if (g_colCount) g_colCount--;    // drop the trailing gap

  g_built = s;
}

// Reverse the bits of a column = flip it vertically. Row 0 becomes row 7.
static inline uint8_t bitrev8(uint8_t v) {
  v = ((v >> 1) & 0x55) | ((v & 0x55) << 1);
  v = ((v >> 2) & 0x33) | ((v & 0x33) << 2);
  v = ((v >> 4) & 0x0F) | ((v & 0x0F) << 4);
  return v;
}

// Blit a 32-column window starting at `offset` into the panel.
static void blit(int32_t offset) {
  if (!g_screen) return;      // drivers are in shutdown; nothing to send

  uint8_t barCols = 0;
  // The bar lives on row 7, which the 8-row digits occupy - so it is drawn
  // only with the stock font.
  if (g_mode == MODE_CLOCK && g_clockStyle == CLK_HM_BAR && g_clockFont == CFONT_STOCK) {
    const time_t now = time(nullptr);
    if (now >= 100000) {
      struct tm tmv;
      localtime_r(&now, &tmv);
      barCols = (uint8_t)(((uint32_t)tmv.tm_sec * PANEL_COLUMNS) / 60);
    }
  }

  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);   // batch, one flush at end
  for (int16_t p = 0; p < PANEL_COLUMNS; p++) {
    const int32_t src = offset + p;
    uint8_t v = (src >= 0 && src < (int32_t)g_colCount) ? g_cols[src] : 0x00;

    // Seconds bar: row 7 is free (every glyph here uses rows 0-6), so it can
    // be overlaid without disturbing the digits. Applied BEFORE the rotation so
    // it turns with everything else.
    if (barCols && p < barCols) v |= 0x80;

    // 180 rotation = reverse the column order AND flip each column vertically.
    // g_mirror is the panel's wiring fix and composes with it, so the two
    // horizontal reversals cancel when both are on.
    int16_t phys = g_flip ? (PANEL_COLUMNS - 1 - p) : p;
    if (g_mirror) phys = PANEL_COLUMNS - 1 - phys;

    mx.setColumn(phys, g_flip ? bitrev8(v) : v);
  }
  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
  mx.update();
}

static int32_t staticOffset() {
  if (g_colCount >= PANEL_COLUMNS) return 0;          // too wide: pin to the left
  switch (g_just) {
    case JUST_CENTER: return -((PANEL_COLUMNS - (int32_t)g_colCount) / 2);
    case JUST_RIGHT:  return -(PANEL_COLUMNS - (int32_t)g_colCount);
    default:          return 0;                        // JUST_LEFT
  }
}

static bool decideScroll() {
  const bool tooLong = g_colCount > PANEL_COLUMNS;
  if (g_mode != MODE_TEXT) return tooLong;
  switch (g_scroll) {
    case SCROLL_ON:  return true;
    case SCROLL_OFF: return false;
    default:         return tooLong;
  }
}

// ---------------------------------------------------------------------------

void begin() {
  g_prefs.begin("display", false);
  uint8_t st = g_prefs.getUChar("clkstyle", (uint8_t)CLK_HM_BAR);
  if (st <= (uint8_t)CLK_CUSTOM) g_clockStyle = (ClockStyle)st;
  g_clockFont = g_prefs.getUChar("clkfont", (uint8_t)CFONT_BIG) ? CFONT_BIG : CFONT_STOCK;
  g_flip      = g_prefs.getUChar("flip", 0) != 0;
  g_screen    = g_prefs.getUChar("screen", 1) != 0;

  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, g_intensity);
  mx.control(MD_MAX72XX::SHUTDOWN, g_screen ? MD_MAX72XX::OFF : MD_MAX72XX::ON);
  mx.clear();
  if (!g_screen)
    Serial.println(F("[disp] screen is OFF (saved) - send 'screen on' to wake it"));
  g_dirty = true;
}

void tick() {
  const uint32_t now = millis();

  // Re-evaluate content at 20 Hz - ample for hundredths on the stopwatch.
  if ((int32_t)(now - g_nextEval) >= 0) {
    g_nextEval = now + 50;
    const String want = currentText();
    if (g_dirty || want != g_built) {
      build(want);
      g_dirty     = false;
      g_scrolling = decideScroll();
      g_offset    = g_scrolling ? -(int32_t)PANEL_COLUMNS : staticOffset();
      g_nextStep  = now;
      blit(g_offset);
    }
  }

  // The seconds bar advances continuously while the HH:MM string changes only
  // once a minute, so it needs its own redraw tick - without this it sat
  // frozen between minute boundaries.
  if (!g_scrolling && g_mode == MODE_CLOCK && g_clockStyle == CLK_HM_BAR &&
      g_clockFont == CFONT_STOCK) {
    if ((int32_t)(now - g_nextBar) >= 0) {
      g_nextBar = now + 200;
      blit(g_offset);
    }
  }

  if (!g_scrolling) return;

  if ((int32_t)(now - g_nextStep) >= 0) {
    g_nextStep = now + g_speed;
    g_offset++;
    if (g_offset > (int32_t)g_colCount) g_offset = -(int32_t)PANEL_COLUMNS;  // wrap
    blit(g_offset);
  }
}

void setMode(DispMode m) { g_mode = m; g_dirty = true; }
DispMode mode()          { return g_mode; }
const char *modeName() {
  switch (g_mode) {
    case MODE_STOPWATCH: return "stopwatch";
    case MODE_CLOCK:     return "clock";
    default:             return "text";
  }
}

void setMessage(const String &s) { g_message = s; g_messageSet = true; g_dirty = true; }
String message()                 { return g_message; }
bool messageWasSet()             { return g_messageSet; }
void setStatusMessage(const String &s) { g_status = s; g_dirty = true; }

void setScroll(ScrollPref p) { g_scroll = p; g_dirty = true; }
ScrollPref scroll()          { return g_scroll; }
const char *scrollName() {
  switch (g_scroll) {
    case SCROLL_ON:  return "on";
    case SCROLL_OFF: return "off";
    default:         return "auto";
  }
}
bool scrollingNow() { return g_scrolling; }

void setTextFont(TextFont f) { g_textFont = f; g_dirty = true; }
TextFont textFont()          { return g_textFont; }
const char *textFontName() {
  return g_textFont == FONT_NARROW ? "narrow" : "normal";
}

void setJustify(Justify j) { g_just = j; g_dirty = true; }
Justify justify()          { return g_just; }
const char *justifyName() {
  switch (g_just) {
    case JUST_CENTER: return "center";
    case JUST_RIGHT:  return "right";
    default:          return "left";
  }
}

void setSpeed(uint16_t ms) { g_speed = ms; }
uint16_t speed()           { return g_speed; }

void setIntensity(uint8_t v) {
  g_intensity = v > 15 ? 15 : v;
  mx.control(MD_MAX72XX::INTENSITY, g_intensity);
}
uint8_t intensity() { return g_intensity; }

void setClockFormat(const String &f) {
  g_clockFmt   = f;
  g_clockStyle = CLK_CUSTOM;          // an explicit format implies custom
  g_dirty      = true;
}
String clockFormat() { return g_clockFmt; }

void setClockFont(ClockFont f) {
  g_clockFont = f;
  g_prefs.putUChar("clkfont", (uint8_t)f);
  g_dirty = true;
}
ClockFont clockFont() { return g_clockFont; }
const char *clockFontName() { return g_clockFont == CFONT_BIG ? "big" : "stock"; }

void setClockStyle(ClockStyle st) {
  g_clockStyle = st;
  g_prefs.putUChar("clkstyle", (uint8_t)st);
  g_dirty = true;
}
ClockStyle clockStyle() { return g_clockStyle; }
const char *clockStyleName() {
  switch (g_clockStyle) {
    case CLK_HM_BLINK: return "hmblink";
    case CLK_MS:       return "ms";
    case CLK_HMS:      return "hms";
    case CLK_CUSTOM:   return "custom";
    default:           return "hmbar";
  }
}

void setScreen(bool on) {
  g_screen = on;
  g_prefs.putUChar("screen", on ? 1 : 0);
  mx.control(MD_MAX72XX::SHUTDOWN, on ? MD_MAX72XX::OFF : MD_MAX72XX::ON);
  g_dirty = true;             // repaint from scratch when it comes back
}
bool screen() { return g_screen; }

void setFlip(bool on) {
  g_flip  = on;
  g_prefs.putUChar("flip", on ? 1 : 0);
  g_dirty = true;
}
bool flip() { return g_flip; }

void setMirror(bool on) { g_mirror = on; g_dirty = true; }
bool mirror()           { return g_mirror; }

void setHwType(uint8_t idx) {
  if (idx > 7) return;
  g_hwIndex = idx;
  mx.setModuleType(HW_TYPES[idx]);
  mx.control(MD_MAX72XX::INTENSITY, g_intensity);
  mx.clear();
  g_dirty = true;
}
uint8_t hwType() { return g_hwIndex; }

uint16_t messageColumns() { return g_colCount; }

// Stopwatch ------------------------------------------------------------------
static bool     g_swRunning = false;
static uint32_t g_swAccum   = 0;
static uint32_t g_swStart   = 0;

void swStart() { if (!g_swRunning) { g_swStart = millis(); g_swRunning = true; } }
void swStop()  { if (g_swRunning) { g_swAccum += millis() - g_swStart; g_swRunning = false; } }
void swToggle(){ g_swRunning ? swStop() : swStart(); }
void swReset() { g_swRunning = false; g_swAccum = 0; }
bool swRunning() { return g_swRunning; }
uint32_t swElapsedMs() {
  return g_swRunning ? g_swAccum + (millis() - g_swStart) : g_swAccum;
}

}  // namespace Display

# esp32_4x8x8_led_matrix

Firmware for a 32×8 LED matrix — four daisy-chained MAX7219 8×8 modules driven by
an ESP32. Displays scrolling text, a stopwatch, and a clock, controllable from a
phone browser, telnet, or USB serial.

## Hardware

| Panel | ESP32 | Notes |
|---|---|---|
| DIN | GPIO23 | VSPI MOSI |
| CLK | GPIO18 | VSPI SCK |
| CS  | GPIO5  | VSPI SS |
| VCC | 5V     | own supply recommended |
| GND | GND    | **must be common with the ESP32** |

Tested on an **ESP32-D0WDQ6** (classic, dual-core) with a level shifter on the
three data lines. The ESP32 is 3.3 V, but a MAX7219 running at VCC=5 V wants
V<sub>IH</sub> ≥ 3.5 V — driving it directly is out of spec and works right up
until it doesn't. A 74AHCT125 buffer (or MAX7221 modules) is the correct fix.

Four modules at full brightness can pull well over an amp, so power the panel
from its own supply rather than the ESP32's regulator.

## Build and flash

```bash
pio run                 # build
pio run -t upload       # flash over USB
pio run -t erasenvs     # wipe saved WiFi credentials and settings
pio run -t eraseall     # full chip erase (needs a re-upload after)
```

**This board has no auto-reset circuit.** Before every upload:

1. Hold **BOOT** (IO0)
2. Tap **EN** (RESET)
3. Release **BOOT**

Then upload, and press **EN** afterwards to run the firmware. `platformio.ini`
sets `--before=no-reset --after=no-reset` for this reason.

On Windows, run PlatformIO with `PYTHONIOENCODING=utf-8`. Without it, esptool's
progress output can raise `UnicodeEncodeError` inside PlatformIO's output
thread, which kills the reader **and makes a failed upload report `[SUCCESS]`**.

## Control

Three surfaces, one shared parser (`commands.cpp`), so a command behaves
identically on all of them.

- **Web** — `http://ledpanel.local` or the device IP. Mobile-friendly page with
  live status polling; every button posts a plain command to `/api/cmd`.
- **TCP** — port **23**: `telnet <ip>` or `nc <ip> 23`. Telnet IAC negotiation is
  parsed and refused, so the first command works immediately.
- **Serial** — **115200 baud**.

### Commands

```
text <msg>                      show a message (switches to text mode)
mode text|stopwatch|clock
scroll on|off|auto              auto = scroll only when wider than 32 columns
align left|center|right
font normal|narrow              normal = 5x7 full ASCII; narrow = 3x8
speed <ms>                      scroll step, 5-1000
bright <0-15>
screen on|off|toggle            MAX7219 shutdown - genuinely dark
flip on|off                     180-degree rotation for upside-down mounting
mirror on|off                   panel wiring compensation (rarely needed)
hw <0-7>                        MAX7219 module type, for orientation debugging
sw start|stop|toggle|reset      stopwatch
clock hmbar|hmblink|ms|hms      clock layout
clock custom <strftime>
clock font stock|big            5x7 (leaves room for the bar) or 8-row digits
tz <posix>                      e.g. PST8PDT,M3.2.0,M11.1.0
wifi status|set <ssid> [pass]|clear|portal
status | help | reboot
```

Settings marked persistent — clock style and font, flip, screen, timezone, WiFi
credentials — are stored in NVS and survive a reboot.

## Display modes

**Text** — auto-scrolls only when the message exceeds 32 columns.

**Stopwatch** — `SS.hh` under 100 s, then `M:SS`. Timing comes from `millis()`
deltas, so display rate never affects accuracy.

**Clock** — NTP over WiFi. 32 columns cannot hold six readable digits (5.3
columns each including separators), so there are four layouts:

| Layout | Shows | Columns |
|---|---|---|
| `hmbar` | `HH:MM` + seconds bar on the bottom row | 24 |
| `hmblink` | `HH:MM`, colon blinks each second | 24 |
| `ms` | `MM:SS` | 26 |
| `hms` | `HH:MM:SS` in narrow 3×8 digits | 27 |

`clock font big` gives 8-row digits. It is mutually exclusive with `hmbar`'s
seconds bar, which needs row 7 — choosing one gives up the other.

## Fonts

**normal** — the MD_MAX72XX 5×7 font: full ASCII, lowercase, descenders. Row 7
is left blank because `g j p q y` hang into it.

**narrow** — hand-drawn 3×8 in `display_mgr.cpp`: full-height caps, lowercase on
a 5-row x-height, digits, and punctuation. About 34% narrower — `HELLO` is 19
columns versus 29 — so noticeably more text fits before scrolling. Lowercase has
no descenders (row 7 is already the last row), which is what lets caps stay full
height. A character outside the set makes the whole message fall back to
`normal`; the giveaway is the bottom row going blank.

## WiFi

Starts an **AP config portal** when it has no saved network, and falls back to
one after 30 s failing to associate or 30 s of a dropped link.

- AP: `LEDPanel-XXXX`, password `ledpanel123`, config at `192.168.4.1`
- Captive DNS, so phones open the setup page automatically
- While the portal is up it keeps scanning for the saved SSID and rejoins on its
  own — but pauses scanning whenever a client is attached, because a scan takes
  the single radio off-channel and would drop whoever is configuring it

## Panel orientation

`DR0CR0RR0_HW` with `mirror off`, determined by measurement. Two traps make this
non-obvious:

- `CR`/`RR` flip pixels only **within** an 8×8 module (`HW_ROW`/`HW_COL` are
  mod-8), so no module type can reverse the order of modules along the chain.
- With `DR0` the digit registers hold **columns**, so the flip bits swap
  apparent roles: `_hwRevCols` bit-reverses a digit byte (a *vertical* flip) and
  `_hwRevRows` selects a different digit (a *horizontal* flip).

Diagnose orientation with a **static asymmetric glyph** such as `F`, never
scrolling text — under scrolling, "mirrored" and "upside down" are
indistinguishable. `hw 0-7` and `mirror on|off` retune it live, without a
reflash.

## Notes and limitations

**No BLE.** Removed deliberately. On this board the Bluetooth controller hangs
the system at startup and shifts the UART baud by exactly 26/40 — the ESP32
crystal ratio — with or without WiFi running. WiFi alone is rock solid. Verified
by testing WiFi-only, BLE-only, and both: BLE alone fails, so it is not a
coexistence problem. Retry on different hardware before spending time in
firmware.

**No OTA yet.** `huge_app.csv` has a single 3 MB app slot and no OTA partitions.
Firmware is ~1.05 MB, so `min_spiffs.csv` (two 1.875 MB slots) would fit
comfortably — but switching needs one serial flash and wipes NVS.

**No authentication** on the web UI or TCP port. Fine on a trusted LAN; do not
expose it.

**Health instrumentation** stays compiled in: a 2 s heartbeat with heap, clocks
and loop timing, plus a monitor task pinned to the core `loop()` does *not* use,
which reports which subsystem a hang occurred in. Loop runs ~15,400 iterations/s
with a worst case around 400 µs, against a 40 ms scroll interval.

## Layout

```
platformio.ini          esp32dev, pioarduino core 3.3.11, custom targets
scripts/extra_targets.py    erasenvs / eraseall
src/
  main.cpp              setup/loop, watchdog, health instrumentation
  config.h              pins, orientation notes, network constants
  display_mgr.*         rendering, fonts, clock/stopwatch, orientation
  netmgr.*              WiFi state machine, AP portal, web routes, JSON API
  tcpsrv.*              TCP/telnet control incl. IAC negotiation
  commands.*            the shared command parser
  webui.h              mobile control page (PROGMEM)
```

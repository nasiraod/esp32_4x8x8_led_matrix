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
pio run                        # build
pio run -e esp32ota -t upload  # flash over WiFi (normal route)
pio run -t upload              # flash over USB (recovery route)
pio run -t erasenvs            # wipe saved WiFi credentials and settings
pio run -t eraseall            # full chip erase (needs a re-upload after)
```

**Updates normally go over the air** and need no cable, no buttons. The serial
route below is for recovery, or for changing the partition table.

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
mode text|stopwatch|timer|clock
scroll on|off|auto              auto = scroll only when wider than 32 columns
align left|center|right
font normal|narrow              normal = 5x7 full ASCII; narrow = 3x8
speed <ms>                      scroll step, 5-1000
bright <0-15>
screen on|off|toggle            MAX7219 shutdown - genuinely dark
sleep HH:MM HH:MM [dim]         blank (or dim to 0-15) between two times
sleep off                       disable the schedule
flip on|off                     180-degree rotation for upside-down mounting
mirror on|off                   panel wiring compensation (rarely needed)
hw <0-7>                        MAX7219 module type, for orientation debugging
sw start|stop|toggle|reset      stopwatch
timer 5m|90s|1h30m|MM:SS        countdown; setting a duration starts it
timer start|stop|toggle|reset
clock hmbar|hmblink|ms|hms      clock layout
clock custom <strftime>
clock font stock|big            5x7 (leaves room for the bar) or 8-row digits
tz <posix>                      e.g. PST8PDT,M3.2.0,M11.1.0
wifi status|set <ssid> [pass]|clear|portal
icons                           list the icon names
build                           firmware timestamp + git revision
status | help | reboot
```

**Everything persists.** Mode, message, font, alignment, scrolling, speed,
brightness, clock style and format, flip, screen state, timer duration, the
sleep schedule, timezone and WiFi credentials are all stored in NVS and come
back after a power cut.

Writes are debounced by two seconds rather than applied per change — OTA
progress alone calls `setMessage()` about a hundred times per update, and
coalescing keeps that to a single flash write.

## Display modes

**Text** — auto-scrolls only when the message exceeds 32 columns.

**Stopwatch** — `SS.hh` under 100 s, then `M:SS`. Timing comes from `millis()`
deltas, so display rate never affects accuracy.

**Timer** — counts down and shows `DONE`, blinking for the first 30 s. `M:SS`
above a minute, `SS.h` below it. It keeps running in the background if you
switch modes, and finishes regardless.

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

## Icons

Written inline in message text, 8x8, using all 8 rows:

```
text {bell} Laundry done      still - a single representative frame
text {bell*} Laundry done     animated
icons                         list them
```

| | |
|---|---|
| Status | `wifi` `bell` `heart` `hourglass` `warn` `check` |
| Notification | `mail` `home` `phone` `alarm` |
| Weather | `sun` `cloud` `rain` `snow` `moon` |
| Ambient | `smile` `note` `star` |

All 18 animate: the mail flap opens, the phone shakes, rain and snow fall, the
star sparkles. Frames advance every 200 ms, and an animated icon inside a
scrolling message rebuilds **in place** - resetting the scroll offset on each
frame would leave a long message permanently off-screen.

At 8 columns an icon is a quarter of the panel, so they read best as a prefix to
short text. `{name}` is reserved syntax; an unknown name renders literally.

The art was designed as ASCII, rendered and reviewed, and only then generated
into `src/icons.h` - worth repeating for any future glyph work.

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

## Firmware updates (OTA)

Three ways, all landing in `otamgr.cpp`:

```bash
pio run -e esp32ota -t upload    # or: pio run -t ota
curl -u admin:<OTA_PASSWORD> -F "firmware=@.pio/build/esp32dev/firmware.bin"      http://ledpanel.local/update
```

or the **Firmware** card in the web UI — pick `firmware.bin` from your phone.

Credentials are in `src/config.h` (`OTA_HOSTNAME`, `OTA_PASSWORD`,
`OTA_TRUST_MS`); the same password guards ArduinoOTA and the HTTP form. **Change
it from the default** before putting this on a network you do not control.

Progress shows on the panel as `OTA nn%` in the narrow font, and the task
watchdog is fed during flash writes — without that, a large upload starves
`loop()` and the 10 s watchdog aborts it mid-write.

**Rollback** is trust-on-survival: a marker goes into NVS before rebooting into a
new image and clears after 30 s. If the next boot finds the marker set *and* the
reset reason indicates a crash, it switches back to the previous slot. The reset
reason is checked deliberately, so power-cycling soon after a good update does
not trigger a spurious rollback.

This only helps for an image that boots and *then* dies. Firmware that hangs
before `setup()` never reaches the rollback code, so **serial remains the
recovery path** — which is why `esp32dev` is still the default environment.

## Build identity

Every build regenerates `src/build_id.h` (gitignored) via
`scripts/extra_targets.py`:

```
build 2026-09-07 05:12:24 g956e2d1+      ('+' = uncommitted changes)
```

Reported at boot, by `build`, in `status`, and in the JSON API. Since updates go
over the air, "did that upload actually land?" comes up constantly - this makes
it checkable rather than a guess. A bare `__DATE__`/`__TIME__` would not do:
those only change when the file holding them is recompiled, so a stale stamp can
claim a build that never happened.

## Notes and limitations

**No BLE.** Removed deliberately. On this board the Bluetooth controller hangs
the system at startup and shifts the UART baud by exactly 26/40 — the ESP32
crystal ratio — with or without WiFi running. WiFi alone is rock solid. Verified
by testing WiFi-only, BLE-only, and both: BLE alone fails, so it is not a
coexistence problem. Retry on different hardware before spending time in
firmware.

**Partitions are `min_spiffs.csv`** — two 1.875 MB app slots so OTA can write to
the inactive one. Firmware is ~1.05 MB, about 56% of a slot. Note that `nvs`
sits at `0x9000` in both this and `huge_app.csv`, so the switch preserved saved
settings; check the offsets before assuming a partition change costs
re-provisioning.

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
  icons.h               generated 8x8 icon frames
  buildinfo.*           build identity accessor
  otamgr.*              OTA updates, progress display, rollback
  sleepsched.*          scheduled screen off/dim
  webui.h              mobile control page (PROGMEM)
```

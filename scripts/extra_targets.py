Import("env")

import datetime, subprocess, os, io

# ---------------------------------------------------------------------------
# Build identity, regenerated on every build into src/build_id.h (gitignored).
#
# A bare __DATE__/__TIME__ is not enough: those only change when the file
# holding them is recompiled, so a stale stamp can claim a build that never
# happened. Writing the header each build means the one file that includes it
# always rebuilds, and the value always moves.
#
# Format: "2026-09-05 05:10:33 g0362090+"   ('+' = uncommitted changes)
# ---------------------------------------------------------------------------
def _git(args, default):
    try:
        return subprocess.check_output(["git"] + args, stderr=subprocess.DEVNULL,
                                       cwd=env.subst("$PROJECT_DIR")).decode().strip()
    except Exception:
        return default

_rev = _git(["rev-parse", "--short", "HEAD"], "nogit")
_dirty = "+" if _git(["status", "--porcelain"], "") else ""
_stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
_bid = '%s g%s%s' % (_stamp, _rev, _dirty)

_path = os.path.join(env.subst("$PROJECT_DIR"), "src", "build_id.h")
_nl = chr(10)
_text = ("// Generated on every build by scripts/extra_targets.py - do not edit." + _nl +
         "#pragma once" + _nl +
         ('#define BUILD_ID "%s"' % _bid) + _nl)
_old = ""
if os.path.exists(_path):
    _old = io.open(_path, encoding="utf-8").read()
if _old != _text:
    io.open(_path, "w", encoding="utf-8", newline=_nl).write(_text)
print("build id: %s" % _bid)


# ---------------------------------------------------------------------------
# `pio run -t erasenvs`  - wipe ONLY the NVS partition.
#
# NVS holds the saved WiFi credentials and the radio mode. Selecting a radio
# mode that hangs at boot (BLE does on this board) leaves the device unable to
# receive the command that would change it back, so this is the way out.
#
# NVS lives at 0x9000 size 0x5000 in the default/huge_app partition layouts.
# The app itself is untouched, so no reflash is needed afterwards.
#
# This board has no auto-reset circuit: hold BOOT, tap EN, release BOOT first.
# ---------------------------------------------------------------------------
env.AddCustomTarget(
    name="erasenvs",
    dependencies=None,
    actions=[
        '"$PYTHONEXE" -m esptool --chip esp32 --port "$UPLOAD_PORT" '
        "--before no-reset --after no-reset erase-region 0x9000 0x5000"
    ],
    title="Erase NVS",
    description="Wipe saved WiFi credentials and radio mode (app untouched)",
)

# `pio run -t eraseall` - full chip erase, when NVS alone is not enough.
env.AddCustomTarget(
    name="eraseall",
    dependencies=None,
    actions=[
        '"$PYTHONEXE" -m esptool --chip esp32 --port "$UPLOAD_PORT" '
        "--before no-reset --after no-reset erase-flash"
    ],
    title="Erase entire flash",
    description="Full erase - requires a re-upload afterwards",
)

# `pio run -t ota` - build, then push over WiFi. No cable, no BOOT/EN sequence.
# Requires the running firmware to be on the network; falls back to a serial
# `pio run -t upload` if the device is unreachable.
env.AddCustomTarget(
    name="ota",
    dependencies="$BUILD_DIR/${PROGNAME}.bin",
    actions=[
        '"$PYTHONEXE" "$PROJECT_PACKAGES_DIR/framework-arduinoespressif32/tools/espota.py" '
        '-i ledpanel.local -a ledpanel-ota -f "$BUILD_DIR/${PROGNAME}.bin" -r'
    ],
    title="OTA upload",
    description="Build and push firmware over WiFi",
)

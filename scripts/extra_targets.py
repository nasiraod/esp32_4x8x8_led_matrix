Import("env")

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

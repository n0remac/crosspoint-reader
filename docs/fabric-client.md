# Fabric client

The Fabric entry on Home opens the page list returned by `GET /api/pages`. Only
pages declaring Fabric `0.2` are shown. Choose a page with touch or mapped
navigation buttons. HTTP LAN URLs such as `http://192.168.1.50:8080` and HTTPS
server URLs are supported. A non-empty `fabricServerUrl` saved in
`/.crosspoint/settings.json` takes precedence over the compiled default. Missing
or empty saved values use the compiled default; with no default, opening Fabric
asks for a URL. The error screen shows the configured address and HTTP status
when a response arrived. Use Up/Down or Left/Right to select **Retry**, **Fabric
server URL**, or **Use default server** (when a default is compiled in), then
Confirm. Touch activates the same actions. An invalid URL selects the URL editor
by default and is rejected before connecting to Wi-Fi. The editor validates new
addresses before saving; clearing the editor uses the compiled default.

Personal builds can set the default in the gitignored `platformio.local.ini`:

```ini
[fabric]
default_server_url = https://fabric-pi.example.ts.net
```

The checked-in default is empty. `FABRIC_DEFAULT_SERVER_URL` initializes the
existing 128-byte settings field, so addresses must fit in 127 bytes plus the
terminator. **Use default server** saves the compiled address and immediately
reloads the page list, discarding navigation from the previous server. Alternatively,
clear or remove the saved `fabricServerUrl` on SD and reboot. Flashing firmware does not erase settings on SD.
The Fabric page transport encrypts HTTPS requests but currently skips certificate
verification for both GET and POST because it has no CA bundle configured.
Page actions are not authenticated. Firmware OTA uses a separate, certificate-
verified client and does not send its reader credential through this page transport.

## Firmware registry OTA on the Pi

The default X3/X4 build uses the compiled `fabric.default_server_url` for firmware
requests. A saved page URL on SD cannot change the firmware origin. When this
default is an HTTPS URL, **Settings → Update Firmware** checks Fabric's dev
channel; otherwise it retains the existing GitHub release flow. The updater
selects a configured Wi-Fi network, reads its reader credential from NVS,
queries the latest compatible X3 build, and downloads it with the ESP-IDF CA
bundle and hostname verification. It checks the image's size, chip and board
tag, and SHA256 before selecting the inactive OTA partition for the next boot.
The About screen shows the first 12 characters of the installed Fabric build ID
after a successful update; a dash means no Fabric update has been installed.

For the first USB bootstrap only, a private `src/FabricProvisioning.local.h` may
define `FABRIC_BOOTSTRAP_READER_TOKEN` as a quoted string. The firmware copies
it into NVS at startup if no reader credential exists there. Remove the header
after USB flashing and before building any publishable image. Never commit it,
and do not erase NVS when flashing subsequent builds. The Pi's publisher token
must never be placed on the device.

With Fabric running locally and the publisher token in
`~/.config/fabric/publisher.token`, publish the token-free application with:

```bash
pio run -e default -t fabric-deploy
```

This target builds `firmware.bin`, rejects a present bootstrap header or an
image containing this Pi's reader credential, and uploads the image to the
immutable Fabric registry. It advances the dev channel only. The default
publisher URL is `http://127.0.0.1:8080`; set `FABRIC_DEPLOY_SERVER` to an HTTPS
origin when publishing from another machine. The target uses the source's Git
branch and commit in the displayed version and records whether the checkout
was dirty. Repeated deployments create distinct build IDs. See Fabric's
`docs/firmware.md` for promotion and credential management.

The firmware handles page components and sends only a component ID to the
server's action endpoint. It does not interpret the action's `name`, `args`, or
requested URL. The server owns application state and action execution. A
navigate response opens the returned page, an action response's `data` replaces
the current page data, and Back uses the local eight-page history. Returning to
the immediately previous page collapses that history entry. Home exits Fabric.

The retained page and current data are separate ArduinoJson documents. Network
bodies are capped at 24 KiB for pages and 32 KiB for data/action responses;
validation limits pages to 96 components, depth 8, 24 action targets, and chart
reads to 256 points. One checked 32 KiB response buffer is allocated on first
use and reused for all transfers during the activity. ArduinoJson copies the
strings it retains, so the response buffer can be reused. The URL buffer lives
in the activity-owned client instead of on the task stack. Both buffers are
released when the activity exits. A 512-byte read chunk is also checked and
freed on each ESP-IDF POST; keeping it off the task stack avoids exceeding the
256-byte local-variable budget.

Chart rendering scans JSON twice to derive bounds and draw points without a
second point array. Numeric and RFC3339 X fields are supported. Point order is
preserved; the server should provide chronological series. Missing values are
skipped. The device renders text, metric, divider, button, row, column, card,
list, progress, and chart components through FreeInkUI's draw target;
components with declared actions use its interaction table. Focus follows
document order. Up/down or left/right buttons move focus; vertical touch swipes
scroll long pages through the mapped input abstraction.

## Verification

Run `./test/fabric/run-host.sh` for parser, binding, formatting, and navigation
checks using page fixtures copied from `n0remac/Fabric/pages` on 2026-09-27.
Build the X4 firmware with `pio run -e default` and, for X4 Pro, the relevant
board environment. In the current development container the system `pio`
installation had a missing SCons module; the C3 build passed using the newer
PlatformIO executable with `SCONS_LIB_DIR` pointed at an intact temporary
SCons 4.11.1 tree. This environment workaround is outside the repository.

On an X4 or X4 Pro, enable debug logging and capture Serial while opening
Fabric, choosing **Markets**, selecting a ticker, opening **Stock Detail**,
changing a period, pressing Back, tapping Refresh, and leaving interval refresh
running for at least ten cycles. The `FABRIC` log records free heap and largest
free block before download, after download, after JSON parsing, and after
rendering. Compare the same stages across cycles for leak or fragmentation.
Also verify malformed/oversized pages and a disconnected server show the error
UI, and that Home exits to CrossPoint.

| Device | Server fixture | Before GET | After GET | After parse | After render | Largest block after ten intervals |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| X4 / X4 Pro | stocks and stock-detail | pending device run | pending | pending | pending | pending |

No X4/X4 Pro serial device was attached to the development host, so these
numbers and touch behavior require a device run before the milestone can be
called complete. No Fabric protocol change was required by the host fixtures
or firmware build.

## Default server verification

Run `pio run -e default` to build, then `pio run -e default -t upload` to flash
an attached X3. Connect to Wi-Fi and open Fabric with an absent or empty saved
URL: the URL keyboard should be skipped when a default is compiled in. Verify
`GET /api/pages`, opening a page, fetching page data, and executing an action
(`POST /api/pages/<id>/actions`) over HTTPS. Monitor the existing `FABRIC` free
heap and largest-block logs across repeated GET and POST requests.

Use the server URL control on the error screen to save an override and reboot
to verify it takes precedence. Clear or remove only `fabricServerUrl` in the SD
settings file, reboot, and verify the compiled default returns. A build with an
empty default and no saved URL should still show the URL keyboard. Also check
that plain HTTP servers continue to work.

## Recovering from a bad address

With a malformed saved URL, verify that Fabric shows the address and an HTTP/HTTPS
validation message before opening Wi-Fi selection. Confirm should open the URL
editor; Up/Down and Left/Right must visibly select each recovery action. Verify
**Use default server** connects to the compiled address and remains selected as
the server after reboot. Without a compiled default, only Retry and the editor
should appear. Check the layout in portrait and landscape, and verify touch on
a touch-capable device. A 404/503 response should show its HTTP status; a connection
failure should explain that no HTTP response arrived.

For Serial logs, use `pio device list` to find the reader's current port, then
`pio device monitor -e default --port /dev/ttyACM1 --baud 115200` (replace the port
with the one listed). USB numbering can change after an upload or reboot. On
Linux, a `monitor_port` under `/dev/serial/by-id/` in the local environment override
keeps `pio device monitor -e default` bound to the same reader. Fabric logs the
settings source, URL validation reason, server connection, GET/POST URL, response
status and byte count, JSON errors, and recovery selections. URLs containing
credentials are rejected without printing their contents to Serial.

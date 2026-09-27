# Fabric client

The Fabric entry on Home opens the page list returned by `GET /api/pages`. Only
pages declaring Fabric `0.2` are shown. Choose a page with touch or mapped
navigation buttons. The first visit asks for a plain HTTP LAN server URL such as
`http://192.168.1.50:8080`; it is saved as `fabricServerUrl` in CrossPoint's
settings JSON on SD. The error screen offers **Retry** and **Fabric server URL**.

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

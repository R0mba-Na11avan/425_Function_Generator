# Web pages

The pages the generator serves, as plain HTML/CSS/JS. The firmware doesn't
read this folder: `tools/embed_web.py` turns each page into one string in
`src/web/*_html.h`, which is what gets compiled.

| Folder | Becomes | Served |
|---|---|---|
| `control/` | `src/web/index_html.h` (`INDEX_HTML`) | at `WEB_CONTROL_PATH` (`/control`), and at `/` outside setup mode |
| `setup/` | `src/web/provisioning_html.h` (`PROVISIONING_HTML`) | at `/` in setup (AP) mode only |

## Changing a page

1. Edit the files here (`index.html`, `style.css`, `app.js`).
2. Run `python tools/embed_web.py`. It rewrites the headers that changed.
3. Build as usual. `bash test/run_tests.sh` fails if a header is older than
   its files here, and syntax-checks the JS when `node` is installed.

Never edit `src/web/*_html.h` by hand, the next run overwrites them.

Rules the script enforces:

- Each page stays self-contained: `index.html` may only pull in
  `<link rel="stylesheet" href="style.css">` and
  `<script src="app.js"></script>` from its own folder, and they are inlined.
  No CDNs or other files (a phone on the setup AP has no internet, and every
  extra file would need its own route).
- `{{NAME}}` is replaced by the `src/config.h` `#define` of that name, so the
  page can't disagree with the firmware (`{{API_STATE_PATH}}`,
  `{{WEB_POLL_MS}}`, ...). An unknown name is an error. In JS keep them inside
  quotes (`'{{API_STATE_PATH}}'`, `Number('{{WEB_POLL_MS}}')`) so the file
  stays valid JavaScript.

Opening `index.html` straight from disk shows the layout, but the page then
has no generator to talk to (and the placeholders are still in it), so the
buttons do nothing.

## Control page (`control/`)

Plain styling for now, a nicer look is planned later.

Controls tab
- CH1 / CH2 buttons pick the channel the controls below show and change
  (each request carries `"channel"`). The Link toggle turns the mirror on/off:
  CH2 follows CH1, so while it is on CH2's controls (all but the output) are
  disabled with a note saying why
- waveform buttons, output toggle, number fields for frequency (with a
  Hz/kHz/MHz unit), amplitude, DC offset and phase
- a field is sent when Enter is pressed or it loses focus (one
  `POST API_STATE_PATH` per change, one setting each). Escape undoes the
  typing. Out-of-range values are clamped by the firmware and the note it
  returns shows under the field
- next to amplitude and offset: the range allowed right now (they share the
  peak limit, so each moves with the other)
- a sketch of two cycles of the output, scaled to ±`output_peak_v`

Device tab
- firmware, Wi-Fi and Bluetooth info from `API_INFO_PATH`, read-only,
  refreshed while the tab is open

Live sync
- polls `API_STATE_PATH` every `poll_ms` (from `/api/info`, `WEB_POLL_MS`),
  paused while the browser tab is hidden. A field being typed in is never
  overwritten. Fields changed elsewhere (knob, MCP, BLE, another browser)
  flash briefly
- a poll that started before one of our own changes finished is dropped, so it
  can't put the old value back for a moment
- a banner shows while the generator can't be reached

Text from the device (SSIDs, BLE name) is only inserted with `textContent`.

The API itself is described in `src/comms/rest_api.h`.

## Setup page (`setup/`)

- on load it fetches `/scan` and lists the networks (SSID, signal, lock),
  polling while a scan runs. Refresh starts a new scan (`POST /scan`). A
  hidden SSID can be typed in
- Save POSTs `{ssid, password}` as JSON to `/save`, then explains that the
  device restarts and the page will lose its connection
- links to the control page (`WEB_CONTROL_PATH`), which also works on the
  setup network
- SSIDs come from anyone nearby, so they are only ever inserted with
  `textContent`, never `innerHTML`

The routes are described in `src/comms/provisioning.h`.

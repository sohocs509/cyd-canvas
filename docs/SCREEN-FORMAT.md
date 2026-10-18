# cyd-canvas screen format (schema 1)

How to format information so the device can display it. Two kinds of JSON:

1. **Screen** — a layout. Sent rarely; stored on the SD card.
2. **Data** — the numbers/tables that fill the layout. Sent often; merged into the screen.

A screen says *where* things go and *which data key* each one shows. Data says *what the
values are*. Change a value → send data. Change the design → send the screen again.

Machine-readable definition: [`schema/screen.schema.json`](../schema/screen.schema.json)
(JSON Schema 2020-12). Validate before uploading.

## Display

| | |
|---|---|
| Resolution | **320 × 240**, landscape, origin top-left, pixels |
| Touch | resistive; tap a menu entry to open, **✕ (top-right, 44×34)** or **long-press** or **BOOT button** to go back |
| Reserved areas | keep the top-right 44×34 px clear (the ✕ back button floats there). Unacknowledged **critical** cards also get a red ACKNOWLEDGE button over the bottom-right 150×40 px |
| Colours | `"#rrggbb"` hex strings only |
| Fonts | sizes `12`, `16`, `24`, `32` (Montserrat); default 16 |

## 1. Screen file

```json
{
  "schema": 1,
  "id": "host-health",
  "title": "Host health",
  "menu": "Hosts",
  "bg": "#000000",
  "widgets": [ ... ],
  "thresholds": { "cpu": [[80, "#ffc400"], [95, "#ff4d4d"]] }
}
```

| Field | Required | Notes |
|---|---|---|
| `schema` | yes | must be `1` |
| `id` | yes | unique **path**, 1-4 segments joined by `/` (`hosts/milo/health`); each segment `[a-z0-9][a-z0-9_-]{0,31}`, whole id ≤ 64 chars. Must equal the id in the URL |
| `title` | no | menu text, ≤ 32 chars; defaults to the last segment of the id |
| `menu` | no | group heading in the menu (≤ 24 chars); defaults to the id's parent path (`hosts/milo`), so path ids group themselves |
| `priority` | no | `low` / `normal` (default) / `high` / `critical`, see below |
| `dwell` | no | seconds (2-120) the card stays up in the slideshow; default is the slideshow setting |
| `expires` | no | UTC time after which the device **deletes** the card, `2026-10-20T18:00:00Z`. No `expires` = keep forever |
| `data` | no | readings stored with the card, same shape as a data message (section 2) |
| `bg` | no | background colour, default black |
| `widgets` | yes | up to 64, drawn in order |
| `thresholds` | no | recolour a value by its reading, see below |

File size limit: **16 KB**.

### Priority

| Value | Menu | Behaviour |
|---|---|---|
| `low` | normal group, greyed | none |
| `normal` | normal group | none |
| `high` | **Alerts** group at the top, amber | none |
| `critical` | **Alerts** group at the top, red with a warning icon | **takes over the display** the moment it is uploaded, forces the backlight to 100 %, blinks the red LED |

A critical card stays on screen until it is **acknowledged**: tap the red **ACKNOWLEDGE** button on the card (or the ✕, BOOT, or a long-press),
or `POST /api/ack/<id>` (`cyd-canvas ack [id]`) from a host. A remote `show`/`menu` request cannot
dismiss it. If several are unacknowledged, the newest shows first and the rest follow as each is
acknowledged. Re-uploading a card (same id) raises the alert again; deleting or expiring it clears it.
After a reboot unacknowledged critical cards show again. Live data updates do not re-raise an alert.

### Slideshow

When enabled the display waits for `idle` seconds without a touch, then cycles through the cards in menu
order, `dwell` seconds each (a card's own `dwell` wins). `low` cards are skipped; `high` and `critical`
cards get twice the screen time. Any touch ends it and returns to the menu. Critical alerts still
interrupt it. Settings: `PUT /api/config {"slideshow":{"enabled":true,"idle":30,"dwell":8,"prefix":"hosts/"}}`
(`prefix` limits it to ids starting with it), `cyd-canvas slideshow on --idle 30 --dwell 8`, the menu
entry on the device, or the Home Assistant switch.

### Housekeeping (the device cleans up after itself)

The card is small and the menu has to stay usable, so the device deletes cards when it has **more than
`max_cards` (default 48)** or **less than `min_free_mb` (default 32 MB)** of free SD space. It deletes
**lowest priority first, then least recently used** (last upload, live data or viewing). It never deletes an
unacknowledged critical card, the card on screen, or the card just uploaded. It also removes data snapshots
of deleted cards and images no card uses (after 10 minutes). Check `evicted` in `GET /api/status`; change the
limits with `cyd-canvas limits --max-cards 60`. Give cards you want to keep a higher `priority`.

### Sources: the display fetches its own data

A card can say where its values come from, so nothing has to push them. The display subscribes to the MQTT
topics for you and updates the card as messages arrive (see [design/info-hub.md](design/info-hub.md)).

```json
"sources": [
  { "type": "mqtt", "topic": "lab/web/health", "path": "status", "key": "web_ok",
    "map": { "ok": 1, "degraded": 0, "down": 0 }, "max_age": 300 },
  { "type": "mqtt", "topic": "lab/room/temperature", "path": "celsius", "key": "temp",
    "scale": 1.8, "offset": 32, "max_age": 600 },
  { "type": "mqtt", "topic": "lab/service/+/state", "path": "state", "table": "services",
    "row": { "name": "{2}", "state": "$" }, "max_age": 300 }
]
```

| Field | Meaning |
|---|---|
| `type` | `mqtt` (`http` polling is planned) |
| `topic` | topic filter; `+` and `#` allowed |
| `path` | where the value is in a JSON payload: `a.b[0].c`. Leave out for a payload that is just a number or text |
| `key` | data key to set. `{n}` is the n-th level of the actual topic (0-based) |
| `map` | string to number, so `"degraded"` can drive an LED, a bar or a colour threshold |
| `scale`, `offset` | `value * scale + offset` (not applied to mapped values) |
| `table` + `row` | fill a table instead: one row per matching topic; each column is an expression (`{n}` = topic level, `$` = the value). Up to 12 rows |
| `max_age` | seconds. After that the value is **stale**: a number shows `--`, bars, arcs and LEDs go grey, table rows are dropped. Stale data must look stale. After a reboot, restored values count as stale until a message arrives |

Limits: 16 sources per card, 24 distinct topics on the display, payloads up to about 2 KB, JSON paths up to five levels.
Read what the display currently holds (and how old each value is) with `GET /api/data/<id>` or
`cyd-canvas`'s `get_data`.

### Identity, overwrite and expiry

- **The id is the identity.** Uploading a card whose id already exists **replaces** it; there is
  never more than one card per id (last write wins). Put the owner and purpose in the path:
  `hosts/milo/health`, `backups/nightly`, `alerts/disk-full`.
- **Expiry deletes.** When the device's clock passes `expires` it removes the card and its saved
  data and, if the card is open, returns to the menu. Sweeps run about every 20 s.
- **The clock comes from NTP over Wi-Fi.** Until it has synced (`time_valid` in `/api/status`)
  nothing expires, so a reboot without network never wipes cards. A card whose `expires` is
  already in the past is refused (HTTP 400) once the clock is valid. Re-uploading a card with a
  later `expires` extends it.
- Send relative lifetimes through the client (`--ttl 6h`); the device only understands absolute times.
- A fresh `PUT` that includes `data` replaces all data stored for that id.

### Common widget fields

Every widget has `type`, `x`, `y` (pixels from the top-left). Most take `w`, `h` and `color`.

### Widget types

| `type` | Shows | Fields |
|---|---|---|
| `label` | fixed text | `text` (≤128), `font` |
| `value` | a number from data | `key`, `fmt` (printf, one numeric conversion, default `%.1f`), `unit`, `font` (default 24) |
| `bar` | horizontal progress | `key`, `min`, `max`, `w`, `h` |
| `arc` | circular gauge with the number in the middle | `key`, `min`, `max`, `w`, `h` |
| `chart` | line or bar history | `w`, `h`, `points` (2–240, default 60), `min`, `max`, `style` (`line`/`bar`), `series[]` of `{key, color}` (1–4) |
| `table` | rows of text | `key`, `cols[]` (1–4 column names), `rows` (1–12, default 6), `w` |
| `image` | picture from the SD card | `src` (name of an uploaded asset, `*.bin`) |
| `led` | on/off dot | `key`, `color` (lit colour) |

`min`/`max` default to 0/100. A value outside the range is clamped by the widget.

### Thresholds

Per data key, an ascending list of `[value, colour]`. The **last pair whose value ≤ the
reading wins**; below the first pair the widget's own colour is used. Applies to `value`
text and `bar`/`arc` indicators.

```json
"thresholds": { "disk": [[70, "#ffc400"], [90, "#ff4d4d"]] }
```

## 2. Data

A flat JSON object. Keys must match the `key` in the widgets. It reaches the card in two ways:
**embedded** in the card's `"data"` field (a self-contained card: shows its content as soon as
it is opened, survives reboots, needs no feeder), or **live** via `POST /api/data/<id>` / MQTT.
Live readings layer on top of embedded ones. After a reboot embedded values only fill keys that
have no saved reading.

```json
{ "cpu": 42.5, "mem": 61, "disk": 63, "up": true,
  "services": [ {"name":"caddy","state":"ok"}, {"name":"pg","state":"ok"} ] }
```

| JSON value | Used by |
|---|---|
| number | `value`, `bar`, `arc`, `chart` (each send appends one point), `led` (> 0 = on) |
| `true` / `false` | stored as 1 / 0 |
| array of numbers | `chart` history (replaces the stored series, oldest first, last 240 kept); other widgets show the last number |
| array of objects (keyed by `cols`) or array of arrays | `table` |

Rules:

- Keys: letters, digits, `_ . -`, ≤ 32 chars. Body ≤ 8 KB.
- **Partial updates are fine.** Send only what changed; other values keep their last reading.
- Data only goes to an **existing** screen id — sending to an unknown id is rejected (HTTP 404).
- Chart history lives in RAM (last 240 points per series); after a reboot it restarts from the
  card's embedded history, if any. The latest value of every key survives reboots (saved to the
  SD card about once a minute).
- Send at a sensible rate: once every 5–60 s is typical. The display redraws at once.

## 3. Images

Upload as an asset, then reference the filename in an `image` widget.

- Names: letters, digits, `- _ .`, must end `.bin`, ≤ 32 chars. Max **512 KB**.
- Format: LVGL binary image, RGB565 (optionally with alpha). Use the client — it converts
  from PNG/JPEG: `cyd-canvas asset put logo.png` (needs Pillow).
- Size them to the pixels you want; there is no on-device scaling.

## 4. Delivery

| Need | How |
|---|---|
| Upload/replace a screen, upload an image | HTTPS `PUT /api/screens/<id>`, `PUT /api/assets/<name>` |
| Send data | HTTPS `POST /api/data/<id>` **or** MQTT `<prefix>/data/<id>` (ids contain slashes: `cyd/<name>/data/hosts/milo/health`) |
| Switch screen / brightness | `POST /api/show/<id>`, `/api/menu`, `/api/brightness` **or** MQTT `<prefix>/cmd` |

HTTPS needs the header `X-Api-Key: <key>`. The key and the certificate pin are shown on the
device (Device info). MQTT default prefix: `cyd/<device-name>`; command payloads are plain
text: `show <id>`, `menu`, `brightness <0-100>`. Full HTTP definition: `docs/openapi.yaml`.

## 5. Design tips for a 320×240 screen

- One idea per screen: a headline number, a gauge, or a chart — not all three.
- Use `font` 32 for the number people read from across a room; 12 for labels.
- Put a status colour on the number with `thresholds` instead of writing "OK"/"FAIL".
- A chart needs `min`/`max` set to the real range, or it will look flat.
- Test with the example in `examples/`, then `cyd-canvas validate yourscreen.json`.

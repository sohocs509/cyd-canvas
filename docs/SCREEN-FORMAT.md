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
| Reserved area | keep the top-right 44×34 px clear — the ✕ button floats over it |
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
| `id` | yes | `^[a-z0-9][a-z0-9_-]{0,31}$`; must equal the id in the URL/filename |
| `title` | no | menu text, ≤ 32 chars; defaults to `id` |
| `menu` | no | group heading in the menu (≤ 24 chars); screens without one list first |
| `bg` | no | background colour, default black |
| `widgets` | yes | up to 64, drawn in order |
| `thresholds` | no | recolour a value by its reading, see below |

File size limit: **16 KB**.

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

A flat JSON object. Keys must match the `key` in the widgets.

```json
{ "cpu": 42.5, "mem": 61, "disk": 63, "up": true,
  "services": [ {"name":"caddy","state":"ok"}, {"name":"pg","state":"ok"} ] }
```

| JSON value | Used by |
|---|---|
| number | `value`, `bar`, `arc`, `chart` (each send appends one point), `led` (> 0 = on) |
| `true` / `false` | stored as 1 / 0 |
| array of objects (keyed by `cols`) or array of arrays | `table` |

Rules:

- Keys: letters, digits, `_ . -`, ≤ 32 chars. Body ≤ 8 KB.
- **Partial updates are fine.** Send only what changed; other values keep their last reading.
- Data only goes to an **existing** screen id — sending to an unknown id is rejected (HTTP 404).
- Chart history lives in RAM (last 240 points per series) and starts empty after a reboot;
  the latest value of every key survives reboots (saved to the SD card about once a minute).
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
| Send data | HTTPS `POST /api/data/<id>` **or** MQTT `<prefix>/data/<id>` |
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

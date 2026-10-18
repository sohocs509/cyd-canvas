# Design: the display as an information hub

Status: **phase 1 (display-side MQTT sources) is implemented and verified on hardware**; the rest is proposal. Written 2026-10-18.

## 1. The idea

Until now the display was *driven*: something pushes a card and some numbers at it. The goal here is the other
way round: the display **gets information and shows it**. A person glances at it for the state of their
home, lab, day and weather without opening anything.

Two kinds of source, because the display is a small device and some information is awkward for it:

| Where the fetching happens | Good for | Why |
|---|---|---|
| **On the display** (a card lists its sources) | MQTT topics you already have, small JSON endpoints on the LAN | no extra service, almost no cost: MQTT is one persistent connection |
| **In a hub service** on a LAN host (Raspberry Pi, server, Home Assistant box) | internet APIs (weather, calendar, news), anything large, anything needing credentials or logic | the display cannot afford TLS to the internet, big payloads, or secrets, and the hub can combine sources |

Both feed the same cards, so a card does not care where its numbers came from.

## 2. Display-side sources

A card gains an optional `sources` array. Each entry says where one value (or one table) comes from.

```json
{
  "schema": 1, "id": "lab/status", "title": "Lab status",
  "widgets": [
    { "type": "led",   "x": 10, "y": 14, "key": "web_ok" },
    { "type": "value", "x": 40, "y": 10, "key": "load", "fmt": "%.1f" },
    { "type": "table", "x": 8, "y": 60, "w": 300, "key": "workers", "cols": ["name", "state"], "rows": 6 }
  ],
  "sources": [
    { "type": "mqtt", "topic": "lab/web/health", "path": "status", "key": "web_ok",
      "map": { "ok": 1, "degraded": 0, "down": 0 }, "max_age": 300 },
    { "type": "mqtt", "topic": "lab/node/load",  "path": "load1",  "key": "load", "scale": 1.0, "max_age": 120 },
    { "type": "mqtt", "topic": "lab/worker/+/status", "path": "state",
      "table": "workers", "row": { "name": "{2}", "state": "$" }, "max_age": 600 }
  ]
}
```

### Fields

| Field | Meaning |
|---|---|
| `type` | `mqtt` (phase 1), `http` (phase 2) |
| `topic` | MQTT topic filter; `+` and `#` allowed. The display subscribes on the card's behalf |
| `path` | where the value is in the JSON payload: `a.b[0].c`. Omit (or `$`) to use the whole payload as a plain number or string |
| `key` | the data key to set (scalar sources). `{n}` is replaced by the n-th level of the actual topic (0-based), so one entry can serve many topics |
| `map` | optional string-to-number map, to turn `"degraded"` into something an LED, bar or colour threshold can use |
| `scale`, `offset` | `value * scale + offset` (units, such as Celsius to Fahrenheit) |
| `table`, `row` | build a table: one row per distinct matching topic, `row` gives the cell values (`{n}` = topic level, `$` = the extracted value). At most 12 rows |
| `max_age` | seconds. A value not refreshed in this time is **stale**: a `value` shows `--`, bars, arcs and LEDs go grey. Stale information must look stale |
| `every`, `url` | for `http` sources: poll interval (seconds, at least 10) and the address |

Limits, because of the device's memory: payloads up to about 2 KB (the MQTT buffer is 3 KB), at most 16 sources per
card and 24 distinct subscriptions on the display, JSON paths up to 5 levels.

### Behaviour

- Subscriptions are rebuilt whenever the set of cards changes, and on every MQTT reconnect.
- A message updates the card's data exactly as if it had been posted to `data/<card id>`, so charts collect
  points, tables fill, and thresholds recolour.
- After a reboot, restored values count as stale until a message arrives (the display does not know how old they are).
- A card with sources is useful even while not on screen: data keeps arriving, so the card is current the moment
  it opens, and in the slideshow.
- `http` sources run one at a time, only while no HTTPS client is being served, with a hard timeout, and never more often
  than the interval. They are for plain `http://` JSON on the local network; internet HTTPS belongs to the hub.

## 3. The hub

A small Python program, `cyd-hub`, run on any Linux host. It reads a TOML file (no third-party dependencies
needed), fetches from its sources on schedules, builds cards from templates, and pushes cards and data to one or
more displays with the existing Python client (cards over HTTPS, readings over MQTT).

```toml
[display.hall]
profile = "hall"                  # a pairing profile made with `cyd-canvas pair`

[[card]]
id = "info/weather"
template = "weather"              # or: file = "cards/weather.json"
display = ["hall"]
every = 600                       # seconds between refreshes
ttl = "2h"                        # the display deletes the card if the hub stops updating it
  [card.source]
  type = "open_meteo"
  latitude = 0.0
  longitude = 0.0

[[alert]]
id = "alerts/pollen"
when = "pollen.ragweed >= 1"      # evaluated against the card's data
priority = "critical"
text = "Ragweed pollen is up: {pollen.ragweed}"
ttl = "1d"
```

### Plugins (each is a function that returns a dictionary of readings)

| Source | Notes |
|---|---|
| `http_json` | any JSON URL, with the same `path` syntax as display sources |
| `command` | run a script, read JSON from its output (the escape hatch for anything bespoke) |
| `mqtt` | collect and summarise topics (for example "n of m services healthy") |
| `homeassistant` | entity states over the REST API (long-lived token, kept outside the repo) |
| `open_meteo` | weather and a short trend; no key needed |
| `ical` | next events from an iCalendar URL |
| `prometheus` | instant queries |

User-specific sources (a personal workspace API, a task board, a pollen service) are written as small plugins
or as `command` sources, and live in the user's own configuration, **not** in this repository.

### Rules the hub follows

- **Every card it pushes has a `ttl`.** If the hub or a source dies, the display empties itself rather than lying.
- **Alerts are explicit**: a rule raises a `critical` or `high` card and clears it (acknowledge, delete) when the
  condition ends. Re-sending an unchanged alert does not re-raise it.
- **Be gentle with the display**: one HTTPS connection, serialised, backoff on failure, data over MQTT, changes only
  (a reading that did not change is not re-sent), and a slow source never blocks the others.
- **Secrets come from the environment or a file with mode 600**, never from the config file in the repository.

## 4. Phases

| Phase | Scope | Exit |
|---|---|---|
| 1 | Display-side `mqtt` sources, mapping, tables, staleness | A card reading three real topics updates live and goes grey when they stop |
| 2 | Display-side `http` sources (LAN) | A card polls a LAN JSON endpoint without disturbing HTTPS clients |
| 3 | Hub core: scheduler, templates, push, ttl, alerts; `http_json`, `command`, `mqtt` | A hub-driven card appears, updates and expires |
| 4 | Hub plugins: weather, calendar, Home Assistant, Prometheus | One card per plugin on a real display |
| 5 | Tuned slideshow, a documented example deployment, packaging (`pip install cyd-hub`) | Another person can follow the docs to a working hub |

## 5. Risks

| Risk | Mitigation |
|---|---|
| A card shows old data as if it were current | Mandatory-in-practice `max_age`; stale values are visibly stale; hub cards carry `ttl` |
| Display memory (MQTT buffer, subscriptions, rows) | Hard limits listed above; oversize payloads are dropped and counted, not buffered |
| A noisy topic floods the display | Updates are coalesced: a card is redrawn at most a few times a second |
| Credentials for internet APIs | They only ever exist on the hub host |
| The hub becomes a single point of failure | Cards expire, display-side sources keep working without it |

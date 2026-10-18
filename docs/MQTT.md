# MQTT reference

Configure the broker once (HTTPS API, or `cyd-canvas mqtt-config`):

```sh
cyd-canvas mqtt-config --host 192.168.1.10 --user cyd --password '…'      # add --tls --ca ca.pem for TLS
```

Fields: `host`, `port` (1883), `user`, `password`, `tls`, `ca` (PEM, required when `tls` is true), `prefix`
(default `cyd/<device name>`), `discovery_prefix` (default `homeassistant`). The password is never returned by
`GET /api/config`.

All topics below use `<prefix>`, for example `cyd/cyd-ab0694`.

## Topics

| Topic | Direction | Payload |
|---|---|---|
| `<prefix>/data/<card id>` | to display | JSON object of readings, merged into the card. The card id contains slashes: `<prefix>/data/hosts/milo/health` |
| `<prefix>/cmd` | to display | plain text, see below |
| `<prefix>/state` | from display | JSON, every 30 s and immediately when something changes (not retained) |
| `<prefix>/status` | from display | `online` (retained) / `offline` (last will) |
| `<discovery_prefix>/<component>/<name>/<object>/config` | from display | Home Assistant discovery, retained |

### Commands (`<prefix>/cmd`)

| Payload | Effect |
|---|---|
| `menu` | go to the menu (cannot dismiss an unacknowledged critical card) |
| `show <card id>` | open a card |
| `brightness <0-100>` | backlight |
| `slideshow on` / `slideshow off` | idle slideshow |

### State (`<prefix>/state`)

```json
{"screen": "", "brightness": 100, "slideshow": "OFF", "ldr": 0, "rssi": -31, "heap": 60616}
```

`screen` is empty for the menu.

### Sending readings

```sh
mosquitto_pub -h broker -t cyd/cyd-ab0694/data/hosts/milo/health -m '{"cpu": 42, "mem": 61}'
```

or with the Python helper (standard library only, no dependencies, suitable for cron jobs):

```python
from cyd_canvas import mqtt_publish
mqtt_publish("broker", "cyd/cyd-ab0694/data/hosts/milo/health", {"cpu": 42, "mem": 61})
```

A card can also **subscribe by itself** to any topic with `sources`: see [SCREEN-FORMAT.md](SCREEN-FORMAT.md#sources-the-display-fetches-its-own-data).

Arrays of numbers set chart history; arrays of objects fill tables. See [SCREEN-FORMAT.md](SCREEN-FORMAT.md).
Cards themselves (layouts) are uploaded over HTTPS, not MQTT.

## Home Assistant entities created by discovery

| Entity | Component | What it does |
|---|---|---|
| Screen | `select` | options `menu` plus every card id; choosing one sends `show <id>` |
| Brightness | `number` (0-100, step 5) | sends `brightness <n>` |
| Slideshow | `switch` | `slideshow on` / `off`; state from the `slideshow` field |
| Light level | `sensor` | raw reading of the light sensor |
| Wi-Fi signal | `sensor` (dBm, diagnostic) | RSSI |
| Back to menu | `button` | sends `menu` |

All entities belong to one device (`<name>`, manufacturer `cyd-canvas`, model `ESP32-2432S028R`) and become
unavailable when `<prefix>/status` says `offline`. Discovery is republished whenever the card list changes
(debounced by 4 seconds), because the Screen selector lists the cards.

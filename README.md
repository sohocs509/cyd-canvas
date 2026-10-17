# cyd-canvas

Turn a **$15 ESP32 "Cheap Yellow Display"** into a networked status screen. Send it a little JSON from any host
and it draws a dashboard: numbers, gauges, bars, charts, tables, status lights and images. Layouts ("cards") and
images live on the display's microSD card, so they survive reboots and change over the network without reflashing.

- **Cards as JSON**, described by a [JSON Schema](schema/screen.schema.json) and a plain-language
  [format guide](docs/SCREEN-FORMAT.md).
- **Live data over HTTPS or MQTT.** A card defines *where* things go; data says *what the values are*.
- **Alerts that cannot be missed**: a `critical` card takes over the screen, lights the backlight and blinks the LED
  until someone taps **ACKNOWLEDGE**.
- **Self-maintaining**: cards can expire, an idle **slideshow** cycles them, and when space runs low the display
  deletes the lowest-priority, least-recently-used cards.
- **Home Assistant** discovers it over MQTT (selector, brightness, slideshow switch, sensors).
- **Wi-Fi setup over Bluetooth** ([Improv Wi-Fi](https://www.improv-wifi.com/ble/)) from a phone or laptop; nothing
  is baked into the firmware.
- **Secure by default**: HTTPS with a pinned self-signed certificate, an API key on every call, no default credentials.
- Clients in **Python** (standard library only) and **bash**.

> **Status: pre-alpha.** Everything above works on the one board it was developed on (ESP32-2432S028R). See
> [docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md) for what is open, including what is **not yet verified**. Power it from
> a proper 5 V / 1 A adapter ([why](docs/HARDWARE.md#power)).

## Quick start

```sh
git clone https://github.com/sohocs509/cyd-canvas && cd cyd-canvas
pio run -t upload                         # flash (PlatformIO)
# set Wi-Fi from web/setup.html in Chrome (see docs/WIFI-SETUP.md), then:
pip install ./clients/python
cyd-canvas pair <display-address> --pin 'sha256//…'    # key and pin are shown on the display
cyd-canvas put examples/host-health.json --ttl 1d
cyd-canvas data hosts/example/health cpu=42 mem=61 disk=63
```

Full walk-through: [docs/GETTING-STARTED.md](docs/GETTING-STARTED.md).

## A card in one look

```json
{ "schema": 1, "id": "hosts/milo/health", "title": "milo", "priority": "normal", "expires": "2026-10-20T18:00:00Z",
  "widgets": [
    { "type": "value", "x": 8,  "y": 26, "key": "cpu", "fmt": "%.0f", "unit": "%", "font": 32 },
    { "type": "arc",   "x": 230, "y": 8,  "w": 80, "h": 80, "key": "disk" },
    { "type": "chart", "x": 8,   "y": 100, "w": 304, "h": 120, "points": 60, "min": 0, "max": 100,
      "series": [ { "key": "cpu", "color": "#3fdc6c" } ] } ],
  "thresholds": { "cpu": [[80, "#ffc400"], [95, "#ff4d4d"]] },
  "data": { "cpu": 42, "disk": 63 } }
```

## Documentation

Start at the [documentation index](docs/README.md): getting started, Wi-Fi setup, card format, HTTPS API
([OpenAPI](docs/openapi.yaml)), [MQTT](docs/MQTT.md), [Home Assistant](docs/HOME-ASSISTANT.md),
[hardware](docs/HARDWARE.md), [troubleshooting](docs/TROUBLESHOOTING.md), [architecture](docs/ARCHITECTURE.md),
[development](docs/DEVELOPMENT.md). A proposed [Home Assistant integration](docs/design/home-assistant-integration.md)
is designed but not built.

## Contributing and licence

See [CONTRIBUTING.md](CONTRIBUTING.md), [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) and [SECURITY.md](SECURITY.md).
Licensed under **Apache-2.0** ([LICENSE](LICENSE)). The firmware links third-party code under other licences, including the
LGPL-2.1 Arduino core: see [THIRD-PARTY.md](THIRD-PARTY.md).

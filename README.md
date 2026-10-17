# cyd-canvas

Push JSON to a tiny ESP32 touchscreen and have it render dashboards.
Screens and assets live on the microSD card, so layouts survive reboots and
you can change them over the network without reflashing.

Target hardware: **ESP32-2432S028R** ("Cheap Yellow Display", 2.8" ILI9341 320x240,
XPT2046 touch, microSD). Other boards are welcome as build profiles.

> Status: **pre-alpha**. Rendering, storage, the HTTPS/MQTT API, clients and the test suite work on real
> hardware. **Known issue:** under sustained load (minutes of back-to-back requests) the Wi-Fi link of
> the first test unit sometimes stops moving packets while still associated; see
> [docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md).

## How it works

1. Flash the firmware, then set up Wi-Fi over Bluetooth ([Improv Wi-Fi](https://www.improv-wifi.com/ble/), see
   [docs/WIFI-SETUP.md](docs/WIFI-SETUP.md)). Nothing is baked into the firmware: no Wi-Fi credentials, broker or API key.
2. Upload **screens** (JSON, see [`schema/screen.schema.json`](schema/screen.schema.json))
   and **assets** (images) over HTTPS with an API key.
3. Send **data** (`{"cpu": 42, "mem": 61}`) over HTTPS or MQTT. Widgets bind to data keys:
   values, bars, gauges, line/bar charts, tables, status LEDs, images.
4. Tap through the on-device menu, or switch screens remotely.

Home Assistant users get the device via MQTT discovery (current screen, brightness,
light sensor, touch events), so automations can drive it with plain `mqtt.publish`.

## Layout

| Path | What |
|---|---|
| `src/` | Firmware (PlatformIO, Arduino, LVGL 9) |
| `schema/` | JSON Schema for screens |
| `docs/` | HTTP API (OpenAPI), MQTT topics, hardware notes |
| `clients/python`, `clients/bash` | Client library and CLI |
| `examples/` | Sample screens |
| `boards/` | Partition tables and per-board notes |

## Power

Use a proper **5 V / 1 A (or better) wall adapter** with a short cable. A weak USB port, an unpowered hub or a
thin cable makes the Wi-Fi radio stop passing packets under load (the board stays associated but goes deaf).
Powering from a PC port is fine for flashing; use an adapter for running.

## Verified hardware map (ESP32-2432S028R)

| Function | GPIO |
|---|---|
| Display (HSPI) MISO/MOSI/SCLK/CS/DC | 12 / 13 / 14 / 15 / 2 (RST tied high) |
| Backlight | 21 |
| Touch XPT2046 (own SPI) MISO/MOSI/SCLK/CS/IRQ | 39 / 32 / 25 / 33 / 36 |
| microSD (VSPI) MISO/MOSI/SCLK/CS | 19 / 23 / 18 / 5 |
| RGB LED (active low) R/G/B | 4 / 16 / 17 |
| Light sensor (ADC) | 34 |
| Speaker amp | 26 |

Display, touch, and SD (SDHC, read/write/verify) were tested on real hardware.
Some CYD revisions use different display drivers or need colour inversion; please
open an issue with your board's markings.

## Security model

- HTTPS only, with a per-device self-signed certificate generated on first boot
  (you can upload your own). Every route requires an `X-Api-Key` header.
- The API key is generated on first boot and shown once on the screen.
- MQTT supports username/password and TLS. No default credentials anywhere.
- Wi-Fi provisioning requires a physical touch to authorize.

## License

Apache-2.0. See [LICENSE](LICENSE). Contributions are accepted under the same license
(see [CONTRIBUTING.md](CONTRIBUTING.md)).

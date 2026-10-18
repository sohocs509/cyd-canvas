# Development guide

## Layout

| Path | What |
|---|---|
| `src/` | firmware: `hal` (display, touch, LED), `store` (SD), `ui` (LVGL renderer, menu, alerts, slideshow, housekeeping), `net` (Wi-Fi, HTTPS API, MQTT), `provision` (Bluetooth setup), `tls`/`tlsmem` (certificate, TLS memory pool), `console` (serial debug) |
| `include/` | `config.h` (pins), `lv_conf.h` (LVGL features: unused widgets are compiled out) |
| `schema/screen.schema.json` | the card format (JSON Schema 2020-12) |
| `docs/` | documentation; `docs/openapi.yaml` is the HTTPS API |
| `clients/python`, `clients/bash` | client library and CLIs |
| `web/setup.html` | Bluetooth Wi-Fi setup page |
| `tests/render/` | on-device render and API test suite |
| `clients/python/tests/` | unit tests (no device needed) |

## Build, flash, test

```sh
pio run                       # build (env cyd-2432s028r)
pio run -t upload             # flash over USB
pio run -e cyd-2432s028r-noble   # variant without Bluetooth setup

pip install -e clients/python pytest jsonschema pillow pyyaml openapi-spec-validator ruff jinja2
pytest clients/python/tests   # unit tests: pin, ttl, images, schema, OpenAPI, Home Assistant discovery
ruff check clients/python tests

cyd-canvas pair <host> --pin 'sha256//…'
python tests/render/run.py                 # API contract, ids, alerts, slideshow, housekeeping, every widget
python tests/render/run.py --interactive   # you judge each rendering against its "expect" line
python tests/render/run.py --slow          # adds the 2-minute expiry cycle
```

The render suite uploads `test/...` cards, steps through them, and (with `--cleanup`) removes them. Run it
against a board on a **wall adapter**; a weak USB port can wedge the Wi-Fi (see [HARDWARE.md](HARDWARE.md)).

CI (`.github/workflows/ci.yml`) builds the firmware, runs the unit tests and `ruff`, and runs `shellcheck`.

## Conventions

- Every source file starts with `SPDX-License-Identifier: Apache-2.0`. Sign off commits (`git commit -s`).
- Never commit credentials, Wi-Fi passwords, API keys or private keys. Test fixtures use throwaway values.
- Keep changes small. Anything memory-sensitive needs a before/after of the serial `info` and `heap` output.
- Changing the card format: update the schema, `SCREEN-FORMAT.md`, a render fixture and its manifest entry, and
  bump `schema` only for breaking changes. Old versions must keep loading.

## Adding a widget

1. Add its definition to `schema/screen.schema.json` and a section to `docs/SCREEN-FORMAT.md`.
2. Switch the LVGL widget on in `include/lv_conf.h` (most are off to save flash) and check the flash figure.
3. Build it in `buildScreen()` and update it in `updateBind()` (both in `src/ui.cpp`); add a `Bind` kind.
4. Add a fixture in `tests/render/screens/`, a case in `tests/render/manifest.json` with an `expect` line a person
   can check in two seconds, and run the suite on a board.

## Adding a board

See [HARDWARE.md](HARDWARE.md#board-variants).

## Releasing

1. Update `CHANGELOG.md` and `CYD_VERSION` in `src/net.h`.
2. CI green; run the render suite (and `--slow`) on a board; a 15-minute soak on a wall adapter.
3. Tag `vX.Y.Z`. The `release` workflow builds the merged image, attaches it with `SHA256SUMS` to the GitHub release and publishes the web flasher (`web/install.html`) to GitHub Pages (Pages source must be set to GitHub Actions). **Release notes must state that the firmware statically links the
   LGPL-2.1 Arduino core** and link to the corresponding source (this repository at that tag). See
   [THIRD-PARTY.md](../THIRD-PARTY.md).

## Lessons that cost time (so they do not cost it again)

- **Verify UI by looking at it.** A "back button" existed in the code paths that show and hide it, but was never
  created. API tests cannot see that.
- **Memory first.** `ESP.getFreeHeap()` over-reports; use the TLS pool and largest-block figures. Intermittent
  `mbedtls_ssl_setup returned -0x7F00` means an allocation failed.
- **Clear flags before the work they trigger.** The index-dirty flag was cleared after a rebuild and swallowed a
  concurrent upload.
- **No shared file handles** between an upload and a background save.
- **SD speed 4 MHz.** 20 MHz drops reads silently.
- **Power before firmware.** A weak USB port looked like a software bug for a day. The reset reason and a wall
  adapter separate the two in minutes.
- **One flasher per serial port.** A capture script holding `/dev/ttyUSB0` breaks `pio run -t upload`.
- Shell quoting damages C escapes (`"\x02"`) in patch scripts; use quoted heredocs.

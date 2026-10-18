# Changelog

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versioning: [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Repository scaffold, screen JSON Schema v1, verified ESP32-2432S028R pin map.
- Path-style card ids (`hosts/milo/health`); re-uploading an id replaces the older card.
- `expires` on cards: the device deletes them after the time passes (NTP clock; nothing expires before it syncs).
- Embedded `data` in cards, numeric arrays as chart history, `GET /api/index`.
- Slideshow mode (idle start, per-card `dwell`, touch stops it, critical interrupts), menu entry and Home Assistant switch.
- Self-maintenance: beyond `max_cards` / `min_free_mb` the device deletes cards lowest priority first, then least recently used; orphaned data and unused images are removed too.
- On-screen ✕ back button and ACKNOWLEDGE button for critical cards.
- Wi-Fi setup over Bluetooth (Improv Wi-Fi BLE) as a separate boot mode, with on-device ALLOW, menu/API entry and an automatic fallback; Web Bluetooth setup page (`web/setup.html`); `cyd-2432s028r-noble` build without Bluetooth.
- Documentation set (`docs/`), Home Assistant integration design, OpenAPI spec, unit tests and CI.
- Network picker in the Bluetooth setup page; paced Bluetooth notifications.
- Home Assistant: the MQTT state message carries the slideshow state; state is published immediately on change; discovery templates are covered by a recorded test.
- LVGL trimmed to the widgets in use (-90 KB flash).
- Browser flasher (`web/install.html`, ESP Web Tools) and a `release` workflow that builds the merged image and publishes it with the flasher on each `v*` tag.
- **Display-side data sources**: a card's `sources` array makes the display subscribe to MQTT topics itself (JSON path, string-to-number map, scale/offset, tables from wildcard topics, `max_age` staleness); `GET /api/data/<id>`.
- Removed an obsolete low-memory restart check that made the 0.2 preview reboot about once a minute.
- Render test suite (`tests/render`) including id, overwrite and expiry checks.

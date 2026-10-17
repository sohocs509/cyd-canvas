# Changelog

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versioning: [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Repository scaffold, screen JSON Schema v1, verified ESP32-2432S028R pin map.
- Path-style card ids (`hosts/milo/health`); re-uploading an id replaces the older card.
- `expires` on cards: the device deletes them after the time passes (NTP clock; nothing expires before it syncs).
- Embedded `data` in cards, numeric arrays as chart history, `GET /api/index`.
- Render test suite (`tests/render`) including id, overwrite and expiry checks.

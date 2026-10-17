# Design: a Home Assistant integration for cyd-canvas

Status: **proposal** (nothing here is built yet). Written 2026-10-17 after the first test of the display against
a real Home Assistant (see [HOME-ASSISTANT.md](../HOME-ASSISTANT.md)).

## 1. Problem

Today the display works with Home Assistant through **MQTT discovery**. That gives Home Assistant the *device
controls*: a screen selector, brightness, a slideshow switch, two sensors and a button. It does not give Home
Assistant any knowledge of the **cards**: which ids exist, what a valid card looks like, which data keys a card
reads. A user who wants "show my solar power on the display" has to hand-write JSON and `mqtt.publish` calls
with no validation and no discoverability.

## 2. Goals and non-goals

Goals

1. Send cards and data from automations with **validated, documented services**.
2. **Bind any Home Assistant entity to a card's data key** from the UI, so the display follows it with no automation.
3. Ready-made **card templates** and an **alert helper** (critical card + acknowledge) for the common cases.
4. Find displays automatically; survive the display being offline, rebooted or busy.

Non-goals (v1)

- A visual card designer (the JSON format is the interface; a designer can come later).
- Replacing MQTT discovery (it stays; the integration adds to it).
- Cloud features. Everything stays on the local network.

## 3. Architecture

A custom integration, domain **`cyd_canvas`**, in its **own repository** (`sohocs509/ha-cyd-canvas`) because HACS
needs `custom_components/<domain>/` at the repository root. It depends on the published `cyd-canvas` Python
package for the device client (HTTPS, key pinning, image conversion), so the protocol code is shared.

```
 Home Assistant                                   display (ESP32)
 +--------------------------+     HTTPS :443     +--------------------+
 | cyd_canvas integration   |  cards, config,    | one TLS client at  |
 |  config flow, services,  |  assets, ack       | a time, ~2 s       |
 |  bindings, templates     | -----------------> | handshake          |
 |                          |     MQTT           |                    |
 |  MQTT integration  ------+-- data, cmd ------>| cheap, persistent  |
 +--------------------------+  <---- state ------+--------------------+
```

Why two transports: the display serves **one HTTPS client at a time** and a TLS handshake costs about two seconds
of CPU, so HTTPS is right for rare, large operations (upload a card, change settings) and wrong for a value that
changes every few seconds. MQTT is a single long-lived connection and costs almost nothing per message, so live
data and commands go through it.

Rules that follow from the device's limits:

- The integration keeps **one** HTTPS connection per display, **serialises** calls, retries with backoff on a
  refused connection, and never polls faster than once a minute.
- Binding updates are **rate-limited** (default one update per key per 5 s) and de-duplicated.
- Everything the integration needs to *read* arrives on the MQTT state message (see section 6), not by polling.

## 4. User-visible features

### 4.1 Setup

- **Discovery**: the display advertises an mDNS service `_cyd-canvas._tcp` with TXT records `name`, `version`,
  `schema`. Home Assistant offers it in Devices & services.
- **Config flow**: host (prefilled), API key (shown on the display under Device info), then a confirmation step that
  shows the certificate fingerprint to compare with the display. The key and pin are stored in the config entry.
- **Re-auth flow** when the key changes (for example after a factory reset).

### 4.2 Services

All services take a `device_id` (or entity target) and are validated with schemas generated from
[`schema/screen.schema.json`](../../schema/screen.schema.json).

| Service | Does |
|---|---|
| `cyd_canvas.send_card` | Upload a card (dict, or a template name + options); optional `ttl` or `expires`, `priority` |
| `cyd_canvas.send_data` | Merge readings into a card (arrays become chart history) |
| `cyd_canvas.show` | Show a card or the menu |
| `cyd_canvas.delete_card` | Remove a card |
| `cyd_canvas.alert` | Raise or update a critical (or high) card from a title and message; optional `ttl` |
| `cyd_canvas.acknowledge` | Acknowledge one critical card or all |
| `cyd_canvas.set_slideshow` / `set_limits` | The matching device settings |

### 4.3 Entity bindings (the main feature)

An **options flow** on the device entry: *Add binding* asks for

1. a source: any entity (state or an attribute), or a template,
2. the **card id** and **data key**,
3. optional: unit/format, minimum interval.

The integration then listens for state changes and sends `{key: value}` over MQTT. Numeric states become numbers;
`on`/`off` become 1/0; anything else is sent as a string. Bindings survive restarts and show up as diagnostics.

A binding to a key that no widget reads is harmless; the card editor note in the UI says which keys the chosen
card actually reads (taken from the card's JSON).

### 4.4 Card templates

A small library shipped with the integration, each with the keys it reads:

| Template | Layout | Keys |
|---|---|---|
| `gauge` | big number + arc | `value` |
| `trend` | number + line chart | `value` |
| `status_list` | table of name/state with an LED | `rows` |
| `weather` | temperature, condition, 3-hour trend | `temp`, `condition`, `history` |
| `alert` | red banner, title, message (critical) | none |

"Create card from template" in the UI uploads the card; the user then binds keys to entities.

### 4.5 Blueprints

- *Show a Home Assistant notification on the display* (persistent notification becomes a high-priority card).
- *Critical alert from a binary sensor* (card raised while the sensor is on, acknowledged and removed when it clears).

## 5. Device-side prerequisites (this repository)

Small, none of them risky for flash or RAM:

1. mDNS service record `_cyd-canvas._tcp` with the TXT records above (a few hundred bytes of flash).
2. A richer MQTT state message: add `alerts`, `cards`, `sd_free_mb`, `evicted`, `reset_reason`, `version` so the
   integration needs no HTTPS polling.
3. `POST /api/alert` (title, message, priority, ttl): builds the alert card on the device. It keeps the
   integration's alert helper to one request instead of a full card upload.
4. Optional later: accept a small card over MQTT (`<prefix>/card/put/<id>`, limited by the 3 KB MQTT buffer) so
   setups without direct HTTPS access can still change cards.

Flash headroom is 228 KB at the time of writing and these need well under 20 KB.

## 6. Quality bar

- Tests with `pytest-homeassistant-custom-component`; CI also runs `hassfest` and the HACS action.
- Diagnostics redact the API key. Strings in English first, translation files from the start.
- The config flow handles: display offline, wrong key, certificate mismatch, a second client already connected.
- Documentation page per service with a YAML example, linted in CI.

## 7. Phases

| Phase | Scope | Exit criterion |
|---|---|---|
| 0 | Device prerequisites (section 5) | `GET /api/status` and the MQTT state carry the new fields; mDNS visible from a Home Assistant host |
| 1 | Integration skeleton: config flow, `send_card`, `send_data`, `show`, `acknowledge`, `delete_card` | A card can be sent from Developer tools > Actions on a real Home Assistant |
| 2 | Entity bindings and the options flow | Changing a sensor updates the display within the rate limit; survives a restart |
| 3 | Templates, `alert`, blueprints | A binary sensor raises and clears a critical alert end to end |
| 4 | Publish: HACS default repository submission, brand assets, docs site page | Installable from HACS; CI green |

Phase 0 and 1 are about three days of work; the whole thing about two weeks.

## 8. Risks and open questions

| Risk | Mitigation |
|---|---|
| Single HTTPS client: Home Assistant and a person with the CLI collide | Retries with backoff already exist in the Python client; the integration holds its connection only while working and releases it after an idle timeout |
| A slow or sleeping display makes service calls hang | Timeouts and an "unavailable" state driven by the MQTT availability topic |
| MQTT broker is anonymous and plaintext by default | Documented as the main security gap; the integration reuses Home Assistant's own broker credentials and supports the display's TLS option |
| Entity-binding spam (a power sensor changing 10 times a second) | Per-binding minimum interval, change threshold, drop-if-busy |
| Schema drift between device firmware and the integration | `schema` version in the mDNS TXT record; the integration refuses cards newer than the firmware supports |

Open questions for the maintainer:

1. Separate repository (recommended) or a `custom_components` folder in this one?
2. Minimum Home Assistant version (suggest the current stable minus six months).
3. Should alerts also appear as Home Assistant persistent notifications, or only on the display?
4. Which three templates first? (Suggested: `gauge`, `trend`, `alert`.)

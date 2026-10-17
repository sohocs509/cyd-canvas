# Architecture

How the firmware is put together, and why. Read this before changing memory-sensitive code.

## The picture

```
  hosts / scripts / Home Assistant
        |  HTTPS :443 + X-Api-Key          |  MQTT (broker)
        v                                  v
 +--------------------- ESP32 (cyd-canvas firmware) ---------------------+
 |  net task (core 0)           ui / loop task (core 1)                   |
 |   Wi-Fi, mDNS, NTP            LVGL, touch, BOOT button, slideshow      |
 |   HTTPS server (1 client)     index, data, alerts, expiry, housekeeping|
 |   MQTT client + HA discovery            |                              |
 |        \__________ SD card (FAT) _______/                              |
 |   /screens/<id>.json   /assets/<name>.bin   /data/<id>.json            |
 +------------------------------------------------------------------------+
        320x240 ILI9341 + touch + RGB LED
```

Cards and assets live on the SD card, so layouts survive reboots and need no reflashing. Live readings are held
in RAM and snapshotted to the card about once a minute.

## Two boot modes

| Mode | When | Runs | Does not run |
|---|---|---|---|
| **Normal** | Wi-Fi saved | display, touch, SD, Wi-Fi, HTTPS, MQTT | Bluetooth (its memory is released at boot) |
| **Setup** | no Wi-Fi saved, menu entry, `POST /api/provision`, or saved network unreachable for 5 minutes | display, touch, Bluetooth (Improv Wi-Fi) | SD, web server, MQTT, TLS pool |

Setup is a separate mode so day-to-day running never carries the Bluetooth stack's RAM. It ends with a restart
into normal mode as soon as credentials are saved. See [WIFI-SETUP.md](WIFI-SETUP.md).

## Memory budget

The ESP32 has 520 KB of SRAM, but after the Wi-Fi driver and the system tasks only about **72 KB of ordinary heap**
is usable. That, not CPU, decides the design.

| Item | Cost | Why |
|---|---|---|
| TLS pool (private, reserved at boot) | 48 KB | A handshake needs two contiguous 16 KB buffers. On a fragmented 72 KB heap that failed intermittently, so the pool is carved out while the heap is pristine and handed to mbedTLS |
| One HTTPS client at a time | | Each TLS session costs about 36 KB of the pool; two at once starve the Wi-Fi driver |
| LVGL render strip | 5 KB | 320x8 pixels; small on purpose |
| SD mount | about 28 KB | FAT driver buffers |
| UI objects (menu, one card) | about 19 KB | rebuilt on every navigation |
| Bluetooth (normal build) | about 10 KB static | none at run time in normal mode |

Rules of thumb: never allocate a large `String` or `JsonDocument` in a hot path; stream JSON from the SD card
(`store::readJson`); keep `max_open_sockets` at 1; measure with the serial `heap` and `info` commands. The
`heap_free` number in `/api/status` over-reports (it includes memory TLS cannot use); watch `tls_pool_free` and
`heap_largest`.

### Flash

88% of the 1.9 MB application slot (the second slot is reserved for future over-the-air updates). By size:
Wi-Fi radio stack about 24%, LVGL 17%, Arduino core 17%, mbedTLS 10%, Bluetooth 9%. About 20 unused LVGL widgets
and one font are compiled out in `include/lv_conf.h`; new widgets must be switched on there.

## Storage

| Path | Content |
|---|---|
| `/screens/<id>.json` | one card; the id's `/` is stored as `~` so the directory stays flat (`hosts~milo~health.json`) |
| `/assets/<name>.bin` | LVGL binary images (RGB565, optionally with an alpha plane) |
| `/data/<id>.json` | last readings of a card, so values survive a reboot |

Writes are atomic (`<file>.tmp`, then rename) and use a per-writer file handle. Reads retry a few times, because a
transient SD error must not make a card vanish.

## What runs where

- **Core 1, loop task**: `ui::loop()` takes the UI lock, handles navigation, the BOOT button, alerts, slideshow,
  expiry and housekeeping every 20 seconds, then runs LVGL. The serial console is polled here too.
- **Core 0, net task**: Wi-Fi, NTP, mDNS, the MQTT client, the self-check, and starting the HTTPS server.
- **HTTPS server task**: one request at a time; handlers take the SD lock briefly per call and the UI lock only to
  apply data.
- **Locks and order**: UI lock first, then SD lock. Never take the UI lock while holding the SD lock.
  The "index needs rebuilding" flag is cleared *before* a rebuild, so an upload that arrives mid-rebuild is not lost.

## Behaviour that keeps an unattended display healthy

| Mechanism | Why |
|---|---|
| Card **expiry** (`expires`, needs NTP; nothing expires before the clock is set) | stale information never lingers |
| **Housekeeping**: beyond `max_cards` or below `min_free_mb`, delete lowest priority first, then least recently used; never the card on screen, the last upload, or an unacknowledged critical card | self-maintaining storage |
| **Priority**: `critical` takes over the screen, forces the backlight up, blinks the LED, and returns after reboot until acknowledged | alerts cannot be missed |
| **SD auto-remount** every 10 s while missing, and after three I/O errors | recovers without a reboot |
| **Network self-check**: DNS round trip every 15 s; reconnect after 3 failures, restart after 6 | a wedged radio heals itself |
| **Reset reason** in the boot log and `/api/status` | power and crash problems are visible |

## Security model

- **HTTPS only.** The device generates a P-256 key and a self-signed certificate on first boot. Trust is a
  **public-key pin** (`sha256//…`, shown on the display), verified by the clients on every connection. There is no CA.
- **API key** (32 hex characters, generated on first boot) required in `X-Api-Key` on every route, compared in
  constant time. Physical access (the Device info screen, or the USB serial `key` command) is the way to read it.
- **No default credentials**; Wi-Fi credentials never live in the firmware.
- **Wi-Fi setup needs a touch** on the display (ALLOW), valid for 60 seconds.
- **Inputs are validated**: ids, sizes (card 16 KB, data 8 KB, config 4 KB, asset 512 KB), expiry format, priority,
  and the `fmt` printf format of a value widget (only one floating-point conversion is accepted; anything else
  falls back to `%.1f`, so a hostile format string cannot corrupt memory).
- **MQTT** is plaintext by default. Set a username and password, and TLS with a CA PEM (the device refuses to fall
  back to unverified TLS). The broker should not be anonymous.
- The API is for a **trusted local network**. It is not meant to be exposed to the internet.

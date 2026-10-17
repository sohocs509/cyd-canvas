# Troubleshooting

Plug the board into a computer and open the serial console at **115200 baud** (for example
`python3 -m serial.tools.miniterm /dev/ttyUSB0 115200`). Type `help`.

| Symptom | Likely cause and fix |
|---|---|
| Screen is black after flashing | Backlight is on GPIO 21; check the board is an ESP32-2432S028R. Hold nothing at reset (BOOT at reset = bootloader) |
| Text missing but boxes show | Fonts not compiled in; the `platformio.ini` flags `LOAD_FONT2` and `LOAD_FONT4` must be present |
| Touch lands in the wrong place | Recalibrate: serial `cal`, or reflash after `pio run -t erase` |
| Display shows **"SD card not found"** | Reseat the card (push in until it clicks), power-cycle. It retries every 10 s by itself, so it recovers without a reboot. Check `sd_ok` in `/api/status` |
| Cards disappear from the menu but the files are on the card | SD read errors; make sure the board is a stock one and SD speed is 4 MHz. Check the serial log for `store:` lines |
| `No route to host` / pings fail, but the display runs | Power. Use a 5 V / 1 A wall adapter and a short cable ([Hardware](HARDWARE.md#power)). The log's `reset reason` shows brownouts |
| Wi-Fi setup: "Could not connect: check the network name and password" | The name is **case sensitive** (phone keyboards lowercase it). Pick it from the list rather than typing it |
| Wi-Fi setup page finds no device | The display must be in setup mode (menu, System, Wi-Fi setup). Chrome needs Bluetooth and location permission, and a secure page (`https://` or `http://localhost`) |
| Display left setup mode after a minute | Only when setup was triggered because the saved network was unreachable (it keeps retrying that network). A deliberate setup stays until you finish or type `exit` on the serial console |
| `cyd-canvas`: `certificate pin mismatch` | The display's certificate changed (factory reset or reflash with erased flash). Compare the pin on the display and run `cyd-canvas pair` again |
| `cyd-canvas`: `401` | Wrong API key; read it from Device info on the display |
| Requests are slow (about 2 s each) or occasionally refused | A TLS handshake costs about 2 s and the display serves **one client at a time**. Reuse one connection (the Python client does), and use MQTT for frequent data |
| A card uploaded but is not in the menu | Check `GET /api/index`. The id must be 1-4 lowercase segments (`a-z 0-9 _ -`) joined by `/` |
| A critical card keeps coming back | Unacknowledged critical cards reappear after a reboot by design. Tap ACKNOWLEDGE, or `cyd-canvas ack` |
| A card vanished | It expired (`expires`/`--ttl`), or housekeeping deleted it: lowest priority first, then least recently used, beyond `max_cards` (48) or under `min_free_mb` (32 MB). See `evicted` in `/api/status` and give important cards a higher `priority` |
| Home Assistant shows no entities | Home Assistant needs the MQTT integration pointed at the same broker; the device's discovery prefix must match (`homeassistant` by default). See [HOME-ASSISTANT.md](HOME-ASSISTANT.md) |

## Serial console commands

Normal mode: `help`, `info` (memory, SD, cards), `heap` (region dump and integrity check), `net` (address),
`nettest [host port]` (outbound connect from the device), `probe [ip]` (DNS round trip), `key` (API key and pin; USB
only), `wifi <ssid> <password>`, `ls`, `put <id> <json>`, `data <id> <json>`, `show <id>`, `menu`, `rm <id>`, `cal`,
`dbg <id>`.

Setup mode: `allow` (authorise without touching the screen), `scan`, `exit` (leave setup, keep the saved network),
`reboot`.

## Reading the boot log

```
reset reason: power-on | software restart | crash (panic) | watchdog | BROWNOUT (power dip)
heap: ... before BT release, ... after
tls pool: reserved 48 KB
SD: ok          (or: SD: missing, and later "store: SD card is back")
net: up 192.168.x.y    net: mqtt connected
```

If the network goes quiet the display runs a self-check every 15 seconds (a DNS round trip to the router) and
reconnects after three failures, restarting after six.

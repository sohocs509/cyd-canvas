# Known issues

Last reviewed 2026-10-17. Developed and tested on **one** ESP32-2432S028R.

## Open

| Issue | Detail |
|---|---|
| **Network list in the Wi-Fi setup page: delivery not re-verified** | The display scans correctly (8 networks found over USB). Delivering the list over Bluetooth was broken (results truncated) and was fixed by sending paced 20-byte chunks, but the fixed version has not yet been seen working on a phone or tablet. The page still works by typing the network name ("Other network") |
| **Home Assistant controls not driven by a person** | Discovery, entity states and command templates are verified (see [HOME-ASSISTANT.md](HOME-ASSISTANT.md)); clicking the entities in Home Assistant's UI has not been checked |
| **One SD-card dropout of unknown cause** | During a soak test on a PC USB port the board rebooted and the SD card stopped responding until it was reseated. The firmware now remounts by itself and logs the reset reason, so a recurrence will be explained |
| **One HTTPS client at a time, about 2 s per new connection** | A design limit of the ESP32's RAM (TLS needs about 36 KB per session). Clients reuse a connection and retry; use MQTT for frequent data |
| **MQTT is plaintext by default** | Configure a username and password, and TLS with a CA. An anonymous broker lets anyone on the network change the display |
| **Flash is 88% full, no over-the-air update yet** | About 228 KB headroom in the 1.9 MB slot. The second slot is reserved for OTA |
| **Improv over USB serial is not implemented** | Bluetooth only; the serial `wifi <ssid> <password>` console command is the fallback |
| **Only one board is verified** | Other CYD revisions may need different display or touch settings ([HARDWARE.md](HARDWARE.md#board-variants)) |
| **General heap is small** | After boot about 52 KB is free and the largest block about 24 KB (it dips lower during heavy churn). TLS is unaffected (private pool), but very large JSON documents may not fit. `GET /api/status` shows `heap_free` and `heap_largest` |
| **`http` sources are not implemented yet** | Only `mqtt` sources exist; see the [info hub design](design/info-hub.md) |
| **The light sensor can read 0** | Normal in room light on the test board; not treated as a fault |

## Resolved (kept for the record)

| Was | Cause and fix |
|---|---|
| Wi-Fi went deaf within minutes of load | **Power**: weak USB port and cable. A wall adapter ran 4,000 requests with no failure. Documented in [HARDWARE.md](HARDWARE.md#power); transmit power lowered; reset reason recorded |
| Intermittent TLS failures (`-0x7F00`) | Heap fragmentation; fixed with a private 48 KB TLS memory pool and one session at a time |
| Cards vanished from the menu | SD at 20 MHz dropped reads; now 4 MHz with retries and auto-remount |
| An upload could be truncated | A shared file handle between an upload and the background data save; now per-writer |
| A second upload in quick succession was not indexed | The dirty flag was cleared after a rebuild; now cleared before |
| Back button did not exist | It was never created in code; now created and verified on the display |
| Slideshow switch showed "unknown" in Home Assistant | The MQTT state message lacked the field; fixed and covered by a test |
| A hostile `fmt` string could corrupt memory | Only one floating-point conversion is accepted |

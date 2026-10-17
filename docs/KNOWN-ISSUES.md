# Known issues

## Wi-Fi stops passing packets under sustained load (open)

Seen on the first test unit (ESP32-2432S028R, USB-powered from a mini-PC port). After roughly one to
three minutes of continuous back-to-back requests the device stays associated with the access point
but stops moving packets: pings and new connections fail, and its own sockets log
`write(): fail ... errno: 11` (send buffer full). The main loop and display keep running.

What has been ruled out or fixed on the way:

| Finding | Fix |
|---|---|
| TLS handshake needs two contiguous ~17 KB buffers; the heap fragments | private 48 KB mbedTLS pool reserved at boot (`src/tlsmem.cpp`) |
| Two TLS sessions at once starve the Wi-Fi driver | `max_open_sockets = 1`; one connection at a time |
| Server-forced socket close filled lwIP's TIME_WAIT table | clients close first; keep-alive allowed |
| SD at 20 MHz dropped reads (cards vanished from the index) | 4 MHz plus read retries |
| One shared write handle let an autosave truncate an upload | per-writer file handle |
| Stalled uploads blocked the single server task forever | bounded body reads |

Not yet resolved: the radio/TCP-IP wedge itself. Suspects, in order: USB power quality (Wi-Fi TX
bursts plus backlight on a weak port or cable), then Wi-Fi driver buffer exhaustion. A self-check
(`net::probe`) detects a dead link and reconnects/restarts, but it has not yet been seen to recover
the unit. **To help:** run `python tests/render/run.py --slow` and report the board, power source and
whether a different supply changes the result.

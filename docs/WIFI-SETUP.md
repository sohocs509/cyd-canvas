# Wi-Fi setup over Bluetooth

A display has no keyboard, so Wi-Fi is set up from a phone, tablet or laptop over Bluetooth using the
[Improv Wi-Fi](https://www.improv-wifi.com/ble/) standard. Nothing is stored in the firmware.

## Using it

1. **Get the display into setup mode.** A new display (no Wi-Fi saved) starts there. Otherwise open the menu,
   **System → Wi-Fi setup (Bluetooth)**, or call `POST /api/provision`. It also enters setup by itself if the saved
   network has been unreachable for five minutes (then it keeps retrying the old network too).
2. **Open the setup page** ([`web/setup.html`](../web/setup.html)) in Chrome on Android, Windows, macOS or ChromeOS
   (Web Bluetooth needs Chrome or Edge, and a secure context: `https://` or `http://localhost`). Home Assistant's
   companion app also speaks Improv.
3. Press **Find my display** and choose it in the Bluetooth list.
4. **Tap ALLOW on the display.** Nothing can be sent until you do, and the permission lapses after 60 seconds,
   so someone nearby cannot push credentials to it.
5. Pick or type the network name (exactly as written: names are case sensitive) and the password, then connect.
   The display shows "Connected!" and restarts into normal mode.

## Design notes

- Setup is a **separate boot mode**: only the display and Bluetooth run (no web server, no TLS pool, no SD), and
  the Bluetooth memory is released at boot in normal mode. Cost in normal mode: about 10 KB less free RAM and about
  250 KB more flash. The `cyd-2432s028r-noble` build environment leaves Bluetooth out entirely.
- Failed connections report "check the network name and password" and stay open for another try.
- A developer shortcut over USB serial: type `allow` to authorise without touching the screen.
- Not implemented yet: Improv over USB serial, and the network list in the page (the display does scan, the page's
  suggestions list has not been verified on hardware).

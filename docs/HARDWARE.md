# Hardware notes

Target: **ESP32-2432S028R** ("Cheap Yellow Display"), ESP32-WROOM-32 (dual-core 240 MHz, Wi-Fi, Bluetooth 4.2,
4 MB flash, 520 KB SRAM), ILI9341 320x240 TFT, XPT2046 resistive touch, microSD, RGB LED, light sensor.

## Pin map (verified on a real board)

| Function | GPIO | Notes |
|---|---|---|
| Display MISO / MOSI / SCLK / CS / DC | 12 / 13 / 14 / 15 / 2 | HSPI; reset is tied high |
| Backlight | 21 | PWM |
| Touch (XPT2046) CLK / MISO / MOSI / CS / IRQ | 25 / 39 / 32 / 33 / 36 | **bit-banged** (see below) |
| microSD SCK / MISO / MOSI / CS | 18 / 19 / 23 / 5 | VSPI, **4 MHz** |
| RGB LED R / G / B | 4 / 16 / 17 | active low; red blinks for a critical alert |
| Light sensor | 34 | ADC; can read 0 in normal room light |
| Speaker amplifier | 26 | not used |
| BOOT button | 0 | back button; holding it at reset enters the ROM bootloader |

## Things that are not obvious

- **SD at 20 MHz silently loses reads** on this board: files vanish from the menu and writes fail. The firmware
  uses 4 MHz (plenty for JSON) and retries reads. It also remounts the card by itself if it stops answering.
- **Touch is bit-banged** because the SD card owns the VSPI peripheral and the display uses HSPI. The XPT2046
  library cannot share a bus, so touch is read by hand at about 1 MHz. Touch is calibrated on first boot.
- **LVGL 9 defaults to a 24-bit colour format**; the panel is RGB565. The firmware forces RGB565.
- **Only about 72 KB of ordinary heap is usable** after boot. A TLS handshake needs two contiguous 16 KB buffers
  plus overhead, so the firmware reserves a private 48 KB pool for TLS at boot. See [ARCHITECTURE.md](ARCHITECTURE.md).
- The microSD may be only partly formatted (a 16 GB card can carry a 500 MB partition). That is plenty for JSON cards.

## Power

**Use a 5 V / 1 A or better wall adapter and a short cable.**

On the first test unit, powered from a mini-PC USB port with an old cable, the Wi-Fi radio stopped passing
packets within one to three minutes of sustained load: the board stayed associated but pings and connections
failed, while the CPU and screen carried on. On a wall adapter the same load ran 4,000 requests in 30 minutes with
no failure. With a new cable on the PC port a 15-minute run was also clean, but an earlier 10-minute run on that
port rebooted once and then lost the SD card until it was reseated (cause not established; the firmware now
remounts the card by itself). Wi-Fi transmit bursts plus the backlight can exceed what a weak port gives. A PC port is fine for flashing; use an adapter for running.

The firmware also lowers Wi-Fi transmit power to 11 dBm (the access point is usually metres away) and records the
**reset reason** (power-on, brownout, crash, watchdog) in `GET /api/status` and the boot log, so a power problem
shows up as `BROWNOUT` instead of a mystery.

## Board variants

Some CYD revisions use a different display controller driver or need colour inversion, a different touch pin map,
or a second USB port. Only the ESP32-2432S028R above is verified. To add a variant, add an environment to
`platformio.ini` with its TFT_eSPI pin flags and a `#ifdef` block in `include/config.h`, then open a pull request
with photos of the board markings and the pin map you verified.

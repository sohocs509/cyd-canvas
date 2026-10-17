# Getting started

From a box to your first card in about fifteen minutes.

## What you need

- An **ESP32-2432S028R** "Cheap Yellow Display" (2.8 inch, ILI9341, resistive touch, microSD slot).
- A **microSD card**, FAT32 (any size; a few MB is enough). The display creates the folders it needs.
- A **5 V / 1 A (or better) wall adapter** and a short, good USB cable. See [Power](HARDWARE.md#power): a weak USB
  port makes the Wi-Fi radio go deaf under load.
- A computer with [PlatformIO](https://platformio.org/) (to build and flash) and Python 3.9+ (for the client).
- A phone, tablet or laptop with **Chrome** (for Wi-Fi setup over Bluetooth).

## 1. Build and flash

```sh
git clone https://github.com/sohocs509/cyd-canvas
cd cyd-canvas
pio run -t upload            # board connected over USB
```

There are no prebuilt images yet. `pio run -e cyd-2432s028r-noble` builds a variant without Bluetooth setup
(about 250 KB less flash, about 10 KB more free RAM) if you prefer to set Wi-Fi over USB serial.

## 2. First boot

1. **Touch calibration.** The screen shows a crosshair three times (top-left, top-right, bottom-right). Tap each.
   It is stored; repeat later from the serial console with `cal`.
2. A display with no saved Wi-Fi starts in **setup mode** (a green ALLOW button and instructions).

## 3. Connect it to Wi-Fi

Full detail in [WIFI-SETUP.md](WIFI-SETUP.md). In short:

1. Open [`web/setup.html`](../web/setup.html) in Chrome (serve it from `http://localhost`, or host it on any
   `https://` page: Web Bluetooth needs a secure context).
2. Press **Find my display**, pick it in the list.
3. Tap **ALLOW** on the display.
4. Choose your network, type the password, connect. The display shows "Connected!" and restarts.

The menu shows the display's name and IP address at the bottom.

## 4. Pair a computer with it

The display has an **API key** (generated on first boot) and a **certificate**. Both are shown on the display:
menu, **System**, **Device info**, then **Show / hide API key**.

```sh
pip install ./clients/python
cyd-canvas pair 192.168.1.50 --pin 'sha256//…'     # the pin as shown on the display; the key is prompted for
cyd-canvas status
```

`pair` saves host, key and certificate pin in `~/.config/cyd-canvas/default.env` (mode 600). The pin is checked on
every connection, so no certificate authority is involved.

## 5. Send your first card

```sh
cyd-canvas put examples/host-health.json --ttl 1d
cyd-canvas data hosts/example/health cpu=42 mem=61 disk=63
cyd-canvas show hosts/example/health
```

The card appears in the menu under its group; `--ttl 1d` makes the display delete it after a day. Data can
be sent as often as you like (each HTTPS call costs the display some CPU; for frequent updates use MQTT).

Next: the card format ([SCREEN-FORMAT.md](SCREEN-FORMAT.md)), MQTT and Home Assistant
([MQTT.md](MQTT.md), [HOME-ASSISTANT.md](HOME-ASSISTANT.md)).

## Using the display

| Do this | To |
|---|---|
| Tap a menu entry | Open a card |
| Tap **✕** (top right), press **BOOT**, or hold a finger on a card | Go back to the menu |
| Tap **ACKNOWLEDGE** (bottom right, on a red card) | Release a critical alert and stop the blinking LED |
| Menu, **System**, **Slideshow** | Start/stop the idle slideshow |
| Menu, **System**, **Wi-Fi setup (Bluetooth)** | Change the Wi-Fi network |

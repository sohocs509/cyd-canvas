# Third-party software

cyd-canvas is licensed under Apache-2.0 (see [LICENSE](LICENSE)). The firmware is built from, and links, these
projects. Each keeps its own licence.

| Component | Licence | Used for |
|---|---|---|
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) | LGPL-2.1 | runtime, Wi-Fi, SD, SPI |
| [ESP-IDF](https://github.com/espressif/esp-idf) (incl. lwIP, mbedTLS, HTTP server) | Apache-2.0 (and component licences) | TLS, networking, Bluetooth controller |
| [LVGL](https://github.com/lvgl/lvgl) | MIT | widgets and rendering |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | BSD-style (FreeBSD) | display driver |
| [ArduinoJson](https://arduinojson.org/) | MIT | JSON |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | MIT | MQTT |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | Apache-2.0 | Bluetooth LE (Wi-Fi setup) |

**LGPL note for binary releases.** A firmware image links the LGPL-2.1 Arduino core statically. This repository
contains everything needed to rebuild the firmware (`pio run`), so you can rebuild it against a modified core
and reflash. Binary release notes must say so and link to the corresponding source.

The Improv Wi-Fi protocol is implemented from its public [specification](https://www.improv-wifi.com/ble/).

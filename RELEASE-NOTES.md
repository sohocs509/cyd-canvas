> **UNTESTED PRE-RELEASE.** This build, the merged image and the browser flasher have **not** been run on hardware
> as a release: the render suite and the soak test were not repeated for this tag, and the info-hub MQTT sources
> (phase 1) have never run on a board. Expect problems; please report them.

Firmware for the ESP32-2432S028R ("Cheap Yellow Display"). Flash it from the browser at
https://sohocs509.github.io/cyd-canvas/install.html (Chrome or Edge, USB cable), or write
`cyd-canvas-merged.bin` at offset 0 with esptool. `cyd-canvas-app.bin` is the application only (offset 0x10000).

**Flashing the merged image erases the settings stored on the board (Wi-Fi, API key, pairing).**
To keep them, update with `pio run -t upload` instead.

**Licence notice:** the firmware statically links the LGPL-2.1 Arduino-ESP32 core. The corresponding source is this
repository at the tag of this release; see THIRD-PARTY.md.

See CHANGELOG.md and docs/KNOWN-ISSUES.md for what is and is not verified.

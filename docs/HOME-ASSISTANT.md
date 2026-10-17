# Home Assistant

## Status of this integration

| Part | Status |
|---|---|
| Entities created by MQTT discovery (selector, brightness, slideshow switch, 2 sensors, button) | **Verified** on a real Home Assistant: the device and all six entities appeared |
| What each entity displays and sends (templates rendered against a recording of the device's real messages) | **Verified**, and covered by an automated test in CI |
| Commands Home Assistant sends (`show <id>`, `menu`, `brightness n`, `slideshow on/off`) handled by the device | **Verified** over MQTT |
| Driving the entities from Home Assistant's own UI (click the selector, slider, switch) | **Not yet checked by a person** |
| Sending cards and data from automations | Works through `mqtt.publish` (below); **no Home Assistant services or validation yet** |
| A proper integration with services, entity bindings and card templates | **Planned**: [design](design/home-assistant-integration.md) |

## Set up

1. In Home Assistant add the **MQTT** integration (Settings, Devices & services, Add integration, MQTT) pointing at
   your broker. Leave discovery on (the default prefix is `homeassistant`).
2. Point the display at the same broker:
   ```sh
   cyd-canvas mqtt-config --host <broker> [--user <u> --password <p>] --discovery-prefix homeassistant
   ```
3. Within a few seconds a device named like `cyd-ab0694` appears with its six entities. It goes unavailable
   when the display is switched off or loses Wi-Fi.

Use a broker with a **username and password** (and TLS if it leaves your network). An anonymous plaintext broker
means anyone on the network can change what the display shows.

## Showing Home Assistant data

Cards are uploaded once (HTTPS, with the CLI or Python client); after that, readings are published to
`<prefix>/data/<card id>` from an automation.

```yaml
automation:
  - alias: Server CPU on the display
    triggers:
      - trigger: state
        entity_id: sensor.server_cpu
    actions:
      - action: mqtt.publish
        data:
          topic: cyd/cyd-ab0694/data/hosts/server/health
          payload: '{"cpu": {{ states("sensor.server_cpu") | float(0) }}}'
```

Tips:

- Give a busy sensor a minimum interval (`for:` on the trigger, or a time pattern) so you send a reading every
  few seconds, not ten times a second.
- Send only what changed; missing keys keep their last value.
- A chart widget collects each number you send as a new point. Send an **array** to backfill a chart.

## Switching screens and brightness

```yaml
actions:
  - action: mqtt.publish
    data: {topic: cyd/cyd-ab0694/cmd, payload: "show hosts/server/health"}
  - action: mqtt.publish
    data: {topic: cyd/cyd-ab0694/cmd, payload: "brightness 40"}
```

Or use the **Screen** selector, **Brightness** slider and **Slideshow** switch that discovery created, for example
to dim the display at night.

## Alerts

A card with `"priority": "critical"` takes over the display, forces the backlight to full and blinks the red
LED until it is acknowledged on the display, or by `POST /api/ack` (`cyd-canvas ack`). Raising one requires a card
**upload**, which today goes over HTTPS, so from Home Assistant you would call the CLI (a `shell_command`) or the
Python client. Making this a one-line service is the first thing the planned integration adds.

## Known gaps

- Cards cannot be uploaded over MQTT (they can be up to 16 KB; the MQTT buffer is 3 KB).
- Home Assistant does not know which card ids or data keys exist; the planned integration fixes that.
- A new device's entities are not exposed to Home Assistant's voice assistant by default (a Home Assistant setting).

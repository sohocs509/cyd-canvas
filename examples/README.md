# Example cards

Upload any of these with `cyd-canvas put examples/<file>.json`, then open the card from the menu (or
`cyd-canvas show <id>`). Every file is checked against the schema by the test suite.

| File | Id | Shows |
|---|---|---|
| `host-health.json` | `hosts/example/health` | Live host metrics: value, arc and chart, fed by data messages |
| `lab-status.json` | `lab/status` | LEDs, bar, value and table filled from MQTT sources |
| `services.json` | `hosts/example/services` | Service list |
| `demo-health.json` | `demo/health` | Two values, a disk arc and a two-series line chart |
| `demo-status.json` | `demo/status` | Three status LEDs, a bar, a value and a services table |
| `demo-gauges.json` | `demo/gauges` | Three arcs, a bar and a value |
| `demo-week.json` | `demo/week` | A bar chart of 14 days |

The `demo-*` cards carry their sample numbers in an embedded `data` field, so they display on their own with no
feeder and survive a reboot. The numbers are made up. To drive a card with live readings, send data to its id
(`cyd-canvas data demo/health cpu=55`); live readings layer on top of the embedded ones.

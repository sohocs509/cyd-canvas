# cyd-canvas Python client

Standard library only (Pillow optional for images, jsonschema optional for validation).

```sh
pip install ./clients/python            # or: python -m cyd_canvas.cli ...
cyd-canvas pair 192.168.10.139 --pin 'sha256//…'   # key prompted; pin shown on the device
cyd-canvas put examples/host-health.json
cyd-canvas data host-health cpu=42 mem=61 disk=63
cyd-canvas show host-health
```

```python
from cyd_canvas import Canvas
c = Canvas.from_profile()
c.put_screen("examples/host-health.json")
c.data("host-health", {"cpu": 42, "mem": 61})
```

The device certificate is self-signed; trust comes from a **public-key pin** saved at pairing
(compare it with the "TLS pin" on the device's *Device info* screen).

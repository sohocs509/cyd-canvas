# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import argparse
import getpass
import json
import sys
from pathlib import Path

from . import Canvas, CanvasError, Profile
from .client import fetch_pin
from .imaging import default_asset_name, to_lvgl_bin
from .mqtt import publish


def _parse_pairs(pairs):
    out = {}
    for p in pairs:
        if "=" not in p:
            raise SystemExit(f"expected key=value, got {p!r}")
        k, v = p.split("=", 1)
        try:
            out[k] = json.loads(v)
        except ValueError:
            out[k] = v
    return out


def _validate(path: str) -> None:
    try:
        import jsonschema
    except ImportError:
        raise SystemExit("validation needs jsonschema: pip install jsonschema")
    candidates = [Path(__file__).resolve().parents[3] / "schema" / "screen.schema.json",
                  Path.cwd() / "schema" / "screen.schema.json"]
    schema_path = next((c for c in candidates if c.exists()), None)
    if not schema_path:
        raise SystemExit("screen.schema.json not found (run from the repository root)")
    schema = json.loads(schema_path.read_text())
    errors = list(jsonschema.Draft202012Validator(schema).iter_errors(json.loads(Path(path).read_text())))
    for e in errors:
        loc = "/".join(str(x) for x in e.absolute_path) or "(root)"
        print(f"{path}: {loc}: {e.message[:200]}")
    if errors:
        raise SystemExit(1)
    print(f"{path}: valid")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="cyd-canvas", description="Control a cyd-canvas display")
    ap.add_argument("-p", "--profile", default="default", help="saved device profile (default: default)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("pair", help="save host + API key + certificate pin")
    s.add_argument("host")
    s.add_argument("--pin", help="expected pin shown on the device (recommended)")
    s.add_argument("--key", help="API key (prompted if omitted; or '-' to read stdin)")

    sub.add_parser("status")
    sub.add_parser("list", help="list screens")
    s = sub.add_parser("get"); s.add_argument("id")
    sub.add_parser("index", help="list cards with title, menu group and expiry")
    s = sub.add_parser("put", help="upload a card (same id replaces the older one)"); s.add_argument("file")
    g = s.add_mutually_exclusive_group()
    g.add_argument("--expires", metavar="UTC", help="e.g. 2026-10-20T18:00:00Z")
    g.add_argument("--ttl", metavar="DUR", help="delete after e.g. 90s 30m 6h 2d 1w")
    s = sub.add_parser("rm", help="delete a screen"); s.add_argument("id")
    s = sub.add_parser("validate", help="check a screen file against the schema"); s.add_argument("file")

    sub.add_parser("assets", help="list images")
    s = sub.add_parser("asset-put", help="upload an image (PNG/JPEG converted automatically)")
    s.add_argument("file"); s.add_argument("--name")
    s = sub.add_parser("asset-rm"); s.add_argument("name")

    s = sub.add_parser("data", help="send readings: data <screen> cpu=42 mem=61 | data <screen> -j '{...}'")
    s.add_argument("id"); s.add_argument("pairs", nargs="*"); s.add_argument("-j", "--json")
    s.add_argument("--mqtt", metavar="HOST", help="send via MQTT instead of HTTPS")
    s.add_argument("--prefix", help="MQTT topic prefix (default cyd/<device name>)")
    s.add_argument("--mqtt-user"); s.add_argument("--mqtt-password")

    s = sub.add_parser("ack", help="acknowledge critical cards (all if no id)"); s.add_argument("id", nargs="?", default="")
    s = sub.add_parser("slideshow", help="cycle cards when idle")
    s.add_argument("state", choices=["on", "off"]); s.add_argument("--idle", type=int, help="seconds without touch before it starts")
    s.add_argument("--dwell", type=int, help="seconds per card"); s.add_argument("--prefix", help="only ids starting with this")
    s = sub.add_parser("limits", help="housekeeping limits (auto-delete lowest priority, oldest first)")
    s.add_argument("--max-cards", type=int); s.add_argument("--min-free-mb", type=int)
    s = sub.add_parser("show"); s.add_argument("id")
    sub.add_parser("menu")
    s = sub.add_parser("brightness"); s.add_argument("percent", type=int)
    s = sub.add_parser("mqtt-config", help="point the device at an MQTT broker")
    s.add_argument("--host", required=True); s.add_argument("--port", type=int, default=1883)
    s.add_argument("--user"); s.add_argument("--password")
    s.add_argument("--tls", action="store_true"); s.add_argument("--ca", help="CA PEM file for TLS")
    s.add_argument("--prefix"); s.add_argument("--discovery-prefix")

    a = ap.parse_args(argv)
    try:
        if a.cmd == "pair":
            key = a.key
            if key == "-":
                key = sys.stdin.readline().strip()
            elif not key:
                key = getpass.getpass("API key (from the device's Device info screen): ")
            pin = fetch_pin(a.host)
            if a.pin and a.pin != pin:
                raise SystemExit(f"PIN MISMATCH\n  expected {a.pin}\n  device   {pin}\nNot saved.")
            if not a.pin:
                print(f"Device presents {pin}\nCompare with the 'TLS pin' on its Device info screen.")
            Profile(a.profile).save(a.host, key, pin)
            c = Canvas(a.host, key, pin)
            print("paired:", json.dumps(c.status()))
            return 0
        if a.cmd == "validate":
            _validate(a.file)
            return 0

        c = Canvas.from_profile(a.profile)
        res = None
        if a.cmd == "status": res = c.status()
        elif a.cmd == "list": res = c.screens()
        elif a.cmd == "get": res = c.get_screen(a.id)
        elif a.cmd == "index": res = c.index()
        elif a.cmd == "put": res = c.put_screen(a.file, expires=a.expires, ttl=a.ttl)
        elif a.cmd == "rm": res = c.delete_screen(a.id)
        elif a.cmd == "assets": res = c.assets()
        elif a.cmd == "asset-put":
            name = a.name or default_asset_name(a.file)
            blob = Path(a.file).read_bytes() if a.file.endswith(".bin") else to_lvgl_bin(a.file)
            res = c.put_asset(name, blob)
            print(f"uploaded {name} ({len(blob)} bytes)")
        elif a.cmd == "asset-rm": res = c.delete_asset(a.name)
        elif a.cmd == "data":
            vals = json.loads(a.json) if a.json else _parse_pairs(a.pairs)
            if a.mqtt:
                prefix = a.prefix or f"cyd/{c.status()['name']}"
                publish(a.mqtt, f"{prefix}/data/{a.id}", vals, username=a.mqtt_user, password=a.mqtt_password)
                res = {"ok": True, "via": "mqtt"}
            else:
                res = c.data(a.id, vals)
        elif a.cmd == "ack": res = c.ack(a.id)
        elif a.cmd == "slideshow": res = c.slideshow(a.state == "on", a.idle, a.dwell, a.prefix)
        elif a.cmd == "limits": res = c.limits(a.max_cards, a.min_free_mb)
        elif a.cmd == "show": res = c.show(a.id)
        elif a.cmd == "menu": res = c.menu()
        elif a.cmd == "brightness": res = c.brightness(a.percent)
        elif a.cmd == "mqtt-config":
            fields = {"host": a.host, "port": a.port, "tls": a.tls}
            for k in ("user", "password", "prefix"):
                if getattr(a, k): fields[k] = getattr(a, k)
            if a.discovery_prefix: fields["discovery_prefix"] = a.discovery_prefix
            if a.ca: fields["ca"] = Path(a.ca).read_text()
            res = c.set_mqtt(**fields)
        if res is not None:
            print(json.dumps(res, indent=2) if isinstance(res, (dict, list)) else res)
        return 0
    except CanvasError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"error: cannot reach device: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())

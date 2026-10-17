# SPDX-License-Identifier: Apache-2.0
"""HTTPS client with public-key pinning (no CA needed for the device's own certificate)."""
from __future__ import annotations

import base64
import hashlib
import hmac
import http.client
import json
import os
import re
import ssl
import time
from pathlib import Path
from typing import Any, Optional, Union


_TTL = re.compile(r"^(\d+)([smhdw])$")


def expires_from_ttl(ttl: str) -> str:
    """'90s', '30m', '6h', '2d', '1w' -> absolute UTC timestamp the device understands."""
    m = _TTL.match(ttl.strip())
    if not m:
        raise ValueError(f"bad ttl {ttl!r}; use e.g. 90s, 30m, 6h, 2d, 1w")
    secs = int(m.group(1)) * {"s": 1, "m": 60, "h": 3600, "d": 86400, "w": 604800}[m.group(2)]
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(time.time() + secs))


class CanvasError(Exception):
    def __init__(self, message: str, status: Optional[int] = None):
        super().__init__(message)
        self.status = status


def _element(der: bytes, i: int):
    """Return (value_start, end) of the DER element at offset i."""
    n = der[i + 1]
    j = i + 2
    if n & 0x80:
        k = n & 0x7F
        n = int.from_bytes(der[j:j + k], "big")
        j += k
    return j, j + n


def spki_pin(cert_der: bytes) -> str:
    """curl-style pin of a certificate's public key: sha256//<base64>."""
    start, _ = _element(cert_der, 0)       # Certificate
    i, _ = _element(cert_der, start)       # TBSCertificate
    if cert_der[i] == 0xA0:                # optional [0] version
        _, i = _element(cert_der, i)
    for _ in range(5):                     # serial, sigAlg, issuer, validity, subject
        _, i = _element(cert_der, i)
    _, end = _element(cert_der, i)         # SubjectPublicKeyInfo
    digest = hashlib.sha256(cert_der[i:end]).digest()
    return "sha256//" + base64.b64encode(digest).decode()


class _PinnedConnection(http.client.HTTPSConnection):
    def __init__(self, host, port, pin, timeout):
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE   # trust comes from the pin, checked below
        super().__init__(host, port, timeout=timeout, context=ctx)
        self._pin = pin

    def connect(self):
        super().connect()
        der = self.sock.getpeercert(binary_form=True)
        got = spki_pin(der)
        if self._pin and not hmac.compare_digest(got, self._pin):
            self.close()
            raise CanvasError(f"certificate pin mismatch (device presented {got})")
        self.peer_pin = got


def fetch_pin(host: str, port: int = 443, timeout: float = 10) -> str:
    """Read the pin a device presents (for first-time pairing; verify it against the screen)."""
    conn = _PinnedConnection(host, port, None, timeout)
    try:
        conn.connect()
        return conn.peer_pin
    finally:
        conn.close()


class Profile:
    """Saved device settings in ~/.config/cyd-canvas/<name>.env (shared with the bash client)."""

    def __init__(self, name: str = "default"):
        base = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "cyd-canvas"
        self.path = base / f"{name}.env"

    def load(self) -> dict:
        if not self.path.exists():
            raise CanvasError(f"no profile at {self.path}; run 'cyd-canvas pair' first")
        out = {}
        for line in self.path.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                out[k.strip()] = v.strip().strip('"')
        return out

    def save(self, host: str, key: str, pin: str) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        fd = os.open(self.path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(f'CYD_HOST="{host}"\nCYD_KEY="{key}"\nCYD_PIN="{pin}"\n')


class Canvas:
    def __init__(self, host: str, api_key: str, pin: Optional[str], port: int = 443, timeout: float = 10):
        self.host, self.api_key, self.pin, self.port, self.timeout = host, api_key, pin, port, timeout

    @classmethod
    def from_profile(cls, name: str = "default") -> "Canvas":
        p = Profile(name).load()
        return cls(p["CYD_HOST"], p["CYD_KEY"], p.get("CYD_PIN"))

    # -- transport ---------------------------------------------------
    def _call(self, method: str, path: str, body: Union[bytes, str, dict, list, None] = None) -> Any:
        headers = {"X-Api-Key": self.api_key}
        data = None
        if body is not None:
            if isinstance(body, (dict, list)):
                data = json.dumps(body, separators=(",", ":")).encode()
                headers["Content-Type"] = "application/json"
            else:
                data = body.encode() if isinstance(body, str) else body
                headers["Content-Type"] = "application/octet-stream"
        conn = _PinnedConnection(self.host, self.port, self.pin, self.timeout)
        try:
            conn.request(method, path, body=data, headers=headers)
            resp = conn.getresponse()
            raw = resp.read()
        finally:
            conn.close()
        try:
            payload = json.loads(raw) if raw else None
        except ValueError:
            payload = raw.decode(errors="replace")
        if resp.status >= 400:
            msg = payload.get("error") if isinstance(payload, dict) else payload
            raise CanvasError(f"{method} {path}: {resp.status} {msg}", resp.status)
        return payload

    # -- API ---------------------------------------------------------
    def status(self) -> dict: return self._call("GET", "/api/status")
    def config(self) -> dict: return self._call("GET", "/api/config")
    def set_mqtt(self, **fields) -> dict: return self._call("PUT", "/api/config", {"mqtt": fields})

    def screens(self) -> list: return self._call("GET", "/api/screens")
    def get_screen(self, sid: str) -> dict: return self._call("GET", f"/api/screens/{sid}")
    def delete_screen(self, sid: str) -> dict: return self._call("DELETE", f"/api/screens/{sid}")

    def index(self) -> list:
        """[{id, title, menu, expires}] for every stored card."""
        return self._call("GET", "/api/index")

    def put_screen(self, screen: Union[dict, str, Path], *, expires: Optional[str] = None,
                   ttl: Optional[str] = None) -> dict:
        """Store a card. The id is a path (hosts/milo/health); the same id replaces the older card.
        `expires` (UTC, 2026-10-20T18:00:00Z) or `ttl` (6h) make the device delete it later."""
        if isinstance(screen, (str, Path)):
            screen = json.loads(Path(screen).read_text())
        if ttl:
            expires = expires_from_ttl(ttl)
        if expires:
            screen = dict(screen, expires=expires)
        return self._call("PUT", f"/api/screens/{screen['id']}", screen)

    def assets(self) -> list: return self._call("GET", "/api/assets")
    def delete_asset(self, name: str) -> dict: return self._call("DELETE", f"/api/assets/{name}")

    def put_asset(self, name: str, data: bytes) -> dict:
        return self._call("PUT", f"/api/assets/{name}", data)

    def data(self, sid: str, values: dict) -> dict: return self._call("POST", f"/api/data/{sid}", values)
    def show(self, sid: str) -> dict: return self._call("POST", f"/api/show/{sid}", b"")
    def menu(self) -> dict: return self._call("POST", "/api/menu", b"")
    def brightness(self, percent: int) -> dict: return self._call("POST", "/api/brightness", {"value": int(percent)})

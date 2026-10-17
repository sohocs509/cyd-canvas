# SPDX-License-Identifier: Apache-2.0
"""Minimal MQTT 3.1.1 publisher (QoS 0), so cron jobs need no extra packages."""
from __future__ import annotations

import json
import socket
import ssl
import struct
from typing import Optional, Union


def _utf(s: str) -> bytes:
    b = s.encode()
    return struct.pack(">H", len(b)) + b


def _remaining(n: int) -> bytes:
    out = bytearray()
    while True:
        byte, n = n % 128, n // 128
        out.append(byte | (0x80 if n else 0))
        if not n:
            return bytes(out)


def publish(host: str, topic: str, payload: Union[str, bytes, dict], *, port: int = 1883,
            username: Optional[str] = None, password: Optional[str] = None,
            tls: bool = False, cafile: Optional[str] = None, retain: bool = False,
            timeout: float = 5.0) -> None:
    if isinstance(payload, dict):
        payload = json.dumps(payload, separators=(",", ":"))
    if isinstance(payload, str):
        payload = payload.encode()
    sock = socket.create_connection((host, port), timeout=timeout)
    try:
        if tls:
            sock = ssl.create_default_context(cafile=cafile).wrap_socket(sock, server_hostname=host)
        flags = 0x02 | (0x80 if username else 0) | (0x40 if username and password else 0)
        body = _utf("MQTT") + bytes([4, flags]) + struct.pack(">H", 30) + _utf("cyd-canvas-pub")
        if username:
            body += _utf(username)
            if password:
                body += _utf(password)
        sock.sendall(b"\x10" + _remaining(len(body)) + body)
        ack = sock.recv(4)
        if len(ack) < 4 or ack[0] != 0x20 or ack[3] != 0:
            raise ConnectionError(f"MQTT connect refused (CONNACK {ack.hex()})")
        t = _utf(topic)
        sock.sendall(bytes([0x30 | (1 if retain else 0)]) + _remaining(len(t) + len(payload)) + t + payload)
        sock.sendall(b"\xe0\x00")
    finally:
        sock.close()

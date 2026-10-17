#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Upload the render fixtures to a device, step through them, and check the API contract.

  python tests/render/run.py [-p profile] [--interactive] [--only t05] [--hold 4]

Without --interactive each step is shown for --hold seconds with its expectation printed;
with it you press Enter (ok) or type a note (problem) after each step and get a report.
The API checks (auth, invalid input, unknown ids) are automatic and decide the exit code.
"""
import argparse, http.client, json, math, os, ssl, struct, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "clients" / "python"))
from cyd_canvas import Canvas, CanvasError            # noqa: E402
from cyd_canvas.client import _PinnedConnection        # noqa: E402
from cyd_canvas.imaging import to_lvgl_bin            # noqa: E402

failures = []


def check(name, ok, detail=""):
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f"  ({detail})" if detail and not ok else ""))
    if not ok:
        failures.append(name)


def make_images(tmp: Path):
    from PIL import Image, ImageDraw
    g = Image.new("RGB", (100, 100))
    px = g.load()
    for x in range(100):
        for y in range(100):
            px[x, y] = (x * 255 // 99, y * 255 // 99, 128)
    g.save(tmp / "t-gradient.png")
    a = Image.new("RGBA", (100, 100), (0, 0, 0, 0))
    ImageDraw.Draw(a).ellipse((4, 4, 95, 95), fill=(255, 200, 0, 255))
    a.save(tmp / "t-alpha.png")


def raw(c: Canvas, method, path, body=None, key="__default__"):
    """Low-level request returning (status, text) without raising."""
    conn = _PinnedConnection(c.host, c.port, c.pin, c.timeout)
    headers = {} if key is None else {"X-Api-Key": c.api_key if key == "__default__" else key}
    try:
        conn.request(method, path, body=body, headers=headers)
        r = conn.getresponse()
        return r.status, r.read().decode(errors="replace")
    finally:
        conn.close()


def api_checks(c: Canvas):
    print("API contract")
    check("status with key -> 200", raw(c, "GET", "/api/status")[0] == 200)
    check("no key -> 401", raw(c, "GET", "/api/status", key=None)[0] == 401)
    check("wrong key -> 401", raw(c, "GET", "/api/status", key="0" * 32)[0] == 401)
    check("unknown route -> 404", raw(c, "GET", "/api/nope")[0] == 404)
    check("data to unknown screen -> 404", raw(c, "POST", "/api/data/does-not-exist", b'{"a":1}')[0] == 404)
    check("show unknown screen -> 404", raw(c, "POST", "/api/show/does-not-exist")[0] == 404)
    check("bad screen id -> 400", raw(c, "GET", "/api/screens/Bad_ID")[0] == 400)
    check("bad asset name -> 400", raw(c, "PUT", "/api/assets/evil.exe", b"\x19")[0] == 400)
    check("non-image asset -> 400", raw(c, "PUT", "/api/assets/plain.bin", b"hello world")[0] == 400)
    check("oversize screen -> 413", raw(c, "PUT", "/api/screens/huge", b" " * 20000)[0] == 413)
    check("data not an object -> 400", raw(c, "POST", "/api/data/t01-text", b"[1,2]")[0] in (400, 404))
    check("traversal id rejected", raw(c, "GET", "/api/screens/..%2f..%2fetc")[0] in (400, 404))


def utc_in(seconds):
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(time.time() + seconds))


def tiny(cid, title="x", **extra):
    d = {"schema": 1, "id": cid, "title": title, "widgets": [{"type": "label", "x": 8, "y": 8, "text": title}]}
    d.update(extra)
    return d


def id_checks(c: Canvas):
    print("Card ids, overwrite and expiry rules")
    deep = "test/nested/deep/card"
    c.put_screen(tiny(deep, "deep v1"))
    check("4-segment path id stored", deep in c.screens())
    c.put_screen(tiny(deep, "deep v2"))
    check("same id overwrites (one entry, new content)",
          c.screens().count(deep) == 1 and c.get_screen(deep)["title"] == "deep v2")
    idx = {e["id"]: e for e in c.index()}
    check("menu group defaults to parent path", idx.get(deep, {}).get("menu") == "test/nested/deep", str(idx.get(deep)))
    check("path id accepts data", c.data(deep, {"a": 1}) == {"ok": True})
    c.delete_screen(deep)
    check("path id deleted", deep not in c.screens())
    for bad in ["test/a/b/c/d", "Test/Upper", "test//double", "/lead", "trail/", "test/a b"]:
        body = json.dumps(tiny("x")).encode()
        st, _ = raw(c, "PUT", "/api/screens/" + bad.replace(" ", "%20"), body)
        check(f"bad id {bad!r} rejected", st in (400, 404), str(st))
    st, _ = raw(c, "PUT", "/api/screens/test/exp-bad", json.dumps(tiny("test/exp-bad", expires="tomorrow")).encode())
    check("malformed expires -> 400", st == 400, str(st))
    if c.status().get("time_valid"):
        st, _ = raw(c, "PUT", "/api/screens/test/exp-past", json.dumps(tiny("test/exp-past", expires=utc_in(-60))).encode())
        check("expires in the past -> 400", st == 400, str(st))
        check("device clock within 120 s of host", abs(time.mktime(time.strptime(c.status()["time_utc"], "%Y-%m-%dT%H:%M:%SZ")) - time.mktime(time.gmtime())) < 120)
    else:
        print("  [SKIP] clock not synced yet; expiry-in-past check skipped")


def expiry_test(c: Canvas, man):
    print("Expiry (slow)")
    if not c.status().get("time_valid"):
        check("device clock synced (NTP)", False, "time_valid is false")
        return
    cid = "test/t14-expiry"
    screen = json.loads((HERE / "screens" / "t14-expiry.json").read_text())
    screen["expires"] = utc_in(75)
    c.put_screen(screen)
    check("expiring card stored", cid in c.screens())
    check("index shows its expiry", any(e["id"] == cid and e["expires"] for e in c.index()))
    c.show(cid)
    deadline = time.time() + 75 + 60
    while time.time() < deadline and cid in c.screens():
        time.sleep(5)
    check("card deleted after expiry", cid not in c.screens())
    st, _ = raw(c, "POST", f"/api/data/{cid}", b'{"n":2}')
    check("data to expired card -> 404", st == 404, str(st))
    check("device returned to the menu", c.status()["current"] == "", c.status()["current"])


def drive(c: Canvas, case, step, hold, interactive, report):
    print(f"  expect: {step['expect']}")
    if "gen" in step:
        n, d = step["count"], step.get("delay", 0.1)
        for i in range(n):
            if step["gen"] == "sine":
                lo, hi = step["min"], step["max"]
                vals = {step["key"]: round(lo + (hi - lo) * (0.5 + 0.5 * math.sin(i / 7.0)), 1)}
            else:
                vals = {k: round(50 + 45 * math.sin(i / (4 + 3 * j) + j), 1) for j, k in enumerate(step["keys"])}
            c.data(case["id"], vals)
            time.sleep(d)
    elif "data" in step:
        c.data(case["id"], step["data"])
    if interactive:
        note = input("    Enter = looks right, or type what is wrong: ").strip()
        if note:
            report.append((case["id"], step["expect"], note))
    else:
        time.sleep(hold)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--profile", default="default")
    ap.add_argument("--interactive", action="store_true")
    ap.add_argument("--only", help="run only cases whose id starts with this")
    ap.add_argument("--hold", type=float, default=4.0, help="seconds to show each step")
    ap.add_argument("--slow", action="store_true", help="include tests that wait (expiry)")
    ap.add_argument("--no-api", action="store_true", help="skip the API contract checks")
    ap.add_argument("--cleanup", action="store_true", help="delete the test screens/assets afterwards")
    a = ap.parse_args()

    c = Canvas.from_profile(a.profile)
    man = json.loads((HERE / "manifest.json").read_text())
    print("device:", c.status()["name"])

    if not a.no_api:
        api_checks(c)

    print("Invalid screens must be rejected")
    for inv in man["invalid"]:
        body = (HERE / inv["file"]).read_bytes()
        st, txt = raw(c, "PUT", f"/api/screens/{inv['id']}", body)
        check(f"{inv['file']} -> {inv['status']}", st == inv["status"], f"got {st} {txt[:80]}")
        check(f"{inv['id']} was not stored", inv["id"] not in c.screens())

    id_checks(c)
    import tempfile
    with tempfile.TemporaryDirectory() as t:
        t = Path(t)
        make_images(t)
        for name in man["assets"]:
            c.put_asset(name, to_lvgl_bin(t / name.replace(".bin", ".png")))
        print("assets uploaded:", ", ".join(man["assets"]))

    report = []
    for case in man["cases"]:
        if a.only and a.only not in case["id"]:
            continue
        if case.get("slow") and not a.slow:
            print(f"\n{case['id']}  (skipped: needs --slow)")
            continue
        print(f"\n{case['id']}")
        c.put_screen(HERE / "screens" / f"{case['id'].split('/')[-1]}.json")
        c.show(case["id"])
        time.sleep(0.8)
        for step in case["steps"]:
            drive(c, case, step, a.hold, a.interactive, report)
        check(f"{case['id']} still stored", case["id"] in c.screens())

    if a.slow:
        expiry_test(c, man)
    st = c.status()
    check("device healthy at end (heap > 40 KB)", st["heap_free"] > 40000, str(st["heap_free"]))
    c.menu()
    if a.cleanup:
        for case in man["cases"]:
            try: c.delete_screen(case["id"])
            except CanvasError: pass
        for n in man["assets"]:
            try: c.delete_asset(n)
            except CanvasError: pass

    print("\n==== RESULT ====")
    for cid, exp, note in report:
        print(f"RENDER ISSUE {cid}: {note}\n    expected: {exp}")
    if failures:
        print("API/contract failures:", ", ".join(failures))
    if not failures and not report:
        print("all checks passed" + ("" if a.interactive else " (rendering not judged: run with --interactive)"))
    return 1 if failures or report else 0


if __name__ == "__main__":
    sys.exit(main())

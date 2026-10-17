# SPDX-License-Identifier: Apache-2.0
import base64
import json
import shutil
import subprocess
import time
from pathlib import Path

import pytest
from cyd_canvas.client import expires_from_ttl, spki_pin
from cyd_canvas.imaging import LV_IMAGE_HEADER_MAGIC, to_lvgl_bin

DATA = Path(__file__).parent / "data"
ROOT = Path(__file__).resolve().parents[3]


def test_spki_pin_matches_openssl():
    pem = (DATA / "device-cert.pem").read_text()
    der = base64.b64decode("".join(l for l in pem.splitlines() if not l.startswith("-----")))
    assert spki_pin(der) == (DATA / "device-cert.pin").read_text().strip()


@pytest.mark.skipif(not shutil.which("openssl"), reason="openssl not installed")
def test_spki_pin_independent_of_openssl_implementation():
    out = subprocess.run(
        "openssl x509 -in {} -pubkey -noout | openssl pkey -pubin -outform der | "
        "openssl dgst -sha256 -binary | base64".format(DATA / "device-cert.pem"),
        shell=True, capture_output=True, text=True, check=True).stdout.strip()
    assert (DATA / "device-cert.pin").read_text().strip() == "sha256//" + out


@pytest.mark.parametrize("ttl,secs", [("90s", 90), ("30m", 1800), ("6h", 21600), ("2d", 172800), ("1w", 604800)])
def test_ttl(ttl, secs):
    got = time.strptime(expires_from_ttl(ttl), "%Y-%m-%dT%H:%M:%SZ")
    import calendar
    assert abs(calendar.timegm(got) - (time.time() + secs)) < 3


@pytest.mark.parametrize("bad", ["", "6", "h", "6x", "-1h", "1.5h"])
def test_ttl_rejects(bad):
    with pytest.raises(ValueError):
        expires_from_ttl(bad)


def test_image_conversion(tmp_path):
    PIL = pytest.importorskip("PIL.Image")
    opaque = tmp_path / "o.png"
    PIL.new("RGB", (10, 6), (255, 0, 0)).save(opaque)
    blob = to_lvgl_bin(opaque)
    assert blob[0] == LV_IMAGE_HEADER_MAGIC and blob[1] == 0x12
    assert int.from_bytes(blob[4:6], "little") == 10 and int.from_bytes(blob[6:8], "little") == 6
    assert len(blob) == 12 + 10 * 6 * 2
    assert blob[12:14] == (0xF800).to_bytes(2, "little")          # pure red in RGB565
    clear = tmp_path / "a.png"
    PIL.new("RGBA", (4, 4), (0, 0, 0, 0)).save(clear)
    blob = to_lvgl_bin(clear)
    assert blob[1] == 0x14 and len(blob) == 12 + 4 * 4 * 2 + 4 * 4   # RGB565A8: colour plane then alpha plane


def test_every_fixture_matches_the_schema():
    jsonschema = pytest.importorskip("jsonschema")
    schema = json.loads((ROOT / "schema" / "screen.schema.json").read_text())
    v = jsonschema.Draft202012Validator(schema)
    jsonschema.Draft202012Validator.check_schema(schema)
    for f in sorted((ROOT / "tests" / "render" / "screens").glob("*.json")) + sorted((ROOT / "examples").glob("*.json")):
        errs = list(v.iter_errors(json.loads(f.read_text())))
        if f.name == "t02-value-format.json":
            assert errs, "t02 deliberately carries a hostile fmt and must not validate"
        else:
            assert not errs, f"{f.name}: {errs[0].message}"


def test_invalid_fixtures_are_really_invalid():
    jsonschema = pytest.importorskip("jsonschema")
    v = jsonschema.Draft202012Validator(json.loads((ROOT / "schema" / "screen.schema.json").read_text()))
    for name in ("schema-2.json", "widgets-not-array.json"):
        assert list(v.iter_errors(json.loads((ROOT / "tests" / "render" / "invalid" / name).read_text()))), name


def test_openapi_is_valid():
    yaml = pytest.importorskip("yaml")
    spec = yaml.safe_load((ROOT / "docs" / "openapi.yaml").read_text())
    assert spec["openapi"].startswith("3.1")
    try:
        from openapi_spec_validator import validate
    except ImportError:
        pytest.skip("openapi-spec-validator not installed")
    validate(spec)

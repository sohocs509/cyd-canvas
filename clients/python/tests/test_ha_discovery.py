# SPDX-License-Identifier: Apache-2.0
"""Render every Home Assistant discovery template exactly as Home Assistant would, against a recording of
the messages a real device published. Catches state fields that a template reads but the device never sends."""
import json
from pathlib import Path

import pytest

jinja2 = pytest.importorskip("jinja2")
REC = json.loads((Path(__file__).parent / "data" / "ha-discovery.json").read_text())
ENV = jinja2.Environment()


def render(tpl, **ctx):
    return ENV.from_string(tpl).render(**ctx).strip()


@pytest.mark.parametrize("topic", sorted(REC["discovery"]))
def test_entity_is_well_formed(topic):
    d = REC["discovery"][topic]
    assert d["unique_id"].startswith("cyd-example_")
    assert d["device"]["identifiers"] == ["cyd-example"]
    assert d["availability_topic"].endswith("/status")
    if "state_topic" in d:
        assert d["state_topic"].endswith("/state")


@pytest.mark.parametrize("topic", [t for t, d in sorted(REC["discovery"].items()) if "value_template" in d])
def test_state_template_shows_a_value(topic):
    d = REC["discovery"][topic]
    shown = render(d["value_template"], value_json=REC["state"])
    assert shown != "", f"{d['name']}: template reads a field the device does not send"
    if "state_on" in d:
        assert shown in (d["state_on"], d["state_off"])


def test_select_commands():
    d = next(v for k, v in REC["discovery"].items() if "/select/" in k)
    sent = [render(d["command_template"], value=o) for o in d["options"]]
    assert sent[0] == "menu"
    assert all(s.startswith("show ") for s in sent[1:])


def test_brightness_command():
    d = next(v for k, v in REC["discovery"].items() if "/number/" in k)
    assert render(d["command_template"], value=50.0) == "brightness 50"      # a number entity sends a float


def test_switch_and_button_payloads():
    sw = next(v for k, v in REC["discovery"].items() if "/switch/" in k)
    assert (sw["payload_on"], sw["payload_off"]) == ("slideshow on", "slideshow off")
    assert next(v for k, v in REC["discovery"].items() if "/button/" in k)["payload_press"] == "menu"

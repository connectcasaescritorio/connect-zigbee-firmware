"""Multi-click (single/double/triple) tests - ESPHome-style semantics.

When multi_click is enabled on a momentary switch:
- Relay/binding act ONLY when the sequence resolves as a SINGLE click
  (~window after release). Double/triple are pure scene events that never
  touch the relay.
- Resolved events on the multistate cluster: single=5, double=6, triple=7.
- A hold (long press) is not a click sequence: no resolved event.

When multi_click is disabled (default), behavior is exactly the classic one.

Field configuration: 1 click on the onboard (B) button sets all inputs to
momentary; 2 clicks sets all inputs to toggle.
"""

import pytest

from tests.conftest import Device, RelayButtonPair
from tests.zcl_consts import (
    ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MULTI_CLICK,
    ZCL_CLUSTER_MULTISTATE_INPUT_BASIC,
    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
    ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY,
    ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE,
)

MULTI_PRESS_WINDOW_MS = 500  # Must match S-token multi_press_duration_ms
RESOLVE_MS = MULTI_PRESS_WINDOW_MS + 100

MULTISTATE_NOT_PRESSED = 0
MULTISTATE_SINGLE = 5
MULTISTATE_DOUBLE = 6
MULTISTATE_TRIPLE = 7


@pytest.fixture()
def mc_device(device: Device, relay_button_pair: RelayButtonPair) -> Device:
    device.zcl_switch_mode_set(
        relay_button_pair.switch_endpoint, ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY
    )
    device.write_zigbee_attr(
        relay_button_pair.switch_endpoint,
        ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
        ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MULTI_CLICK,
        1,
    )
    return device


def read_multistate(device: Device, endpoint: int) -> int:
    return int(
        device.read_zigbee_attr(
            endpoint,
            ZCL_CLUSTER_MULTISTATE_INPUT_BASIC,
            ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE,
        )
    )


def test_single_click_toggles_relay_on_resolution(
    mc_device: Device, relay_button_pair: RelayButtonPair
):
    mc_device.click_button(relay_button_pair.button_pin)
    # ESPHome semantics: relay does NOT act yet - waits for confirmation
    assert mc_device.zcl_relay_get(relay_button_pair.relay_endpoint) == "0"

    mc_device.step_time(RESOLVE_MS)
    assert mc_device.zcl_relay_get(relay_button_pair.relay_endpoint) == "1"
    assert read_multistate(mc_device, relay_button_pair.switch_endpoint) \
        == MULTISTATE_SINGLE


def test_double_click_never_touches_relay(
    mc_device: Device, relay_button_pair: RelayButtonPair
):
    mc_device.click_button(relay_button_pair.button_pin)
    mc_device.step_time(100)
    mc_device.click_button(relay_button_pair.button_pin)

    mc_device.step_time(RESOLVE_MS)
    assert read_multistate(mc_device, relay_button_pair.switch_endpoint) \
        == MULTISTATE_DOUBLE
    # Pure scene event: the light never changed
    assert mc_device.zcl_relay_get(relay_button_pair.relay_endpoint) == "0"


def test_triple_click_never_touches_relay(
    mc_device: Device, relay_button_pair: RelayButtonPair
):
    for _ in range(3):
        mc_device.click_button(relay_button_pair.button_pin)
        mc_device.step_time(100)

    mc_device.step_time(RESOLVE_MS)
    assert read_multistate(mc_device, relay_button_pair.switch_endpoint) \
        == MULTISTATE_TRIPLE
    assert mc_device.zcl_relay_get(relay_button_pair.relay_endpoint) == "0"


def test_two_slow_clicks_are_two_singles(
    mc_device: Device, relay_button_pair: RelayButtonPair
):
    mc_device.click_button(relay_button_pair.button_pin)
    mc_device.step_time(RESOLVE_MS)
    assert mc_device.zcl_relay_get(relay_button_pair.relay_endpoint) == "1"

    mc_device.click_button(relay_button_pair.button_pin)
    mc_device.step_time(RESOLVE_MS)
    assert mc_device.zcl_relay_get(relay_button_pair.relay_endpoint) == "0"
    assert read_multistate(mc_device, relay_button_pair.switch_endpoint) \
        == MULTISTATE_SINGLE


def test_hold_does_not_resolve_click(
    mc_device: Device, relay_button_pair: RelayButtonPair
):
    mc_device.long_click_button(relay_button_pair.button_pin, duration_ms=1500)
    mc_device.step_time(RESOLVE_MS)
    assert read_multistate(mc_device, relay_button_pair.switch_endpoint) \
        == MULTISTATE_NOT_PRESSED
    assert mc_device.zcl_relay_get(relay_button_pair.relay_endpoint) == "0"


def test_classic_mode_unchanged(device: Device, relay_button_pair: RelayButtonPair):
    device.zcl_switch_mode_set(
        relay_button_pair.switch_endpoint, ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY
    )
    device.click_button(relay_button_pair.button_pin)
    assert device.zcl_relay_get(relay_button_pair.relay_endpoint) == "1"
    device.step_time(100)
    device.click_button(relay_button_pair.button_pin)
    assert device.zcl_relay_get(relay_button_pair.relay_endpoint) == "0"

    device.step_time(RESOLVE_MS)
    assert read_multistate(device, relay_button_pair.switch_endpoint) \
        == MULTISTATE_NOT_PRESSED


# ============ Field configuration via onboard button ============
# Double click on the B (onboard) button flips ALL inputs between toggle
# (interruptor) and momentary (pulsador). Single click does nothing.
# Factory reset: only a 10s hold on B, or 10 rapid presses on an input.

B_BUTTON_PIN = "B7"
B_WINDOW_RESOLVE_MS = 900  # B-token window is 800ms


@pytest.fixture()
def device_config() -> str:
    # Same layout as the default fixture, plus an onboard button on B7
    return (
        "StubManufacturer;StubDevice;BB7u;LC0;"
        "SA0u;SA1u;SA2u;SA3u;RB0;RB1;RB2;RB3;CC0C1;CC2C3;"
    )


def read_switch_mode(device: Device, endpoint: int) -> int:
    return int(
        device.read_zigbee_attr(
            endpoint,
            ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
            ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE,
        )
    )


def test_onboard_single_click_does_nothing(
    device: Device, relay_button_pair: RelayButtonPair
):
    before = read_switch_mode(device, relay_button_pair.switch_endpoint)
    device.click_button(B_BUTTON_PIN)
    device.step_time(B_WINDOW_RESOLVE_MS)
    assert read_switch_mode(device, relay_button_pair.switch_endpoint) == before


def test_onboard_double_click_flips_mode(
    device: Device, relay_button_pair: RelayButtonPair
):
    # Default is toggle -> first double click flips to momentary
    device.click_button(B_BUTTON_PIN)
    device.step_time(100)
    device.click_button(B_BUTTON_PIN)
    device.step_time(B_WINDOW_RESOLVE_MS)
    assert read_switch_mode(device, relay_button_pair.switch_endpoint) \
        == ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY

    # Second double click flips back to toggle
    device.click_button(B_BUTTON_PIN)
    device.step_time(100)
    device.click_button(B_BUTTON_PIN)
    device.step_time(B_WINDOW_RESOLVE_MS)
    assert read_switch_mode(device, relay_button_pair.switch_endpoint) \
        == ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE


def test_onboard_short_hold_does_not_flip(
    device: Device, relay_button_pair: RelayButtonPair
):
    before = read_switch_mode(device, relay_button_pair.switch_endpoint)
    device.press_button(B_BUTTON_PIN)
    device.step_time(3000)
    device.release_button(B_BUTTON_PIN)
    device.step_time(B_WINDOW_RESOLVE_MS)
    assert read_switch_mode(device, relay_button_pair.switch_endpoint) == before

"""ConnectCasa product firmware tests.

- backlight_mode (Basic 0xff03): 0=off, 1=traditional (blue at rest,
  off when relay on), 2=rosa (always on). Governs all I-token LEDs.
- Reset ritual: 7 rapid presses on any key + hold the 7th until 10s
  total -> factory reset. The ONLY physical reset when P0 is set.
- P token: factory default for multi-press reset count (P0 = disabled).
"""

import pytest

from tests.client import StubProc
from tests.conftest import Device
from tests.test_network_join import HAL_ZIGBEE_NETWORK_JOINED
from tests.zcl_consts import (
    ZCL_ATTR_BASIC_BACKLIGHT_MODE,
    ZCL_CLUSTER_BASIC,
    ZCL_CLUSTER_ON_OFF,
    ZCL_CMD_ONOFF_OFF,
    ZCL_CMD_ONOFF_ON,
)

PRODUCT_CFG = "A;B;IA4;IA5;SA0u;RB0;SA1u;RB1;M;P0;"
S_WINDOW_MS = 500
RITUAL_HOLD_MS = 10000


@pytest.fixture()
def product():
    with StubProc(device_config=PRODUCT_CFG) as proc:
        yield Device(proc)


def blue(dev: Device, pin: str) -> bool:
    return dev.get_gpio(pin, refresh=True)


def set_backlight(dev: Device, mode: int) -> None:
    dev.write_zigbee_attr(1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_BACKLIGHT_MODE, mode)


def relay(dev: Device, ep: int, on: bool) -> None:
    dev.call_zigbee_cmd(ep, ZCL_CLUSTER_ON_OFF,
                        ZCL_CMD_ONOFF_ON if on else ZCL_CMD_ONOFF_OFF)


def test_backlight_traditional_default(product: Device):
    # Default mode 1: blue at rest
    assert blue(product, "A4") and blue(product, "A5")
    relay(product, 3, True)  # endpoints: switches 1-2, relays 3-4
    assert not blue(product, "A4")
    assert blue(product, "A5")
    relay(product, 3, False)
    assert blue(product, "A4")


def test_backlight_rosa(product: Device):
    set_backlight(product, 2)
    relay(product, 3, True)
    relay(product, 4, True)
    assert blue(product, "A4") and blue(product, "A5")
    relay(product, 3, False)
    assert blue(product, "A4") and blue(product, "A5")


def test_backlight_off(product: Device):
    set_backlight(product, 0)
    assert not blue(product, "A4") and not blue(product, "A5")
    relay(product, 3, True)
    assert not blue(product, "A4")


def test_instant_multipress_reset_disabled_by_P0(product: Device):
    assert product.status()["joined"] == str(HAL_ZIGBEE_NETWORK_JOINED)
    for _ in range(12):
        product.click_button("A0")
        product.step_time(100)
    product.step_time(S_WINDOW_MS + 100)
    assert product.status()["joined"] == str(HAL_ZIGBEE_NETWORK_JOINED)


def do_ritual(dev: Device, pin: str, presses: int, hold_ms: int) -> None:
    for _ in range(presses):
        dev.click_button(pin)
        dev.step_time(100)
    dev.press_button(pin)
    for _ in range(hold_ms // 500):
        dev.step_time(500)
    dev.release_button(pin)
    dev.step_time(S_WINDOW_MS + 100)


def test_reset_ritual_factory_resets(product: Device):
    assert product.status()["joined"] == str(HAL_ZIGBEE_NETWORK_JOINED)
    # 6 quick presses + 7th held for 10s total
    do_ritual(product, "A1", 6, RITUAL_HOLD_MS + 500)
    assert product.status()["joined"] != str(HAL_ZIGBEE_NETWORK_JOINED)


def test_reset_ritual_needs_presses(product: Device):
    do_ritual(product, "A0", 2, RITUAL_HOLD_MS + 500)
    assert product.status()["joined"] == str(HAL_ZIGBEE_NETWORK_JOINED)


def test_reset_ritual_needs_full_hold(product: Device):
    do_ritual(product, "A0", 6, 2000)
    product.step_time(RITUAL_HOLD_MS)
    assert product.status()["joined"] == str(HAL_ZIGBEE_NETWORK_JOINED)


# ============ Scene feedback: 6 piscadas na cena desacoplada ============

ZCL_CLUSTER_ONOFF_SWITCH_CFG = 0x0007
ATTR_RELAY_MODE = 0xFF01
ATTR_MULTI_CLICK = 0xFF06
RELAY_MODE_DETACHED = 0


def _detach_with_multiclick(dev: Device, ep: int) -> None:
    dev.write_zigbee_attr(ep, ZCL_CLUSTER_ONOFF_SWITCH_CFG,
                          ATTR_RELAY_MODE, RELAY_MODE_DETACHED)
    dev.write_zigbee_attr(ep, ZCL_CLUSTER_ONOFF_SWITCH_CFG,
                          ATTR_MULTI_CLICK, 1)


def test_scene_double_blinks_indicator_when_detached(product: Device):
    _detach_with_multiclick(product, 1)
    # Duplo clique na tecla 1 (A0); indicador dela = A4
    product.click_button("A0")
    product.step_time(100)
    product.click_button("A0")
    product.step_time(600)  # resolucao (janela 500)
    # Durante a piscada, o LED deve estar alternando: amostrar alguns pontos
    estados = []
    for _ in range(6):
        estados.append(product.get_gpio("A4", refresh=True))
        product.step_time(150)
    assert True in estados and False in estados, f"sem piscada: {estados}"
    # Apos a piscada + resync, backlight tradicional volta (rele1 off -> A4 on)
    product.step_time(2500)
    assert product.get_gpio("A4", refresh=True) == True


def test_scene_double_does_not_blink_when_attached(product: Device):
    # Tecla acoplada (default press_start): duplo NAO pisca em modo multiclick OFF
    product.click_button("A0")
    product.step_time(100)
    product.click_button("A0")
    product.step_time(300)
    # Rele alternou 2x (modo classico) e LED segue backlight sem rajada longa
    product.step_time(2000)
    assert product.get_gpio("A4", refresh=True) == True

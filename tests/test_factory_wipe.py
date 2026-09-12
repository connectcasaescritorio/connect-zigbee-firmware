"""Faxina total via Zigbee: attr 0xff04, so dispara com o valor magico 90."""
import pytest
from tests.client import StubProc
from tests.conftest import Device
from tests.test_network_join import HAL_ZIGBEE_NETWORK_JOINED

CFG = "A;B;SA0u;RB0;SA1u;RB1;M;P0;"

def test_wrong_magic_does_nothing():
    with StubProc(device_config=CFG) as proc:
        dev = Device(proc)
        dev.write_zigbee_attr(1, 0x0000, 0xFF04, 1)
        dev.step_time(2000)
        assert dev.status()["joined"] == str(HAL_ZIGBEE_NETWORK_JOINED)

def test_magic_90_wipes():
    with StubProc(device_config=CFG) as proc:
        dev = Device(proc)
        dev.write_zigbee_attr(1, 0x0000, 0xFF04, 90)
        # schedule_full_reset(500) -> reset_all -> o stub encerra (reboot)
        died = False
        try:
            dev.step_time(1000)
            dev.status()
        except Exception:
            died = True
        assert died, "wipe nao disparou"

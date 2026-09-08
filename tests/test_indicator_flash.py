"""Tests for indicator LED flash on button press/release."""

from typing import Iterator

import pytest

from tests.client import StubProc
from tests.conftest import Device
from tests.zcl_consts import (
    ZCL_ATTR_ONOFF,
    ZCL_ATTR_ONOFF_INDICATOR_MODE,
    ZCL_CLUSTER_ON_OFF,
    ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED,
    ZCL_ONOFF_INDICATOR_MODE_SAME,
)


@pytest.fixture()
def indicator_device() -> Iterator[Device]:
    # One switch, one relay, indicator LED on A1
    cfg = "X;Y;SA0u;RB0;IA1;"
    with StubProc(device_config=cfg) as proc:
        yield Device(proc)




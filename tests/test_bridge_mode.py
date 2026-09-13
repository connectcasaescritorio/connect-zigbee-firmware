"""Ponte Tuya: ativa com modelo -BR, inerte nos produtos normais."""
from tests.client import StubProc
from tests.conftest import Device

BRIDGE_CFG = "3kjidznp;TS0601-BR;SB4u;RC1;SB5u;RD3;SC0u;RB6;M;P0;"
NORMAL_CFG = "A;B;SA0u;RB0;M;P0;"

def test_bridge_boots_with_br_model():
    with StubProc(device_config=BRIDGE_CFG) as proc:
        dev = Device(proc)
        dev.step_time(500)
        # Vivo, pareado, clusters de 3 teclas + 3 reles montados
        assert dev.status()["joined"]

def test_normal_product_sem_ponte():
    with StubProc(device_config=NORMAL_CFG) as proc:
        dev = Device(proc)
        dev.step_time(500)
        # Rele fisico segue funcionando (ponte inerte nao interfere)
        dev.click_button("A0")
        dev.step_time(450)
        assert dev.get_gpio("B0", refresh=True) == True

# Connect Zigbee Firmware

Firmware Zigbee da ConnectCasa para interruptores e módulos baseados em Telink TLSR825x.

Baseado no projeto tuya-zigbee-switch de romasku (https://github.com/romasku/tuya-zigbee-switch),
usado sob os termos da licença original (ver LICENSE), que permite uso, modificação,
distribuição e venda sem restrições.

Modificações em relação ao projeto original:
- Removido suporte Silabs/EFR32 (apenas Telink)
- Removidos geradores ZHA e HOMEd (apenas Zigbee2MQTT)
- device_db reduzido a dispositivos Telink
- Workflow de build simplificado (compila apenas boards selecionadas)

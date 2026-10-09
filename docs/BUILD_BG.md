# Компилиране от чисто копие

## Зависимости

- Python 3.10+ и `python -m pip install -r requirements.txt`.
- CMake и Ninja в PATH (`python -m pip install cmake ninja` е един вариант).
- Arm GNU Embedded toolchain с `arm-none-eabi-gcc` в PATH.
- Pico SDK 2.3.1, включително подмодулите; променливата `PICO_SDK_PATH`
  сочи към SDK. Проверен SDK commit: `079c6f39023649b154152db30f1d781e884879bc`.
- Pico SDK host tools pioasm/picotool или среда, в която SDK може да ги
  компилира. При готови пакети задайте `pioasm_DIR` и `picotool_DIR` чрез
  повторяемия аргумент `--cmake-option NAME=VALUE`.
- За текста: Arial Bold на Windows или DejaVu Sans Bold; алтернативно
  `PANEL_FONT` сочи към ваш TTF с кирилица. Шрифт не се разпространява тук.

```powershell
git clone --branch 2.3.1 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git
$env:PICO_SDK_PATH=(Resolve-Path pico-sdk).Path
python build.py usb --board vcc-gnd_yd-rp2040_8m
```

`build.py` е публичният общ build entrypoint. Старите `pico/build*.ps1`
остават за оригиналната Windows инсталация под `%USERPROFILE%/.cache/pico-panel`.
Всеки режим има отделна директория `pico/generated/build-<вариант>/` и изрично
нулира чуждите CMake флагове. USB може да се компилира и с `--board pico_w`;
Solar и четирипанелните тестове изискват `pico_w`.

## Собствен Wi-Fi и ключове

Създайте локален, игнориран `wifi.local.json` със собствени стойности:
```json
{"ssid":"YOUR_WIFI_NAME","password":"YOUR_WIFI_PASSWORD","router":"192.168.1.1"}
```
```powershell
python pico/prepare_solar_wifi.py --config wifi.local.json
python build.py solar
python build.py chain-text
python build.py chain-unrotated
python build.py chain-arrow
```
`ota_package.py` генерира нови частни ключове само ако няма съществуващи.
Пазете `pico/generated/panel-admin.json`: за бъдещо OTA трябва същият ключ.
Не споделяйте компилираните образи. Няма нужда от Wi-Fi конфигурация за USB-only.

При първа инсталация на OTA вариант копирайте общия `panel_initial_usb.uf2`
в BOOTSEL диска. Това променя firmware-а на устройството. За последващ OTA:
```powershell
python pico/panel_admin.py --host YOUR_PANEL_IP --upload pico/generated/build-solar/pico_panel.panel --wait-version 0.4.2-solar-readings
```
Съществуващият HTTP сървър има една активна връзка. Скриптът изчаква между
OTA пакетите; спрете други polling клиенти по време на обновяване. Не прекъсвайте
захранването и оставете физически достъп до BOOTSEL за аварийно възстановяване.

## Исторически варианти

В `snapshots/static-0.1` и `snapshots/usb-0.2` има отделни изходни дървета.
Техните Windows build скриптове очакват оригиналната toolchain подредба.
Изпълнявайте ги от корена на съответния snapshot. Това са архиви, не текущият
Solar/OTA код; нямат съвременните настройки и не бива да се качват през Solar OTA.

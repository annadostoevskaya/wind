# WIND

Production-oriented firmware for the STM32F407VG-based wind monitoring node.

Прошивка измеряет трёхфазные напряжения и токи, контролирует вращение и питание,
опрашивает до четырёх DS18B20 и передаёт бинарную телеметрию через RAK3172 по
LoRaWAN. Код поддерживает управляемый STOP-цикл, восстановление после ошибок
ADC/DMA/UART и отключение только при одновременном подтверждении низкой батареи
и нулевого вращения.

## Документация

- [Инженерное руководство](docs/WIND_FIRMWARE.md) — архитектура, аппаратный
  контракт, протокол, сборка, тестирование и диагностика.
- [Production checklist](docs/PRODUCTION_CHECKLIST.md) — обязательные проверки
  перед прошивкой партии и публикацией release.
- [Оформленная PDF-версия](output/pdf/wind-firmware-guide.pdf) — руководство для
  печати и передачи производству.
- [Changelog](CHANGELOG.md) — история значимых изменений.

PDF воспроизводимо собирается из Markdown:

```powershell
python -m pip install -r tools/requirements-docs.txt
python tools/generate_documentation_pdf.py
```

## Быстрый старт

Требования:

- CMake 3.22 или новее;
- Ninja;
- ARM GNU Toolchain с префиксом `arm-none-eabi-`;
- обычный GCC и PowerShell для host-тестов.

```powershell
cmake --preset debug
cmake --build --preset debug

cmake --preset release
cmake --build --preset release
```

Production-образы появляются в `build/release/`:

- `wind.elf` — ELF с символами;
- `wind.hex` — Intel HEX;
- `wind.bin` — бинарный образ;
- `wind.map` — карта линковки.

Host-сценарии и статические аппаратные инварианты:

```powershell
gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror `
  -I Core/Inc tests/app_logic_scenarios.c Core/Src/app_logic.c `
  -o build/tests/app_logic_scenarios.exe

./build/tests/app_logic_scenarios.exe
powershell -ExecutionPolicy Bypass -File tests/verify_firmware_invariants.ps1
```

## Критические ограничения

- Физические GPIO, ADC channel/rank, UART, DMA, EXTI и TIM2 являются аппаратным
  контрактом. Их нельзя менять без схемы, ревизии платы и повторной валидации.
- LoRaWAN payload имеет фиксированный размер 33 байта и отправляется на fPort 2.
- `PWR_OFF` удерживается в HIGH в штатном режиме. Аварийная последовательность
  HIGH 1 с / LOW 1 с предполагает, что после снятия питания MCU перестанет работать.
- В репозитории отсутствует принципиальная схема. Тракт батареи через
  `ON_PWR_DET` и ADC2_IN3 подтверждён кодом и историей проекта, но должен быть
  сопоставлен со схемой перед серийным выпуском.

## Структура

```text
Core/                 application, interrupts, startup and board headers
Drivers/              STM32CubeF4 CMSIS and HAL sources
cmake/                ARM cross-toolchain definition
docs/                 engineering and release documentation
tests/                host scenarios and firmware invariant checks
tools/                reproducible documentation tooling
output/pdf/            approved printable documentation
wind.ioc              STM32CubeMX hardware metadata
CMakeLists.txt         reproducible firmware build
CMakePresets.json      warning-clean Debug and Release configurations
```

Generated build output is intentionally excluded from Git. CI repeats host tests,
invariant checks and the Release build on every push and pull request.

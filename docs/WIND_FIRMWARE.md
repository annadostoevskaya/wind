# WIND

## Инженерное руководство по прошивке

**Целевая платформа:** STM32F407VGTx
**Радиомодуль:** RAK3172 / LoRaWAN
**Документ:** production engineering guide
**Дата ревизии:** 25 августа 2026

> **Статус документа.** Руководство описывает проверенное состояние репозитория и
> аппаратный контракт прошивки. Изменение GPIO, ADC channel/rank, DMA, EXTI,
> таймеров или формата payload требует отдельной аппаратной ревизии.

<!-- pagebreak -->

## 1. Назначение и границы системы

WIND - автономный узел измерения и телеметрии для трёхфазной ветроэнергетической
установки. Прошивка собирает аналоговые и температурные данные, контролирует
вращение, оценивает состояние питания и передаёт компактный бинарный пакет через
RAK3172. Между измерениями устройство может переходить в STOP для снижения
потребления.

Основные функции:

- RMS напряжения и тока по фазам A, B и C;
- подсчёт оборотов по импульсам датчика, 6 импульсов на оборот;
- чтение аналоговых каналов X, Y, Z и напряжения батареи;
- опрос до четырёх DS18B20 с CRC и фиксированным порядком ROM;
- JOIN и uplink через RAK3172 с поддержкой старых и текущих RUI3-событий;
- controlled STOP/wakeup, контроль отсутствия вращения и аварийное отключение;
- диагностические флаги для ADC, UART, сети, температур и батареи.

### 1.1 Что считается аппаратным контрактом

Следующие параметры нельзя менять как «рефакторинг»:

- физические номера GPIO и alternate functions;
- USART1 для RAK3172 и USART2 для debug;
- порядок ADC1 `VA, VB, VC, IA, IB, IC`;
- ADC2 channels `Z, Y, X, BAT`;
- DMA2 Stream0 / Channel0, circular, halfword;
- TIM2 PSC 8399, ARR 3 и TRGO update;
- EXTI12 и общий обработчик EXTI15_10;
- аналоговые коэффициенты, RMS-формулы и 33-байтовый payload.

### 1.2 Ограничение исходных данных

В репозитории отсутствует принципиальная схема платы. Связь батарейного делителя
с `ON_PWR_DET` и ADC2_IN3 подтверждается текущей прошивкой и историей проекта, но
перед серийным выпуском должна быть сопоставлена со схемой либо измерена на плате.
Прошивка намеренно не подменяет этот тракт предположениями об `ON_AX` или `ON_AN`.

<!-- pagebreak -->

## 2. Архитектура выполнения

Приложение построено как неблокирующий конечный автомат. ISR выполняют только
короткую фиксацию событий, а обработка, UART logging, вычисления RMS и переходы
состояний выполняются в main context.

```text
TIM2 TRGO -> ADC1 sequence -> DMA circular -> half/full events
                                            |
                                            v
                                   RMS accumulation/window
                                            |
EXTI12 -> pulse counter -> rotation window + battery gate
                                            |
ADC2 + DS18B20 -----------------------------+
                                            |
                                            v
                                  33-byte payload / fPort 2
                                            |
                                            v
USART1 <-> RAK3172 -> JOIN / SEND final events -> LoRaWAN
```

### 2.1 Состояния верхнего уровня

| Состояние | Назначение | Условие выхода |
|---|---|---|
| STARTUP | Ранняя фиксация питания и запуск служб | Начало power-cycle RAK |
| RAK_OFF_WAIT | Гарантированная пауза питания модема | Включение ON_RAK |
| RAK_BOOT | Ожидание загрузки RAK3172 | Переход к JOINING |
| JOINING | JOIN/NJS FSM и контроль батареи | Финальный JOINED |
| ACTIVE_POWER_SETTLE | Стабилизация измерительных трактов | Старт ADC/DMA/TIM2 |
| ACTIVE | Измерения, датчики и uplink | Потеря сети или STOP condition |
| STOP | RTC WUT и low-power режим | RTC или другой разрешённый IRQ |
| WAKE_BATTERY | Проверка питания после wakeup | Проверка вращения |
| WAKE_ROTATION_SETTLE | Включение ON_FR | Новое 10-секундное окно |
| WAKE_ROTATION_CHECK | Подсчёт свежих импульсов | ACTIVE либо повторный STOP |
| EMERGENCY | Управляемое снятие PWR_OFF | Физическое исчезновение питания |

### 2.2 Прерывания и владение данными

- DMA half/full callbacks выставляют pending bits и увеличивают generation.
- `pending == 0` означает штатное отсутствие работы, а не ошибку.
- Одновременные half/full bits или повтор одной половины означают повреждённое
  окно; окно отбрасывается без публикации смешанных данных.
- ADC hardware error перезапускает только ADC/DMA/TIM2 acquisition. Независимое
  окно вращения сохраняется.
- RTC handler очищает WUTF и EXTI22 и делегирует событие приложению без UART и
  длительных операций.
- EXTI15_10 остаётся включённым; при необходимости маскируется только EXTI12.

<!-- pagebreak -->

## 3. Аппаратная конфигурация

### 3.1 GPIO и аналоговые входы

| Сигнал | MCU pin | Роль |
|---|---|---|
| Z | PA0 / ADC2_IN0 | Аналоговый канал Z |
| Y | PA1 / ADC2_IN1 | Аналоговый канал Y |
| X | PA2 / ADC2_IN2 | Аналоговый канал X |
| FIVEV_AN | PA3 / ADC2_IN3 | Батарейный/питающий ADC-вход |
| V_A | PA4 / ADC1_IN4 | Напряжение фазы A |
| I_A | PA5 / ADC1_IN5 | Ток фазы A |
| V_B | PA6 / ADC1_IN6 | Напряжение фазы B |
| I_B | PA7 / ADC1_IN7 | Ток фазы B |
| V_C | PC4 / ADC1_IN14 | Напряжение фазы C |
| I_C | PC5 / ADC1_IN15 | Ток фазы C |
| Freqency | PB12 / EXTI12 | Импульсы вращения |
| ON_FR | PB13 | Питание/разрешение датчика вращения |
| ON_RAK | PC6 | Питание RAK3172 |
| ON_AN | PC10 | Питание аналогового тракта |
| DS | PC11 | 1-Wire DS18B20 |
| ON_PWR_DET | PC12 | Питание детектора/делителя батареи |
| PWR | PD0 | Вход контроля внешнего питания |
| PWR_OFF | PD3 | Удержание и аварийное отключение питания |
| ON_AX | PE2 | Питание вспомогательного аналогового тракта |

### 3.2 UART

| Интерфейс | Pins | Настройки | Назначение |
|---|---|---|---|
| USART1 | PA9 TX, PA10 RX | 115200 8N1, no flow control | RAK3172 |
| USART2 | PD5 TX, PD6 RX | 115200 8N1, no flow control | Debug log |

<!-- pagebreak -->

### 3.3 ADC1, DMA и TIM2

ADC1 выполняет последовательность из шести conversion ranks:

| Rank | Channel | Индекс DMA | Измерение |
|---:|---:|---:|---|
| 1 | ADC1_IN4 | 0 | VA |
| 2 | ADC1_IN6 | 1 | VB |
| 3 | ADC1_IN14 | 2 | VC |
| 4 | ADC1_IN5 | 3 | IA |
| 5 | ADC1_IN7 | 4 | IB |
| 6 | ADC1_IN15 | 5 | IC |

DMA2 Stream0 / Channel0 работает в circular mode, memory increment enabled,
peripheral и memory alignment - halfword. Буфер содержит 6000 `uint16_t`, то есть
12 000 байт SRAM и две половины по 500 полных шестиканальных последовательностей.

### 3.4 Частота дискретизации

При HSE 8 МГц система работает на 72 МГц. APB1 имеет делитель 2, поэтому timer
clock TIM2 снова равен 72 МГц.

```text
Fs = 72 000 000 / ((PSC + 1) * (ARR + 1))
   = 72 000 000 / (8400 * 4)
   = 2142.857143 sequence/s
```

Отсюда:

- 42,857 выборки каждого канала на период 50 Гц;
- 5000 выборок каждого канала на RMS-окно;
- окно RMS 2,333333 с, или 116,667 периода 50 Гц.

<!-- pagebreak -->

## 4. Измерения и питание

### 4.1 RMS

Для каждого канала накапливаются сумма и сумма квадратов. DC component удаляется
через variance:

```text
mean     = sum / N
variance = sum_sq / N - mean * mean
rms_adc  = sqrt(max(variance, 0))
```

Базовые коэффициенты:

| Параметр | Значение |
|---|---:|
| VREF | 3,0 В |
| ADC_MAX | 4095 |
| Analog midpoint | 1,5 В |
| Current shunt | 0,016 Ом |
| Stage-1 current gain | 100000 / 8200 |
| Total current gain | stage-1 / 2 |
| Voltage divider | 100 кОм / 10 кОм |
| Battery divider | 10 кОм / 13 кОм |
| Battery correction | 1,10 |
| Low battery threshold | 3,6 В |

### 4.2 Батарея

Последовательность надёжного чтения:

1. Включить `ON_PWR_DET` и выдержать 50 мс.
2. Отбросить первую conversion после переключения ADC2 channel.
3. Усреднить 8 успешных conversion.
4. При HAL error повторить всю попытку, максимум 3 раза.
5. Считать invalid только HAL/config/poll failure.

Raw-коды 0 и 4095 являются валидными результатами при `HAL_OK`. Код 0 должен
привести к низкому напряжению и аварийному решению, а 4095 - к saturation flag,
но не к ложной ошибке ADC.

### 4.3 Вращение и STOP

Шесть импульсов соответствуют одному обороту. Основное отчётное окно близко к
RMS-окну (2333 мс). Для решения о STOP используется отдельное 10-секундное окно.
После RTC wakeup включается `ON_FR` и выполняется ещё одно независимое
10-секундное окно. Поэтому полный цикл подтверждения нулевого вращения занимает
примерно 20 секунд плюс допуск LSI и короткие settle/processing intervals.

LSI используется как номинальный источник RTC WUT. Прошивка не выполняет
необоснованную «калибровку» без независимого эталонного таймера.

<!-- pagebreak -->

## 5. RAK3172 и LoRaWAN

### 5.1 JOIN

JOIN-команда фиксирована:

```text
AT+JOIN=1:0:10:1\r\n
```

Правила FSM:

- `OK` означает только принятие AT-команды и не завершает JOIN;
- успех требует финального `JOINED` либо подтверждения `AT+NJS:1`;
- `AT_BUSY_ERROR` и duty-cycle restriction используют backoff и не увеличивают
  счётчик реальных radio failures;
- после 10 завершённых неудач выполняется проверка батареи;
- попытки не имеют искусственного общего лимита;
- ответы классифицируются как полные строки, без substring false positives.

### 5.2 Uplink

Период по умолчанию - 300 000 мс. Значение можно переопределить через
`UPLINK_PERIOD_MS` только после проверки региона, data rate, airtime и duty cycle.
Успех SEND определяется по финальному событию, например `TX_DONE` или
`SEND_*_OK`; bare `OK` не завершает передачу. BUSY запускает отдельный backoff.

### 5.3 Формат payload

Payload имеет 33 байта, little-endian, fPort 2.

| Bytes | Поле | Формат / масштаб |
|---:|---|---|
| 0 | Format version | `2` |
| 1-2 | UA RMS | uint16, x100 |
| 3-4 | UB RMS | uint16, x100 |
| 5-6 | UC RMS | uint16, x100 |
| 7-8 | IA RMS | uint16, x100 |
| 9-10 | IB RMS | uint16, x100 |
| 11-12 | IC RMS | uint16, x100 |
| 13-14 | Revolutions | uint16, x100 |
| 15-16 | X raw | uint16 |
| 17-18 | Y raw | uint16 |
| 19-20 | Z raw | uint16 |
| 21-22 | Battery | uint16, volts x100 |
| 23 | DS18B20 validity | bit mask slots 0..3 |
| 24-25 | Temperature 0 | int16, °C x100 |
| 26-27 | Temperature 1 | int16, °C x100 |
| 28-29 | Temperature 2 | int16, °C x100 |
| 30-31 | Temperature 3 | int16, °C x100 |
| 32 | Device status | bit field |

| Status bit | Маска | Значение |
|---:|---:|---|
| 0 | 0x01 | Повреждено/отброшено ADC window |
| 1 | 0x02 | ADC saturation |
| 2 | 0x04 | Ошибка auxiliary ADC |
| 3 | 0x08 | Ошибка DS18B20/CRC |
| 4 | 0x10 | Потеря LoRaWAN membership |
| 5 | 0x20 | Финальная ошибка SEND |
| 6 | 0x40 | UART overflow или повреждённая строка |
| 7 | 0x80 | Невалидное измерение батареи |

Серверный декодер находится в `chirpstack_decoder.js` и является частью
протокольного контракта.

<!-- pagebreak -->

## 6. Сборка и артефакты

### 6.1 Требования

- CMake 3.22+;
- Ninja;
- ARM GNU Toolchain (`arm-none-eabi-gcc`, `objcopy`, `size`);
- GCC и PowerShell для host-проверок.

### 6.2 Debug

```powershell
cmake --preset debug
cmake --build --preset debug
```

Debug использует `-O0 -g3`, stack-usage reports и строгие предупреждения с
`-Werror`. Проверенная сборка занимает 50 516 байт Flash и 18 528 байт RAM.

### 6.3 Release

```powershell
cmake --preset release
cmake --build --preset release
```

Release использует `-Os -g1`. Проверенный размер:

| Регион | Использовано | Доступно | Доля |
|---|---:|---:|---:|
| FLASH | 33 152 Б | 1 МиБ | 3,16% |
| RAM | 18 520 Б | 128 КиБ | 14,13% |
| CCMRAM | 0 Б | 64 КиБ | 0% |

Результат находится в `build/release/`: `wind.elf`, `wind.hex`, `wind.bin` и
`wind.map`. В Git эти generated-файлы не добавляются. Release package должен
хранить ELF и MAP вместе с программируемым HEX/BIN для последующей диагностики.

### 6.4 STM32CubeIDE

Проект сохраняет `.project`, `.cproject`, `wind.ioc`, `wind-debug.launch` и
`wind-debug.cfg`. CubeIDE может генерировать локальные `Debug/` и `Release/`, но
эти каталоги игнорируются. После любого CubeMX regeneration обязателен полный
diff review: hardware metadata не даёт права автоматически менять рабочую
инициализацию или USER CODE.

<!-- pagebreak -->

## 7. Верификация и выпуск

### 7.1 Host-сценарии

```powershell
gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror `
  -I Core/Inc tests/app_logic_scenarios.c Core/Src/app_logic.c `
  -o build/tests/app_logic_scenarios.exe

./build/tests/app_logic_scenarios.exe
```

Сценарии покрывают JOIN first try, десять failures, low battery, NJS recovery,
BUSY, точную классификацию строк, SEND outcomes, raw 0/4095, HAL failures,
DMA half/full/overrun/generation, shared EXTI, RTC reload, rotation и UART ring.

### 7.2 Аппаратные инварианты

```powershell
powershell -ExecutionPolicy Bypass -File tests/verify_firmware_invariants.ps1
```

Скрипт сравнивает критическую семантику с Git HEAD и одновременно проверяет
production metadata: pins, ADC ranks, DMA, UART, TIM2, RTC vector/handler,
STOP recovery, payload, linker stack reserve, CMake presets и CI workflow.

### 7.3 CI

`.github/workflows/firmware-ci.yml` запускает на push и pull request:

1. host compilation с warnings-as-errors;
2. сценарные тесты;
3. PowerShell invariant verifier;
4. CMake Release build;
5. публикацию ELF/HEX/BIN/MAP как CI artifact.

### 7.4 Обязательная проверка на плате

Автоматические тесты не заменяют физическую проверку:

- соответствие схемы и pinout, особенно battery path;
- реальная частота TIM2/TRGO и корректность фаз ADC;
- STOP current, RTC WUT interval и температурный дрейф LSI;
- JOIN/SEND на целевом регионе и data rate;
- реакция силовой части на `PWR_OFF`;
- end-to-end decoding 33-байтового пакета сервером.

Полный пошаговый список находится в `docs/PRODUCTION_CHECKLIST.md`.

<!-- pagebreak -->

## 8. Диагностика

| Симптом | Проверить | Ожидаемое действие |
|---|---|---|
| JOIN завис после OK | Финальное событие и NJS query | Не считать OK успехом; запросить membership |
| Частые BUSY | Airtime, duty cycle, data rate | Использовать busy backoff, не считать radio failure |
| Нет RMS | DMA IRQ, half/full generation, status bit 0 | Отбросить только повреждённое окно |
| ADC saturation | Raw 0/4095 и status bit 1 | Проверить входной диапазон и аналоговый тракт |
| Battery invalid | HAL/config/poll logs, status bit 7 | Повторить до трёх полных попыток |
| Нулевые обороты | EXTI12, ON_FR и два независимых окна | Не переходить в STOP по неполному окну |
| UART overflow | Status bit 6 и границы строк | Отбросить повреждённую строку до EOL |
| Wakeup нестабилен | LSI, WUTF, EXTI22, clocks after STOP | Измерить LSI; проверить восстановление HSE/PLL/SysTick |

### 8.1 Логи загрузки

При старте debug UART должен показать:

- идентификатор `wind`;
- SYSCLK/HCLK/PCLK после настройки clock tree;
- фактические TIM2 clock, PSC, ARR, Fs и длительность RMS window;
- результат запуска RTC WUT/LSI;
- переходы FSM с причиной.

### 8.2 Сохранение доказательств выпуска

Для каждого production release рекомендуется сохранять:

- Git commit и tag;
- версии CMake, ARM GCC и STM32CubeF4;
- CI log и результаты host/invariant checks;
- `wind.elf`, `wind.hex`, `wind.bin`, `wind.map` и SHA-256;
- результаты стендовых измерений clock, STOP current, RTC interval и radio test;
- эту PDF-ревизию и заполненный production checklist.

---

**WIND firmware documentation**
Единственный источник содержимого PDF: `docs/WIND_FIRMWARE.md`.

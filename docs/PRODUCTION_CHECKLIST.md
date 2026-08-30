# WIND production checklist

Используйте этот список перед созданием release tag, прошивкой партии или передачей
образов производству. Каждый пункт должен иметь ответственного и подтверждение.

## 1. Исходники и конфигурация

- [ ] Рабочее дерево Git чистое; release создаётся из подписанного commit/tag.
- [ ] В diff отсутствуют незапланированные изменения `wind.ioc`, `main.h`, linker
      scripts и `chirpstack_decoder.js`.
- [ ] Параметры LoRaWAN и период uplink соответствуют региону и data rate.
- [ ] Ключи/учётные данные устройства не хранятся в репозитории и build-логах.
- [ ] Версия STM32CubeF4 и ARM toolchain записана в release notes.

## 2. Автоматическая проверка

- [ ] `cmake --preset debug && cmake --build --preset debug` проходит без warnings.
- [ ] `cmake --preset release && cmake --build --preset release` проходит без warnings.
- [ ] Все host-сценарии `tests/app_logic_scenarios.c` проходят.
- [ ] `tests/verify_firmware_invariants.ps1` завершён с итогом PASS.
- [ ] В `wind.map` нет overflow и неожиданных крупных секций.
- [ ] `wind.elf`, `wind.hex` и `wind.bin` получены из одного release commit.

## 3. Проверка на целевой плате

- [ ] Подтверждены HSE 8 МГц, SYSCLK/TIM2 72 МГц и Fs около 2142,857 Гц.
- [ ] Проверены ADC-фазы VA/VB/VC/IA/IB/IC и отсутствие перестановки каналов.
- [ ] Проверены ADC2 X/Y/Z/BAT и физический тракт `ON_PWR_DET -> ADC2_IN3`.
- [ ] Raw-коды батареи 0 и 4095 не интерпретируются как ошибка HAL.
- [ ] Проверены JOIN first try, длительная потеря сети и восстановление через NJS.
- [ ] Проверены SEND success, BUSY/duty-cycle backoff и потеря membership.
- [ ] Проверены DMA half/full, искусственный overrun и восстановление измерений.
- [ ] Проверены STOP/wakeup, окно ON_FR и EXTI12 при активных shared EXTI lines.
- [ ] Проверена таблица отключения: только `BAT < 3,6 В AND pulses == 0` включает
  `PWR_OFF`; каждое условие по отдельности оставляет устройство в работе.
- [ ] Измерен реальный период RTC WUT на температурном диапазоне изделия.
- [ ] Проверена аварийная последовательность `PWR_OFF` на реальной силовой схеме.

## 4. Серверная совместимость

- [ ] Тестовый 33-байтовый uplink корректно разобран `chirpstack_decoder.js`.
- [ ] fPort равен 2, byte 0 равен версии формата 2.
- [ ] Little-endian поля, масштабы x100, validity mask и status bits совпадают с
      документацией.
- [ ] Сервер допускает повторную отправку после BUSY и не считает её новым измерением.

## 5. Release package

- [ ] Зафиксированы SHA-256 для ELF/HEX/BIN.
- [ ] Приложены release notes, PDF-руководство и известные ограничения.
- [ ] Сохранены ELF и MAP для последующей диагностики crash dump.
- [ ] HEX/BIN промаркированы версией продукта вне самого имени target `wind`.
- [ ] Выполнена выборочная read-back verification после программирования.

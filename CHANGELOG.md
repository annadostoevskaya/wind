# Changelog

Все значимые изменения проекта документируются в этом файле. Формат основан на
Keep a Changelog; версия прошивки назначается владельцем продукта при выпуске.

## Unreleased

### Changed

- Проект и все IDE/debug-артефакты переименованы в `wind`.
- Generated-каталоги `Debug/` и `Release/` исключены из репозитория.
- Добавлены воспроизводимые CMake/Ninja presets для Debug и Release.
- Добавлена CI-проверка host-сценариев, аппаратных инвариантов и Release-сборки.
- Добавлены `.gitignore`, `.gitattributes` и production-документация.

### Removed

- Удалены отслеживаемые объектные файлы, dependency-файлы, отчёты анализа,
  ELF/HEX/MAP/LIST и generated makefiles STM32CubeIDE.

## 2026-08-25 - Firmware audit baseline

### Fixed

- Исправлено восстановление системной частоты и SysTick после STOP.
- Перезапуск ADC/DMA отделён от независимого окна контроля вращения.
- CubeMX metadata синхронизирована с фактическими ADC, DMA, EXTI и TIM2.
- Резерв стека увеличен до 4 КиБ.
- Устранён абсолютный путь к linker script и несовместимые build-флаги.

### Verified

- JOIN/SEND FSM и точная классификация ответов RAK3172.
- Граничные значения батареи, ошибки HAL, DMA-overrun и shared EXTI.
- 33-байтовый LoRaWAN payload и аппаратные/измерительные инварианты.

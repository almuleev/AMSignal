# Сборка и CLI

[Документация](README_RU.md) · [English](BUILD_EN.md)

## Готовая программа

Для обычной работы скачайте GUI EXE из [выпусков](https://github.com/almuleev/AMSignal/releases/latest). CLI не публикуется отдельным вложением. Архивы исходников GitHub содержат код для его сборки.

## Зависимости Windows

Нужны Git, Windows PowerShell и MSYS2/MinGW с `g++`, `windres` и библиотеками Windows. Если `g++` отсутствует в PATH, скрипты добавляют `C:\msys64\ucrt64\bin`. Программа использует C++17, Win32 и GDI/GDI+; среды Qt и LabVIEW не требуются.

Выполните команды из корня репозитория:

```powershell
# Собрать GUI
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build_gui.ps1
# Собрать CLI
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build_cli.ps1
```

Имя GUI — `AMSignal-<версия>-x64.exe`; версия и признак dirty берутся из `git describe`. Результат сборки CLI — `lvm_reader.exe`. Если запущенное приложение блокирует GUI EXE, используйте отдельный каталог:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build_gui.ps1 -OutputDirectory dist/my-build
```

## Примеры CLI

```powershell
.\lvm_reader.exe --help
.\lvm_reader.exe .\examples\demo-signals.txt --info --stats
.\lvm_reader.exe .\examples\demo-signals.txt --fft --peaks 5
.\lvm_reader.exe .\examples\demo-signals.txt --start 1 --end 3 --channels 1,2 --csv selected.csv
.\lvm_reader.exe .\examples\demo-signals.txt --fft-csv spectrum.csv
```

Нумерация каналов в `--channels` начинается с **1**. `--start` и `--end` задают временные границы до статистики, экспорта и FFT. `--fft-samples 0` использует все выбранные отсчёты, положительное значение ограничивает их число. `--monotonic` восстанавливает монотонную шкалу для файлов с секциями. CLI предоставляет статистику, CSV и поиск FFT-пиков; интерактивный FRF доступен в GUI.

## Проверки

В оболочке MSYS2/MinGW `make all test test-gui gui` собирает обе программы и запускает core/GUI-регрессии. GUI-тесты можно также собрать и выполнить через PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build_gui.ps1 -Test
```

Для CLI/ядра в Linux используйте `make all test`; нативный GUI предназначен для Windows. Точные правила выбора проверок и выпуска — в [справочнике проверок](development/TESTING.md). Перед проверкой изменённых исходников пересоберите соответствующий тестовый EXE.

## Структура репозитория

| Путь | Ответственность |
| --- | --- |
| `gui_*.cpp/.hpp` | Нативный интерфейс и работа с документами |
| `main.cpp` | Аргументы и действия CLI |
| `lvm_parser.*`, `data_io.*`, `recording_source.*` | Парсинг и чтение исходников |
| `analysis.*`, `fft.*`, `frf_*`, `filter_engine.*` | Численный анализ и обработка |
| `tests/` | Регрессии ядра и GUI |
| `examples/` | Воспроизводимый пользовательский пример |
| `docs/` | Пользовательские инструкции, справочники разработки и история выпусков |

Владельцы модулей описаны в [архитектуре](ARCHITECTURE.md), порядок внесения изменений — в [правилах участия](../CONTRIBUTING.md).

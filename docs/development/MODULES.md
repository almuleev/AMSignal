# Модули и владельцы поведения

[Карта архитектуры](../ARCHITECTURE.md) · [Контракты](CONTRACTS.md) · [Проверки](TESTING.md)

Все перечисленные исходники находятся в корне репозитория; имя без расширения
обозначает `.cpp/.hpp`. Ищи символ сначала в выбранном модуле и его заголовке,
затем просматривай вызывающие места. Таблица не требует читать все файлы строки.

| Область | Владелец и ближайшие зависимости |
| --- | --- |
| CLI | `main.cpp`: аргументы, чтение, статистика, FFT и CSV |
| Парсинг и файловый ввод/вывод | `lvm_parser`: Dataset, упорядоченный блочный разбор чисел, нормализация времени, ScanIndex, диапазоны; `data_io`: форматы, физические размеры записей, Unicode-пути, безопасная запись |
| Источник записи | `recording_source`: независимые чтения, версия файла, resident snapshots; `analysis_range.hpp`: разрешение участка |
| Обзор и точный Snap | `recording_overview`, `overview_worker`: индекс/view/разрывы; `gui_overview`, `gui_overview_render`, `gui_overview_snap`: связь с UI |
| Общий дисковый кэш | `disk_cache`: LRU, owner-lock, quota, lease; `scan_index_cache`: AMSCIDX1 и managed overview; `gui_recording_cache`: каталог GUI |
| RAM и исполнение | `resource_budget.hpp`, `analysis_resources.hpp`, `analysis_executor.hpp` (общий пул парсера/FFT/FRF); `gui_resources`: бюджет загрузки |
| Загрузка и отмена | `gui_loading`, `gui_loading_drop`: очередь файлов, прогресс, drop и приём результата |
| Документы | `gui_documents`, `gui_state`: App/DocumentState, переключение/закрытие, готовые кэши и контрольная точка сохранения |
| Сообщения и команды | `gui_main`: маршрутизация WndProc; `gui_window`: окна и контролы; `gui_commands`: меню, кнопки, смена режима |
| Панель и раскладка | `gui_layout`: геометрия и видимость; `gui_side_panel`: синхронизация контролов и вкладок |
| Общий viewport | `gui_frequency_view`: поля только снизу «Каналов», секунды Signal и Гц FFT/FRF; Enter, валидация и draft |
| Участок анализа | `gui_analysis_source`: общий вход FFT/FRF; `gui_analysis_range`: границы, drag, Snap и Undo |
| FFT | `analysis`, `fft`, `sampling.hpp`, `disk_spectrum`; `spectrum_worker`, `gui_spectrum`: фон и кэш |
| FRF | `frf_analysis`: H1/Direct/coherence; `frf_stream`: подготовленный RAM/дисковый вход; `frf_worker`: отменяемое задание |
| Панель и графики FRF | `gui_frf`: роли, метод/L, запуск; `gui_frf_render`: КД, средняя опора, частотная навигация и Snap |
| Формулы и фильтр | `formula_engine`, `filter_engine`; `gui_processing`: применение, ревизии и кэши |
| Обработка источника | `gui_recording_processing`: блочная обработка; `gui_filter_state_cache`: постоянные continuation фильтра |
| Графики и шкалы | `gui_render`, `gui_render_data`, `minmax_index.hpp`, `gui_time_axis`; `gap_details`, `gui_gap_details`: карточки разрывов |
| Мышь и навигация | `gui_input`, `gui_navigation`, `gui_playback`; FRF-ввод также в `gui_frf_render` |
| Разметка и её отображение | `gui_state_history`: Undo/Redo; `gui_marker_values`: карточка текущего маркера; `gui_guide_dialog`: точная линия |
| Экспорт | `gui_export`, `gui_source_export`, `gui_export_metadata`, `export_helpers`, `data_io`; `gui_project_state` — рабочее состояние проекта, `gui_project_results` — необязательные готовые результаты |
| Настройки и горячие клавиши | `gui_settings`, `gui_settings_window`, `gui_settings_hotkeys`, `gui_hotkeys` |
| Общие диалоги и оформление | `gui_dialogs`, `gui_controls`, `gui_theme`, `gui_text`; `gui_welcome`: стартовый экран |
| Меню, статус и ID | `gui_menu`, `gui_status`, `gui_ids.hpp`, `gui_platform.hpp` |
| Сборка и поставка | `build_cli.ps1`, `build_gui.ps1`, `build_version_resource.ps1`, `Makefile`, `package_release.ps1` |
| Регрессии | `tests/run_tests.cpp`, `tests/recording_overview_tests.cpp`, `tests/recording_cache_tests.cpp`, `tests/gui_regression.cpp` |
| Ручные замеры | `tests/perf_benchmark.cpp`; внешние стенды — [EXTERNAL_MATERIALS.md](EXTERNAL_MATERIALS.md) |
| Документация продукта | `README.md`, `README_RU.md`, `README_EN.md`; `docs/manual/AMSignal_User_Manual_RU.md`, `docs/manual/build_manual_docx.py` |

## Практические связи при изменениях GUI

- Новая команда: ID → меню/горячая клавиша → маршрутизация → профильный обработчик
  → обновление состояния/истории/кэшей. Проверь путь настоящего WM_COMMAND.
- Новое состояние: определи, относится ли оно к приложению или документу;
  нужны ли INI, проект, Undo/Redo и инвалидация численного результата.
- Новый контрол: создание в профильном модуле/`gui_window`, геометрия у
  `gui_layout`, оформление через `gui_controls`, тексты RU/EN через `gui_text`.
- Новое асинхронное задание: владеющий вход, отмена, source version/generation,
  обработка ошибки и shutdown. Проверяй поздний результат после смены документа.
- Новый экспортируемый флаг: запись и восстановление в `gui_export_metadata`,
  совместимость отсутствующего ключа, правильная принадлежность документу.

GUI-модули компилируются отдельно; `.cpp` не включаются друг в друга.
Заголовки самодостаточны, глобальные определения находятся в одном `.cpp`.
Новые `gui_*.cpp` автоматически входят в Makefile и `build_gui.ps1`.

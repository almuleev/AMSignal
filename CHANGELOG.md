# Changelog

## v0.15.0

- Добавлена одновременная работа с несколькими исходными файлами. Диалог
  открытия принимает множественный выбор, а drag-and-drop добавляет все
  переданные файлы; один фоновый загрузчик обрабатывает их последовательно.
- Каждый файл теперь является самостоятельным документом: отдельно хранятся
  каналы и их цвета, формулы и фильтрация, временное выделение, режим
  Time/FFT/FRF, кэши спектра и АЧХ, точки, линии и параметры отображения.
  Переключение между документами не копирует большие массивы измерений:
  состояния перемещаются между активным и неактивными слотами.
- В верхнюю панель добавлен заметный селектор `Файлы (N): <имя>` для быстрого
  перехода к любому открытому источнику и соседняя кнопка закрытия текущего
  файла. Запасной список сохранён в `Файл → Открытые файлы`.
- При переключении отменяются только незавершённые FFT/FRF-задачи; готовые
  результаты и отдельные параметры каждого документа сохраняются. Добавлены
  регрессионные проверки переключения и закрытия файлов.
- Added concurrent work with multiple source files. The Open dialog supports
  multi-select, drag-and-drop adds every supplied file, and one background
  loader processes them sequentially.
- Each file is now an independent document with its own channels and colors,
  formulas and filtering, time selection, Time/FFT/FRF mode, spectrum and FRF
  caches, measurement annotations, and display settings. Switching documents
  moves state between active and inactive slots without copying large sample
  arrays.
- Added a prominent top-bar `Files (N): <name>` selector for fast document
  switching and an adjacent Close button. `File → Open files` remains as an
  alternative list.
- Switching cancels only unfinished FFT/FRF tasks; completed results and each
  document's settings remain available. Added regression coverage for document
  switching and closing.

## v0.14.0

- Полностью переработано «Сохранить как»: теперь оно явно разделяет проект,
  обработанные каналы и исходные каналы. Проект сохраняет исходные отсчёты и
  все восстанавливаемые параметры; обработанные каналы записываются с уже
  применёнными формулами и фильтром; исходные каналы сохраняются без обработки.
  В диалоге убрана лишняя верхняя подсказка, проект стоит первым и оставляет
  доступными только имя и расположение файла.
- Добавлен переносимый формат проекта `.AMSig`. При первом запуске AMSignal
  регистрирует ассоциацию этого расширения в профиле текущего пользователя
  Windows, поэтому проекты открываются двойным кликом без прав администратора.
- Горячие клавиши сохранения приведены к однозначной схеме: `Ctrl+S` сохраняет
  текущий проект и запрашивает имя для нового, `Ctrl+Shift+S` открывает
  «Сохранить как», `Ctrl+Alt+S` экспортирует PNG. Прежняя стандартная
  привязка `Ctrl+S` для PNG автоматически переносится при загрузке настроек.
- Добавлены регрессионные проверки исходного и проектного экспорта, восстановления
  настроек проекта и стандартных сочетаний клавиш. Сценарии сборки дополнены
  библиотеками Windows, нужными для регистрации пользовательской ассоциации.
- Reworked the entire `Save as` flow around three explicit choices: project,
  processed channels, and original channels. A project stores raw samples and
  every restorable setting; processed-channel exports bake in formulas and
  filters; original-channel exports remain unprocessed. The redundant heading
  was removed, and the first project option only permits naming and locating
  the file.
- Added the portable `.AMSig` project format. On first run, AMSignal registers
  a per-user Windows association, so projects open by double-click without
  administrator rights.
- Standard save shortcuts are now unambiguous: `Ctrl+S` saves the current
  project or asks for a new project location, `Ctrl+Shift+S` opens `Save as`,
  and `Ctrl+Alt+S` exports PNG. The previous default `Ctrl+S` PNG binding is
  migrated automatically when settings load.
- Added regression coverage for raw and project export, project-setting recovery,
  and the default shortcuts. Build scripts now link the Windows libraries used
  for the per-user file association.

## v0.13.6

- Во всех редактируемых полях AMSignal клавиша Enter теперь подтверждает
  введённое значение. В панели АЧХ она запускает расчёт из поля длины сегмента
  и применяет диапазон из полей частоты; в панели каналов подтверждает общий
  коэффициент и имя группы точек; в настройках сохраняет подписи осей. В
  числовых и диапазонном диалогах Enter активирует основное действие. Прежнее
  поведение индивидуального множителя и переименования канала сохранено.
- Переключатели языка, светлой и тёмной темы собраны в одну верхнюю строку
  раздела «Общие», без отдельной подписи темы. Русская подпись тёмной темы
  приведена к написанию «Темная тема».
- Карта архитектуры и правила проекта уточнены: Enter означает подтверждение
  значения в редактируемом поле и запуск его явного действия либо сохранение
  значения при потере фокуса.
- Enter now confirms a value in every editable AMSignal field. In the FRF
  panel it starts calculation from the segment-length field and applies the
  entered frequency range; in the channel panel it confirms the global
  coefficient and a point-group name; in Settings it saves axis labels.
  Numeric and range dialogs activate their primary action with Enter. Existing
  channel-multiplier and channel-renaming behaviour is retained.
- Language, Light-theme, and Dark-theme controls are grouped into one top row
  in General Settings, with no separate theme label. The Russian Dark-theme
  caption was also normalized to «Темная тема».
- The architecture map and project guidance now explicitly document that Enter
  confirms editable-field input by invoking its explicit action or committing
  the value on focus loss.

## v0.13.5

- Приведены к палитре AMSignal выпадающие списки метода и сглаживания АЧХ,
  выбора клавиши, формата и области экспорта, а также режима и топологии
  фильтра. Закрытая часть списков имеет собственные скруглённую рамку,
  индикатор фокуса и стрелку; пункты сохраняют owner-draw оформление.
- The FRF method and smoothing, hotkey key, export format and range, and
  filter mode and topology drop-downs now use the AMSignal palette. Their
  closed fields have a rounded frame, focus indicator, and custom arrow while
  preserving owner-drawn list items.
- Меню «Файл → Недавние файлы» получило оформление вложенного меню приложения:
  каждый файл отображается карточкой с именем и приглушённым путём. Исправлено
  определение уровня вложенных popup-меню.
- The `File → Recent files` menu now uses the nested AMSignal menu treatment:
  every entry is a card with a file name and muted path. Popup nesting levels
  are now classified correctly.
- В панели каналов индивидуальный множитель перенесён в компактное поле справа
  от имени канала. Поле применяет значение как формулу `coefficient*x` по
  Enter или при потере фокуса и использует тематическую рамку вместо тяжёлого
  системного края.
- The channel panel now has a compact per-channel multiplier field beside each
  channel name. It applies `coefficient*x` on Enter or focus loss and uses a
  themed frame instead of the heavy system edge.

## v0.13.4

- В нижней строке состояния во всех рабочих режимах теперь показана подпись
  `AMSignal · Alexander Muleev · al.muleev@gmail.com`. Рабочий текст не
  перекрывает её, а скрытый нативный статус-контрол больше не закрывает
  отрисованную строку. В PNG-экспорте добавлена подпись `Generated by AMSignal`
  в свободном нижнем правом поле.
- The status bar in every working mode now shows
  `AMSignal · Alexander Muleev · al.muleev@gmail.com`. Its owner-drawn content
  is no longer covered by the hidden native status control, and working text
  does not overlap the credit. PNG exports now include `Generated by AMSignal`
  in the free lower-right area.

## v0.13.3

- Меню выбора ролей АЧХ теперь использует палитру приложения в тёмной теме,
  включая фон, границы, подсветку, текст и флажки. В светлой теме сохранено
  нативное меню Windows. Повторное нажатие на тот же контроль надёжно скрывает
  popup даже при смене фокуса окна.
- The FRF role menu now uses the application palette in Dark theme, including
  its background, borders, hover state, text, and checkmarks. Light theme keeps
  the native Windows menu appearance. A second click on the same control now
  reliably hides the popup even while its window loses focus.
- В АЧХ исправлены маркеры и точки измерения: оба инструмента используют
  координаты Hz/КД, корректно отображаются на логарифмической шкале и не
  смешиваются с аннотациями времени или FFT.
- FRF markers and measurement points are fixed: both use Hz/KD coordinates,
  render correctly on the logarithmic frequency axis, and remain separate from
  Time and FFT annotations.
- В АЧХ кнопка `Точки` теперь открывает встроенную правую панель точек:
  она временно заменяет панель расчёта, а повторное нажатие возвращает её.
  Пункты общих настроек в меню и на стартовом экране сохранены отдельно.
- In FRF, the `Points` button now opens the docked point panel in place
  of the calculation panel; pressing it again restores the FRF controls.
  General Settings remain separately available in the menu and welcome screen.

## v0.13.2

- Выпущена стабилизированная версия выбора ролей АЧХ: постоянное системно
  оформленное меню, скруглённые границы, выбор без скрытия правой панели,
  независимая очистка текущей роли через `Clear` и отсутствие предвыбора
  каналов как для нового, так и для загруженного документа.
- Released the stabilized FRF role-selection interface: a persistent
  system-themed menu with rounded corners, selection without hiding the
  right panel, independent clearing of the current role through `Clear`,
  and no preselected channels for either new or loaded documents.

## v0.13.1

- Выбор опор и откликов в АЧХ возвращён к одному меню-подобному Win32-popup с
  системными флажками из первой версии. Изменение флажка сразу обновляет роль и
  запускает нужный пересчёт, но popup остаётся открытым для выбора нескольких
  каналов; остальные контроли правой панели не скрываются. Он закрывается
  кликом вне него или повторным
  нажатием того же контрола. Удалены встроенный редактор ролей, кнопки Apply/Cancel и обмен
  ролями.
- При новом или загруженном документе роли опор и откликов не предвыбраны. Окно
  выбора имеет слегка скруглённые границы. На кнопках выводятся
  имена каналов, если они помещаются, иначе число выбранных каналов; под
  несколькими опорами показывается `AVG of N channels`. Пункт `Clear` очищает
  только открытую роль и оставляет меню открытым. Каналы, уже назначенные
  противоположной роли, видны в списке, но недоступны, поэтому роли не могут
  пересекаться.
- FRF support and response selection has returned to one menu-like Win32 popup
  with the original system checkbox appearance. Each toggle takes effect immediately
  and triggers the required recalculation while the popup remains open for multi-select;
  the remaining right-panel controls stay visible. It closes on an outside click or a second click on the same control. The embedded
  role editor, Apply/Cancel buttons, and role swap action have been removed.
- A new or loaded document starts with no preselected support or response roles.
  The selection popup has lightly rounded corners. Buttons
  show channel names when they fit and a selected-channel count otherwise; multiple
  supports also show `AVG of N channels`. The `Clear` item removes only the open
  role and leaves the menu open. Channels already assigned to the opposite
  role remain visible but disabled, so roles cannot overlap.

## v0.13.0

- Программа переименована в `AMSignal`: обновлены заголовки окон, справка,
  метаданные экспортируемых LVM-файлов, документация и скрипты запуска.
  Файл пользовательских настроек теперь называется `AMS.ini`.
- The application is now named `AMSignal`: window titles, help text, exported
  LVM metadata, documentation, and launch scripts have been updated. The user
  settings file is now named `AMS.ini`.
- Артефакты GUI-релиза теперь используют единый формат
  `AMSignal-<version>-x64.exe`, например `AMSignal-0.13.0-x64.exe`; номер в
  имени файла не содержит префикс `v` тега.
- GUI release artifacts now use the consistent
  `AMSignal-<version>-x64.exe` format, for example `AMSignal-0.13.0-x64.exe`;
  the file version omits the tag's leading `v`.

## v0.12.7

- Добавлен режим АЧХ: независимые наборы опор и откликов, H1/Welch и Direct,
  фоновый расчёт, coherence, логарифмическая частота, экспорт CSV/PNG и
  поддержка нескольких откликов. По вертикали отображается линейный
  коэффициент динамичности `КД = abs(H)`, то есть отношение амплитуды отклика
  к средней амплитуде опор; перевод в dB не используется.
- Added an FRF mode with separate support/response selection, H1/Welch and
  Direct estimators, background calculation, coherence, logarithmic frequency,
  CSV/PNG export, and multiple response curves. The vertical scale is the
  linear dynamic coefficient `KD = abs(H)`, not dB.
- Бины со слабой опорой исключаются из кривой и CSV, а coherence остаётся
  диагностической величиной и не скрывает данные автоматически. Убрана min/max-отрисовка, создававшая вертикальные иглы; сглаживание
  графика на логарифмической частотной шкале выбирается от выключенного до
  1/3 октавы и не меняет исходные данные экспорта.
- Weak-reference bins are excluded from the curve and CSV, while coherence is
  diagnostic and does not hide data automatically. Rendering no longer draws min/max spike columns; optional display-only
  logarithmic smoothing ranges from off to one-third octave.
- Удалены порог и визуальная отметка coherence: coherence остаётся только
  диагностическим значением под курсором и в CSV.
- Removed the coherence threshold and visual marking: coherence remains a
  diagnostic value under the cursor and in CSV.
- Легенда нескольких откликов больше не выводится поверх графика АЧХ.
- The multi-response legend is no longer drawn over the FRF plot.
- Выбор опор и откликов перенесён из двух всплывающих меню в единый диалог с
  двумя списками, проверкой пересечения и обменом ролями.
- Support and response selection moved from popup menus into an editor embedded
  in the FRF panel, with role-overlap validation and a swap action.
- FRF использует общие с Time/FFT точки измерения и вертикальные/горизонтальные
  линии в Hz и КД, включая группы, настройки отображения и Undo/Redo.
- FRF now reuses the Time/FFT measurement points and vertical/horizontal guides
  in Hz and KD, including groups, display settings, and Undo/Redo.

- Добавлены короткие правила `AGENTS.md` и карта `docs/ARCHITECTURE.md` для адресного поиска по проекту. Удалён устаревший `PROJECT_CONTEXT.md`; исторические отчёты перенесены в локальный архив, исключённый из Git. Обновлены ссылки в README.
- Убраны повторные и малоценные условия тестов; сохранены полные сравнения FFT для 10 и 10 000 разрывов, проверки Light Mode, экспорта и истории. Усилена проверка успешности двух расчётов FFT. Пройдены 197 проверок ядра и 141 проверка GUI.
- Added concise agent rules and an architecture map, removed redundant test assertions, and excluded local historical reports from version control. Core and GUI regression suites retain gap, caching, export, and history coverage.

- GUI переведён на самостоятельные модули `.cpp/.hpp`: убраны включения реализаций, вынесены FFT, временная ось, загрузка, боковая панель, настройки и обработчики сообщений. `gui_main.cpp` сокращён с 4869 до 138 строк.
- GUI и интеграционные тесты линкуются с одинаковыми объектными файлами. Добавлены инкрементальная сборка и запуск GUI-тестов через `build_gui.ps1 -Test`; обновлён Makefile. Проверены маршрутизация команд, клавиш и завершение отменённой загрузки.
- GUI implementations now compile as separate modules with explicit headers. The application and integration tests share object files; incremental builds and PowerShell GUI test execution are supported.

- Light Mode больше не пересчитывает FFT при скрытии и повторном показе каналов из текущего расчёта, в том числе во время фонового задания. Новый расчёт требуется при включении канала вне текущего кэша или изменении данных, участка и обработки. Добавлены проверки настоящего асинхронного пути GUI через невидимое служебное окно.

- Повторно проверена склейка для FFT: исправлены короткие разрывы в 2–4 шага, оценка шага при преобладании больших разрывов, зависимость интерполяции от длительности разрыва и влияние отсчётов вне лимита CLI на частотную шкалу.
- Склейка графика использует общее с FFT правило определения пропусков; преобразование координат сохраняет точность при больших разрывах и выполняет обратный поиск по индексам отсчётов.
- Добавлено сравнение всех частотных бинов и амплитуд с эталоном для 10 и 10 000 разрывов, двух каналов, выделенных диапазонов, Light Mode и экспорта. Уточнены ограничения автоматического определения шага и обработка NaN.
- Исправлено определение версии в сценариях сборки и упаковки для Git, отвергающего NUL в качестве файла исключений.
- Rechecked FFT gap compression, cadence estimation with dominant outages, short gaps, jitter correction across huge outages, and sample-cap isolation. Added complete-spectrum reference comparisons and GUI selection/export regressions.

## v0.12.5

- Исправлена потеря одиночных выбросов при автомасштабировании Light Mode и пропуск узких пиков при плотной отрисовке спектра. Диапазоны отрисовки хранятся в double.
- Добавлен режим «Склеивать пропуски времени на графике». Он сжимает все разрывы на экранной оси и не меняет исходные данные, измерения или экспорт.
- Ползунок фильтра применяет изменение после отпускания; одно движение создаёт одно действие Undo. Undo восстанавливает удалённые измерения. История ограничена 128 действиями и 64 МиБ полезной нагрузки.
- Экспортированный спектр открывается непосредственно по сохранённым частотам и амплитудам, без повторного FFT и применения временных формул. Time и временной фильтр для него недоступны.
- Снижены лишние копирования при фильтрации, освобождаются ненужные кэши. Запись метаданных теперь целиком находится в `gui_export_metadata.cpp`.
- CLI поддерживает Unicode-пути Windows. Сборки GUI/CLI маркируются тегом, commit и dirty; добавлены `build_cli.ps1` и CI. `Start GUI.bat` выбирает последнюю собранную программу.
- FFT больше не отклоняет запись из-за неравномерных или округлённых меток времени: добавлена линейная интерполяция на равномерную сетку. Большие разрывы исключаются только из временной шкалы FFT, а все отсчёты выбранного диапазона остаются в расчёте.
- Исправлен масштаб частотной оси после повторного перехода в FFT и завершения фонового расчёта.
- FFT now resamples uneven timestamps and removes large timestamp gaps from its time scale while retaining every selected sample in the calculation.
- Fixed frequency axis fitting when returning to FFT mode after background calculation.

## v0.12.4

- Разделены группы точек для временного и FFT-режимов: у каждого режима теперь свой набор, свой активный элемент и своя отрисовка, чтобы при переключении режимов точки не смешивались.
- Проверены тексты в интерфейсе и релизных строках: русские подписи должны отображаться корректно без `???` и битых символов.
- Point groups are now separated between time and FFT modes, with independent active groups and rendering so markers no longer mix when switching views.
- UI and release text were checked to keep Russian strings readable and avoid `???` or broken characters.

## v0.12.1

- Вынесены крупные блоки интерфейса из `gui_main.cpp` в отдельные `gui_*.cpp/.hpp` модули, чтобы упростить поддержку и дальнейшее развитие проекта.
- Исправлено окно горячих клавиш: добавлен сброс всех привязок к заводским настройкам, расширен список доступных клавиш и устранено исчезновение пунктов в списке и выпадающем поле.
- Файл настроек теперь создаётся под именем `AMGV.ini`.
- Исправлены подписи `Δx`, `Δy` и `1/Δt` в панели точек и связанные элементы интерфейса.
- The GUI was split into separate `gui_*.cpp/.hpp` modules to make `gui_main.cpp` smaller and easier to maintain.
- The hotkeys window now includes a full reset to defaults, a broader key picker, and stable item rendering so entries no longer disappear from the list or combo box.
- The settings file is now created as `AMGV.ini`.
- Point readouts now show `Δx`, `Δy`, and `1/Δt` correctly in the point panel and related UI.

## v0.12.0

- Исправлено отображение русских и английских строк в меню, подсказках и справке; интерфейс снова показывает корректный текст.
- Fixed broken Russian and English text in menus, tooltips, and help so the UI renders readable strings again.

## v0.11.5

- Вынесены обработчики хоткеев, настроек и связанных диалогов в `gui_settings_hotkeys.cpp`, чтобы разгрузить `gui_main.cpp`.
- Extracted hotkey, settings, and related dialog helpers into `gui_settings_hotkeys.cpp` to keep `gui_main.cpp` smaller.
- Удалены подтверждённые неиспользуемые переменные в фильтре и импорте метаданных экспорта.
- Removed confirmed unused variables in the filter and export metadata import path.

## v0.11.4

- Добавлена возможность сохранять скрытые каналы при экспорте.
- Added the ability to keep hidden channels in exported files.
- Добавлена поддержка загрузки файлов `.csv` и восстановления экспортных настроек из встроенных комментариев.
- Added `.csv` file loading and restored export settings from embedded comments.
- Улучшена совместимость экспорта для TXT / CSV / LVM и частотного режима.
- Improved export compatibility for TXT / CSV / LVM and frequency mode.

## v0.11.3

- Упрощён экспорт: отдельные действия для `TXT`, `CSV` и `LVM` сведены в единый `Save as…` с выбором формата, области выгрузки и режима обработки данных.
- Simplified export: separate `TXT`, `CSV`, and `LVM` actions were merged into one `Save as…` flow with format, range, and processing-mode selection.
- В окне экспорта оставлены дополнительные опции для точек, маркеров, направляющих, формул, фильтра и настроек графика.
- The export dialog still lets you include points, markers, guide lines, formulas, filter settings, and graph settings.
- Убран лишний поясняющий текст и уплотнена компоновка окна экспорта.
- Removed the extra explanatory note and tightened the export dialog layout.

## v0.11.1

- Расширен экспорт: теперь отдельно выбираются область выгрузки, состав сохраняемых данных и режим обработки формул/фильтра.
- Added richer export controls: the export range, included data, and formula/filter handling can now be configured independently.
- Обновлён диалог экспорта и описания опций в интерфейсе.
- Updated the export dialog and the option descriptions in the UI.

## v0.11.0

- Ребрендинг проекта: основное имя приложения изменено на `AM Graph Viewer`, а release-файл теперь называется `AMGraphViewer-v0.11.0-win-x64.exe`.
- Rebranded the project so the main app name is now `AM Graph Viewer`, and the release artifact is now `AMGraphViewer-v0.11.0-win-x64.exe`.
- Обновлены build/release-скрипты, ссылки в документации и пользовательские заголовки.
- Updated the build/release scripts, documentation links, and user-facing window titles.

## v0.10.7

- Исправлены подписи в верхнем меню: пункты общих настроек, горячих клавиш, линий и вертикального панорамирования больше не обрезаются.
- Fixed top-menu labels so general settings, hotkeys, line tools, and vertical panning are shown in full.
- Добавлены глобальные подписи осей X/Y, которые отображаются в углах графика и настраиваются отдельно в окне настроек.
- Added global X/Y axis labels rendered in the graph corners, with dedicated settings fields.
- Настройка отображения измерительных значений теперь хранится отдельно для каждой группы точек.
- Measurement read-outs are now stored separately for each point group.
- Улучшена обработка `.lvm`: имена каналов из чередующихся таблиц сохраняются корректнее, а секции времени восстанавливаются точнее.
- Improved `.lvm` parsing: channel names from interleaved tables are preserved more reliably, and section timing is reconstructed more accurately.
- Окна горячих клавиш и выбора диапазона подстроены под размеры экрана и не блокируют основное приложение.
- The hotkeys dialog and range-selection prompt now fit the screen better and no longer block the main app.

## v0.10.5

- Доработана работа с пропущенными промежутками: подписи внутри графика убраны, сами разрывы по-прежнему выделяются, а подробная информация теперь открывается отдельным окном по клику.
- Окно горячих клавиш больше не блокирует основное приложение: оно открывается в пределах рабочей области экрана, при нехватке высоты использует прокрутку и не мешает дальнейшей работе с главным окном.
- Упрощена сборка проекта: версия приложения теперь передаётся через define, а generated-файл `build_version.hpp` удалён из репозитория.
- Reworked gap handling: inline labels are removed from the plot, the highlighted gaps remain visible, and detailed information is now shown in a separate dialog on click.
- The hotkeys window no longer blocks the main app: it stays within the monitor work area, uses scrolling when vertical space is limited, and keeps the rest of the UI interactive.
- Simplified the build pipeline by passing the app version through a define and removing the generated `build_version.hpp` file from the repository.

## v0.10.4

- Переработан welcome-экран: стартовая страница теперь устойчиво адаптируется к разным размерам окна, получила переключатели темы и более согласованную компоновку.
- Light mode и правая рабочая панель доработаны для больших файлов: выбор временного диапазона, скрытие каналов по умолчанию, более аккуратные отступы, единый стиль переключателей и исправления текстовых артефактов при ресайзе.
- Улучшено редактирование в интерфейсе: ввод имени канала завершается по клику вне поля, текстовые поля больше не перехватывают горячие клавиши, а компактная легенда каналов зачёркивает только название скрытого канала.
- Исправлена обработка много-секционных `.lvm` с повреждёнными временными блоками: время секций теперь восстанавливается по `Date` / `Time` / `X0`, а на больших разрывах времени график больше не рисует ложные прямые линии.
- Reworked the welcome screen so the start page adapts cleanly across window sizes, includes theme switching, and keeps a more consistent layout.
- Refined Light mode and the right-side work panel for large files with explicit time-range selection, channels hidden by default, better spacing, unified toggle styling, and resize artefact fixes.
- Improved in-app editing: channel rename closes on outside click, text inputs no longer trigger global hotkeys, and the compact channel legend now strikes through only the hidden channel name.
- Fixed multi-section `.lvm` handling for corrupted time blocks by rebuilding section timing from `Date` / `Time` / `X0`, while large time gaps now break the plot instead of drawing misleading straight lines.

## v0.10.3

- Исправлено обрезание текста Light mode на приветственном экране и в настройках.
- Подсказка Light mode теперь помещается целиком и не подрезается по нижней границе.
- Обновлена версия релиза для текущего набора UI-исправлений.
- Fixed Light mode text clipping on the welcome screen and in Settings.
- The Light mode hint now fits fully and no longer gets clipped at the bottom.
- Bumped the release version for the current UI polish pass.

## v0.10.2

- Добавлен `undo/redo` для изменений в настройках каналов и точек: видимость, переименование, цвета, коэффициенты и параметры отображения точек теперь откатываются через `Ctrl+Z` / `Ctrl+Shift+Z`.
- Added `undo/redo` for channel and point setting changes: visibility, renaming, colours, coefficients, and point display options can now be reverted with `Ctrl+Z` / `Ctrl+Shift+Z`.
- Исправлено некорректное отображение текста на кнопках правой панели после изменения размера окна: подписи больше не вылезают за границы соседних кнопок.
- Fixed button text clipping in the right-side panel after window resizing so labels no longer spill into neighbouring controls.
- Упрощена панель точек: укорочены подписи переключателей, убрана лишняя подсказка под списком каналов, добавлена явная кнопка смены цвета выбранного канала.
- Streamlined the side panels: shorter point toggle labels, the extra channel hint removed, and an explicit colour picker button for the selected channel.
- Привязка точек и маркеров к графику теперь выбирает ближайшую видимую точку по экранному расстоянию, а не только по оси X.
- Point and marker snapping now chooses the nearest visible point by on-screen distance instead of snapping by X only.

## v0.10.1

- Убраны все добавленные подсказки по формулам из правой рабочей панели.
- Removed the newly added formula hints from the right-side work panel.
- Все пользовательские упоминания формул в панели преобразований переименованы в коэффициенты.
- Reworded the transform panel UI so user-facing formula labels are now presented as coefficients.
- Добавлена отдельная кнопка для сброса всех локальных коэффициентов каналов без затрагивания общего коэффициента.
- Added a separate action to reset all per-channel local coefficients without changing the global coefficient.

## v0.10.0

- Добавлена формульная система преобразования сигналов: общая формула для всех графиков и отдельная формула для выбранного канала.
- Added formula-based signal transforms with a shared formula for all charts and a separate formula for the selected channel.
- В правой рабочей панели появились подсказки по формулам, объяснение переменной `x` и примеры выражений.
- Added inline formula help, an explanation for the `x` variable, and ready-to-use expression examples in the right-side work panel.
- Пункт `About` теперь открывает стартовый welcome-экран, а сам экран показывает версию текущей сборки.
- The About action now opens the start welcome screen, which also shows the current build version.

## v0.10.0-rc1

- Добавлена встроенная правая рабочая панель с вкладками для каналов и точек.
- Added a docked right-side work panel with separate tabs for channels and points.
- В панель каналов вынесены быстрые коэффициенты: общий множитель, общее слагаемое и коэффициенты выбранного канала.
- The channel panel now exposes quick transform coefficients: global multiplier, global offset, and per-channel coefficients for the selected channel.
- В панель точек добавлено переименование групп, а отдельное окно настроек очищено от дублирующих блоков каналов и точек.
- Point groups can now be renamed from the panel, while the separate settings window has been reduced to non-duplicated general settings and hotkeys.

## v0.9.3

- Исправлено поведение выделенного диапазона при приближении: индикация больше не пропадает, если текущий экран смещён относительно выделенного окна.
- Fixed selected-range behaviour during zooming: the visual indication no longer disappears when the current viewport moves away from the selected window.
- Штриховка и затемнение теперь подчёркивают именно невыделенную область, а внутри выделенного диапазона оставлена только мягкая подсветка без лишнего визуального шума.
- Hatch shading now emphasizes the non-selected area, while the selected range keeps only a soft tint without extra visual noise.

## v0.9.2

- Добавлено ручное задание точных значений для вертикальных и горизонтальных линий.
- Added manual input for exact vertical and horizontal guide line values.
- Выбор диапазона через `Shift` стал заметнее: остальные участки графика приглушены штриховкой, а маркеры диапазона остаются видимыми.
- Shift-based range selection is now easier to read: the rest of the graph is muted with a subtle hatch, and the range handles stay visible.

## v0.9.1

- Added real application screenshots to the GitHub presentation.
- Promoted the dark main workspace and measurement-group workflow as the primary README visuals.
- Cleaned up screenshot assets so the repository keeps only the selected UI images.

## v0.9.0

- Переработано оформление GitHub-страницы: новый баннер, более аккуратная документация и визуальные превью.
- Reworked the GitHub presentation with a new banner, cleaner documentation, and visual previews.
- Добавлены базовые элементы публичного проекта: лицензия MIT, шаблоны для issues и более аккуратная структура репозитория.
- Added public-project essentials: MIT license, issue templates, and cleaner repository hygiene.
- Добавлены независимые группы точек измерения с отдельными цветами и видимостью.
- Introduced independent measurement point groups with separate colours and visibility.
- Обновлено окно настроек: управление цветом активной группы, цветом выбранной группы и видимостью групп.
- Updated the settings window to manage active point colour, selected group colour, and group visibility.
- Улучшены undo/redo для точек и отображение состояния точек.
- Improved point-related undo/redo and point status reporting.

## v0.8.3

- Восстановлено взаимодействие в окне настроек после прошлых UI-рефакторингов.
- Restored settings interactions after earlier UI refactors.
- Доработано поведение меню и настроек.
- Refined menu and settings behaviour.
- Продолжена полировка темы и согласованности интерфейса.
- Continued polishing theme and interface consistency.

## v0.8.1

- Fixed mojibake in Russian interface labels.
- Refined active button visuals and unified `Auto zoom` naming.
- Clamped edge axis labels so they stay inside the chart area.
- Simplified playback speed selection to direct numeric input.

## v0.8.0

- Rebuilt the welcome screen into a cleaner start page.
- Added a unified settings window for language, hotkeys, markers and measurement options.
- Moved channel renaming into the channel list.
- Added signal transform controls with global and per-channel multiplier/offset.

## v0.5.1

- Fixed strict monotonic time rebuild behaviour.
- Corrected DC/Nyquist FFT edge-bin scaling.
- Improved CLI validation for `--fft-samples`.

## v0.5.0

- Added drag and drop support.
- Added channel renaming.
- Added start/end navigation shortcuts.
- Added vertical panning support.

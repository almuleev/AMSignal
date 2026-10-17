# Image manifest / Описание изображений

[Gallery](../../GALLERY_EN.md) · [Галерея](../../GALLERY_RU.md) · [Features](../../FEATURES_EN.md) · [Возможности](../../FEATURES_RU.md)

These images use AMSignal's native Win32/GDI drawing and real controls with the synthetic example. An isolated documentation entry point loads the source through the production parser, calculates FFT/FRF through production analysis and renders the application's client area to a bitmap. Windows desktop chrome and the menu bar are outside the captured client area. The cursor is never drawn; curves and labels are not retouched or AI-generated. The export images are written by the application's own PNG exporter.

Кадры получены нативным Win32/GDI-кодом AMSignal и настоящими контролами на синтетическом примере. Отдельная точка входа для документации загружает исходник штатным парсером, рассчитывает FFT/FRF штатным анализом и рисует клиентскую область приложения в bitmap. Рамка Windows и строка меню находятся за пределами этой области. Курсор не отрисовывается; кривые и подписи не ретушировались и не генерировались ИИ. Изображения экспорта записаны собственной функцией PNG приложения.

## Build / Сборка

Local working snapshot based on `v0.17.5-1-g2660b65-dirty`, captured 2026-10-17. This is a documentation rendering of the working code, not a capture or verification of the published v0.17.5 executable. No production source was changed for these images.

Локальный рабочий снимок на основе `v0.17.5-1-g2660b65-dirty`, изображения от 17.10.2026. Это отрисовка рабочего кода для документации, а не съёмка или проверка опубликованного EXE v0.17.5. Для кадров производственные исходники не менялись.

SHA-256 of the documentation rendering executable / EXE отрисовки документации:

```text
cff179070b54af4fd366d3ec9a01ec7cfb1eefbb2d930abe7d6fbe73c7219775
```

SHA-256 of the compiled `gui_render.cpp` source / исходника отрисовки:

```text
2801dab75076a561f6d0ac468a4713c5631222e213621b6935975b8a842093d1
```

## Data / Данные

[`examples/demo-signals.txt`](../../../examples/demo-signals.txt): 32,768 samples, Fs=1024 Hz, two references and two responses. The references measure common broadband/multitone excitation with separate small noise additions; response models use their arithmetic mean. [Generator and scene recipe](../../../examples/README.md).

32 768 отсчётов, Fs=1024 Гц, две опоры и два отклика. Опоры измеряют общее широкополосное/многотональное возбуждение с отдельными малыми добавками шума; вход моделей отклика — их арифметическое среднее. [Генератор и рецепт сцен](../../../examples/README_RU.md).

SHA-256:

```text
85a1a48b025adbb649b1402502824dd3a3ca73af00c6260093c15708b88cb625
```

## Scenes / Сцены

Each scene has matching `-en.png` and `-ru.png` versions / Каждая сцена имеет варианты `-en.png` и `-ru.png`.

| Stem / Имя | State / Состояние |
| --- | --- |
| `signal` | Dark, all four channels, 1–1.5 s / Тёмная тема, четыре канала, 1–1,5 с |
| `light` | Same source and view, light theme / Тот же источник и вид, светлая тема |
| `fft` | Entire recording, 0–210 Hz / Вся запись, 0–210 Гц |
| `frf` | Two references, two responses, H1, L=2048, K=31, 50% overlap, 0.5 Hz spacing, no smoothing, view 5–210 Hz / Две опоры, два отклика, те же параметры |
| `measurements` | Response B, three sample points, one marker, t=1.10 s and U=0.5 V guides / Три точки на отсчётах, маркер и две направляющие |
| `processing` | Enabled 45 Hz Butterworth low-pass / Включён ФНЧ Баттерворта 45 Гц |
| `interval` | Selected 1.15–1.35 s inside a 1–1.5 s view / Участок 1,15–1,35 с внутри вида 1–1,5 с |
| `channels` | Response A renamed, multiplier 2, global formula (x-0.02)*1.5 / Переименование, множитель 2 и общая формула |
| `print` | Two responses, light theme, curve symbols, 1–1.12 s / Два отклика, светлая тема, фигуры, 1–1,12 с |
| `export` | Actual PNG export of the measurement scene / Настоящий экспорт PNG измерительного примера |
| `print-export` | Actual PNG export with curve symbols / Настоящий экспорт PNG с фигурами |

FRF export metadata confirmed N=32768, L=2048, K=31, two reference indices and two response indices. The recording is synthetic; these images do not constitute a physical experiment, release certification or a performance benchmark.

Метаданные экспорта FRF подтверждают N=32768, L=2048, K=31, два индекса опор и два индекса откликов. Запись синтетическая; изображения не являются физическим экспериментом, аттестацией выпуска или замером производительности.

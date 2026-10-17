# Performance and resources

[Documentation](README.md) · [Русский](PERFORMANCE_RU.md)

This is a user-facing extract from the completed comparison series of **16 October 2026**. It describes **v0.17.3**, not a new benchmark of the latest release. The consolidated report and raw protocols are kept separately; maintainers can find their location in the [external materials reference](development/EXTERNAL_MATERIALS.md).

## A real 1.024 GB recording

Input: **1,024,104,009 bytes**, 6,832,000 samples, eight channels. GUI in Auto, with data fitting in RAM. Values are medians of three separate processes, in seconds.

| Operation | v0.17.3 |
| --- | ---: |
| Open in the analysis sequence | 33.2168 |
| First full FFT of one raw channel | 2.6251 |
| New full FFT of one raw channel | 2.6555 |
| First H1 for seven raw responses | 1.8154 |
| Repeat H1 | 0.9736 |
| First processed FFT | 3.4361 |
| First processed H1 | 2.1636 |

FFT uses all 6,832,000 samples of one channel. H1: one reference, seven responses, `L=2048`, periodic Hann, 50% overlap, mean removal. Repeat H1 recalculates while reusing input preparation; it is not a ready FFT cache lookup.

The filter in processed scenarios is a Butterworth low-pass at 0.2 times Nyquist, applied to all eight channels. Peak working set of the **whole sequence** was 2545.95 MiB; peak commit was 2663.25 MiB. These peaks do not describe a single FFT.

## Viewing a large recording

A separate navigation scenario used the same file and v0.17.3:

| Conditions | Open + first view, s | Mean ready redraw, s |
| --- | ---: | ---: |
| Auto, new application index | 33.1535 | 0.0092 |
| Manual 64 MiB, new index | 63.7563 | 0.0099 |
| Manual 64 MiB, cached index | 32.1647 | 0.0099 |

A fast ready redraw is not the total cost of navigation, reading, overview generation or analysis. Manual 64 MiB is the application's data allowance, not a process-wide memory cap or the physical computer's capacity. The disk-backed path retains the full source but adds reads and scratch files.

## Conditions and reproducibility

- Windows 10 Pro 10.0.19045, AMD Ryzen 5 5600X, 6 cores / 12 logical processors, 32 GiB RAM, Power saver profile.
- GCC 14.2.0, C++17, `-O2 -static`, no fast-math. Clean v0.17.3 tag, commit `0a0e23e40df72d244ae8fe3df0f850b315f0b350`.
- Three processes per version and file. Versions ran sequentially with rotating order; compilation finished before timing. Windows cache was not cleared: a “new index” does not mean a cold disk read.
- The benchmark invokes the production loader, workers and GDI bitmap rendering; timings cover the software path, excluding visible window, monitor and user input latency. Analysis geometry: bitmap 1100×700 / plot 1000×590; navigation: bitmap 1000×650 / plot 880×530.
- Input SHA-256: `302c016159448f72713bf31e82782429604a449700be53b156c70873d8852465`.
- Figure sources: “GUI Auto: анализ и обработка” and “Открытие, фильтр и навигация отдельного свежего процесса” in the consolidated report; raw `gui.jsonl`, `navigation.jsonl`, `manifest.json` from the 2026-10-16 series.

## Interpreting the figures

Time depends on the format, channel count, N, interval, L, processing, RAM, disk and cache state. A larger file or another computer need not give the same speed. These Auto timings do not describe disk-backed FFT.

A smaller interval reduces analysis input when explicitly selected using **Analysis**. Zooming alone does not reduce FFT/FRF input. Compare the complete workflow and resource costs, rather than only a ready redraw or an isolated mathematical kernel.

[Analysis and limitations](ANALYSIS_EN.md) · [Moving your workspace](PORTABLE_EN.md)

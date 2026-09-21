# AMSignal

<p align="center">
  <img src="docs/assets/github-banner-v2.png" alt="AMSignal signal-analysis banner">
</p>

Native Win32 viewer and CLI toolkit for LabVIEW `.lvm`, `.txt` and `.csv` signal files.

## Why This Project Exists

LabVIEW measurement logs are often easy to produce but inconvenient to inspect quickly outside a LabVIEW environment. `AMSignal` focuses on a practical middle ground:

- no Qt
- no external GUI runtime
- no heavyweight dependencies
- fast opening of real measurement files
- interactive viewing plus script-friendly CLI access

## Core Features

### Desktop viewer

- Time-domain, FFT and FRF (frequency-response) modes in one window
- Zoom, pan, playback and auto-zoom
- Dark and light themes
- Russian and English interface
- Inline channel rename in the channel list
- PNG, CSV, TXT and LVM export; FRF results export to CSV and PNG
- Drag and drop file opening

### Measurements and analysis

- Snapped measurement points
- Independent point groups with separate colours and visibility
- `X`, `Y`, `Δx`, `Δy`, `1/Δt` and distance readouts
- Vertical and horizontal guide lines
- Named markers
- Undo and redo for points, lines and markers

### Frequency response (FRF)

- Separate non-overlapping Reference and Response channel roles
- H1/Welch estimator with Hann window and 50% overlap, plus Direct comparison
- Multiple response curves, logarithmic frequency scale and display-only smoothing
- Dynamic coefficient `KD = abs(H)` and diagnostic coherence; DC and weak-reference bins are excluded
- Exported FRF CSV includes complex transfer, validity, coherence and calculation metadata. It is a result file and cannot yet be reopened as an FRF document.

FFT accepts uneven and rounded timestamps by linearly interpolating values onto
an evenly spaced gap-free grid with the same sample count. Gaps greater than
1.5 estimated sample steps are compressed to one step on the FFT timeline, while all samples on
both sides of every gap remain in the calculation. This is reported in the
status bar and spectrum export metadata. Interpolation can attenuate high frequencies; an
original uniformly sampled recording is preferable for precise analysis.
The original time-domain data is preserved.

Cadence is inferred from the median interval. When a short-interval cluster
(at least two intervals) is separated from long outages by a factor greater
than four, its median is used even if outages are the majority. Dominant short
gaps are also detected when intervals are integer multiples of the lower-cluster
median within 5% of one sample step. This remains
an estimate: timestamps alone cannot unambiguously distinguish a sample-rate
change from missing samples. Uniformly long intervals require external cadence
information to recover the original rate. Missing channel values (`NaN`) are
handled separately by filling with the channel mean, without shifting channels.

### Command-line mode

- File structure and parser information
- Per-channel statistics
- CSV export
- FFT peak inspection
- Windowed processing for selected time ranges

### Operational notes

- Light Mode includes every sample in its min/max ranges, preserving single-sample impulses.
- Light Mode computes FFT in the background for requested channels. Hiding and restoring a channel from the current cache does not restart FFT. Showing an uncached channel schedules a new job for visible channels; the cache holds the latest job, not every previously viewed channel. Changing the source interval or processing requires recalculation.
- Settings include “Stitch time gaps in the graph”. It compresses only the displayed time axis and navigation; source data, measurements, and export retain real timestamps.
- Filter sliders apply on release and create one undo action, including affected measurements.
- History retains up to 128 actions and 64 MiB of payload. Old entries are removed at the limit; an action larger than the entire budget clears history.
- `.AMSig` projects restore their raw samples and saved AMSignal settings. After the first run, Windows opens this per-user associated extension in AMSignal on double-click. CSV/TXT files with a `Frequency` column open directly as stored spectra, without another FFT. They contain no time-domain signal.
- Windows CLI supports Unicode paths. `build_cli.ps1` and `build_gui.ps1` label builds with the checkout version and dirty state; `Start GUI.bat` selects the newest executable.
- Run `make test` and, on Windows, `make test-gui`. CI builds the CLI/core on Linux and GUI/CLI on Windows.
- GUI modules compile separately with cached object files. Without Make, run GUI tests using `powershell -ExecutionPolicy Bypass -File .\build_gui.ps1 -Test`. See the [architecture map (Russian)](docs/ARCHITECTURE.md).

## Interface Screenshots

| Main workspace | Measurement groups in action |
|---|---|
| ![Main workspace](docs/assets/ui-overview-dark.png) | ![Measurement groups in action](docs/assets/ui-measurement-groups-dark.png) |

## Data Previews

| Time-domain | FFT |
|---|---|
| ![Time-domain preview](docs/assets/preview-time.png) | ![FFT preview](docs/assets/preview-fft.png) |

| Point groups |
|---|
| ![Point groups preview](docs/assets/preview-point-groups.png) |

The screenshots above show the real application interface.
The graph previews below are based on the bundled sample input files from [`lvm_files_for_tests`](lvm_files_for_tests).

## Quick Start

### Download a ready-to-run build

Use the [latest GitHub release](https://github.com/almuleev/AMSignal/releases/latest) and download the Windows executable.

### Build the GUI

```powershell
powershell -ExecutionPolicy Bypass -File .\build_gui.ps1
```

### Build the CLI

```powershell
powershell -ExecutionPolicy Bypass -File .\build_cli.ps1
```

### Run tests

```powershell
powershell -ExecutionPolicy Bypass -File .\build_gui.ps1 -Test
```

In an MSYS2/MinGW shell, `make all test test-gui gui` builds both applications and runs both test suites, as in Windows CI.

## Build Notes

- Language: `C++17`
- GUI stack: `Win32 API + GDI/GDI+`
- Recommended Windows toolchain: `MSYS2 / MinGW g++`
- GUI output name follows the current git tag: `AMSignal-<version>-x64.exe`

## Repository Layout

| Path | Purpose |
|---|---|
| `gui_main.cpp` | GUI entry point, message loop and routing |
| `gui_*.cpp/.hpp` | Separately compiled GUI modules |
| `main.cpp` | CLI implementation |
| `lvm_parser.cpp/.hpp` | `.lvm` / `.txt` parser |
| `analysis.cpp/.hpp` | Analysis helpers |
| `fft.cpp/.hpp` | FFT engine |
| `tests/run_tests.cpp` | Regression tests |
| `docs/assets/` | Repository visuals |
| `AGENTS.md`, `docs/ARCHITECTURE.md` | Agent working rules and current architecture map |

## Related Files

- [Russian README](README_RU.md)
- [Main README](README.md)
- [Changelog](CHANGELOG.md)
- [GNU GPLv3](LICENSE)

## License

The Software is distributed under the [GNU GPLv3](LICENSE). It may be used, studied, modified and redistributed under GPLv3; distributed modified versions must remain under GPLv3 with corresponding source code available.

# Build and CLI

[Documentation](README.md) · [Русский](BUILD_RU.md)

## Ready-to-run application

For normal desktop use, download the GUI EXE from [Releases](https://github.com/almuleev/AMSignal/releases/latest). The CLI is not a separate release attachment. GitHub's source archives contain the code used to build it.

## Windows prerequisites

Use Git, Windows PowerShell and an MSYS2/MinGW toolchain providing `g++`, `windres` and Windows libraries. The scripts fall back to `C:\msys64\ucrt64\bin` when `g++` is not on PATH. The application uses C++17, Win32 and GDI/GDI+; it does not need a Qt or LabVIEW runtime.

Run these commands from the repository root:

```powershell
# Build the GUI
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build_gui.ps1
# Build the CLI
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build_cli.ps1
```

The GUI file name is `AMSignal-<version>-x64.exe`; version and dirty state come from `git describe`. The CLI output is `lvm_reader.exe`. If a running app locks the GUI EXE, use a separate output folder:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build_gui.ps1 -OutputDirectory dist/my-build
```

## CLI examples

```powershell
.\lvm_reader.exe --help
.\lvm_reader.exe .\examples\demo-signals.txt --info --stats
.\lvm_reader.exe .\examples\demo-signals.txt --fft --peaks 5
.\lvm_reader.exe .\examples\demo-signals.txt --start 1 --end 3 --channels 1,2 --csv selected.csv
.\lvm_reader.exe .\examples\demo-signals.txt --fft-csv spectrum.csv
```

Channel positions in `--channels` are **1-based**. `--start` and `--end` select time bounds before statistics, export and FFT. `--fft-samples 0` uses all selected samples; an explicit positive value limits them. `--monotonic` rebuilds a monotonic timeline for sectioned files. The CLI provides statistics, CSV export and FFT peak inspection; interactive FRF is in the GUI.

## Validation

In an MSYS2/MinGW shell, `make all test test-gui gui` builds both programs and runs core and GUI regressions. GUI tests can also be built and run with PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build_gui.ps1 -Test
```

For Linux CLI/core work, use `make all test`; the native GUI targets Windows. For the exact test-selection and release rules, see [Testing — Russian](development/TESTING.md). Rebuild the relevant test executable before using it to validate changed sources.

## Repository layout

| Path | Responsibility |
| --- | --- |
| `gui_*.cpp/.hpp` | Native interface and document workflows |
| `main.cpp` | CLI arguments and actions |
| `lvm_parser.*`, `data_io.*`, `recording_source.*` | Parsing and source access |
| `analysis.*`, `fft.*`, `frf_*`, `filter_engine.*` | Numerical analysis and processing |
| `tests/` | Core and GUI regressions |
| `examples/` | Reproducible user demonstration |
| `docs/` | User documentation, development references and release history |

See [Architecture — Russian](ARCHITECTURE.md) for module ownership and [Contributing](../CONTRIBUTING.md) before submitting changes.

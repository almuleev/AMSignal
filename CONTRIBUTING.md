# Contributing

Thanks for your interest in improving `AMSignal`.

## Before You Start

- Check existing issues before opening a new one.
- For UI changes, include screenshots or a short screen recording when possible.
- For parser, FFT or export changes, include a reproducible sample file or a minimal test case.

## Build

Start with [Build and CLI](docs/BUILD_EN.md) ([русская версия](docs/BUILD_RU.md)).
The [architecture map](docs/ARCHITECTURE.md) and [testing matrix](docs/development/TESTING.md)
describe module ownership and the checks required for each change.

### GUI

```powershell
powershell -ExecutionPolicy Bypass -File .\build_gui.ps1
```

### CLI and tests

```bash
make
make test
```

## Pull Request Notes

- Keep changes focused.
- Mention behavioural changes clearly.
- Update documentation when UI, hotkeys or export behaviour changes.
- Keep the Russian and English user pages in sync; `README.md` is the English
  project page and `README_RU.md` is its Russian counterpart. `README_EN.md`
  preserves the previous entry link. See [documentation](docs/README.md).
- If you touch parser or FFT logic, add or update tests where practical.

## Reporting Bugs

Please include:

- release version or commit hash
- Windows version
- exact reproduction steps
- sample file if the issue depends on input data
- screenshot if the issue is visual

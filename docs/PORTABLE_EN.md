# A workspace you can move

[Documentation](README.md) · [Русский](PORTABLE_RU.md)

AMSignal runs as a standalone Windows EXE: no installer, Qt or installed LabVIEW is required. Settings live in `AMS.ini` beside the application; an `.AMSig` project contains source samples and supported analysis and annotation settings.

## What to copy

| File | Purpose |
| --- | --- |
| `AMSignal-<version>-x64.exe` | Application |
| `AMS.ini` beside the EXE | Language, theme, shortcuts and local preferences |
| Your `.AMSig` files | Source samples, processing and restorable document state |
| FRF CSV and experiment notes | Calculation passport and results, physical experiment conditions |
| Original LVM/TXT/CSV if needed | Archive of primary measurements and work without a project |

## Resume on another computer

1. Save open documents as `.AMSig` and close the application so local settings are written.
2. Copy the EXE, `AMS.ini` and required projects to a writable folder on another Windows x64 PC.
3. Run the EXE from the new folder. Open a project using **Open**.
4. Check units, processing and the analysis interval. Starting with v0.17.8-cpu.2, new projects restore FRF roles, method, L, mean removal, reference threshold and smoothing. Switch to FRF mode to recalculate.

A project contains the data and does not require the original LVM/TXT/CSV to read its saved samples. Projects from versions before v0.17.8-cpu.2, including v0.17.7 and v0.17.8-cpu.1, do not contain FRF roles, method or L: restore them from CSV or experiment notes. A new build cannot recover information absent from an older file. FRF results are recalculated when entering that mode; CSV stores numerical results and the calculation passport. Record physical conditions, calibrations and acceptance criteria in experiment notes. Recent files can retain old absolute paths; open the copied projects from their new folder.

## Settings, cache and Windows

The application folder must be writable to save `AMS.ini`. Use your own working folder if a system directory is protected from writes.

Indexes, caches and calculation scratch files can live outside the EXE folder. You do not need to move them to resume: they are regenerated and require free disk space. A cache does not replace a source file or saved project.

On launch, AMSignal registers the `.AMSig` association for the current Windows user without administrator rights. After moving it, run the EXE from its new folder; Windows controls the system default application choice.

[Projects and export](manual/AMSignal_User_Manual_EN.md) · [Analysis resources](ANALYSIS_EN.md)

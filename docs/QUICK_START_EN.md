# Quick start

[Documentation](README.md) · [Русский](QUICK_START_RU.md)

## Download and open

1. Open the [latest release](https://github.com/almuleev/AMSignal/releases/latest).
2. Download `AMSignal-<version>-x64.exe`. The **Source code** archives are for building, not ready-to-run application bundles.
3. Run the EXE. Choose English or Russian and a light or dark theme.
4. Use **Open file…** or drag a recording into the window. `.lvm`, `.txt`, `.csv` and `.AMSig` are supported.

To load part of a time recording, choose **Open interval…** on the start screen
or **File → Open recording interval…**. Select the file, enter the start and end
in seconds, then click **Load**. Original samples and times are preserved; the
document title shows the actual bounds. Analysis and export use this fragment,
and the filter starts at its beginning. At least two samples are required.
Without an index, reading the text preceding the interval can take time.
Ordinary opening and drag and drop still open the entire recording.

No LabVIEW or Qt installation is needed. The GUI is portable; it writes `AMS.ini` beside its executable and registers `.AMSig` for the current Windows user. Analysis can create disk caches. See [portable behaviour and signing](CODE_SIGNING.md).

To follow the screenshots, download [demo-signals.txt](https://raw.githubusercontent.com/almuleev/AMSignal/main/examples/demo-signals.txt) with **Save link as**. It is [synthetic demonstration data](../examples/README.md).

## Inspect a signal

1. Select **Signal** (`S` with default shortcuts).
2. Use the **Channels** tab to show the required curves. Channel names and colours are editable.
3. Zoom with the mouse wheel; drag the graph to pan. **Auto zoom** (`A`) adjusts Y.
4. In the bottom of **Channels**, enter the visible range in seconds and apply it with **Show range** or Enter.
5. Enable **Magnet (Snap)** (`G`), choose **Points** (`P`) and click a curve to place a measurement.

![Signal workspace](assets/screenshots/signal-en.png)

## Calculate FFT

1. In the Signal view choose **Analysis → Entire recording**, or specify **Interval**. **Use visible interval** explicitly copies the current time view.
2. Leave the channels you want to analyse visible.
3. Switch to **Spectrum** (`F`) and wait for the background calculation.
4. Use the visible frequency range in **Channels** to inspect peaks. Frequency zoom does not change the analysis interval.

With no explicit interval, FFT and FRF use the entire recording. Simply zooming the signal does not select the calculation input.

## Calculate FRF

1. Open a recording with synchronised excitation and response channels.
2. Switch to **FRF** (`R`). Select **Reference** and **Response** channels; the roles must not overlap.
3. For the demo choose `Reference A` and `Reference B` as References and `Response A` / `Response B` as Responses. The parser removes bracketed units from channel display names.
4. Start with **H1 Welch**, `L = 0` (Auto), and calculate. For the reproducible gallery view use `L = 2048`.
5. Inspect the dynamic coefficient `KD = abs(H)`, the separate mean-reference spectrum and coherence diagnostics.

![Calculated FRF with two references and two responses](assets/screenshots/frf-en.png)

*The scene shows calculated H1 for two responses against the mean of two references: L=2048, K=31, 0.5 Hz spacing, smoothing off.*

Several Reference channels are averaged before estimation; this is not MIMO. Coherence is a diagnostic value. Read [analysis assumptions](ANALYSIS_EN.md) before drawing experimental conclusions.

## Save and resume

| Goal | Command and result |
| --- | --- |
| Continue working later | **Save project**, `Ctrl+S`: `.AMSig` with raw samples and restorable settings |
| Save a graph | **Save PNG**, `Ctrl+Alt+S` |
| Export channels or results | **Save as**, `Ctrl+Shift+S`: choose purpose, format, range and channels |

Signal export supports CSV/TXT/LVM; spectrum export supports CSV/TXT; FRF supports CSV and PNG. Original-channel export omits processing. A `Frequency` CSV/TXT can reopen as a stored spectrum; FRF CSV cannot reopen as a FRF document.

## Next steps

[Full manual](manual/AMSignal_User_Manual_EN.md) · [Analysis and limits](ANALYSIS_EN.md) · [Build and CLI](BUILD_EN.md) · [Gallery](GALLERY_EN.md)

# AMSignal user manual

Application version: 0.17.5
Document language: English

[Documentation](../README.md) · [Русский](AMSignal_User_Manual_RU.md) · [Quick start](../QUICK_START_EN.md)

## 1. Purpose and terminology

AMSignal opens measurement recordings, displays time signals, builds amplitude spectra with FFT and estimates frequency response functions. This manual covers the graphical application; CLI commands are on the [build page](../BUILD_EN.md).

A **channel** is a sensor value series. **Original data** are file samples without processing. **Processed data** are the result of the global formula, individual formula and then the filter. The **analysis interval** is the shared time input to FFT/FRF, independent of graph zoom. **Reference** is the input, **Response** is the output. **KD** is `abs(H)`, the linear magnitude of the transfer function.

The app helps analyse an experiment but does not replace verification of sensors, units, synchronisation, sampling rate and excitation quality. A warning is a reason to examine the data, not a certification of the result.

## 2. Starting, files and documents

Download the GUI EXE from the [latest release](https://github.com/almuleev/AMSignal/releases/latest). Source code archives are for building. The start screen provides language, theme, file opening, recent recordings, settings and shortcut help. **Help → Start screen** returns to version information.

Supported files are `.lvm`, `.txt`, `.csv` and `.AMSig` projects. A time recording needs a time column and at least one numeric channel. FRF needs at least two synchronised channels. In tabular TXT, `X_Value` is a suitable time-column heading; see the [demonstration file](../../examples/demo-signals.txt). Bracketed units are removed from displayed channel names.

Use **Open** or drag files into the window. Several files load sequentially, each as a separate document. The top selector switches documents; the adjacent `×` closes the active one. The list is also under **File → Open files**. Documents have independent data, annotations, FRF roles, interval and history; a late result from an old calculation is not applied to a new document.

An asterisk in the title means unsaved changes. Closing a modified document offers save, close without saving or cancel. Save the active project before moving to an alternative processing setup.

CSV/TXT with a `Frequency` column opens as a stored spectrum. It has no time samples, so Signal and FRF calculation are unavailable. An exported FRF CSV cannot reopen as an FRF document.

The app writes `AMS.ini` beside its EXE, can create caches and registers `.AMSig` for the current Windows user. See the [signing policy](../CODE_SIGNING.md) for portable behaviour and signing details.

## 3. The workspace

The top toolbar provides opening and document selection, **Signal**, **Spectrum**, **FRF**, annotation tools, Snap, annotation lock, auto scale and the side-panel toggle.

Signal and Spectrum have **Channels**, **Points** and **Filter** tabs. FRF has **Channels** for calculation settings and **Points**. The bottom visible-range controls appear only in **Channels**. The status bar reports mode, interval, parameters and warnings; hover to read the full text in a narrow window.

![Signal workspace](../assets/screenshots/signal-en.png)

The [features page](../FEATURES_EN.md) and [gallery](../GALLERY_EN.md) show the working build's native interface with a synthetic recording. It illustrates the interface rather than a physical experiment.

## 4. Navigation and analysis interval

The wheel zooms X around the pointer. Dragging pans the view; a short click adds an object for the selected tool, while movement from 4 px pans and preserves the tool and Snap. Vertical movement is available when vertical pan is enabled. **View** includes zoom, reset, beginning/end, playback and speed. Playback changes viewing, not analysis samples.

**Auto scale** restores automatic Y. FFT and FRF natural amplitude scales start at zero. The lower FRF mean-reference graph has its own Y scale; drag the divider to change its height. Ctrl/Alt with the wheel above it changes its upper limit while retaining a zero baseline.

At the bottom of **Channels**, enter the visible bounds: seconds for Signal, hertz for FFT/FRF. Confirm with Enter or **Show range**. This controls presentation.

**Analysis** defines the numerical input:

- **Entire recording** uses the complete source independently of the visible graph.
- **Interval** uses explicitly entered time bounds.
- **Use visible interval** copies the time view once.

In Signal, Shift-drag selects an interval; its edges can then be moved. Bounds use original time even with visual gap stitching. Without a selection the entire recording is used. After changing input, wait for a new FFT or calculate FRF again.

Gap markers indicate outages; clicking shows duration and estimated missing samples. Visual stitching compresses only the axis and navigation, retaining original timestamps in measurements and export. It does not recover missing measurements.

## 5. Channels

Checkboxes show curves; **All** and **None** change visibility for the list. Double-click to edit a name; use the indicator or channel-colour chooser to change colour. Changes appear in legends and exports while retaining original numbers.

The numeric field at the right sets a multiplier: `-1` reverses polarity, `0.001` scales by one thousandth. Enter or leaving the field confirms it. Enter a shared expression in the global-formula field of **Channels**.

Visibility controls displayed curves and requested FFT channels. It does not assign FRF roles: a hidden channel can still be a Reference or Response. Compare different physical quantities on one vertical scale only with their units in mind.

## 6. Points, markers, lines and history

Enable **Magnet/Snap** to measure an original sample or spectrum bin. Select **Points** and click a curve. The **Points** tab creates, renames, hides and removes groups, and sets colours and labels: number, X, Y, Δx, Δy, inverse coordinate difference and distance. In Signal the inverse difference is `1/Δt`; in frequency modes it is `1/Δf`. This does not automatically identify the physical cause of a peak. Euclidean distance may have no physical interpretation when axis units differ.

An identical X/Y point cannot repeat within one group; separate groups may coincide. Removing the final point removes its group; Undo restores both.

**Marker** displays a named position and values. **Line** adds vertical or horizontal guides; exact input is preferable for a specified frequency or level. Tools remain active for repeated annotations. **Cursor** returns to selecting and moving existing objects; Snap also applies when moving them.

Delete/Backspace removes the selected object. Clear commands act on the relevant annotation type in the current mode; Signal, FFT and FRF have separate sets. Right-click does not clear annotations. Lock prevents annotation changes while retaining navigation.

Esc cancels an unfinished gesture or tool; its effect depends on state, including the gap card, playback and interval. Use **File → Undo** and **Redo** for completed changes. One drag or slider release makes one history action. History is per document, limited to 128 actions and 64 MiB of payload; an oversized action can clear it.

## 7. Formulas and digital filtering

Processing order is global formula → individual formula → filter. The source file is not rewritten. In **Channels**, the global-formula field and apply button set an expression for all channels; use the numeric multipliers in channel rows for individual adjustments. Formulas saved in a project are restored with its processing. Digital filters are on the **Filter** tab.

Supported expressions include `x`, numbers, parentheses, arithmetic and `abs`, `sqrt`, `sin`, `cos`, `tan`, `log`, `exp`. Examples: `2.5*x`, `-x`, `x+0.15`, `sqrt(abs(x))`. Check domains and units: `log(x)` is invalid for non-positive arguments.

The filter provides low-pass, high-pass, band-pass and notch modes. Cutoffs must respect the available Nyquist frequency. Butterworth targets a smooth amplitude response, Bessel phase behaviour, Chebyshev allows ripple, and Linkwitz–Riley supports aligned band separation. Assess the chosen filter for your measurement method.

A slider applies on release and creates one Undo action. The filter has finite roll-off rather than setting everything beyond a cutoff to zero. Processing changes invalidate analysis and can affect annotations; configure processing before placing final measurement points.

Signal/FFT use active processing. FRF explicitly selects processed or original channels. References are processed before averaging. A causal filter may need warm-up from the source beginning for a distant interval; continuation caches help when resources permit.

## 8. FFT

1. Define the interval under **Analysis**, check channels and processing.
2. Switch to **Spectrum** and wait for background calculation.
3. Set a frequency view, examine peaks and place points.
4. Save PNG or spectrum CSV/TXT.

FFT removes the mean and displays one-sided linear amplitude. Frequency spacing is `Fs/N`. Short intervals give a coarse grid; long ones may mix physical operating states. A peak does not prove where an oscillation originates.

Uneven timestamps are prepared on an even grid with the same sample count; large gaps are compressed while retaining available samples. Interpolation can attenuate high frequencies; NaN values are replaced by the channel mean. Prefer a continuous uniform recording. See [analysis notes](../ANALYSIS_EN.md) for preparation and cadence-estimation limits.

Results are cached by source, interval, processing and channels. Showing an already calculated channel again does not need another FFT; a new channel or input requires an update. Spectrum export includes preparation information; FFT does not export LVM.

## 9. FRF

1. Open a synchronised excitation/response recording and choose an interval.
2. Under **FRF → Channels**, select non-empty, non-overlapping References and Responses.
3. Choose processed or original channels, H1 Welch or Direct, and `L`.
4. Select **Calculate**, inspect KD, the mean-reference spectrum and coherence status.
5. Export CSV and PNG, and separately record the experiment settings.

Role menus allow several channels and remain open during selection. Clearing a menu clears only that role. References are averaged sample by sample; each Response is compared with the common mean Reference. This is not MIMO. An error in one Response must not hide other valid results.

H1 Welch uses periodic Hann and 50% overlap. `L = 0` is Auto; manual L must be even, from 4 to N. A larger L improves frequency spacing but reduces segment count. The incomplete tail is not zero-padded. When enabled, mean removal is per segment.

Direct uses one segment for the entire selected sequence: `L=N`, `K=1`, no overlap; in valid bins the result is the complex spectral ratio. It is a comparison method sensitive to noise and non-stationarity, not a universal H1 replacement.

FRF uses a sample sequence with Fs estimated from a typical positive time step. It does not insert, remove or interpolate samples at gaps. Welch segments can cross gaps and LVM sections; warnings require an assessment of the experiment. Non-finite input is rejected.

KD is `abs(H)`, not dB, with Response units divided by Reference units. The mean-reference spectrum has a separate scale. DC and weak-reference bins are excluded; a true zero Response is retained. Coherence has separate validity and is unavailable for Direct, K<2 and zero output power. It does not correct KD automatically.

Linear/log frequency, zoom, lower-graph height and smoothing from off to 1/3 octave change presentation without recalculation. Smoothing changes neither complex H nor CSV. Phase is not plotted; H2 and MIMO are not implemented.

## 10. Projects and export

| Goal | Result |
| --- | --- |
| Save project | `.AMSig`: original samples and restorable settings |
| Save processed channels | Formulas and filtering applied to the data |
| Save original channels | All original channels without processing or annotations |
| Save PNG | Current graph with visible curves, styling and annotations |

**Save project** uses Ctrl+S. **Save as** (Ctrl+Shift+S) offers purpose, format, range and content. Project format is fixed; channel results offer applicable CSV/TXT/LVM formats. Signal supports all three, Spectrum CSV/TXT, FRF CSV. Save PNG separately with Ctrl+Alt+S.

For Signal, distinguish selected, visible and entire ranges. Spectrum ranges are in frequency. Processed export can include hidden channels, names, formulas, filter and annotation/graph metadata; original export retains original channels without these settings.

A project retains names, visibility, processing, annotations, interval and supported presentation settings. It is not an experiment protocol: additionally keep FRF roles, method, L and the actual result in CSV and experiment notes. A write error must not mark the project saved or replace a previous file with a partial one.

FRF CSV includes frequency, KD, real/imaginary H, valid, coherence/coherence_valid, the reference spectrum and Response identifiers. Metadata includes calculation parameters; check exact columns in your version's file. This CSV has no time samples for recalculation.

PNG uses the shared graph renderer and a Generated by AMSignal signature. Before publication, check units, legend, range and accidental objects. Curve symbols are available for monochrome printing. Time-curve smoothing and symbols are presentation rather than digital processing.

## 11. Large recordings and resources

The source opens fully in RAM or through a complete disk overview automatically. There is no separate Light Mode or compulsory manual fragment selection during normal opening. The min/max overview is for rendering; exact Snap and analysis use original samples.

The **Data + analysis** budget is Auto or 64–65536 MiB, considering available memory and system headroom. It is not a process-wide memory cap. Process RAM/private commit, recording-cache quota and temporary analysis files are distinct quantities.

FFT can use temporary disk storage while preserving N and the frequency grid. It requires free space and time; resource or I/O failures may stop calculation. If a source file changes during work, reopen it. Do not remove sources or working files in use.

The current test scope is up to **10,000,000,000 bytes inclusive**. This is not a hard opening limit or a guarantee of any analysis on every machine. Old measurements apply to their recorded versions and conditions. See [analysis and limitations](../ANALYSIS_EN.md).

## 12. Settings and shortcuts

**Settings → General settings** provides language, theme, axis labels, gap markers and resources. Settings are written to AMS.ini. Theme and language do not change numbers; labels should include physical units. The global gap-marker preference applies across documents and is not part of a project.

**Help → Keyboard shortcuts** displays current assignments. They can be changed, cleared or reset; conflicts must be resolved. Ordinary keys type text in an edit field rather than switching modes.

| Action | Default |
| --- | --- |
| Open | Ctrl+O |
| Project / Save as / PNG | Ctrl+S / Ctrl+Shift+S / Ctrl+Alt+S |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z |
| Signal / Spectrum / FRF | S / F / R |
| Cursor / Points / Snap / Lock | N / P / G / L |
| Marker / Vertical / Horizontal line | M / V / H |
| Exact line | Shift+L |
| Channels / Points / Filter | Ctrl+1 / Ctrl+2 / Ctrl+3 |
| Panel / Auto scale | B / A |
| Interval / Visible interval / Entire recording | Ctrl+I / Ctrl+Shift+I / Ctrl+Alt+I |
| Filter / Stitch gaps | Shift+F / Shift+G |
| Theme / Playback | D / Space |
| Reset / Beginning / End | Home / Ctrl+Home / Ctrl+End |
| Shortcut help | F1 |

## 13. Troubleshooting

| Symptom | Check |
| --- | --- |
| File will not open | Format, access, numeric rows and parser warnings; FRF CSV needs the original signal |
| FFT unavailable or unexpected | Job completion, interval, visibility, processing, units, gaps and stationarity |
| FRF unavailable | Time samples, non-empty non-overlapping roles, valid L and finite values |
| Noisy KD or low coherence | Reference amplitude, synchronisation, noise, interval and averaging; do not hide a problem with smoothing alone |
| Coherence unavailable | Direct, K<2, weak reference, zero output power or an invalid bin |
| A large recording is slow | Free space, RAM/commit, disk path and filter warm-up; reduce the interval consciously |
| Source changed or result is stale | Save needed work, reopen the source and recalculate |
| Saving failed | Path, access and free space; do not consider the document saved |

Bug reports should include the version, Windows version, steps, parameters, full status and a small non-sensitive sample. Use the [private channel](../../.github/SECURITY.md) for vulnerabilities.

## 14. Method and reproducibility

Notation: N is sample count, Fs sampling frequency, L segment length, K segment count. For FFT, `f[k]=k*Fs/N`; for H1, `df=Fs/L`.

`X[k]=sum(x[n]*exp(-j*2*pi*k*n/N))`. After mean removal, one-sided internal-bin amplitude is `2*abs(X)/N`; DC and Nyquist use `abs(X)/N`, with the corresponding treatment for odd N.

For H1 segment spectra: `Sxx=sum(abs(Xm)^2)`, `Syy=sum(abs(Ym)^2)`, `Sxy=sum(conj(Xm)*Ym)`; `H1=Sxy/Sxx`, `KD=abs(H1)`. Spectral accumulation precedes the ratio; averaging completed block FRFs is not equivalent to H1.

`coherence=abs(Sxy)^2/(Sxx*Syy)` in valid bins. It lies in 0…1, but a value near 1 does not rule out systematic error. Weak reference is masked by a relative power threshold of approximately `max(Sxx)*10^-12` with numerical protection; this is a technical threshold, not a physical experiment-quality criterion.

The mean-reference linear amplitude for internal H1 bins is `Aref=2*sqrt(mean(abs(Xm)^2))/sum(Hann)`; edge bins have the corresponding one-sided normalisation. Do not confuse this scale with KD. An identical linear filter on input and output can cancel in their ratio, so examine the reference spectrum separately.

Keep the source, project, actual version, units, interval, roles, formulas, filter, method, N/L/K, frequency view, smoothing and warnings. For a paper, state these in the method and figure caption. Do not infer a resonance cause from KD shape alone or treat different inputs/processing as equivalent without justification.

## 15. Practical workflows and resources

- **Check a new recording:** open it, inspect channels, units and gaps; save a project before processing.
- **Find a frequency:** select a steady-state interval, calculate FFT, measure the peak, save CSV and PNG.
- **Compare responses:** assign one common Reference and several Responses, calculate H1, inspect excitation and coherence.
- **Prepare a figure:** set clear axes and a legend, remove stray annotations, check PNG and retain settings with CSV.

[Example](../../examples/README.md) · [Gallery](../GALLERY_EN.md) · [Analysis and limitations](../ANALYSIS_EN.md) · [Build and CLI](../BUILD_EN.md)

The previous expanded edition and old DOCX are historical; current language versions are listed on the [manuals page](README.md).

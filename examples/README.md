# Demonstration recording

[Project](../README.md) · [Русский](README_RU.md) · [Quick start](../docs/QUICK_START_EN.md)

[`demo-signals.txt`](demo-signals.txt) is a tab-separated **synthetic** signal recording for trying the interface and reproducing the documentation scenes. It contains 32,768 samples at 1024 Hz, from 0 to 31.9990234375 seconds, and four channels in illustrative volts:

| Channel | Role |
| --- | --- |
| `Reference A [V]` | First reference: broadband excitation with 12 tones and a small noise addition |
| `Reference B [V]` | Second synchronous reference of the same excitation, with independent noise |
| `Response A [V]` | Output of a second-order model, natural frequency 32 Hz, damping ratio 0.045 |
| `Response B [V]` | Output of a second-order model, natural frequency 75 Hz, damping ratio 0.065 |

The response models use `H(s) = w0^2 / (s^2 + 2*zeta*w0*s + w0^2)`, `w0 = 2*pi*f0`, discretised with the bilinear transform at 1024 Hz. They start at zero; 4096 warm-up samples are omitted before the exported sequence. The input is a sum of 12 sinusoids with seeded random phases and uniform noise `0.45*u`. Tones: 12, 24, 39, 48, 61, 88, 103, 117, 136, 151, 173 and 192 Hz. Python's `random.Random(1729)` generates phases first, then broadband noise. Both references receive independent `0.01*u` additions from `random.Random(1730)`. Both response models are driven by the arithmetic mean of these references. These are two synchronous measurements of shared excitation, not two independent MIMO inputs. The full reproducible recipe is [`generate_demo.py`](generate_demo.py), with no third-party libraries; `python examples/generate_demo.py` recreates only this TXT.

This example is not a physical experiment, calibrated benchmark or numerical certification of the app. Finite windows, excitation strength and leakage affect the estimated curves; FRF peaks need not coincide exactly with continuous-model natural frequencies.

## Reproduce the scenes

1. Open the TXT in AMSignal; show all four channels.
2. For the Signal scene set the visible range to 1–1.5 s. Keep **Analysis → Entire recording** selected.
3. Switch to Spectrum; use a 0–210 Hz view to inspect the tones.
4. Switch to FRF, select `Reference A` and `Reference B` as References and both Responses, choose H1 Welch and `L = 2048`, disable smoothing, then calculate. The entire recording gives 31 segments and a 0.5 Hz frequency spacing. Use a 5–210 Hz frequency view. Bracketed units are removed from display names.
5. Switch the interface language or theme to reproduce the matching gallery variant.

The recording is distributed under the repository's [GPLv3 license](../LICENSE). The existing [`lvm_files_for_tests/test.lvm`](../lvm_files_for_tests/test.lvm) remains available as a LabVIEW-format example.

// Spectral analysis built on top of the parsed Dataset.
//
// Mean removal and one-sided amplitudes. Irregular timestamps are resampled.
// Large timestamp gaps are ignored, while samples on both sides stay in FFT.
#pragma once

#include <string>
#include <vector>

#include "lvm_parser.hpp"
#include "analysis_resources.hpp"

namespace lvm {
class DiskSpectrum;
class RecordingSource;

struct Peak {
    double freq = 0.0;
    double amp = 0.0;
};

struct Spectrum {
    // Declared first: output arrays die before their admission reservation.
    std::shared_ptr<MemoryBudget::Reservation> memory_reservation;
    std::vector<double> freqs;             // length N/2 + 1
    std::vector<std::string> names;        // channels that produced a spectrum
    std::vector<std::vector<double>> amp;  // per channel, length N/2 + 1
    std::vector<std::size_t> source_channels; // indices in the input Dataset, independent of names
    std::shared_ptr<const DiskSpectrum> disk;
    std::shared_ptr<const RecordingSource> recording_source;
    std::size_t bin_count() const;
    double frequency(std::size_t bin) const;
    std::pair<std::size_t,std::size_t> bin_range(double start,double end) const;
    void read_amplitudes(std::size_t channel,std::size_t first,std::size_t count,std::vector<double>& values) const;
    double source_start = 0.0;
    double source_end = 0.0;
    double sample_dt = 0.0;
    double nyquist = 0.0;
    int n = 0;                             // number of samples used
    bool resampled = false;                // linear interpolation onto a uniform time grid
    bool gaps_ignored = false;             // timestamp gaps were compressed; all samples remain
    bool imported = false;                 // amplitudes read directly, no FFT was performed
    bool ok = false;
    std::string error;
};

// Compute the magnitude spectrum for every channel. `max_samples > 0` caps the
// sample count using the first N selected samples. Recognized timestamp gaps
// (> 1.5 estimated sample steps) are compressed to one typical step, so every available
// sample in the selected range is retained. Values are linearly interpolated
// onto an evenly spaced grid based on this gap-free timeline. See sampling.hpp
// for cadence inference. NaN channel values are filled with the channel mean;
// they are not treated as missing timestamps and are not removed independently.
Spectrum compute_spectrum(const Dataset& ds, int max_samples, const std::atomic<bool>* cancel = nullptr,
                          const AnalysisResources& resources = {});

// Return up to `count` strongest spectral peaks (local maxima), excluding DC.
std::vector<Peak> find_peaks(const std::vector<double>& freqs,
                             const std::vector<double>& amp, int count);

}  // namespace lvm

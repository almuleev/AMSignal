#pragma once
#include <atomic>
#include <vector>
#include <complex>

namespace lvm {

enum class FrfEstimator { Direct, H1 };
enum class FrfWindow { Hann };
enum class FrfError {
    None, InvalidChannels, FrequencyData, TooShort, InvalidTime,
    MissingValues, InvalidOptions, WeakReference, Overflow
};

struct FrfOptions {
    FrfEstimator estimator = FrfEstimator::H1;
    FrfWindow window = FrfWindow::Hann;
    bool remove_mean = true;
    // Amplitude threshold relative to the strongest non-DC reference bin.
    double reference_threshold = 1e-6;
    // 0 = automatic. H1 requires an even length >= 4; overlap is fixed at 50%.
    std::size_t segment_length = 0;
};

// File/GUI-independent pair, still on the acquisition timeline.
struct FrfInput {
    std::vector<double> time, reference, response;
};

// Unmodified selected samples on the implicit uniform timeline t[n] = n/Fs.
// Timestamp gaps are diagnostic only and never delimit Welch segments.
struct FrfSamples {
    std::vector<double> reference, response;
    double sample_dt = 0;
    double source_start = 0, source_end = 0;
    std::size_t source_count = 0;
    bool gaps_ignored = false;
    FrfError error = FrfError::None;
};

struct FrfResult {
    std::vector<double> frequencies;
    std::vector<std::complex<double>> transfer;
    std::vector<unsigned char> valid;
    // One-sided linear amplitude spectrum of the (possibly averaged)
    // reference used by this result. It is kept separately from |H| so the
    // GUI can show the actual excitation without mixing units with KD.
    std::vector<double> reference_amplitude;
    std::vector<unsigned char> reference_amplitude_valid;
    std::vector<double> coherence;
    std::vector<unsigned char> coherence_valid;
    FrfOptions options;
    double sample_dt = 0.0;
    double source_start = 0.0, source_end = 0.0;
    std::size_t sample_count = 0;
    std::size_t segment_length = 0, averages = 0, overlap_samples = 0;
    bool gaps_ignored = false;
    bool ok = false;
    FrfError error = FrfError::None;
    // Complex H is retained for phase; validity of coherence is independent.
};

// Array-only batch: arithmetic mean of references before Welch, one result
// per response in the original order. Channel identities belong to the caller.
struct FrfBatchInput {
    std::vector<double> time;
    std::vector<std::vector<double>> references, responses;
};
struct FrfBatchResult {
    FrfOptions options;
    std::vector<FrfResult> responses;
    bool ok = false; // at least one response succeeded
    FrfError error = FrfError::None;
    const FrfResult& common() const;
};
FrfBatchResult analyze_frf_batch(FrfBatchInput input, const FrfOptions& options = {},
                                 const std::atomic<bool>* cancel = nullptr);

FrfSamples prepare_frf_samples(const FrfInput& input,
                               const std::atomic<bool>* cancel = nullptr);
// Numerical estimator takes prepared arrays and their uniform cadence,
// never Dataset/channel indices. Dynamic coefficient is strictly abs(H).
FrfResult compute_frf(const FrfSamples& samples,
                      const FrfOptions& options = {}, const std::atomic<bool>* cancel = nullptr);
FrfResult analyze_frf(const FrfInput& input, const FrfOptions& options = {},
                      const std::atomic<bool>* cancel = nullptr);
// A true zero response has coefficient 0; an invalid bin is NaN.
double frf_dynamic_coefficient(const FrfResult& result, std::size_t bin);
const char* frf_error_text(FrfError error);

} // namespace lvm

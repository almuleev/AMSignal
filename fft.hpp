// Small dependency-free FFT used for the Hz / spectrum mode.
//
// `dft` accepts an arbitrary length (not just powers of two) by falling back to
// Bluestein's algorithm, so the spectrum matches numpy's rfft used by the
// Python viewer.
#pragma once

#include <complex>
#include <atomic>
#include <vector>

namespace lvm {
// Actual retained Bluestein storage across all callers/helpers, including idle
// threads. Schedulers add prospective workspace/plans separately.
std::size_t fft_cached_plan_bytes();

// In-place iterative radix-2 FFT. `a.size()` must be a power of two.
// inverse=true computes the IFFT (normalised by 1/N).
// Cancellation is checked between chunks of at most 16384 butterflies,
// outside the arithmetic loop; partial in-place data is unspecified on cancel.
void fft_radix2(std::vector<std::complex<double>>& a, bool inverse, const std::atomic<bool>* cancel = nullptr);

// Forward DFT of an arbitrary-length signal.
// Bluestein reuses one completed chirp/kernel plan per thread (up to 64 MiB);
// larger plans are temporary. Neither FFT length nor frequency grid changes.
std::vector<std::complex<double>> dft(const std::vector<std::complex<double>>& in, const std::atomic<bool>* cancel = nullptr);

}  // namespace lvm

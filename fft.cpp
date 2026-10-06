#include "fft.hpp"

#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace lvm {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kButterflyChunk = 16384;
constexpr std::size_t kPlanCacheBytes = 64 * 1024 * 1024;
struct BluesteinPlan {
    std::size_t n = 0;
    std::vector<std::complex<double>> chirp, kernel;
};
void check_cancel(const std::atomic<bool>* cancel) {
    if (cancel && cancel->load(std::memory_order_relaxed)) throw std::runtime_error("Operation cancelled.");
}
inline void butterfly_range(std::vector<std::complex<double>>& a, std::size_t i,
                            std::size_t half, std::size_t first, std::size_t last,
                            std::complex<double>& w, const std::complex<double>& wlen) {
    for (std::size_t k = first; k < last; ++k) {
        const std::complex<double> u = a[i + k];
        const std::complex<double> v = a[i + k + half] * w;
        a[i + k] = u + v;
        a[i + k + half] = u - v;
        w *= wlen;
    }
}
}

void fft_radix2(std::vector<std::complex<double>>& a, bool inverse, const std::atomic<bool>* cancel) {
    check_cancel(cancel);
    const std::size_t n = a.size();
    if (n <= 1) return;
    if ((n & (n - 1)) != 0) throw std::invalid_argument("Radix-2 FFT requires a power-of-two size.");

    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        if ((i & 0xFFFF) == 0) check_cancel(cancel);
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        check_cancel(cancel);
        const double ang = 2.0 * kPi / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        const std::size_t half = len / 2;
        if (half <= kButterflyChunk) {
            // Group short blocks: k=0 in each block must not cause a cancel
            // call (and impede optimisation) in the arithmetic loop.
            const std::size_t span = std::min(n, 2 * kButterflyChunk);
            for (std::size_t first = 0; first < n; first += span) {
                check_cancel(cancel);
                const std::size_t last = first + span;
                for (std::size_t i = first; i < last; i += len) {
                    std::complex<double> w(1.0, 0.0);
                    butterfly_range(a, i, half, 0, half, w, wlen);
                }
            }
        } else {
            for (std::size_t i = 0; i < n; i += len) {
                std::complex<double> w(1.0, 0.0);
                for (std::size_t first = 0; first < half; first += kButterflyChunk) {
                    check_cancel(cancel);
                    // Keep w across chunks: restarting it changes rounding.
                    butterfly_range(a, i, half, first, first + kButterflyChunk, w, wlen);
                }
            }
        }
    }

    if (inverse) {
        for (std::size_t first = 0; first < n; first += kButterflyChunk) {
            check_cancel(cancel);
            const std::size_t last = std::min(n, first + kButterflyChunk);
            for (std::size_t i = first; i < last; ++i) a[i] /= static_cast<double>(n);
        }
    }
    check_cancel(cancel);
}

std::vector<std::complex<double>> dft(const std::vector<std::complex<double>>& in, const std::atomic<bool>* cancel) {
    const std::size_t n = in.size();
    check_cancel(cancel);
    if (n == 0) return {};

    // Power-of-two fast path.
    if ((n & (n - 1)) == 0) {
        std::vector<std::complex<double>> a = in;
        fft_radix2(a, false, cancel);
        return a;
    }

    // Bluestein's algorithm for arbitrary length.
    if (n > std::numeric_limits<std::size_t>::max() / 2) throw std::length_error("FFT is too large.");
    std::size_t m = 1;
    while (m < 2 * n - 1) {
        if (m > std::numeric_limits<std::size_t>::max() / 2) throw std::length_error("FFT is too large.");
        m <<= 1;
    }

    // One bounded plan per worker/thread. Publish only a complete plan; a
    // cancelled or failed rebuild must never leave reusable partial data.
    static thread_local BluesteinPlan cached;
    BluesteinPlan temporary;
    const BluesteinPlan* plan = &cached;
    if (cached.n != n) {
        cached = {};
        temporary.chirp.resize(n);
        for (std::size_t k = 0; k < n; ++k) {
            if ((k & 0x3FFF) == 0) check_cancel(cancel);
            // angle = -pi * k^2 / n, reduced mod 2n to keep precision for large k.
            const unsigned long long sq = (static_cast<unsigned long long>(k) * k) % (2ULL * n);
            const double ang = -kPi * static_cast<double>(sq) / static_cast<double>(n);
            temporary.chirp[k] = std::complex<double>(std::cos(ang), std::sin(ang));
        }
        temporary.kernel.assign(m, {0.0, 0.0});
        for (std::size_t k = 0; k < n; ++k) {
            if ((k & 0x3FFF) == 0) check_cancel(cancel);
            temporary.kernel[k] = std::conj(temporary.chirp[k]);
            if (k != 0) temporary.kernel[m - k] = std::conj(temporary.chirp[k]);
        }
        fft_radix2(temporary.kernel, false, cancel);
        temporary.n = n;
        check_cancel(cancel);
        if (m <= kPlanCacheBytes / sizeof(std::complex<double>) &&
            n <= kPlanCacheBytes / sizeof(std::complex<double>) - m) {
            cached = std::move(temporary);
        } else {
            plan = &temporary; // Large plans live only for this call.
        }
    }
    const auto& chirp = plan->chirp;
    const auto& b = plan->kernel;

    std::vector<std::complex<double>> a(m, {0.0, 0.0});
    for (std::size_t k = 0; k < n; ++k) {
        if ((k & 0x3FFF) == 0) check_cancel(cancel);
        a[k] = in[k] * chirp[k];
    }

    fft_radix2(a, false, cancel);
    for (std::size_t first = 0; first < m; first += kButterflyChunk) {
        check_cancel(cancel);
        const std::size_t last = std::min(m, first + kButterflyChunk);
        for (std::size_t i = first; i < last; ++i) a[i] *= b[i];
    }
    fft_radix2(a, true, cancel);

    std::vector<std::complex<double>> out(n);
    for (std::size_t first = 0; first < n; first += kButterflyChunk) {
        check_cancel(cancel);
        const std::size_t last = std::min(n, first + kButterflyChunk);
        for (std::size_t k = first; k < last; ++k) out[k] = a[k] * chirp[k];
    }
    check_cancel(cancel);
    return out;
}

}  // namespace lvm

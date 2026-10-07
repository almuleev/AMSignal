#include "analysis.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

#include "fft.hpp"
#include "sampling.hpp"

namespace lvm {
namespace {
// One admitted batch at a time. Contending callers use their sequential path.
// Jobs never recursively submit work; the caller also processes channels.
class SpectrumChannelPool {
    std::mutex admission_, mutex_;
    std::condition_variable ready_, done_;
    std::vector<std::thread> threads_;
    const std::function<void(std::size_t)>* job_ = nullptr;
    std::atomic<std::size_t> next_{0};
    std::size_t count_=0, generation_=0, pending_=0, participants_=0;
    bool stopping_=false;

    void drain() {
        for (;;) {
            const auto c=next_.fetch_add(1,std::memory_order_relaxed);
            if (c>=count_) return;
            (*job_)(c); // The job captures exceptions in its indexed result.
        }
    }
    void worker(std::size_t id) {
        std::size_t seen=0;
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            ready_.wait(lock,[&]{return stopping_ || generation_!=seen;});
            if (stopping_) return;
            seen=generation_;
            if (id>=participants_-1) continue;
            lock.unlock(); drain(); lock.lock();
            if (--pending_==0) done_.notify_one();
        }
    }
public:
    explicit SpectrumChannelPool(std::size_t participants) {
        try {
            for (std::size_t i=0;i<participants-1;++i) threads_.emplace_back([this,i]{worker(i);});
        } catch (...) {
            { std::lock_guard<std::mutex> lock(mutex_); stopping_=true; }
            ready_.notify_all();
            for (auto& thread:threads_) thread.join();
            throw;
        }
    }
    ~SpectrumChannelPool() {
        std::lock_guard<std::mutex> admission(admission_);
        { std::lock_guard<std::mutex> lock(mutex_); stopping_=true; }
        ready_.notify_all();
        for (auto& thread:threads_) thread.join();
    }
    bool run(std::size_t count,std::size_t participants,const std::function<void(std::size_t)>& job) {
        std::unique_lock<std::mutex> admission(admission_,std::try_to_lock);
        if (!admission.owns_lock()) return false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            job_=&job; count_=count; next_.store(0,std::memory_order_relaxed);
            participants_=participants; pending_=participants-1; ++generation_;
        }
        ready_.notify_all(); drain();
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock,[&]{return pending_==0;});
        job_=nullptr;
        return true;
    }
};

std::size_t spectrum_participants(std::size_t n,std::size_t channels) {
    if (n<65536 || channels<2) return 1;
    const auto hardware=std::thread::hardware_concurrency();
    std::size_t participants=std::min({channels,std::size_t(4),std::size_t(hardware ? hardware : 1)});
    // Scheduling allowance, not a process memory limit: include every possible
    // retained pool/caller plan (64 MiB each), even for idle participants, plus
    // active signal, DFT workspace, output and amplitudes. Input ownership and
    // the common axis are outside this 384 MiB allowance.
    long double scratch=36.0L*n;
    long double plan=64.0L*1024*1024;
    if ((n & (n-1))!=0) {
        std::size_t m=1; while (m<2*n-1) m*=2;
        scratch=36.0L*n+16.0L*m;
        plan=std::max(plan,16.0L*(n+m));
    }
    const long double retained=4.0L*64*1024*1024;
    const long double extra_plan=plan-64.0L*1024*1024;
    while (participants>1 && retained+participants*(scratch+extra_plan)>384.0L*1024*1024) --participants;
    return participants;
}

}


Spectrum compute_spectrum(const Dataset& ds, int max_samples, const std::atomic<bool>* cancel) {
    const auto check_cancel = [&] { if (cancel && cancel->load(std::memory_order_relaxed)) throw std::runtime_error("Operation cancelled."); };
    check_cancel();
    Spectrum spec;
    const std::size_t rows = ds.rows();
    if (ds.names.size() != ds.channels.size() ||
        std::any_of(ds.channels.begin(), ds.channels.end(), [rows](const auto& c) { return c.size() != rows; })) {
        spec.error = "Inconsistent dataset dimensions.";
        return spec;
    }
    if (ds.frequency_axis) {
        if (rows < 2) { spec.error = "A stored spectrum needs at least two frequency bins."; return spec; }
        for (std::size_t i = 0; i < rows; ++i) {
            if ((i & 0xFFFF) == 0) check_cancel();
            if (!std::isfinite(ds.time[i]) || ds.time[i] < 0 || (i && ds.time[i] <= ds.time[i - 1])) {
                spec.error = "Invalid frequency axis."; return spec;
            }
        }
        spec.freqs = ds.time;
        spec.nyquist = ds.time.back(); // Upper display bound; original sample rate may be unknown.
        spec.imported = true;
        for (std::size_t c = 0; c < ds.channels.size(); ++c) {
            for (std::size_t i = 0; i < rows; ++i) {
                if ((i & 0xFFFF) == 0) check_cancel();
                if (!std::isfinite(ds.channels[c][i]) || ds.channels[c][i] < 0) {
                    spec.error = "Spectrum amplitudes must be finite and non-negative."; return spec;
                }
            }
            spec.names.push_back(ds.names[c]); spec.source_channels.push_back(c);
            spec.amp.push_back(ds.channels[c]);
        }
        bool sampling_section = false;
        for (const auto& comment : ds.export_comments) {
            if (!comment.empty() && comment.front() == '[') { sampling_section = comment == "[fft_sampling]"; continue; }
            if (!sampling_section) continue;
            const auto equals = comment.find('=');
            if (equals == std::string::npos) continue;
            const auto key = comment.substr(0, equals), value = comment.substr(equals + 1);
            char* tail = nullptr;
            const double number = std::strtod(value.c_str(), &tail);
            if (tail == value.c_str() || *tail || !std::isfinite(number)) continue;
            if (key == "source_start") spec.source_start = number;
            else if (key == "source_end") spec.source_end = number;
            else if (key == "sample_dt" && number > 0) spec.sample_dt = number;
            else if (key == "sample_count" && number >= 0 && number <= std::numeric_limits<int>::max()) spec.n = static_cast<int>(number);
            else if (key == "resampled") spec.resampled = number == 1;
            else if (key == "gaps_ignored") spec.gaps_ignored = number == 1;
        }
        spec.ok = !spec.amp.empty();
        if (!spec.ok) spec.error = "No spectrum channels available.";
        return spec;
    }
    if (rows < 4) {
        spec.error = "Not enough samples for FFT (need at least 4).";
        return spec;
    }

    if (max_samples > 0 && max_samples < 4) {
        spec.error = "FFT sample cap leaves fewer than 4 samples.";
        return spec;
    }

    const std::size_t begin = 0;
    const std::size_t end = max_samples > 0
        ? std::min(rows, static_cast<std::size_t>(max_samples)) : rows;
    if (end > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        spec.error = "FFT is too large; select a smaller time window.";
        return spec;
    }

    // Validate the axis and estimate its typical spacing. Small clock jitter and
    // rounded timestamps are common in real measurements, not a reason to fail.
    std::vector<double> positive_dt;
    positive_dt.reserve(end - 1);
    if (!std::isfinite(ds.time.front())) { spec.error = "Invalid time axis."; return spec; }
    for (std::size_t i = 1; i < end; ++i) {
        if ((i & 0xFFFF) == 0) check_cancel();
        const double d = ds.time[i] - ds.time[i - 1];
        if (!std::isfinite(ds.time[i]) || !std::isfinite(d) || d <= 0.0) {
            spec.error = "FFT requires a finite, strictly increasing time axis.";
            return spec;
        }
        positive_dt.push_back(d);
    }
    const double typical_dt = typical_sample_spacing(positive_dt);
    check_cancel();

    const int n = static_cast<int>(end);
    spec.n = n;
    spec.source_start = ds.time[begin];
    spec.source_end = ds.time[end - 1];
    // Build the time axis used by FFT. A timestamp jump denotes unavailable
    // time, not unavailable values, so it is compressed to a normal step.
    // This retains every sample before and after every gap.
    std::vector<long double> offsets(static_cast<std::size_t>(n), 0.0L);
    // A missing sample already gives a two-step interval. Keep a margin for
    // rounding/jitter, but do not require an outage longer than four steps.
    const double gap_limit = typical_dt * sampling_gap_factor;
    for (int i = 1; i < n; ++i) {
        if ((i & 0xFFFF) == 0) check_cancel();
        double step = ds.time[static_cast<std::size_t>(i)] - ds.time[static_cast<std::size_t>(i - 1)];
        if (step > gap_limit) { step = typical_dt; spec.gaps_ignored = true; }
        offsets[static_cast<std::size_t>(i)] = offsets[static_cast<std::size_t>(i - 1)] + step;
    }
    const long double grid_step = offsets.back() / (n - 1);
    spec.sample_dt = static_cast<double>(grid_step);
    spec.nyquist = 0.5 / spec.sample_dt;
    if (!std::isfinite(spec.sample_dt) || spec.sample_dt <= 0 || !std::isfinite(spec.nyquist) || spec.nyquist <= 0) {
        spec.error = "Invalid sample spacing.";
        return spec;
    }
    // Only the compact axis matters here. The physical end may be arbitrarily
    // far away after an outage and must not suppress resampling of real jitter.
    const long double tolerance = std::max(
        2 * std::numeric_limits<long double>::epsilon() * offsets.back(),
        static_cast<long double>(spec.sample_dt) * 1e-6L);
    for (int i = 1; i < n - 1; ++i) {
        if ((i & 0xFFFF) == 0) check_cancel();
        const long double offset = offsets[static_cast<std::size_t>(i)];
        if (std::fabs(offset - i * grid_step) > tolerance) { spec.resampled = true; break; }
    }
    const int half = n / 2;  // rfft returns N/2 + 1 bins
    spec.freqs.resize(half + 1);
    for (int k = 0; k <= half; ++k) {
        spec.freqs[k] = static_cast<double>(k) / (static_cast<double>(n) * spec.sample_dt);
    }

    struct ChannelResult { std::vector<double> amplitude; std::string error; };
    const auto calculate_channel = [&](std::size_t c) {
        ChannelResult result;
        check_cancel();
        const auto& col = ds.channels[c];

        // Replace NaNs with the channel mean (needs >= 4 finite samples).
        long double sum = 0.0;
        int finite = 0;
        for (std::size_t i = begin; i < end; ++i) {
            if ((i & 0xFFFF) == 0) check_cancel();
            const double v = col[i];
            if (std::isinf(v)) { result.error = "Channel contains infinite values."; return result; }
            if (std::isfinite(v)) { sum += v; ++finite; }
        }
        if (finite < 4) return result;
        const double fill = sum / static_cast<double>(finite);

        const auto value_at = [&](std::size_t i) { return std::isnan(col[i]) ? fill : col[i]; };
        std::vector<std::complex<double>> sig(n);
        long double total = 0.0;
        std::size_t right = begin + 1;
        for (int i = 0; i < n; ++i) {
            if ((i & 0xFFFF) == 0) check_cancel();
            double clean = value_at(begin + i);
            if (spec.resampled && i > 0 && i < n - 1) {
                // Work with offsets, so a large absolute timestamp does not
                // discard precision while constructing the uniform grid.
                const long double target = i * grid_step;
                while (right < end - 1 && offsets[right] < target) {
                    ++right;
                    if ((right & 0xFFFF) == 0) check_cancel();
                }
                const long double left_offset = offsets[right - 1];
                const long double interval = offsets[right] - offsets[right - 1];
                const long double weight = std::clamp((target - left_offset) / interval, 0.0L, 1.0L);
                clean = static_cast<double>((1 - weight) * value_at(right - 1) + weight * value_at(right));
            }
            sig[i] = std::complex<double>(clean, 0.0);
            total += clean;
        }
        const double mean = total / static_cast<double>(n);
        for (auto& s : sig) s -= mean;  // remove DC

        const std::vector<std::complex<double>> spectrum = dft(sig, cancel);
        std::vector<double> amp(half + 1);
        const double interior_scale = 2.0 / static_cast<double>(n);
        const double edge_scale = 1.0 / static_cast<double>(n);
        for (int k = 0; k <= half; ++k) {
            const bool is_edge_bin = (k == 0) || (n % 2 == 0 && k == half);
            amp[k] = (is_edge_bin ? edge_scale : interior_scale) * std::abs(spectrum[k]);
            if (!std::isfinite(amp[k])) { result.error = "FFT overflow; reduce signal magnitude."; return result; }
        }

        result.amplitude=std::move(amp);
        return result;
    };
    const auto append = [&](std::size_t c,ChannelResult& result) {
        if (result.amplitude.empty()) return;
        spec.names.push_back(ds.names[c]); spec.source_channels.push_back(c);
        spec.amp.push_back(std::move(result.amplitude));
    };
    const std::size_t participants=spectrum_participants(end,ds.channels.size());
    bool parallel=false;
    if (participants>1) {
        static SpectrumChannelPool pool(4);
        std::vector<ChannelResult> results(ds.channels.size());
        std::vector<std::exception_ptr> errors(ds.channels.size());
        const auto job=[&](std::size_t c) {
            try { results[c]=calculate_channel(c); }
            catch (...) { errors[c]=std::current_exception(); }
        };
        parallel=pool.run(ds.channels.size(),participants,job);
        if (parallel) {
            for (std::size_t c=0;c<results.size();++c) {
                if (errors[c]) std::rethrow_exception(errors[c]);
                if (!results[c].error.empty()) { spec.error=results[c].error; return spec; }
                append(c,results[c]);
            }
        }
    }
    if (!parallel) {
        for (std::size_t c=0;c<ds.channels.size();++c) {
            auto result=calculate_channel(c);
            if (!result.error.empty()) { spec.error=result.error; return spec; }
            append(c,result);
        }
    }

    spec.ok = !spec.amp.empty();
    if (!spec.ok) spec.error = "No channel had enough finite samples for FFT.";
    return spec;
}

std::vector<Peak> find_peaks(const std::vector<double>& freqs,
                             const std::vector<double>& amp, int count) {
    std::vector<Peak> peaks;
    if (amp.size() < 2 || freqs.size() != amp.size()) return peaks;
    if (std::any_of(amp.begin(), amp.end(), [](double v) { return !std::isfinite(v); })) return peaks;

    // Interior local maxima, skipping DC (k = 0).
    for (std::size_t k = 1; k + 1 < amp.size(); ++k) {
        if (amp[k] > amp[k - 1] && amp[k] >= amp[k + 1]) {
            peaks.push_back({freqs[k], amp[k]});
        }
    }
    if (amp.back() > 0.0 && amp.back() > amp[amp.size() - 2]) peaks.push_back({freqs.back(), amp.back()});
    // Fallback: no interior maxima -> take the single strongest non-DC bin.
    if (peaks.empty()) {
        std::size_t best = 1;
        for (std::size_t k = 1; k < amp.size(); ++k) {
            if (amp[k] > amp[best]) best = k;
        }
        if (amp[best] > 0.0) peaks.push_back({freqs[best], amp[best]});
    }

    std::sort(peaks.begin(), peaks.end(),
              [](const Peak& a, const Peak& b) { return a.amp > b.amp; });
    if (count > 0 && static_cast<int>(peaks.size()) > count) {
        peaks.resize(static_cast<std::size_t>(count));
    }
    return peaks;
}

}  // namespace lvm

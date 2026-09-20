// Manual performance baseline for the parsing and analysis core.
//
// This executable is deliberately outside the test suite: it reports timings
// but never enforces a machine-dependent performance threshold in CI.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis.hpp"
#include "frf_analysis.hpp"
#include "lvm_parser.hpp"
#include "minmax_index.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t samples = 262144;
    std::size_t responses = 4;
    int repeats = 3;
    std::string input_path;
};

void usage(const char* program) {
    std::cout << "Usage: " << program
              << " [--samples N] [--responses N] [--repeats N] [--input FILE]\n"
              << "\n"
              << "Measures synthetic uniform signals for FFT, H1/FRF and MinMaxIndex.\n"
              << "--input additionally measures parsing of an existing LVM/TXT/CSV file.\n";
}

std::size_t parse_size(const char* value, const char* option) {
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (end == value || *end || parsed == 0 || parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error(std::string("Invalid value for ") + option);
    }
    return static_cast<std::size_t>(parsed);
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto require_value = [&](const char* option) -> const char* {
            if (++i >= argc) throw std::runtime_error(std::string("Missing value for ") + option);
            return argv[i];
        };
        if (arg == "--samples") options.samples = parse_size(require_value("--samples"), "--samples");
        else if (arg == "--responses") options.responses = parse_size(require_value("--responses"), "--responses");
        else if (arg == "--repeats") options.repeats = static_cast<int>(parse_size(require_value("--repeats"), "--repeats"));
        else if (arg == "--input") options.input_path = require_value("--input");
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); std::exit(0); }
        else throw std::runtime_error("Unknown option: " + arg);
    }
    if (options.samples < 4) throw std::runtime_error("--samples must be at least 4");
    return options;
}

template<class Work>
double median_milliseconds(int repeats, Work&& work) {
    std::vector<double> timings;
    timings.reserve(static_cast<std::size_t>(repeats));
    for (int i = 0; i < repeats; ++i) {
        const auto start = Clock::now();
        work();
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        timings.push_back(elapsed);
    }
    std::sort(timings.begin(), timings.end());
    return timings[timings.size() / 2];
}

lvm::Dataset make_fft_input(std::size_t samples) {
    constexpr double sample_dt = 1.0 / 2000.0;
    constexpr double pi = 3.14159265358979323846;
    lvm::Dataset data;
    data.ok = true;
    data.time.resize(samples);
    data.names = {"Reference", "Response_A", "Response_B"};
    data.channels.resize(data.names.size(), std::vector<double>(samples));
    for (std::size_t i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) * sample_dt;
        data.time[i] = t;
        data.channels[0][i] = std::sin(2.0 * pi * 47.0 * t) + .25 * std::sin(2.0 * pi * 131.0 * t);
        data.channels[1][i] = 1.7 * std::sin(2.0 * pi * 47.0 * t - .3) + .11 * std::sin(2.0 * pi * 311.0 * t);
        data.channels[2][i] = .55 * std::sin(2.0 * pi * 47.0 * t + .8) + .07 * std::sin(2.0 * pi * 509.0 * t);
    }
    return data;
}

lvm::FrfBatchInput make_frf_input(const lvm::Dataset& data, std::size_t responses) {
    lvm::FrfBatchInput batch;
    batch.time = data.time;
    batch.references = {data.channels[0], data.channels[0]};
    batch.references[1][0] += .001; // Keep the averaging branch in the representative workload.
    batch.responses.reserve(responses);
    for (std::size_t response = 0; response < responses; ++response) {
        std::vector<double> values(data.time.size());
        const double gain = .5 + .3 * static_cast<double>(response + 1);
        const double phase = .15 * static_cast<double>(response + 1);
        for (std::size_t i = 0; i < values.size(); ++i) {
            const double t = data.time[i];
            values[i] = gain * std::sin(2.0 * 3.14159265358979323846 * 47.0 * t - phase)
                + .03 * std::sin(2.0 * 3.14159265358979323846 * (173.0 + response) * t);
        }
        batch.responses.push_back(std::move(values));
    }
    return batch;
}

void print_result(const char* label, double milliseconds) {
    std::cout << std::left << std::setw(30) << label << std::right
              << std::fixed << std::setprecision(2) << milliseconds << " ms (median)\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const lvm::Dataset fft_input = make_fft_input(options.samples);
        const lvm::FrfBatchInput frf_input = make_frf_input(fft_input, options.responses);
        lvm::FrfOptions frf_options;
        frf_options.estimator = lvm::FrfEstimator::H1;
        frf_options.segment_length = std::min<std::size_t>(65536, options.samples & ~std::size_t(1));
        if (frf_options.segment_length < 4) frf_options.segment_length = 4;

        std::cout << "AMSignal manual performance baseline\n"
                  << "Samples: " << options.samples << ", FFT channels: " << fft_input.channel_count()
                  << ", FRF references: " << frf_input.references.size()
                  << ", FRF responses: " << options.responses << ", repeats: " << options.repeats << "\n";
        if ((options.samples & (options.samples - 1)) != 0) {
            std::cout << "Note: non-power-of-two sample count exercises Bluestein FFT.\n";
        }

        if (!options.input_path.empty()) {
            lvm::Dataset parsed;
            const double parse_ms = median_milliseconds(options.repeats, [&] {
                parsed = lvm::read_lvm_file(options.input_path);
                if (!parsed.ok) throw std::runtime_error(parsed.error);
            });
            print_result("Parse input file", parse_ms);
            std::cout << "  Parsed rows/channels: " << parsed.rows() << "/" << parsed.channel_count() << "\n";
        }

        lvm::Spectrum spectrum;
        const double fft_ms = median_milliseconds(options.repeats, [&] {
            spectrum = lvm::compute_spectrum(fft_input, 0);
            if (!spectrum.ok) throw std::runtime_error(spectrum.error);
        });
        print_result("FFT all channels", fft_ms);

        lvm::FrfBatchResult frf;
        const double frf_ms = median_milliseconds(options.repeats, [&] {
            frf = lvm::analyze_frf_batch(frf_input, frf_options);
            if (!frf.ok) throw std::runtime_error(lvm::frf_error_text(frf.error));
        });
        print_result("H1 FRF batch", frf_ms);

        MinMaxIndex envelope;
        const double index_ms = median_milliseconds(options.repeats, [&] {
            envelope.build(fft_input.rows(), [&](std::size_t i) { return fft_input.channels[1][i]; });
            const auto range = envelope.query(fft_input.rows() / 4, fft_input.rows() * 3 / 4,
                                              [&](std::size_t i) { return fft_input.channels[1][i]; });
            if (!(range.first <= range.second)) throw std::runtime_error("Invalid min/max range");
        });
        print_result("MinMaxIndex build + query", index_ms);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << "\n";
        return 1;
    }
}

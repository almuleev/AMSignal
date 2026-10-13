// LVM Reader CLI — a C++ command-line companion to the Python LVM Signal Viewer.
//
// Reads .lvm / tab-separated .txt / comma-separated .csv files, reports structure and per-channel
// statistics, prints data rows, computes an FFT spectrum, and exports CSV.
#include "analysis.hpp"
#include "data_io.hpp"
#include "version.hpp"
#include "lvm_parser.hpp"
#include "fft.hpp"
#include <thread>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <cerrno>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "processor_resources.hpp"
namespace {

const char* kVersion = APP_VERSION;
const int kDefaultFftSamples = 0;  // 0 = use all samples (correct frequency axis)
const int kDefaultPeaks = 5;

lvm::AnalysisResources automatic_fft_resources(const lvm::Dataset& data) {
    lvm::AnalysisResources resources;
#ifdef _WIN32
    MEMORYSTATUSEX status{};status.dwLength=sizeof(status);
    if(GlobalMemoryStatusEx(&status)) {
        std::uint64_t resident=(data.time.capacity()+data.raw_time.capacity())*8+lvm::fft_cached_plan_bytes();
        for(const auto& c:data.channels)resident+=c.capacity()*8;
        const lvm::ResourceSnapshot memory{status.ullAvailPhys,status.ullAvailPageFile,resident};
        resources.budget=std::make_shared<lvm::MemoryBudget>();
        resources.budget->configure(lvm::resource_limit({},memory),resident,lvm::resource_headroom(memory));
        resources.memory_bytes=resources.budget->available();resources.cached_plans_accounted=true;
        resources.max_participants=lvm::processor_participants();
    }
#endif
    return resources;
}

void print_usage(const char* prog) {
    std::cout <<
        "LVM Reader CLI " << kVersion << "\n"
        "Usage: " << prog << " <file.lvm|file.txt|file.csv> [options]\n"
        "\n"
        "Actions:\n"
        "  -i, --info          Show file structure / header info\n"
        "  -s, --stats         Show per-channel statistics (min/max/mean/count)\n"
        "  -H, --head N        Print the first N data rows\n"
        "  -c, --csv FILE      Export the (selected) data to a CSV file\n"
        "      --fft           Show the strongest spectral peaks per channel\n"
        "      --peaks N       Number of peaks to show (default 5; implies --fft)\n"
        "      --fft-csv FILE  Export the magnitude spectrum to a CSV file\n"
        "\n"
        "Selection (applied to stats / head / csv / fft):\n"
        "      --start T       Keep rows with time >= T\n"
        "      --end T         Keep rows with time <= T\n"
        "      --channels LIST Comma-separated 1-based channel positions, e.g. 1,3\n"
        "\n"
        "Parsing / spectrum options:\n"
        "  -m, --monotonic     Rebuild a monotonic timeline (for sectioned files)\n"
        "      --keep-dup-time Keep channels that duplicate the time axis\n"
        "      --fft-samples N  Use up to N selected samples; timestamp gaps are ignored (0 = all)\n"
        "  -v, --verbose       Verbose parser output\n"
        "  -h, --help          Show this help\n"
        "\n"
        "With no action flag, --info and --stats are shown.\n";
}

// Format a double compactly; empty string for NaN (matches pandas CSV output).
std::string fmt(double v) {
    if (std::isnan(v)) return "";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

struct ChannelStat {
    long long count = 0;  // non-NaN samples
    double min = std::numeric_limits<double>::quiet_NaN();
    double max = std::numeric_limits<double>::quiet_NaN();
    double mean = std::numeric_limits<double>::quiet_NaN();
};

ChannelStat compute_stat(const std::vector<double>& col) {
    ChannelStat s;
    double sum = 0.0;
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (double v : col) {
        if (std::isnan(v)) continue;
        ++s.count;
        sum += v;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    if (s.count > 0) {
        s.min = lo;
        s.max = hi;
        s.mean = sum / static_cast<double>(s.count);
    }
    return s;
}

// Build a new Dataset limited to the time window [start, end] and the chosen
// channels (1-based positions; empty = keep all). Returns false on bad input.
bool apply_selection(const lvm::Dataset& in, double start, double end,
                     const std::vector<int>& channel_pos, lvm::Dataset& out,
                     std::string& error) {
    std::vector<std::size_t> chans;
    if (channel_pos.empty()) {
        for (std::size_t c = 0; c < in.channels.size(); ++c) chans.push_back(c);
    } else {
        for (int pos : channel_pos) {
            if (pos < 1 || pos > static_cast<int>(in.channels.size())) {
                error = "Channel position out of range: " + std::to_string(pos) +
                        " (have " + std::to_string(in.channels.size()) + ")";
                return false;
            }
            chans.push_back(static_cast<std::size_t>(pos - 1));
        }
    }

    out = lvm::Dataset{};
    out.stats = in.stats;
    out.frequency_axis = in.frequency_axis;
    out.export_comments = in.export_comments;
    out.ok = true;
    for (std::size_t c : chans) out.names.push_back(in.names[c]);
    out.channels.resize(chans.size());

    for (std::size_t r = 0; r < in.time.size(); ++r) {
        const double t = in.time[r];
        if (t < start || t > end) continue;
        out.time.push_back(t);
        for (std::size_t k = 0; k < chans.size(); ++k) {
            out.channels[k].push_back(in.channels[chans[k]][r]);
        }
    }
    return true;
}

void print_info(const lvm::Dataset& ds) {
    std::cout << "File structure\n";
    std::cout << "  Header end markers : " << ds.stats.header_markers << "\n";
    std::cout << "  Data sections      : " << ds.stats.data_sections << "\n";
    std::cout << "  Data rows (raw)    : " << ds.stats.data_rows << "\n";
    std::cout << "  Max columns        : " << ds.stats.max_columns << "\n";
    std::cout << "  Rows selected      : " << ds.rows() << "\n";
    std::cout << "  Channels           : " << ds.channel_count() << "\n";
    if (!ds.time.empty()) {
        std::cout << "  Time range         : " << fmt(ds.time.front()) << " .. "
                  << fmt(ds.time.back()) << "\n";
    }
}

void print_stats(const lvm::Dataset& ds) {
    std::cout << "\nChannel statistics\n";
    std::printf("  %-20s %12s %14s %14s %14s\n", "channel", "count", "min", "max", "mean");
    for (std::size_t c = 0; c < ds.channels.size(); ++c) {
        const ChannelStat s = compute_stat(ds.channels[c]);
        std::printf("  %-20s %12lld %14s %14s %14s\n",
                    ds.names[c].c_str(), s.count,
                    fmt(s.min).c_str(), fmt(s.max).c_str(), fmt(s.mean).c_str());
    }
}

void print_head(const lvm::Dataset& ds, long long n) {
    const long long rows = std::min<long long>(n, static_cast<long long>(ds.rows()));
    std::cout << "\nFirst " << rows << " rows\n";
    std::cout << (ds.frequency_axis ? "Frequency" : "Time");
    for (std::size_t c = 0; c < ds.channels.size(); ++c) std::cout << "\t" << ds.names[c];
    std::cout << "\n";
    for (long long r = 0; r < rows; ++r) {
        std::cout << fmt(ds.time[r]);
        for (std::size_t c = 0; c < ds.channels.size(); ++c) {
            std::cout << "\t" << fmt(ds.channels[c][r]);
        }
        std::cout << "\n";
    }
}

bool export_csv(const lvm::Dataset& ds, const std::string& path) {
    return lvm::atomic_write_file(std::filesystem::u8path(path), [&](std::ofstream& out) {
    out << (ds.frequency_axis ? "Frequency" : "Time");
    for (std::size_t c = 0; c < ds.channels.size(); ++c) out << "," << lvm::csv_field(ds.names[c]);
    out << "\n";
    for (std::size_t r = 0; r < ds.rows(); ++r) {
        out << fmt(ds.time[r]);
        for (std::size_t c = 0; c < ds.channels.size(); ++c) {
            out << "," << fmt(ds.channels[c][r]);
        }
        out << "\n";
    }
    return true;
    });
}

void print_peaks(const lvm::Spectrum& spec, int peak_count) {
    if (spec.imported) {
        std::cout << "\nStored spectrum (" << spec.freqs.size() << " bins, " << fmt(spec.freqs.front())
                  << ".." << fmt(spec.freqs.back()) << " Hz)\n";
    } else {
        std::cout << "\nFFT spectrum (N=" << spec.n << ", dt=" << fmt(spec.sample_dt)
                  << " s, Nyquist=" << fmt(spec.nyquist) << " Hz)\n";
    }
    if (spec.gaps_ignored) std::cout << "  Timestamp gaps ignored; " << spec.n << " samples used.\n";
    if (spec.resampled) std::cout << "  Uneven timestamps resampled by linear interpolation.\n";
    for (std::size_t c = 0; c < spec.names.size(); ++c) {
        const auto peaks = lvm::find_peaks(spec.freqs, spec.amp[c], peak_count);
        std::cout << "  " << spec.names[c] << ":";
        for (const auto& p : peaks) {
            std::cout << "  " << fmt(p.freq) << " Hz (amp " << fmt(p.amp) << ")";
        }
        std::cout << "\n";
    }
}

bool export_fft_csv(const lvm::Spectrum& spec, const std::string& path) {
    return lvm::atomic_write_file(std::filesystem::u8path(path), [&](std::ofstream& out) {
    out << "Frequency";
    for (const auto& name : spec.names) out << "," << lvm::csv_field(name);
    out << "\n";
    for (std::size_t k = 0; k < spec.freqs.size(); ++k) {
        out << fmt(spec.freqs[k]);
        for (std::size_t c = 0; c < spec.amp.size(); ++c) out << "," << fmt(spec.amp[c][k]);
        out << "\n";
    }
    return true;
    });
}

bool parse_int_arg(const std::string& text, int min_value, int& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') return false;
    if (value < min_value || value > std::numeric_limits<int>::max()) return false;
    out = static_cast<int>(value);
    return true;
}

bool parse_long_long_arg(const std::string& text, long long min_value, long long& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') return false;
    if (value < min_value) return false;
    out = value;
    return true;
}

bool parse_double_arg(const std::string& text, double& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(value)) return false;
    out = value;
    return true;
}

bool parse_channel_list(const std::string& text, std::vector<int>& out) {
    out.clear();
    std::stringstream ss(text);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;
        int value = 0;
        if (!parse_int_arg(token, 1, value)) return false;
        out.push_back(value);
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // The CRT's narrow argv uses the active ANSI code page and can already have
    // lost characters. Decode Windows' original UTF-16 command line instead.
    int wide_argc = 0;
    wchar_t** wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (!wide_argv) { std::cerr << "Cannot read command line.\n"; return 1; }
    std::vector<std::string> utf8_args;
    utf8_args.reserve(wide_argc);
    for (int i = 0; i < wide_argc; ++i) {
        const int length = static_cast<int>(wcslen(wide_argv[i]));
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], length, nullptr, 0, nullptr, nullptr);
        if (length && !size) { LocalFree(wide_argv); std::cerr << "Invalid Unicode argument.\n"; return 2; }
        std::string value(size, '\0');
        if (size) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], length, value.data(), size, nullptr, nullptr);
        utf8_args.push_back(std::move(value));
    }
    LocalFree(wide_argv);
    std::vector<char*> utf8_argv;
    for (auto& value : utf8_args) utf8_argv.push_back(value.data());
    utf8_argv.push_back(nullptr);
    argc = wide_argc; argv = utf8_argv.data();
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::string file;
    std::string parse_error;
    bool want_info = false, want_stats = false, want_monotonic = false;
    bool keep_dup_time = false, verbose = false, want_fft = false;
    long long head_n = -1;
    std::string csv_path, fft_csv_path, channels_text;
    int peak_count = kDefaultPeaks;
    int fft_samples = kDefaultFftSamples;
    double start = -std::numeric_limits<double>::infinity();
    double end = std::numeric_limits<double>::infinity();

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { print_usage(argv[0]); return 0; }
        else if (a == "-i" || a == "--info") want_info = true;
        else if (a == "-s" || a == "--stats") want_stats = true;
        else if (a == "-m" || a == "--monotonic") want_monotonic = true;
        else if (a == "--keep-dup-time") keep_dup_time = true;
        else if (a == "-v" || a == "--verbose") verbose = true;
        else if (a == "-H" || a == "--head") {
            if (!parse_long_long_arg(next("--head"), 0, head_n)) {
                parse_error = "Invalid value for --head (expected an integer >= 0).";
                break;
            }
        }
        else if (a == "-c" || a == "--csv") csv_path = next("--csv");
        else if (a == "--fft") want_fft = true;
        else if (a == "--peaks") {
            if (!parse_int_arg(next("--peaks"), 1, peak_count)) {
                parse_error = "Invalid value for --peaks (expected an integer >= 1).";
                break;
            }
            want_fft = true;
        }
        else if (a == "--fft-csv") fft_csv_path = next("--fft-csv");
        else if (a == "--fft-samples") {
            if (!parse_int_arg(next("--fft-samples"), 0, fft_samples) ||
                (fft_samples > 0 && fft_samples < 4)) {
                parse_error =
                    "Invalid value for --fft-samples (expected 0 or an integer >= 4).";
                break;
            }
        }
        else if (a == "--start") {
            if (!parse_double_arg(next("--start"), start)) {
                parse_error = "Invalid value for --start (expected a finite number).";
                break;
            }
        }
        else if (a == "--end") {
            if (!parse_double_arg(next("--end"), end)) {
                parse_error = "Invalid value for --end (expected a finite number).";
                break;
            }
        }
        else if (a == "--channels") channels_text = next("--channels");
        else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << "\n";
            return 2;
        } else if (file.empty()) {
            file = a;
        } else {
            std::cerr << "Unexpected extra argument: " << a << "\n";
            return 2;
        }
    }

    if (!parse_error.empty()) {
        std::cerr << "Error: " << parse_error << "\n";
        return 2;
    }

    if (file.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    lvm::Dataset ds = lvm::read_lvm_file(std::filesystem::u8path(file));
    if (!ds.ok) {
        std::cerr << "Error: " << ds.error << "\n";
        return 1;
    }

    const std::vector<double>& raw_time = ds.raw_time.empty() ? ds.time : ds.raw_time;
    if (!keep_dup_time) {
        const auto dropped = lvm::drop_duplicate_time_channels(ds, raw_time);
        if (!dropped.empty() && verbose) {
            std::cout << "Dropped time-axis duplicate channels:";
            for (const auto& d : dropped) std::cout << " " << d;
            std::cout << "\n";
        }
    }
    if (want_monotonic && !ds.frequency_axis) lvm::make_monotonic(ds.time);

    // Apply time-range / channel selection.
    std::vector<int> channel_pos;
    if (!channels_text.empty()) {
        if (!parse_channel_list(channels_text, channel_pos)) {
            std::cerr << "Error: invalid --channels list (expected comma-separated integers >= 1).\n";
            return 2;
        }
    }
    lvm::Dataset view;
    std::string sel_error;
    const bool whole=channel_pos.empty() && (ds.time.empty() || (start<=ds.time.front() && end>=ds.time.back()));
    if(whole) {view=std::move(ds);std::vector<double>().swap(view.raw_time);}
    else if (!apply_selection(ds, start, end, channel_pos, view, sel_error)) {
        std::cerr << "Error: " << sel_error << "\n";
        return 2;
    }
    ds={}; // Selection owns its values; never retain a second complete input.
    if (view.rows() == 0) {
        std::cerr << "Error: selection is empty (no rows in the given time range).\n";
        return 1;
    }

    const bool want_fft_any = want_fft || !fft_csv_path.empty();
    const bool any_action =
        want_info || want_stats || head_n >= 0 || !csv_path.empty() || want_fft_any;
    if (!any_action) { want_info = true; want_stats = true; }

    if (want_info) print_info(view);
    if (want_stats) print_stats(view);
    if (head_n >= 0) print_head(view, head_n);
    if (!csv_path.empty()) {
        if (!export_csv(view, csv_path)) return 1;
        std::cout << "\nCSV exported: " << csv_path << " (" << view.rows()
                  << " rows, " << view.channel_count() << " channels)\n";
    }

    if (want_fft_any) {
        const lvm::Spectrum spec = lvm::compute_spectrum(view, fft_samples,nullptr,automatic_fft_resources(view));
        if (!spec.ok) {
            std::cerr << "FFT error: " << spec.error << "\n";
            return 1;
        }
        if (want_fft) print_peaks(spec, peak_count);
        if (!fft_csv_path.empty()) {
            if (!export_fft_csv(spec, fft_csv_path)) return 1;
            std::cout << "\nSpectrum exported: " << fft_csv_path << " ("
                      << spec.freqs.size() << " bins, " << spec.names.size()
                      << " channels)\n";
        }
    }
    return 0;
}

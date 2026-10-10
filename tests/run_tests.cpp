// Unit tests for the LVM parser and spectrum analysis.
//
// A tiny assertion harness (no external framework). Each test writes a small
// temporary .lvm/.txt file, parses it, and checks the result. Exit code is
// non-zero if any check fails, so it works as a `make test` gate.
#include <cmath>
#include <random>
#include <cstdio>
#include <fstream>
#include <future>
#include <string>
#include <vector>

#include "analysis.hpp"
#include "analysis_executor.hpp"
#include "fft.hpp"
#include "frf_analysis.hpp"
#include "frf_worker.hpp"
#include "frf_stream.hpp"
#include "filter_engine.hpp"
#include "sampling.hpp"
#include "spectrum_worker.hpp"
#include <chrono>
#include "export_helpers.hpp"
#include "gap_details.hpp"
#include "formula_engine.hpp"
#include "lvm_parser.hpp"
#include "data_io.hpp"
#include "minmax_index.hpp"
#include <limits>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void check_near(double a, double b, double tol, const std::string& what) {
    ++g_checks;
    if (!(std::fabs(a - b) <= tol)) {
        ++g_failures;
        std::printf("  FAIL: %s (got %.9g, expected %.9g)\n", what.c_str(), a, b);
    }
}

struct TempFile {
    std::string path;

    TempFile(const std::string& name, const std::string& content) : path("tests/_tmp_" + name) {
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    ~TempFile() {
        std::remove(path.c_str());
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

void test_basic_parse() {
    std::printf("test_basic_parse\n");
    const std::string content =
        "LabVIEW Measurement\n"
        "Separator\tTab\n"
        "Multi_Headings\tNo\n"
        "***End_of_Header***\n"
        "Time\tChannel_1[V]\tChannel_2[V]\n"
        "0.0\t1.0\t10.0\n"
        "0.1\t2.0\t20.0\n"
        "0.2\t3.0\t30.0\n";
    const TempFile temp("basic.lvm", content);
    lvm::Dataset ds = lvm::read_lvm_file(temp.path);

    check(ds.ok, "parse ok");
    check(ds.stats.header_markers == 1, "one header marker");
    check(ds.rows() == 3, "three rows kept");
    check(ds.channel_count() == 2, "two channels");
    check(ds.names.size() == 2 && ds.names[0] == "Channel_1" && ds.names[1] == "Channel_2",
          "channel names Channel_1/Channel_2");
    check_near(ds.time[0], 0.0, 1e-12, "time[0]");
    check_near(ds.time[2], 0.2, 1e-12, "time[2]");
    check_near(ds.channels[1][2], 30.0, 1e-12, "channel_2 last value");
}

void test_metadata_and_nan() {
    std::printf("test_metadata_and_nan\n");
    const std::string content =
        "LabVIEW Measurement\n"
        "Date\t11/12/2025\n"
        "Time\t12:00:00.0000000\n"     // metadata timestamp -> skipped
        "***End_of_Header***\n"
        "Time\tCh1\tCh2\n"             // first cell "Time" is metadata -> skipped
        "0.0\t1.0\t2.0\n"
        "0.1\t\t4.0\n"                 // empty cell -> NaN, two numeric? only 0.1,4.0 -> kept
        "0.2\tbad\t6.0\n"              // non-numeric -> NaN
        "junk line with one 5\n";      // < 2 numeric values -> skipped
    const TempFile temp("nan.lvm", content);
    lvm::Dataset ds = lvm::read_lvm_file(temp.path);

    check(ds.ok, "parse ok");
    check(ds.rows() == 3, "three data rows (junk skipped)");
    check(ds.channel_count() == 2, "two channels");
    check(std::isnan(ds.channels[0][1]), "empty cell -> NaN");
    check(std::isnan(ds.channels[0][2]), "non-numeric cell -> NaN");
    check_near(ds.channels[1][1], 4.0, 1e-12, "ch2 row 1 value");
}

void test_decimal_comma() {
    std::printf("test_decimal_comma\n");
    const std::string content =
        "***End_of_Header***\n"
        "0,0\t1,5\n"
        "0,1\t2,5\n"
        "0,2\t3,5\n"
        "0,3\t4,5\n";
    const TempFile temp("comma.txt", content);
    lvm::Dataset ds = lvm::read_lvm_file(temp.path);

    check(ds.ok, "parse ok");
    check(ds.rows() == 4, "four rows");
    check_near(ds.time[1], 0.1, 1e-12, "comma decimal in time");
    check_near(ds.channels[0][0], 1.5, 1e-12, "comma decimal in channel");
}

void test_multi_header() {
    std::printf("test_multi_header\n");
    const std::string content =
        "***End_of_Header***\n"
        "0.0\t1.0\n"
        "0.1\t2.0\n"
        "***End_of_Header***\n"        // second section
        "0.0\t3.0\n"                   // local time resets
        "0.1\t4.0\n";
    const TempFile temp("multi.lvm", content);
    lvm::Dataset ds = lvm::read_lvm_file(temp.path);

    check(ds.stats.header_markers == 2, "two header markers");
    check(ds.stats.data_sections == 2, "two data sections");
    check(ds.rows() == 4, "four combined rows");

    // raw time is non-monotonic (0,0.1,0,0.1) -> make_monotonic fixes it.
    std::vector<double> t = ds.time;
    lvm::make_monotonic(t);
    bool increasing = true;
    for (std::size_t i = 1; i < t.size(); ++i) increasing = increasing && (t[i] > t[i - 1]);
    check(increasing, "make_monotonic yields strictly increasing time");
}

void test_make_monotonic_equal_times() {
    std::printf("test_make_monotonic_equal_times\n");
    std::vector<double> t = {0.0, 0.1, 0.1, 0.2};
    lvm::make_monotonic(t);

    bool increasing = true;
    for (std::size_t i = 1; i < t.size(); ++i) increasing = increasing && (t[i] > t[i - 1]);
    check(increasing, "equal timestamps are pushed forward to stay strictly increasing");
    check_near(t[2], 0.2, 1e-12, "duplicate timestamp advanced by the fallback step");
}

void test_make_monotonic_backward_jump() {
    std::printf("test_make_monotonic_backward_jump\n");
    std::vector<double> t = {0.0, 0.1, 0.05, 0.15};
    lvm::make_monotonic(t);

    bool increasing = true;
    for (std::size_t i = 1; i < t.size(); ++i) increasing = increasing && (t[i] > t[i - 1]);
    check(increasing, "backward jumps are pushed forward to stay strictly increasing");
}

void test_drop_duplicate_time() {
    std::printf("test_drop_duplicate_time\n");
    // Channel_1 duplicates time exactly; Channel_2 is real data.
    const std::string content =
        "***End_of_Header***\n"
        "0.0\t0.0\t5.0\n"
        "1.0\t1.0\t6.0\n"
        "2.0\t2.0\t7.0\n"
        "3.0\t3.0\t8.0\n";
    const TempFile temp("dup.lvm", content);
    lvm::Dataset ds = lvm::read_lvm_file(temp.path);
    check(ds.channel_count() == 2, "two channels before drop");

    const std::vector<double> raw_time = ds.time;
    const auto dropped = lvm::drop_duplicate_time_channels(ds, raw_time);
    check(dropped.size() == 1 && dropped[0] == "Channel_1", "Channel_1 dropped as time dup");
    check(ds.channel_count() == 1 && ds.names[0] == "Channel_1", "generated name follows remaining channel order");
}

void test_generated_names_after_interleaved_time_columns() {
    std::printf("test_generated_names_after_interleaved_time_columns\n");
    const TempFile unnamed("interleaved_unnamed.lvm",
        "***End_of_Header***\n"
        "X_Value\t\tX_Value\t\tX_Value\t\n"
        "0\t10\t0\t20\t0\t30\n"
        "1\t11\t1\t21\t1\t31\n");
    auto ds = lvm::read_lvm_file(unnamed.path);
    check(ds.ok && ds.channel_count() == 5, "interleaved unnamed columns parse");
    lvm::drop_duplicate_time_channels(ds, ds.raw_time);
    check(ds.names == std::vector<std::string>{"Channel_1", "Channel_2", "Channel_3"},
          "generated names are sequential after duplicate time columns are removed");

    const TempFile named("interleaved_explicit.lvm",
        "***End_of_Header***\n"
        "X_Value\tChannel_1\tX_Value\tChannel_3\n"
        "0\t10\t0\t20\n"
        "1\t11\t1\t21\n");
    ds = lvm::read_lvm_file(named.path);
    check(ds.ok, "explicit channel names parse");
    lvm::drop_duplicate_time_channels(ds, ds.raw_time);
    check(ds.names == std::vector<std::string>{"Channel_1", "Channel_3"},
          "explicit channel names are preserved");
}

void test_interleaved_channel_names() {
    std::printf("test_interleaved_channel_names\n");
    const std::string content =
        "LabVIEW Measurement\n"
        "Writer_Version\t0.92\n"
        "Reader_Version\t1\n"
        "Separator\tTab\n"
        "Multi_Headings\tYes\n"
        "X_Columns\tMulti\n"
        "***End_of_Header***\n"
        "Channels\t8\t\t\t\t\t\t\t\n"
        "Samples\t2\t2\t2\t2\t2\t2\t2\t2\n"
        "Date\t2009/05/15\t2009/05/15\t2009/05/15\t2009/05/15\t2009/05/15\t2009/05/15\t2009/05/15\t2009/05/15\n"
        "Time\t00:00:00,000\t00:00:00,000\t00:00:00,000\t00:00:00,000\t00:00:00,000\t00:00:00,000\t00:00:00,000\t00:00:00,000\n"
        "X0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "Delta_X\t0.1\t0.1\t0.1\t0.1\t0.1\t0.1\t0.1\t0.1\n"
        "***End_of_Header***\n"
        "X_Value\tg1\tX_Value\tg2\tX_Value\tg3\tX_Value\tg4\tX_Value\tg5\tX_Value\tg6\tX_Value\tg7\tX_Value\tg8\tComment\n"
        "0.0\t1.0\t0.0\t2.0\t0.0\t3.0\t0.0\t4.0\t0.0\t5.0\t0.0\t6.0\t0.0\t7.0\t0.0\t8.0\tok\n"
        "0.1\t1.1\t0.1\t2.1\t0.1\t3.1\t0.1\t4.1\t0.1\t5.1\t0.1\t6.1\t0.1\t7.1\t0.1\t8.1\tok\n";
    const TempFile temp("interleaved_names.lvm", content);
    lvm::Dataset ds = lvm::read_lvm_file(temp.path);
    check(ds.ok, "interleaved file parses");
    check(ds.channel_count() == 15, "interleaved file initially has 15 channels before de-dup");

    const std::vector<double> raw_time = ds.raw_time.empty() ? ds.time : ds.raw_time;
    const auto dropped = lvm::drop_duplicate_time_channels(ds, raw_time);
    check(dropped.size() == 7, "seven duplicate time columns dropped");
    check(ds.channel_count() == 8, "eight data channels remain");
    const std::vector<std::string> expected = {"g1", "g2", "g3", "g4", "g5", "g6", "g7", "g8"};
    check(ds.names == expected, "channel names preserved as g1..g8");
}

void test_reference_test_lvm() {
    std::printf("test_reference_test_lvm\n");
    lvm::Dataset ds = lvm::read_lvm_file("lvm_files_for_tests/test.lvm");
    check(ds.ok, "reference test.lvm parses");
    check(ds.rows() == 10000, "reference test.lvm row count");
    check(ds.channel_count() == 3, "reference test.lvm channel count");
    check(ds.names.size() == 3 && ds.names[0] == "Channel_1" && ds.names[1] == "Channel_2" &&
              ds.names[2] == "Channel_3",
          "reference test.lvm channel names");
    if (ds.ok && ds.rows() == 10000 && ds.channel_count() == 3) {
        check_near(ds.time.front(), 0.0, 1e-12, "reference time starts at zero");
        check_near(ds.time.back(), 9.999, 1e-12, "reference time ends at 9.999");
    }
}

void test_fft_peak() {
    std::printf("test_fft_peak\n");
    // 50 Hz sine sampled at 1000 Hz for 1 s -> peak near 50 Hz.
    lvm::Dataset ds;
    ds.names.push_back("Channel_1");
    ds.channels.resize(1);
    const int n = 1000;
    const double fs = 1000.0, freq = 50.0;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        ds.time.push_back(t);
        ds.channels[0].push_back(std::sin(2.0 * 3.14159265358979323846 * freq * t));
    }
    ds.ok = true;

    lvm::Spectrum spec = lvm::compute_spectrum(ds, 0);  // no cap
    check(spec.ok, "spectrum ok");
    check_near(spec.sample_dt, 1.0 / fs, 1e-9, "sample_dt");
    check_near(spec.nyquist, fs / 2.0, 1e-6, "nyquist");

    const auto peaks = lvm::find_peaks(spec.freqs, spec.amp[0], 1);
    check(!peaks.empty(), "found a peak");
    if (!peaks.empty()) {
        check_near(peaks[0].freq, freq, 1.0, "peak at ~50 Hz");
        check_near(peaks[0].amp, 1.0, 0.05, "peak amplitude ~1.0");
    }
}

void test_fft_nyquist_amplitude() {
    std::printf("test_fft_nyquist_amplitude\n");
    lvm::Dataset ds;
    ds.names.push_back("Channel_1");
    ds.channels.resize(1);
    const int n = 8;
    const double fs = 8.0;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        ds.time.push_back(t);
        ds.channels[0].push_back((i % 2 == 0) ? 1.0 : -1.0);
    }
    ds.ok = true;

    const lvm::Spectrum spec = lvm::compute_spectrum(ds, 0);
    check(spec.ok, "nyquist spectrum ok");
    check(spec.amp.size() == 1, "one channel in nyquist spectrum");
    if (spec.ok && spec.amp.size() == 1) {
        check_near(spec.freqs.back(), fs / 2.0, 1e-12, "nyquist frequency bin");
        check_near(spec.amp[0].back(), 1.0, 1e-12, "nyquist amplitude is not doubled");
    }
}

void test_fft_sample_cap_too_small() {
    std::printf("test_fft_sample_cap_too_small\n");
    lvm::Dataset ds;
    ds.names.push_back("Channel_1");
    ds.channels.resize(1);
    for (int i = 0; i < 16; ++i) {
        ds.time.push_back(static_cast<double>(i));
        ds.channels[0].push_back(static_cast<double>(i));
    }
    ds.ok = true;

    const lvm::Spectrum spec = lvm::compute_spectrum(ds, 3);
    check(!spec.ok, "tiny fft sample cap is rejected");
    check(!spec.error.empty(), "tiny fft sample cap reports an error");
}

void test_missing_file() {
    std::printf("test_missing_file\n");
    lvm::Dataset ds = lvm::read_lvm_file("tests/_does_not_exist.lvm");
    check(!ds.ok, "missing file -> not ok");
    check(!ds.error.empty(), "error message set");
}

void test_formula_engine() {
    std::printf("test_formula_engine\n");
    std::vector<FormulaToken> rpn;
    std::wstring error;
    check(compile_formula_rpn(L"2*x + 3", rpn, error, true), "formula compiles");
    check(!formula_rpn_is_identity(rpn), "non-identity formula detected");

    const AffineFormulaInfo affine = analyze_formula_rpn_affine(rpn);
    check(affine.valid, "affine formula detected");
    check_near(affine.mul, 2.0, 1e-12, "affine mul");
    check_near(affine.add, 3.0, 1e-12, "affine add");
    check_near(eval_formula_rpn(rpn, 4.0), 11.0, 1e-12, "formula evaluation");

    std::vector<FormulaToken> identity;
    error.clear();
    check(compile_formula_rpn(L"x", identity, error, true), "identity formula compiles");
    check(formula_rpn_is_identity(identity), "identity formula detected");
}

void test_export_helpers() {
    std::printf("test_export_helpers\n");
    const std::vector<double> values = {0.0, 0.1, 0.2, 0.3, 0.4};
    const auto bounds = export_range_bounds(values, 0.15, 0.35);
    check(bounds.first == 2 && bounds.second == 4, "export range bounds");
}

void test_gap_details() {
    std::printf("test_gap_details\n");
    check(gap_estimated_missing_samples(0.131, 0.03275) == 3, "gap estimate formula");
    check_near(gap_reference_step_from_estimate(0.131, 3), 0.03275, 1e-12, "gap reference step");

    const std::wstring en = gap_details_body_text(true, 0.131, 3, 0.03275);
    check(en.find(L"Gap duration: 0.131 s") != std::wstring::npos, "gap body EN duration");
    check(en.find(L"Approx. missing samples: ~3") != std::wstring::npos, "gap body EN samples");
    check(en.find(L"Reference step: 0.03275 s") != std::wstring::npos, "gap body EN step");

    const std::wstring ru = gap_details_body_text(false, 0.131, 3, 0.03275);
    check(ru.find(L"Длительность разрыва: 0.131 c") != std::wstring::npos, "gap body RU duration");
    check(ru.find(L"Пропущено примерно: ~3 отсч.") != std::wstring::npos, "gap body RU samples");
    check(ru.find(L"Типичный шаг: 0.03275 c") != std::wstring::npos, "gap body RU step");
}

void test_scan_and_window_load() {
    std::printf("test_scan_and_window_load\n");
    const std::string content =
        "***End_of_Header***\n"
        "0.0\t1.0\t10.0\n"
        "0.1\t2.0\t20.0\n"
        "0.2\t3.0\t30.0\n"
        "0.3\t4.0\t40.0\n"
        "0.4\t5.0\t50.0\n";
    const TempFile temp("window.lvm", content);

    double start = 0.0;
    double end = 0.0;
    std::string error;
    check(lvm::scan_time_bounds(temp.path, start, end, error), "scan_time_bounds ok");
    check(error.empty(), "scan_time_bounds error empty");
    check_near(start, 0.0, 1e-12, "scan start");
    check_near(end, 0.4, 1e-12, "scan end");

    lvm::LoadOptions options;
    options.use_time_window = true;
    options.time_start = 0.15;
    options.time_end = 0.35;
    lvm::Dataset ds = lvm::read_lvm_file(temp.path, options);
    check(ds.ok, "windowed load ok");
    check(ds.partial, "windowed load marked partial");
    check(ds.rows() == 2, "windowed load rows");
    check_near(ds.time[0], 0.2, 1e-12, "windowed load first time");
    check_near(ds.time[1], 0.3, 1e-12, "windowed load second time");
    check(ds.channel_count() == 2, "windowed load channel count");
}

void test_data_integrity_regressions() {
    std::printf("test_data_integrity_regressions\n");
    const TempFile blank("missing_time.txt", "0\t10\t20\n\t11\t21\n2\t12\t22\n");
    auto ds = lvm::read_lvm_file(blank.path);
    check(ds.ok && ds.rows() == 2, "missing timestamp is not replaced by channel value");
    if(ds.rows() == 2) check_near(ds.channels[0][1],12,1e-12,"columns remain aligned");
    const TempFile csv("quoted.csv", "\xEF\xBB\xBFTime,\"A, volts\",B\n\"0\",\"10\",\"20\"\n\"1\",\"11\",\"21\"\n");
    ds = lvm::read_lvm_file(csv.path);
    check(ds.ok && ds.rows() == 2 && ds.names == std::vector<std::string>{"A, volts","B"}, "BOM and quoted CSV");
    const TempFile multiline("multiline.csv", "Time,\"line one\nline two\"\n0,10\n1,20\n");
    ds=lvm::read_lvm_file(multiline.path);
    check(ds.ok && ds.rows()==2 && ds.names[0]=="line one\nline two","multiline CSV label");
    const TempFile times("same_times.txt", "0\t10\n1\t11\n11\t12\n0\t13\n1\t14\n");
    ds = lvm::read_lvm_file(times.path); lvm::make_monotonic(ds.time);
    lvm::LoadOptions options; options.use_time_window=true; options.time_start=0; options.time_end=100;
    auto partial=lvm::read_lvm_file(times.path,options);
    check(ds.time == partial.time,"full and window timeline normalization agree");
    double start=0,end=0;std::string error;
    auto index=std::make_shared<lvm::ScanIndex>();
    check(lvm::scan_time_bounds(times.path,start,end,error,nullptr,index.get()),"indexed scan");
    check_near(end,ds.time.back(),1e-12,"scan timeline matches full load");
    options.scan_index=index;options.time_start=1;
    partial=lvm::read_lvm_file(times.path,options);
    check(partial.time==std::vector<double>(ds.time.begin()+1,ds.time.end()),"indexed window agrees");
    auto original=ds.channels;
    lvm::drop_duplicate_time_channels(ds,ds.raw_time);
    check(ds.channels==original,"nonduplicate data survives channel compaction");
    for(const auto* formula:{L"2x",L"x 2",L"x+()",L"1e999",L"sin x",L"x(2)"}) {
        std::vector<FormulaToken> rpn;std::wstring message;
        check(!compile_formula_rpn(formula,rpn,message,true) && rpn.empty() && !message.empty(),"invalid formula rejected atomically");
    }
    const TempFile destination("atomic.txt","previous data");
    check(!lvm::atomic_write_file(destination.path,[](std::ofstream& out){out << "new";return false;}),"failed writer returns failure");
    std::ifstream in(destination.path);std::string text;std::getline(in,text);
    check(text=="previous data","failed writer preserves original");
}

void test_spectrum_integrity_regressions() {
    std::printf("test_spectrum_integrity_regressions\n");
    lvm::Dataset ds;ds.names={"signal"};ds.channels.resize(1);
    for(int i=0;i<32768;++i){double t=double(i)/1024;ds.time.push_back(t);ds.channels[0].push_back(std::sin(2*3.14159265358979323846*400*t));}
    auto spectrum=lvm::compute_spectrum(ds,16384);
    check(spectrum.ok,"capped spectrum works");
    if(spectrum.ok){const auto peaks=lvm::find_peaks(spectrum.freqs,spectrum.amp[0],1);check(!peaks.empty(),"capped peak found");if(!peaks.empty())check_near(peaks[0].freq,400,1e-9,"cap cannot alias 400Hz to 112Hz");}
    ds.channels[0][8]=std::numeric_limits<double>::infinity();
    check(!lvm::compute_spectrum(ds,0).ok,"infinite input cannot produce successful FFT");
    ds.channels[0][8]=0;ds.time[10]+=1;
    check(!lvm::compute_spectrum(ds,0).ok,"invalid time cannot produce successful FFT");
    ds.time[10]-=1;ds.channels[0].pop_back();
    check(!lvm::compute_spectrum(ds,0).ok,"misaligned dataset rejected");
}

void test_fft_irregular_timestamps() {
    std::printf("test_fft_irregular_timestamps\n");
    constexpr double pi = 3.14159265358979323846;
    const auto append_tone = [pi](lvm::Dataset& ds, int count, double start, double dt, double frequency, bool jitter) {
        for (int i = 0; i < count; ++i) {
            const double offset = jitter && i > 0 && i < count - 1 ? 0.12 * dt * std::sin(2 * pi * i / 17) : 0;
            const double t = start + i * dt + offset;
            ds.time.push_back(t);
            ds.channels[0].push_back(std::sin(2 * pi * frequency * (t - start)));
        }
    };
    const auto check_tone = [](const lvm::Spectrum& spec, double frequency, double frequency_tolerance) {
        check(spec.ok, "irregular recording produces FFT");
        if (!spec.ok) return;
        const auto peaks = lvm::find_peaks(spec.freqs, spec.amp[0], 1);
        check(!peaks.empty(), "irregular recording has a peak");
        if (peaks.empty()) return;
        check_near(peaks[0].freq, frequency, frequency_tolerance, "irregular timestamps preserve tone frequency");
        check_near(peaks[0].amp, 1, 0.06, "interpolation preserves low-frequency tone amplitude");
    };

    lvm::Dataset jittered;
    jittered.names = {"signal"}; jittered.channels.resize(1);
    append_tone(jittered, 1024, 0, 1.0 / 1024, 64, true);
    auto spec = lvm::compute_spectrum(jittered, 0);
    check_tone(spec, 64, 1e-9);
    check(spec.resampled && !spec.gaps_ignored && spec.n == 1024, "jitter resamples the whole recording");

    lvm::Dataset rounded;
    rounded.names = {"signal"}; rounded.channels.resize(1);
    const double frequency = 32.0 / (512 * 0.03275);
    append_tone(rounded, 512, 0, 0.03275, frequency, false);
    for (auto& t : rounded.time) t = std::round(t * 1000) / 1000;
    spec = lvm::compute_spectrum(rounded, 0);
    check_tone(spec, frequency, 0.001);
    check(spec.resampled && !spec.gaps_ignored, "millisecond timestamp rounding does not reject FFT");

    lvm::Dataset gapped;
    gapped.names = {"signal"}; gapped.channels.resize(1);
    append_tone(gapped, 128, 0, 1.0 / 1024, 32, false);
    append_tone(gapped, 1024, 10, 1.0 / 1024, 128, false);
    spec = lvm::compute_spectrum(gapped, 0);
    check(spec.ok && spec.gaps_ignored && !spec.resampled && spec.n == 1152, "gap does not discard samples from either side");
    if (spec.ok) {
        const auto peaks = lvm::find_peaks(spec.freqs, spec.amp[0], 2);
        check(peaks.size() == 2, "gap-compressed spectrum retains both signal fragments");
        if (peaks.size() == 2) {
            check_near(peaks[0].freq, 128, 1e-9, "longer fragment is present after a gap");
            check_near(peaks[0].amp, 1024.0 / 1152.0, 0.02, "fragment amplitude reflects all selected samples");
            check_near(peaks[1].freq, 32, 1e-9, "shorter fragment is present after a gap");
            check_near(peaks[1].amp, 128.0 / 1152.0, 0.02, "short fragment is not discarded after a gap");
        }
    }
    check_near(spec.source_start, 0, 1e-12, "full selected range start reported");
    check_near(spec.source_end, gapped.time.back(), 1e-12, "actual FFT source end reported");
    spec = lvm::compute_spectrum(gapped, 256);
    check(spec.ok && spec.n == 256 && spec.gaps_ignored, "sample cap applies to selected samples without discarding the gap rule");
    if (spec.ok) {
        const auto peaks = lvm::find_peaks(spec.freqs, spec.amp[0], 2);
        check(peaks.size() == 2, "capped FFT retains data on both sides of a gap");
        if (peaks.size() == 2) {
            check_near(peaks[0].amp, 0.5, 0.02, "capped FFT includes the first signal fragment");
            check_near(peaks[1].amp, 0.5, 0.02, "capped FFT includes the second signal fragment");
            check(std::abs(peaks[0].freq - 32) < 1e-9 || std::abs(peaks[0].freq - 128) < 1e-9,
                  "first capped peak has a source frequency");
            check(std::abs(peaks[1].freq - 32) < 1e-9 || std::abs(peaks[1].freq - 128) < 1e-9,
                  "second capped peak has a source frequency");
        }
    }
    check_near(spec.source_end, 10 + 127.0 / 1024, 1e-12, "capped FFT reports the selected source window");

    // Large gaps must not allocate a uniform grid spanning the entire gap.
    gapped.time.push_back(1e100); gapped.channels[0].push_back(0);
    spec = lvm::compute_spectrum(gapped, 0);
    check(spec.ok && spec.n == 1153 && spec.gaps_ignored, "huge gap keeps all values and FFT memory bounded");

    lvm::Dataset offset;
    offset.names = {"signal"}; offset.channels.resize(1);
    append_tone(offset, 1024, 1e9, 1.0 / 1024, 64, false);
    spec = lvm::compute_spectrum(offset, 0);
    check_tone(spec, 64, 1e-9);
    check(!spec.resampled && !spec.gaps_ignored, "uniform absolute timestamps do not alter samples");

    // A straight line has an exact interpolation oracle. Simply removing the
    // uniformity check and running FFT on uneven samples fails this comparison.
    lvm::Dataset uneven, uniform;
    uneven.names = uniform.names = {"ramp"}; uneven.channels.resize(1); uniform.channels.resize(1);
    for (int i = 0; i < 16; ++i) {
        const double t = i + (i > 0 && i < 15 ? (i % 2 ? 0.2 : -0.2) : 0);
        uneven.time.push_back(t); uneven.channels[0].push_back(2 * t + 3);
        uniform.time.push_back(i); uniform.channels[0].push_back(2 * i + 3);
    }
    const auto interpolated = lvm::compute_spectrum(uneven, 0);
    const auto reference = lvm::compute_spectrum(uniform, 0);
    check(interpolated.ok && interpolated.resampled && reference.ok, "linear interpolation oracle setup");
    if (interpolated.ok && reference.ok) {
        for (std::size_t k = 0; k < reference.freqs.size(); ++k) {
            check_near(interpolated.amp[0][k], reference.amp[0][k], 1e-10, "resampling matches the exact uniform signal");
        }
    }
    std::atomic<bool> cancelled{true};
    bool cancelled_cleanly = false;
    try { lvm::compute_spectrum(jittered, 0, &cancelled); }
    catch (const std::runtime_error&) { cancelled_cleanly = true; }
    check(cancelled_cleanly, "resampling respects FFT cancellation");
}

void test_fft_gap_regressions() {
    std::printf("test_fft_gap_regressions\n");
    // Compare every bin against the same available values on a compact axis.
    // Non-periodic values make losing or interpolating a fragment observable.
    for (int gap_count : {10, 10000}) {
        for (int pattern = 0; pattern < 4; ++pattern) {
            lvm::Dataset compact, recorded;
            compact.names = recorded.names = {"A", "B"};
            compact.channels.resize(2); recorded.channels.resize(2);
            const int rows = pattern == 0 ? gap_count * 3 + 1 : gap_count + 1001;
            double time = 0;
            int gaps = 0;
            for (int i = 0; i < rows; ++i) {
                const bool gap = i && (pattern == 0 ? i % 3 == 0 : i > 1000);
                if (i) time += (1.0 + (gap ? (pattern == 0 || pattern == 3 ? 1 + gaps % 3 : 1024.0 * (1 + gaps % 7)) : 0.0)) / 1024;
                if (gap) ++gaps;
                compact.time.push_back(i / 1024.0);
                recorded.time.push_back(time);
                for (int c = 0; c < 2; ++c) {
                    const double value = std::sin(i * (0.19 + c * 0.27)) + (i % 31 == 0 ? 3.0 : 0.0);
                    compact.channels[c].push_back(value);
                    recorded.channels[c].push_back(value);
                }
            }
            // Explicit cap is the only permitted source of truncation.
            const int cap = pattern == 2 ? rows - 3 : 0;
            const auto expected = lvm::compute_spectrum(compact, cap);
            const auto actual = lvm::compute_spectrum(recorded, cap);
            check(gaps == gap_count && actual.ok && expected.ok, "gap oracle setup");
            check(actual.gaps_ignored && !actual.resampled, "short and dominant large gaps are compressed without interpolation");
            check(actual.n == expected.n && actual.source_end == recorded.time[expected.n - 1], "all selected values and correct physical bounds retained");
            check(actual.freqs == expected.freqs, "gap-compressed frequency bins match compact reference");
            check(actual.amp == expected.amp, "every amplitude in both channels matches compact reference");
        }
    }

    lvm::Dataset jittered;
    jittered.names = {"A"}; jittered.channels.resize(1);
    for (int i = 0; i < 256; ++i) {
        const double t = (i + 0.1 * std::sin(i * 0.3)) / 1024.0;
        jittered.time.push_back(t); jittered.channels[0].push_back(std::sin(t * 200));
    }
    jittered.time.push_back(1000); jittered.channels[0].push_back(0.4);
    const auto moderate = lvm::compute_spectrum(jittered, 0);
    jittered.time.back() = 1e100;
    const auto huge = lvm::compute_spectrum(jittered, 0);
    check(moderate.ok && huge.ok && moderate.resampled && huge.resampled, "huge gap does not suppress jitter correction");
    check(huge.freqs == moderate.freqs && huge.amp == moderate.amp, "gap duration cannot change the compacted spectrum");

    // Rows beyond an explicit cap must not change sampling inference.
    lvm::Dataset capped;
    capped.names = {"A"}; capped.channels.resize(1);
    for (int i = 0; i < 16; ++i) { capped.time.push_back(i / 1024.0); capped.channels[0].push_back(std::sin(i)); }
    const auto before = lvm::compute_spectrum(capped, 16);
    for (int i = 0; i < 256; ++i) { capped.time.push_back(capped.time.back() + 1e-6); capped.channels[0].push_back(i); }
    const auto after = lvm::compute_spectrum(capped, 16);
    check(after.ok && after.freqs == before.freqs && after.amp == before.amp, "unselected tail cannot affect capped FFT");
}

void test_minmax_and_spectrum_import() {
    std::printf("test_minmax_and_spectrum_import\n");
    std::vector<double> values(1027);
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = std::sin(i * 0.7);
    values[64] = 1e35; values[1026] = -1e35; values[130] = std::nan("");
    const auto sample = [&](std::size_t i) { return values[i]; };
    MinMaxIndex index; index.build(values.size(), sample);
    bool exact = true;
    for (std::size_t lo = 0; lo < values.size(); lo += 13) {
        for (std::size_t hi = lo; hi <= values.size(); hi += 17) {
            double low = std::numeric_limits<double>::infinity(), high = -low;
            for (std::size_t i = lo; i < hi; ++i) if (std::isfinite(values[i])) {
                low = std::min(low, values[i]); high = std::max(high, values[i]);
            }
            const auto range = index.query(lo, hi, sample);
            exact = exact && range.first == low && range.second == high;
        }
    }
    check(exact, "minmax pyramid matches exhaustive finite sample ranges");
    check(index.query(1024, 2000, sample).first == -1e35, "minmax includes last partial block without float overflow");
    const TempFile spectrum("stored.csv", "Frequency,A,B\n0,0,3\n1,1,8\n2,2,5\n");
    auto ds = lvm::read_lvm_file(spectrum.path);
    check(ds.ok && ds.frequency_axis, "stored frequency header recognized");
    lvm::drop_duplicate_time_channels(ds, ds.raw_time);
    check(ds.channels.size() == 2, "amplitude equal to frequency is not discarded as duplicate time");
    const auto spec = lvm::compute_spectrum(ds, 0);
    check(spec.ok && spec.imported && spec.freqs == ds.time && spec.amp == ds.channels, "three-bin spectrum loads without FFT or minimum-four-sample restriction");
    lvm::LoadOptions options; options.use_time_window = true; options.time_start = 1; options.time_end = 2;
    const auto window = lvm::read_lvm_file(spectrum.path, options);
    check(window.ok && window.frequency_axis && window.time == std::vector<double>{1,2}, "spectrum window preserves frequencies");
    const TempFile invalid("invalid_spectrum.csv", "Frequency,A\n0,3\n2,4\n1,5\n");
    check(!lvm::read_lvm_file(invalid.path).ok, "invalid spectrum frequency ordering rejected");
    const TempFile text_spectrum("stored_spectrum.txt", "Frequency\tSensor\n0\t3\n1\t4\n2\t5\n");
    const auto text_ds = lvm::read_lvm_file(text_spectrum.path);
    check(text_ds.ok && text_ds.frequency_axis && text_ds.names == std::vector<std::string>{"Sensor"}, "plain TXT spectrum header recognized without metadata");
    check(lvm::tsv_header_field("A\tB\nC\rD") == "A B C D", "TXT header cannot introduce extra columns or rows");
}

void test_frf_streaming() {
    const auto equivalent=[](const lvm::FrfResult& a,const lvm::FrfResult& b) {
        if(a.ok!=b.ok || a.error!=b.error || a.valid!=b.valid || a.coherence_valid!=b.coherence_valid ||
           a.reference_amplitude_valid!=b.reference_amplitude_valid || a.frequencies!=b.frequencies ||
           a.sample_dt!=b.sample_dt || a.source_start!=b.source_start || a.source_end!=b.source_end ||
           a.sample_count!=b.sample_count || a.segment_length!=b.segment_length || a.averages!=b.averages ||
           a.overlap_samples!=b.overlap_samples || a.gaps_ignored!=b.gaps_ignored)return false;
        const auto close=[](double x,double y) {
            return (std::isnan(x) && std::isnan(y)) || x==y || std::fabs(x-y)<=2e-14*std::max({1.0,std::fabs(x),std::fabs(y)});
        };
        for(std::size_t k=0;k<a.transfer.size();++k)
            if(!close(a.transfer[k].real(),b.transfer[k].real()) || !close(a.transfer[k].imag(),b.transfer[k].imag()) ||
               !close(a.coherence[k],b.coherence[k]) || !close(a.reference_amplitude[k],b.reference_amplitude[k]))return false;
        return true;
    };
    for(std::size_t n:{4096u,337132u})for(std::size_t outputs:{1u,7u}) {
        lvm::FrfBatchInput input;input.references.resize(2);input.responses.resize(outputs);
        for(std::size_t i=0;i<n;++i) {
            const double x=std::sin(2*std::acos(-1.)*13*i/256)+.2*std::cos(.07*i);
            input.time.push_back(i/1024.+(i>n/3 ? 20 : 0));
            input.references[0].push_back(x);input.references[1].push_back(3*x);
            for(std::size_t c=0;c<outputs;++c)input.responses[c].push_back((c+1)*x+.1*std::sin(.13*i));
        }
        auto prepared=lvm::prepare_frf_batch(input);
        for(bool mean:{false,true})for(std::size_t length:{2048u,1000u,0u}) {
            lvm::FrfOptions options;options.segment_length=length;options.remove_mean=mean;
            const auto expected=lvm::compute_prepared_frf(*prepared,options);
            for(std::size_t block:{317u,4096u}) {
                lvm::FrfSource source;source.references=2;source.responses=outputs;
                source.replay=[&](const auto& visitor,const auto*) {
                    for(std::size_t at=0;at<n;at+=block) {
                        const auto end=std::min(n,at+block);
                        std::vector<double> time(input.time.begin()+at,input.time.begin()+end);
                        std::vector<std::vector<double>> values;
                        for(const auto& c:input.references)values.emplace_back(c.begin()+at,c.begin()+end);
                        for(const auto& c:input.responses)values.emplace_back(c.begin()+at,c.begin()+end);
                        visitor(time,values);
                    }
                };
                const auto stream=lvm::prepare_frf_stream(source);
                const auto actual=lvm::compute_prepared_frf(*stream,options);
                bool same=expected.ok==actual.ok && expected.error==actual.error && expected.responses.size()==actual.responses.size();
                for(std::size_t c=0;c<actual.responses.size();++c)same &= equivalent(expected.responses[c],actual.responses[c]);
                check(same,"FRF is independent of parser block boundaries, L, means and response count");
            }
            lvm::FrfSamples scalar;scalar.sample_dt=prepared->metadata.sample_dt;scalar.source_start=input.time.front();
            scalar.source_end=input.time.back();scalar.source_count=n;scalar.gaps_ignored=true;
            scalar.reference=prepared->memory->references.front();scalar.response=input.responses.front();
            check(equivalent(lvm::compute_frf(scalar,options),expected.responses.front()),"ordered frames agree with the scalar estimator");
        }
        if(n==4096)for(std::size_t length:{4096u,4097u}) {
            lvm::FrfOptions direct;direct.estimator=lvm::FrfEstimator::Direct;direct.segment_length=0;
            auto odd=input;
            if(length==4097) {
                odd.time.push_back(odd.time.back()+1./1024);
                for(auto& c:odd.references)c.push_back(c.back());
                for(auto& c:odd.responses)c.push_back(c.back());
            }
            auto p=lvm::prepare_frf_batch(std::move(odd));
            lvm::FrfSamples scalar=p->metadata;scalar.reference=p->memory->references.front();scalar.response=p->memory->responses.front();
            check(equivalent(lvm::compute_frf(scalar,direct),lvm::compute_prepared_frf(*p,direct).responses.front()),
                  "Direct retains radix-2 and odd Bluestein lengths");
        }
    }
    // More than one sorting run exercises the exact external median/cluster rule.
    std::vector<double> times(1050000);std::vector<double> steps;
    for(std::size_t i=1;i<times.size();++i) { const double d=i%5 ? 4./1024 : 1./1024;times[i]=times[i-1]+d;steps.push_back(d); }
    lvm::FrfSource timeline;
    timeline.replay=[&](const auto& visit,const auto*) {
        for(std::size_t at=0;at<times.size();at+=65536)
            visit(std::vector<double>(times.begin()+at,times.begin()+std::min(times.size(),at+65536)),{});
    };
    check(lvm::inspect_frf_timeline(timeline).sample_dt==lvm::typical_sample_spacing(steps),"external cadence sort preserves the majority-gap cluster rule");
    std::vector<double> signal(times.size());
    for(std::size_t i=0;i<signal.size();++i)signal[i]=std::sin(.017*i);
    times[1000]+=1; // Acquisition discontinuity; block boundaries are unrelated.
    for(int mode=0;mode<4;++mode)for(int topology=0;topology<4;++topology) {
        FilterSettings settings{mode,topology,10,100,1./1024};
        const auto expected=filter_signal(times,signal,settings);
        FilterStream filter(settings);std::vector<double> actual;
        for(std::size_t at=0;at<signal.size();at+=317) {
            const auto end=std::min(signal.size(),at+317);
            const auto block=filter.process(std::vector<double>(times.begin()+at,times.begin()+end),
                                             std::vector<double>(signal.begin()+at,signal.begin()+end));
            actual.insert(actual.end(),block.begin(),block.end());
        }
        check(actual==expected,"causal filter states and resets match for every family/mode across blocks");
    }
    std::atomic<bool> cancelled{true};bool threw=false;
    try {lvm::compute_prepared_frf(lvm::PreparedFrf{}, {}, &cancelled);}catch(...){threw=true;}
    check(threw,"prepared FRF honours cancellation before work");
    lvm::PreparedFrf invalid_length;invalid_length.metadata.source_count=4096;
    invalid_length.metadata.sample_dt=1./1024;invalid_length.response_errors={lvm::FrfError::None};
    lvm::FrfOptions invalid_options;invalid_options.segment_length=std::numeric_limits<std::size_t>::max();
    check(lvm::compute_prepared_frf(invalid_length,invalid_options).error==lvm::FrfError::InvalidOptions,
          "invalid extreme L is rejected before estimating Bluestein workspace");
    lvm::PreparedFrf large_length;large_length.metadata.source_count=100000000;
    large_length.metadata.sample_dt=1./1024;large_length.response_errors.assign(7,lvm::FrfError::None);
    for(auto estimator:{lvm::FrfEstimator::H1,lvm::FrfEstimator::Direct}) {
        lvm::FrfOptions options;options.estimator=estimator;
        const auto result=lvm::compute_prepared_frf(large_length,options);
        check(result.error==lvm::FrfError::ResourceLimit && result.options.segment_length==0 && result.options.estimator==estimator,
              "oversized Auto/Direct reports the budget without allocating or silently changing L");
    }
    lvm::FrfBatchInput worker_input;worker_input.references.resize(1);worker_input.responses.resize(1);
    for(std::size_t i=0;i<8192;++i) {
        worker_input.time.push_back(i/1024.);const double x=std::sin(.13*i);
        worker_input.references[0].push_back(x);worker_input.responses[0].push_back(2*x);
    }
    lvm::FrfWorker worker;lvm::FrfOptions options;options.segment_length=2048;
    const auto scratch_directories=[] {
        std::vector<std::filesystem::path> paths;
        for(const auto& entry:std::filesystem::directory_iterator(std::filesystem::temp_directory_path()))
            if(entry.path().filename().string().rfind("ams-frf-",0)==0)paths.push_back(entry.path());
        return paths;
    };
    const auto prior_scratch=scratch_directories();
    lvm::FrfSource disk_source;disk_source.references=disk_source.responses=1;
    disk_source.replay=[&](const auto& visitor,const auto*) {
        visitor(worker_input.time,{worker_input.references.front(),worker_input.responses.front()});
    };
    auto disk_input=lvm::prepare_frf_stream(disk_source);
    std::filesystem::path owned_scratch;
    for(const auto& path:scratch_directories())
        if(std::find(prior_scratch.begin(),prior_scratch.end(),path)==prior_scratch.end() &&
           std::filesystem::exists(path/"samples.bin"))owned_scratch=path;
    check(!owned_scratch.empty(),"disk input owns an identifiable temporary sample snapshot");
    std::vector<std::vector<double>> read_values;
    bool range_rejected=false,read_cancelled=false,damaged_rejected=false;
    try {disk_input->read(8191,2,read_values);}catch(const std::out_of_range&){range_rejected=true;}
    try {disk_input->read(0,2048,read_values,&cancelled);}catch(const std::runtime_error&){read_cancelled=true;}
    check(range_rejected && read_cancelled,"snapshot reads reject out-of-range access and cancellation before allocation");
    if(!owned_scratch.empty()) {
        {std::ofstream damaged(owned_scratch/"samples.bin",std::ios::binary|std::ios::trunc);damaged.put('x');}
        try {lvm::compute_prepared_frf(*disk_input,options);}
        catch(const lvm::FrfSourceFailure& failure){damaged_rejected=failure.error==lvm::FrfError::ResourceLimit;}
    }
    check(damaged_rejected,"truncated sample snapshot cannot publish a numerical result and reports a resource error");
    disk_input.reset();
    check(!owned_scratch.empty() && !std::filesystem::exists(owned_scratch),"damaged snapshot directory is released by RAII");
    worker.submit(worker_input,options,11,"revision-1");
    const auto await_result=[&] {
        std::optional<lvm::FrfWorker::Result> result;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!result && std::chrono::steady_clock::now()<deadline) {
            result=worker.take_result();if(!result)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return result;
    };
    auto result=await_result();check(result && result->frf.ok && result->generation==11,"worker publishes the first immutable input");
    lvm::FrfSource failed_source;failed_source.references=failed_source.responses=1;
    failed_source.replay=[](const auto&,const auto*){throw lvm::FrfSourceFailure(lvm::FrfError::ResourceLimit);};
    worker.submit(failed_source,options,17,"disk-error");result=await_result();
    check(result && !result->frf.ok && result->frf.error==lvm::FrfError::ResourceLimit && result->generation==17,
          "worker preserves a typed resource failure instead of reporting numeric overflow");
    worker.submit(worker_input,options,11,"revision-1");result=await_result();
    check(result && result->frf.ok,"worker recovers after a resource failure");
    options.remove_mean=false;
    check(worker.submit_cached(options,12,"revision-1"),"L/mean changes can reuse a prepared input independently of job generation");
    result=await_result();check(result && result->frf.ok && result->generation==12,"cached input survives the prior request lifetime");
    check(!worker.submit_cached(options,13,"revision-2"),"different numerical revision cannot use the input cache");
    std::atomic<bool> started{false};lvm::FrfSource slow;slow.references=slow.responses=1;
    slow.replay=[&](const auto&,const auto* flag) {
        started=true;
        while(!flag->load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        throw std::runtime_error("cancelled");
    };
    worker.submit(slow,options,14,"slow-source");
    const auto wait_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!started && std::chrono::steady_clock::now()<wait_deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(started,"streamed source executes in the worker");
    worker.cancel();worker.submit(worker_input,options,15,"other-document");
    result=await_result();check(result && result->frf.ok && result->generation==15,"replacement request never receives a cancelled document's result");
    // Hold the actual shared executor deterministically: FRF must fall back
    // without waiting, and an exception must release admission for the next job.
    std::promise<void> entered,release;auto allowed=release.get_future().share();
    std::atomic<bool> announced{false};
    auto held=std::async(std::launch::async,[&] {
        lvm::analysis_executor().run(4,4,[&](std::size_t i) {
            if(!announced.exchange(true))entered.set_value();
            allowed.wait();if(!i)throw std::runtime_error("shared executor test");
        });
    });
    entered.get_future().wait();
    auto owned=lvm::prepare_frf_batch(worker_input);
    lvm::FrfBatchResult sequential;
    try {sequential=lvm::compute_prepared_frf(*owned,options);}
    catch(...) {release.set_value();try{held.get();}catch(...){}throw;}
    release.set_value();bool propagated=false;
    try {held.get();}catch(const std::runtime_error&){propagated=true;}
    check(result && equivalent(sequential.responses.front(),result->frf.responses.front()),"busy shared executor uses the identical sequential FRF path");
    check(propagated,"shared executor joins helpers before propagating a job exception");
    check(equivalent(lvm::compute_prepared_frf(*owned,options).responses.front(),sequential.responses.front()),
          "shared executor can run FRF again after an exception");
    worker.shutdown();worker.shutdown();worker.submit(worker_input,options,16);
    check(!worker.take_result(),"FRF shutdown is idempotent and prevents submissions after join");
}

void test_frf_multi() {
    const double pi=std::acos(-1.0);
    lvm::FrfBatchInput batch;
    batch.references.resize(2); batch.responses.resize(3);
    std::vector<double> average;
    for (int i=0;i<4096;++i) {
        const double angle=2*pi*13*i/256, x=std::sin(angle);
        batch.time.push_back(i/1024.0);
        batch.references[0].push_back(x); batch.references[1].push_back(3*x);
        average.push_back(2*x);
        batch.responses[0].push_back(6*x);
        batch.responses[1].push_back(-2*x);
        batch.responses[2].push_back(10*std::sin(angle+.4));
    }
    lvm::FrfOptions options; options.segment_length=256;
    const auto r=lvm::analyze_frf_batch(batch,options);
    check(r.ok && r.responses.size()==3,"multi-reference produces one FRF per response");
    check_near(lvm::frf_dynamic_coefficient(r.responses[0],13),3,1e-9,"sample mean of x and 3x gives reference 2x before H1");
    check(r.common().reference_amplitude_valid[13],"averaged Reference amplitude is available at the excited bin");
    check_near(r.common().reference_amplitude[13],2,1e-9,"averaged Reference has its own linear amplitude spectrum");
    check_near(r.common().reference_amplitude[13]*lvm::frf_dynamic_coefficient(r.responses[0],13),6,1e-9,
               "Reference amplitude times KD recovers the first Response amplitude");
    check_near(r.common().reference_amplitude[13]*lvm::frf_dynamic_coefficient(r.responses[1],13),2,1e-9,
               "Reference amplitude and KD use compatible linear scales for the second Response");
    check_near(r.common().reference_amplitude[13]*lvm::frf_dynamic_coefficient(r.responses[2],13),10,1e-9,
               "Reference amplitude times KD also preserves a phase-shifted Response magnitude");
    check_near(std::abs(r.responses[1].transfer[13]+1.0),0,1e-9,"second response retains signed complex gain");
    check_near(std::abs(r.responses[2].transfer[13]-std::polar(5.0,.4)),0,1e-9,"third response preserves its own gain and phase");
    for (std::size_t i=0;i<3;++i) {
        const auto expected=lvm::analyze_frf({batch.time,average,batch.responses[i]},options);
        double max_error=0;
        for (std::size_t k=0;k<expected.transfer.size();++k) if (expected.valid[k]) max_error=std::max(max_error,std::abs(expected.transfer[k]-r.responses[i].transfer[k]));
        check(max_error<1e-10 && expected.valid==r.responses[i].valid,"batch agrees with independent pair using pre-averaged reference");
        check(r.responses[i].sample_dt==r.common().sample_dt && r.responses[i].segment_length==256 &&
              r.responses[i].averages==31 && r.responses[i].overlap_samples==128,"all responses share Fs, L, K and overlap");
        bool same_reference_amplitude=r.responses[i].reference_amplitude_valid==r.common().reference_amplitude_valid;
        for (std::size_t k=0;k<r.common().reference_amplitude.size() && same_reference_amplitude;++k) {
            if (r.common().reference_amplitude_valid[k] &&
                r.responses[i].reference_amplitude[k]!=r.common().reference_amplitude[k]) same_reference_amplitude=false;
        }
        check(same_reference_amplitude,"all responses retain the same averaged Reference graph");
        check_near(r.responses[i].coherence[13],1,1e-10,"coherence belongs to each response against averaged reference");
    }
    lvm::FrfBatchInput single{batch.time,{batch.references[0]},{batch.responses[0]}};
    const auto pair=lvm::analyze_frf({single.time,single.references[0],single.responses[0]},options);
    const auto one=lvm::analyze_frf_batch(single,options);
    check(one.ok && one.responses.size()==1 && one.common().transfer==pair.transfer && one.common().valid==pair.valid,
          "one reference and response match the original pair exactly");
    single.references=batch.references;
    check(lvm::analyze_frf_batch(single,options).common().transfer==r.responses[0].transfer,"one response still uses the arithmetic reference mean");
    auto reversed=batch; std::reverse(reversed.references.begin(),reversed.references.end());
    check(lvm::analyze_frf_batch(reversed,options).common().transfer==r.common().transfer,"reference order does not change the mean");
    auto gaps=batch; for(std::size_t i=2048;i<gaps.time.size();++i) gaps.time[i]+=100;
    auto joined=lvm::analyze_frf_batch(gaps,options);
    check(joined.ok && joined.common().gaps_ignored && joined.responses[2].transfer==r.responses[2].transfer,"multi-channel FRF retains gaps-ignored invariance");
    auto bad=batch; bad.references[0][100]=std::nan("");
    check(lvm::analyze_frf_batch(bad,options).error==lvm::FrfError::MissingValues,"invalid reference rejects the whole batch");
    bad=batch; bad.references[1].pop_back();
    check(lvm::analyze_frf_batch(bad,options).error==lvm::FrfError::InvalidChannels,"reference lengths must match the shared selection");
    bad=batch; bad.responses[0][100]=std::nan(""); bad.responses[2].pop_back();
    const auto partial=lvm::analyze_frf_batch(bad,options);
    check(partial.ok && !partial.responses[0].ok && partial.responses[1].ok && !partial.responses[2].ok &&
          partial.responses[1].transfer==r.responses[1].transfer,"invalid responses do not discard valid curves");
    bad=batch; bad.references.clear();
    check(lvm::analyze_frf_batch(bad,options).error==lvm::FrfError::InvalidChannels,"empty reference list rejected");
    bad=batch; bad.responses.clear();
    check(lvm::analyze_frf_batch(bad,options).error==lvm::FrfError::InvalidChannels,"empty response list rejected");
    bad=batch; for(std::size_t i=0;i<bad.time.size();++i) bad.references[1][i]=-bad.references[0][i];
    check(lvm::analyze_frf_batch(bad,options).error==lvm::FrfError::WeakReference,"opposite references cancel instead of producing RMS average");
    std::atomic<bool> cancel{true}; bool cancelled=false;
    try { lvm::analyze_frf_batch(batch,options,&cancel); } catch(const std::exception&) { cancelled=true; }
    check(cancelled,"multi-reference averaging honours cancellation");
    lvm::FrfWorker worker;
    worker.submit(batch,options,40); worker.submit(single,options,41);
    std::optional<lvm::FrfWorker::Result> result;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!result && std::chrono::steady_clock::now()<deadline) {
        result=worker.take_result(); if(!result) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(result && result->generation==41 && result->frf.ok && result->frf.responses.size()==1,"worker replaces the whole batch atomically");

    const auto same_values=[](const std::vector<double>& a,const std::vector<double>& b) {
        if (a.size()!=b.size()) return false;
        for (std::size_t k=0;k<a.size();++k)
            if (a[k]!=b[k] && !(std::isnan(a[k]) && std::isnan(b[k]))) return false;
        return true;
    };
    // Exercise both window-mean policies, non-radix-2 segments and Direct.
    // Compare complete outputs, including independently invalid coherence bins.
    for (bool remove_mean:{false,true}) for (std::size_t length:{256,254,0}) {
        auto opts=options; opts.remove_mean=remove_mean; opts.segment_length=length;
        if (!length) opts.estimator=lvm::FrfEstimator::Direct;
        const auto shared=lvm::analyze_frf_batch(batch,opts);
        for (std::size_t c=0;c<batch.responses.size();++c) {
            const auto expected=lvm::analyze_frf({batch.time,average,batch.responses[c]},opts);
            const auto& actual=shared.responses[c];
            check(actual.ok==expected.ok && actual.error==expected.error &&
                  actual.frequencies==expected.frequencies && actual.transfer==expected.transfer &&
                  actual.valid==expected.valid && same_values(actual.coherence,expected.coherence) &&
                  actual.coherence_valid==expected.coherence_valid &&
                  same_values(actual.reference_amplitude,expected.reference_amplitude) &&
                  actual.reference_amplitude_valid==expected.reference_amplitude_valid &&
                  actual.averages==expected.averages,
                  "shared reference matches every pair array/mask for H1 and Direct");
        }
    }
    bad=batch; bad.responses[0][100]=std::numeric_limits<double>::infinity();
    const auto infinite=lvm::analyze_frf_batch(bad,options);
    check(infinite.ok && infinite.responses[0].error==lvm::FrfError::MissingValues &&
          infinite.responses[1].transfer==r.responses[1].transfer,
          "infinite Response is independent of valid Responses");
    bad=batch; for(auto& ref:bad.references) for(auto& value:ref) value=0;
    const auto zero=lvm::analyze_frf_batch(bad,options);
    check(!zero.ok && zero.error==lvm::FrfError::WeakReference &&
          zero.responses[0].valid==zero.responses[1].valid &&
          same_values(zero.responses[0].reference_amplitude,zero.responses[1].reference_amplitude),
          "zero common reference retains identical masks and amplitude graphs");
}

void test_frf() {
    const double pi=std::acos(-1.0);
    const auto make_pair=[&](std::size_t n, double gain=2.0, double phase=0.7) {
        lvm::FrfInput in;
        for (std::size_t i=0;i<n;++i) {
            in.time.push_back(i/1024.0);
            in.reference.push_back(std::sin(2*pi*13*i/256));
            in.response.push_back(gain*std::sin(2*pi*13*i/256+phase));
        }
        return in;
    };
    auto in=make_pair(4096);
    const auto original=in;
    lvm::FrfOptions opt; opt.segment_length=256;
    auto r=lvm::analyze_frf(in,opt);
    check(r.ok && r.segment_length==256 && r.averages==31 && r.overlap_samples==128,"H1 uses Hann with 50 percent overlap");
    check(r.valid[13] && !r.valid[100] && !r.valid[0],"H1 masks unexcited input and DC");
    check_near(r.frequencies[13],52,1e-10,"H1 frequency uses Fs/L");
    check_near(lvm::frf_dynamic_coefficient(r,13),2,1e-9,"H1 dynamic coefficient preserves gain");
    check(r.reference_amplitude_valid[13],"H1 exposes the linear Reference amplitude separately");
    check_near(r.reference_amplitude[13],1,1e-9,"one-sided Hann normalization preserves Reference sine amplitude");
    check_near(std::arg(r.transfer[13]),.7,1e-9,"H1 preserves known positive phase shift");
    check_near(std::abs(r.transfer[13]-std::polar(2.0,.7)),0,1e-9,"H1 stores known complex gain");
    check(r.coherence_valid[13],"coherence is available after averaging");
    check_near(r.coherence[13],1,1e-10,"coherence of noiseless linear pair is unity");
    check(in.time==original.time && in.reference==original.reference && in.response==original.response,"FRF leaves source arrays unchanged");
    auto samples=lvm::prepare_frf_samples(in);
    auto prepared=lvm::compute_frf(samples,opt);
    check(prepared.transfer==r.transfer,"numerical H1 accepts prepared arrays without Dataset or indices");
    check(!samples.gaps_ignored && samples.reference==in.reference && samples.response==in.response,"continuous preparation preserves every sample");
    auto automatic=lvm::analyze_frf(in);
    check(automatic.segment_length==1024 && automatic.averages==7,"Auto balances resolution and averaging");
    in.response=in.reference;
    check_near(lvm::frf_dynamic_coefficient(lvm::analyze_frf(in,opt),13),1,1e-10,"unity transfer has dynamic coefficient one");
    in.response.assign(in.time.size(),0);
    auto zero=lvm::analyze_frf(in,opt);
    check(zero.ok && lvm::frf_dynamic_coefficient(zero,13)==0,"zero response remains valid with zero dynamic coefficient");
    check(!zero.coherence_valid[13] && std::isnan(zero.coherence[13]),"zero output has undefined coherence independently of valid H1");
    in.reference.assign(in.time.size(),3);
    check(lvm::analyze_frf(in,opt).error==lvm::FrfError::WeakReference,"constant reference rejected");

    for (std::size_t n:{1024u,1001u,5u}) {
        in=make_pair(n,2,0);
        lvm::FrfOptions direct; direct.estimator=lvm::FrfEstimator::Direct;
        auto d=lvm::analyze_frf(in,direct);
        check(d.ok && d.segment_length==n && d.averages==1 && d.frequencies.size()==n/2+1,"Direct preserves arbitrary FFT lengths");
        check(!d.coherence_valid[1],"Direct does not report spurious unity coherence");
        auto excited=std::find(d.valid.begin(),d.valid.end(),1);
        check(excited!=d.valid.end(),"Direct finds an excited input bin");
        if (excited!=d.valid.end()) check_near(lvm::frf_dynamic_coefficient(d,excited-d.valid.begin()),2,1e-7,"Direct gain preserved");
    }
    in=make_pair(256);
    auto one=lvm::analyze_frf(in,opt);
    lvm::FrfOptions direct; direct.estimator=lvm::FrfEstimator::Direct;
    auto d=lvm::analyze_frf(in,direct);
    check(one.averages==1 && !one.coherence_valid[13],"one H1 segment has insufficient coherence statistics");
    check_near(std::abs(one.transfer[13]-d.transfer[13]),0,1e-12,"one-segment H1 agrees with Direct");
    std::swap(in.reference,in.response);
    check_near(std::arg(lvm::analyze_frf(in,opt).transfer[13]),-.7,1e-9,"swapping arrays reverses phase convention");

    in=make_pair(1024);
    const auto continuous=lvm::analyze_frf(in,opt);
    for (std::size_t i=512;i<1024;++i) in.time[i]+=1000000;
    samples=lvm::prepare_frf_samples(in);
    check(samples.gaps_ignored && samples.reference==in.reference && samples.response==in.response,"gaps only set a warning and never insert, remove or interpolate samples");
    r=lvm::compute_frf(samples,opt);
    check(r.ok && r.gaps_ignored && r.averages==7 && r.sample_count==1024,"H1 includes Welch windows crossing timestamp gaps");
    check(r.transfer==continuous.transfer && r.valid==continuous.valid && r.coherence_valid==continuous.coherence_valid &&
          r.frequencies==continuous.frequencies,"timestamp gaps leave complex H1 and frequency scale unchanged");
    bool same_coherence=true;
    for (std::size_t k=0;k<r.coherence.size();++k) if (r.coherence_valid[k] && r.coherence[k]!=continuous.coherence[k]) same_coherence=false;
    check(same_coherence,"timestamp gaps leave averaged coherence unchanged");
    check_near(r.sample_dt,1.0/1024,1e-12,"large outage does not change physical sample rate");
    check_near(std::abs(r.transfer[13]-std::polar(2.0,.7)),0,1e-9,"H1 with ignored gaps preserves known phase and gain");
    opt.segment_length=768;
    r=lvm::analyze_frf(in,opt);
    check(r.ok && r.segment_length==768 && r.averages==1,"L larger than either fragment fits the total selection without reduction");
    opt.segment_length=1024;
    check(lvm::analyze_frf(in,opt).ok,"selected samples equal to L produce one complete segment across a gap");
    opt.segment_length=2048;
    check(lvm::analyze_frf(in,opt).error==lvm::FrfError::TooShort,"selected samples less than L report TooShort");
    opt.segment_length=256;
    for (std::size_t i=1;i+1<in.time.size();++i) in.time[i]+=(i%2 ? .01 : -.01)/1024;
    for (std::size_t i=0;i<in.time.size();++i) in.response[i]=2*in.reference[i];
    r=lvm::analyze_frf(in,opt);
    samples=lvm::prepare_frf_samples(in);
    check(r.ok && r.gaps_ignored && samples.reference==in.reference && samples.response==in.response,"jitter plus gaps do not alter selected arrays");
    check_near(lvm::frf_dynamic_coefficient(r,13),2,1e-8,"uniform sample indexing preserves channel ratio");
    // Model repeated 2000-sample LVM sections at 20 kHz: L=4096 spans sections.
    in=make_pair(16000);
    for (std::size_t i=0;i<in.time.size();++i) in.time[i]=i/20000.0;
    opt.segment_length=4096;
    const auto no_sections=lvm::analyze_frf(in,opt);
    for (std::size_t i=0;i<in.time.size();++i) in.time[i]+=(i/2000)*.01;
    r=lvm::analyze_frf(in,opt);
    check(r.ok && r.segment_length==4096 && r.averages==6 && r.gaps_ignored,"L=4096 spans 2000-sample sections at 20 kHz");
    check(r.transfer==no_sections.transfer && r.valid==no_sections.valid,"section timestamp offsets never change complex transfer");
    check_near(1.0/r.sample_dt,20000,1e-5,"section gaps do not lower Fs");
    check(lvm::analyze_frf(in).segment_length==lvm::analyze_frf(make_pair(16000)).segment_length,"Auto L depends on sample count, not gaps");
    opt.segment_length=256;

    in=make_pair(1024);
    auto bad=in; bad.response.pop_back();
    check(lvm::analyze_frf(bad).error==lvm::FrfError::InvalidChannels,"mismatched arrays rejected");
    bad=in; bad.response[100]=std::numeric_limits<double>::quiet_NaN();
    check(lvm::analyze_frf(bad,opt).error==lvm::FrfError::MissingValues,"NaN rejected even with stitching");
    bad=in; bad.reference[100]=std::numeric_limits<double>::infinity();
    check(lvm::analyze_frf(bad).error==lvm::FrfError::MissingValues,"infinite samples rejected");
    bad=in; bad.time[10]=bad.time[9];
    check(lvm::analyze_frf(bad).error==lvm::FrfError::InvalidTime,"duplicate time rejected");
    bad=in; bad.time[10]=std::numeric_limits<double>::infinity();
    check(lvm::analyze_frf(bad).error==lvm::FrfError::InvalidTime,"nonfinite time rejected");
    auto invalid=opt; invalid.segment_length=3;
    check(lvm::analyze_frf(in,invalid).error==lvm::FrfError::InvalidOptions,"odd short segment rejected");
    invalid=opt; invalid.reference_threshold=0;
    check(lvm::analyze_frf(in,invalid).error==lvm::FrfError::InvalidOptions,"zero denominator threshold rejected");
    samples=lvm::prepare_frf_samples(in); samples.sample_dt=0;
    check(lvm::compute_frf(samples,opt).error==lvm::FrfError::InvalidTime,"prepared core validates uniform cadence");
    std::atomic<bool> cancel{true}; bool cancelled=false;
    try { lvm::analyze_frf(in,opt,&cancel); } catch(const std::exception&) { cancelled=true; }
    check(cancelled,"FRF preparation honours cancellation");
    cancelled=false;
    try { lvm::compute_frf(samples,opt,&cancel); } catch(const std::exception&) { cancelled=true; }
    check(cancelled,"prepared estimator honours cancellation");

    // Reproducible broadband excitation with independent output noise.
    std::mt19937 random(1731); std::normal_distribution<double> normal;
    lvm::FrfInput noisy;
    for (std::size_t i=0;i<65536;++i) {
        const double x=normal(random);
        noisy.time.push_back(i/1024.0); noisy.reference.push_back(x); noisy.response.push_back(2*x+normal(random));
    }
    opt.segment_length=1024;
    auto h1=lvm::analyze_frf(noisy,opt);
    auto raw=lvm::analyze_frf(noisy,direct);
    double mse_h1=0,mse_direct=0,cmean=0;
    for(std::size_t k=2;k<500;++k) {
        mse_h1+=std::norm(h1.transfer[k]-2.0); mse_direct+=std::norm(raw.transfer[k*64]-2.0);
        cmean+=h1.coherence[k];
    }
    cmean/=498;
    check(h1.ok && h1.averages==127 && mse_h1<mse_direct*.1,"Welch H1 reduces independent output-noise error");
    check(cmean>.75 && cmean<.85,"coherence matches 4/(4+1) for output noise");
    for (auto& y:noisy.response) y=normal(random);
    auto unrelated=lvm::analyze_frf(noisy,opt);
    double unrelated_mean=0;
    for(std::size_t k=2;k<500;++k) unrelated_mean+=unrelated.coherence[k]/498;
    check(unrelated_mean<.05,"unrelated broadband signals have low averaged coherence");
    check(unrelated.ok,"low coherence remains a diagnostic and does not remove the transfer curve");

    lvm::FrfWorker worker;
    worker.submit(noisy,opt,17); worker.submit(in,opt,18);
    std::optional<lvm::FrfWorker::Result> result;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!result && std::chrono::steady_clock::now()<deadline) {
        result=worker.take_result();
        if(!result) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(result && result->generation==18 && result->frf.ok,"FRF worker publishes latest array-pair request");
    worker.submit(noisy,opt,19); worker.cancel();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    check(!worker.take_result(),"cancelled FRF result is not published");
}

void test_fft_chunks_and_plans() {
    std::printf("test_fft_chunks_and_plans\n");
    constexpr double pi = 3.14159265358979323846;
    // Odd/even lengths, a warm plan, then eviction and a new input at the
    // previous length: compare complex values with an independent direct DFT.
    int pass = 0;
    for (std::size_t n : {15u, 30u, 30u, 29u, 15u, 32u}) {
        std::vector<std::complex<double>> input(n);
        const double scale = 1 + .1*pass++;
        for (std::size_t j = 0; j < n; ++j) input[j] = {scale*std::sin(.7*j), std::cos(.3*j)};
        const auto result = lvm::dft(input);
        double error = 0;
        for (std::size_t k = 0; k < n; ++k) {
            std::complex<double> expected{};
            for (std::size_t j = 0; j < n; ++j) {
                const double angle = -2*pi*k*j/n;
                expected += input[j] * std::complex<double>(std::cos(angle), std::sin(angle));
            }
            error = std::max(error, std::abs(result[k]-expected));
        }
        check(error < 1e-10, "cold/warm/replaced plan agrees with complex direct DFT");
    }
    for (std::size_t n : {8192u, 32768u, 131072u}) {
        std::vector<std::complex<double>> data(n);
        for (std::size_t j = 0; j < n; ++j) data[j] = {std::sin(.01*j), std::cos(.07*j)};
        const auto original = data;
        lvm::fft_radix2(data, false);
        lvm::fft_radix2(data, true);
        double error = 0;
        for (std::size_t j = 0; j < n; ++j) error = std::max(error,std::abs(data[j]-original[j]));
        check(error < 1e-9, "chunk boundaries preserve forward/inverse FFT normalisation");
    }
    std::atomic<bool> cancel{true};
    for (std::size_t n : {16u, 15u, 29u}) {
        std::vector<std::complex<double>> input(n, {1, 2});
        bool stopped = false;
        try { (void)lvm::dft(input, &cancel); } catch (const std::runtime_error&) { stopped = true; }
        check(stopped, "cancelled radix-2/cached/new plan stops before publishing output");
        const auto result = lvm::dft(input);
        check(std::abs(result.front()-std::complex<double>(n,2*n)) < 1e-10,
              "a cancelled call cannot poison the next plan or result");
    }
    // Different inputs/lengths on independent threads must not share a plan.
    const auto retained_before=lvm::fft_cached_plan_bytes();
    std::vector<std::thread> threads;
    std::vector<unsigned char> correct(3,0);
    std::vector<unsigned char> accounted(3,0);
    for (std::size_t i = 0; i < correct.size(); ++i) threads.emplace_back([&,i] {
        const std::size_t n = 15+2*i;
        std::vector<std::complex<double>> input(n, {double(i+1), -1});
        const auto first = lvm::dft(input), second = lvm::dft(input);
        correct[i] = first == second && std::abs(first.front()-std::complex<double>(n*(i+1),-double(n))) < 1e-10;
        accounted[i]=lvm::fft_cached_plan_bytes()>retained_before;
    });
    for (auto& thread : threads) thread.join();
    check(std::all_of(correct.begin(),correct.end(),[](auto v){return v!=0;}), "plan caches are independent across threads");
    check(std::all_of(accounted.begin(),accounted.end(),[](auto v){return v!=0;}),"workspace accounting includes published thread-local plans");
    check(lvm::fft_cached_plan_bytes()==retained_before,"thread exit releases its retained-plan allowance");
    bool repeated_correct=true,repeated_released=true;
    for(int round=0;round<256;++round) {
        threads.clear();std::fill(correct.begin(),correct.end(),0);
        for(std::size_t i=0;i<correct.size();++i)threads.emplace_back([&,i] {
            std::vector<std::complex<double>> input(15+2*i,{double(round+1),-1});
            correct[i]=lvm::dft(input)==lvm::dft(input);
        });
        for(auto& thread:threads)thread.join();
        repeated_correct&=std::all_of(correct.begin(),correct.end(),[](auto v){return v!=0;});
        repeated_released&=lvm::fft_cached_plan_bytes()==retained_before;
    }
    check(repeated_correct,"768 short-lived FFT threads preserve their own cold and warm plan results");
    check(repeated_released,"repeated concurrent thread teardown releases every retained FFT plan");
}

void test_fft_channel_pool() {
    lvm::Dataset data;
    data.names={"skip","first","second"}; data.channels.resize(3);
    for (std::size_t i=0;i<65537;++i) {
        data.time.push_back(i/1024.0);
        data.channels[0].push_back(std::nan(""));
        data.channels[1].push_back(std::sin(.13*i));
        data.channels[2].push_back(3*std::cos(.07*i));
    }
    data.channels[1][10]=std::nan(""); // Existing mean-fill policy is retained.
    const double untouched=data.channels[2][100];
    const auto calculate=[&] { return lvm::compute_spectrum(data,0); };
    auto a=std::async(std::launch::async,calculate);
    auto b=std::async(std::launch::async,calculate);
    const auto first=a.get(), second=b.get();
    check(first.ok && first.names==std::vector<std::string>{"first","second"} &&
          first.source_channels==std::vector<std::size_t>{1,2} &&
          first.freqs==second.freqs && first.amp==second.amp,
          "concurrent pool callers preserve channel order and skipped channels");
    for (std::size_t c=1;c<3;++c) {
        auto single=data; single.names={data.names[c]}; single.channels={data.channels[c]};
        const auto expected=lvm::compute_spectrum(single,0);
        check(expected.ok && expected.freqs==first.freqs && expected.amp[0]==first.amp[c-1],
              "parallel arbitrary-length FFT exactly matches one-channel sequential path");
    }
    check(std::isnan(data.channels[1][10]) && data.channels[2][100]==untouched,
          "channel pool leaves source arrays untouched");
    data.channels[2][10]=std::numeric_limits<double>::infinity();
    const auto failed=lvm::compute_spectrum(data,0);
    check(!failed.ok && failed.error=="Channel contains infinite values." &&
          failed.names==std::vector<std::string>{"first"},
          "parallel failure retains the sequential error and completed prefix");
    data.channels[2][10]=0;
    lvm::SpectrumWorker worker;
    worker.submit(data,{0,1,2},90);
    worker.shutdown(); worker.shutdown();
    worker.submit(data,{0,1,2},91);
    check(!worker.take_result(),"terminal shutdown joins work, clears results and ignores new submissions");
}

}  // namespace

int main(int argc, char** argv) {
    test_frf_streaming();
    test_frf_multi();
    test_frf();
    if (argc>1 && std::string(argv[1])=="--frf") {
        std::printf("\n%d FRF checks, %d failure(s)\n",g_checks,g_failures);
        return g_failures==0 ? 0 : 1;
    }
    test_data_integrity_regressions();
    test_spectrum_integrity_regressions();
    test_fft_chunks_and_plans();
    test_fft_channel_pool();
    test_fft_irregular_timestamps();
    test_fft_gap_regressions();
    test_minmax_and_spectrum_import();
    test_basic_parse();
    test_metadata_and_nan();
    test_decimal_comma();
    test_multi_header();
    test_make_monotonic_equal_times();
    test_make_monotonic_backward_jump();
    test_drop_duplicate_time();
    test_interleaved_channel_names();
    test_generated_names_after_interleaved_time_columns();
    test_reference_test_lvm();
    test_fft_peak();
    test_fft_nyquist_amplitude();
    test_fft_sample_cap_too_small();
    test_missing_file();
    test_formula_engine();
    test_export_helpers();
    test_gap_details();
    test_scan_and_window_load();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

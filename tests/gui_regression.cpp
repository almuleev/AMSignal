// Integration tests linked to the production GUI modules; no visible windows.
#include "../gui_dialogs.hpp"
#include "../gui_analysis_source.hpp"
#include "../gui_commands.hpp"
#include "../gui_frf.hpp"
#include "../gui_frf_render.hpp"
#include "../gui_input.hpp"
#include "../gui_navigation.hpp"
#include "../gui_hotkeys.hpp"
#include "../gui_window.hpp"
#include "../gui_layout.hpp"
#include "../gui_export.hpp"
#include "../gui_export_metadata.hpp"
#include "../gui_ids.hpp"
#include "../gui_loading.hpp"
#include "../gui_documents.hpp"
#include "../gui_main.hpp"
#include "../gui_processing.hpp"
#include "../gui_render.hpp"
#include "../gui_render_data.hpp"
#include "../gui_spectrum.hpp"
#include "../gui_state.hpp"
#include "../gui_state_history.hpp"
#include "../gui_text.hpp"
#include "../gui_theme.hpp"
#include "../gui_time_axis.hpp"
#include "../gui_settings_window.hpp"
using namespace gui;
#include <iostream>
#include <stdexcept>
#include <chrono>
#ifdef near
#undef near
#endif

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message) {
    require(std::isfinite(actual) && std::fabs(actual - expected) < 1e-9, message);
}
const std::filesystem::path test_dir = "tests/_tmp_gui_regression";

void reset_document(const std::vector<std::string>& names, const std::vector<double>& time,
                    const std::vector<std::vector<double>>& channels) {
    g_frf_worker.cancel();
    g = App{};
    g_undo.clear(); g_redo.clear(); g_filter_slider_before.reset();
    g_str = &kEn;
    g_config_path = (test_dir / "settings.ini").wstring();
    g.ds.ok = true; g.ds.names = names; g.ds.time = time; g.ds.raw_time = time; g.ds.channels = channels;
    g.visible.assign(names.size(), 1);
    g.channel_colors.clear();
    for (const auto& name : names) g.channel_labels.push_back(to_w(name));
    g.data_t0 = g.win_start = time.front(); g.data_t1 = g.win_end = time.back();
    g.channel_formulas.assign(names.size(), L"x");
    ensure_channel_formula_vectors();
}

void reopen(const std::filesystem::path& path, bool dedup = false) {
    auto ds = lvm::read_lvm_file(path);
    require(ds.ok, "export must be readable");
    if (dedup) lvm::drop_duplicate_time_channels(ds, ds.raw_time);
    auto comments = ds.export_comments;
    reset_document(ds.names, ds.time, ds.channels);
    g.ds.frequency_axis = ds.frequency_axis;
    g.ds.export_comments = comments;
    apply_export_metadata_from_comments(comments);
}

void document_history_and_save_state() {
    reset_document({"A"}, {0, 1, 2, 3}, {{1, 2, 3, 4}});
    mark_active_document_saved();
    g.visual_smooth = true;
    mark_active_document_dirty();
    require(active_document_has_unsaved_changes(), "project graph setting marks the document dirty without an undo action");
    mark_active_document_saved();
    GuideLine first;
    first.value = 1.0;
    g.guides.push_back(first);
    UndoAction first_action;
    first_action.type = UndoAction::ADD_LINE;
    first_action.line = first;
    push_undo(first_action);
    require(active_document_has_unsaved_changes(), "editing a document marks its project dirty");
    pop_undo();
    require(!active_document_has_unsaved_changes(), "undoing to the saved revision clears the dirty state");
    pop_redo();
    require(active_document_has_unsaved_changes(), "redoing past the saved revision restores the dirty state");
    pop_undo();
    require(!active_document_has_unsaved_changes(), "undo returns to the original saved revision before branching");
    GuideLine branch;
    branch.value = 1.5;
    g.guides.push_back(branch);
    UndoAction branch_action;
    branch_action.type = UndoAction::ADD_LINE;
    branch_action.line = branch;
    push_undo(branch_action);
    require(active_document_has_unsaved_changes(), "a new branch after undo cannot reuse the saved revision");
    mark_active_document_saved();
    require(!active_document_has_unsaved_changes(), "successful project save records the active history revision");
    GuideLine after_save;
    after_save.value = 3.0;
    g.guides.push_back(after_save);
    UndoAction after_save_action;
    after_save_action.type = UndoAction::ADD_LINE;
    after_save_action.line = after_save;
    push_undo(after_save_action);
    require(active_document_has_unsaved_changes(), "new edit after saving marks the project dirty again");

    DocumentState other = static_cast<DocumentState&>(g);
    other.file_name = L"other.lvm";
    other.history.reset();
    other.project_revision = 0;
    other.next_project_revision = 0;
    other.saved_project_revision = 0;
    other.project_dirty = false;
    g.inactive_documents.push_back(std::move(other));
    const std::size_t first_history_count = g_undo.size();
    require(switch_to_document(1), "switches to an inactive document for independent history");
    require(g_undo.empty(), "inactive document starts with its own empty undo history");
    GuideLine second;
    second.value = 2.0;
    g.guides.push_back(second);
    UndoAction second_action;
    second_action.type = UndoAction::ADD_LINE;
    second_action.line = second;
    push_undo(second_action);
    require(g_undo.size() == 1, "second document receives its own undo action");
    require(switch_to_document(1), "switches back to the first document");
    require(g_undo.size() == first_history_count, "first document restores its own undo history");
    require(active_document_has_unsaved_changes(), "first document restores its independent dirty state");
}

void exports() {
    ExportOptions opts;
    opts.selected_range = ExportRangeMode::Whole;
    opts.include_hidden_channels = true;
    const auto path = test_dir / std::filesystem::u8path(u8"измерение_測定.csv");
    reset_document({"A"}, {0,1,2,3}, {{10,11,12,13}});
    g.global_formula = L"2*x"; rebuild_formula_cache_from_state();
    require(save_tabular_export(path.wstring(), opts), "processed export");
    reopen(path);
    near(rendered_channel_sample(0,0), 20, "processed export must not apply formula twice");
    require(save_tabular_export(path.wstring(), opts), "second export");
    reopen(path);
    near(rendered_channel_sample(0,0), 20, "multiple roundtrips remain stable");

    reset_document({"A","B"}, {0,1,2,3}, {{10,11,12,13},{20,21,22,23}});
    g.visible = {0,1}; g.channel_formulas = {L"3*x",L"x+7"}; rebuild_formula_cache_from_state();
    opts.include_hidden_channels = false; opts.apply_processing_to_data = false;
    require(save_tabular_export(path.wstring(), opts), "subset export");
    reopen(path);
    require(g.ds.names == std::vector<std::string>{"B"} && g.visible[0], "subset identity and visibility");
    near(rendered_channel_sample(0,0), 27, "subset local formula mapping");

    reset_document({u8"Датчик, \"A\", visible=0","B"}, {0,1,2,3}, {{10,11,12,13},{20,21,22,23}});
    opts.include_hidden_channels = true;
    require(save_tabular_export(path.wstring(), opts), "quoted name export");
    reopen(path);
    require(g.ds.names[0] == u8"Датчик, \"A\", visible=0" && g.ds.names[1] == "B", "UTF-8 and metadata text escaping");

    reset_document({"A","B"}, {0,1,2,3}, {{10,11,12,13},{20,21,22,23}});
    opts.format = ExportFileFormat::Lvm;
    const auto lvm_path = test_dir / "roundtrip.lvm";
    require(save_lvm_export(lvm_path.wstring(), opts), "LVM export");
    auto parsed = lvm::read_lvm_file(lvm_path);
    require(parsed.ok, "read LVM");
    lvm::drop_duplicate_time_channels(parsed, parsed.raw_time);
    require(parsed.names == std::vector<std::string>{"A","B"}, "LVM labels match data without metadata import");
    near(parsed.channels[0][0], 10, "LVM channel A"); near(parsed.channels[1][3], 23, "LVM channel B");

    ExportOptions original;
    original.save_mode = ExportSaveMode::OriginalData;
    original.selected_range = ExportRangeMode::Whole;
    original.apply_processing_to_data = false;
    original.include_channel_names = true;
    original.include_hidden_channels = true;
    original.include_metadata = false;
    g.global_formula = L"5*x"; rebuild_formula_cache_from_state();
    const auto original_path = test_dir / "original.csv";
    require(save_tabular_export(original_path.wstring(), original), "original-data export");
    auto original_data = lvm::read_lvm_file(original_path);
    require(original_data.ok && original_data.export_comments.empty(), "original export contains no AMSignal settings");
    near(original_data.channels[0][0], 10, "original export retains unprocessed samples");

    g.visible = {1, 0};
    g.global_formula = L"2*x"; g.channel_formulas = {L"x+1", L"x-1"};
    g.noise_threshold_enabled = true; g.noise_threshold_min = 0.25; g.noise_threshold_max = 0.75;
    g.distinguish_curves = true;
    g.frf.apply_processing = false;
    g.frf.logarithmic_frequency_axis = false;
    g.frf.show_reference_amplitude = false;
    g.frf.reference_height_fraction = .42;
    g.frf.reference_auto_y = false;
    g.frf.reference_y_max = 7.5;
    rebuild_formula_cache_from_state();
    const auto project_path = test_dir / "roundtrip.AMSig";
    require(save_project_file(project_path.wstring()), "project export");
    reopen(project_path, true);
    require(g.ds.names == std::vector<std::string>{"A", "B"} && g.visible == std::vector<char>{1, 0},
            "project restores all channels and visibility");
    near(g.ds.channels[0][0], 10, "project retains raw samples");
    require(g.global_formula == L"2*x" && g.channel_formulas[0] == L"x+1",
            "project restores formulas without baking them into data");
    require(g.noise_threshold_enabled, "project restores filter settings");
    require(g.distinguish_curves, "project restores grayscale curve symbols");
    require(!g.frf.apply_processing, "project restores the FRF raw-channel choice");
    require(!g.frf.logarithmic_frequency_axis && !g.frf.show_reference_amplitude &&
            std::fabs(g.frf.reference_height_fraction-.42)<1e-9 && !g.frf.reference_auto_y &&
            std::fabs(g.frf.reference_y_max-7.5)<1e-9,
            "project restores FRF axis and Reference graph display settings");

    opts.format = ExportFileFormat::Csv; opts.selected_range = ExportRangeMode::Visible;
    { std::ofstream out(path); out << "KEEP THIS FILE"; }
    g.win_start = 10; g.win_end = 11;
    require(!save_tabular_export(path.wstring(), opts), "invalid export fails");
    std::ifstream in(path); std::string content; std::getline(in, content);
    require(content == "KEEP THIS FILE", "failed export preserves destination");
}

void processing() {
    reset_document({"same","same"},{0,.1,.2,.3,.4,.5,.6,.7},{{1,0,-1,0,1,0,-1,0},{2,0,-2,0,2,0,-2,0}});
    compute_spectrum_for_window(0,.7,false);
    require(g.spec_channel_indices == std::vector<int>{0,1}, "duplicate names retain independent spectrum identities");
    g.global_formula=L"2*x"; rebuild_formula_cache_from_state();
    compute_spectrum_for_window(0,.7,false); g.mode = AnalysisMode::FFT;
    ExportOptions opts; opts.apply_processing_to_data=false; opts.include_hidden_channels=true;
    lvm::Spectrum spec; std::vector<int> ids; bool all=false;
    require(build_export_spectrum(opts,spec,ids,all), "raw spectrum export");
    const auto peaks=lvm::find_peaks(spec.freqs,spec.amp[0],1);
    require(!peaks.empty(), "raw spectrum peak"); near(peaks[0].amp,1,"raw FFT export ignores active formula");

    std::vector<double> time(1000), signal(1000,0);
    for(std::size_t i=0;i<time.size();++i) time[i]=double(i)/1000;
    signal[499]=1; signal[500]=std::nan("");
    reset_document({"signal"},time,{signal});
    g.noise_threshold_enabled=true;g.noise_threshold_mode=FilterModeBandPass;
    g.noise_threshold_topology=FilterTopologyLinkwitzRiley;g.noise_threshold_min=10;g.noise_threshold_max=100;
    ensure_filtered_channel_cache(0);
    near(g.filtered_channel_cache[0][501],0,"all LR stages reset after NaN");
    g.noise_threshold_enabled=false;
    g.global_formula=L"sqrt(x)";rebuild_formula_cache_from_state();
    require(std::isnan(transform_channel_value(0,-1)),"domain error does not silently restore input");

    constexpr std::size_t count=4096;
    constexpr double sample_rate=16384.0;
    constexpr double pi=3.14159265358979323846;
    std::vector<double> filter_time(count),mixed(count),response(count);
    for (std::size_t i=0;i<count;++i) {
        filter_time[i]=i/sample_rate;
        mixed[i]=std::sin(2*pi*1000*filter_time[i])+std::sin(2*pi*4000*filter_time[i]);
        response[i]=2*mixed[i];
    }
    reset_document({"Reference","Response"},filter_time,{mixed,response});
    compute_spectrum_for_window(filter_time.front(),filter_time.back(),false);
    const double raw_low=g.spec.amp[0][250],raw_high=g.spec.amp[0][1000];
    g.noise_threshold_enabled=true;
    g.noise_threshold_mode=FilterModeLowPass;
    g.noise_threshold_min=0;
    g.noise_threshold_max=2000;
    invalidate_filtered_channel_cache();
    compute_spectrum_for_window(filter_time.front(),filter_time.back(),false);
    require(g.spec.amp[0][250]>raw_low*.7,"2 kHz low-pass keeps the 1 kHz FFT component");
    require(g.spec.amp[0][1000]<raw_high*.3,"2 kHz low-pass attenuates, rather than removes, the 4 kHz FFT component");
    require(rendered_channel_sample(0,10)!=g.ds.channels[0][10],"Time view reads the filtered channel cache");
    require(g.frf.apply_processing,"new FRF documents apply active processing by default");
    require(set_frf_channels({0},{1}),"processed FRF test selects Reference and Response");
    g.frf.options.estimator=lvm::FrfEstimator::Direct;
    set_mode(AnalysisMode::FRF);
    require(g.frf.result.ok,"processed FRF is calculated");
    near(lvm::frf_dynamic_coefficient(g.frf.result.common(),250),2,"equal filtering of Reference and Response cancels in linear KD");
    require(g.frf.result.common().reference_amplitude[1000] < g.frf.result.common().reference_amplitude[250]*.3,
            "separate averaged Reference graph shows low-pass attenuation");
}

void fft_recording_recovery() {
    std::vector<double> time, signal;
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < 1152; ++i) {
        const int local = i < 128 ? i : i - 128;
        const double offset = i >= 128 && local > 0 && local < 1023 ? 0.0001 * std::sin(2 * pi * local / 17) : 0;
        const double t = (i < 128 ? 0 : 10) + local / 1024.0 + offset;
        time.push_back(t); signal.push_back(std::sin(2 * pi * 64 * t));
    }
    reset_document({"signal"}, time, {signal});
    compute_spectrum_for_window(time.front(), time.back(), false);
    require(g.spec_valid && g.spec.gaps_ignored && g.spec.resampled && g.spec.n == 1152, "GUI uses every jittered sample around a gap");
    require(g.spec_channel_indices == std::vector<int>{0}, "recovered FFT preserves GUI channel mapping");
    near(g.spec.source_start, 0, "GUI spectrum records the full selected interval");
    g_str = &kRu;
    const auto status = spectrum_sampling_status(g.spec);
    require(status.find(L"без учёта пропусков") != std::wstring::npos && status.find(L"интерполяция") != std::wstring::npos,
            "Russian status explains ignored gaps and interpolation");
    g_str = &kEn;

    // Exercise the real background worker, then its GUI completion handler.
    lvm::SpectrumWorker worker;
    worker.submit(g.ds, {7}, 1);
    worker.submit(g.ds, {0}, 2);
    std::optional<lvm::SpectrumWorker::Result> result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!result && std::chrono::steady_clock::now() < deadline) {
        result = worker.take_result();
        if (!result) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(result && result->generation == 2, "worker publishes the latest FFT request");
    require(result->spectrum.ok && result->spectrum.resampled && result->spectrum.gaps_ignored && result->spectrum.n == 1152,
            "background FFT recovers the same irregular recording");
    g.freq_start = 0; g.freq_end = 1; g.spec_pending = true; g.spec_fit_pending = true;
    apply_spectrum_result(std::move(result->spectrum));
    require(g.spec_valid && !g.spec_pending && !g.spec_fit_pending, "GUI accepts background FFT result");
    near(g.freq_end, g.spec.nyquist, "background FFT restores full frequency axis");

    // Re-entering the same source window must still request a fresh axis fit.
    set_mode(AnalysisMode::FFT);
    require(g.spec_valid && g.freq_end > 100, "entering FFT fits the frequency axis");
    set_mode(AnalysisMode::Time); g.freq_end = 1;
    set_mode(AnalysisMode::FFT);
    near(g.freq_end, g.spec.nyquist, "re-entering FFT restores full frequency axis");

    ExportOptions opts; opts.selected_range = ExportRangeMode::Whole; opts.include_hidden_channels = true;
    const auto path = test_dir / "recovered_fft.csv";
    require(save_tabular_export(path.wstring(), opts), "recovered spectrum exports");
    const auto exported = lvm::read_lvm_file(path);
    require(exported.ok && exported.rows() == g.spec.freqs.size(), "recovered spectrum export has all frequency bins");
    bool source = false, resampled = false, gaps_ignored = false;
    for (const auto& comment : exported.export_comments) {
        source = source || comment == "source_start=0";
        resampled = resampled || comment == "resampled=1";
        gaps_ignored = gaps_ignored || comment == "gaps_ignored=1";
    }
    require(source && resampled && gaps_ignored, "export records full FFT source, interpolation and ignored gaps");
}

void fft_selected_gap_range() {
    // Selection boundaries lie between samples, inside gaps. Large outside
    // impulses must not leak into the FFT; both interior channels must survive.
    constexpr std::size_t count = 12032, lo = 100, hi = 11932;
    std::vector<double> time(count);
    std::vector<std::vector<double>> values(2, std::vector<double>(count));
    lvm::Dataset compact;
    compact.names = {"A", "B"}; compact.channels.resize(2);
    for (std::size_t i = 0; i < count; ++i) {
        time[i] = i ? time[i - 1] + (i % 8 ? 1025.0 : 1.0) / 1024 : 50.0;
        for (std::size_t c = 0; c < 2; ++c) {
            values[c][i] = i < lo || i >= hi ? 1e6 : std::sin(i * (0.13 + 0.2 * c));
            if (i >= lo && i < hi) compact.channels[c].push_back(values[c][i]);
        }
        if (i >= lo && i < hi) compact.time.push_back((i - lo) / 1024.0);
    }
    const auto expected = lvm::compute_spectrum(compact, 0);
    require(expected.ok, "selected-range FFT oracle");
    for (bool light : {false, true}) for (bool stitched : {false, true}) {
        reset_document({"A", "B"}, time, values);
        g.light_mode = light; g.stitch_time_gaps = stitched; g.visible = {0, 1};
        const double start = (time[lo - 1] + time[lo]) / 2;
        const double end = (time[hi - 1] + time[hi]) / 2;
        set_fft_window(start, end);
        set_mode(AnalysisMode::FFT);
        require(g.spec_valid && g.spec.n == int(hi - lo) && g.spec_source_from_selection, "GUI FFT uses the entire selected range in every display mode");
        near(g.spec.source_start, time[lo], "selected FFT first included timestamp");
        near(g.spec.source_end, time[hi - 1], "selected FFT last included timestamp");
        require(g.spec.freqs == expected.freqs && g.spec.amp.back() == expected.amp[1], "selected GUI FFT matches all compact reference bins");
        require(g.spec.source_channels.back() == 1, "selected FFT keeps original visible channel identity");
        ExportOptions opts; opts.include_hidden_channels = true; opts.apply_processing_to_data = false;
        lvm::Spectrum exported; std::vector<int> channels; bool all = false;
        require(build_export_spectrum(opts, exported, channels, all), "selected FFT export rebuilds all channels");
        require(exported.freqs == expected.freqs && exported.amp == expected.amp, "selected FFT export uses every sample and excludes outside impulses");
    }
}

void stitched_gap_regressions() {
    std::vector<double> time(11001), values(11001);
    for (std::size_t i = 0; i < time.size(); ++i) {
        time[i] = i ? time[i - 1] + (i > 1000 ? 1025.0 : 1.0) / 1024 : 0;
        values[i] = std::sin(i);
    }
    reset_document({"A"}, time, {values});
    g.stitch_time_gaps = true;
    near(stitched_time_at_index(11000), 11000.0 / 1024, "graph compresses 10000 gaps even when they are the majority");
    bool invertible = true;
    for (std::size_t i = 0; i < time.size(); i += 113) {
        invertible = invertible && std::fabs(raw_time_from_stitched(i / 1024.0) - time[i]) < 1e-9;
    }
    require(invertible, "graph selection maps compact positions back across many gaps");
    reset_document({"A"}, {0, 1, 2, 1e100}, {{0, 1, 2, 3}});
    g.stitch_time_gaps = true;
    near(stitched_time_at_index(3), 3, "huge gap does not collapse the graph axis");
    near(raw_time_from_stitched(1.5), 1.5, "huge final gap cannot move a selection in the initial segment");
    reset_document({"A"}, {0, 1, 2, 4, 5, 6}, {{0, 1, 2, 3, 4, 5}});
    g.stitch_time_gaps = true;
    near(stitched_time_at_index(5), 5, "graph also compresses a single missing sample");
}

void light_mode_fft_visibility() {
    std::vector<double> time(8192);
    std::vector<std::vector<double>> values(3, std::vector<double>(time.size()));
    for (std::size_t i = 0; i < time.size(); ++i) {
        time[i] = i / 1024.0;
        for (std::size_t c = 0; c < values.size(); ++c) values[c][i] = std::sin(i * (0.1 + c * 0.2));
    }
    reset_document({"A", "B", "C"}, time, values);
    g.light_mode = true; g.visible = {1, 1, 0}; g.mode = AnalysisMode::FFT;
    // A message-only window exercises the production asynchronous branch
    // without showing any UI. Earlier GUI tests used only its synchronous path.
    struct TestWindow {
        HWND handle = CreateWindowExW(0, L"STATIC", L"FFT regression", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~TestWindow() { g_spectrum_worker.cancel(); g.main = nullptr; if (handle) DestroyWindow(handle); }
    } window;
    require(window.handle != nullptr, "message-only FFT test window");
    g.main = window.handle;
    const auto finish = [] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        std::optional<lvm::SpectrumWorker::Result> result;
        while (!result && std::chrono::steady_clock::now() < deadline) {
            result = g_spectrum_worker.take_result();
            if (!result) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        require(result && result->generation == g.spec_generation, "production worker returns the current generation");
        apply_spectrum_result(std::move(result->spectrum));
        require(g.spec_valid && !g.spec_pending, "production FFT leaves loading state only after applying its result");
    };
    compute_spectrum_for_window(time.front(), time.back(), true);
    require(g.spec_pending && !g.spec_valid, "initial LM FFT enters asynchronous loading state");
    const auto initial_generation = g.spec_generation;
    for (const auto& visible : std::vector<std::vector<char>>{{0,1,0}, {0,0,0}, {1,1,0}}) {
        g.visible = visible;
        require(!ensure_current_spectrum() && g.spec_pending, "visibility toggles keep pending FFT in loading state");
        require(g.spec_generation == initial_generation, "hiding or restoring requested channels does not restart pending FFT");
    }
    finish();
    require(g.spec.source_channels == std::vector<std::size_t>{0,1}, "LM calculates only requested channels");
    const double* cached_amplitudes = g.spec.amp[1].data();
    for (const auto& visible : std::vector<std::vector<char>>{{0,1,0}, {0,0,0}, {1,1,0}}) {
        g.visible = visible;
        require(ensure_current_spectrum() && !g.spec_pending, "hiding and restoring cached channels is immediate");
        require(g.spec_generation == initial_generation && g.spec.amp[1].data() == cached_amplitudes,
                "visibility preserves existing FFT buffers without recomputation");
    }
    g.visible = {0,1,1};
    require(!ensure_current_spectrum() && g.spec_pending && g.spec_generation > initial_generation,
            "showing an uncomputed channel schedules background FFT");
    finish();
    require(g.spec.source_channels == std::vector<std::size_t>{1,2}, "new LM request respects current visible channels");
    const auto prior_window = g.spec_generation;
    set_fft_window(time[100], time[2000]);
    compute_spectrum_for_window(time[100], time[2000], true);
    require(g.spec_pending && g.spec_generation > prior_window, "changing the source window invalidates cached spectra");
    finish();
    require(g.spec.n == 1901, "recomputed LM FFT uses the new source window");
    const auto prior_transform = g.spec_generation;
    g.global_formula = L"2*x"; rebuild_formula_cache_from_state();
    on_signal_transform_changed();
    require(g.spec_pending && g.spec_generation > prior_transform, "changing processing invalidates cached spectra");
    finish();
    const auto cached_generation=g.spec_generation;
    g.mode=AnalysisMode::Time;
    set_mode(AnalysisMode::FFT);
    require(g.spec_generation==cached_generation && !g.spec_pending,
            "returning to the FFT tab reuses an unchanged completed cache");
}

void routed_window_messages() {
    reset_document({"A", "B"}, {0,1,2,3}, {{1,2,3,4},{4,3,2,1}});
    struct TestWindow {
        HWND handle = CreateWindowExW(0, L"STATIC", L"Message routing regression", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~TestWindow() { g.main = nullptr; if (handle) DestroyWindow(handle); }
    } window;
    require(window.handle != nullptr, "message routing test window");
    g.main = window.handle;
    WndProc(g.main, WM_COMMAND, IDC_HIDE_ALL, 0);
    require(g.visible == std::vector<char>{0,0} && g_undo.size() == 1,
            "command routing hides channels and records undo");
    WndProc(g.main, WM_COMMAND, IDC_SHOW_ALL, 0);
    require(g.visible == std::vector<char>{1,1} && g_undo.size() == 2,
            "command routing restores channel visibility");
    WndProc(g.main, WM_COMMAND, IDM_ADD_MARKER, 0);
    require(g.pending_marker, "command routing arms marker placement");
    WndProc(g.main, WM_KEYDOWN, VK_ESCAPE, 0);
    require(!g.pending_marker, "input routing cancels pending marker placement");
    for (int command : {IDC_MEASURE, IDM_ADD_MARKER, IDM_ADD_VLINE, IDM_ADD_HLINE}) {
        WndProc(g.main, WM_COMMAND, command, 0);
        WndProc(g.main, WM_COMMAND, IDC_CURSOR_TOOL, 0);
        require(!g.measure_mode && !g.pending_marker && g.pending_line == 0,
                "cursor tool cancels every annotation placement mode");
    }
    MINMAXINFO limits{};
    WndProc(g.main, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
    require(limits.ptMinTrackSize.x == 980 && limits.ptMinTrackSize.y == 560,
            "window routing preserves minimum window size");
    require(WndProc(g.main, WM_ERASEBKGND, 0, 0) == 1, "window routing preserves background erase handling");
    g.async_load_token = 17;
    g.async_load_stage = AsyncLoadStage::LoadingFile;
    auto result = std::make_unique<AsyncLoadResult>();
    result->token = 17; result->cancelled = true;
    WndProc(g.main, WM_APP_ASYNC_LOAD_DONE, 0, reinterpret_cast<LPARAM>(result.release()));
    require(g.async_load_stage == AsyncLoadStage::None && g.ds.rows() == 4,
            "loading routing consumes cancelled results without replacing the document");
}

void light_mode_and_history() {
    reset_document({"gap"}, {0, 1, 2, 102, 103, 104}, {{0, 1, 2, 3, 4, 5}});
    g.stitch_time_gaps = true;
    invalidate_stitched_time_cache();
    near(stitched_time_at_index(0), 0, "stitched graph preserves first timestamp");
    near(stitched_time_from_raw(103), 4, "raw time maps to stitched graph axis");
    near(raw_time_from_stitched(4), 103, "stitched graph coordinate maps back to raw time");
    g.stitch_time_gaps = false;
    near(stitched_time_at_index(3), 102, "disabling stitched graph restores the original time axis");

    std::vector<double> time(300001), values(time.size(), 0);
    for (std::size_t i = 0; i < time.size(); ++i) time[i] = i * 0.001;
    values[11] = 100;
    reset_document({"impulse"}, time, {values});
    g.light_mode = true;
    double low = 0, high = 0;
    require(current_time_yrange_window(0, time.size(), low, high) && high >= 100, "Light Mode auto Y includes a single-sample impulse");
    g.global_formula = L"2*x"; rebuild_formula_cache_from_state(); recompute_transforms_from_state();
    require(current_time_yrange_window(0, time.size(), low, high) && high >= 200, "envelope invalidates after formula changes");
    require(current_time_yrange_window(12, 100, low, high) && high < 10, "envelope query excludes an impulse outside the window");

    reset_document({"signal"}, {0,.001,.002,.003}, {{1,0,-1,0}});
    g.noise_threshold_enabled = true; g.noise_threshold_min = 10; g.noise_threshold_max = 400;
    PointGroup group; group.name = L"measurements"; group.points = {{0,1}};
    g.point_groups.push_back(group); g.active_point_group = g.time_active_point_group = 0;
    const auto serial = g.plot_analysis_serial;
    for (int position = 400; position < 600; ++position) apply_filter_slider_change(true, position, true);
    near(g.noise_threshold_min, 10, "drag previews do not change active filter");
    require(g_undo.empty() && g.plot_analysis_serial == serial, "drag previews avoid repeated FFT/filter invalidation and history copies");
    apply_filter_slider_change(true, 599, false);
    const double applied = g.noise_threshold_min;
    require(g_undo.size() == 1 && g.point_groups.empty(), "one filter drag creates one undo action and clears obsolete measurements");
    apply_filter_slider_change(true, 599, false);
    require(g_undo.size() == 1, "track end notification does not duplicate undo");
    pop_undo();
    near(g.noise_threshold_min, 10, "undo restores original cutoff");
    require(g.point_groups.size() == 1 && g.point_groups[0].points.size() == 1, "undo restores measurements removed by the filter change");
    pop_redo();
    near(g.noise_threshold_min, applied, "redo restores applied cutoff");
    require(g.point_groups.empty(), "redo clears obsolete measurements again");
    ensure_filtered_channel_cache(0);
    require(!g.filtered_channel_cache[0].empty(), "enabled filter produces a cache");
    g.noise_threshold_enabled = false; invalidate_filtered_channel_cache();
    require(g.filtered_channel_cache[0].capacity() == 0, "disabling filter releases stored filtered samples");

    g_undo.clear(); g_redo.clear();
    for (std::size_t i = 0; i < kUndoActionLimit + 20; ++i) {
        UndoAction action; action.type = UndoAction::ADD_LINE; action.line.value = static_cast<double>(i);
        push_undo(std::move(action));
    }
    require(g_undo.size() == kUndoActionLimit && g_undo.front().line.value == 20, "history discards oldest actions at the count limit");
    g_undo.clear();
    for (int i = 0; i < 10; ++i) {
        UndoAction action; action.type = UndoAction::CLEAR_POINTS;
        action.saved_point_groups.resize(1);
        action.saved_point_groups[0].points.resize(kUndoByteLimit / 8 / sizeof(std::pair<double,double>));
        push_undo(std::move(action));
    }
    require(history_stack_bytes(g_undo) <= kUndoByteLimit && g_undo.size() < 10, "history obeys memory limit for large measurement groups");
    g_undo.clear();
}

void frf_integration() {
    std::vector<double> t, x, y;
    for (int i=0;i<1024;++i) {
        t.push_back(i/1024.0);
        x.push_back(std::sin(2*std::acos(-1.0)*16*i/1024));
        y.push_back((i<512 ? 2 : 4)*x.back());
    }
    reset_document({"Input, special", "Output"}, t, {x,y});
    require(g.frf.inputs.empty() && g.frf.outputs.empty(), "FRF starts without preselected channel roles");
    require(set_frf_channels({0},{1}), "FRF accepts the first selected support and response");
    g.frf.options.estimator=lvm::FrfEstimator::Direct;
    set_fft_window(t[0],t[511]);
    g.visible={0,0};
    set_mode(AnalysisMode::FRF);
    require(g.mode==AnalysisMode::FRF && g.frf.result.ok, "FRF is a third mode independent of channel visibility");
    require(g.frf.result.common().sample_count==512 && g.frf.from_selection, "FRF uses one shared selected interval");
    near(lvm::frf_dynamic_coefficient(g.frf.result.common(),8),2,"selected FRF excludes the different response outside the selection");
    require(g.frf.result.common().valid[8] && !g.frf.result.common().valid[80], "weak input bins are masked in GUI result");
    require(g.frf.result.common().reference_amplitude_valid[8] &&
            g.frf.result.common().reference_amplitude[8]>0,"GUI receives the averaged Reference amplitude graph");
    require(set_frf_frequency_range(4,64),"valid FRF frequency limits accepted");
    near(frf_frequency_at_fraction(.5),16,"log-frequency midpoint is geometric");
    near(frf_frequency_fraction(16),.5,"frequency-to-pixel inverse matches log mapping");
    g.frf.logarithmic_frequency_axis=false;
    sync_frf_frequency_limits();
    near(frf_frequency_at_fraction(.5),34,"linear-frequency midpoint is arithmetic");
    near(frf_frequency_fraction(34),.5,"frequency-to-pixel inverse matches linear mapping");
    zoom_at(.5,.5);
    near(frf_frequency_at_fraction(.5),34,"linear FRF zoom preserves frequency at cursor");
    g.frf.logarithmic_frequency_axis=true;
    sync_frf_frequency_limits();
    require(set_frf_frequency_range(4,64),"restore logarithmic range after linear navigation");
    const auto generation=g.frf.generation;
    const auto transfer=g.frf.result.common().transfer;
    zoom_at(.5,.5);
    near(frf_frequency_at_fraction(.5),16,"FRF zoom preserves frequency at cursor");
    require(g.frf.generation==generation && g.frf.result.common().transfer==transfer,"view zoom does not recalculate FRF");
    require(!set_frf_frequency_range(0,64) && !set_frf_frequency_range(64,4) && !set_frf_frequency_range(1,1000),
            "invalid logarithmic ranges rejected");
    require(set_frf_frequency_range(4,64),"restore test export range");
    const auto path=test_dir / std::filesystem::u8path(u8"ачх_測定.csv");
    require(save_frf_csv(path.wstring()),"FRF CSV supports Unicode paths");
    ExportOptions frf_options; frf_options.format=ExportFileFormat::Csv;
    require(save_tabular_export((test_dir/"frf_generic.csv").wstring(),frf_options),
            "generic CSV exporter dispatches to FRF in the third mode");
    require(!save_lvm_export((test_dir/"invalid_frf.lvm").wstring(),frf_options),
            "time-domain LVM export is unavailable in FRF");
    std::ifstream input(path);
    std::string text((std::istreambuf_iterator<char>(input)),{});
    require(text.find("data_kind=frf")!=std::string::npos && text.find("window=hann_periodic")!=std::string::npos &&
            text.find("frequency_hz,dynamic_coefficient,h_real,h_imag,valid")!=std::string::npos,
            "FRF CSV contains schema, window, complex transfer, and validity");
    require(text.find("input_name=Input%2C special")!=std::string::npos &&
            text.find(",,,0")!=std::string::npos,"FRF CSV escapes labels and leaves invalid ratios empty");
    require(!lvm::read_lvm_file(path).ok,"FRF CSV cannot silently reopen as a time signal");
    g.frf.pending=true;
    require(!save_frf_csv(path.wstring()),"pending FRF export rejected");
    std::ifstream preserved(path);
    require(std::string((std::istreambuf_iterator<char>(preserved)),{})==text,"failed FRF export preserves existing file");
    g.frf.pending=false;
    const double view_start=g.frf.log_start, view_end=g.frf.log_end;
    set_mode(AnalysisMode::Time);
    require(g.win_start==t.front() && g.win_end==t.back(),"FRF navigation leaves time view unchanged");
    set_mode(AnalysisMode::FRF);
    near(g.frf.log_start,view_start,"FRF view preserved on mode switch");
    near(g.frf.log_end,view_end,"FRF frequency limits preserved on return");
    g.pending_marker=false;
    WndProc(nullptr,WM_COMMAND,IDM_ADD_MARKER,0);
    require(g.pending_marker && frf_command_supported(IDM_ADD_MARKER),"FRF accepts the common marker tool");
    g.pending_marker=false;
    WndProc(nullptr,WM_COMMAND,IDM_ADD_VLINE,0);
    require(g.pending_line==1 && frf_command_supported(IDC_MEASURE),"FRF reuses the standard point and line tools");
    add_guide_line(true,16);
    add_guide_line(false,2);
    require(g.guides.size()>=2 && g.guides[g.guides.size()-2].mode==AnalysisMode::FRF &&
            g.guides.back().mode==AnalysisMode::FRF,"FRF lines use the common annotation collection");
    clear_fft_window();
    g.win_start=t[512]; g.win_end=t.back();
    ensure_current_frf();
    near(lvm::frf_dynamic_coefficient(g.frf.result.common(),8),4,"FRF visible-range fallback updates both channels");
    g.frf.apply_processing=true;
    g.channel_formulas[1]=L"3*x"; rebuild_formula_cache_from_state();
    on_frf_processing_changed();
    near(lvm::frf_dynamic_coefficient(g.frf.result.common(),8),12,"FRF can explicitly use channel processing");
    g.frf.apply_processing=false; invalidate_frf(); ensure_current_frf();
    near(lvm::frf_dynamic_coefficient(g.frf.result.common(),8),4,"raw FRF ignores display processing");
    {
        Gdiplus::GdiplusStartupInput startup;
        ULONG_PTR token=0;
        require(Gdiplus::GdiplusStartup(&token,&startup,nullptr)==Gdiplus::Ok,"GDI+ starts for FRF PNG test");
        struct Window {
            HWND hwnd=CreateWindowExW(0,L"STATIC",L"FRF rendering test",WS_POPUP|WS_CLIPCHILDREN,
                0,0,980,500,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            ~Window() { g.main=nullptr; if(hwnd) DestroyWindow(hwnd); }
        } window;
        require(window.hwnd!=nullptr,"hidden FRF rendering window");
        g.main=window.hwnd;
        g.ui_font=reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        update_theme_brushes();
        create_frf_panel(g.main,GetModuleHandleW(nullptr));
        layout();
        require(g.frf_panel!=nullptr,"FRF panel is embedded in the existing main window");
        double frf_low=0, frf_high=0; frf_y_range(frf_low,frf_high);
        const RECT chart=plot_rect();
        g.vvalid=true; g.vrect=chart; g.vx0=g.frf.log_start; g.vx1=g.frf.log_end;
        g.vy0=frf_low; g.vy1=frf_high;
        const int point_x=(chart.left+chart.right)/2, point_y=(chart.top+chart.bottom)/2;
        WndProc(g.main,WM_COMMAND,IDM_ADD_MARKER,0);
        require(g.pending_marker,"FRF marker command remains armed in the rendered window");
        handle_frf_input(g.main,WM_LBUTTONDOWN,0,MAKELPARAM(point_x,point_y));
        require(!g.markers.empty() && g.markers.back().mode==AnalysisMode::FRF && !g.pending_marker,
                "FRF marker uses frequency and dynamic-coefficient coordinates");
        WndProc(g.main,WM_COMMAND,IDC_MEASURE,0);
        handle_frf_input(g.main,WM_LBUTTONDOWN,0,MAKELPARAM(point_x,point_y));
        require(!g.point_groups.empty() && g.point_groups.back().mode==PointGroupMode::FRF &&
                !g.point_groups.back().points.empty(),"FRF measurement point uses its dedicated point group");
        const double snapped_frequency=g.point_groups.back().points.back().first;
        require(std::find(g.frf.result.common().frequencies.begin(),g.frf.result.common().frequencies.end(),snapped_frequency) !=
                g.frf.result.common().frequencies.end(),"FRF point snapping selects an actual response-frequency bin");
        g.measure_mode=false;
        wchar_t input_text[128]{};
        GetWindowTextW(GetDlgItem(g.frf_panel,7101),input_text,128);
        require(std::wstring(input_text).find(L"Input, special")!=std::wstring::npos,
                "FRF Input button displays the selected channel");
        SendMessageW(g.frf_panel,WM_COMMAND,MAKEWPARAM(7101,BN_CLICKED),0);
        HWND role_menu=FindWindowW(L"AMSignalFrfRoleMenu",nullptr);
        require(role_menu!=nullptr && IsWindowVisible(role_menu),"FRF roles open in one persistent menu");
        SendMessageW(role_menu,WM_LBUTTONUP,0,MAKELPARAM(10,GetSystemMetrics(SM_CYMENU)+3));
        require(IsWindow(role_menu) && IsWindow(GetDlgItem(g.frf_panel,7111)) &&
                g.frf.inputs==std::vector<int>{0},
            "FRF role menu remains open and leaves panel controls visible after a choice");
        SendMessageW(g.frf_panel,WM_COMMAND,MAKEWPARAM(7101,BN_CLICKED),0);
        require(!IsWindow(role_menu),"the same FRF role control closes its menu");
        g_theme=&kDarkTheme;
        update_theme_brushes();
        SendMessageW(g.frf_panel,WM_COMMAND,MAKEWPARAM(7101,BN_CLICKED),0);
        role_menu=FindWindowW(L"AMSignalFrfRoleMenu",nullptr);
        require(role_menu!=nullptr && IsWindowVisible(role_menu),"FRF role menu opens in the dark theme");
        SendMessageW(role_menu,WM_PAINT,0,0);
        SendMessageW(g.frf_panel,WM_COMMAND,MAKEWPARAM(7101,BN_CLICKED),0);
        require(!IsWindow(role_menu),"the dark FRF role menu also closes on a repeated control click");
        g_theme=&kLightTheme;
        update_theme_brushes();
        wchar_t range_text[80]{};
        require(GetWindowTextW(GetDlgItem(g.frf_panel,7107),range_text,80)>0,"FRF range edit displays calculated limit");
        RECT panel; GetClientRect(g.frf_panel,&panel);
        require(panel.right==kRightPanel && panel.bottom>=354,"FRF controls fit below the FRF tab strip");
        SendMessageW(GetDlgItem(g.frf_panel,7122),CB_SETCURSEL,0,0);
        SendMessageW(g.frf_panel,WM_COMMAND,MAKEWPARAM(7122,CBN_SELCHANGE),0);
        require(g.frf.display_smoothing_octaves==0,"FRF display smoothing can be disabled without recalculation");
        SendMessageW(GetDlgItem(g.frf_panel,7122),CB_SETCURSEL,2,0);
        SendMessageW(g.frf_panel,WM_COMMAND,MAKEWPARAM(7122,CBN_SELCHANGE),0);
        require(std::abs(g.frf.display_smoothing_octaves-1.0/12.0)<1e-12,"FRF display smoothing can select one twelfth octave");
        const auto png=test_dir / "frf_plot.png";
        require(save_png(png.wstring()),"FRF saves graph through the existing PNG exporter");
        {
            Gdiplus::Bitmap bitmap(png.c_str());
            require(bitmap.GetLastStatus()==Gdiplus::Ok && bitmap.GetWidth()>=400 && bitmap.GetHeight()>=240,
                    "exported FRF PNG is a readable image");
        }
        // Render the real controls into an artifact for layout inspection.
        HDC screen=GetDC(g.main), dc=CreateCompatibleDC(screen);
        HBITMAP bmp=CreateCompatibleBitmap(screen,panel.right,panel.bottom);
        HGDIOBJ previous=SelectObject(dc,bmp);
        FillRect(dc,&panel,g_panel_brush);
        SendMessageW(g.frf_panel,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
        SelectObject(dc,previous);
        {
            Gdiplus::Bitmap image(bmp,nullptr); CLSID encoder;
            require(png_encoder_clsid(&encoder)>=0 &&
                image.Save((test_dir/"frf_panel.png").c_str(),&encoder,nullptr)==Gdiplus::Ok,"FRF control layout artifact");
        }
        DeleteObject(bmp); DeleteDC(dc); ReleaseDC(g.main,screen);
        // A control change schedules a new pair; polling accepts only its generation.
        require(!set_frf_channels({1},{1}) && g.frf.result.ok,"same-channel choice is rejected without discarding valid results");
        require(set_frf_channels({1},{0}) && set_frf_channels({0},{1}),"valid pair changes are accepted");
        require(g.frf.pending,"valid channel change runs FRF in the background");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(g.frf.pending && std::chrono::steady_clock::now()<deadline) {
            WndProc(g.main,WM_TIMER,2,0);
            if(g.frf.pending) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        require(g.frf.result.ok && !g.frf.pending,"main-window timer accepts the FRF worker result");
        SetWindowTextW(GetDlgItem(g.frf_panel,7120),L"128");
        SendMessageW(GetDlgItem(g.frf_panel,7118),CB_SETCURSEL,0,0);
        SendMessageW(g.frf_panel,WM_COMMAND,MAKEWPARAM(7118,CBN_SELCHANGE),0);
        const auto h1_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(g.frf.pending && std::chrono::steady_clock::now()<h1_deadline) {
            poll_frf_result();
            if(g.frf.pending) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        require(g.frf.result.ok && g.frf.result.options.estimator==lvm::FrfEstimator::H1 &&
                g.frf.result.common().segment_length==128 && g.frf.result.common().averages==7 && g.frf.result.common().overlap_samples==64,
                "panel switches to H1 with explicit L and recalculates actual averaging count");
        near(g.frf.result.common().coherence[2],1,"GUI H1 exposes coherence from the same FFT averages");
        near(lvm::frf_dynamic_coefficient(g.frf.result.common(),2),4,"H1 dynamic coefficient preserves known gain");
        wchar_t details[256]{};
        GetWindowTextW(GetDlgItem(g.frf_panel,7111),details,256);
        const std::wstring actual=details;
        require(actual.find(L"L=128")!=std::wstring::npos && actual.find(L"K=7")!=std::wstring::npos &&
                actual.find(L"Δf=8")!=std::wstring::npos && actual.find(L"overlap=50%")!=std::wstring::npos,
                "FRF panel reports actual L, K, delta f and overlap");
        const auto h1_csv=test_dir/"frf_h1.csv";
        require(save_frf_csv(h1_csv.wstring()),"H1 CSV saves");
        std::ifstream h1_file(h1_csv);
        const std::string h1_text((std::istreambuf_iterator<char>(h1_file)),{});
        require(h1_text.find("estimator=h1_welch")!=std::string::npos &&
                h1_text.find("segment_length=128")!=std::string::npos && h1_text.find("averages=7")!=std::string::npos &&
                h1_text.find("valid,coherence,coherence_valid")!=std::string::npos &&
                h1_text.find("dynamic_coefficient_scale=linear_abs_h")!=std::string::npos &&
                h1_text.find("reference_amplitude_scale=linear_one_sided")!=std::string::npos,
                "H1 CSV records estimator, actual parameters, linear KD, Reference amplitude, and coherence");
        require(save_png((test_dir/"frf_h1.png").wstring()),"H1 graph reuses PNG export");
        Gdiplus::GdiplusShutdown(token);
    }
    g.ds.frequency_axis=true;
    set_mode(AnalysisMode::FFT);
    set_mode(AnalysisMode::FRF);
    require(g.mode==AnalysisMode::FFT,"stored magnitude spectrum cannot enter FRF");
}

void frf_multi_channels() {
    std::vector<double> time,x,ref2,y1,y2,y3;
    for(int i=0;i<2048;++i) {
        time.push_back(i/1024.0+(i>=1024 ? 10 : 0));
        x.push_back(std::sin(2*std::acos(-1.0)*16*i/1024));
        ref2.push_back(3*x.back());
        y1.push_back((i>=512 && i<1536 ? 6 : 100)*x.back());
        y2.push_back(-2*x.back()); y3.push_back(10*x.back());
    }
    reset_document({"R1","R2","Y, one","Y two","Y three"},time,{x,ref2,y1,y2,y3});
    set_fft_window(time[512],time[1535]);
    g.visible={0,0,0,0,0};
    require(set_frf_channels({0,1},{2,3,4}),"multi Reference and Response selected independently of visibility");
    g.frf.options.segment_length=256;
    set_mode(AnalysisMode::FRF);
    require(g.frf.result.ok && g.frf.result.responses.size()==3 && g.frf.result.common().sample_count==1024,
            "all responses share the same selected rows");
    require(g.frf.result.common().gaps_ignored && g.frf.result.common().averages==7,"multi-channel batch preserves gaps ignored and Welch parameters");
    near(lvm::frf_dynamic_coefficient(g.frf.result.responses[0],4),3,"GUI averages Reference samples before H1");
    near(lvm::frf_dynamic_coefficient(g.frf.result.responses[1],4),1,"second GUI response has independent gain");
    near(lvm::frf_dynamic_coefficient(g.frf.result.responses[2],4),5,"third GUI response has independent gain");
    require(frf_curve_label(0)==L"Y, one / AVG(R1, R2)" && frf_curve_label(2)==L"Y three / AVG(R1, R2)","legend labels identify averaged reference and each response");
    const auto generation=g.frf.generation;
    require(!set_frf_channels({0,1},{1,2}) && !set_frf_channels({},{2}) && !set_frf_channels({0,0},{2}) &&
            !set_frf_channels({0},{9}) && g.frf.generation==generation,"invalid roles, empty lists, duplicates and indices cannot change selection");
    g.frf.apply_processing=true; g.channel_formulas[0]=L"3*x"; rebuild_formula_cache_from_state(); on_frf_processing_changed();
    near(lvm::frf_dynamic_coefficient(g.frf.result.responses[0],4),2,"each reference is processed before averaging");
    g.frf.apply_processing=false; invalidate_frf(); ensure_current_frf();
    require(set_frf_frequency_range(4,64),"multi-FRF has one frequency range");
    const auto csv_path=test_dir/"frf_multi.csv";
    require(save_frf_csv(csv_path.wstring()),"multi-FRF CSV export");
    std::ifstream csv(csv_path); const std::string csv_text((std::istreambuf_iterator<char>(csv)),{});
    require(csv_text.find("reference_average=arithmetic_samples")!=std::string::npos &&
            csv_text.find("reference_1_index=1")!=std::string::npos && csv_text.find("response_2_index=4")!=std::string::npos &&
            csv_text.find("response_index,response_name,frequency_hz")!=std::string::npos &&
            csv_text.find("2,\"Y, one\",")!=std::string::npos && csv_text.find("4,\"Y three\",")!=std::string::npos,
            "CSV contains all series and complete reference membership with quoted names");
    double low,high; frf_y_range(low,high);
    require(low==0 && high>5,"Auto Y starts at zero and includes every linear dynamic-coefficient curve");
    const RECT full_plot{70,72,900,560};
    const RECT coefficient_plot=frf_coefficient_plot_rect(full_plot);
    const RECT reference_plot=frf_reference_plot_rect(full_plot);
    require(coefficient_plot.bottom<reference_plot.top && reference_plot.bottom==full_plot.bottom,
            "averaged Reference uses a separate graph below KD");
    const double reference_auto_max=frf_reference_y_max();
    g.frf.reference_auto_y=false; g.frf.reference_y_max=reference_auto_max*.5;
    near(frf_reference_y_max(),reference_auto_max*.5,"Reference graph has an independent vertical scale");
    g.frf.show_reference_amplitude=false;
    require(frf_coefficient_plot_rect(full_plot).bottom==full_plot.bottom &&
            frf_reference_plot_rect(full_plot).top==full_plot.bottom,
            "Reference graph can be hidden without changing KD data");
    g.frf.show_reference_amplitude=true; g.frf.reference_auto_y=true;
    const SettingsSnapshot before_symbols=capture_settings_snapshot();
    g.distinguish_curves=true;
    require(record_settings_change(before_symbols),"grayscale curve symbols participate in settings history");
    pop_undo();
    require(!g.distinguish_curves,"curve-symbol mode is undoable");
    pop_redo();
    require(g.distinguish_curves,"curve-symbol mode is redoable");
    Gdiplus::GdiplusStartupInput startup; ULONG_PTR token=0;
    require(Gdiplus::GdiplusStartup(&token,&startup,nullptr)==Gdiplus::Ok,"multi-FRF PNG startup");
    struct Window {
        HWND hwnd=CreateWindowExW(0,L"STATIC",L"Multi FRF",WS_POPUP|WS_CLIPCHILDREN,0,0,1100,650,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        ~Window() { g.main=nullptr; if(hwnd) DestroyWindow(hwnd); }
    } window;
    require(window.hwnd!=nullptr,"multi-FRF hidden rendering window");
    g.main=window.hwnd; g.ui_font=reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)); update_theme_brushes();
    create_frf_panel(g.main,GetModuleHandleW(nullptr)); layout();
    require(save_png((test_dir/"frf_multi.png").wstring()),"all FRF curves export to one PNG");
    require(g_legend_items.empty(),"FRF plot omits the overlay legend");
    require(channel_color(2)!=channel_color(3) && channel_color(3)!=channel_color(4),"responses have distinct colors");
    Gdiplus::GdiplusShutdown(token);
    g.main=nullptr;
    g.ds.channels[2][700]=std::nan(""); invalidate_frf(); ensure_current_frf();
    require(g.frf.result.ok && !g.frf.result.responses[0].ok && g.frf.result.responses[1].ok,
            "one failed response does not hide the other GUI curves");
    require(frf_status_text().find(L"Y, one")!=std::wstring::npos,"partial batch error identifies the failed response");
    require(save_frf_csv((test_dir/"frf_multi_partial.csv").wstring()),"partial batch exports valid curves and error metadata");
}

void frf_gap_stitching() {
    std::vector<double> t, x, y;
    for (int i=0;i<1024;++i) {
        t.push_back(i/1024.0 + (i>=512 ? 10 : 0));
        x.push_back(std::sin(2*std::acos(-1.0)*16*i/1024));
        y.push_back(2*x.back());
    }
    reset_document({"Input","Output"},t,{x,y});
    require(set_frf_channels({0},{1}),"FRF gap test selects both channel roles");
    set_fft_window(t[256],t[767]);
    g.frf.options.segment_length=512;
    set_mode(AnalysisMode::FRF);
    require(g.frf.result.ok && g.frf.result.common().gaps_ignored && !g.stitch_time_gaps,"FRF ignores gaps even when display stitching is disabled");
    require(frf_status_text().find(L"Gaps ignored")!=std::wstring::npos,"FRF status warns about ignored gaps");
    const auto generation=g.frf.generation;
    const auto transfer=g.frf.result.common().transfer;
    struct Window {
        HWND hwnd=CreateWindowExW(0,L"STATIC",L"Settings test",WS_POPUP,0,0,400,400,
                                  nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        ~Window() { g_frf_worker.cancel(); g.main=nullptr; if(hwnd) DestroyWindow(hwnd); }
    } window;
    require(window.hwnd!=nullptr,"hidden settings test window");
    g.main=window.hwnd;
    HWND toggle=CreateWindowExW(0,L"BUTTON",L"Stitch",WS_CHILD|BS_OWNERDRAW,0,0,100,25,
        window.hwnd,reinterpret_cast<HMENU>(IDC_SET_STITCH_GAPS),GetModuleHandleW(nullptr),nullptr);
    require(toggle!=nullptr,"time stitching settings control");
    const auto click = [&] {
        SettingsProc(window.hwnd,WM_COMMAND,MAKEWPARAM(IDC_SET_STITCH_GAPS,BN_CLICKED),
                     reinterpret_cast<LPARAM>(toggle));
    };
    click();
    require(g.stitch_time_gaps && g.frf.result.ok && g.frf.result.common().gaps_ignored &&
            g.frf.result.common().sample_count==512,"display stitching toggle preserves the FRF pair");
    require(g.frf.result.common().segment_length==512 && g.frf.result.common().averages==1,"GUI H1 allows L larger than each 256-sample fragment");
    near(g.frf.result.common().frequencies[8],16,"FRF with ignored gaps retains physical frequency scale");
    near(lvm::frf_dynamic_coefficient(g.frf.result.common(),8),2,"FRF gain with ignored gaps");
    require(g.ds.time==t && g.frf.result.common().source_start==t[256] && g.frf.result.common().source_end==t[767],
            "GUI stitching preserves raw selection coordinates");
    const auto path=test_dir/"frf_stitched.csv";
    require(save_frf_csv(path.wstring()),"stitched FRF exports CSV");
    std::ifstream csv(path);
    const std::string text((std::istreambuf_iterator<char>(csv)),{});
    require(text.find("gap_policy=ignore_gaps_uniform_sample_sequence")!=std::string::npos && text.find("gaps_ignored=1")!=std::string::npos &&
            text.find("resampled=0")!=std::string::npos,"CSV records ignored gaps without interpolation");
    click();
    require(!g.stitch_time_gaps && g.frf.result.ok && !g.frf.pending,"disabling display stitching keeps FRF available");
    click(); click();
    require(g.frf.generation==generation && g.frf.result.common().transfer==transfer,"display stitching does not recalculate or change FRF");
    g.main=nullptr;
    g.stitch_time_gaps=true;
    require(ensure_current_frf() && g.frf.generation==generation,"FRF cache is independent of display stitching");
    g.frf.options.segment_length=1024;
    require(!ensure_current_frf() && g.frf.result.error==lvm::FrfError::TooShort,"GUI reports insufficient selected samples for L");
}

void frf_loaded_document_defaults() {
    reset_document({"Old support","Old response"},{0,1,2,3},{{1,2,3,4},{4,3,2,1}});
    g.frf.inputs={0}; g.frf.outputs={1};
    lvm::Dataset loaded;
    loaded.ok=true;
    loaded.names={"Loaded support","Loaded response"};
    loaded.time={0,1,2,3}; loaded.raw_time=loaded.time;
    loaded.channels={{1,0,-1,0},{2,0,-2,0}};
    apply_loaded_dataset(std::move(loaded),L"loaded.lvm",false,false,0,false);
    require(g.frf.inputs.empty() && g.frf.outputs.empty(),
        "opening a file clears preselected FRF channel roles");
    require(set_frf_channels({0},{1}),"FRF clear test selects both roles");
    clear_frf_channel_selection(true);
    require(g.frf.inputs.empty() && g.frf.outputs==std::vector<int>{1},
        "FRF Clear removes only the selected role");
}

void reopen_spectrum() {
    const auto path = test_dir / "recovered_fft.csv";
    const auto original = lvm::read_lvm_file(path);
    require(original.ok && original.frequency_axis, "Frequency column identifies an exported spectrum");
    reopen(path, true);
    set_mode(AnalysisMode::FFT);
    require(g.spec_valid && g.spec.imported && g.spec.freqs == original.time && g.spec.amp == original.channels,
            "opening spectrum preserves all bins and amplitudes without a second FFT");
    near(g.spec.source_start, 0, "reopened spectrum preserves source interval");
    require(g.spec.resampled && g.spec.gaps_ignored, "reopened spectrum preserves sampling provenance");
    set_mode(AnalysisMode::Time);
    require((g.mode == AnalysisMode::FFT) && current_filter_nyquist() == 0, "stored spectrum cannot enter time mode or use a time-domain filter");
    ExportOptions opts; opts.include_hidden_channels = true; opts.selected_range = ExportRangeMode::Whole;
    const auto second_path = test_dir / "spectrum_second_roundtrip.csv";
    require(save_tabular_export(second_path.wstring(), opts), "stored spectrum exports again");
    const auto second = lvm::read_lvm_file(second_path);
    require(second.ok && second.frequency_axis && second.time == original.time && second.channels == original.channels,
            "repeated spectrum export preserves exact numeric values");
    reset_document({"signal"},{0,.1,.2,.3,.4,.5,.6,.7},{{1,0,-1,0,1,0,-1,0}});
    g.global_formula = L"3*x"; rebuild_formula_cache_from_state(); set_mode(AnalysisMode::FFT);
    opts.apply_processing_to_data = false;
    require(save_tabular_export(second_path.wstring(), opts), "raw spectrum export stores processing recipe");
    reopen(second_path); set_mode(AnalysisMode::FFT);
    require(g.spec_valid && g.spec.imported, "raw spectrum reopens directly");
    const auto peaks = lvm::find_peaks(g.spec.freqs, g.spec.amp[0], 1);
    require(!peaks.empty(), "reopened raw spectrum retains peak");
    near(peaks[0].amp, 1, "time-domain recipe is never reapplied to imported FFT amplitudes");
}

void save_hotkeys() {
    const auto bindings = default_hotkeys();
    auto find = [&](int command) -> const HotkeyBinding* {
        for (const auto& binding : bindings) if (binding.command == command) return &binding;
        return nullptr;
    };
    const auto* project = find(IDC_SAVE_PROJECT);
    const auto* save_as = find(IDC_SAVECSV);
    const auto* png = find(IDC_SAVEPNG);
    require(project && project->fvirt == (FVIRTKEY | FCONTROL) && project->key == 'S',
            "Ctrl+S saves the project");
    require(save_as && save_as->fvirt == (FVIRTKEY | FCONTROL | FSHIFT) && save_as->key == 'S',
            "Ctrl+Shift+S opens Save as");
    require(png && png->fvirt == (FVIRTKEY | FCONTROL | FALT) && png->key == 'S',
            "Ctrl+Alt+S saves PNG");
}

void channel_coefficient_fields() {
    reset_document({"A", "B"}, {0, 1}, {{1, 2}, {3, 4}});
    g.channel_formulas = {L"2.5*x", L"x+7"};
    rebuild_formula_cache_from_state();
    require(channel_coefficient_text(0) == L"2.5", "multiplier field shows the channel coefficient");
    require(channel_coefficient_text(1).empty(), "non-multiplicative legacy formula does not pretend to be a coefficient");
}

void point_display_defaults() {
    reset_document({"A"}, {0, 1}, {{1, 2}});
    g.pdisp = {false, true, false, true, false, true, true};
    clear_all_measure_point_groups();
    const int index = create_point_group(g.marker_color);
    require(index == 0, "first point group is created from point display defaults");
    const PointDisplay& display = g.point_groups[0].display;
    require(!display.number && display.x && !display.y && display.dx && !display.dy && display.inv_dt && display.dist,
            "first point group inherits display choices set before any point exists");
}

void multiple_open_documents() {
    reset_document({"first"}, {0, 1, 2}, {{1, 2, 3}});
    g.file_name = L"first.lvm";
    g.source_path = L"C:\\data\\first.lvm";
    g.win_start = .5; g.win_end = 1.5;
    g.mode = AnalysisMode::FFT;
    g.channel_colors = {RGB(1, 2, 3)};

    DocumentState second;
    second.ds.ok = true;
    second.ds.names = {"second"};
    second.ds.time = {10, 11, 12};
    second.ds.raw_time = second.ds.time;
    second.ds.channels = {{7, 8, 9}};
    second.visible = {1};
    second.channel_labels = {L"second"};
    second.channel_colors = {RGB(9, 8, 7)};
    second.data_t0 = second.win_start = 10;
    second.data_t1 = second.win_end = 12;
    second.file_name = L"second.lvm";
    second.source_path = L"C:\\data\\second.lvm";
    second.mode = AnalysisMode::FRF;
    g.inactive_documents.push_back(std::move(second));

    require(open_document_count() == 2, "two datasets are tracked as independent open documents");
    require(switch_to_document(1), "switches to an inactive document");
    require(g.file_name == L"second.lvm" && g.mode == AnalysisMode::FRF && g.win_start == 10,
            "switch restores the selected document's analysis state and time range");
    require(g.channel_colors == std::vector<COLORREF>{RGB(9, 8, 7)},
            "switch restores per-document channel presentation without duplicating samples");
    require(switch_to_document(1), "switches back to the original document");
    require(g.file_name == L"first.lvm" && g.mode == AnalysisMode::FFT && g.win_start == .5,
            "original signal and FFT state survive a document round trip");
    require(close_active_document() && open_document_count() == 1 && g.file_name == L"second.lvm",
            "closing one file keeps the remaining document open");
}
}

int main() {
    std::filesystem::create_directories(test_dir);
    try {
        document_history_and_save_state(); exports(); save_hotkeys(); channel_coefficient_fields(); point_display_defaults(); multiple_open_documents(); processing(); fft_recording_recovery();
        light_mode_and_history(); reopen_spectrum(); fft_selected_gap_range(); stitched_gap_regressions();
        light_mode_fft_visibility();
        routed_window_messages();
        frf_integration();
        frf_multi_channels();
        frf_gap_stitching();
        frf_loaded_document_defaults();
        std::cout << checks << " GUI integration checks passed\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << "FAIL after " << checks << " checks: " << ex.what() << '\n';
        return 1;
    }
}

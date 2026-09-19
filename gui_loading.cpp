// Loading: native viewer implementation.
#include "gui_loading.hpp"
#include "gui_menu.hpp"
#include "gui_frf.hpp"
#include "gui_analysis_source.hpp"
#include "gui_gap_details.hpp"
#include "gui_commands.hpp"
#include "gui_settings_window.hpp"
#include "gui_export_metadata.hpp"
#include "gui_dialogs.hpp"
#include "gui_documents.hpp"
#include "gui_ids.hpp"
#include "gui_layout.hpp"
#include "gui_loading_drop.hpp"
#include "gui_playback.hpp"
#include "gui_processing.hpp"
#include "gui_render.hpp"
#include "gui_render_data.hpp"
#include "gui_settings.hpp"
#include "gui_side_panel.hpp"
#include "gui_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_state_history.hpp"
#include "gui_status.hpp"
#include "gui_text.hpp"
#include "gui_theme.hpp"
#include "gui_time_axis.hpp"
#include "gui_welcome.hpp"
#include <shlobj.h>

namespace gui {

std::thread g_load_worker;

namespace {
constexpr wchar_t kProjectExtension[] = L".AMSig";
constexpr wchar_t kProjectClass[] = L"AMSignal.Project";

bool write_user_class_value(const std::wstring& key_name, const wchar_t* value_name,
                            const std::wstring& value) {
    HKEY key = nullptr;
    const std::wstring path = L"Software\\Classes\\" + key_name;
    const LONG created = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr,
                                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (created != ERROR_SUCCESS) return false;
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LONG written = RegSetValueExW(key, value_name, 0, REG_SZ,
                                        reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
    return written == ERROR_SUCCESS;
}
} // namespace

void register_project_file_association() {
    wchar_t executable[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return;
    const std::wstring exe_path(executable, length);
    const std::wstring command = L"\"" + exe_path + L"\" \"%1\"";
    const std::wstring icon = L"\"" + exe_path + L"\",0";
    if (write_user_class_value(kProjectExtension, nullptr, kProjectClass) &&
        write_user_class_value(kProjectClass, nullptr, L"AMSignal Project") &&
        write_user_class_value(std::wstring(kProjectClass) + L"\\DefaultIcon", nullptr, icon) &&
        write_user_class_value(std::wstring(kProjectClass) + L"\\shell\\open\\command", nullptr, command)) {
        // Explorer recognizes per-user associations without administrator rights.
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
}

template <typename TResult>
void post_async_result(HWND target, UINT message, std::unique_ptr<TResult> result) {
    TResult* raw = result.release();
    if (!raw) return;
    if (!target || !PostMessageW(target, message, 0, reinterpret_cast<LPARAM>(raw))) {
        delete raw;
    }
}

void request_async_load_cancel() {
    if (g.async_load_cancel_flag) {
        g.async_load_cancel_flag->store(true, std::memory_order_relaxed);
    }
    if (g_loading_cancel_btn && IsWindow(g_loading_cancel_btn)) {
        EnableWindow(g_loading_cancel_btn, FALSE);
    }
}

void apply_loaded_dataset(lvm::Dataset ds, const std::wstring& wpath, bool hide_channels,
                          bool requested_time_window, double cached_global_gap_step,
                          bool cached_global_gap_step_ready) {
    if (ds.time.empty()) {
        g.last_error = "No time data available.";
        MessageBoxW(g.main, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
        return;
    }

    begin_loaded_document();
    g.current_file_partial = requested_time_window || ds.partial;
    g_filter_slider_before.reset();
    g.ds = std::move(ds);
    invalidate_stitched_time_cache();
    g.envelopes.clear();
    g.envelope_serial.clear();
    g.visible.assign(g.ds.channel_count(), hide_channels ? 0 : 1);
    g.channel_labels.assign(g.ds.channel_count(), L"");
    for (std::size_t i = 0; i < g.ds.channel_count(); ++i) {
        if (i < g.ds.names.size() && !g.ds.names[i].empty()) {
            g.channel_labels[i] = to_w(g.ds.names[i]);
        } else {
            g.channel_labels[i] = std::wstring(L"Channel_") + std::to_wstring(i + 1);
        }
    }
    g.channel_colors.clear();
    g.channel_colors.reserve(g.ds.channel_count());
    for (std::size_t i = 0; i < g.ds.channel_count(); ++i) g.channel_colors.push_back(kPalette[i % (sizeof(kPalette) / sizeof(kPalette[0]))]);
    g.side_selected_channel = g.ds.channel_count() > 0 ? 0 : -1;
    g.side_scroll_y = 0;
    g.channel_formulas.assign(g.ds.channel_count(), default_channel_formula_text());
    g.channel_formula_rpn.assign(g.ds.channel_count(), {});
    g.global_formula = default_channel_formula_text();
    g.global_formula_rpn.clear();
    // Signal transforms belong to this document, never silently inherit another file's calibration.
    g.formula_ini_deferred = false;
    g.noise_threshold_enabled = false;
    invalidate_formula_runtime();
    g.data_t0 = g.ds.time.front();
    g.data_t1 = g.ds.time.back();
    if (g.data_t1 <= g.data_t0) g.data_t1 = g.data_t0 + 1.0;
    g.win_start = g.data_t0;
    g.win_end = g.data_t1;
    g.approx_dt = (g.data_t1 - g.data_t0) / static_cast<double>(g.ds.rows());
    g.cached_global_gap_step = cached_global_gap_step;
    g.cached_global_gap_step_ready = cached_global_gap_step_ready;
    invalidate_plot_analysis_cache();
    clear_all_measure_point_groups();
    g.guides.clear();
    g.markers.clear();
    g.active_marker = -1;
    invalidate_frf(true);
    g.frf.inputs.clear(); g.frf.outputs.clear(); g.frf.apply_processing = true;
    clear_fft_window();
    g.fft_selecting = false;
    g.spec_source_valid = false;
    g.spec_source_start = 0.0;
    g.spec_source_end = 0.0;
    g.spec_source_from_selection = false;
    g_undo.clear();
    g_redo.clear();
    hide_gap_details_card();
    stop_play();
    g.playhead = g.data_t0;
    g.playhead_active = false;
    g.auto_y = true;   // a fresh file starts on auto-fit
    if (hide_channels) g.auto_y_amp = true;
    if (g.mode != AnalysisMode::Time) {
        // New files should open in the time plot by default.
        set_mode(AnalysisMode::Time);
    }
    if (g.autoy) { SendMessageW(g.autoy, BM_SETCHECK, BST_CHECKED, 0); InvalidateRect(g.autoy, nullptr, FALSE); }
    if (g.menu) CheckMenuItem(g.menu, IDC_AUTOY, MF_BYCOMMAND | MF_CHECKED);
    apply_export_metadata_from_comments(g.ds.export_comments);
    bool reloaded_ini_formulas = false;
    if (!g.light_mode && g.formula_ini_deferred) {
        load_channel_formulas_from_ini();
        g.formula_ini_deferred = false;
        reloaded_ini_formulas = true;
    }
    if (reloaded_ini_formulas) {
        recompute_transforms_from_state();
    }
    clear_spectrum_cache_state();
    g.spec_source_valid = false;
    g.freq_start = 0.0;
    g.freq_end = 1.0;
    if (g.ds.frequency_axis) {
        g.noise_threshold_enabled = false;
        g.mode = AnalysisMode::Time;
        set_mode(AnalysisMode::FFT);
    }
    if (!requested_time_window) {
        g.cached_scan_path = wpath;
        g.cached_scan_start = g.data_t0;
        g.cached_scan_end = g.data_t1;
        g.cached_scan_valid = false;
        g.cached_scan_index.reset();
    }

    const wchar_t* base = wcsrchr(wpath.c_str(), L'\\');
    g.file_name = base ? base + 1 : wpath;
    const std::filesystem::path source_path(wpath);
    g.source_path = wpath;
    g.project_path = lstrcmpiW(source_path.extension().c_str(), kProjectExtension) == 0 ? wpath : L"";
    SetWindowTextW(g.main, (std::wstring(g_str->app_title) + L" — " + g.file_name).c_str());
    add_recent_file(wpath);
    if (g.welcome_wnd) { ShowWindow(g.welcome_wnd, SW_HIDE); show_ui_controls(); }

    rebuild_checks();
    refresh_open_document_selector();
    refresh_side_panel_controls();
    refresh_frf_controls(true);
    rebuild_menu_bar();
    sync_menu();
    refresh_settings_controls();
    layout();
    set_status();
    release_backbuffer();
    invalidate_plot();
    RedrawWindow(g.main, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    raise_main_window();
    g.last_error.clear();
}

bool start_async_scan_task(const std::wstring& wpath) {
    if (g.async_load_stage != AsyncLoadStage::None) {
        g.last_error = "A file is already loading.";
        return false;
    }
    g.last_error.clear();
    g.async_load_stage = AsyncLoadStage::ScanningRange;
    const unsigned long long token = ++g.async_load_token;
    auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
    g.async_load_cancel_flag = cancel_flag;
    show_loading(g_str->msg_scanning_range, true);

    const std::wstring path_copy = wpath;
    const HWND target = g.main;
    try {
        if (g_load_worker.joinable()) g_load_worker.join();
        g_load_worker = std::thread([token, path_copy, target, cancel_flag]() {
          try {
            auto result = std::make_unique<AsyncScanResult>();
            result->token = token;
            result->path = path_copy;
            std::string scan_error;
            result->index = std::make_shared<lvm::ScanIndex>();
            result->ok = lvm::scan_time_bounds(std::filesystem::path(path_copy), result->range_start, result->range_end, scan_error, cancel_flag.get(), result->index.get());
            result->cancelled = cancel_flag->load(std::memory_order_relaxed);
            if (!result->ok) result->error = std::move(scan_error);
            post_async_result(target, WM_APP_ASYNC_SCAN_DONE, std::move(result));
          } catch (...) { PostMessageW(target, WM_APP_ASYNC_SCAN_DONE, 0, 0); }
        });
    } catch (const std::exception& ex) {
        g.async_load_stage = AsyncLoadStage::None;
        g.async_load_cancel_flag.reset();
        hide_loading();
        g.last_error = ex.what();
        return false;
    }
    return true;
}

bool start_async_load_task(const std::wstring& wpath, const double* fragment_start,
                           const double* fragment_end, bool hide_channels) {
    if (g.async_load_stage != AsyncLoadStage::None) {
        g.last_error = "A file is already loading.";
        return false;
    }

    lvm::LoadOptions load_options{};
    load_options.scan_index = g.cached_scan_index;
    if (fragment_start && fragment_end && std::isfinite(*fragment_start) &&
        std::isfinite(*fragment_end) && *fragment_end > *fragment_start) {
        load_options.use_time_window = true;
        load_options.time_start = *fragment_start;
        load_options.time_end = *fragment_end;
    }

    g.last_error.clear();
    g.async_load_stage = AsyncLoadStage::LoadingFile;
    const unsigned long long token = ++g.async_load_token;
    auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
    g.async_load_cancel_flag = cancel_flag;
    show_loading(hide_channels ? g_str->msg_loading_light : g_str->msg_loading, true);

    const std::wstring path_copy = wpath;
    const HWND target = g.main;
    try {
        if (g_load_worker.joinable()) g_load_worker.join();
        g_load_worker = std::thread([token, path_copy, target, load_options, hide_channels, cancel_flag]() mutable {
          try {
            auto result = std::make_unique<AsyncLoadResult>();
            result->token = token;
            result->path = path_copy;
            result->hide_channels = hide_channels;
            result->requested_time_window = load_options.use_time_window;
            load_options.cancel_flag = cancel_flag.get();
            result->ds = lvm::read_lvm_file(std::filesystem::path(path_copy), load_options);
            result->ok = result->ds.ok;
            result->cancelled = !result->ok && cancel_flag->load(std::memory_order_relaxed);
            if (result->ok) {
                const std::vector<double>& raw_time = result->ds.raw_time.empty() ? result->ds.time : result->ds.raw_time;
                lvm::drop_duplicate_time_channels(result->ds, raw_time, cancel_flag.get());
                if (!load_options.use_time_window && !result->ds.frequency_axis) {
                    // Sectioned LabVIEW exports can still contain small backward jumps
                    // between blocks; normalize them so the plot stays strictly ordered.
                    lvm::make_monotonic(result->ds.time, cancel_flag.get());
                }
                if (result->ds.channel_count() == 0) { result->ok = false; result->error = "No data channels remain after removing duplicate time columns."; }
                if (!hide_channels) {
                    result->cached_global_gap_step = precompute_global_gap_step(result->ds.time);
                    result->cached_global_gap_step_ready = true;
                }
            } else {
                result->error = result->ds.error;
            }
            result->cancelled = cancel_flag->load(std::memory_order_relaxed);
            post_async_result(target, WM_APP_ASYNC_LOAD_DONE, std::move(result));
          } catch (...) { PostMessageW(target, WM_APP_ASYNC_LOAD_DONE, 0, 0); }
        });
    } catch (const std::exception& ex) {
        g.async_load_stage = AsyncLoadStage::None;
        g.async_load_cancel_flag.reset();
        hide_loading();
        g.last_error = ex.what();
        return false;
    }
    return true;
}

bool prompt_and_start_light_mode_load(const std::wstring& wpath, double range_start, double range_end) {
    double fragment_start = 0.0;
    double fragment_end = 0.0;
    if (!prompt_light_mode_window(range_start, range_end, fragment_start, fragment_end)) {
        g.last_error.clear();
        return false;
    }
    return start_async_load_task(wpath, &fragment_start, &fragment_end, true);
}

bool load_path_interactive(const std::wstring& wpath) {
    g.last_error.clear();
    if (g.light_mode) {
        std::error_code ec;
        g.cached_scan_valid = g.cached_scan_valid && g.cached_scan_index &&
            std::filesystem::file_size(std::filesystem::path(wpath), ec) == g.cached_scan_index->file_size && !ec &&
            std::filesystem::last_write_time(std::filesystem::path(wpath), ec) == g.cached_scan_index->modified && !ec;
        if (g.cached_scan_valid && lstrcmpiW(g.cached_scan_path.c_str(), wpath.c_str()) == 0) {
            return prompt_and_start_light_mode_load(wpath, g.cached_scan_start, g.cached_scan_end);
        }
        return start_async_scan_task(wpath);
    }
    return start_async_load_task(wpath);
}

void open_file() {
    std::vector<wchar_t> file(65536, L'\0');
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.main;
    ofn.lpstrFilter = g_str->filter_open;
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;
    std::vector<std::wstring> paths;
    const wchar_t* first = file.data();
    const wchar_t* next = first + lstrlenW(first) + 1;
    if (*next == L'\0') {
        paths.emplace_back(first);
    } else {
        const std::wstring folder(first);
        while (*next != L'\0') {
            paths.push_back((std::filesystem::path(folder) / next).wstring());
            next += lstrlenW(next) + 1;
        }
    }
    queue_open_paths(std::move(paths));
}

void show_recent_files_menu(HWND owner) {
    toggle_welcome_recent_files_panel(owner);
}

LRESULT handle_loading_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_APP_ASYNC_SCAN_DONE: {
            std::unique_ptr<AsyncScanResult> result(reinterpret_cast<AsyncScanResult*>(lp));
            if (g_load_worker.joinable()) g_load_worker.join();
            if (!result) {
                const bool cancelled = g.async_load_cancel_flag && g.async_load_cancel_flag->load();
                g.async_load_stage = AsyncLoadStage::None; g.async_load_cancel_flag.reset(); hide_loading();
                if (!cancelled) MessageBoxW(hwnd, L"Unable to scan file. Check available memory and file access.", g_str->msg_read_err, MB_ICONERROR);
                return 0;
            }
            if (result->token != g.async_load_token || g.async_load_stage != AsyncLoadStage::ScanningRange) return 0;
            g.async_load_stage = AsyncLoadStage::None;
            result->cancelled = result->cancelled || (g.async_load_cancel_flag && g.async_load_cancel_flag->load());
            g.async_load_cancel_flag.reset();
            hide_loading();
            if (result->cancelled) {
                g.last_error.clear();
                clear_open_queue();
                return 0;
            }
            if (!result->ok) {
                g.last_error = result->error;
                MessageBoxW(hwnd, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
                continue_open_queue();
                return 0;
            }
            g.cached_scan_path = result->path;
            g.cached_scan_start = result->range_start;
            g.cached_scan_end = result->range_end;
            g.cached_scan_valid = true;
            g.cached_scan_index = result->index;
            if (!prompt_and_start_light_mode_load(result->path, result->range_start, result->range_end) &&
                !g.last_error.empty()) {
                MessageBoxW(hwnd, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
            }
            return 0;
        }
        case WM_APP_ASYNC_LOAD_DONE: {
            std::unique_ptr<AsyncLoadResult> result(reinterpret_cast<AsyncLoadResult*>(lp));
            if (g_load_worker.joinable()) g_load_worker.join();
            if (!result) {
                const bool cancelled = g.async_load_cancel_flag && g.async_load_cancel_flag->load();
                g.async_load_stage = AsyncLoadStage::None; g.async_load_cancel_flag.reset(); hide_loading();
                if (!cancelled) MessageBoxW(hwnd, L"Unable to load file. Check available memory and file access.", g_str->msg_read_err, MB_ICONERROR);
                return 0;
            }
            if (result->token != g.async_load_token || g.async_load_stage != AsyncLoadStage::LoadingFile) return 0;
            g.async_load_stage = AsyncLoadStage::None;
            result->cancelled = result->cancelled || (g.async_load_cancel_flag && g.async_load_cancel_flag->load());
            g.async_load_cancel_flag.reset();
            hide_loading();
            if (result->cancelled) {
                g.last_error.clear();
                clear_open_queue();
                return 0;
            }
            if (!result->ok) {
                g.last_error = result->error;
                MessageBoxW(hwnd, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
                continue_open_queue();
                return 0;
            }
            apply_loaded_dataset(std::move(result->ds), result->path, result->hide_channels,
                                 result->requested_time_window, result->cached_global_gap_step,
                                 result->cached_global_gap_step_ready);
            continue_open_queue();
            return 0;
        }
        case WM_DROPFILES: {
            handle_file_drop(hwnd, reinterpret_cast<HDROP>(wp));
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace gui

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
#include "gui_resources.hpp"
#include "gui_recording_cache.hpp"
#include "scan_index_cache.hpp"
#include "gui_overview.hpp"
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

bool apply_loaded_dataset(lvm::Dataset ds, const std::wstring& wpath, bool hide_channels,
                          bool requested_time_window, double cached_global_gap_step,
                          bool cached_global_gap_step_ready,
                          std::optional<lvm::RecordingStamp> expected_stamp) {
    if (ds.time.empty()) {
        g.last_error = "No time data available.";
        return false;
    }

    const auto index=g.cached_scan_valid && lstrcmpiW(g.cached_scan_path.c_str(),wpath.c_str())==0
        ? g.cached_scan_index : nullptr;
    std::error_code stamp_error;
    const auto size=std::filesystem::file_size(std::filesystem::path(wpath),stamp_error);
    const auto modified=stamp_error ? std::filesystem::file_time_type{} :
        std::filesystem::last_write_time(std::filesystem::path(wpath),stamp_error);
    std::shared_ptr<const lvm::RecordingSource> recording;
    try {
        if(expected_stamp && (stamp_error || expected_stamp->bytes!=size || expected_stamp->modified!=modified))
            throw lvm::RecordingFailure(lvm::RecordingError::SourceChanged,"The source file changed while loading. Reopen the recording.");
        if(!stamp_error && !ds.frequency_axis && ds.source_columns.size()==ds.channel_count())
            recording=lvm::RecordingSource::from_file(std::filesystem::path(wpath),
                {ds.time.front(),ds.time.back()},ds.source_columns,index,lvm::RecordingStamp{size,modified});
    } catch(const std::exception& ex) {
        g.last_error=ex.what();return false;
    }

    begin_loaded_document();
    g.current_file_partial = requested_time_window || ds.partial;
    g_filter_slider_before.reset();
    g.ds = std::move(ds);
    g.source_scan_index=index;
    g.recording_source=std::move(recording);
    g.loaded_source_size=size;
    g.loaded_source_modified=modified;
    g.loaded_source_stamp_valid=!stamp_error;
    ++g.numerical_revision;
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
    g.show_channel_legend = true;
    apply_export_metadata_from_comments(g.ds.export_comments);
    bool reloaded_ini_formulas = false;
    if (g.formula_ini_deferred) {
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
    return true;
}

bool apply_scanned_recording(AsyncScanResult& result) {
    if(!result.ok || !result.index || !result.source_metadata.ok)return false;
    g.cached_scan_path=result.path;g.cached_scan_valid=true;g.cached_scan_index=result.index;
    g.cached_scan_start=result.range_start;g.cached_scan_end=result.range_end;
    if(!apply_loaded_dataset(std::move(result.source_metadata),result.path,false,true,0,false,
        lvm::RecordingStamp{result.index->file_size,result.index->modified}))return false;
    g.recording_rows=result.source_rows;
    g.data_t0=g.win_start=result.range_start;g.data_t1=g.win_end=result.range_end;
    g.current_file_partial=false;
    g.overview.render_enabled=true;
    // Source processing is explicit; no unrelated document's stitched axis.
    g.stitch_time_gaps=false;
    g.cached_global_gap_step_ready=false;
    g.approx_dt=result.source_rows>1 ? (result.range_end-result.range_start)/(result.source_rows-1) : 1;
    synchronize_overview_viewport(1024);
    sync_menu();set_status();invalidate_plot();return true;
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
    if (g.main) show_loading(g_str->msg_scanning_range, true);

    const std::wstring path_copy = wpath;
    const HWND target = g.main;
    try {
        const auto memory_budget=refresh_load_memory_budget();
        if (g_load_worker.joinable()) g_load_worker.join();
        g_load_worker = std::thread([token, path_copy, target, cancel_flag,memory_budget]() {
          try {
            auto result = std::make_unique<AsyncScanResult>();
            result->token = token;
            result->path = path_copy;
            try {
                std::shared_ptr<lvm::DiskCache> cache;
                // Optional persistence must not prevent valid source opening.
                try{cache=recording_disk_cache();}catch(const std::exception&){}
                lvm::ScanIndexCacheOptions options;options.budget=memory_budget;
                auto scanned=lvm::scan_index_cached(std::filesystem::path(path_copy),std::move(cache),options,cancel_flag.get());
                result->index=std::move(scanned.index);result->index_from_disk_cache=scanned.from_cache;
                result->range_start=result->index->range_start;result->range_end=result->index->range_end;result->ok=true;
                {
                    // Discover channels across the complete recording: a column
                    // matching time only in the prefix must not be discarded.
                    auto admission=memory_budget->reserve(16ULL*1024*1024);
                    if(!admission)throw lvm::RecordingFailure(lvm::RecordingError::ResourceLimit,"No memory budget for source metadata.");
                    lvm::LoadOptions read;read.stream_all_columns=true;read.block_rows=1024;
                    read.max_resident_bytes=8ULL*1024*1024;
                    read.use_time_window=true;read.time_start=result->range_start;read.time_end=result->range_end;
                    read.cancel_flag=cancel_flag.get();
                    std::vector<char> valid,duplicate;
                    lvm::Dataset seed;
                    read.consume_block=[&](const auto& time,const auto& values) {
                        if(values.size()*16ULL*1024>8ULL*1024*1024)
                            throw lvm::RecordingFailure(lvm::RecordingError::ResourceLimit,"Too many source columns for the metadata budget.");
                        valid.resize(values.size(),0);duplicate.resize(values.size(),1);
                        seed.channels.resize(values.size()>0 ? values.size()-1 : 0);
                        for(std::size_t i=0;i<time.size();++i) {
                            if(!std::isfinite(time[i]))continue;
                            if(seed.time.size()<2) {
                                seed.time.push_back(time[i]);seed.raw_time.push_back(values[0][i]);
                                for(std::size_t c=1;c<values.size();++c)seed.channels[c-1].push_back(values[c][i]);
                            }
                            ++result->source_rows;
                            for(std::size_t c=1;c<values.size();++c)if(!std::isnan(values[c][i])) {
                                valid[c]=1;
                                if(!std::isnan(values[0][i]) &&
                                   !(std::fabs(values[c][i]-values[0][i])<=1e-12+1e-9*std::fabs(values[0][i])))duplicate[c]=0;
                            }
                        }
                    };
                    auto metadata=lvm::read_lvm_file(std::filesystem::path(path_copy),read);
                    if(!metadata.ok)throw std::runtime_error(metadata.error);
                    seed.names=std::move(metadata.names);seed.generated_names=std::move(metadata.generated_names);
                    seed.source_columns=std::move(metadata.source_columns);seed.export_comments=std::move(metadata.export_comments);
                    seed.frequency_axis=metadata.frequency_axis;seed.stats=metadata.stats;seed.ok=true;
                    lvm::Dataset kept;kept.time=std::move(seed.time);kept.raw_time=std::move(seed.raw_time);
                    kept.export_comments=std::move(seed.export_comments);kept.stats=seed.stats;kept.frequency_axis=seed.frequency_axis;kept.ok=true;
                    for(std::size_t c=0;c<seed.channels.size();++c)if(valid[c+1] && (!duplicate[c+1] || seed.frequency_axis)) {
                        kept.channels.push_back(std::move(seed.channels[c]));
                        kept.channels.back().resize(kept.time.size(),std::numeric_limits<double>::quiet_NaN());
                        kept.names.push_back(seed.generated_names[c] ? "Channel_"+std::to_string(kept.channels.size()) : seed.names[c]);
                        kept.generated_names.push_back(seed.generated_names[c]);kept.source_columns.push_back(seed.source_columns[c]);
                    }
                    if(kept.rows()<2 || !kept.channel_count())throw std::runtime_error("No signal channels or fewer than two samples.");
                    // Frequency-axis imports stay on the existing spectrum path.
                    if(kept.frequency_axis)throw std::runtime_error("Large stored spectra require the spectrum import path.");
                    result->source_metadata=std::move(kept);
                    std::error_code ec;
                    if(std::filesystem::file_size(path_copy,ec)!=result->index->file_size || ec ||
                       std::filesystem::last_write_time(path_copy,ec)!=result->index->modified || ec)
                        throw lvm::RecordingFailure(lvm::RecordingError::SourceChanged,"The source changed while opening. Reopen it.");
                }
            }catch(const std::exception& e){result->ok=false;result->error=e.what();}
            result->cancelled = cancel_flag->load(std::memory_order_relaxed);
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

std::size_t display_load_memory_budget(std::uint64_t available_physical,
                                     std::uint64_t available_commit) {
    constexpr std::uint64_t maximum=2ULL*1024*1024*1024;
    const auto allowance=std::min(maximum,std::min(available_physical,available_commit)/8);
    // Zero means unbounded to the parser; never return zero under memory pressure.
    return static_cast<std::size_t>(std::max<std::uint64_t>(1,allowance));
}

bool start_async_load_task(const std::wstring& wpath, const double* fragment_start,
                           const double* fragment_end, bool hide_channels) {
    if (g.async_load_stage != AsyncLoadStage::None) {
        g.last_error = "A file is already loading.";
        return false;
    }

    lvm::LoadOptions load_options{};
    try {
        if(g_load_worker.joinable())g_load_worker.join();
        g_frf_worker.clear_cache(); // A new file must not be blocked by an idle input cache.
        auto budget=refresh_load_memory_budget();
        const auto memory=resource_snapshot();
        const auto fragment_cap=g.resource_policy.automatic
            ? display_load_memory_budget(memory.available_physical,memory.available_commit) : 2ULL*1024*1024*1024;
        // Reserve the parser capacity bound and equal headroom for preparation.
        load_options.max_resident_bytes=static_cast<std::size_t>(std::min<std::uint64_t>(fragment_cap,budget->available()/2));
        if(load_options.max_resident_bytes==0) {
            g.last_error=g_str==&kEn ? "No memory budget remains for loading. Close another recording or adjust the data budget in Settings."
                : "Бюджет данных исчерпан. Закройте другую запись или измените бюджет данных в настройках.";
            return false;
        }
        auto reservation=budget->reserve(2*load_options.max_resident_bytes);
        if(!reservation) { g.last_error="Unable to reserve memory for loading.";return false; }
        if(g.cached_scan_valid && lstrcmpiW(g.cached_scan_path.c_str(),wpath.c_str())==0)
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
        show_loading(g_str->msg_loading, true);

        const std::wstring path_copy = wpath;
        const HWND target = g.main;
        g_load_worker = std::thread([token, path_copy, target, load_options, hide_channels, cancel_flag,reservation]() mutable {
          try {
            auto result = std::make_unique<AsyncLoadResult>();
            result->memory_reservation=reservation;
            result->token = token;
            result->path = path_copy;
            result->hide_channels = hide_channels;
            result->requested_time_window = load_options.use_time_window;
            std::error_code stamp_error;
            const auto source_path=std::filesystem::path(path_copy);
            result->source_size=std::filesystem::file_size(source_path,stamp_error);
            if(!stamp_error)result->source_modified=std::filesystem::last_write_time(source_path,stamp_error);
            result->source_stamp_valid=!stamp_error;
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

static bool start_source_open(const std::wstring& wpath) {
    std::error_code ec;
    g.cached_scan_valid = g.cached_scan_valid && g.cached_scan_index &&
        std::filesystem::file_size(std::filesystem::path(wpath), ec) == g.cached_scan_index->file_size && !ec &&
        std::filesystem::last_write_time(std::filesystem::path(wpath), ec) == g.cached_scan_index->modified && !ec;
    if (g.cached_scan_valid && lstrcmpiW(g.cached_scan_path.c_str(), wpath.c_str()) == 0) {
        return start_async_scan_task(wpath);
    }
    return start_async_scan_task(wpath);
}

bool retry_load_as_source(AsyncLoadResult& result) {
    // Drop failed resident samples before opening the complete bounded source.
    const auto path=result.path;
    result.ds={};
    result.memory_reservation.reset();
    g.last_error.clear();
    return start_source_open(path);
}

bool load_path_interactive(const std::wstring& wpath) {
    g.last_error.clear();
    std::error_code size_error;
    const bool large=std::filesystem::file_size(std::filesystem::path(wpath),size_error)>512ULL*1024*1024 && !size_error;
    if (large) return start_source_open(wpath);
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
            if(!apply_scanned_recording(*result))MessageBoxW(hwnd,to_w(g.last_error).c_str(),g_str->msg_read_err,MB_ICONERROR | MB_OK);
            continue_open_queue();return 0;
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
            if(result->ok && result->source_stamp_valid) {
                std::error_code stamp_error;
                const auto path=std::filesystem::path(result->path);
                const auto size=std::filesystem::file_size(path,stamp_error);
                const auto modified=stamp_error ? std::filesystem::file_time_type{} :
                    std::filesystem::last_write_time(path,stamp_error);
                if(stamp_error || size!=result->source_size || modified!=result->source_modified) {
                    result->ok=false;
                    result->error="The source file changed while loading. Reopen the recording.";
                }
            }
            if (!result->ok) {
                if (result->ds.resident_limit_exceeded) {
                    if (!retry_load_as_source(*result)) {
                        if (!g.last_error.empty())
                            MessageBoxW(hwnd,to_w(g.last_error).c_str(),g_str->msg_read_err,MB_ICONERROR | MB_OK);
                        continue_open_queue();
                    }
                    return 0;
                }
                g.last_error = result->error;
                MessageBoxW(hwnd, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
                result->ds={};result->memory_reservation.reset();
                continue_open_queue();
                return 0;
            }
            const bool accepted=apply_loaded_dataset(std::move(result->ds), result->path, result->hide_channels,
                                 result->requested_time_window, result->cached_global_gap_step,
                                 result->cached_global_gap_step_ready,result->source_stamp_valid
                                    ? std::optional<lvm::RecordingStamp>{{result->source_size,result->source_modified}} : std::nullopt);
            result->ds={};result->memory_reservation.reset();
            if(!accepted)MessageBoxW(hwnd,to_w(g.last_error).c_str(),g_str->msg_read_err,MB_ICONERROR | MB_OK);
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

#include "gui_documents.hpp"
#include "gui_analysis_source.hpp"
#include "gui_dialogs.hpp"
#include "gui_export.hpp"
#include "gui_frf.hpp"
#include "gui_layout.hpp"
#include "gui_loading.hpp"
#include "gui_menu.hpp"
#include "gui_playback.hpp"
#include "gui_render.hpp"
#include "gui_settings_window.hpp"
#include "gui_side_panel.hpp"
#include "gui_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_state_history.hpp"
#include "gui_status.hpp"
#include "gui_text.hpp"
#include "gui_welcome.hpp"
#include "gui_window.hpp"

namespace gui {
namespace {

DocumentState& active_document_state() {
    return static_cast<DocumentState&>(g);
}

void cancel_document_workers() {
    stop_play();
    g_spectrum_worker.cancel();
    g_frf_worker.cancel();
    // Results from a computation launched before a switch are stale.  Keep a
    // completed cache, but do not leave the target document marked pending.
    ++g.spec_generation;
    ++g.frf.generation;
    g.spec_pending = false;
    g.spec_fit_pending = false;
    g.frf.pending = false;
}

void refresh_active_document_ui() {
    if (!g.main) return;
    refresh_active_document_title();
    if (g.play) SetWindowTextW(g.play, g.playing ? g_str->btn_pause : g_str->btn_play);
    refresh_open_document_selector();
    if (g.autoy) {
        SendMessageW(g.autoy, BM_SETCHECK,
                     (g.mode == AnalysisMode::FRF ? g.frf.auto_y : g.auto_y) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    rebuild_checks();
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
}

bool same_path(const std::wstring& a, const std::wstring& b) {
    return !a.empty() && !b.empty() && lstrcmpiW(a.c_str(), b.c_str()) == 0;
}

bool is_open_or_queued(const std::wstring& path) {
    if (same_path(g.source_path, path)) return true;
    for (const auto& document : g.inactive_documents) {
        if (same_path(document.source_path, path)) return true;
    }
    for (const auto& queued : g.pending_open_paths) {
        if (same_path(queued, path)) return true;
    }
    return false;
}

} // namespace

void refresh_active_document_title() {
    if (!g.main) return;
    if (g.file_name.empty()) {
        SetWindowTextW(g.main, g_str->app_title);
    } else {
        const wchar_t* changed = g.project_dirty ? L" *" : L"";
        SetWindowTextW(g.main, (std::wstring(g_str->app_title) + L" — " + g.file_name + changed).c_str());
    }
}

bool active_document_has_unsaved_changes() {
    return has_data() && g.project_dirty;
}

void mark_active_document_dirty() {
    if (!has_data()) return;
    if (g.next_project_revision < g.project_revision) g.next_project_revision = g.project_revision;
    g.project_revision = ++g.next_project_revision;
    g.project_dirty = g.project_revision != g.saved_project_revision;
    refresh_active_document_title();
}

void mark_active_document_saved() {
    if (!has_data()) return;
    g.saved_project_revision = g.project_revision;
    g.project_dirty = false;
    refresh_active_document_title();
}

namespace {

bool confirm_active_document_close() {
    if (!active_document_has_unsaved_changes()) return true;
    const std::wstring name = g.file_name.empty()
        ? (g_str == &kEn ? L"Untitled" : L"Без имени")
        : g.file_name;
    const std::wstring message = (g_str == &kEn)
        ? L"Save changes to \"" + name + L"\" before closing?"
        : L"Сохранить изменения в \"" + name + L"\" перед закрытием?";
    const int choice = show_styled_save_changes_prompt(g.main, g_str->app_title, message.c_str());
    if (choice == IDYES) return save_current_project();
    return choice == IDNO;
}

} // namespace

std::size_t open_document_count() {
    return (has_data() ? 1u : 0u) + g.inactive_documents.size();
}

std::wstring open_document_label(std::size_t index, bool mark_active) {
    const DocumentState* document = nullptr;
    if (index == 0 && has_data()) {
        document = &active_document_state();
    } else {
        const std::size_t inactive_index = has_data() ? index - 1 : index;
        if (inactive_index < g.inactive_documents.size()) document = &g.inactive_documents[inactive_index];
    }
    if (!document) return L"";
    const std::wstring name = document->file_name.empty() ? L"Untitled" : document->file_name;
    return mark_active && index == 0 ? L"• " + name : name;
}

bool switch_to_document(std::size_t index) {
    if (!has_data() || index == 0 || index > g.inactive_documents.size() ||
        g.async_load_stage != AsyncLoadStage::None) return index == 0 && has_data();
    cancel_document_workers();
    save_active_document_history();
    std::swap(active_document_state(), g.inactive_documents[index - 1]);
    restore_active_document_history();
    refresh_active_document_ui();
    return true;
}

void refresh_open_document_selector() {
    if (!g.document_selector) return;
    SendMessageW(g.document_selector, CB_RESETCONTENT, 0, 0);
    const std::size_t count = open_document_count();
    for (std::size_t i = 0; i < count; ++i) {
        const std::wstring label = open_document_label(i);
        SendMessageW(g.document_selector, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (count != 0) SendMessageW(g.document_selector, CB_SETCURSEL, 0, 0);
    EnableWindow(g.document_selector, count != 0);
    if (g.document_close) EnableWindow(g.document_close, count != 0);
}

bool close_active_document() {
    if (!has_data() || g.async_load_stage != AsyncLoadStage::None) return false;
    if (!confirm_active_document_close()) return false;
    cancel_document_workers();
    if (!g.inactive_documents.empty()) {
        std::swap(active_document_state(), g.inactive_documents.back());
        g.inactive_documents.pop_back();
        restore_active_document_history();
        refresh_active_document_ui();
        return true;
    }

    active_document_state() = DocumentState{};
    clear_active_document_history();
    if (g.main) show_welcome(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g.main, GWLP_HINSTANCE)));
    refresh_active_document_ui();
    return true;
}

bool request_application_close(HWND hwnd) {
    if (g.async_load_stage != AsyncLoadStage::None) {
        show_styled_info_prompt(hwnd, g_str->app_title,
            g_str == &kEn ? L"Wait for loading to finish or cancel it before closing the application."
                         : L"Дождитесь завершения загрузки или отмените её перед закрытием программы.", false);
        return false;
    }
    while (has_data()) {
        if (!close_active_document()) return false;
    }
    DestroyWindow(hwnd);
    return true;
}

void begin_loaded_document() {
    if (!has_data()) return;
    cancel_document_workers();
    save_active_document_history();
    g.inactive_documents.emplace_back();
    std::swap(active_document_state(), g.inactive_documents.back());
    restore_active_document_history();
}

void queue_open_paths(std::vector<std::wstring> paths) {
    for (auto& path : paths) {
        if (!path.empty() && !is_open_or_queued(path)) g.pending_open_paths.push_back(std::move(path));
    }
    continue_open_queue();
}

void continue_open_queue() {
    while (g.async_load_stage == AsyncLoadStage::None && !g.pending_open_paths.empty()) {
        const std::wstring path = std::move(g.pending_open_paths.front());
        g.pending_open_paths.erase(g.pending_open_paths.begin());
        if (load_path_interactive(path)) return;
        if (!g.last_error.empty()) {
            MessageBoxW(g.main, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
            g.last_error.clear();
        }
    }
}

void clear_open_queue() {
    g.pending_open_paths.clear();
}

} // namespace gui

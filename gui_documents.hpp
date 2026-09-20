#pragma once
#include "gui_platform.hpp"

namespace gui {

std::size_t open_document_count();

std::wstring open_document_label(std::size_t index, bool mark_active = false);

void refresh_open_document_selector();

void refresh_active_document_title();

bool active_document_has_unsaved_changes();

void mark_active_document_dirty();

void mark_active_document_saved();

bool switch_to_document(std::size_t index);

bool close_active_document();

bool request_application_close(HWND hwnd);

// Called only after a loader has produced a valid Dataset.  It preserves the
// current active document as an inactive slot and prepares a fresh active one.
void begin_loaded_document();

void queue_open_paths(std::vector<std::wstring> paths);

void continue_open_queue();

void clear_open_queue();

} // namespace gui

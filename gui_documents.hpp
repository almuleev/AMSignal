#pragma once
#include "gui_platform.hpp"

namespace gui {

std::size_t open_document_count();

std::wstring open_document_label(std::size_t index, bool mark_active = false);

void refresh_open_document_selector();

bool switch_to_document(std::size_t index);

bool close_active_document();

// Called only after a loader has produced a valid Dataset.  It preserves the
// current active document as an inactive slot and prepares a fresh active one.
void begin_loaded_document();

void queue_open_paths(std::vector<std::wstring> paths);

void continue_open_queue();

void clear_open_queue();

} // namespace gui

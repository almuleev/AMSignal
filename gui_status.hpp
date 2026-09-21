#pragma once
#include "gui_platform.hpp"

namespace gui {

bool marker_status_detail(std::wstring& text, COLORREF& color);

void set_status();

// Keeps the status-bar tooltip aligned to the bottom row and supplies its
// complete, untruncated text when the row is narrower than its contents.
void update_status_tooltip();

std::wstring toolbar_hover_text(HWND btn);

void status_msg(const std::wstring& m);

} // namespace gui

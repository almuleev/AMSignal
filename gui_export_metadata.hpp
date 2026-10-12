#pragma once
#include "gui_platform.hpp"
#include "gui_state.hpp"
#include "gui_dialogs.hpp"

namespace gui {

std::wstring export_channel_label_text(std::size_t channel_index, bool include_names);

const wchar_t* export_filter_mode_key(int mode);

const wchar_t* export_filter_topology_key(int topology);

std::wstring export_color_triplet(COLORREF color);

std::wstring export_point_display_text(const PointDisplay& display);

void write_export_comment(std::ostream& out, const std::wstring& text, const char* line_end);

void write_export_key_value(std::ostream& out, const std::wstring& key, const std::wstring& value, const char* line_end);

const wchar_t* export_range_mode_key(ExportRangeMode range);

const wchar_t* export_processing_mode_key(bool apply_processing);

std::wstring export_metadata_text(const std::wstring& value);

void write_export_metadata(std::ostream& out,
                           const ExportOptions& opts,
                           bool csv,
                           double range_start,
                           double range_end,
                           bool actual_selected_range,
                           const std::vector<std::size_t>& exported_channels);

void apply_export_metadata_from_comments(const std::vector<std::string>& comments);

void write_frf_metadata(std::ostream& out);

} // namespace gui

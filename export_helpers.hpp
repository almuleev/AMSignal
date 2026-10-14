#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

enum class ExportRangeMode {
    Selected,
    Visible,
    Whole,
};

const wchar_t* export_prompt_title_text(bool english);
std::wstring export_range_title_text(ExportRangeMode range, bool english);
const wchar_t* export_prompt_continue_text(bool english);
const wchar_t* export_prompt_cancel_text(bool english);
const wchar_t* export_include_channel_names_text(bool english);
const wchar_t* export_include_points_text(bool english);
const wchar_t* export_include_markers_text(bool english);
const wchar_t* export_include_guides_text(bool english);
const wchar_t* export_include_formulas_text(bool english);
const wchar_t* export_include_filter_text(bool english);
const wchar_t* export_include_graph_settings_text(bool english);
std::wstring export_default_name(const std::wstring& stem,
                                 ExportRangeMode range,
                                 const wchar_t* ext,
                                 bool freq_mode);
std::pair<std::size_t, std::size_t> export_range_bounds(const std::vector<double>& values,
                                                        double start,
                                                        double end);
std::wstring export_status_prefix(const wchar_t* format_name,
                                  ExportRangeMode range,
                                  bool english);

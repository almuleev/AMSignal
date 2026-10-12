#pragma once
#include "gui_platform.hpp"

namespace gui {

std::wstring app_directory_path();

std::wstring app_config_path();

void load_app_settings();

void save_app_settings();

extern bool g_settings_dirty;

inline constexpr std::size_t kMaxRecentFiles = 6;

std::wstring canonical_recent_file_path(const std::wstring& path);

void load_recent_files_from_ini();

void save_recent_files_to_ini();

void add_recent_file(const std::wstring& path);

void save_runtime_settings_now();

void save_runtime_settings();


int read_ini_int(const wchar_t* section, const wchar_t* key, int def_value);

double read_ini_double(const wchar_t* section, const wchar_t* key, double def_value);

double read_ini_optional_double(const wchar_t* section, const wchar_t* key, double def_value);

std::wstring trim_wide_ascii(const std::wstring& text);

std::wstring read_ini_wstring(const wchar_t* section, const wchar_t* key, const std::wstring& def_value);

std::wstring normalize_axis_label_text(const std::wstring& text, const wchar_t* fallback);

void write_ini_double(const wchar_t* section, const wchar_t* key, double value);

void write_ini_optional_double(const wchar_t* section, const wchar_t* key, double value);

void load_channel_formulas_from_ini();

void load_runtime_settings();

} // namespace gui

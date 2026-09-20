#pragma once
#include "gui_platform.hpp"
#include "gui_dialogs.hpp"

namespace gui {

int png_encoder_clsid(CLSID* clsid);

bool save_png(const std::wstring& path);

double export_channel_sample(std::size_t channel_index, std::size_t row_index, bool apply_processing);

std::vector<std::size_t> export_channel_indices(bool include_hidden_channels);

std::vector<int> export_spectrum_channel_indices(const lvm::Spectrum& spec);

bool build_export_spectrum(const ExportOptions& opts, lvm::Spectrum& out_spec,
                           std::vector<int>& out_channel_indices, bool& out_all_channels);

bool export_range_bounds_for_mode(ExportRangeMode range, double& start, double& end, bool& actual_selected);

bool write_tabular_export(std::ofstream& out, const ExportOptions& opts);

bool save_tabular_export(const std::wstring& path, const ExportOptions& opts);

bool write_lvm_export(std::ofstream& out, const ExportOptions& opts);

bool save_lvm_export(const std::wstring& path, const ExportOptions& opts);

bool save_dialog(std::wstring& out_path, const wchar_t* filter, const wchar_t* defext,
                 const std::wstring& defname);

std::wstring file_stem();

void save_png_dialog();

const wchar_t* export_file_extension(ExportFileFormat format);

const wchar_t* export_file_filter(ExportFileFormat format);

const wchar_t* export_file_name(ExportFileFormat format);

bool save_export_file(const std::wstring& path, const ExportOptions& opts);

bool save_project_file(const std::wstring& path);

bool save_current_project();

bool write_frf_csv(std::ofstream& out);

bool save_frf_csv(const std::wstring& path);

void save_as_dialog();

std::wstring lvm_current_date_text(const SYSTEMTIME& st);

std::wstring lvm_current_time_text(const SYSTEMTIME& st);

double lvm_export_nominal_delta_x();

} // namespace gui

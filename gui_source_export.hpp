#pragma once
#include "gui_export.hpp"
#include <functional>
namespace gui {
using SourceExportWriter=std::function<bool(std::ofstream&,const std::atomic<bool>*)>;
SourceExportWriter prepare_source_export(const ExportOptions& options);
bool save_source_export(const std::wstring& path,const ExportOptions& options,const std::atomic<bool>* cancel=nullptr);
bool start_source_export(const std::wstring& path,ExportOptions options);
void cancel_source_export();
void poll_source_export();
void shutdown_source_exports();
}

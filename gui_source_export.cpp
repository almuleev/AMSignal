#include "gui_source_export.hpp"
#include "gui_recording_processing.hpp"
#include "gui_export_metadata.hpp"
#include "gui_analysis_source.hpp"
#include "gui_resources.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include "gui_status.hpp"
#include "gui_documents.hpp"
#include "gui_loading_drop.hpp"
#include "disk_spectrum.hpp"
#include <sstream>
#include <mutex>
#include <thread>

namespace gui {
namespace {
void check(const std::atomic<bool>* cancel) {if(cancel && cancel->load())throw lvm::RecordingFailure(lvm::RecordingError::Cancelled,"Operation cancelled.");}
bool safe_destination(const std::filesystem::path& destination,const lvm::RecordingSource& source) {
    std::error_code ec;const auto target=std::filesystem::absolute(destination,ec).lexically_normal();if(ec)return false;
    if(target==source.path().lexically_normal())return false;
    if(std::filesystem::exists(target,ec) && std::filesystem::equivalent(target,source.path(),ec))return false;
    return true;
}
struct Worker {
    std::thread thread;std::shared_ptr<std::atomic<bool>> cancel;
    std::mutex mutex;bool done=false,ok=false,active=false;std::string error;
    std::wstring path;bool project=false;std::uint64_t revision=0;
    std::shared_ptr<const lvm::RecordingSource> source;
    ~Worker(){if(cancel)cancel->store(true);if(thread.joinable())thread.join();}
} worker;
}
SourceExportWriter prepare_source_export(const ExportOptions& supplied) {
    auto opts=supplied;const bool project=opts.save_mode==ExportSaveMode::Project;
    if(project){opts.format=ExportFileFormat::Lvm;opts.selected_range=ExportRangeMode::Whole;opts.apply_processing_to_data=false;opts.include_hidden_channels=true;}
    const bool fft=!project && g.mode==AnalysisMode::FFT;
    if(!project && g.mode==AnalysisMode::FRF)throw std::runtime_error("Use FRF CSV export.");
    const bool csv=opts.format==ExportFileFormat::Csv,is_lvm=opts.format==ExportFileFormat::Lvm;
    if(fft && is_lvm)throw std::runtime_error("Use CSV or TXT to export spectrum bins.");
    const auto source=processed_recording_source(opts.apply_processing_to_data);
    lvm::RecordingReadRequest read;read.channels=export_channel_indices(opts.include_hidden_channels);read.block_bytes=1024*1024;read.block_rows=4096;
    if(read.channels.empty())throw std::runtime_error("No channels selected for export.");
    double start=0,end=0;bool selected=false;
    if(project){start=source->bounds().start;end=source->bounds().end;}
    else if(!export_range_bounds_for_mode(opts.selected_range,start,end,selected))throw std::runtime_error("Invalid export range.");
    read.range=lvm::AnalysisRange::selected(start,end);
    if(fft){double a=0,b=0;bool flag=false;if(!current_fft_source_window(a,b,flag))throw std::runtime_error("Invalid FFT source range.");read.range=lvm::AnalysisRange::selected(a,b);}
    std::vector<std::string> labels,names;
    for(auto c:read.channels){labels.push_back(to_utf8(export_channel_label_text(c,opts.include_channel_names)));names.push_back(g.ds.names[c]);}
    std::ostringstream metadata;write_export_metadata(metadata,opts,csv,start,end,selected,read.channels);
    const auto header=metadata.str();const auto resources=current_analysis_resources();
    return [source,read,labels,names,header,resources,start,end,csv,is_lvm,fft,opts]
        (std::ofstream& out,const std::atomic<bool>* cancel) {
        check(cancel);const auto reserve_bytes=fft ? 2ULL*1024*1024 : 12ULL*1024*1024;
        auto guard=resources.budget ? resources.budget->reserve(reserve_bytes) : nullptr;
        if(resources.budget && !guard)throw lvm::RecordingFailure(lvm::RecordingError::ResourceLimit,"No memory budget for streaming export.");
        const char sep=csv ? ',' : '\t';const char* newline=csv ? "\n" : "\r\n";
        out<<header;
        if(fft) {
            const auto spec=lvm::compute_recording_spectrum(source,read,names,resources,cancel);
            if(!spec.ok)throw std::runtime_error(spec.error);
            out<<"# [fft_sampling]"<<newline<<"# source_start="<<numfmt(spec.source_start)<<newline<<"# source_end="<<numfmt(spec.source_end)<<newline
                <<"# sample_dt="<<numfmt(spec.sample_dt)<<newline<<"# sample_count="<<spec.n<<newline<<"# resampled="<<spec.resampled<<newline<<"# gaps_ignored="<<spec.gaps_ignored<<newline;
            out<<"Frequency";for(auto c:spec.source_channels)out<<sep<<(csv ? lvm::csv_field(labels[c]) : lvm::tsv_header_field(labels[c]));out<<newline;
            const auto bins=opts.selected_range==ExportRangeMode::Whole ? std::make_pair(std::size_t(0),spec.bin_count()) : spec.bin_range(start,end);
            std::vector<std::vector<double>> values(spec.names.size());
            for(std::size_t at=bins.first;at<bins.second;at+=4096){check(cancel);const auto count=std::min<std::size_t>(4096,bins.second-at);
                for(std::size_t c=0;c<values.size();++c)spec.read_amplitudes(c,at,count,values[c]);
                for(std::size_t k=0;k<count;++k){out<<numfmt(spec.frequency(at+k));for(const auto& c:values)out<<sep<<numfmt(c[k]);out<<newline;}
                if(!out)throw std::runtime_error("Cannot write spectrum export.");}
        } else {
            if(is_lvm) {
                auto timeline=read;timeline.channels.clear();const auto rows=source->replay(timeline,[](const auto&,const auto&){},cancel);
                out<<"LabVIEW Measurement\r\nWriter_Version\t0.92\r\nReader_Version\t1\r\nSeparator\tTab\r\nMulti_Headings\tYes\r\nX_Columns\tMulti\r\nTime_Pref\tAbsolute\r\n***End_of_Header***\r\n\r\nChannels\t"<<labels.size()<<"\r\nSamples";
                for(std::size_t c=0;c<labels.size();++c)out<<'\t'<<rows;
                out<<"\r\n***End_of_Header***\r\n";
                for(std::size_t c=0;c<labels.size();++c){if(c)out<<'\t';out<<"X_Value\t"<<lvm::tsv_header_field(labels[c]);}out<<"\tComment\r\n";
            } else {out<<"Time";for(const auto& label:labels)out<<sep<<(csv ? lvm::csv_field(label) : lvm::tsv_header_field(label));out<<newline;}
            source->replay(read,[&](const auto& time,const auto& values){check(cancel);
                for(std::size_t row=0;row<time.size();++row){if(is_lvm){for(std::size_t c=0;c<values.size();++c){if(c)out<<'\t';out<<numfmt(time[row])<<'\t'<<numfmt(values[c][row]);}out<<'\t';}
                    else {out<<numfmt(time[row]);for(const auto& column:values)out<<sep<<numfmt(column[row]);}out<<newline;}
                if(!out)throw std::runtime_error("Cannot write recording export.");},cancel);
        }
        check(cancel);source->check_current();return static_cast<bool>(out);
    };
}
bool save_source_export(const std::wstring& path,const ExportOptions& opts,const std::atomic<bool>* cancel) {
    try {
        if(!safe_destination(path,*g.recording_source))throw std::runtime_error("Export must not overwrite the open recording source. Choose a different file.");
        auto writer=prepare_source_export(opts);
        return lvm::atomic_write_file(path,[&](auto& out){return writer(out,cancel);},&g.last_error);
    }catch(const std::exception& e){g.last_error=e.what();return false;}
}
bool start_source_export(const std::wstring& path,ExportOptions opts) {
    if(worker.active)return false;
    try {
        if(opts.save_mode==ExportSaveMode::Project){opts.include_metadata=true;opts.include_points=opts.include_markers=opts.include_guides=true;
            opts.include_formulas=opts.include_filter_settings=opts.include_graph_settings=opts.include_channel_names=true;}
        if(!safe_destination(path,*g.recording_source))throw std::runtime_error("Export must not overwrite the open recording source. Choose a different file.");
        auto writer=prepare_source_export(opts);
        worker.cancel=std::make_shared<std::atomic<bool>>(false);worker.path=path;worker.project=opts.save_mode==ExportSaveMode::Project;
        worker.revision=g.project_revision;worker.source=g.recording_source;worker.done=false;worker.error.clear();worker.active=true;
        worker.thread=std::thread([writer=std::move(writer),path,flag=worker.cancel] {
            std::string error;const bool ok=lvm::atomic_write_file(path,[&](auto& out){return writer(out,flag.get());},&error);
            std::lock_guard<std::mutex> lock(worker.mutex);worker.ok=ok;worker.error=std::move(error);worker.done=true;
        });
        show_loading(g_str==&kEn ? L"Exporting original samples…" : L"Экспорт исходных отсчётов…",true);return true;
    }catch(const std::exception& e){worker.active=false;g.last_error=e.what();return false;}
}
void cancel_source_export(){if(worker.cancel)worker.cancel->store(true);}
void poll_source_export() {
    if(!worker.active)return;
    {std::lock_guard<std::mutex> lock(worker.mutex);if(!worker.done)return;}
    worker.thread.join();worker.active=false;hide_loading();
    const bool cancelled=worker.cancel->load();worker.cancel.reset();
    if(!cancelled && worker.ok) {
        bool current=false;try{worker.source->check_current();current=true;}catch(const lvm::RecordingFailure&){}
        if(current && worker.project && worker.source==g.recording_source && worker.revision==g.project_revision){g.project_path=worker.path;mark_active_document_saved();}
        status_msg((g_str==&kEn ? L"Saved: " : L"Сохранено: ")+worker.path);
    } else if(!cancelled) {g.last_error=worker.error;MessageBoxW(g.main,to_w(worker.error).c_str(),g_str->msg_error_title,MB_ICONERROR);}
    else status_msg(g_str==&kEn ? L"Export cancelled." : L"Экспорт отменён.");
    worker.source.reset();
}
void shutdown_source_exports(){cancel_source_export();if(worker.thread.joinable())worker.thread.join();worker.source.reset();worker.active=false;}
}

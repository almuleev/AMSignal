#include "gui_project_state.hpp"
#include "gui_analysis_source.hpp"
#include "gui_commands.hpp"
#include "gui_export_metadata.hpp"
#include "gui_project_results.hpp"
#include "gui_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include <map>

namespace gui {
namespace {
std::map<std::string,std::string> project_section(const std::vector<std::string>& comments,
                                                const std::string& name) {
    std::map<std::string,std::string> values;
    bool inside=false;
    for(const auto& line:comments) {
        if(!line.empty() && line.front()=='[') {inside=line==name;continue;}
        if(!inside)continue;
        const auto eq=line.find('=');
        if(eq!=std::string::npos)values[line.substr(0,eq)]=line.substr(eq+1);
    }
    return values;
}
bool number(const std::map<std::string,std::string>& values,const char* key,double& result) {
    const auto found=values.find(key);
    if(found==values.end() || found->second.empty())return false;
    char* end=nullptr;
    const auto value=std::strtod(found->second.c_str(),&end);
    if(*end || !std::isfinite(value))return false;
    result=value;return true;
}
}

void write_project_workspace(std::ostream& out,const char* nl) {
    write_export_comment(out,L"[workspace]",nl);
    write_export_key_value(out,L"mode",g.mode==AnalysisMode::FRF ? L"frf" : g.mode==AnalysisMode::FFT ? L"fft" : L"time",nl);
    write_export_key_value(out,L"time_start",to_w(numfmt(g.win_start)),nl);
    write_export_key_value(out,L"time_end",to_w(numfmt(g.win_end)),nl);
    if(g.spec_source_valid || g.spec_restore_view_pending || g.ds.frequency_axis) {
        write_export_key_value(out,L"fft_start",to_w(numfmt(g.freq_start)),nl);
        write_export_key_value(out,L"fft_end",to_w(numfmt(g.freq_end)),nl);
    }
    write_export_key_value(out,L"annotations_locked",g.annotations_locked ? L"1" : L"0",nl);
    write_export_key_value(out,L"playhead_active",g.playhead_active ? L"1" : L"0",nl);
    write_export_key_value(out,L"playhead",to_w(numfmt(g.playhead)),nl);
    write_export_comment(out,L"",nl);
    write_project_results(out,nl);
}

void restore_project_workspace(const std::vector<std::string>& comments) {
    if(std::find(comments.begin(),comments.end(),"document_kind=amsignal_project")==comments.end())return;
    const auto values=project_section(comments,"[workspace]");
    const auto bounds=analysis_recording_bounds();
    double low=0,high=0;
    if(number(values,"time_start",low) && number(values,"time_end",high) && high>low) {
        low=std::max(low,bounds.start);high=std::min(high,bounds.end);
        if(high>low) {g.win_start=low;g.win_end=high;}
    }
    if(number(values,"fft_start",low) && number(values,"fft_end",high) && low>=0 && high>low) {
        g.freq_start=low;g.freq_end=high;g.spec_restore_view_pending=true;
    }
    const auto locked=values.find("annotations_locked"),active=values.find("playhead_active");
    if(locked!=values.end() && (locked->second=="0" || locked->second=="1"))g.annotations_locked=locked->second=="1";
    if(number(values,"playhead",low))g.playhead=std::clamp(low,bounds.start,bounds.end);
    if(active!=values.end() && (active->second=="0" || active->second=="1"))g.playhead_active=active->second=="1";
    restore_project_results(comments);
    // Stored-spectrum projects already have a result before workspace restore.
    if(g.spec_valid && g.spec_restore_view_pending)apply_spectrum_result(std::move(g.spec));
    const auto mode=values.find("mode");
    if(mode!=values.end() && !g.ds.frequency_axis) {
        if(mode->second=="fft")set_mode(AnalysisMode::FFT);
        else if(mode->second=="frf")set_mode(AnalysisMode::FRF);
    }
}
} // namespace gui

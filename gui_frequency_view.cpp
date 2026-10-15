#include "gui_frequency_view.hpp"
#include "gui_controls.hpp"
#include "gui_dialogs.hpp"
#include "gui_frf.hpp"
#include "gui_ids.hpp"
#include "gui_processing.hpp"
#include "gui_render.hpp"
#include "gui_state.hpp"
#include "gui_status.hpp"
#include "gui_text.hpp"

namespace gui {
namespace {
bool updating=false,dirty=false;
AnalysisMode draft_mode=AnalysisMode::Time;
const double* draft_data=nullptr;
const lvm::RecordingSource* draft_source=nullptr;
constexpr UINT_PTR edit_subclass=0x46524551;
const wchar_t* tr(const wchar_t* en,const wchar_t* ru){return g_str==&kEn ? en : ru;}
bool limits(double& low,double& high) {
    if(g.mode==AnalysisMode::FRF) {
        const auto& f=g.frf.result.common().frequencies;
        if(!g.frf.result.ok || g.frf.pending || f.size()<2)return false;
        low=f[1];high=f.back();
    } else if(g.mode==AnalysisMode::FFT) {
        if(!g.spec_valid || !g.spec.ok || g.spec_pending || g.spec.bin_count()<2)return false;
        low=0;high=g.spec.nyquist;
    } else {
        if(!has_data() || g.ds.frequency_axis)return false;
        low=g.data_t0;high=g.data_t1;
    }
    return std::isfinite(low) && std::isfinite(high) && high>low;
}
LRESULT CALLBACK edit_proc(HWND edit,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(message==WM_GETDLGCODE)return DefSubclassProc(edit,message,wp,lp)|DLGC_WANTALLKEYS;
    if(message==WM_KEYDOWN && wp==VK_RETURN) {
        SendMessageW(GetParent(edit),WM_COMMAND,MAKEWPARAM(IDC_FREQUENCY_VIEW_APPLY,BN_CLICKED),0);return 0;
    }
    if(message==WM_NCDESTROY)RemoveWindowSubclass(edit,edit_proc,edit_subclass);
    return DefSubclassProc(edit,message,wp,lp);
}
void text(HWND control,const std::wstring& value) {
    if(!control)return;
    wchar_t previous[160]{};GetWindowTextW(control,previous,160);
    if(value!=previous)SetWindowTextW(control,value.c_str());
}
}
const wchar_t* frequency_view_title_text(){return tr(L"Visible range",L"Видимый диапазон");}
const wchar_t* frequency_view_apply_text(){return tr(L"Show range",L"Показать диапазон");}

void create_frequency_view_controls(HWND parent,HINSTANCE instance) {
    updating=false;dirty=false;draft_data=nullptr;draft_source=nullptr;draft_mode=AnalysisMode::Time;
    const auto make=[&](const wchar_t* cls,DWORD style,int id) {
        HWND control=CreateWindowExW(0,cls,L"",WS_CHILD|WS_CLIPSIBLINGS|style,0,0,10,10,parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(g.ui_font ? g.ui_font : GetStockObject(DEFAULT_GUI_FONT)),FALSE);
        g.side_frequency_controls.push_back(control);return control;
    };
    g.side_frequency_title=make(L"STATIC",SS_LEFT|SS_NOPREFIX,0);
    g.side_frequency_low_label=make(L"STATIC",SS_LEFT|SS_NOPREFIX,0);
    g.side_frequency_high_label=make(L"STATIC",SS_LEFT|SS_NOPREFIX,0);
    g.side_frequency_low=make(L"EDIT",WS_TABSTOP|ES_AUTOHSCROLL,IDC_FREQUENCY_VIEW_LOW);
    g.side_frequency_high=make(L"EDIT",WS_TABSTOP|ES_AUTOHSCROLL,IDC_FREQUENCY_VIEW_HIGH);
    g.side_frequency_apply=make(L"BUTTON",WS_TABSTOP|BS_OWNERDRAW,IDC_FREQUENCY_VIEW_APPLY);
    for(HWND edit:{g.side_frequency_low,g.side_frequency_high}) {
        install_compact_themed_edit(edit);SetWindowSubclass(edit,edit_proc,edit_subclass,0);
        SendMessageW(edit,EM_SETLIMITTEXT,127,0);
    }
    sync_frequency_view_controls();
}

void sync_frequency_view_controls() {
    if(!g.side_frequency_apply)return;
    if(draft_mode!=g.mode || draft_data!=g.ds.time.data() || draft_source!=g.recording_source.get()) {
        dirty=false;draft_mode=g.mode;draft_data=g.ds.time.data();draft_source=g.recording_source.get();
    }
    updating=true;
    text(g.side_frequency_title,frequency_view_title_text());
    text(g.side_frequency_low_label,g.mode==AnalysisMode::Time ? tr(L"From, s",L"От, с") : tr(L"From, Hz",L"От, Гц"));
    text(g.side_frequency_high_label,g.mode==AnalysisMode::Time ? tr(L"To, s",L"До, с") : tr(L"To, Hz",L"До, Гц"));
    text(g.side_frequency_apply,frequency_view_apply_text());
    double minimum=0,maximum=0;const bool ready=limits(minimum,maximum);
    for(HWND control:{g.side_frequency_low,g.side_frequency_high,g.side_frequency_apply})
        if((IsWindowEnabled(control)!=FALSE)!=ready)EnableWindow(control,ready);
    if(!dirty) {
        text(g.side_frequency_low,ready ? format_edit_number(g.mode==AnalysisMode::FRF ? g.frf.frequency_start : g.mode==AnalysisMode::Time ? g.win_start : g.freq_start) : L"");
        text(g.side_frequency_high,ready ? format_edit_number(g.mode==AnalysisMode::FRF ? g.frf.frequency_end : g.mode==AnalysisMode::Time ? g.win_end : g.freq_end) : L"");
    }
    updating=false;
}

void layout_frequency_view_controls(int x,int y,int width,int viewport_top,int viewport_bottom,bool visible) {
    const int half=(width-12)/2;
    const auto place=[&](HWND control,int dx,int dy,int w,int h) {
        if(!control)return;
        const bool show=visible && y+dy>=viewport_top && y+dy+h<=viewport_bottom;
        if(show)MoveWindow(control,x+dx,y+dy,w,h,FALSE);
        ShowWindow(control,show ? SW_SHOW : SW_HIDE);
    };
    place(g.side_frequency_title,0,0,width,20);
    place(g.side_frequency_low_label,0,28,half,18);place(g.side_frequency_high_label,half+12,28,half,18);
    place(g.side_frequency_low,0,50,half,28);place(g.side_frequency_high,half+12,50,half,28);
    place(g.side_frequency_apply,0,86,width,28);
}

bool set_visible_frequency_range(double low,double high) {
    double minimum=0,maximum=0;
    if(!limits(minimum,maximum) || !std::isfinite(low) || !std::isfinite(high) || high<=low ||
        low<minimum-std::abs(minimum)*1e-9 || high>maximum+std::abs(maximum)*1e-9)return false;
    low=std::max(minimum,low);high=std::min(maximum,high);if(!(high>low))return false;
    if(g.mode==AnalysisMode::FRF)return set_frf_frequency_range(low,high);
    if(g.mode==AnalysisMode::Time) {g.win_start=low;g.win_end=high;}
    else {g.freq_start=low;g.freq_end=high;}
    sync_frequency_view_controls();set_status();if(g.main)invalidate_plot();return true;
}

bool handle_frequency_view_command(HWND owner,WPARAM wp) {
    const int id=LOWORD(wp),code=HIWORD(wp);
    if(id==IDC_FREQUENCY_VIEW_LOW || id==IDC_FREQUENCY_VIEW_HIGH) {
        if(code==EN_CHANGE && !updating)dirty=true;
        return true;
    }
    if(id!=IDC_FREQUENCY_VIEW_APPLY)return false;
    if(code!=BN_CLICKED && code!=BN_DOUBLECLICKED)return true;
    wchar_t low[128]{},high[128]{};double a=0,b=0;
    GetWindowTextW(g.side_frequency_low,low,128);GetWindowTextW(g.side_frequency_high,high,128);
    if(!parse_wide_double_text(low,a) || !parse_wide_double_text(high,b) || !set_visible_frequency_range(a,b)) {
        show_styled_info_prompt(owner,frequency_view_title_text(),tr(L"Enter From < To within the available range.",
            L"Укажите «От» меньше «До» в пределах доступного диапазона."),true);
        return true;
    }
    dirty=false;sync_frequency_view_controls();return true;
}
}

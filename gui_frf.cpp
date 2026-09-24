#include "gui_frf.hpp"
#include "gui_analysis_source.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include "gui_processing.hpp"
#include "gui_render.hpp"
#include "gui_status.hpp"
#include "gui_menu.hpp"
#include "gui_controls.hpp"
#include "gui_side_panel.hpp"
#include "gui_settings_window.hpp"
#include "gui_ids.hpp"
#include "gui_export.hpp"

namespace gui {
lvm::FrfWorker g_frf_worker;
void show_frf_channel_menu(bool supports);
void set_frf_channels_from_menu(bool supports,std::vector<int> selection);
void clear_frf_channel_selection(bool supports);
namespace {
enum {
    InputLabel = 7100, Input, InputSummary, OutputLabel, Output, Processing,
    LowLabel, Low, HighLabel, High, ApplyRange, Method, Source,
    Calculate, Csv, Png, Hint, EstimatorLabel, Estimator, LengthLabel, Length,
    SmoothingLabel, Smoothing, AxisScaleLabel, AxisScale, Reference
};
constexpr double smoothing_choices[] = {0, 1.0/24.0, 1.0/12.0, 1.0/6.0, 1.0/3.0};
int smoothing_choice(double octaves) {
    for (int i=0;i<5;++i)
        if (std::fabs(octaves-smoothing_choices[i])<1e-12) return i;
    return 0;
}
HWND control(int id) { return g.frf_panel ? GetDlgItem(g.frf_panel, id) : nullptr; }
const wchar_t* tr(const wchar_t* en, const wchar_t* ru) { return g_str == &kEn ? en : ru; }
void label(int id, const wchar_t* value) { if (HWND h = control(id)) SetWindowTextW(h, value); }
WNDPROC g_frf_edit_proc = nullptr;

LRESULT CALLBACK FrfEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_GETDLGCODE) {
        return CallWindowProcW(g_frf_edit_proc, hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        HWND panel = GetParent(hwnd);
        const int action = GetDlgCtrlID(hwnd) == Length ? Calculate : ApplyRange;
        SendMessageW(panel, WM_COMMAND, MAKEWPARAM(action, BN_CLICKED),
                     reinterpret_cast<LPARAM>(GetDlgItem(panel, action)));
        return 0;
    }
    return CallWindowProcW(g_frf_edit_proc, hwnd, msg, wp, lp);
}
bool read_segment_length() {
    wchar_t text[64]{};
    GetWindowTextW(control(Length), text, 64);
    double value=0;
    if (!parse_wide_double_text(text,value) || !std::isfinite(value) || value<0 || value>100000000 ||
        std::floor(value)!=value || (value!=0 && (value<4 || std::fmod(value,2)!=0))) {
        MessageBoxW(g.frf_panel, tr(L"Use 0 (Auto), or an even L >= 4.", L"Укажите 0 (Auto) или чётное L ≥ 4."), L"FRF", MB_OK);
        return false;
    }
    g.frf.options.segment_length=static_cast<std::size_t>(value);
    return true;
}
LRESULT CALLBACK FrfPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.frf_panel = hwnd;
            HINSTANCE inst = reinterpret_cast<CREATESTRUCTW*>(lp)->hInstance;
            auto make = [&](int id, const wchar_t* cls, DWORD style, int x, int y, int w, int h) {
                HWND c = CreateWindowExW(0, cls, L"", WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g.ui_font), TRUE);
            };
            make(InputLabel, L"STATIC", SS_LEFT, 12, 4, 278, 18);
            make(Input, L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, 12, 22, 278, 24);
            make(InputSummary, L"STATIC", SS_LEFT, 12, 47, 278, 24);
            // The active UI font has descenders that do not fit in a standard
            // 18px static control. Reserve their full line box before Outputs.
            make(OutputLabel, L"STATIC", SS_LEFT, 12, 74, 278, 18);
            make(Output, L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, 12, 92, 278, 24);
            make(Processing, L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, 12, 120, 278, 28);
            make(EstimatorLabel, L"STATIC", SS_LEFT, 12, 152, 130, 18);
            make(LengthLabel, L"STATIC", SS_LEFT, 156, 152, 134, 18);
            make(Estimator, L"COMBOBOX", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | CBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, 12, 172, 130, 200);
            make(Length, L"EDIT", WS_BORDER | ES_NUMBER | WS_TABSTOP, 156, 172, 134, 24);
            make(Method, L"STATIC", SS_LEFT, 12, 202, 278, 44);
            make(SmoothingLabel, L"STATIC", SS_LEFT, 12, 250, 92, 18);
            make(Smoothing, L"COMBOBOX", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | CBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, 108, 248, 182, 200);
            make(AxisScaleLabel, L"STATIC", SS_LEFT, 12, 276, 92, 18);
            make(AxisScale, L"COMBOBOX", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | CBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, 108, 274, 182, 100);
            make(Reference, L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, 12, 300, 278, 24);
            make(Source, L"STATIC", SS_LEFT, 12, 326, 278, 28);
            make(LowLabel, L"STATIC", SS_LEFT, 12, 358, 130, 18);
            make(HighLabel, L"STATIC", SS_LEFT, 156, 358, 134, 18);
            make(Low, L"EDIT", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 12, 378, 130, 24);
            make(High, L"EDIT", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 156, 378, 134, 24);
            make(ApplyRange, L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, 12, 408, 278, 24);
            make(Calculate, L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, 12, 438, 122, 24);
            make(Csv, L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, 140, 438, 72, 24);
            make(Png, L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, 218, 438, 72, 24);
            make(Hint, L"STATIC", SS_LEFT, 12, 470, 278, 80);
            install_themed_combo(control(Estimator));
            install_themed_combo(control(Smoothing));
            install_themed_combo(control(AxisScale));
            for (const int id : {Length, Low, High}) {
                if (HWND edit = control(id)) {
                    WNDPROC previous = reinterpret_cast<WNDPROC>(
                        SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(FrfEditProc)));
                    if (!g_frf_edit_proc) g_frf_edit_proc = previous;
                }
            }
            SendMessageW(control(Estimator), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"H1 (Welch)"));
            SendMessageW(control(Estimator), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Direct Y/X"));
            const wchar_t* const smoothing_en[] = {L"Off",L"1/24 octave",L"1/12 octave",L"1/6 octave",L"1/3 octave"};
            const wchar_t* const smoothing_ru[] = {L"Без сглаживания",L"1/24 октавы",L"1/12 октавы",L"1/6 октавы",L"1/3 октавы"};
            const wchar_t* const* smoothing_text = g_str == &kEn ? smoothing_en : smoothing_ru;
            for (const wchar_t* choice : {smoothing_text[0], smoothing_text[1], smoothing_text[2], smoothing_text[3], smoothing_text[4]})
                SendMessageW(control(Smoothing),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(choice));
            SendMessageW(control(AxisScale),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(tr(L"Logarithmic",L"Логарифмическая")));
            SendMessageW(control(AxisScale),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(tr(L"Linear",L"Линейная")));
            refresh_frf_controls(true);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (id==Estimator && code==CBN_SELCHANGE) {
                if (!read_segment_length()) {
                    SendMessageW(control(Estimator),CB_SETCURSEL,g.frf.options.estimator==lvm::FrfEstimator::H1 ? 0 : 1,0);
                    return 0;
                }
                g.frf.options.estimator=SendMessageW(control(Estimator),CB_GETCURSEL,0,0)==1 ?
                    lvm::FrfEstimator::Direct : lvm::FrfEstimator::H1;
                compute_frf_from_current_source();
                return 0;
            }
            if (id==Smoothing && code==CBN_SELCHANGE) {
                const int choice=static_cast<int>(SendMessageW(control(Smoothing),CB_GETCURSEL,0,0));
                if (choice>=0 && choice<5)
                    g.frf.display_smoothing_octaves=smoothing_choices[choice];
                refresh_frf_controls(); set_status(); invalidate_plot();
                return 0;
            }
            if (id==AxisScale && code==CBN_SELCHANGE) {
                g.frf.logarithmic_frequency_axis=SendMessageW(control(AxisScale),CB_GETCURSEL,0,0)!=1;
                sync_frf_frequency_limits();
                refresh_frf_controls(); set_status(); invalidate_plot();
                return 0;
            }
            if ((id==Input || id==Output) && code==BN_CLICKED) {
                show_frf_channel_menu(id==Input);
                return 0;
            }
            if (code != BN_CLICKED && code != BN_DOUBLECLICKED) return 0;
            if (id == Processing) {
                g.frf.apply_processing = !g.frf.apply_processing;
                invalidate_frf();
                compute_frf_from_current_source();
            } else if (id == Reference) {
                g.frf.show_reference_amplitude=!g.frf.show_reference_amplitude;
                refresh_frf_controls(); invalidate_plot();
            } else if (id == Calculate) {
                if (read_segment_length()) compute_frf_from_current_source();
            } else if (id == ApplyRange) {
                wchar_t low[80]{}, high[80]{};
                GetWindowTextW(control(Low), low, 80); GetWindowTextW(control(High), high, 80);
                double a = 0, b = 0;
                if (!parse_wide_double_text(low, a) || !parse_wide_double_text(high, b) ||
                    !set_frf_frequency_range(a, b)) {
                    MessageBoxW(hwnd, tr(L"Use 0 < F min < F max within the calculated frequency range.",
                        L"Укажите 0 < F min < F max в пределах рассчитанных частот."), L"FRF", MB_OK | MB_ICONINFORMATION);
                }
            } else if (id == Csv) {
                save_as_dialog();
            } else if (id == Png) {
                save_png_dialog();
            }
            return 0;
        }
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis || !dis->hwndItem) break;
            if (dis->CtlType == ODT_COMBOBOX && (dis->CtlID == Estimator || dis->CtlID == Smoothing || dis->CtlID == AxisScale)) {
                draw_settings_combo_item(dis);
                return TRUE;
            }
            wchar_t text[128]{}; GetWindowTextW(dis->hwndItem, text, 128);
            if (dis->CtlID == Processing || dis->CtlID == Reference) {
                draw_themed_check_control(dis->hDC, dis->rcItem, text,
                    dis->CtlID == Processing ? g.frf.apply_processing : g.frf.show_reference_amplitude,
                    (dis->itemState & ODS_SELECTED) != 0, true, false, false);
            } else {
                draw_themed_button(dis->hDC, dis->rcItem, text,
                    (dis->itemState & ODS_SELECTED) != 0, false, false);
            }
            return TRUE;
        }
        case WM_MEASUREITEM: {
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (mis && mis->CtlType == ODT_COMBOBOX && (mis->CtlID == Estimator || mis->CtlID == Smoothing || mis->CtlID == AxisScale)) {
                measure_settings_combo_item(mis);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, g_theme->text_primary); SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, g_theme->text_primary); SetBkColor(dc, g_theme->bg_plot);
            return reinterpret_cast<LRESULT>(g_input_brush);
        }
        case WM_ERASEBKGND: {
            RECT r; GetClientRect(hwnd, &r);
            FillRect(reinterpret_cast<HDC>(wp), &r, g_panel_brush);
            return 1;
        }
        case WM_DESTROY: g.frf_panel = nullptr; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

std::wstring frf_error_text(lvm::FrfError e) {
    if (e==lvm::FrfError::InvalidChannels) return tr(
        L"Select at least one Support and Response without shared channels.",
        L"Выберите хотя бы одну опору и отклик без общих каналов.");
    if (g_str == &kEn) return to_w(lvm::frf_error_text(e));
    switch (e) {
        case lvm::FrfError::None: return L"";
        case lvm::FrfError::InvalidChannels: break;
        case lvm::FrfError::FrequencyData: return L"Для FRF нужны исходные сигналы во времени.";
        case lvm::FrfError::TooShort: return L"В выбранном участке меньше L отсчётов. Уменьшите L или расширьте участок.";
        case lvm::FrfError::InvalidTime: return L"Временные метки должны быть конечными и строго возрастающими.";
        case lvm::FrfError::MissingValues: return L"В выбранных каналах есть пропущенные или бесконечные значения.";
        case lvm::FrfError::InvalidOptions: return L"Некорректные параметры FRF.";
        case lvm::FrfError::WeakReference: return L"Во входном канале нет достаточного переменного сигнала.";
        case lvm::FrfError::Overflow: return L"Превышены численные ограничения или недостаточно памяти для FRF.";
    }
    return L"Не удалось рассчитать FRF.";
}

namespace {
bool valid_selection(std::vector<int> references,std::vector<int> responses) {
    const int count=static_cast<int>(g.ds.channel_count());
    if (references.empty() || responses.empty()) return false;
    for (auto* list:{&references,&responses}) {
        std::sort(list->begin(),list->end());
        if (list->front()<0 || list->back()>=count || std::adjacent_find(list->begin(),list->end())!=list->end()) return false;
    }
    for (int channel:references) if (std::binary_search(responses.begin(),responses.end(),channel)) return false;
    return true;
}
}

namespace {
struct FrfRoleMenu {
    HWND window=nullptr;
    HFONT font=nullptr;
    bool owns_font=false;
    bool supports=true;
    int width=0, row_height=0, channel_rows=0, clear_top=0, hot_row=-1;
} g_frf_role_menu;
int g_frf_role_menu_suppressed_role=-1;

bool role_menu_disabled(int channel) {
    const auto& opposite=g_frf_role_menu.supports ? g.frf.outputs : g.frf.inputs;
    return std::find(opposite.begin(),opposite.end(),channel)!=opposite.end();
}
void close_frf_role_menu() {
    if (g_frf_role_menu.window) DestroyWindow(g_frf_role_menu.window);
}
bool cursor_over_role_control(bool supports) {
    RECT bounds{};
    if (!GetWindowRect(control(supports ? Input : Output),&bounds)) return false;
    POINT cursor{}; GetCursorPos(&cursor);
    return PtInRect(&bounds,cursor)!=FALSE;
}
int role_menu_row(POINT point) {
    const int row=(point.y-2)/g_frf_role_menu.row_height;
    if (point.y>=2 && row>=0 && row<g_frf_role_menu.channel_rows) return row;
    if (point.y>=g_frf_role_menu.clear_top &&
        point.y<g_frf_role_menu.clear_top+g_frf_role_menu.row_height) return -2;
    return -1;
}
void draw_role_menu(HWND window,HDC dc) {
    RECT area{}; GetClientRect(window,&area);
    const bool dark=g_theme==&kDarkTheme;
    HTHEME theme=dark ? nullptr : OpenThemeData(window,L"MENU");
    if (theme) {
        DrawThemeBackground(theme,dc,MENU_POPUPBACKGROUND,0,&area,nullptr);
        DrawThemeBackground(theme,dc,MENU_POPUPBORDERS,0,&area,nullptr);
    } else {
        HBRUSH background=CreateSolidBrush(dark ? g_theme->bg_panel : GetSysColor(COLOR_MENU));
        FillRect(dc,&area,background); DeleteObject(background);
        if (dark) {
            HBRUSH border=CreateSolidBrush(g_theme->frame);
            FrameRect(dc,&area,border); DeleteObject(border);
        }
    }
    HGDIOBJ previous=SelectObject(dc,g_frf_role_menu.font);
    SetBkMode(dc,TRANSPARENT);
    const auto& selected=g_frf_role_menu.supports ? g.frf.inputs : g.frf.outputs;
    for (int channel=0;channel<g_frf_role_menu.channel_rows;++channel) {
        RECT item{2,2+channel*g_frf_role_menu.row_height,g_frf_role_menu.width-2,
            2+(channel+1)*g_frf_role_menu.row_height};
        const bool disabled=role_menu_disabled(channel);
        const bool checked=std::find(selected.begin(),selected.end(),channel)!=selected.end();
        const bool hot=channel==g_frf_role_menu.hot_row;
        if (theme) {
            DrawThemeBackground(theme,dc,MENU_POPUPITEM,
                disabled ? (hot ? MPI_DISABLEDHOT : MPI_DISABLED) : (hot ? MPI_HOT : MPI_NORMAL),&item,nullptr);
        } else if (hot && !disabled) {
            HBRUSH hover=CreateSolidBrush(dark ? g_theme->btn_hover : GetSysColor(COLOR_HIGHLIGHT));
            FillRect(dc,&item,hover); DeleteObject(hover);
        }
        RECT check{item.left+3,item.top+2,item.left+23,item.bottom-2};
        if (theme) {
            DrawThemeBackground(theme,dc,MENU_POPUPCHECKBACKGROUND,disabled ? MCB_DISABLED : MCB_NORMAL,&check,nullptr);
            if (checked) DrawThemeBackground(theme,dc,MENU_POPUPCHECK,disabled ? MC_CHECKMARKDISABLED : MC_CHECKMARKNORMAL,&check,nullptr);
        } else if (dark) {
            HBRUSH frame=CreateSolidBrush(g_theme->frame);
            FrameRect(dc,&check,frame); DeleteObject(frame);
            if (checked) {
                HPEN pen=CreatePen(PS_SOLID,2,disabled ? g_theme->text_secondary : g_theme->accent);
                HGDIOBJ old_pen=SelectObject(dc,pen);
                MoveToEx(dc,check.left+4,check.top+(check.bottom-check.top)/2,nullptr);
                LineTo(dc,check.left+8,check.bottom-5);
                LineTo(dc,check.right-4,check.top+4);
                SelectObject(dc,old_pen); DeleteObject(pen);
            }
        } else {
            DrawFrameControl(dc,&check,DFC_BUTTON,DFCS_BUTTONCHECK|(checked ? DFCS_CHECKED : 0)|(disabled ? DFCS_INACTIVE : 0));
        }
        SetTextColor(dc,dark ? (disabled ? g_theme->text_secondary : g_theme->text_primary) :
            GetSysColor(disabled ? COLOR_GRAYTEXT : (hot ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT)));
        const std::wstring name=std::to_wstring(channel+1)+L": "+channel_display_label(channel);
        RECT text{check.right+4,item.top,item.right-6,item.bottom};
        DrawTextW(dc,name.c_str(),-1,&text,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    RECT separator{2,g_frf_role_menu.clear_top-5,g_frf_role_menu.width-2,g_frf_role_menu.clear_top-1};
    if (theme) DrawThemeBackground(theme,dc,MENU_POPUPSEPARATOR,0,&separator,nullptr);
    else {
        HBRUSH separator_brush=CreateSolidBrush(dark ? g_theme->separator : GetSysColor(COLOR_MENU));
        FillRect(dc,&separator,separator_brush); DeleteObject(separator_brush);
    }
    const bool can_clear=!selected.empty();
    const bool clear_hot=g_frf_role_menu.hot_row==-2;
    RECT clear{2,g_frf_role_menu.clear_top,g_frf_role_menu.width-2,
        g_frf_role_menu.clear_top+g_frf_role_menu.row_height};
    if (theme) {
        DrawThemeBackground(theme,dc,MENU_POPUPITEM,
            can_clear ? (clear_hot ? MPI_HOT : MPI_NORMAL) : (clear_hot ? MPI_DISABLEDHOT : MPI_DISABLED),&clear,nullptr);
    } else if (clear_hot && can_clear) {
        HBRUSH hover=CreateSolidBrush(dark ? g_theme->btn_hover : GetSysColor(COLOR_HIGHLIGHT));
        FillRect(dc,&clear,hover); DeleteObject(hover);
    }
    SetTextColor(dc,dark ? (can_clear ? g_theme->text_primary : g_theme->text_secondary) :
        GetSysColor(can_clear ? (clear_hot ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT) : COLOR_GRAYTEXT));
    RECT clear_text{27,clear.top,clear.right-6,clear.bottom};
    DrawTextW(dc,tr(L"Clear",L"Очистить"),-1,&clear_text,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(dc,previous);
    if (theme) CloseThemeData(theme);
}
LRESULT CALLBACK FrfRoleMenuProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch (message) {
        case WM_CREATE: {
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{}; HDC dc=BeginPaint(window,&paint);
            draw_role_menu(window,dc); EndPaint(window,&paint);
            return 0;
        }
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};
            TrackMouseEvent(&track);
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            const int row=role_menu_row(point);
            if (row!=g_frf_role_menu.hot_row) {
                g_frf_role_menu.hot_row=row;
                InvalidateRect(window,nullptr,FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_frf_role_menu.hot_row!=-1) {
                g_frf_role_menu.hot_row=-1;
                InvalidateRect(window,nullptr,FALSE);
            }
            return 0;
        case WM_LBUTTONUP: {
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            const int channel=role_menu_row(point);
            if (channel==-2) {
                clear_frf_channel_selection(g_frf_role_menu.supports);
                InvalidateRect(window,nullptr,FALSE);
            } else if (channel>=0 && !role_menu_disabled(channel)) {
                auto selected=g_frf_role_menu.supports ? g.frf.inputs : g.frf.outputs;
                auto it=std::find(selected.begin(),selected.end(),channel);
                if (it==selected.end()) selected.push_back(channel); else selected.erase(it);
                set_frf_channels_from_menu(g_frf_role_menu.supports,std::move(selected));
                InvalidateRect(window,nullptr,FALSE);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wp==VK_ESCAPE) { close_frf_role_menu(); return 0; }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_ACTIVATE:
            if (LOWORD(wp)==WA_INACTIVE) {
                if (cursor_over_role_control(g_frf_role_menu.supports))
                    g_frf_role_menu_suppressed_role=g_frf_role_menu.supports ? 1 : 0;
                PostMessageW(window,WM_CLOSE,0,0);
            }
            return 0;
        case WM_CLOSE: DestroyWindow(window); return 0;
        case WM_DESTROY:
            if (g_frf_role_menu.owns_font) DeleteObject(g_frf_role_menu.font);
            g_frf_role_menu={};
            return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
}

void show_frf_channel_menu(bool supports) {
    const int role=supports ? 1 : 0;
    if (g_frf_role_menu_suppressed_role==role) {
        g_frf_role_menu_suppressed_role=-1;
        return;
    }
    g_frf_role_menu_suppressed_role=-1;
    if (g_frf_role_menu.window) {
        if (g_frf_role_menu.supports==supports) { close_frf_role_menu(); return; }
        close_frf_role_menu();
    }
    static bool registered=false;
    if (!registered) {
        WNDCLASSW window_class{}; window_class.hInstance=GetModuleHandleW(nullptr);
        window_class.lpfnWndProc=FrfRoleMenuProc; window_class.hCursor=LoadCursor(nullptr,IDC_ARROW);
        window_class.lpszClassName=L"AMSignalFrfRoleMenu";
        RegisterClassW(&window_class); registered=true;
    }
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize=sizeof(metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0);
    g_frf_role_menu.font=CreateFontIndirectW(&metrics.lfMenuFont);
    g_frf_role_menu.owns_font=g_frf_role_menu.font!=nullptr;
    if (!g_frf_role_menu.font) g_frf_role_menu.font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    RECT anchor{}; GetWindowRect(control(supports ? Input : Output),&anchor);
    HDC dc=GetDC(nullptr); HGDIOBJ previous=SelectObject(dc,g_frf_role_menu.font);
    int widest=anchor.right-anchor.left;
    for (std::size_t c=0;c<g.ds.channel_count();++c) {
        const std::wstring name=std::to_wstring(c+1)+L": "+channel_display_label(static_cast<int>(c));
        SIZE size{}; GetTextExtentPoint32W(dc,name.c_str(),static_cast<int>(name.size()),&size);
        widest=std::max(widest,static_cast<int>(size.cx)+40);
    }
    SelectObject(dc,previous); ReleaseDC(nullptr,dc);
    g_frf_role_menu.supports=supports;
    g_frf_role_menu.width=std::min(std::max(widest,180),500);
    g_frf_role_menu.row_height=std::max(GetSystemMetrics(SM_CYMENU),20);
    g_frf_role_menu.channel_rows=static_cast<int>(g.ds.channel_count());
    g_frf_role_menu.clear_top=2+g_frf_role_menu.channel_rows*g_frf_role_menu.row_height+5;
    const int height=g_frf_role_menu.clear_top+g_frf_role_menu.row_height+2;
    g_frf_role_menu.window=CreateWindowExW(WS_EX_TOOLWINDOW,L"AMSignalFrfRoleMenu",L"",WS_POPUP|WS_BORDER,
        anchor.left,anchor.bottom,g_frf_role_menu.width,height,g.main,nullptr,GetModuleHandleW(nullptr),nullptr);
    if (g_frf_role_menu.window) {
        HRGN rounded=CreateRoundRectRgn(0,0,g_frf_role_menu.width+1,height+1,10,10);
        if (!SetWindowRgn(g_frf_role_menu.window,rounded,TRUE)) DeleteObject(rounded);
        ShowWindow(g_frf_role_menu.window,SW_SHOWNORMAL);
    }
}

bool set_frf_channels(std::vector<int> references,std::vector<int> responses) {
    if (!valid_selection(references,responses)) return false;
    std::sort(references.begin(),references.end()); std::sort(responses.begin(),responses.end());
    if (references==g.frf.inputs && responses==g.frf.outputs) return true;
    g.frf.inputs=std::move(references); g.frf.outputs=std::move(responses);
    invalidate_frf();
    if (g.mode==AnalysisMode::FRF) compute_frf_from_current_source();
    refresh_frf_controls();
    return true;
}
void set_frf_channels_from_menu(bool supports,std::vector<int> selection) {
    std::sort(selection.begin(),selection.end());
    selection.erase(std::unique(selection.begin(),selection.end()),selection.end());
    auto references=supports ? selection : g.frf.inputs;
    auto responses=supports ? g.frf.outputs : selection;
    const auto& other=supports ? responses : references;
    selection.erase(std::remove_if(selection.begin(),selection.end(),[&](int channel) {
        return std::find(other.begin(),other.end(),channel)!=other.end();
    }),selection.end());
    if (supports) references=selection; else responses=selection;
    if (references==g.frf.inputs && responses==g.frf.outputs) return;
    g.frf.inputs=std::move(references); g.frf.outputs=std::move(responses);
    invalidate_frf();
    if (g.mode==AnalysisMode::FRF) compute_frf_from_current_source();
    refresh_frf_controls();
}
void clear_frf_channel_selection(bool supports) {
    set_frf_channels_from_menu(supports,{});
}
std::wstring frf_curve_label(std::size_t response) {
    if (response>=g.frf.output_names.size()) return L"";
    return g.frf.output_names[response]+L" / "+g.frf.input_name;
}

void invalidate_frf(bool reset_view) {
    g_frf_worker.cancel();
    ++g.frf.generation;
    g.frf.pending = false; g.frf.attempted = false;
    g.frf.result = {};
    if (reset_view) { g.frf.view_initialized = false; g.frf.auto_y = true; }
}

void compute_frf_from_current_source() {
    invalidate_frf();
    g.frf.attempted = true;
    double a = 0, b = 0; bool selected = false;
    lvm::FrfBatchResult failure;
    failure.options=g.frf.options;
    if (g.ds.frequency_axis) failure.error = lvm::FrfError::FrequencyData;
    else if (!valid_selection(g.frf.inputs,g.frf.outputs)) failure.error=lvm::FrfError::InvalidChannels;
    else if (!current_fft_source_window(a, b, selected)) failure.error = lvm::FrfError::TooShort;
    if (failure.error != lvm::FrfError::None) { apply_frf_result(std::move(failure)); return; }
    g.frf.source_start = a; g.frf.source_end = b; g.frf.from_selection = selected;
    g.frf.input_name.clear(); g.frf.output_names.clear();
    for (int c:g.frf.inputs) {
        if (!g.frf.input_name.empty()) g.frf.input_name+=L", ";
        g.frf.input_name+=channel_display_label(c);
    }
    if (g.frf.inputs.size()>1) g.frf.input_name=L"AVG("+g.frf.input_name+L")";
    for (int c:g.frf.outputs) g.frf.output_names.push_back(channel_display_label(c));
    try {
        lvm::Dataset pair;
        std::vector<std::size_t> channels(g.frf.inputs.begin(),g.frf.inputs.end());
        channels.insert(channels.end(),g.frf.outputs.begin(),g.frf.outputs.end());
        build_time_window_dataset(g.ds, a, b, pair, &channels, g.frf.apply_processing);
        lvm::FrfBatchInput input; input.time=std::move(pair.time);
        for (std::size_t i=0;i<pair.channels.size();++i) {
            auto& destination=i<g.frf.inputs.size() ? input.references : input.responses;
            destination.push_back(std::move(pair.channels[i]));
        }
        g.frf.processing_description = L"raw";
        if (g.frf.apply_processing) {
            g.frf.processing_description = L"global=" + g.global_formula +
                L"; filter=" + std::to_wstring(g.noise_threshold_enabled) +
                L"; mode=" + std::to_wstring(g.noise_threshold_mode) +
                L"; topology=" + std::to_wstring(g.noise_threshold_topology) +
                L"; low=" + format_optional_edit_number(g.noise_threshold_min) +
                L"; high=" + format_optional_edit_number(g.noise_threshold_max);
            for (auto c:channels) g.frf.processing_description+=L"; channel["+std::to_wstring(c)+L"]="+g.channel_formulas[c];
        }
        if (g.main) {
            g.frf.pending = true;
            g_frf_worker.submit(std::move(input), g.frf.options, g.frf.generation);
            refresh_frf_controls(); set_status(); invalidate_plot();
        } else {
            apply_frf_result(lvm::analyze_frf_batch(std::move(input), g.frf.options));
        }
    } catch (...) {
        failure.error = lvm::FrfError::Overflow;
        apply_frf_result(std::move(failure));
    }
}

bool ensure_current_frf() {
    double start = 0, end = 0; bool selected = false;
    if (g.frf.attempted && !g.frf.pending &&
        (g.frf.result.options.estimator!=g.frf.options.estimator ||
         g.frf.result.options.segment_length!=g.frf.options.segment_length))
        invalidate_frf();
    if (g.frf.attempted && current_fft_source_window(start, end, selected) &&
        (start != g.frf.source_start || end != g.frf.source_end || selected != g.frf.from_selection))
        invalidate_frf();
    if (!g.frf.attempted) compute_frf_from_current_source();
    return g.frf.result.ok && !g.frf.pending;
}

void apply_frf_result(lvm::FrfBatchResult result) {
    g.frf.result = std::move(result); g.frf.pending = false;
    if (g.frf.result.ok) {
        const auto& f = g.frf.result.common().frequencies;
        const double lo = std::log10(f[1]), hi = std::log10(f.back());
        if (!g.frf.view_initialized || g.frf.log_end <= lo || g.frf.log_start >= hi) {
            g.frf.log_start = lo; g.frf.log_end = hi;
        } else {
            g.frf.log_start = std::max(lo, g.frf.log_start);
            g.frf.log_end = std::min(hi, g.frf.log_end);
        }
        g.frf.frequency_start=std::pow(10.0,g.frf.log_start);
        g.frf.frequency_end=std::pow(10.0,g.frf.log_end);
        g.frf.view_initialized = true;
    }
    refresh_frf_controls();
    if (g.mode == AnalysisMode::FRF) { set_status(); if (g.main) invalidate_plot(); }
}

void poll_frf_result() {
    if (auto r = g_frf_worker.take_result())
        if (r->generation == g.frf.generation) apply_frf_result(std::move(r->frf));
}

void on_frf_processing_changed() {
    refresh_frf_controls(true);
    if (!g.frf.apply_processing) return;
    invalidate_frf();
    if (g.mode == AnalysisMode::FRF) compute_frf_from_current_source();
}

std::wstring frf_status_text() {
    if (g.frf.pending) return tr(L"Calculating FRF…", L"Вычисление АЧХ…");
    if (!g.frf.result.ok) return gui::frf_error_text(g.frf.result.error);
    wchar_t buf[256]{};
    const auto& r = g.frf.result.common();
    swprintf(buf, 256, L"FRF: %.6g–%.6g Hz | N=%zu | Fs=%.6g Hz | Hann | %ls",
        g.frf.frequency_start, g.frf.frequency_end,
        r.sample_count, 1.0 / r.sample_dt,
        g.frf.apply_processing ? tr(L"Processed", L"С обработкой") : tr(L"Raw", L"Исходные"));
    std::wstring text = buf;
    if (r.gaps_ignored) text = tr(L"Warning: Gaps ignored | ",
        L"Внимание: пропуски проигнорированы (Gaps ignored) | ") + text;
    wchar_t method[160]{};
    swprintf(method,160,L" | %ls L=%zu K=%zu Δf=%.6g Hz overlap=%.0f%%",
        r.options.estimator==lvm::FrfEstimator::H1 ? L"H1" : L"Direct",
        r.segment_length,r.averages,1.0/(r.sample_dt*r.segment_length),
        100.0*r.overlap_samples/r.segment_length);
    text+=method;
    for (std::size_t i=0;i<g.frf.result.responses.size();++i)
        if (!g.frf.result.responses[i].ok) text+=L" | "+g.frf.output_names[i]+L": "+gui::frf_error_text(g.frf.result.responses[i].error);
    if (r.averages<2) text+=tr(L" | Coherence unavailable: K < 2",L" | Coherence недоступна: K < 2");
    return text;
}

void create_frf_panel(HWND parent, HINSTANCE instance) {
    WNDCLASSW wc{};
    wc.hInstance = instance; wc.lpfnWndProc = FrfPanelProc;
    wc.lpszClassName = L"AMGraphFrfPanel"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    g.frf_panel = CreateWindowExW(0, wc.lpszClassName, L"FRF", WS_CHILD | WS_CLIPCHILDREN,
        0, 0, kRightPanel, 400, parent, nullptr, instance, nullptr);
}

void layout_frf_panel() {
    if (!g.frf_panel) return;
    const bool show = g.mode == AnalysisMode::FRF && g.side_panel_visible && !welcome_visible() && !g.frf_point_settings_open;
    ShowWindow(g.frf_panel, show ? SW_SHOW : SW_HIDE);
    if (show) {
        RECT r; GetClientRect(g.main, &r);
        constexpr int tabs_height = 44;
        MoveWindow(g.frf_panel, r.right - kRightPanel, kTopBar + tabs_height, kRightPanel,
            std::max(1L, r.bottom - kTopBar - tabs_height - kBottomBar), TRUE);
    }
}

void refresh_frf_controls(bool repopulate) {
    if (!g.frf_panel) return;
    label(InputLabel, tr(L"Supports / Reference", L"Опоры"));
    label(OutputLabel, tr(L"Responses", L"Отклики"));
    label(Processing, tr(L"Apply channel processing", L"Применять обработку каналов"));
    label(LowLabel, L"F min, Hz"); label(HighLabel, L"F max, Hz");
    label(ApplyRange, tr(L"Apply frequency range", L"Применить диапазон частот"));
    label(EstimatorLabel, tr(L"Estimator",L"Метод")); label(LengthLabel,L"L (0 = Auto)");
    label(SmoothingLabel, tr(L"Smoothing",L"Сглаживание"));
    label(AxisScaleLabel, tr(L"Frequency axis",L"Ось частоты"));
    label(Reference, tr(L"Show average Reference",L"Показывать среднюю опору"));
    const auto& r=g.frf.result.common();
    wchar_t details[192]{};
    if (r.segment_length && !g.frf.pending) {
        swprintf(details,192,L"%ls · Hann · L=%zu · K=%zu\nΔf=%.6g Hz · overlap=%.0f%% (%zu)",
            r.options.estimator==lvm::FrfEstimator::H1 ? L"H1" : L"Direct",
            r.segment_length,r.averages,1.0/(r.sample_dt*r.segment_length),
            100.0*r.overlap_samples/r.segment_length,r.overlap_samples);
        label(Method,details);
    } else label(Method,g.frf.pending ? tr(L"Calculating…",L"Вычисление…") : L"Hann · L/K/Δf: —");
    EnableWindow(control(Length),g.frf.options.estimator==lvm::FrfEstimator::H1);
    label(Calculate, tr(L"Calculate", L"Рассчитать")); label(Csv, L"CSV"); label(Png, L"PNG");
    label(Hint, tr(L"KD = Response / average Reference (not dB).\nA filter attenuates beyond cutoff.\nEqual processing can cancel in KD.",
                   L"КД = отклик / средняя опора (без dB).\nФильтр ослабляет частоты за срезом.\nОдинаковая обработка может сократиться."));
    if (repopulate) {
        SendMessageW(control(Estimator),CB_SETCURSEL,g.frf.options.estimator==lvm::FrfEstimator::H1 ? 0 : 1,0);
        SendMessageW(control(Smoothing),CB_SETCURSEL,smoothing_choice(g.frf.display_smoothing_octaves),0);
        SendMessageW(control(AxisScale),CB_SETCURSEL,g.frf.logarithmic_frequency_axis ? 0 : 1,0);
        label(Length,std::to_wstring(g.frf.options.segment_length).c_str());
    }
    const auto button_text=[&](const std::vector<int>& channels,bool reference) {
        if (channels.empty()) return std::wstring(reference ? tr(L"Choose supports…",L"Выбрать опоры…") :
            tr(L"Choose responses…",L"Выбрать отклики…"));
        std::wstring names;
        for (int channel:channels) {
            if (!names.empty()) names+=L", ";
            names+=channel_display_label(channel);
        }
        HWND button=control(reference ? Input : Output);
        if (!button || !g.ui_font)
            return names.size()<=34 ? names+L" ▾" : std::to_wstring(channels.size())+
                tr(L" channels selected ▾",L" каналов выбрано ▾");
        RECT rect{}; GetClientRect(button,&rect);
        HDC dc=GetDC(button); HGDIOBJ previous=SelectObject(dc,g.ui_font);
        SIZE extent{}; GetTextExtentPoint32W(dc,names.c_str(),static_cast<int>(names.size()),&extent);
        SelectObject(dc,previous); ReleaseDC(button,dc);
        if (extent.cx<=rect.right-rect.left-30) return names+L" ▾";
        return std::to_wstring(channels.size())+tr(L" channels selected ▾",L" каналов выбрано ▾");
    };
    label(Input,button_text(g.frf.inputs,true).c_str());
    label(Output,button_text(g.frf.outputs,false).c_str());
    label(InputSummary,g.frf.inputs.size()>1 ? (tr(L"AVG of ",L"Среднее из ")+std::to_wstring(g.frf.inputs.size())+
        tr(L" channels",L" каналов")).c_str() : L"");
    if (g.frf.view_initialized) {
        label(Low, format_edit_number(g.frf.frequency_start).c_str());
        label(High, format_edit_number(g.frf.frequency_end).c_str());
    }
    wchar_t source[192]{};
    swprintf(source, 192, tr(L"%ls: %.5g–%.5g s", L"%ls: %.5g–%.5g с"),
        g.frf.from_selection ? tr(L"Selection", L"Выделение") : tr(L"View", L"Вид"),
        g.frf.source_start, g.frf.source_end);
    label(Source, source);
    const bool ready = g.frf.result.ok && !g.frf.pending;
    EnableWindow(control(Csv), ready); EnableWindow(control(Png), ready);
    EnableWindow(control(ApplyRange), ready);
    InvalidateRect(control(Processing), nullptr, TRUE);
    InvalidateRect(control(Reference), nullptr, TRUE);
}

bool set_frf_frequency_range(double low, double high) {
    if (!g.frf.result.ok || !std::isfinite(low) || !std::isfinite(high) || low <= 0 || high <= low) return false;
    const auto& f = g.frf.result.common().frequencies;
    if (low < f[1] * (1 - 1e-9) || high > f.back() * (1 + 1e-9)) return false;
    g.frf.log_start = std::log10(std::max(low, f[1]));
    g.frf.log_end = std::log10(std::min(high, f.back()));
    g.frf.frequency_start=std::max(low,f[1]);
    g.frf.frequency_end=std::min(high,f.back());
    refresh_frf_controls(); set_status(); if (g.main) invalidate_plot();
    return true;
}
void sync_frf_frequency_limits() {
    if (g.frf.logarithmic_frequency_axis) {
        g.frf.frequency_start=std::pow(10.0,g.frf.log_start);
        g.frf.frequency_end=std::pow(10.0,g.frf.log_end);
    } else {
        g.frf.log_start=std::log10(g.frf.frequency_start);
        g.frf.log_end=std::log10(g.frf.frequency_end);
    }
}
void reset_frf_view() {
    if (!g.frf.result.ok) return;
    g.frf.auto_y = true;
    g.frf.reference_auto_y = true;
    set_frf_frequency_range(g.frf.result.common().frequencies[1], g.frf.result.common().frequencies.back());
}
bool frf_command_supported(int id) {
    switch (id) {
        case IDC_PLAY: case IDM_VISMOOTH:
            return false;
    }
    return true;
}
} // namespace gui

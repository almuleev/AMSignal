#include "gui_dialogs.hpp"
#include "gui_controls.hpp"
#include "gui_ids.hpp"
#include "gui_processing.hpp"
#include "gui_settings_window.hpp"
#include "gui_side_panel.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include "gui_theme.hpp"

namespace gui {
GuidePromptState g_guide_prompt;

LRESULT CALLBACK GuidePromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
        case WM_CREATE: {
            const auto instance=reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance;
            const HFONT font=g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const auto control=[&](const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id) {
                HWND ctl=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
                SendMessageW(ctl,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
                return ctl;
            };
            control(L"STATIC",g_str==&kEn ? L"Direction:" : L"Направление:",
                SS_LEFT|SS_CENTERIMAGE|SS_NOPREFIX,18,18,132,28,0);
            g_guide_prompt.direction=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|
                CBS_HASSTRINGS|CBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_BORDER,166,18,276,160,IDC_GUIDE_DIRECTION);
            install_themed_combo(g_guide_prompt.direction);
            for(const auto* text:{g_str==&kEn ? L"Vertical" : L"Вертикальная",
                                 g_str==&kEn ? L"Horizontal" : L"Горизонтальная"})
                SendMessageW(g_guide_prompt.direction,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
            SendMessageW(g_guide_prompt.direction,CB_SETCURSEL,g_guide_prompt.vertical ? 0 : 1,0);
            g_guide_prompt.label=control(L"STATIC",guide_prompt_label_text(g_guide_prompt.vertical),
                SS_LEFT|SS_NOPREFIX,18,64,424,36,0);
            g_guide_prompt.vertical_text=format_edit_number(g_guide_prompt.vertical_value);
            g_guide_prompt.horizontal_text=format_edit_number(g_guide_prompt.horizontal_value);
            g_guide_prompt.edit=control(L"EDIT",(g_guide_prompt.vertical ?
                g_guide_prompt.vertical_text : g_guide_prompt.horizontal_text).c_str(),
                WS_TABSTOP|ES_AUTOHSCROLL,18,106,424,28,IDC_SPEED_PROMPT_EDIT);
            SendMessageW(g_guide_prompt.edit,EM_SETLIMITTEXT,127,0);
            install_compact_themed_edit(g_guide_prompt.edit);
            g_guide_prompt.error=control(L"STATIC",L"",SS_LEFT|SS_NOPREFIX,18,142,424,34,0);
            control(L"BUTTON",guide_prompt_apply_text(),WS_TABSTOP|BS_OWNERDRAW|BS_DEFPUSHBUTTON,196,190,118,28,IDOK);
            control(L"BUTTON",guide_prompt_cancel_text(),WS_TABSTOP|BS_OWNERDRAW,324,190,118,28,IDCANCEL);
            return 0;
        }
        case WM_COMMAND:
            if(LOWORD(wp)==IDC_GUIDE_DIRECTION) {
                if(HIWORD(wp)==CBN_DROPDOWN) {expand_combo_dropdown(g_guide_prompt.direction);return 0;}
                if(HIWORD(wp)!=CBN_SELCHANGE)return 0;
                const auto selection=SendMessageW(g_guide_prompt.direction,CB_GETCURSEL,0,0);
                if(selection!=0 && selection!=1)return 0;
                wchar_t text[128]{};
                GetWindowTextW(g_guide_prompt.edit,text,128);
                (g_guide_prompt.vertical ? g_guide_prompt.vertical_text : g_guide_prompt.horizontal_text)=text;
                g_guide_prompt.vertical=selection==0;
                SetWindowTextW(g_guide_prompt.label,guide_prompt_label_text(g_guide_prompt.vertical));
                SetWindowTextW(g_guide_prompt.edit,(g_guide_prompt.vertical ?
                    g_guide_prompt.vertical_text : g_guide_prompt.horizontal_text).c_str());
                SetWindowTextW(g_guide_prompt.error,L"");
                return 0;
            }
            if(LOWORD(wp)==IDC_SPEED_PROMPT_EDIT && HIWORD(wp)==EN_CHANGE) {
                if(g_guide_prompt.error)SetWindowTextW(g_guide_prompt.error,L"");
                return 0;
            }
            if(LOWORD(wp)==IDOK) {
                wchar_t text[128]{};double value=0;
                GetWindowTextW(g_guide_prompt.edit,text,128);
                if(!parse_wide_double_text(text,value)) {
                    SetWindowTextW(g_guide_prompt.error,guide_prompt_invalid_text());
                    SetFocus(g_guide_prompt.edit);SendMessageW(g_guide_prompt.edit,EM_SETSEL,0,-1);
                    return 0;
                }
                g_guide_prompt.value=value;g_guide_prompt.accepted=true;
                DestroyWindow(hwnd);return 0;
            }
            if(LOWORD(wp)==IDCANCEL) {DestroyWindow(hwnd);return 0;}
            break;
        case WM_CLOSE: DestroyWindow(hwnd);return 0;
        case WM_ERASEBKGND: draw_prompt_surface(hwnd,reinterpret_cast<HDC>(wp));return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};HDC dc=BeginPaint(hwnd,&paint);
            draw_prompt_surface(hwnd,dc);EndPaint(hwnd,&paint);return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT: {
            HDC dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);
            SetTextColor(dc,g_theme->text_primary);
            if(msg==WM_CTLCOLOREDIT) {
                SetBkColor(dc,g_theme->bg_plot);
                return reinterpret_cast<LRESULT>(g_input_brush ? g_input_brush : g_panel_brush);
            }
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_MEASUREITEM: {
            auto* item=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if(item && item->CtlType==ODT_COMBOBOX) {measure_settings_combo_item(item);return TRUE;}
            break;
        }
        case WM_DRAWITEM: {
            const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(!item)break;
            if(item->CtlType==ODT_COMBOBOX) {draw_settings_combo_item(item);return TRUE;}
            if(item->CtlID==IDOK || item->CtlID==IDCANCEL) {
                wchar_t text[64]{};GetWindowTextW(item->hwndItem,text,64);
                draw_themed_button(item->hDC,item->rcItem,text,(item->itemState&ODS_SELECTED)!=0,item->CtlID==IDOK,false);
                return TRUE;
            }
            break;
        }
        case WM_DESTROY:
            g_guide_prompt.done=true;g_guide_prompt.wnd=nullptr;
            g_guide_prompt.direction=g_guide_prompt.label=g_guide_prompt.edit=g_guide_prompt.error=nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

bool prompt_exact_guide_value(bool& vertical,double& out_value) {
    static ATOM atom=0;
    const auto instance=GetModuleHandleW(nullptr);
    if(!atom) {
        WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.lpfnWndProc=GuidePromptProc;
        cls.hInstance=instance;cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.lpszClassName=L"LvmGuidePrompt";
        atom=RegisterClassExW(&cls);
    }
    g_guide_prompt=GuidePromptState{};g_guide_prompt.vertical=vertical;
    g_guide_prompt.vertical_value=exact_guide_default_value(true);
    g_guide_prompt.horizontal_value=exact_guide_default_value(false);
    const DWORD style=WS_CAPTION|WS_SYSMENU|WS_POPUP;
    const DWORD ex_style=WS_EX_DLGMODALFRAME|WS_EX_TOOLWINDOW;
    RECT size{0,0,460,236};AdjustWindowRectEx(&size,style,FALSE,ex_style);
    g_guide_prompt.wnd=CreateWindowExW(ex_style,L"LvmGuidePrompt",guide_prompt_title_text(),style,
        CW_USEDEFAULT,CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,g.main,nullptr,instance,nullptr);
    if(!g_guide_prompt.wnd)return false;
    RECT owner{},window{};GetWindowRect(g.main,&owner);GetWindowRect(g_guide_prompt.wnd,&window);
    EnableWindow(g.main,FALSE);
    SetWindowPos(g_guide_prompt.wnd,HWND_TOP,owner.left+((owner.right-owner.left)-(window.right-window.left))/2,
        owner.top+((owner.bottom-owner.top)-(window.bottom-window.top))/2,0,0,SWP_NOSIZE|SWP_SHOWWINDOW);
    SetFocus(g_guide_prompt.edit);SendMessageW(g_guide_prompt.edit,EM_SETSEL,0,-1);
    MSG message{};
    while(!g_guide_prompt.done && GetMessageW(&message,nullptr,0,0)>0) {
        if(!IsDialogMessageW(g_guide_prompt.wnd,&message)) {TranslateMessage(&message);DispatchMessageW(&message);}
    }
    EnableWindow(g.main,TRUE);SetForegroundWindow(g.main);
    if(!g_guide_prompt.accepted)return false;
    vertical=g_guide_prompt.vertical;out_value=g_guide_prompt.value;
    return true;
}
}

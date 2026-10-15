// Gap marker hit testing and the plot details card.
#include "gui_gap_details.hpp"
#include "gui_controls.hpp"
#include "gui_state.hpp"
#include "gui_theme.hpp"
#include "gui_text.hpp"

namespace gui {

int hit_test_gap_marker(int x, int y) {
    POINT pt{ x, y };
    for (int i = static_cast<int>(g.visible_gap_markers.size()) - 1; i >= 0; --i) {
        if (PtInRect(&g.visible_gap_markers[static_cast<std::size_t>(i)].rect, pt)) return i;
    }
    return -1;
}

void hide_gap_details_card() {
    if (!g.gap_details_visible &&
        g.gap_details_duration == 0.0 &&
        g.gap_details_missing_samples == 0 &&
        g.gap_details_reference_step == 0.0) {
        return;
    }
    g.gap_details_visible = false;
    g.gap_details_duration = 0.0;
    g.gap_details_missing_samples = 0;
    g.gap_details_reference_step = 0.0;
    if (g.main && IsWindow(g.main)) InvalidateRect(g.main, nullptr, FALSE);
}

void set_gap_markers_visible(bool visible) {
    g.show_gap_markers=visible;
    if(!visible) {
        const auto clear=[](DocumentState& document) {
            document.visible_gap_markers.clear();
            document.gap_details_visible=false;
            document.gap_details_duration=0;
            document.gap_details_missing_samples=0;
            document.gap_details_reference_step=0;
        };
        clear(g);
        for(auto& document:g.inactive_documents)clear(document);
        g.gap_click_pending=false;g.gap_click_index=-1;
    }
    if(g.main && IsWindow(g.main))InvalidateRect(g.main,nullptr,FALSE);
}

void show_gap_details_card(double duration, long long estimated_missing_samples) {
    g.gap_details_visible = true;
    g.gap_details_duration = duration;
    g.gap_details_missing_samples = estimated_missing_samples;
    g.gap_details_reference_step = gap_reference_step_from_estimate(duration, estimated_missing_samples);
    if (g.main && IsWindow(g.main)) InvalidateRect(g.main, nullptr, FALSE);
}

void draw_gap_details_card(HDC dc, const RECT& plot) {
    if (!g.gap_details_visible || !has_data() || !g.show_gap_markers || g.mode != AnalysisMode::Time) return;

    const int margin = 12;
    const int pad = 14;
    const int available_w = plot.right - plot.left - margin * 2;
    const int available_h = plot.bottom - plot.top - margin * 2;
    if (available_w < 220 || available_h < 120) return;

    int card_w = available_w > 420 ? 420 : available_w;
    if (card_w < 260) card_w = available_w;
    if (card_w < 220) card_w = 220;

    const std::wstring title = gap_details_title_text(g_str == &kEn);
    const std::wstring body = gap_details_body_text(
        g_str == &kEn,
        g.gap_details_duration,
        g.gap_details_missing_samples,
        g.gap_details_reference_step);

    RECT title_rc = {0, 0, card_w - pad * 2, 0};
    RECT body_rc = {0, 0, card_w - pad * 2, 0};
    HFONT body_font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT title_font = g.bold_font ? g.bold_font : body_font;
    HGDIOBJ old_font = SelectObject(dc, title_font);
    DrawTextW(dc, title.c_str(), -1, &title_rc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, body_font);
    DrawTextW(dc, body.c_str(), -1, &body_rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old_font);

    const int title_h = title_rc.bottom - title_rc.top;
    const int body_h = body_rc.bottom - body_rc.top;
    const int card_h = pad * 2 + title_h + 10 + body_h;
    if (card_h >= available_h) return;

    RECT card = {
        plot.right - margin - card_w,
        plot.top + margin,
        plot.right - margin,
        plot.top + margin + card_h
    };
    if (card.left < plot.left + margin) {
        card.left = plot.left + margin;
        card.right = card.left + card_w;
    }
    if (card.bottom > plot.bottom - margin) {
        card.bottom = plot.bottom - margin;
        card.top = card.bottom - card_h;
    }

    const COLORREF gap_orange = RGB(245, 140, 32);
    const COLORREF gap_fill = mix_color(g_theme->bg_panel, gap_orange, (g_theme == &kDarkTheme) ? 24 : 18);
    const COLORREF gap_border = mix_color(g_theme->separator, gap_orange, (g_theme == &kDarkTheme) ? 110 : 96);
    fill_rounded_rect(dc, card, gap_fill, gap_border, 14);

    RECT text = card;
    InflateRect(&text, -pad, -pad);
    SetBkMode(dc, TRANSPARENT);

    RECT title_rect = text;
    title_rect.bottom = title_rect.top + title_h;
    SelectObject(dc, title_font);
    SetTextColor(dc, gap_orange);
    DrawTextW(dc, title.c_str(), -1, &title_rect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    HPEN sep = CreatePen(PS_SOLID, 1, mix_color(g_theme->separator, gap_orange, 72));
    HGDIOBJ old_pen = SelectObject(dc, sep);
    const int line_y = title_rect.bottom + 5;
    MoveToEx(dc, text.left, line_y, nullptr);
    LineTo(dc, text.right, line_y);
    SelectObject(dc, old_pen);
    DeleteObject(sep);

    RECT body_rect = text;
    body_rect.top = line_y + 7;
    SelectObject(dc, body_font);
    SetTextColor(dc, g_theme->text_primary);
    DrawTextW(dc, body.c_str(), -1, &body_rect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    SelectObject(dc, old_font);
}

} // namespace gui

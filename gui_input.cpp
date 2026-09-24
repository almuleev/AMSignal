// Input: native viewer implementation.
#include "gui_input.hpp"
#include "gui_frf_render.hpp"
#include "gui_analysis_source.hpp"
#include "gui_gap_details.hpp"
#include "gui_controls.hpp"
#include "gui_menu.hpp"
#include "gui_ids.hpp"
#include "gui_layout.hpp"
#include "gui_loading.hpp"
#include "gui_navigation.hpp"
#include "gui_processing.hpp"
#include "gui_render.hpp"
#include "gui_render_data.hpp"
#include "gui_side_panel.hpp"
#include "gui_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_state_history.hpp"
#include "gui_status.hpp"
#include "gui_theme.hpp"
#include "gui_time_axis.hpp"

namespace gui {

void add_guide_line(bool vertical, double value) {
    GuideLine gl;
    gl.vertical = vertical;
    gl.value = value;
    gl.mode = g.mode;
    g.guides.push_back(gl);
    UndoAction ua;
    ua.type = UndoAction::ADD_LINE;
    ua.line = gl;
    push_undo(ua);
    g.pending_line = 0;
    sync_menu();
    set_status();
    invalidate_plot();
}

// ---- hit testing ---------------------------------------------------------

bool px_to_data(int px, int py, double& dx, double& dy) {
    if (!g.vvalid) return false;
    const RECT& p = g.vrect;
    if (p.right <= p.left || p.bottom <= p.top) return false;
    dx = g.vx0 + static_cast<double>(px - p.left) / (p.right - p.left) * (g.vx1 - g.vx0);
    if (g.mode == AnalysisMode::FRF) dx = frf_frequency_at_fraction(static_cast<double>(px-p.left)/(p.right-p.left));
    if ((g.mode == AnalysisMode::Time) && g.stitch_time_gaps) dx = raw_time_from_stitched(dx);
    dy = g.vy0 + static_cast<double>(p.bottom - py) / (p.bottom - p.top) * (g.vy1 - g.vy0);
    return true;
}

namespace {
bool same_annotation_value(double left, double right) {
    return std::abs(left-right)<=1e-12*std::max({1.0,std::abs(left),std::abs(right)});
}
}

bool point_group_contains_point(int group_index, double x, double y) {
    if (group_index<0 || static_cast<std::size_t>(group_index)>=g.point_groups.size()) return false;
    for (const auto& point:g.point_groups[static_cast<std::size_t>(group_index)].points) {
        if (same_annotation_value(point.first,x) && same_annotation_value(point.second,y)) return true;
    }
    return false;
}

bool marker_exists_at_current_position(double x, double y) {
    for (const auto& marker:g.markers) {
        if (marker.mode==g.mode && same_annotation_value(marker.x,x) && same_annotation_value(marker.y,y)) return true;
    }
    return false;
}

bool guide_exists_at_current_position(bool vertical, double value) {
    for (const auto& guide:g.guides) {
        if (guide.mode==g.mode && guide.vertical==vertical && same_annotation_value(guide.value,value)) return true;
    }
    return false;
}

// Snap a clicked coordinate to the nearest visible real sample (Time mode) or
// spectrum point (Hz mode) by on-screen distance, so a marker lands on the
// visually closest data point. The stored data is never modified � only the
// marker is adjusted.
bool snap_to_nearest_target(double& dx, double& dy, int* out_channel) {
    if (!g.vvalid) return false;
    const RECT& p = g.vrect;
    const int pw = p.right - p.left;
    const int ph = p.bottom - p.top;
    if (pw <= 0 || ph <= 0) return false;

    auto to_px = [&](double x) -> double {
        const double displayed_x = (g.mode == AnalysisMode::FRF) ? (g.vx0+frf_frequency_fraction(x)*(g.vx1-g.vx0)) :
            (g.mode == AnalysisMode::FFT) ? x : stitched_time_from_raw(x);
        return static_cast<double>(p.left) + (displayed_x - g.vx0) / (g.vx1 - g.vx0) * pw;
    };
    auto to_py = [&](double y) -> double {
        return static_cast<double>(p.bottom) - (y - g.vy0) / (g.vy1 - g.vy0) * ph;
    };

    const double target_px = to_px(dx);
    const double target_py = to_py(dy);
    double best_dist2 = std::numeric_limits<double>::max();
    double best_x = dx;
    double best_y = dy;
    int best_ci = -1;

    if (g.mode == AnalysisMode::FRF) {
        if (!g.frf.result.ok || g.vx1 <= g.vx0 || g.vy1 <= g.vy0) return false;
        for (std::size_t response=0; response<g.frf.result.responses.size(); ++response) {
            const auto& result=g.frf.result.responses[response];
            if (!result.ok) continue;
            for (std::size_t k=1; k<result.frequencies.size(); ++k) {
                const double x=result.frequencies[k];
                const double y=lvm::frf_dynamic_coefficient(result,k);
                if (!(x>0) || !std::isfinite(y)) continue;
                const double dxp=to_px(x)-target_px;
                const double dyp=to_py(y)-target_py;
                const double distance=dxp*dxp+dyp*dyp;
                if (distance<best_dist2) {
                    best_dist2=distance; best_x=x; best_y=y;
                    best_ci=response<g.frf.outputs.size() ? g.frf.outputs[response] : -1;
                }
            }
        }
        if (best_ci>=0) {
            dx=best_x; dy=best_y;
            if (out_channel) *out_channel=best_ci;
            return true;
        }
        return false;
    }

    if ((g.mode == AnalysisMode::FFT)) {
        if (!ensure_current_spectrum() || g.spec.freqs.empty() || g.vx1 <= g.vx0 || g.vy1 <= g.vy0) return false;
        const auto& f = g.spec.freqs;
        std::size_t lo = static_cast<std::size_t>(std::lower_bound(f.begin(), f.end(), g.vx0) - f.begin());
        std::size_t hi = static_cast<std::size_t>(std::upper_bound(f.begin(), f.end(), g.vx1) - f.begin());
        if (lo >= hi) return false;
        for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
            const int ci = (j < g.spec_channel_indices.size()) ? g.spec_channel_indices[j] : -1;
            if (ci < 0 || !g.visible[ci]) continue;
            for (std::size_t k = lo; k < hi; ++k) {
                const double x = f[k];
                const double y = g.spec.amp[j][k];
                if (!std::isfinite(y)) continue;
                const double px = to_px(x);
                const double py = to_py(y);
                const double dxp = px - target_px;
                const double dyp = py - target_py;
                const double dist2 = dxp * dxp + dyp * dyp;
                if (dist2 < best_dist2) {
                    best_dist2 = dist2;
                    best_x = x;
                    best_y = y;
                    best_ci = ci;
                }
            }
        }
        if (best_ci >= 0) {
            dx = best_x;
            dy = best_y;
            if (out_channel) *out_channel = best_ci;
            return true;
        }
        return false;
    }
    if (!has_data()) return false;
    ensure_channel_formula_vectors();
    const auto& t = g.ds.time;
    if (t.empty() || g.vx1 <= g.vx0 || g.vy1 <= g.vy0) return false;
    std::size_t lo = static_cast<std::size_t>(std::lower_bound(t.begin(), t.end(), g.win_start) - t.begin());
    std::size_t hi = static_cast<std::size_t>(std::upper_bound(t.begin(), t.end(), g.win_end) - t.begin());
    if (lo >= hi) return false;
    for (std::size_t c = 0; c < g.ds.channel_count(); ++c) {
        if (!g.visible[c]) continue;
        for (std::size_t i = lo; i < hi; ++i) {
            const double y = rendered_channel_sample(c, i);
            if (!std::isfinite(y)) continue;
            const double x = t[i];
            const double px = to_px(x);
            const double py = to_py(y);
            const double dxp = px - target_px;
            const double dyp = py - target_py;
            const double dist2 = dxp * dxp + dyp * dyp;
            if (dist2 < best_dist2) {
                best_dist2 = dist2;
                best_x = x;
                best_y = y;
                best_ci = static_cast<int>(c);
            }
        }
    }
    if (best_ci >= 0) {
        dx = best_x;
        dy = best_y;
        if (out_channel) *out_channel = best_ci;
        return true;
    }
    return false;
}

void snap_to_nearest(double& dx, double& dy) {
    snap_to_nearest_target(dx, dy, nullptr);
}

int hit_test_marker(int px, int py) {
    if (!g.vvalid || g.markers.empty()) return -1;
    const RECT& p = g.vrect;
    if (px < p.left || px > p.right || py < p.top || py > p.bottom) return -1;
    if (g.vx1 <= g.vx0 || g.vy1 <= g.vy0) return -1;
    auto mx = [&](double dx) {
        const double displayed_x = (g.mode == AnalysisMode::FRF) ?
            (g.vx0+frf_frequency_fraction(dx)*(g.vx1-g.vx0)) :
            (g.mode == AnalysisMode::FFT) ? dx : stitched_time_from_raw(dx);
        return p.left + static_cast<int>((displayed_x - g.vx0) / (g.vx1 - g.vx0) * (p.right - p.left));
    };
    auto my = [&](double dy) {
        return p.bottom - static_cast<int>((dy - g.vy0) / (g.vy1 - g.vy0) * (p.bottom - p.top));
    };
    int best = -1;
    int best_score = 999999;
    for (std::size_t i = 0; i < g.markers.size(); ++i) {
        const App::Marker& m = g.markers[i];
        if (m.mode != g.mode) continue;
        const int dxp = std::abs(px - mx(m.x));
        if (dxp > 6) continue;
        int score = dxp * 10;
        if (m.snapped) {
            const int dyp = std::abs(py - my(m.y));
            if (dyp <= 8) score = dxp + dyp;
        }
        if (score < best_score) {
            best_score = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void clear_annotation_selection() {
    g.annotation_selection_kind=App::AnnotationSelectionKind::None;
    g.annotation_selection_index=-1;
    g.annotation_selection_point_index=-1;
}

bool delete_selected_annotation() {
    if (g.annotations_locked || g.annotation_selection_kind==App::AnnotationSelectionKind::None) return false;
    UndoAction action;
    const int index=g.annotation_selection_index;
    switch (g.annotation_selection_kind) {
        case App::AnnotationSelectionKind::Point: {
            const int point_index=g.annotation_selection_point_index;
            if (index<0 || point_index<0 || static_cast<std::size_t>(index)>=g.point_groups.size()) return false;
            auto& group=g.point_groups[static_cast<std::size_t>(index)];
            if (group.mode!=current_point_group_mode()) return false;
            auto& points=group.points;
            if (static_cast<std::size_t>(point_index)>=points.size()) return false;
            action.type=UndoAction::REMOVE_POINT;
            action.point_group_index=index;
            action.annotation_drag_point_index=point_index;
            action.point=points[static_cast<std::size_t>(point_index)];
            action.point_group_state=group;
            points.erase(points.begin()+point_index);
            if (points.empty()) {
                action.point_group_erased=true;
                erase_point_group(static_cast<std::size_t>(index));
            }
            break;
        }
        case App::AnnotationSelectionKind::Guide:
            if (index<0 || static_cast<std::size_t>(index)>=g.guides.size()) return false;
            if (g.guides[static_cast<std::size_t>(index)].mode!=g.mode) return false;
            action.type=UndoAction::REMOVE_LINE;
            action.point_group_index=index;
            action.line=g.guides[static_cast<std::size_t>(index)];
            g.guides.erase(g.guides.begin()+index);
            break;
        case App::AnnotationSelectionKind::Marker:
            if (index<0 || static_cast<std::size_t>(index)>=g.markers.size()) return false;
            if (g.markers[static_cast<std::size_t>(index)].mode!=g.mode) return false;
            action.type=UndoAction::REMOVE_MARKER;
            action.point_group_index=index;
            action.marker=g.markers[static_cast<std::size_t>(index)];
            g.markers.erase(g.markers.begin()+index);
            g.active_marker=-1;
            break;
        case App::AnnotationSelectionKind::None:
            return false;
    }
    clear_annotation_selection();
    push_undo(std::move(action));
    refresh_side_panel_controls();
    return true;
}

bool begin_annotation_drag(HWND hwnd, int px, int py) {
    if (g.annotations_locked || !g.vvalid) return false;
    g.annotation_drag_reference_axis=false;
    const RECT& p=g.vrect;
    if (px<p.left || px>p.right || py<p.top || py>p.bottom || g.vx1<=g.vx0 || g.vy1<=g.vy0) return false;
    const auto map_x=[&](double x) {
        const double displayed=g.mode==AnalysisMode::FRF ? (g.vx0+frf_frequency_fraction(x)*(g.vx1-g.vx0)) :
            g.mode==AnalysisMode::FFT ? x : stitched_time_from_raw(x);
        return p.left+static_cast<int>((displayed-g.vx0)/(g.vx1-g.vx0)*(p.right-p.left));
    };
    const auto map_y=[&](double y) { return p.bottom-static_cast<int>((y-g.vy0)/(g.vy1-g.vy0)*(p.bottom-p.top)); };
    int best_distance=10, group_index=-1, point_index=-1;
    for (std::size_t group=0;group<g.point_groups.size();++group) {
        const auto& points=g.point_groups[group];
        if (!points.visible || points.mode!=current_point_group_mode() || points.frf_reference_axis) continue;
        for (std::size_t point=0;point<points.points.size();++point) {
            const int distance=std::max(std::abs(px-map_x(points.points[point].first)),
                                        std::abs(py-map_y(points.points[point].second)));
            if (distance<best_distance) { best_distance=distance; group_index=static_cast<int>(group); point_index=static_cast<int>(point); }
        }
    }
    if (group_index>=0) {
        g.annotation_selection_kind=App::AnnotationSelectionKind::Point;
        g.annotation_selection_index=group_index;
        g.annotation_selection_point_index=point_index;
        g.annotation_drag_kind=App::AnnotationDragKind::Point;
        g.annotation_drag_index=group_index;
        g.annotation_drag_point_index=point_index;
        g.annotation_drag_point_before=g.point_groups[static_cast<std::size_t>(group_index)].points[static_cast<std::size_t>(point_index)];
        SetCapture(hwnd); return true;
    }
    const int marker=hit_test_marker(px,py);
    if (marker>=0) {
        g.annotation_selection_kind=App::AnnotationSelectionKind::Marker;
        g.annotation_selection_index=marker;
        g.annotation_selection_point_index=-1;
        g.annotation_drag_kind=App::AnnotationDragKind::Marker;
        g.annotation_drag_index=marker;
        g.annotation_drag_marker_before=g.markers[static_cast<std::size_t>(marker)];
        SetCapture(hwnd); return true;
    }
    int guide=-1; best_distance=7;
    for (std::size_t i=0;i<g.guides.size();++i) {
        const auto& line=g.guides[i]; if (line.mode!=g.mode) continue;
        const int distance=line.vertical ? std::abs(px-map_x(line.value)) : std::abs(py-map_y(line.value));
        if (distance<best_distance) { best_distance=distance; guide=static_cast<int>(i); }
    }
    if (guide>=0) {
        g.annotation_selection_kind=App::AnnotationSelectionKind::Guide;
        g.annotation_selection_index=guide;
        g.annotation_selection_point_index=-1;
        g.annotation_drag_kind=App::AnnotationDragKind::Guide;
        g.annotation_drag_index=guide;
        g.annotation_drag_guide_before=g.guides[static_cast<std::size_t>(guide)];
        SetCapture(hwnd); return true;
    }
    return false;
}

void update_annotation_drag(int px, int py) {
    if (g.annotation_drag_kind==App::AnnotationDragKind::None) return;
    double x=0,y=0; if (!px_to_data(px,py,x,y)) return;
    if (g.annotation_drag_kind==App::AnnotationDragKind::Point && g.annotation_drag_index>=0 && g.annotation_drag_point_index>=0) {
        if (g.snap_to_data) {
            if (g.mode==AnalysisMode::FRF) snap_to_displayed_frf_curve(x,y);
            else snap_to_nearest(x,y);
        }
        auto& points=g.point_groups[static_cast<std::size_t>(g.annotation_drag_index)].points;
        if (static_cast<std::size_t>(g.annotation_drag_point_index)<points.size()) points[static_cast<std::size_t>(g.annotation_drag_point_index)]={x,y};
    } else if (g.annotation_drag_kind==App::AnnotationDragKind::Marker && g.annotation_drag_index>=0 &&
               static_cast<std::size_t>(g.annotation_drag_index)<g.markers.size()) {
        int channel=-1; const bool snapped=g.snap_to_data && snap_to_nearest_target(x,y,&channel);
        auto& marker=g.markers[static_cast<std::size_t>(g.annotation_drag_index)];
        marker.x=x; marker.y=y; marker.snapped=snapped; marker.channel=snapped ? channel : -1;
    } else if (g.annotation_drag_kind==App::AnnotationDragKind::Guide && g.annotation_drag_index>=0 &&
               static_cast<std::size_t>(g.annotation_drag_index)<g.guides.size()) {
        auto& line=g.guides[static_cast<std::size_t>(g.annotation_drag_index)];
        if (g.snap_to_data) {
            if (g.mode==AnalysisMode::FRF) snap_to_displayed_frf_curve(x,y);
            else snap_to_nearest(x,y);
        }
        line.value=line.vertical ? x : y;
    }
}

void finish_annotation_drag(bool commit) {
    const auto kind=g.annotation_drag_kind;
    const int index=g.annotation_drag_index, point_index=g.annotation_drag_point_index;
    g.annotation_drag_kind=App::AnnotationDragKind::None;
    g.annotation_drag_index=-1; g.annotation_drag_point_index=-1;
    g.annotation_drag_reference_axis=false;
    if (!commit || index<0) {
        if (kind==App::AnnotationDragKind::Point && index>=0 && point_index>=0 && static_cast<std::size_t>(index)<g.point_groups.size() &&
            static_cast<std::size_t>(point_index)<g.point_groups[static_cast<std::size_t>(index)].points.size())
            g.point_groups[static_cast<std::size_t>(index)].points[static_cast<std::size_t>(point_index)]=g.annotation_drag_point_before;
        else if (kind==App::AnnotationDragKind::Guide && static_cast<std::size_t>(index)<g.guides.size()) g.guides[static_cast<std::size_t>(index)]=g.annotation_drag_guide_before;
        else if (kind==App::AnnotationDragKind::Marker && static_cast<std::size_t>(index)<g.markers.size()) g.markers[static_cast<std::size_t>(index)]=g.annotation_drag_marker_before;
        return;
    }
    UndoAction action;
    if (kind==App::AnnotationDragKind::Point && static_cast<std::size_t>(index)<g.point_groups.size() && static_cast<std::size_t>(point_index)<g.point_groups[static_cast<std::size_t>(index)].points.size()) {
        const auto after=g.point_groups[static_cast<std::size_t>(index)].points[static_cast<std::size_t>(point_index)];
        if (after==g.annotation_drag_point_before) return;
        action.type=UndoAction::MOVE_POINT; action.point_group_index=index; action.point_group_created=false;
        action.point=after; action.old_point=g.annotation_drag_point_before; action.annotation_drag_point_index=point_index;
    } else if (kind==App::AnnotationDragKind::Guide && static_cast<std::size_t>(index)<g.guides.size()) {
        const auto after=g.guides[static_cast<std::size_t>(index)];
        if (after.vertical==g.annotation_drag_guide_before.vertical && after.value==g.annotation_drag_guide_before.value && after.mode==g.annotation_drag_guide_before.mode) return;
        action.type=UndoAction::MOVE_LINE; action.point_group_index=index; action.line=after; action.old_line=g.annotation_drag_guide_before;
    } else if (kind==App::AnnotationDragKind::Marker && static_cast<std::size_t>(index)<g.markers.size()) {
        const auto after=g.markers[static_cast<std::size_t>(index)];
        if (after.x==g.annotation_drag_marker_before.x && after.y==g.annotation_drag_marker_before.y && after.snapped==g.annotation_drag_marker_before.snapped && after.channel==g.annotation_drag_marker_before.channel) return;
        action.type=UndoAction::MOVE_MARKER; action.point_group_index=index; action.marker=after; action.old_marker=g.annotation_drag_marker_before;
    } else return;
    push_undo(std::move(action));
}

LRESULT handle_input_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g.mode == AnalysisMode::FRF) return handle_frf_input(hwnd, msg, wp, lp);
    switch (msg) {
        case WM_CANCELMODE:
            if (g.annotation_drag_kind!=App::AnnotationDragKind::None) finish_annotation_drag(false);
            g_filter_slider_before.reset();
            refresh_side_panel_controls();
            return 0;
        case WM_SETCURSOR: {
            if (reinterpret_cast<HWND>(wp) == hwnd) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                RECT p = plot_rect();
                bool in_plot = (pt.x >= p.left && pt.x <= p.right && pt.y >= p.top && pt.y <= p.bottom);
                const bool selecting_fft_here =
                    (g.mode == AnalysisMode::Time) && in_plot &&
                    (GetKeyState(VK_SHIFT) & 0x8000) != 0 &&
                    !g.measure_mode && !g.pending_line && !g.pending_marker;
                if (g.dragging || g.fft_selecting || g.measure_mode || g.pending_line || g.pending_marker || selecting_fft_here) {
                    SetCursor(LoadCursor(nullptr, IDC_CROSS));
                    return TRUE;
                }
                if (in_plot) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            if (g.gap_details_visible) {
                hide_gap_details_card();
            }
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (side_panel_hit_test(pt) && g.side_scroll_max > 0) {
                const int direction = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -48 : 48;
                scroll_side_panel(direction);
                return 0;
            }
            if (!has_data()) return 0;
            const RECT p = plot_rect();
            bool in_plot = (pt.x >= p.left && pt.x <= p.right && pt.y >= p.top && pt.y <= p.bottom);
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            bool up = GET_WHEEL_DELTA_WPARAM(wp) > 0;

            if (shift) {
                pan_by(up ? -0.1 : 0.1);
                return 0;
            }
            if (ctrl) {
                if ((g.mode == AnalysisMode::FFT)) {
                    if (in_plot) {
                        double frac = static_cast<double>(p.bottom - pt.y) / (p.bottom - p.top);
                        zoom_y_amp_at(frac, up ? 0.85 : 1.0 / 0.85);
                    } else {
                        zoom_y_amp_at(0.5, up ? 0.85 : 1.0 / 0.85);
                    }
                } else {
                    if (in_plot) {
                        double frac = static_cast<double>(p.bottom - pt.y) / (p.bottom - p.top);
                        zoom_y_at(frac, up ? 0.85 : 1.0 / 0.85);
                    } else {
                        zoom_y_at(0.5, up ? 0.85 : 1.0 / 0.85);
                    }
                }
                return 0;
            }
            if (alt) {
                pan_y_by(up ? -0.1 : 0.1);
                return 0;
            }
            double frac = 0.5;
            if (pt.x >= p.left && pt.x <= p.right)
                frac = static_cast<double>(pt.x - p.left) / (p.right - p.left);
            zoom_at(frac, up ? 0.8 : 1.25);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            // Child controls may retain keyboard focus after editing a field.
            // A graph click must return it to the main window so Delete and
            // Backspace target the selected annotation without an app switch.
            if (GetFocus()!=hwnd) SetFocus(hwnd);
            finish_channel_rename_if_click_outside(hwnd);
            if (g.gap_details_visible) {
                hide_gap_details_card();
            }
            const int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            const RECT p = plot_rect();
            if (!has_data()) {
                if (mx >= p.left && mx <= p.right && my >= p.top && my <= p.bottom) {
                    open_file();
                }
                return 0;
            }

            // --- Legend click handling (toggle / solo) ---
            if (mx >= g_legend_box.left && mx < g_legend_box.right &&
                my >= g_legend_box.top && my < g_legend_box.bottom) {
                for (const auto& li : g_legend_items) {
                    if (mx >= li.rect.left && mx < li.rect.right &&
                        my >= li.rect.top && my < li.rect.bottom) {
                        const int ci = li.channel;
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                            // Solo: show only this channel
                            const SettingsSnapshot before = capture_settings_snapshot();
                            for (std::size_t j = 0; j < g.visible.size(); ++j) {
                                g.visible[j] = (static_cast<int>(j) == ci);
                                if (j < g.checks.size())
                                    set_toggle_checked(g.checks[j], g.visible[j] != 0);
                            }
                            invalidate_plot_analysis_cache();
                            record_settings_change(before);
                        } else {
                            // Toggle
                            const SettingsSnapshot before = capture_settings_snapshot();
                            g.visible[ci] = !g.visible[ci];
                            if (ci < static_cast<int>(g.checks.size()))
                                set_toggle_checked(g.checks[ci], g.visible[ci] != 0);
                            invalidate_plot_analysis_cache();
                            record_settings_change(before);
                        }
                        InvalidateRect(hwnd, nullptr, TRUE);
                        return 0;
                    }
                }
            }

            const bool placing_annotation=g.measure_mode || g.pending_line || g.pending_marker;
            if ((g.mode == AnalysisMode::Time) && g.show_gap_markers && !placing_annotation) {
                const int gap_index = hit_test_gap_marker(mx, my);
                if (gap_index >= 0) {
                    // An annotation drawn over a gap must be selectable before
                    // the gap card claims the click.
                    if (begin_annotation_drag(hwnd,mx,my)) return 0;
                    g.gap_click_pending = false;
                    g.gap_click_index = -1;
                    if (prepare_plot_drag(mx, my)) {
                        g.gap_click_pending = true;
                        g.gap_click_index = gap_index;
                        SetCapture(hwnd);
                    }
                    return 0;
                }
            }

            if (g.side_panel_visible && g.side_panel_tab == 0) {
                for (std::size_t i = 0; i < g.checks.size(); ++i) {
                    HWND c = g.checks[i];
                    if (!c || !IsWindowVisible(c)) continue;
                    RECT cr;
                    GetWindowRect(c, &cr);
                    MapWindowPoints(nullptr, g.main, reinterpret_cast<LPPOINT>(&cr), 2);
                    RECT sr = {cr.left - 18, cr.top + (cr.bottom - cr.top - 12) / 2, cr.left - 6, cr.top + (cr.bottom - cr.top - 12) / 2 + 12};
                    if (mx >= sr.left && mx < sr.right && my >= sr.top && my < sr.bottom) {
                        CHOOSECOLORW cc = {};
                        cc.lStructSize = sizeof(cc);
                        cc.hwndOwner = hwnd;
                        cc.lpCustColors = g_custom_colors;
                        cc.rgbResult = channel_color(i);
                        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                        if (ChooseColorW(&cc)) {
                            const SettingsSnapshot before = capture_settings_snapshot();
                            if (i >= g.channel_colors.size()) g.channel_colors.resize(g.ds.channel_count());
                            g.channel_colors[i] = cc.rgbResult;
                            record_settings_change(before);
                            InvalidateRect(hwnd, nullptr, FALSE);
                        }
                        return 0;
                    }
                }
            }

            if (mx < p.left || mx > p.right || my < p.top || my > p.bottom) return 0;
            // Creation tools deliberately take priority over existing
            // annotations, so a point, marker, or guide can be placed at an
            // already marked coordinate. In cursor mode a press on an object
            // selects it and starts a possible drag.
            if (!placing_annotation && begin_annotation_drag(hwnd,mx,my)) return 0;
            clear_annotation_selection();
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if ((g.mode == AnalysisMode::Time) && shift && !g.pending_line && !g.pending_marker && !g.measure_mode) {
                const int pw = p.right - p.left;
                if (pw > 0) {
                    const int clamped_x = std::clamp(mx, static_cast<int>(p.left), static_cast<int>(p.right));
                    const double frac = static_cast<double>(clamped_x - p.left) / pw;
                    const double tt = raw_time_from_stitched(
                        stitched_time_from_raw(g.win_start) + frac *
                        (stitched_time_from_raw(g.win_end) - stitched_time_from_raw(g.win_start)));
                    if (fft_window_contains_time(tt)) {
                        clear_fft_window();
                        set_status();
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                    g.fft_selecting = true;
                    g.fft_select_anchor_x = clamped_x;
                    g.fft_select_current_x = clamped_x;
                    g.fft_select_anchor_t = tt;
                    g.fft_select_current_t = tt;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            if ((g.measure_mode || g.pending_line || g.pending_marker) && !g.annotations_locked) {
                if (!prepare_plot_drag(mx, my)) return 0;
                g.point_click_pending = true;
                g.point_click_reference_axis = false;
                g.point_click_x = mx;
                g.point_click_y = my;
                SetCapture(hwnd);
                return 0;
            }
            if (!prepare_plot_drag(mx, my)) return 0;
            g.gap_click_pending = false;
            g.gap_click_index = -1;
            g.dragging = true;
            SetCapture(hwnd);
            return 0;
        }
        case WM_RBUTTONDOWN:
            if (g.annotations_locked) return 0;
            if (g.gap_details_visible) {
                hide_gap_details_card();
            }
            if (has_fft_window()) {
                clear_fft_window();
                set_status();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            return 0;
        case WM_MOUSEMOVE: {
            if (g.annotation_drag_kind!=App::AnnotationDragKind::None) {
                update_annotation_drag(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
                InvalidateRect(hwnd,nullptr,FALSE);
                return 0;
            }
            if (g.fft_selecting) {
                const RECT p = plot_rect();
                const int pw = p.right - p.left;
                if (pw > 0) {
                    const int clamped_x = std::clamp(GET_X_LPARAM(lp), static_cast<int>(p.left), static_cast<int>(p.right));
                    const double frac = static_cast<double>(clamped_x - p.left) / pw;
                    g.fft_select_current_x = clamped_x;
                    g.fft_select_current_t = raw_time_from_stitched(
                        stitched_time_from_raw(g.win_start) + frac *
                        (stitched_time_from_raw(g.win_end) - stitched_time_from_raw(g.win_start)));
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (g.point_click_pending) {
                const int dx = GET_X_LPARAM(lp) - g.point_click_x;
                const int dy = GET_Y_LPARAM(lp) - g.point_click_y;
                if (std::abs(dx) < 4 && std::abs(dy) < 4) return 0;
                g.point_click_pending = false;
                g.point_click_reference_axis = false;
                g.dragging = true;
            }
            const int hovered_marker = hit_test_marker(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (hovered_marker >= 0 && hovered_marker != g.active_marker) {
                g.active_marker = hovered_marker;
                set_status();
                RECT rc; GetClientRect(hwnd, &rc);
                RECT sr = {0, rc.bottom - kBottomBar, rc.right, rc.bottom};
                InvalidateRect(hwnd, &sr, FALSE);
            }
            if (g.gap_click_pending && !g.dragging) {
                const int dx = GET_X_LPARAM(lp) - g.drag_x;
                const int dy = GET_Y_LPARAM(lp) - g.drag_y;
                if (std::abs(dx) >= 4 || std::abs(dy) >= 4) {
                    g.gap_click_pending = false;
                    g.gap_click_index = -1;
                    g.dragging = true;
                } else {
                    return 0;
                }
            }
            if (!g.dragging) return 0;
            double *lo, *hi, minb, maxb, minw;
            if (!active_axis(lo, hi, minb, maxb, minw)) return 0;
            const RECT p = plot_rect();
            const int pw = p.right - p.left;
            const double span = g.drag_hi - g.drag_lo;
            const double d = static_cast<double>(GET_X_LPARAM(lp) - g.drag_x) / pw * span;
            *lo = g.drag_lo - d;
            *hi = g.drag_hi - d;
            clamp_range(*lo, *hi, minb, maxb, minw);
            // Vertical panning
            if (g.vertical_pan) {
                const int ph = p.bottom - p.top;
                if (ph > 0) {
                    const double dy = static_cast<double>(GET_Y_LPARAM(lp) - g.drag_y) / ph * (g.drag_y_hi - g.drag_y_lo);
                    if ((g.mode == AnalysisMode::FFT)) {
                        double new_ytop = g.drag_y_hi + dy;
                        if (new_ytop < 1e-12) new_ytop = 1e-12;
                        g.y_amp_max = new_ytop;
                        g.auto_y_amp = false;
                    } else {
                        double new_lo = g.drag_y_lo + dy;
                        double new_hi = g.drag_y_hi + dy;
                        g.y_lock_min = new_lo;
                        g.y_lock_max = new_hi;
                        g.auto_y = false;
                        if (g.autoy) { SendMessageW(g.autoy, BM_SETCHECK, BST_UNCHECKED, 0); InvalidateRect(g.autoy, nullptr, FALSE); }
                    }
                }
            }
            set_status();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP:
            if (g.annotation_drag_kind!=App::AnnotationDragKind::None) {
                finish_annotation_drag(true);
                if (GetCapture()==hwnd) ReleaseCapture();
                set_status(); InvalidateRect(hwnd,nullptr,FALSE);
                return 0;
            }
            if (g.fft_selecting) {
                g.fft_selecting = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                if (std::abs(g.fft_select_current_x - g.fft_select_anchor_x) >= 4) {
                    set_fft_window(g.fft_select_anchor_t, g.fft_select_current_t);
                }
                set_status();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g.gap_click_pending) {
                const int pending_gap_index = g.gap_click_index;
                g.gap_click_pending = false;
                g.gap_click_index = -1;
                if (GetCapture() == hwnd) ReleaseCapture();
                if ((g.mode == AnalysisMode::Time) && g.show_gap_markers) {
                    const int released_gap_index = hit_test_gap_marker(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                    if (released_gap_index >= 0 && released_gap_index == pending_gap_index) {
                        const auto& gap = g.visible_gap_markers[static_cast<std::size_t>(released_gap_index)];
                        show_gap_details_card(gap.duration, gap.estimated_missing_samples);
                    }
                }
                return 0;
            }
            if (g.point_click_pending) {
                const int point_x = g.point_click_x, point_y = g.point_click_y;
                g.point_click_pending = false;
                g.point_click_reference_axis = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                double dx, dy;
                if (px_to_data(point_x, point_y, dx, dy)) {
                    if (g.measure_mode) {
                        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                        if (g.snap_to_data) snap_to_nearest(dx, dy);
                    bool created_group = false;
                    const int group_index = ensure_point_group_for_measurement(ctrl, &created_group);
                    if (group_index >= 0 && group_index < static_cast<int>(g.point_groups.size()) &&
                        !point_group_contains_point(group_index,dx,dy)) {
                        g.point_groups[static_cast<std::size_t>(group_index)].points.push_back({dx, dy});
                        UndoAction ua;
                        ua.type = UndoAction::ADD_POINT;
                        ua.point = {dx, dy};
                        ua.point_group_index = group_index;
                        ua.point_group_created = created_group;
                        ua.point_group_state = g.point_groups[static_cast<std::size_t>(group_index)];
                        ua.point_group_state.points.clear();
                        push_undo(ua);
                        refresh_side_panel_controls();
                        set_status();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    } else if (g.pending_line) {
                        GuideLine line;
                        line.vertical=g.pending_line==1;
                        line.mode=g.mode;
                        if (g.snap_to_data) {
                            if (g.mode==AnalysisMode::FRF) snap_to_displayed_frf_curve(dx,dy);
                            else snap_to_nearest(dx,dy);
                        }
                        line.value=line.vertical ? dx : dy;
                        if (!guide_exists_at_current_position(line.vertical,line.value)) {
                            g.guides.push_back(line);
                            UndoAction action; action.type=UndoAction::ADD_LINE; action.line=line;
                            push_undo(action);
                        }
                        set_status(); sync_menu(); InvalidateRect(hwnd,nullptr,FALSE);
                    } else if (g.pending_marker) {
                        App::Marker marker;
                        int channel=-1;
                        const bool snapped=g.snap_to_data && snap_to_nearest_target(dx,dy,&channel);
                        marker.x=dx; marker.y=dy; marker.freq=g.mode==AnalysisMode::FFT;
                        marker.mode=g.mode; marker.snapped=snapped; marker.channel=snapped ? channel : -1;
                        wchar_t label[16]{}; swprintf(label,16,L"M%zu",g.markers.size()+1);
                        marker.label=label;
                        if (!marker_exists_at_current_position(marker.x,marker.y)) {
                            g.markers.push_back(marker);
                            g.active_marker=static_cast<int>(g.markers.size())-1;
                            UndoAction action; action.type=UndoAction::ADD_MARKER; action.marker=marker;
                            push_undo(action);
                        }
                        set_status(); sync_menu(); InvalidateRect(hwnd,nullptr,FALSE);
                    }
                }
                return 0;
            }
            if (g.dragging) { g.dragging = false; ReleaseCapture(); }
            return 0;
        case WM_KEYDOWN:
            if (wp==VK_DELETE || wp==VK_BACK) {
                if (delete_selected_annotation()) {
                    set_status();
                    InvalidateRect(hwnd,nullptr,FALSE);
                }
                return 0;
            }
            if (g.gap_details_visible && wp != VK_ESCAPE) {
                hide_gap_details_card();
            }
            if (wp == VK_ESCAPE) {
                if (g.annotation_drag_kind!=App::AnnotationDragKind::None) {
                    finish_annotation_drag(false);
                    if (GetCapture()==hwnd) ReleaseCapture();
                    InvalidateRect(hwnd,nullptr,FALSE);
                    return 0;
                }
                if (g.point_click_pending) {
                    g.point_click_pending = false;
                    g.point_click_reference_axis = false;
                    if (GetCapture() == hwnd) ReleaseCapture();
                    return 0;
                }
                if (g.gap_details_visible) {
                    hide_gap_details_card();
                    return 0;
                }
                if (g.pending_line || g.pending_marker) {
                    g.pending_line = 0;
                    g.pending_marker = false;
                    set_status();
                    sync_menu();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (g.fft_selecting) {
                    g.fft_selecting = false;
                    if (GetCapture() == hwnd) ReleaseCapture();
                    set_status();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (has_fft_window()) {
                    clear_fft_window();
                    set_status();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace gui

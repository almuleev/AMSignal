// Export metadata serialization and state restoration on import.
#include "gui_export_metadata.hpp"
#include "gui_gap_details.hpp"
#include "gui_processing.hpp"
#include "gui_settings.hpp"
#include "gui_state_history.hpp"
#include "gui_state.hpp"
#include "gui_theme.hpp"
#include "gui_text.hpp"

namespace gui {

std::wstring export_channel_label_text(std::size_t channel_index, bool include_names) {
    if (!include_names) {
        return L"Channel_" + std::to_wstring(channel_index + 1);
    }
    return channel_display_label(channel_index);
}

const wchar_t* export_filter_mode_key(int mode) {
    switch (mode) {
        case FilterModeLowPass: return L"low_pass";
        case FilterModeHighPass: return L"high_pass";
        case FilterModeBandPass: return L"band_pass";
        case FilterModeBandStop: return L"band_stop";
    }
    return L"band_pass";
}

const wchar_t* export_filter_topology_key(int topology) {
    switch (topology) {
        case FilterTopologyButterworth: return L"butterworth";
        case FilterTopologyBessel: return L"bessel";
        case FilterTopologyChebyshev: return L"chebyshev";
        case FilterTopologyLinkwitzRiley: return L"linkwitz_riley";
    }
    return L"butterworth";
}

std::wstring export_color_triplet(COLORREF color) {
    wchar_t buf[64]{};
    swprintf(buf, 64, L"%u,%u,%u", static_cast<unsigned int>(GetRValue(color)),
             static_cast<unsigned int>(GetGValue(color)), static_cast<unsigned int>(GetBValue(color)));
    return buf;
}

std::wstring export_point_display_text(const PointDisplay& display) {
    wchar_t buf[160]{};
    swprintf(buf, 160, L"number=%d,x=%d,y=%d,dx=%d,dy=%d,inv_dt=%d,dist=%d",
             display.number ? 1 : 0, display.x ? 1 : 0, display.y ? 1 : 0,
             display.dx ? 1 : 0, display.dy ? 1 : 0, display.inv_dt ? 1 : 0,
             display.dist ? 1 : 0);
    return buf;
}

void write_export_comment(std::ofstream& out, const std::wstring& text, const char* line_end) {
    out << "# " << to_utf8(text) << line_end;
}

void write_export_key_value(std::ofstream& out, const std::wstring& key, const std::wstring& value, const char* line_end) {
    write_export_comment(out, key + L"=" + value, line_end);
}

const wchar_t* export_range_mode_key(ExportRangeMode range) {
    switch (range) {
        case ExportRangeMode::Selected: return L"selected";
        case ExportRangeMode::Visible: return L"visible";
        case ExportRangeMode::Whole: return L"whole";
    }
    return L"visible";
}

const wchar_t* export_processing_mode_key(bool apply_processing) {
    return apply_processing ? L"applied_to_data" : L"settings_only";
}

std::wstring export_metadata_text(const std::wstring& value) {
    const std::string utf8 = to_utf8(value);
    const wchar_t* hex = L"0123456789ABCDEF";
    std::wstring encoded;
    for (unsigned char ch : utf8) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == ' ' || ch == '_' || ch == '-' || ch == '.') encoded += ch;
        else { encoded += L'%'; encoded += hex[ch >> 4]; encoded += hex[ch & 15]; }
    }
    return encoded;
}

void write_export_metadata(std::ofstream& out,
                           const ExportOptions& opts,
                           bool csv,
                           double range_start,
                           double range_end,
                           bool actual_selected_range,
                           const std::vector<std::size_t>& exported_channels) {
    if (!opts.include_metadata) return;
    const char* line_end = csv ? "\n" : "\r\n";

    write_export_comment(out, L"[export]", line_end);
    write_export_key_value(out, L"schema_version", L"2", line_end);
    if (opts.save_mode == ExportSaveMode::Project) {
        write_export_key_value(out, L"document_kind", L"amsignal_project", line_end);
    }
    write_export_key_value(out, L"text_encoding", L"percent_utf8", line_end);
    write_export_key_value(out, L"range_mode", export_range_mode_key(opts.selected_range), line_end);
    write_export_key_value(out, L"range_source", actual_selected_range ? L"selected" : L"visible", line_end);
    write_export_key_value(out, L"data_mode", export_processing_mode_key(opts.apply_processing_to_data), line_end);
    write_export_key_value(out, L"plot_mode", (g.mode == AnalysisMode::FFT) ? L"frequency" : L"time", line_end);
    write_export_key_value(out, L"range_start", format_edit_number(range_start), line_end);
    write_export_key_value(out, L"range_end", format_edit_number(range_end), line_end);
    if (!g.file_name.empty()) {
        write_export_key_value(out, L"source_file", export_metadata_text(g.file_name), line_end);
    }
    write_export_key_value(out, L"partial_fragment", g.current_file_partial ? L"1" : L"0", line_end);
    write_export_comment(out, L"", line_end);

    if (opts.include_graph_settings) {
        write_export_comment(out, L"[graph_settings]", line_end);
        write_export_key_value(out, L"axis_x_label", export_metadata_text(g.axis_x_label), line_end);
        write_export_key_value(out, L"axis_y_label", export_metadata_text(g.axis_y_label), line_end);
        write_export_key_value(out, L"marker_color", export_color_triplet(g.marker_color), line_end);
        write_export_key_value(out, L"smoothing", g.visual_smooth ? L"1" : L"0", line_end);
        write_export_key_value(out, L"distinguish_curves", g.distinguish_curves ? L"1" : L"0", line_end);
        write_export_key_value(out, L"vertical_pan", g.vertical_pan ? L"1" : L"0", line_end);
        write_export_key_value(out, L"snap_to_data", g.snap_to_data ? L"1" : L"0", line_end);
        write_export_key_value(out, L"show_gap_markers", g.show_gap_markers ? L"1" : L"0", line_end);
        write_export_key_value(out, L"stitch_time_gaps", g.stitch_time_gaps ? L"1" : L"0", line_end);
        write_export_key_value(out, L"light_mode", g.light_mode ? L"1" : L"0", line_end);
        write_export_key_value(out, L"auto_y", g.auto_y ? L"1" : L"0", line_end);
        write_export_key_value(out, L"y_lock_min", format_optional_edit_number(g.y_lock_min), line_end);
        write_export_key_value(out, L"y_lock_max", format_optional_edit_number(g.y_lock_max), line_end);
        write_export_key_value(out, L"auto_y_amp", g.auto_y_amp ? L"1" : L"0", line_end);
        write_export_key_value(out, L"y_amp_max", format_optional_edit_number(g.y_amp_max), line_end);
        write_export_key_value(out, L"frf_apply_processing", g.frf.apply_processing ? L"1" : L"0", line_end);
        write_export_key_value(out, L"frf_logarithmic_frequency_axis", g.frf.logarithmic_frequency_axis ? L"1" : L"0", line_end);
        write_export_key_value(out, L"frf_show_reference_amplitude", g.frf.show_reference_amplitude ? L"1" : L"0", line_end);
        write_export_key_value(out, L"frf_reference_height_fraction", format_edit_number(g.frf.reference_height_fraction), line_end);
        write_export_key_value(out, L"frf_reference_auto_y", g.frf.reference_auto_y ? L"1" : L"0", line_end);
        write_export_key_value(out, L"frf_reference_y_max", format_optional_edit_number(g.frf.reference_y_max), line_end);
        write_export_key_value(out, L"play_speed", format_edit_number(g.play_speed), line_end);
        write_export_key_value(out, L"point_display", export_point_display_text(g.pdisp), line_end);
        write_export_comment(out, L"", line_end);
    }

    if (opts.include_filter_settings) {
        write_export_comment(out, L"[filter_settings]", line_end);
        write_export_key_value(out, L"enabled", g.noise_threshold_enabled ? L"1" : L"0", line_end);
        write_export_key_value(out, L"mode", export_filter_mode_key(g.noise_threshold_mode), line_end);
        write_export_key_value(out, L"topology", export_filter_topology_key(g.noise_threshold_topology), line_end);
        write_export_key_value(out, L"low_cutoff", format_optional_edit_number(g.noise_threshold_min), line_end);
        write_export_key_value(out, L"high_cutoff", format_optional_edit_number(g.noise_threshold_max), line_end);
        write_export_comment(out, L"", line_end);
    }

    if (opts.include_channel_names || opts.include_hidden_channels) {
        write_export_comment(out, L"[channels]", line_end);
        ensure_channel_formula_vectors();
        for (std::size_t j = 0; j < exported_channels.size(); ++j) {
            const std::size_t c = exported_channels[j];
            std::wstring line = L"channel[" + std::to_wstring(j + 1) + L"] name=" + export_metadata_text(export_channel_label_text(c, opts.include_channel_names));
            line += L", visible=";
            line += (c < g.visible.size() && g.visible[c]) ? L"1" : L"0";
            line += L", color=" + export_color_triplet(channel_color(c));
            write_export_comment(out, line, line_end);
        }
        write_export_comment(out, L"", line_end);
    }

    if (opts.include_formulas) {
        write_export_comment(out, L"[formulas]", line_end);
        write_export_key_value(out, L"global", export_metadata_text(g.global_formula), line_end);
        for (std::size_t j = 0; j < exported_channels.size(); ++j) {
            const std::size_t c = exported_channels[j];
            write_export_key_value(out, L"channel[" + std::to_wstring(j + 1) + L"]", export_metadata_text(g.channel_formulas[c]), line_end);
        }
        write_export_comment(out, L"", line_end);
    }

    if (opts.include_points && !g.point_groups.empty()) {
        write_export_comment(out, L"[point_groups]", line_end);
        for (std::size_t i = 0; i < g.point_groups.size(); ++i) {
            const auto& group = g.point_groups[i];
            std::wstring line = L"group[" + std::to_wstring(i + 1) + L"] name=" + export_metadata_text(group.name);
            line += L", visible=" + std::wstring(group.visible ? L"1" : L"0");
            line += L", color=" + export_color_triplet(group.color);
            line += L", mode=" + std::wstring(group.mode == PointGroupMode::FRF ? L"frf" :
                group.mode == PointGroupMode::Frequency ? L"frequency" : L"time");
            line += L", active=" + std::wstring((i == static_cast<std::size_t>(active_point_group_index_for_mode(group.mode))) ? L"1" : L"0");
            line += L", display=" + export_point_display_text(group.display);
            write_export_comment(out, line, line_end);
            for (std::size_t j = 0; j < group.points.size(); ++j) {
                const auto& pt = group.points[j];
                std::wstring point_line = L"point[" + std::to_wstring(j + 1) + L"] x=" + format_edit_number(pt.first);
                point_line += L", y=" + format_edit_number(pt.second);
                write_export_comment(out, point_line, line_end);
            }
        }
        write_export_comment(out, L"", line_end);
    }

    if (opts.include_markers && !g.markers.empty()) {
        write_export_comment(out, L"[markers]", line_end);
        for (std::size_t i = 0; i < g.markers.size(); ++i) {
            const auto& m = g.markers[i];
            std::wstring line = L"marker[" + std::to_wstring(i + 1) + L"] label=" + export_metadata_text(m.label);
            line += L", x=" + format_edit_number(m.x);
            line += L", y=" + format_edit_number(m.y);
            line += L", mode=" + std::wstring(m.mode == AnalysisMode::FRF ? L"frf" : m.freq ? L"frequency" : L"time");
            const auto found = m.channel >= 0 ? std::find(exported_channels.begin(), exported_channels.end(), static_cast<std::size_t>(m.channel)) : exported_channels.end();
            const int export_channel = found == exported_channels.end() ? -1 : static_cast<int>(found - exported_channels.begin());
            line += L", snapped=" + std::wstring(m.snapped && export_channel >= 0 ? L"1" : L"0");
            line += L", channel=" + std::to_wstring(export_channel);
            write_export_comment(out, line, line_end);
        }
        write_export_comment(out, L"", line_end);
    }

    if (opts.include_guides && !g.guides.empty()) {
        write_export_comment(out, L"[guides]", line_end);
        for (std::size_t i = 0; i < g.guides.size(); ++i) {
            const auto& gl = g.guides[i];
            std::wstring line = L"guide[" + std::to_wstring(i + 1) + L"] kind=" + (gl.vertical ? L"vertical" : L"horizontal");
            line += L", value=" + format_edit_number(gl.value);
            line += L", mode=" + std::wstring(gl.mode == AnalysisMode::FRF ? L"frf" :
                gl.mode == AnalysisMode::FFT ? L"frequency" : L"time");
            write_export_comment(out, line, line_end);
        }
        write_export_comment(out, L"", line_end);
    }
}

void apply_export_metadata_from_comments(const std::vector<std::string>& comments) {
    if (comments.empty() || g.ds.channel_count() == 0) return;
    const bool encoded_text = std::find(comments.begin(), comments.end(), "text_encoding=percent_utf8") != comments.end();
    const bool already_processed = g.ds.frequency_axis || std::find(comments.begin(), comments.end(), "data_mode=applied_to_data") != comments.end();

    enum class ExportSection {
        None,
        Export,
        Graph,
        Filter,
        Channels,
        Formulas,
        PointGroups,
        Markers,
        Guides,
    };

    auto trim_copy = [](const std::string& text) {
        const char* ws = " \t\r\n\f\v";
        const std::size_t begin = text.find_first_not_of(ws);
        if (begin == std::string::npos) return std::string();
        const std::size_t end = text.find_last_not_of(ws);
        return text.substr(begin, end - begin + 1);
    };
    auto lower_copy = [](std::string text) {
        for (char& ch : text) {
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        }
        return text;
    };
    auto parse_bool = [&](const std::string& text, bool& out) {
        const std::string value = lower_copy(trim_copy(text));
        if (value == "1" || value == "true" || value == "yes" || value == "on") {
            out = true;
            return true;
        }
        if (value == "0" || value == "false" || value == "no" || value == "off") {
            out = false;
            return true;
        }
        int numeric = 0;
        char tail = '\0';
        if (std::sscanf(value.c_str(), "%d%c", &numeric, &tail) == 1) {
            out = (numeric != 0);
            return true;
        }
        return false;
    };
    auto parse_int = [&](const std::string& text, int& out) {
        const std::string value = trim_copy(text);
        if (value.empty()) return false;
        char* end = nullptr;
        const long parsed = std::strtol(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != '\0' || parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) return false;
        out = static_cast<int>(parsed);
        return true;
    };
    auto parse_double = [&](const std::string& text, double& out) {
        const std::string value = trim_copy(text);
        if (value.empty()) return false;
        char* end = nullptr;
        const double parsed = std::strtod(value.c_str(), &end);
        if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
        out = parsed;
        return true;
    };
    auto parse_color_triplet = [&](const std::string& text, COLORREF& out) {
        unsigned int r = 0, g = 0, b = 0;
        if (std::sscanf(text.c_str(), "%u,%u,%u", &r, &g, &b) != 3) return false;
        out = RGB(static_cast<BYTE>(std::clamp(r, 0u, 255u)),
                  static_cast<BYTE>(std::clamp(g, 0u, 255u)),
                  static_cast<BYTE>(std::clamp(b, 0u, 255u)));
        return true;
    };
    auto decode_export_text = [&](const std::string& text) {
        if (text.empty()) return std::wstring();
        std::string decoded;
        auto hex = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            return -1;
        };
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (encoded_text && text[i] == '%' && i + 2 < text.size() && hex(text[i+1]) >= 0 && hex(text[i+2]) >= 0) {
                decoded += static_cast<char>((hex(text[i+1]) << 4) | hex(text[i+2])); i += 2;
            } else decoded += text[i];
        }
        std::wstring utf8 = to_w(decoded);
        if (!utf8.empty()) return utf8;
        return to_w_acp(decoded);
    };
    auto extract_after = [&](const std::string& text, const std::string& prefix) {
        const std::size_t pos = text.find(prefix);
        if (pos == std::string::npos) return std::string();
        return trim_copy(text.substr(pos + prefix.size()));
    };
    auto extract_between = [&](const std::string& text, const std::string& prefix, const std::string& suffix) {
        const std::size_t pos = text.find(prefix);
        if (pos == std::string::npos) return std::string();
        const std::size_t begin = pos + prefix.size();
        if (suffix.empty()) return trim_copy(text.substr(begin));
        const std::size_t end = text.find(suffix, begin);
        if (end == std::string::npos) return trim_copy(text.substr(begin));
        return trim_copy(text.substr(begin, end - begin));
    };
    auto parse_point_display = [&](const std::string& text, PointDisplay& display) {
        bool any = false;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t comma = text.find(',', start);
            const std::string token = trim_copy(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            if (!token.empty()) {
                const std::size_t eq = token.find('=');
                if (eq != std::string::npos) {
                    const std::string key = lower_copy(trim_copy(token.substr(0, eq)));
                    bool value = false;
                    if (parse_bool(token.substr(eq + 1), value)) {
                        any = true;
                        if (key == "number") display.number = value;
                        else if (key == "x") display.x = value;
                        else if (key == "y") display.y = value;
                        else if (key == "dx") display.dx = value;
                        else if (key == "dy") display.dy = value;
                        else if (key == "inv_dt") display.inv_dt = value;
                        else if (key == "dist") display.dist = value;
                    }
                }
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return any;
    };
    auto parse_filter_mode = [&](const std::string& text, int& out) {
        const std::string value = lower_copy(trim_copy(text));
        if (value == "low_pass" || value == "low-pass") { out = FilterModeLowPass; return true; }
        if (value == "high_pass" || value == "high-pass") { out = FilterModeHighPass; return true; }
        if (value == "band_pass" || value == "band-pass" || value == "bandpass") { out = FilterModeBandPass; return true; }
        if (value == "band_stop" || value == "band-stop" || value == "bandstop") { out = FilterModeBandStop; return true; }
        return false;
    };
    auto parse_filter_topology = [&](const std::string& text, int& out) {
        const std::string value = lower_copy(trim_copy(text));
        if (value == "butterworth") { out = FilterTopologyButterworth; return true; }
        if (value == "bessel") { out = FilterTopologyBessel; return true; }
        if (value == "chebyshev") { out = FilterTopologyChebyshev; return true; }
        if (value == "linkwitz_riley" || value == "linkwitz-riley") { out = FilterTopologyLinkwitzRiley; return true; }
        return false;
    };
    auto parse_section_name = [&](const std::string& text) {
        const std::string section = lower_copy(trim_copy(text));
        if (section == "[export]") return ExportSection::Export;
        if (section == "[graph_settings]" || section == "[graph]") return ExportSection::Graph;
        if (section == "[filter_settings]" || section == "[filter]") return ExportSection::Filter;
        if (section == "[channels]") return ExportSection::Channels;
        if (section == "[formulas]") return ExportSection::Formulas;
        if (section == "[point_groups]") return ExportSection::PointGroups;
        if (section == "[markers]") return ExportSection::Markers;
        if (section == "[guides]") return ExportSection::Guides;
        return ExportSection::None;
    };
    auto parse_indexed_line = [&](const std::string& text, const char* prefix, int& out_index, std::string& tail) {
        const std::string line = trim_copy(text);
        if (line.rfind(prefix, 0) != 0) return false;
        const std::size_t lb = line.find('[');
        const std::size_t rb = line.find(']', lb == std::string::npos ? 0 : lb + 1);
        if (lb == std::string::npos || rb == std::string::npos || rb <= lb + 1) return false;
        int one_based = 0;
        if (!parse_int(line.substr(lb + 1, rb - lb - 1), one_based) || one_based <= 0) return false;
        out_index = one_based - 1;
        tail = trim_copy(line.substr(rb + 1));
        return true;
    };

    ExportSection section = ExportSection::None;
    bool imported_formulas = false;
    std::vector<PointGroup> point_groups;
    std::vector<App::Marker> markers;
    std::vector<GuideLine> guides;
    int current_group = -1;
    int active_time_group = -1;
    int active_freq_group = -1;
    int active_frf_group = -1;
    int last_time_group = -1;
    int last_freq_group = -1;
    int last_frf_group = -1;

    for (const std::string& raw_comment : comments) {
        const std::string line = trim_copy(raw_comment);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = parse_section_name(line);
            if (section != ExportSection::PointGroups) {
                current_group = -1;
            }
            continue;
        }
        if (section == ExportSection::None) continue;

        if (section == ExportSection::Export) {
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = lower_copy(trim_copy(line.substr(0, eq)));
            const std::string value = trim_copy(line.substr(eq + 1));
            if (key == "partial_fragment") {
                bool partial = false;
                if (parse_bool(value, partial) && partial) {
                    g.current_file_partial = true;
                }
            }
            continue;
        }

        if (section == ExportSection::Graph) {
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = lower_copy(trim_copy(line.substr(0, eq)));
            const std::string value = trim_copy(line.substr(eq + 1));
            if (key == "axis_x_label") {
                g.axis_x_label = normalize_axis_label_text(decode_export_text(value), L"X");
            } else if (key == "axis_y_label") {
                g.axis_y_label = normalize_axis_label_text(decode_export_text(value), L"Y");
            } else if (key == "marker_color") {
                parse_color_triplet(value, g.marker_color);
            } else if (key == "smoothing") {
                parse_bool(value, g.visual_smooth);
            } else if (key == "distinguish_curves") {
                parse_bool(value, g.distinguish_curves);
            } else if (key == "vertical_pan") {
                parse_bool(value, g.vertical_pan);
            } else if (key == "snap_to_data") {
                parse_bool(value, g.snap_to_data);
            } else if (key == "show_gap_markers") {
                parse_bool(value, g.show_gap_markers);
            } else if (key == "stitch_time_gaps") {
                parse_bool(value, g.stitch_time_gaps);
                if (g.stitch_time_gaps) hide_gap_details_card();
            } else if (key == "light_mode") {
                // Loading policy is a user preference, not a property of the signal.
            } else if (key == "auto_y") {
                parse_bool(value, g.auto_y);
            } else if (key == "y_lock_min") {
                parse_double(value, g.y_lock_min);
            } else if (key == "y_lock_max") {
                parse_double(value, g.y_lock_max);
            } else if (key == "auto_y_amp") {
                parse_bool(value, g.auto_y_amp);
            } else if (key == "y_amp_max") {
                parse_double(value, g.y_amp_max);
            } else if (key == "frf_apply_processing") {
                parse_bool(value, g.frf.apply_processing);
            } else if (key == "frf_logarithmic_frequency_axis") {
                parse_bool(value, g.frf.logarithmic_frequency_axis);
            } else if (key == "frf_show_reference_amplitude") {
                parse_bool(value, g.frf.show_reference_amplitude);
            } else if (key == "frf_reference_height_fraction") {
                double fraction=0;
                if (parse_double(value, fraction) && std::isfinite(fraction))
                    g.frf.reference_height_fraction=std::clamp(fraction,.12,.55);
            } else if (key == "frf_reference_auto_y") {
                parse_bool(value, g.frf.reference_auto_y);
            } else if (key == "frf_reference_y_max") {
                double ymax=0;
                if (parse_double(value,ymax) && std::isfinite(ymax) && ymax>0)
                    g.frf.reference_y_max=ymax;
            } else if (key == "play_speed") {
                double speed = g.play_speed;
                if (parse_double(value, speed) && std::isfinite(speed) && speed > 0.0) g.play_speed = speed;
            } else if (key == "point_display") {
                parse_point_display(value, g.pdisp);
            }
            continue;
        }

        if (section == ExportSection::Filter) {
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = lower_copy(trim_copy(line.substr(0, eq)));
            const std::string value = trim_copy(line.substr(eq + 1));
            if (key == "enabled") {
                parse_bool(value, g.noise_threshold_enabled);
            } else if (key == "mode") {
                parse_filter_mode(value, g.noise_threshold_mode);
            } else if (key == "topology") {
                parse_filter_topology(value, g.noise_threshold_topology);
            } else if (key == "low_cutoff") {
                parse_double(value, g.noise_threshold_min);
            } else if (key == "high_cutoff") {
                parse_double(value, g.noise_threshold_max);
            }
            continue;
        }

        if (section == ExportSection::Channels) {
            int channel_index = -1;
            std::string tail;
            if (!parse_indexed_line(line, "channel", channel_index, tail)) continue;
            if (channel_index < 0 || static_cast<std::size_t>(channel_index) >= g.ds.channel_count()) continue;
            const std::string name = extract_between(tail, "name=", ", visible=");
            const std::string visible_text = extract_between(tail, "visible=", ", color=");
            const std::string color_text = extract_after(tail, "color=");
            if (!name.empty()) {
                const std::wstring wname = decode_export_text(name);
                g.channel_labels[static_cast<std::size_t>(channel_index)] = wname;
                g.ds.names[static_cast<std::size_t>(channel_index)] = to_utf8(wname);
            }
            bool visible = true;
            if (parse_bool(visible_text, visible)) {
                g.visible[static_cast<std::size_t>(channel_index)] = visible ? 1 : 0;
            }
            COLORREF color = channel_color(static_cast<std::size_t>(channel_index));
            if (parse_color_triplet(color_text, color)) {
                if (static_cast<std::size_t>(channel_index) >= g.channel_colors.size()) {
                    g.channel_colors.resize(g.ds.channel_count());
                }
                g.channel_colors[static_cast<std::size_t>(channel_index)] = color;
            }
            continue;
        }

        if (section == ExportSection::Formulas) {
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = lower_copy(trim_copy(line.substr(0, eq)));
            const std::string value = trim_copy(line.substr(eq + 1));
            if (key == "global") {
                g.global_formula = decode_export_text(value);
                imported_formulas = true;
            } else {
                int channel_index = -1;
                std::string tail;
                if (parse_indexed_line(line, "channel", channel_index, tail) && channel_index >= 0 &&
                    static_cast<std::size_t>(channel_index) < g.channel_formulas.size()) {
                    g.channel_formulas[static_cast<std::size_t>(channel_index)] = decode_export_text(value);
                    imported_formulas = true;
                }
            }
            continue;
        }

        if (section == ExportSection::PointGroups) {
            int group_index = -1;
            std::string tail;
            if (parse_indexed_line(line, "group", group_index, tail)) {
                PointGroup group;
                const std::string name = extract_between(tail, "name=", ", visible=");
                const std::string visible_text = extract_between(tail, "visible=", ", color=");
                const std::string color_text = extract_between(tail, "color=", ", display=");
                const std::string mode_text = extract_between(tail, "mode=", ", active=");
                const std::string active_text = extract_between(tail, "active=", ", display=");
                const std::string display_text = extract_after(tail, "display=");
                group.name = decode_export_text(name);
                group.mode = (g.mode == AnalysisMode::FFT) ? PointGroupMode::Frequency : PointGroupMode::Time;
                const std::string mode = lower_copy(trim_copy(mode_text));
                if (mode == "frequency" || mode == "fft" || mode == "hz") {
                    group.mode = PointGroupMode::Frequency;
                } else if (mode == "frf") {
                    group.mode = PointGroupMode::FRF;
                } else if (mode == "time" || mode == "seconds" || mode == "sec") {
                    group.mode = PointGroupMode::Time;
                }
                bool visible = true;
                if (parse_bool(visible_text, visible)) group.visible = visible;
                parse_color_triplet(color_text, group.color);
                parse_point_display(display_text, group.display);
                if (group.mode == PointGroupMode::Frequency) {
                    last_freq_group = static_cast<int>(point_groups.size());
                } else if (group.mode == PointGroupMode::FRF) {
                    last_frf_group = static_cast<int>(point_groups.size());
                } else {
                    last_time_group = static_cast<int>(point_groups.size());
                }
                bool active = false;
                if (parse_bool(active_text, active) && active) {
                    if (group.mode == PointGroupMode::Frequency) active_freq_group = static_cast<int>(point_groups.size());
                    else if (group.mode == PointGroupMode::FRF) active_frf_group = static_cast<int>(point_groups.size());
                    else active_time_group = static_cast<int>(point_groups.size());
                }
                point_groups.push_back(std::move(group));
                current_group = static_cast<int>(point_groups.size()) - 1;
                continue;
            }
            if (current_group >= 0) {
                int point_index = -1;
                if (parse_indexed_line(line, "point", point_index, tail)) {
                    double x = 0.0;
                    double y = 0.0;
                    if (parse_double(extract_between(tail, "x=", ", y="), x) &&
                        parse_double(extract_after(tail, "y="), y)) {
                        point_groups[static_cast<std::size_t>(current_group)].points.emplace_back(x, y);
                    }
                }
            }
            continue;
        }

        if (section == ExportSection::Markers) {
            int marker_index = -1;
            std::string tail;
            if (!parse_indexed_line(line, "marker", marker_index, tail)) continue;
            App::Marker marker;
            marker.label = decode_export_text(extract_between(tail, "label=", ", x="));
            parse_double(extract_between(tail, "x=", ", y="), marker.x);
            parse_double(extract_between(tail, "y=", ", mode="), marker.y);
            const std::string mode = lower_copy(extract_between(tail, "mode=", ", snapped="));
            marker.freq = (mode == "frequency" || mode == "frf");
            marker.mode = mode == "frf" ? AnalysisMode::FRF : marker.freq ? AnalysisMode::FFT : AnalysisMode::Time;
            bool snapped = false;
            if (parse_bool(extract_between(tail, "snapped=", ", channel="), snapped)) marker.snapped = snapped;
            parse_int(extract_after(tail, "channel="), marker.channel);
            markers.push_back(std::move(marker));
            continue;
        }

        if (section == ExportSection::Guides) {
            int guide_index = -1;
            std::string tail;
            if (!parse_indexed_line(line, "guide", guide_index, tail)) continue;
            GuideLine guide;
            const std::string kind = lower_copy(extract_between(tail, "kind=", ", value="));
            guide.vertical = !(kind == "horizontal");
            parse_double(extract_between(tail, "value=", ", mode="), guide.value);
            const std::string mode = lower_copy(extract_after(tail, "mode="));
            guide.mode = mode == "frf" ? AnalysisMode::FRF : mode == "frequency" ? AnalysisMode::FFT : AnalysisMode::Time;
            guides.push_back(guide);
            continue;
        }
    }

    normalize_filter_bounds();

    if (!point_groups.empty()) {
        g.point_groups = std::move(point_groups);
        g.time_active_point_group = active_time_group >= 0 ? active_time_group : last_time_group;
        g.freq_active_point_group = active_freq_group >= 0 ? active_freq_group : last_freq_group;
        g.frf_active_point_group = active_frf_group >= 0 ? active_frf_group : last_frf_group;
        normalize_active_point_group();
        sync_point_display_from_active_group();
    }
    if (!markers.empty()) {
        g.markers = std::move(markers);
        g.active_marker = -1;
    }
    if (!guides.empty()) {
        g.guides = std::move(guides);
    }
    if (already_processed) {
        g.global_formula = L"x";
        g.channel_formulas.assign(g.ds.channel_count(), L"x");
        g.noise_threshold_enabled = false;
        imported_formulas = true;
    }
    if (imported_formulas) {
        g.formula_ini_deferred = false;
        rebuild_formula_cache_from_state();
    }
    if (!g.show_gap_markers) hide_gap_details_card();
    recompute_transforms_from_state();
    sync_channel_controls_from_state();
}

void write_frf_metadata(std::ofstream& out) {
    const auto& s = g.frf;
    const auto& r = s.result.common();
    const char* nl = "\n";
    write_export_comment(out, L"[export]", nl);
    write_export_key_value(out, L"data_kind", L"frf", nl);
    write_export_key_value(out, L"schema_version", L"1", nl);
    write_export_key_value(out, L"text_encoding", L"percent_utf8", nl);
    write_export_key_value(out, L"source_file", export_metadata_text(g.file_name), nl);
    write_export_comment(out, L"[frf]", nl);
    write_export_key_value(out, L"estimator", r.options.estimator==lvm::FrfEstimator::H1 ? L"h1_welch" : L"direct_y_over_x", nl);
    write_export_key_value(out, L"segment_length", std::to_wstring(r.segment_length), nl);
    write_export_key_value(out, L"averages", std::to_wstring(r.averages), nl);
    write_export_key_value(out, L"overlap_samples", std::to_wstring(r.overlap_samples), nl);
    write_export_key_value(out, L"overlap_fraction", to_w(numfmt(static_cast<double>(r.overlap_samples)/r.segment_length)), nl);
    write_export_key_value(out, L"frequency_resolution", to_w(numfmt(1.0/(r.sample_dt*r.segment_length))), nl);
    write_export_key_value(out, L"gap_policy", L"ignore_gaps_uniform_sample_sequence", nl);
    write_export_key_value(out, L"window", L"hann_periodic", nl);
    write_export_key_value(out, L"remove_mean", r.options.remove_mean ? L"1" : L"0", nl);
    if (s.inputs.size()==1) write_export_key_value(out,L"input_index",std::to_wstring(s.inputs.front()),nl);
    if (s.outputs.size()==1) write_export_key_value(out,L"output_index",std::to_wstring(s.outputs.front()),nl);
    write_export_key_value(out,L"reference_average",L"arithmetic_samples",nl);
    for (std::size_t i=0;i<s.inputs.size();++i) {
        const auto prefix=L"reference_"+std::to_wstring(i);
        write_export_key_value(out,prefix+L"_index",std::to_wstring(s.inputs[i]),nl);
        write_export_key_value(out,prefix+L"_name",export_metadata_text(channel_display_label(s.inputs[i])),nl);
    }
    for (std::size_t i=0;i<s.outputs.size();++i) {
        const auto prefix=L"response_"+std::to_wstring(i);
        write_export_key_value(out,prefix+L"_index",std::to_wstring(s.outputs[i]),nl);
        write_export_key_value(out,prefix+L"_name",export_metadata_text(s.output_names[i]),nl);
        write_export_key_value(out,prefix+L"_error",to_w(lvm::frf_error_text(s.result.responses[i].error)),nl);
    }
    write_export_key_value(out, L"input_name", export_metadata_text(s.input_name), nl);
    if (s.output_names.size()==1) write_export_key_value(out,L"output_name",export_metadata_text(s.output_names.front()),nl);
    write_export_key_value(out, L"processing", export_metadata_text(s.processing_description), nl);
    write_export_key_value(out, L"source_start", to_w(numfmt(r.source_start)), nl);
    write_export_key_value(out, L"source_end", to_w(numfmt(r.source_end)), nl);
    write_export_key_value(out, L"source_selection", s.from_selection ? L"1" : L"0", nl);
    write_export_key_value(out, L"sample_count", std::to_wstring(r.sample_count), nl);
    write_export_key_value(out, L"sample_dt", to_w(numfmt(r.sample_dt)), nl);
    write_export_key_value(out, L"resampled", L"0", nl);
    write_export_key_value(out, L"gaps_ignored", r.gaps_ignored ? L"1" : L"0", nl);
    write_export_key_value(out, L"reference_threshold", to_w(numfmt(r.options.reference_threshold)), nl);
    write_export_key_value(out, L"dynamic_coefficient_scale", L"linear_abs_h", nl);
    write_export_key_value(out, L"reference_amplitude_scale", L"linear_one_sided", nl);
    write_export_key_value(out, L"frequency_min", to_w(numfmt(std::pow(10.0, s.log_start))), nl);
    write_export_key_value(out, L"frequency_max", to_w(numfmt(std::pow(10.0, s.log_end))), nl);
    write_export_comment(out, L"", nl);
}

} // namespace gui

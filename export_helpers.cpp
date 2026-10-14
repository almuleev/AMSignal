#include "export_helpers.hpp"

#include <algorithm>

const wchar_t* export_prompt_title_text(bool english) {
    return english ? L"Export data" : L"Экспорт данных";
}

std::wstring export_range_title_text(ExportRangeMode range, bool english) {
    switch (range) {
        case ExportRangeMode::Selected:
            return english ? L"Selected range" : L"Выделенная область";
        case ExportRangeMode::Visible:
            return english ? L"Visible range" : L"Видимая область";
        case ExportRangeMode::Whole:
            return english ? L"Whole file" : L"Весь файл";
    }
    return L"";
}

const wchar_t* export_prompt_continue_text(bool english) {
    return english ? L"Continue" : L"Продолжить";
}

const wchar_t* export_prompt_cancel_text(bool english) {
    return english ? L"Cancel" : L"Отмена";
}

const wchar_t* export_include_channel_names_text(bool english) {
    return english ? L"Channel names" : L"Названия каналов";
}

const wchar_t* export_include_points_text(bool english) {
    return english ? L"Points" : L"Точки";
}

const wchar_t* export_include_markers_text(bool english) {
    return english ? L"Markers" : L"Маркеры";
}

const wchar_t* export_include_guides_text(bool english) {
    return english ? L"Guide lines" : L"Направляющие";
}

const wchar_t* export_include_formulas_text(bool english) {
    return english ? L"Formulas" : L"Формулы";
}

const wchar_t* export_include_filter_text(bool english) {
    return english ? L"Filter settings" : L"Настройки фильтра";
}

const wchar_t* export_include_graph_settings_text(bool english) {
    return english ? L"Graph settings" : L"Настройки графика";
}

std::wstring export_default_name(const std::wstring& stem,
                                 ExportRangeMode range,
                                 const wchar_t* ext,
                                 bool freq_mode) {
    std::wstring out = stem;
    switch (range) {
        case ExportRangeMode::Selected:
            out += freq_mode ? L"_selected_freq" : L"_selected_time";
            break;
        case ExportRangeMode::Visible:
            out += freq_mode ? L"_visible_freq" : L"_visible_time";
            break;
        case ExportRangeMode::Whole:
            out += freq_mode ? L"_whole_freq" : L"_whole_time";
            break;
    }
    out += ext ? ext : L"";
    return out;
}

std::pair<std::size_t, std::size_t> export_range_bounds(const std::vector<double>& values,
                                                        double start,
                                                        double end) {
    if (start > end) std::swap(start, end);
    const auto first = std::lower_bound(values.begin(), values.end(), start);
    const auto last = std::upper_bound(values.begin(), values.end(), end);
    return {
        static_cast<std::size_t>(first - values.begin()),
        static_cast<std::size_t>(last - values.begin())
    };
}

std::wstring export_status_prefix(const wchar_t* format_name,
                                  ExportRangeMode range,
                                  bool english) {
    const std::wstring scope_text = export_range_title_text(range, english);
    if (english) {
        return std::wstring(L"Exported ") + format_name + L" (" + scope_text + L"): ";
    }
    return std::wstring(L"Выгружен ") + format_name + L" (" + scope_text + L"): ";
}

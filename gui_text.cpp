// Text: native viewer implementation.
#include "gui_text.hpp"

namespace gui {

const Strings kRu = {
    L"AMSignal",
    L"Открыть", L"PNG", L"Сохранить как…", L"Старт", L"Стоп", L"Точки", L"Сброс", L"АвтоМасштаб",
    L"Время: %zu каналов, %zu отсчётов, %.6g–%.6g с", L"Спектр: %zu каналов, верхняя частота %.6g Гц | %.6g–%.6g Гц", L" | Масштаб Y: авто", L" | Масштаб Y: фикс.", L" | Линий: %zu", L" | Маркеров: %zu", L" | Скорость: %.4gx",
    L"Время, c", L"Частота, Гц",
    L"Δx=%.5g", L"Δy=%.5g", L"1/Δt=%.5g Гц", L"d=%.5g",
    L"Горячие клавиши — AMSignal",
    L"Нет данных", L"Сначала откройте файл.", L"Не удалось сохранить PNG.", L"Ошибка чтения",
    L"AMSignal",
    L"Недавние файлы", L"Горячие клавиши", L"Начать работу",
    L"Открыть файл…", L"PNG", L"Старт", L"Стоп", L"Режим измерения точек", L"Сбросить вид", L"АвтоМасштаб",
    L"Русский", L"English", L"Язык",
    L"Лёгкий режим",
    L"   |   Лёгкий режим: открыт только выбранный временной фрагмент",
    L"Лёгкий режим: диапазон открытия",
    L"С какой секунды открыть фрагмент:",
    L"По какую секунду открыть фрагмент:",
    L"Открыть фрагмент",
    L"Введите конечное число больше начального времени.",
    L"Загрузка файла...\r\nПожалуйста, подождите",
    L"Лёгкий режим: загрузка фрагмента...\r\nПожалуйста, подождите",
    L"Лёгкий режим: сканирование диапазона времени...\r\nПожалуйста, подождите",
    L"   |   Δf = %.5g Гц,  Δamp = %.4g",
    L"   |   Δt = %.6g c,  Δy = %.5g,  1/Δt = %.6g Гц",
    L" (+сплайн)",
    L"%.5g Гц",
    L"%.5g c",
    L"y=%.5g",
    L"Гц",
    L"c",
    L"Светлая тема",
    L"Темная тема",
    L"Ошибка",
    L"Сохранено (PNG): ",
    L"Проекты AMSignal / LVM / текст / CSV файлы\0*.AMSig;*.lvm;*.txt;*.csv\0Все файлы\0*.*\0",
    L"PNG изображение\0*.png\0Все файлы\0*.*\0",
    L"Время",
    L"Частота",
    L"Кликните на графике, чтобы поставить вертикальную линию (Esc — отмена). Можно добавить несколько линий подряд.",
    L"Кликните на графике, чтобы поставить горизонтальную линию (Esc — отмена). Можно добавить несколько линий подряд.",
    L"Кликните на графике, чтобы поставить маркер (Esc — отмена)."
};

const Strings kEn = {
    L"AMSignal",
    L"Open", L"PNG", L"Save as…", L"▶ Play", L"⏸ Pause", L"Points", L"Reset", L"Auto zoom",
    L"Time: %zu channels, %zu samples, %.6g–%.6g s", L"Spectrum: %zu channels, upper frequency %.6g Hz | %.6g–%.6g Hz", L" | Y scale: auto", L" | Y scale: fixed", L" | Lines: %zu", L" | Markers: %zu", L" | Speed: %.4gx",
    L"Time, s", L"Frequency, Hz",
    L"Δx=%.5g", L"Δy=%.5g", L"1/Δt=%.5g Hz", L"d=%.5g",
    L"Keyboard shortcuts — AMSignal",
    L"No data", L"Open a file first.", L"Failed to save PNG.", L"Read error",
    L"AMSignal",
    L"Recent files", L"Keyboard shortcuts", L"Start working",
    L"Open file…", L"PNG", L"Playback", L"Pause", L"Measurement point mode", L"Reset view", L"Auto zoom",
    L"Русский", L"English", L"Language",
    L"Light mode",
    L"   |   Light mode: only the selected time fragment is open",
    L"Light mode: open time range",
    L"Open the fragment starting from this second:",
    L"Open the fragment until this second:",
    L"Open fragment",
    L"Enter a finite number greater than the start time.",
    L"Loading file...\r\nPlease wait",
    L"Light mode: loading fragment...\r\nPlease wait",
    L"Light mode: scanning time range...\r\nPlease wait",
    L"   |   Δf = %.5g Hz,  Δamp = %.4g",
    L"   |   Δt = %.6g s,  Δy = %.5g,  1/Δt = %.6g Hz",
    L" (+spline)",
    L"%.5g Hz",
    L"%.5g s",
    L"y=%.5g",
    L"Hz",
    L"s",
    L"Light theme",
    L"Dark theme",
    L"Error",
    L"Saved (PNG): ",
    L"AMSignal projects / LVM / text / CSV files\0*.AMSig;*.lvm;*.txt;*.csv\0All files\0*.*\0",
    L"PNG image\0*.png\0All files\0*.*\0",
    L"Time",
    L"Frequency",
    L"Click on the plot to place a vertical line (Esc to cancel). You can add multiple lines.",
    L"Click on the plot to place a horizontal line (Esc to cancel). You can add multiple lines.",
    L"Click on the plot to place a marker (Esc to cancel)."
};

const Strings* g_str = &kRu;

const wchar_t* mode_time_text() {
    return g_str == &kEn ? L"Time" : L"Время";
}

const wchar_t* mode_spectrum_text() {
    return g_str == &kEn ? L"Spectrum" : L"Спектр";
}

const wchar_t* mode_frf_text() {
    return g_str == &kEn ? L"FRF" : L"АЧХ";
}

const wchar_t* gap_markers_toggle_text() {
    return (g_str == &kEn) ? L"Show gap markers" : L"Показывать разрывы";
}

const wchar_t* filter_toggle_text() {
    return (g_str == &kEn) ? L"Signal filter" : L"Фильтр сигнала";
}

const wchar_t* filter_low_cutoff_text() {
    return (g_str == &kEn) ? L"Low cutoff:" : L"Нижняя граница:";
}

const wchar_t* filter_high_cutoff_text() {
    return (g_str == &kEn) ? L"High cutoff:" : L"Верхняя граница:";
}

const wchar_t* filter_section_title_text() {
    return (g_str == &kEn) ? L"Filter" : L"Фильтр";
}

const wchar_t* filter_mode_label_text() {
    return (g_str == &kEn) ? L"Mode:" : L"Режим:";
}

const wchar_t* filter_topology_label_text() {
    return (g_str == &kEn) ? L"Topology:" : L"Топология:";
}

const wchar_t* filter_mode_lowpass_text() {
    return (g_str == &kEn) ? L"Low-pass" : L"НЧ";
}

const wchar_t* filter_mode_highpass_text() {
    return (g_str == &kEn) ? L"High-pass" : L"ВЧ";
}

const wchar_t* filter_mode_bandpass_text() {
    return (g_str == &kEn) ? L"Band-pass" : L"Полосовой";
}

const wchar_t* filter_mode_bandstop_text() {
    return (g_str == &kEn) ? L"Band-stop" : L"Режекторный";
}

const wchar_t* filter_topology_butterworth_text() {
    return (g_str == &kEn) ? L"Butterworth" : L"Баттерворт";
}

const wchar_t* filter_topology_bessel_text() {
    return (g_str == &kEn) ? L"Bessel" : L"Бессель";
}

const wchar_t* filter_topology_chebyshev_text() {
    return (g_str == &kEn) ? L"Chebyshev" : L"Чебышёв";
}

const wchar_t* filter_topology_linkwitz_text() {
    return (g_str == &kEn) ? L"Linkwitz-Riley" : L"Линквиц-Райли";
}

const wchar_t* side_global_formula_label_text() {
    return (g_str == &kEn) ? L"Global coefficient for all charts:" : L"Общий коэффициент для всех графиков:";
}

const wchar_t* side_global_formula_apply_text() {
    return (g_str == &kEn) ? L"Apply to all charts" : L"Применить ко всем графикам";
}

const wchar_t* side_channel_formula_label_text() {
    return (g_str == &kEn) ? L"Individual multiplier for each channel:" : L"Индивидуальный множитель канала:";
}

const wchar_t* point_group_list_title() {
    return g_str == &kEn ? L"Point groups" : L"Группы точек";
}

const wchar_t* point_current_color_button_text() {
    return g_str == &kEn ? L"Colour for new points…" : L"Цвет новых точек…";
}

const wchar_t* point_selected_group_color_button_text() {
    return g_str == &kEn ? L"Selected group colour…" : L"Цвет выбранной группы…";
}

const wchar_t* point_group_visible_text() {
    return g_str == &kEn ? L"Show selected group" : L"Показывать выбранную группу";
}

const wchar_t* point_group_new_button_text() {
    return g_str == &kEn ? L"Start new group" : L"Новая группа";
}

const wchar_t* point_group_empty_text() {
    return g_str == &kEn ? L"No point groups yet" : L"Групп точек пока нет";
}

const wchar_t* side_panel_button_text() {
    return (g_str == &kEn) ? L"Panel" : L"Панель";
}

const wchar_t* side_tab_channels_text() {
    return (g_str == &kEn) ? L"Channels" : L"Каналы";
}

const wchar_t* side_tab_points_text() {
    return (g_str == &kEn) ? L"Points" : L"Точки";
}

const wchar_t* side_tab_filter_text() {
    return (g_str == &kEn) ? L"Filter" : L"Фильтр";
}

const wchar_t* side_channel_color_button_text() {
    return (g_str == &kEn) ? L"Channel colour…" : L"Цвет канала…";
}

const wchar_t* side_channel_hint_text() {
    return filter_section_title_text();
}

const wchar_t* side_formula_apply_selected_text() {
    return (g_str == &kEn) ? L"Apply to selected" : L"К выбранному";
}

const wchar_t* side_formula_apply_visible_text() {
    return (g_str == &kEn) ? L"Apply to visible" : L"К видимым";
}

const wchar_t* side_formula_reset_selected_text() {
    return (g_str == &kEn) ? L"Reset selected" : L"Сбросить канал";
}

const wchar_t* side_formula_reset_all_text() {
    return (g_str == &kEn) ? L"Reset all channels" : L"Сбросить все каналы";
}

const wchar_t* side_point_group_delete_text() {
    return (g_str == &kEn) ? L"Delete group" : L"Удалить группу";
}

const wchar_t* side_point_group_rename_text() {
    return (g_str == &kEn) ? L"Rename" : L"Переименовать";
}

const wchar_t* side_pt_num_text() {
    return (g_str == &kEn) ? L"Point #" : L"Номер";
}

const wchar_t* side_pt_x_text() {
    return L"X";
}

const wchar_t* stitch_gaps_toggle_text() {
    return (g_str == &kEn) ? L"Stitch time gaps in the graph" : L"Склеивать пропуски времени на графике";
}

const wchar_t* side_pt_y_text() {
    return L"Y";
}

const wchar_t* side_pt_dx_text() {
    return L"Δx";
}

const wchar_t* side_pt_dy_text() {
    return L"Δy";
}

const wchar_t* side_pt_invdt_text() {
    return L"1/Δt";
}

const wchar_t* side_pt_dist_text() {
    return L"d";
}

const wchar_t* side_pt_snap_text() {
    return (g_str == &kEn) ? L"Snap" : L"Привязка";
}

std::wstring to_w(const std::string& s) {
    if (s.empty()) return L"";
    UINT encoding = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int n = MultiByteToWideChar(encoding, flags, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n == 0) { encoding = CP_ACP; flags = 0; n = MultiByteToWideChar(encoding, flags, s.data(), static_cast<int>(s.size()), nullptr, 0); }
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(encoding, flags, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring to_w_acp(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty()) w.pop_back();
    return w;
}

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    if (!s.empty()) s.pop_back();
    return s;
}

// Compact ASCII number for CSV; empty for NaN (matches the CLI / pandas).
std::string numfmt(double v) {
    if (std::isnan(v)) return "";
    char b[32];
    std::snprintf(b, sizeof(b), "%.17g", v);
    return b;
}

} // namespace gui

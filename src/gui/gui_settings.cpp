/// @file gui_settings.cpp
/// JSON persistence for GuiSettings.

#include "gui/gui_settings.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace canmatik {

using json = nlohmann::json;

std::string GuiSettings::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "Cannot open " + path;

    json j;
    try {
        f >> j;
    } catch (const json::parse_error& e) {
        return std::string("JSON parse error: ") + e.what();
    }

    if (j.contains("provider"))          provider          = j["provider"].get<std::string>();
    if (j.contains("bitrate"))           bitrate           = j["bitrate"].get<uint32_t>();
    if (j.contains("mock_enabled"))      mock_enabled      = j["mock_enabled"].get<bool>();
    if (j.contains("buffer_capacity"))   buffer_capacity   = j["buffer_capacity"].get<uint32_t>();
    if (j.contains("change_filter_n"))   change_filter_n   = j["change_filter_n"].get<uint32_t>();
    if (j.contains("show_changed_only")) show_changed_only = j["show_changed_only"].get<bool>();
    if (j.contains("obd_interval_ms"))   obd_interval_ms   = j["obd_interval_ms"].get<uint32_t>();
    if (j.contains("window_width"))      window_width      = j["window_width"].get<int>();
    if (j.contains("window_height"))     window_height     = j["window_height"].get<int>();
    if (j.contains("last_file_path"))    last_file_path    = j["last_file_path"].get<std::string>();

    if (j.contains("color_scheme")) {
        auto cs = j["color_scheme"].get<std::string>();
        if (cs == "light")      color_scheme = ColorScheme::LIGHT;
        else if (cs == "retro") color_scheme = ColorScheme::RETRO;
        else                   color_scheme = ColorScheme::DARK;
    }

    if (j.contains("id_filter_mode")) {
        auto fm = j["id_filter_mode"].get<std::string>();
        id_filter_mode = (fm == "include") ? IdFilterMode::INCLUDE : IdFilterMode::EXCLUDE;
    }
    if (j.contains("id_filter_list") && j["id_filter_list"].is_array()) {
        id_filter_list.clear();
        for (auto& v : j["id_filter_list"])
            if (v.is_number()) id_filter_list.push_back(v.get<uint32_t>());
    }

    if (j.contains("obd_mode")) {
        auto m = j["obd_mode"].get<std::string>();
        if (m == "obd_only")          obd_mode = ObdDisplayMode::OBD_ONLY;
        else if (m == "broadcast_only") obd_mode = ObdDisplayMode::BROADCAST_ONLY;
        else                          obd_mode = ObdDisplayMode::OBD_AND_BROADCAST;
    }

    if (j.contains("obd_pids") && j["obd_pids"].is_array()) {
        obd_pids.clear();
        for (auto& v : j["obd_pids"]) {
            if (v.is_number())
                obd_pids.push_back(static_cast<uint8_t>(v.get<int>()));
            else if (v.is_string()) {
                auto s = v.get<std::string>();
                obd_pids.push_back(static_cast<uint8_t>(std::stoul(s, nullptr, 16)));
            }
        }
    }

    // Clamp buffer capacity
    if (buffer_capacity < 1000)     buffer_capacity = 1000;
    if (buffer_capacity > 10000000) buffer_capacity = 10000000;
    if (change_filter_n < 1)   change_filter_n = 1;
    if (change_filter_n > 100) change_filter_n = 100;

    return {};
}

static const char* color_scheme_string(ColorScheme s) {
    switch (s) {
    case ColorScheme::LIGHT: return "light";
    case ColorScheme::RETRO: return "retro";
    default:                 return "dark";
    }
}

static const char* obd_mode_string(ObdDisplayMode m) {
    switch (m) {
    case ObdDisplayMode::OBD_ONLY:       return "obd_only";
    case ObdDisplayMode::BROADCAST_ONLY: return "broadcast_only";
    default:                             return "obd_and_broadcast";
    }
}

std::string GuiSettings::save(const std::string& path) const {
    json j;
    j["provider"]          = provider;
    j["bitrate"]           = bitrate;
    j["mock_enabled"]      = mock_enabled;
    j["buffer_capacity"]   = buffer_capacity;
    j["change_filter_n"]   = change_filter_n;
    j["show_changed_only"] = show_changed_only;
    j["id_filter_mode"]    = (id_filter_mode == IdFilterMode::INCLUDE) ? "include" : "exclude";
    {
        json ids = json::array();
        for (auto id : id_filter_list) ids.push_back(id);
        j["id_filter_list"] = ids;
    }
    j["obd_mode"]          = obd_mode_string(obd_mode);
    j["obd_interval_ms"]   = obd_interval_ms;
    j["color_scheme"]      = color_scheme_string(color_scheme);
    j["window_width"]      = window_width;
    j["window_height"]     = window_height;
    j["last_file_path"]    = last_file_path;

    json pids = json::array();
    for (auto p : obd_pids) pids.push_back(static_cast<int>(p));
    j["obd_pids"] = pids;

    std::ofstream f(path);
    if (!f.is_open()) return "Cannot write to " + path;
    f << j.dump(2) << '\n';
    return {};
}

} // namespace canmatik

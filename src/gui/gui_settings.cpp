/// @file gui_settings.cpp
/// JSON persistence for GuiSettings.

#include "gui/gui_settings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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
    if (j.contains("bus_protocol")) {
        auto bp = j["bus_protocol"].get<std::string>();
        if (bp == "j1850_vpw")      bus_protocol = BusProtocol::J1850_VPW;
        else if (bp == "j1850_pwm") bus_protocol = BusProtocol::J1850_PWM;
        else                        bus_protocol = BusProtocol::CAN;
    }
    if (j.contains("buffer_capacity"))   buffer_capacity   = j["buffer_capacity"].get<uint32_t>();
    if (j.contains("buffer_overwrite"))  buffer_overwrite  = j["buffer_overwrite"].get<bool>();
    if (j.contains("change_filter_n"))   change_filter_n   = j["change_filter_n"].get<uint32_t>();
    if (j.contains("show_changed_only")) show_changed_only = j["show_changed_only"].get<bool>();
    if (j.contains("obd_interval_ms"))   obd_interval_ms   = j["obd_interval_ms"].get<uint32_t>();
    if (j.contains("watchdog_history_size")) watchdog_history_size = j["watchdog_history_size"].get<uint32_t>();
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

    // Load font scales
    if (j.contains("font_scale_can")) font_scale_can = std::clamp(j["font_scale_can"].get<float>(), 0.5f, 3.0f);
    if (j.contains("font_scale_obd")) font_scale_obd = std::clamp(j["font_scale_obd"].get<float>(), 0.5f, 3.0f);

    // Load font colors
    auto load_color = [&](const char* key, float* c) {
        if (j.contains(key) && j[key].is_array() && j[key].size() >= 4) {
            for (int i = 0; i < 4; ++i) {
                if (j[key][i].is_number())
                    c[i] = static_cast<float>(j[key][i].get<double>());
            }
        }
    };
    load_color("color_can_new",      color_can_new);
    load_color("color_can_changed",  color_can_changed);
    load_color("color_can_dlc",      color_can_dlc);
    load_color("color_can_default",  color_can_default);
    load_color("color_can_watched",  color_can_watched);
    load_color("color_obd_changed",  color_obd_changed);
    load_color("color_obd_normal",   color_obd_normal);
    load_color("color_obd_dim",      color_obd_dim);

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
    {
        const char* bp_str = "can";
        if (bus_protocol == BusProtocol::J1850_VPW) bp_str = "j1850_vpw";
        else if (bus_protocol == BusProtocol::J1850_PWM) bp_str = "j1850_pwm";
        j["bus_protocol"] = bp_str;
    }
    j["buffer_capacity"]   = buffer_capacity;
    j["buffer_overwrite"]  = buffer_overwrite;
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
    j["watchdog_history_size"] = watchdog_history_size;
    j["color_scheme"]      = color_scheme_string(color_scheme);
    j["window_width"]      = window_width;
    j["window_height"]     = window_height;
    j["last_file_path"]    = last_file_path;

    json pids = json::array();
    for (auto p : obd_pids) pids.push_back(static_cast<int>(p));
    j["obd_pids"] = pids;

    // Save font scales
    j["font_scale_can"] = font_scale_can;
    j["font_scale_obd"] = font_scale_obd;

    // Save font colors
    auto save_color = [&](const char* key, const float* c) {
        json arr = json::array();
        for (int i = 0; i < 4; ++i) arr.push_back(c[i]);
        j[key] = arr;
    };
    save_color("color_can_new",      color_can_new);
    save_color("color_can_changed",  color_can_changed);
    save_color("color_can_dlc",      color_can_dlc);
    save_color("color_can_default",  color_can_default);
    save_color("color_can_watched",  color_can_watched);
    save_color("color_obd_changed",  color_obd_changed);
    save_color("color_obd_normal",   color_obd_normal);
    save_color("color_obd_dim",      color_obd_dim);

    std::ofstream f(path);
    if (!f.is_open()) return "Cannot write to " + path;
    f << j.dump(2) << '\n';
    return {};
}

} // namespace canmatik

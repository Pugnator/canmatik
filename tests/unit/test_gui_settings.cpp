/// @file test_gui_settings.cpp
/// Unit tests for GuiSettings load/save roundtrip.

#include <catch2/catch_test_macros.hpp>
#include "gui/gui_settings.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace canmatik;

namespace {
    struct TempFile {
        std::string path;
        TempFile(const std::string& p) : path(p) {}
        ~TempFile() { std::remove(path.c_str()); }
    };
}

TEST_CASE("GuiSettings defaults are valid", "[gui_settings]") {
    GuiSettings s;
    CHECK(s.bitrate == 500000);
    CHECK(s.buffer_capacity == 100000);
    CHECK(s.change_filter_n == 1);
    CHECK_FALSE(s.show_changed_only);
    CHECK(s.obd_interval_ms == 500);
    CHECK(s.obd_pids.size() == 3);
    CHECK(s.window_width == 1024);
    CHECK(s.window_height == 720);
}

TEST_CASE("GuiSettings save/load roundtrip", "[gui_settings]") {
    TempFile tmp("_test_gui_settings.json");

    GuiSettings orig;
    orig.provider = "TestProvider";
    orig.bitrate  = 250000;
    orig.buffer_capacity = 5000;
    orig.change_filter_n = 5;
    orig.show_changed_only = true;
    orig.obd_mode = ObdDisplayMode::OBD_ONLY;
    orig.obd_pids = {0x0C, 0x0D, 0x05, 0x11};
    orig.obd_interval_ms = 200;
    orig.window_width = 800;
    orig.window_height = 600;
    orig.last_file_path = "C:\\test.asc";

    auto err = orig.save(tmp.path);
    REQUIRE(err.empty());

    GuiSettings loaded;
    err = loaded.load(tmp.path);
    REQUIRE(err.empty());

    CHECK(loaded.provider == "TestProvider");
    CHECK(loaded.bitrate == 250000);
    CHECK(loaded.buffer_capacity == 5000);
    CHECK(loaded.change_filter_n == 5);
    CHECK(loaded.show_changed_only == true);
    CHECK(loaded.obd_mode == ObdDisplayMode::OBD_ONLY);
    CHECK(loaded.obd_pids.size() == 4);
    CHECK(loaded.obd_interval_ms == 200);
    CHECK(loaded.window_width == 800);
    CHECK(loaded.window_height == 600);
    CHECK(loaded.last_file_path == "C:\\test.asc");
}

TEST_CASE("GuiSettings load nonexistent file keeps defaults", "[gui_settings]") {
    GuiSettings s;
    auto err = s.load("_nonexistent_file_12345.json");
    CHECK_FALSE(err.empty());
    // Defaults preserved
    CHECK(s.bitrate == 500000);
    CHECK(s.buffer_capacity == 100000);
}

TEST_CASE("GuiSettings load corrupt JSON keeps defaults", "[gui_settings]") {
    TempFile tmp("_test_corrupt.json");
    {
        FILE* f = std::fopen(tmp.path.c_str(), "w");
        std::fputs("{invalid json!!!", f);
        std::fclose(f);
    }
    GuiSettings s;
    auto err = s.load(tmp.path);
    CHECK_FALSE(err.empty());
    CHECK(s.bitrate == 500000);
}

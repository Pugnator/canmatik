/// @file test_cli_commands.cpp
/// Integration tests for CLI command dispatch via subprocess (T058 — US7).
/// Runs the built canmatik.exe with various subcommands and checks exit codes
/// and stdout content. Requires the canmatik executable in the build directory.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using Catch::Matchers::ContainsSubstring;

namespace {

/// Path to built canmatik executable (resolved once).
/// CTest sets working dir to build32/tests, so we also check ../canmatik.exe.
std::string exe_path() {
    for (const char* candidate : {
             "canmatik.exe",
             "./canmatik.exe",
             "../canmatik.exe",
             "..\\canmatik.exe"}) {
        if (std::filesystem::exists(candidate))
            return std::filesystem::absolute(candidate).string();
    }
    return "canmatik.exe";
}

struct RunResult {
    int exit_code;
    std::string output;
};

/// Run a command line and capture stdout+stderr combined.
RunResult run_cmd(const std::string& cmd) {
    RunResult result;
    std::string full_cmd = cmd + " 2>&1";
    FILE* pipe = _popen(full_cmd.c_str(), "r");
    if (!pipe) {
        result.exit_code = -1;
        result.output = "Failed to open pipe";
        return result;
    }
    std::array<char, 4096> buffer;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result.output += buffer.data();
    }
    result.exit_code = _pclose(pipe);
    return result;
}

/// Create a minimal JSONL file for replay testing.
std::string create_test_jsonl() {
    auto path = (std::filesystem::temp_directory_path() / "canmatik_cli_test.jsonl").string();
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    f << R"({"_meta":true,"format":"canmatik-jsonl","version":"1.0","adapter":"Test","bitrate":500000})" << '\n';
    f << R"({"ts":0.0,"ats":0,"id":"7E0","ext":false,"dlc":2,"data":"02 01"})" << '\n';
    f << R"({"ts":0.1,"ats":100000,"id":"7E8","ext":false,"dlc":3,"data":"06 41 00"})" << '\n';
    f << R"({"_meta":true,"type":"session_summary","frames":2,"errors":0,"dropped":0,"duration":0.1})" << '\n';
    return path;
}

void cleanup(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // anonymous namespace

// ── Scan ──────────────────────────────────────────────────────────────────

TEST_CASE("CLI: scan --mock returns success", "[integration][cli]") {
    auto r = run_cmd(exe_path() + " scan --mock");
    CHECK(r.exit_code == 0);
    CHECK_THAT(r.output, ContainsSubstring("Mock"));
}

TEST_CASE("CLI: scan --mock --json returns valid JSON", "[integration][cli]") {
    auto r = run_cmd(exe_path() + " scan --mock --json");
    CHECK(r.exit_code == 0);
    CHECK_THAT(r.output, ContainsSubstring("\"name\""));
}

// ── Status ────────────────────────────────────────────────────────────────

TEST_CASE("CLI: status --mock returns success", "[integration][cli]") {
    auto r = run_cmd(exe_path() + " status --mock");
    CHECK(r.exit_code == 0);
    CHECK_THAT(r.output, ContainsSubstring("provider"));
}

TEST_CASE("CLI: status --mock --json returns JSON", "[integration][cli]") {
    auto r = run_cmd(exe_path() + " status --mock --json");
    CHECK(r.exit_code == 0);
    CHECK_THAT(r.output, ContainsSubstring("\"provider_count\""));
}

// ── Replay ────────────────────────────────────────────────────────────────

TEST_CASE("CLI: replay loads JSONL file", "[integration][cli]") {
    auto path = create_test_jsonl();
    auto r = run_cmd(exe_path() + " replay \"" + path + "\"");
    CHECK(r.exit_code == 0);
    CHECK_THAT(r.output, ContainsSubstring("7E0"));
    cleanup(path);
}

TEST_CASE("CLI: replay --summary shows frame count", "[integration][cli]") {
    auto path = create_test_jsonl();
    auto r = run_cmd(exe_path() + " replay \"" + path + "\" --summary");
    CHECK(r.exit_code == 0);
    CHECK_THAT(r.output, ContainsSubstring("2")); // total frames
    cleanup(path);
}

TEST_CASE("CLI: replay --search filters by ID", "[integration][cli]") {
    auto path = create_test_jsonl();
    auto r = run_cmd(exe_path() + " replay \"" + path + "\" --search 0x7E8");
    CHECK(r.exit_code == 0);
    CHECK_THAT(r.output, ContainsSubstring("7E8"));
    cleanup(path);
}

TEST_CASE("CLI: replay nonexistent file returns error", "[integration][cli]") {
    auto r = run_cmd(exe_path() + " replay nonexistent_file_999.jsonl");
    CHECK(r.exit_code != 0);
}

// ── Help / version ────────────────────────────────────────────────────────

TEST_CASE("CLI: --version prints version", "[integration][cli]") {
    auto r = run_cmd(exe_path() + " --version");
    CHECK(r.exit_code == 0);
    CHECK_THAT(r.output, ContainsSubstring("0.1.0"));
}

TEST_CASE("CLI: --help prints usage", "[integration][cli]") {
    auto r = run_cmd(exe_path() + " --help");
    CHECK(r.exit_code == 0);
    CHECK_THAT(r.output, ContainsSubstring("Usage:"));
}

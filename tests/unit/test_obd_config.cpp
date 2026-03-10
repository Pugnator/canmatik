/// @file test_obd_config.cpp
/// Unit tests for ObdConfig YAML loader and default generation (T107).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "obd/obd_config.h"

#include <filesystem>
#include <fstream>

using namespace canmatik;

namespace {

const char* kTestConfigPath = "test_obd_config_temp.yaml";

struct CleanupFile {
    ~CleanupFile() {
        std::filesystem::remove(kTestConfigPath);
    }
};

void write_yaml(const char* content) {
    std::ofstream out(kTestConfigPath);
    out << content;
}

} // namespace

TEST_CASE("ObdConfig: load valid YAML", "[obd][config]") {
    CleanupFile cleanup;
    write_yaml(R"(
interval: "500ms"
addressing:
  mode: functional
  tx_id: 0x7DF
  rx_base: 0x7E8
groups:
  - name: engine
    pids:
      - id: 0x0C
      - id: 0x0D
)");

    auto cfg = ObdConfig::load(kTestConfigPath);
    REQUIRE(cfg.has_value());
    CHECK(cfg->default_interval.milliseconds == 500);
    CHECK(cfg->addressing_mode == AddressingMode::Functional);
    CHECK(cfg->tx_id == 0x7DF);
    CHECK(cfg->rx_base == 0x7E8);
    REQUIRE(cfg->groups.size() == 1);
    CHECK(cfg->groups[0].name == "engine");
    CHECK(cfg->groups[0].pids.size() == 2);
    CHECK(cfg->groups[0].pids[0] == 0x0C);
    CHECK(cfg->groups[0].pids[1] == 0x0D);
}

TEST_CASE("ObdConfig: load with group intervals", "[obd][config]") {
    CleanupFile cleanup;
    write_yaml(R"(
interval: "1s"
groups:
  - name: fast
    interval: "200ms"
    pids:
      - id: 0x0C
  - name: slow
    interval: "5s"
    pids:
      - id: 0x05
)");

    auto cfg = ObdConfig::load(kTestConfigPath);
    REQUIRE(cfg.has_value());
    CHECK(cfg->default_interval.milliseconds == 1000);
    REQUIRE(cfg->groups.size() == 2);
    CHECK(cfg->groups[0].has_interval);
    CHECK(cfg->groups[0].interval.milliseconds == 200);
    CHECK(cfg->groups[1].has_interval);
    CHECK(cfg->groups[1].interval.milliseconds == 5000);
}

TEST_CASE("ObdConfig: standalone PIDs", "[obd][config]") {
    CleanupFile cleanup;
    write_yaml(R"(
interval: "1s"
standalone_pids:
  - 0x0C
  - 0x0D
  - 0x05
)");

    auto cfg = ObdConfig::load(kTestConfigPath);
    REQUIRE(cfg.has_value());
    CHECK(cfg->groups.empty());
    REQUIRE(cfg->standalone_pids.size() == 3);
    CHECK(cfg->standalone_pids[0] == 0x0C);
}

TEST_CASE("ObdConfig: missing fields use defaults", "[obd][config]") {
    CleanupFile cleanup;
    write_yaml(R"(
groups:
  - name: minimal
    pids:
      - id: 0x0C
)");

    auto cfg = ObdConfig::load(kTestConfigPath);
    REQUIRE(cfg.has_value());
    CHECK(cfg->default_interval.milliseconds == 1000);  // default
    CHECK(cfg->addressing_mode == AddressingMode::Functional);
    CHECK(cfg->tx_id == 0x7DF);
}

TEST_CASE("ObdConfig: invalid YAML returns error", "[obd][config]") {
    CleanupFile cleanup;
    write_yaml("{{{{invalid yaml");

    auto cfg = ObdConfig::load(kTestConfigPath);
    REQUIRE_FALSE(cfg.has_value());
    CHECK_THAT(cfg.error(), Catch::Matchers::ContainsSubstring("failed to load"));
}

TEST_CASE("ObdConfig: nonexistent file", "[obd][config]") {
    auto cfg = ObdConfig::load("nonexistent_file_12345.yaml");
    REQUIRE_FALSE(cfg.has_value());
}

TEST_CASE("ObdConfig: generate_default creates parseable file", "[obd][config]") {
    CleanupFile cleanup;

    auto result = ObdConfig::generate_default(kTestConfigPath);
    REQUIRE(result.has_value());
    CHECK(std::filesystem::exists(kTestConfigPath));

    // Round-trip: load the generated file
    auto cfg = ObdConfig::load(kTestConfigPath);
    REQUIRE(cfg.has_value());
    CHECK(cfg->default_interval.milliseconds == 1000);
    CHECK(cfg->groups.size() == 2);
    CHECK(cfg->groups[0].name == "engine_basics");
    CHECK(cfg->groups[1].name == "fuel_system");
}

TEST_CASE("ObdConfig: physical addressing", "[obd][config]") {
    CleanupFile cleanup;
    write_yaml(R"(
addressing:
  mode: physical
  tx_id: 0x7E0
  rx_base: 0x7E8
groups:
  - name: test
    pids:
      - id: 0x0C
)");

    auto cfg = ObdConfig::load(kTestConfigPath);
    REQUIRE(cfg.has_value());
    CHECK(cfg->addressing_mode == AddressingMode::Physical);
    CHECK(cfg->tx_id == 0x7E0);
}

TEST_CASE("ObdConfig: empty groups yields empty config", "[obd][config][edge]") {
    CleanupFile cleanup;
    write_yaml(R"(
interval: "500ms"
)");

    auto cfg = ObdConfig::load(kTestConfigPath);
    REQUIRE(cfg.has_value());
    CHECK(cfg->groups.empty());
    CHECK(cfg->standalone_pids.empty());
}

TEST_CASE("ObdConfig: group with no PIDs rejected", "[obd][config][edge]") {
    CleanupFile cleanup;
    write_yaml(R"(
groups:
  - name: empty_group
    pids: []
)");

    auto cfg = ObdConfig::load(kTestConfigPath);
    REQUIRE_FALSE(cfg.has_value());
    CHECK_THAT(cfg.error(), Catch::Matchers::ContainsSubstring("no PIDs"));
}

TEST_CASE("ObdConfig: unknown addressing mode rejected", "[obd][config][edge]") {
    CleanupFile cleanup;
    write_yaml(R"(
addressing:
  mode: extended
groups:
  - name: test
    pids:
      - id: 0x0C
)");

    auto cfg = ObdConfig::load(kTestConfigPath);
    REQUIRE_FALSE(cfg.has_value());
    CHECK_THAT(cfg.error(), Catch::Matchers::ContainsSubstring("unknown mode"));
}

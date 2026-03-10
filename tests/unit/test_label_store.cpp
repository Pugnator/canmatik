/// @file test_label_store.cpp
/// Unit tests for LabelStore (placeholder — Phase 10).

#include <catch2/catch_test_macros.hpp>
#include "core/label_store.h"

#include <filesystem>

using namespace canmatik;

TEST_CASE("LabelStore default is empty", "[label_store]") {
    LabelStore store;
    CHECK_FALSE(store.lookup(0x7E8).has_value());
}

TEST_CASE("LabelStore set and lookup", "[label_store]") {
    LabelStore store;
    store.set(0x7E8, "Engine RPM");
    CHECK(store.lookup(0x7E8) == "Engine RPM");
    CHECK_FALSE(store.lookup(0x100).has_value());
}

TEST_CASE("LabelStore remove", "[label_store]") {
    LabelStore store;
    store.set(0x100, "Test");
    CHECK(store.lookup(0x100).has_value());
    store.remove(0x100);
    CHECK_FALSE(store.lookup(0x100).has_value());
}

TEST_CASE("LabelStore save and load roundtrip", "[label_store]") {
    const std::string path = "test_labels_roundtrip.json";
    {
        LabelStore store;
        store.set(0x7E8, "Engine RPM");
        store.set(0x100, "Steering Angle");
        auto err = store.save(path);
        REQUIRE(err.empty());
    }
    {
        LabelStore store;
        auto err = store.load(path);
        REQUIRE(err.empty());
        CHECK(store.lookup(0x7E8) == "Engine RPM");
        CHECK(store.lookup(0x100) == "Steering Angle");
    }
    std::filesystem::remove(path);
}

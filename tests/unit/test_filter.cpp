/// @file test_filter.cpp
/// Unit tests for FilterRule, FilterAction, parse_filter, and FilterEngine (T036 — US3).

#include <catch2/catch_test_macros.hpp>
#include "core/filter.h"

using namespace canmatik;

// ---------------------------------------------------------------------------
// Enum / struct basics
// ---------------------------------------------------------------------------

TEST_CASE("FilterAction enum values", "[filter]") {
    CHECK(static_cast<int>(FilterAction::Pass)  == 0);
    CHECK(static_cast<int>(FilterAction::Block) == 1);
}

TEST_CASE("FilterRule default construction", "[filter]") {
    FilterRule r;
    CHECK(r.action == FilterAction::Pass);
    CHECK(r.id_value == 0);
    CHECK(r.id_mask == 0xFFFFFFFF);
}

// ---------------------------------------------------------------------------
// parse_filter
// ---------------------------------------------------------------------------

TEST_CASE("parse_filter: simple hex ID", "[filter]") {
    FilterRule r;
    CHECK(parse_filter("0x7E8", r).empty());
    CHECK(r.action == FilterAction::Pass);
    CHECK(r.id_value == 0x7E8);
    CHECK(r.id_mask == 0xFFFFFFFF);
}

TEST_CASE("parse_filter: hex ID without 0x prefix", "[filter]") {
    FilterRule r;
    CHECK(parse_filter("3B0", r).empty());
    CHECK(r.id_value == 0x3B0);
}

TEST_CASE("parse_filter: block with ! prefix", "[filter]") {
    FilterRule r;
    CHECK(parse_filter("!0x000", r).empty());
    CHECK(r.action == FilterAction::Block);
    CHECK(r.id_value == 0x000);
    CHECK(r.id_mask == 0xFFFFFFFF);
}

TEST_CASE("parse_filter: explicit pass: prefix", "[filter]") {
    FilterRule r;
    CHECK(parse_filter("pass:0x100", r).empty());
    CHECK(r.action == FilterAction::Pass);
    CHECK(r.id_value == 0x100);
}

TEST_CASE("parse_filter: explicit block: prefix", "[filter]") {
    FilterRule r;
    CHECK(parse_filter("block:0x000", r).empty());
    CHECK(r.action == FilterAction::Block);
    CHECK(r.id_value == 0x000);
}

TEST_CASE("parse_filter: range syntax", "[filter]") {
    FilterRule r;
    CHECK(parse_filter("0x7E0-0x7EF", r).empty());
    CHECK(r.action == FilterAction::Pass);
    // Range 0x7E0..0x7EF should produce a mask covering that range
    // Common prefix: 0x7E0 ^ 0x7EF = 0x00F → mask clears lower 4 bits
    CHECK(r.id_mask == 0xFFFFFFF0);
    CHECK((r.id_value & r.id_mask) == 0x7E0);
}

TEST_CASE("parse_filter: mask syntax", "[filter]") {
    FilterRule r;
    CHECK(parse_filter("0x700/0xFF0", r).empty());
    CHECK(r.action == FilterAction::Pass);
    CHECK(r.id_value == 0x700);
    CHECK(r.id_mask == 0xFF0);
}

TEST_CASE("parse_filter: empty string rejected", "[filter]") {
    FilterRule r;
    CHECK(!parse_filter("", r).empty());
}

TEST_CASE("parse_filter: invalid hex rejected", "[filter]") {
    FilterRule r;
    CHECK(!parse_filter("ZZZZ", r).empty());
}

TEST_CASE("parse_filter: reversed range rejected", "[filter]") {
    FilterRule r;
    CHECK(!parse_filter("0x7EF-0x7E0", r).empty());
}

// ---------------------------------------------------------------------------
// FilterEngine — evaluate
// ---------------------------------------------------------------------------

TEST_CASE("FilterEngine: no rules passes all", "[filter]") {
    FilterEngine engine;
    CHECK(engine.evaluate(0x100));
    CHECK(engine.evaluate(0x7FF));
    CHECK(engine.evaluate(0x000));
}

TEST_CASE("FilterEngine: exact pass rule", "[filter]") {
    FilterEngine engine;
    FilterRule r;
    r.action = FilterAction::Pass;
    r.id_value = 0x100;
    r.id_mask = 0xFFFFFFFF;
    engine.add_rule(r);

    CHECK(engine.evaluate(0x100));
    CHECK(!engine.evaluate(0x101));
    CHECK(!engine.evaluate(0x200));
}

TEST_CASE("FilterEngine: exact block rule", "[filter]") {
    FilterEngine engine;
    FilterRule r;
    r.action = FilterAction::Block;
    r.id_value = 0x000;
    r.id_mask = 0xFFFFFFFF;
    engine.add_rule(r);

    CHECK(!engine.evaluate(0x000));
    CHECK(engine.evaluate(0x100));
    CHECK(engine.evaluate(0x7FF));
}

TEST_CASE("FilterEngine: mask-based pass rule", "[filter]") {
    FilterEngine engine;
    FilterRule r;
    r.action = FilterAction::Pass;
    r.id_value = 0x700;
    r.id_mask = 0xFF0;
    engine.add_rule(r);

    CHECK(engine.evaluate(0x700));
    CHECK(engine.evaluate(0x701));
    CHECK(engine.evaluate(0x70F));
    CHECK(!engine.evaluate(0x600));
    CHECK(!engine.evaluate(0x710));
}

TEST_CASE("FilterEngine: multiple pass rules (OR logic)", "[filter]") {
    FilterEngine engine;

    FilterRule r1;
    r1.action = FilterAction::Pass;
    r1.id_value = 0x100;
    r1.id_mask = 0xFFFFFFFF;
    engine.add_rule(r1);

    FilterRule r2;
    r2.action = FilterAction::Pass;
    r2.id_value = 0x200;
    r2.id_mask = 0xFFFFFFFF;
    engine.add_rule(r2);

    CHECK(engine.evaluate(0x100));
    CHECK(engine.evaluate(0x200));
    CHECK(!engine.evaluate(0x300));
}

TEST_CASE("FilterEngine: block overrides pass", "[filter]") {
    FilterEngine engine;

    // Pass range 0x100-0x1FF (mask-based)
    FilterRule pass;
    pass.action = FilterAction::Pass;
    pass.id_value = 0x100;
    pass.id_mask = 0xF00;
    engine.add_rule(pass);

    // Block specific ID within that range
    FilterRule block;
    block.action = FilterAction::Block;
    block.id_value = 0x150;
    block.id_mask = 0xFFFFFFFF;
    engine.add_rule(block);

    CHECK(engine.evaluate(0x100));
    CHECK(engine.evaluate(0x1FF));
    CHECK(!engine.evaluate(0x150));   // blocked
    CHECK(!engine.evaluate(0x200));   // not in pass range
}

TEST_CASE("FilterEngine: clear resets to pass-all", "[filter]") {
    FilterEngine engine;
    FilterRule r;
    r.action = FilterAction::Pass;
    r.id_value = 0x100;
    r.id_mask = 0xFFFFFFFF;
    engine.add_rule(r);

    CHECK(!engine.evaluate(0x200));

    engine.clear();
    CHECK(engine.empty());
    CHECK(engine.evaluate(0x200));
}

TEST_CASE("FilterEngine: rules() returns added rules", "[filter]") {
    FilterEngine engine;
    CHECK(engine.rules().empty());

    FilterRule r;
    r.id_value = 0x100;
    engine.add_rule(r);

    CHECK(engine.rules().size() == 1);
    CHECK(engine.rules()[0].id_value == 0x100);
}

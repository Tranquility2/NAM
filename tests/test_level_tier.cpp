#include <doctest/doctest.h>

#include <string>

#include "coordinates.h"
#include "expedition_progress.h"
#include "level_tier.h"

TEST_SUITE("game") {

TEST_CASE("V2 level tiers expose the approved fixed dimensions") {
    const LevelDimensions small = dimensions_of(LevelTier::small);
    const LevelDimensions medium = dimensions_of(LevelTier::medium);
    const LevelDimensions large = dimensions_of(LevelTier::large);
    const LevelDimensions x_large = dimensions_of(LevelTier::x_large);

    CHECK(small.width == 21u);
    CHECK(small.height == 11u);
    CHECK(medium.width == 29u);
    CHECK(medium.height == 15u);
    CHECK(large.width == 39u);
    CHECK(large.height == 21u);
    CHECK(x_large.width == 51u);
    CHECK(x_large.height == 27u);
}

TEST_CASE("each level tier has a stable label index and centre spawn") {
    CHECK(index_of(LevelTier::small) == 0u);
    CHECK(index_of(LevelTier::medium) == 1u);
    CHECK(index_of(LevelTier::large) == 2u);
    CHECK(index_of(LevelTier::x_large) == 3u);

    CHECK(std::string(to_string(LevelTier::small)) == "Small");
    CHECK(std::string(to_string(LevelTier::medium)) == "Medium");
    CHECK(std::string(to_string(LevelTier::large)) == "Large");
    CHECK(std::string(to_string(LevelTier::x_large)) == "X-Large");

    CHECK(center_spawn_of(LevelTier::small) == Coordinates{10, 5});
    CHECK(center_spawn_of(LevelTier::medium) == Coordinates{14, 7});
    CHECK(center_spawn_of(LevelTier::large) == Coordinates{19, 10});
    CHECK(center_spawn_of(LevelTier::x_large) == Coordinates{25, 13});
}

TEST_CASE("tier succession ends after X-Large") {
    REQUIRE(next_level_tier(LevelTier::small).has_value());
    CHECK(*next_level_tier(LevelTier::small) == LevelTier::medium);
    REQUIRE(next_level_tier(LevelTier::medium).has_value());
    CHECK(*next_level_tier(LevelTier::medium) == LevelTier::large);
    REQUIRE(next_level_tier(LevelTier::large).has_value());
    CHECK(*next_level_tier(LevelTier::large) == LevelTier::x_large);
    CHECK_FALSE(next_level_tier(LevelTier::x_large).has_value());
}

TEST_CASE("expedition progress advances once through all four tiers") {
    ExpeditionProgress progress;
    CHECK(progress.current_tier() == LevelTier::small);
    CHECK(progress.completed_levels() == 0u);
    CHECK_FALSE(progress.completed());

    CHECK(progress.complete_current_level() == LevelTransition::advanced);
    CHECK(progress.current_tier() == LevelTier::medium);
    CHECK(progress.completed_levels() == 1u);

    CHECK(progress.complete_current_level() == LevelTransition::advanced);
    CHECK(progress.current_tier() == LevelTier::large);
    CHECK(progress.completed_levels() == 2u);

    CHECK(progress.complete_current_level() == LevelTransition::advanced);
    CHECK(progress.current_tier() == LevelTier::x_large);
    CHECK(progress.completed_levels() == 3u);

    CHECK(progress.complete_current_level() == LevelTransition::expedition_completed);
    CHECK(progress.current_tier() == LevelTier::x_large);
    CHECK(progress.completed_levels() == 4u);
    CHECK(progress.completed());

    CHECK(progress.complete_current_level() == LevelTransition::none);
    CHECK(progress.completed_levels() == 4u);
}

}  // TEST_SUITE("game")

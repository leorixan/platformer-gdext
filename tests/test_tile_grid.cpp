#include "third_party/doctest.h"
#include "test_world.h"

using platformer::Vec2;

// The collision contract these tests pin down is the reason the core does not need
// the shape-shrinking patches the GDScript version carried: touching edges are not
// a hit, so a body whose box is exactly as wide as a gap goes through it.

TEST_CASE("an 8px body fits an 8px corridor exactly") {
	// Walls at columns 0 and 2, corridor at column 1 (x in [8, 16)).
	std::vector<std::string> rows(8, std::string(3, '.'));
	testing::fill_rect(rows, 0, 0, 0, 7, '#');
	testing::fill_rect(rows, 2, 0, 2, 7, '#');

	platformer::TileGrid grid;
	grid.load(rows);

	// Body box [8, 16] x [0, 8]: flush with both walls, overlapping neither.
	CHECK_FALSE(grid.overlaps_box(Vec2(8.0f, 0.0f), Vec2(16.0f, 8.0f)));
	// One pixel into either wall is a genuine overlap.
	CHECK(grid.overlaps_box(Vec2(7.0f, 0.0f), Vec2(15.0f, 8.0f)));
	CHECK(grid.overlaps_box(Vec2(9.0f, 0.0f), Vec2(17.0f, 8.0f)));
	// Sitting just outside the left wall is not an overlap either.
	CHECK_FALSE(grid.overlaps_box(Vec2(-8.0f, 0.0f), Vec2(0.0f, 8.0f)));
}

TEST_CASE("a body falls through a shaft exactly one brick wide") {
	// Walls at columns 5 and 7 leave an 8px shaft at column 6, and the body is
	// exactly 8px wide. The body therefore touches both walls for the whole fall:
	// with any margin at all it would jam at the top of the shaft.
	std::vector<std::string> rows = testing::room_with_floor(24, 30, 20);
	testing::fill_rect(rows, 5, 4, 5, 19, '#');
	testing::fill_rect(rows, 7, 4, 7, 19, '#');

	testing::World world;
	world.build(rows, Vec2(52.0f, 30.0f)); // centred on the shaft, above it
	world.run(testing::hold(0.0f), 90);

	CHECK(world.player().position().x == 52.0f); // never squeezed sideways
	CHECK(world.player().position().y == 160.0f); // landed on the floor at row 20
}

TEST_CASE("out of bounds is empty space, not an invisible wall") {
	platformer::TileGrid grid;
	grid.load({"###", "###"});

	CHECK(grid.is_solid(0, 0));
	CHECK_FALSE(grid.is_solid(-1, 0));
	CHECK_FALSE(grid.is_solid(0, -1));
	CHECK_FALSE(grid.is_solid(3, 0));
	CHECK_FALSE(grid.overlaps_box(Vec2(-100.0f, -100.0f), Vec2(-90.0f, -90.0f)));
}

TEST_CASE("fill_rect and set_solid address tiles directly") {
	platformer::TileGrid grid;
	grid.load(std::vector<std::string>(4, std::string(4, '.')));
	grid.fill_rect(1, 1, 2, 2);

	CHECK(grid.cols() == 4);
	CHECK(grid.rows() == 4);
	CHECK(grid.is_solid(1, 1));
	CHECK(grid.is_solid(2, 2));
	CHECK_FALSE(grid.is_solid(0, 0));
	CHECK_FALSE(grid.is_solid(3, 3));

	// Out-of-range writes are ignored rather than wrapping into a neighbouring row.
	grid.set_solid(9, 9, true);
	CHECK(grid.is_solid(1, 1));
	CHECK_FALSE(grid.is_solid(3, 3));
}

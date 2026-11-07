#include "third_party/doctest.h"
#include "test_world.h"

using platformer::Vec2;

namespace {

constexpr int kFloorRow = 10;
constexpr float kFloorY = kFloorRow * 8.0f;
// A floor far below the spawn, so a jump arc can be measured in mid-air without
// anything in the way and without the player reaching the ground mid-measurement.
constexpr int kDeepFloorRow = 50;
constexpr float kDeepFloorY = kDeepFloorRow * 8.0f;

float highest_point(testing::World &p_world, const platformer::InputFrame &p_input, int p_steps) {
	float top = p_world.player().position().y;
	for (int i = 0; i < p_steps; ++i) {
		p_world.player().step(p_input, testing::kDt);
		top = std::min(top, p_world.player().position().y);
	}
	return top;
}

} // namespace

TEST_CASE("a jump launches at the reference speed") {
	testing::World world;
	world.build(testing::room_with_floor(40, 40, kFloorRow), Vec2(64.0f, kFloorY));
	world.settle();

	world.press_jump(testing::hold(0.0f), 1);
	CHECK(world.player().velocity().y == doctest::Approx(-105.0f)); // JumpSpeed
	CHECK(world.player().ducking() == false);
}

TEST_CASE("a jump carries a horizontal boost in the direction held") {
	testing::World world;
	world.build(testing::room_with_floor(40, 40, kFloorRow), Vec2(64.0f, kFloorY));

	// One step at full run speed, then jump: the boost is added on top of whatever
	// the ground frame already produced, which is what makes a bunny hop keep speed.
	world.run(testing::hold(1.0f), 30);
	const float run_speed = world.player().velocity().x;
	CHECK(run_speed == doctest::Approx(90.0f));

	world.run(testing::hold(1.0f), 1);
	const float before_jump = world.player().velocity().x;
	world.press_jump(testing::hold(1.0f), 1);
	CHECK(world.player().velocity().x == doctest::Approx(before_jump + 40.0f)); // JumpHBoost
}

TEST_CASE("holding jump rises further than tapping it") {
	testing::World tapped;
	tapped.build(testing::room_with_floor(40, 60, kDeepFloorRow), Vec2(64.0f, kDeepFloorY));
	tapped.settle();
	tapped.press_jump(testing::hold(0.0f, 0.0f, true), 1);
	const float tapped_top = highest_point(tapped, testing::hold(0.0f), 60);

	testing::World held;
	held.build(testing::room_with_floor(40, 60, kDeepFloorRow), Vec2(64.0f, kDeepFloorY));
	held.settle();
	held.press_jump(testing::hold(0.0f, 0.0f, true), 1);
	const float held_top = highest_point(held, testing::hold(0.0f, 0.0f, true), 60);

	// Variable jump height lives in the held window, not in a jump-cut multiplier.
	// The reference measures a held jump at 28.6px: 3.6 bricks at 8px per brick.
	CHECK(kDeepFloorY - held_top == doctest::Approx(28.6f).epsilon(0.05));
	CHECK(tapped_top - held_top > 8.0f);
}

TEST_CASE("variable jump height stops the moment jump is released") {
	testing::World world;
	world.build(testing::room_with_floor(40, 60, kDeepFloorRow), Vec2(64.0f, kDeepFloorY));
	world.settle();

	world.press_jump(testing::hold(0.0f, 0.0f, true), 4);
	const float held_speed = world.player().velocity().y;
	CHECK(held_speed == doctest::Approx(-105.0f)); // still pinned by VarJumpTime

	world.run(testing::hold(0.0f), 1);
	CHECK(world.player().velocity().y == doctest::Approx(-105.0f + 900.0f / 60.0f));
}

TEST_CASE("coyote time allows a jump just after a ledge") {
	// Floor ends at column 19, so x = 160 is the last solid pixel.
	std::vector<std::string> rows = testing::room_with_floor(40, 40, kFloorRow);
	testing::fill_rect(rows, 20, kFloorRow, 39, 39, '.');

	testing::World world;
	world.build(rows, Vec2(140.0f, kFloorY));
	CHECK(world.wait_for_x_above(165.0f, testing::hold(1.0f)));

	world.press_jump(testing::hold(1.0f), 1);
	CHECK(world.player().velocity().y == doctest::Approx(-105.0f)); // JumpGraceTime still live
}

TEST_CASE("coyote time runs out") {
	std::vector<std::string> rows = testing::room_with_floor(40, 40, kFloorRow);
	testing::fill_rect(rows, 20, kFloorRow, 39, 39, '.');

	testing::World world;
	world.build(rows, Vec2(140.0f, kFloorY));
	world.wait_for_x_above(165.0f, testing::hold(1.0f));

	world.run(testing::hold(1.0f), 8); // 0.133s > JumpGraceTime 0.10s
	world.press_jump(testing::hold(1.0f), 1);
	CHECK(world.player().velocity().y > 0.0f); // still falling: the jump was refused
}

TEST_CASE("a jump pressed just before landing is buffered into the landing") {
	std::vector<std::string> rows = testing::room_with_floor(40, 40, kFloorRow);

	testing::World world;
	world.build(rows, Vec2(64.0f, 60.0f)); // dropped from 20px up
	world.run(testing::hold(0.0f), 8); // most of the fall
	CHECK(world.player().position().y < kFloorY);

	world.press_jump(testing::hold(0.0f), 1);

	// Step until the buffered jump fires. Its launch speed is the tell: nothing else
	// in this room writes exactly -JumpSpeed downwards-to-upwards.
	bool jumped = false;
	for (int i = 0; i < 30; ++i) {
		if (world.player().velocity().y == doctest::Approx(-105.0f)) {
			jumped = true;
			break;
		}
		world.player().step(testing::hold(0.0f), testing::kDt);
	}
	CHECK(jumped);
	// The buffered jump fires on the landing frame and lifts off again immediately,
	// so the player is a pixel or two above the floor and never still falling.
	CHECK(world.player().position().y <= kFloorY);
	CHECK(world.player().position().y > kFloorY - 4.0f);
}

TEST_CASE("a jump pressed long before landing is not buffered") {
	testing::World world;
	world.build(testing::room_with_floor(40, 40, kFloorRow), Vec2(64.0f, 40.0f)); // dropped from 40px up

	world.press_jump(testing::hold(0.0f), 1); // far too early
	world.run(testing::hold(0.0f), 60);

	CHECK(world.player().position().y == kFloorY); // landed and stayed
	CHECK(world.player().velocity().y == doctest::Approx(0.0f));
}

TEST_CASE("a ceiling stops a jump dead") {
	// Ceiling one brick above the standing head, spanning the whole room.
	std::vector<std::string> rows = testing::room_with_floor(40, 40, kFloorRow);
	testing::fill_rect(rows, 0, 7, 39, 7, '#');

	testing::World world;
	world.build(rows, Vec2(64.0f, kFloorY));
	world.press_jump(testing::hold(0.0f, 0.0f, true), 1);
	world.run(testing::hold(0.0f, 0.0f, true), 30);

	// Head height is position.y - 11, and the ceiling occupies y in [56, 64).
	CHECK(world.player().position().y >= 75.0f);
}

TEST_CASE("a rising jump corrects around a corner into a one-brick gap") {
	// Ceiling at row 6 with a single brick missing at column 8 (x in [64, 72]).
	// The player jumps from one pixel off centre, so the box clips the ceiling and
	// has to be shifted a pixel sideways to fit through the gap.
	std::vector<std::string> rows = testing::room_with_floor(40, 40, kFloorRow);
	testing::fill_rect(rows, 0, 6, 39, 6, '#');
	testing::fill_rect(rows, 8, 6, 8, 6, '.');

	testing::World world;
	world.build(rows, Vec2(69.0f, kFloorY));
	world.press_jump(testing::hold(0.0f, 0.0f, true), 1);

	// Step until the correction happens, or give up after half a second of rising.
	bool corrected = false;
	for (int i = 0; i < 30; ++i) {
		if (world.player().last_technique() == platformer::Technique::kCornerCorrect) {
			corrected = true;
			break;
		}
		world.player().step(testing::hold(0.0f, 0.0f, true), testing::kDt);
	}

	CHECK(corrected);
	CHECK(world.player().position().x == 68.0f); // recentred onto the gap
	CHECK(world.player().velocity().x == doctest::Approx(0.0f));
}

TEST_CASE("corner correction only searches the way the player is moving") {
	// The reference sweeps one direction: Speed.X < 0 tries the left shifts, Speed.X
	// > 0 the right ones. Same room and same one-pixel starting offset as the test
	// above, so only the direction of travel differs: the gap is to the right, and a
	// player rising while moving left never looks there.
	std::vector<std::string> rows = testing::room_with_floor(40, 40, kFloorRow);
	testing::fill_rect(rows, 0, 6, 39, 6, '#');
	testing::fill_rect(rows, 8, 6, 8, 6, '.');

	testing::World world;
	world.build(rows, Vec2(67.0f, kFloorY));
	world.settle();
	CHECK(world.player().velocity().x == doctest::Approx(0.0f));

	world.press_jump(testing::hold(-1.0f, 0.0f, true), 1);
	CHECK(world.player().velocity().x < 0.0f);

	const float top = highest_point(world, testing::hold(-1.0f, 0.0f, true), 15);

	CHECK(top >= 66.0f); // stopped by the ceiling; through the gap it reaches 51
	CHECK(world.player().position().x < 67.0f); // carried left, never nudged right
	CHECK(world.player().last_technique() != platformer::Technique::kCornerCorrect);
}

TEST_CASE("corner correction will not shove the player backwards") {
	// Mirror image: now the only place the box fits is behind the player, which the
	// rightward search never considers. The jump is stopped by the ceiling instead
	// of being redirected, and the player is never pulled back towards the gap.
	std::vector<std::string> rows = testing::room_with_floor(40, 40, kFloorRow);
	testing::fill_rect(rows, 0, 6, 39, 6, '#');
	testing::fill_rect(rows, 8, 6, 8, 6, '.');

	testing::World world;
	world.build(rows, Vec2(70.0f, kFloorY));
	world.settle();

	world.press_jump(testing::hold(1.0f, 0.0f, true), 1);
	CHECK(world.player().velocity().x > 0.0f);

	const float top = highest_point(world, testing::hold(1.0f, 0.0f, true), 15);

	CHECK(top >= 66.0f); // the ceiling won
	CHECK(world.player().position().x > 70.0f); // and the player kept going forwards
	CHECK(world.player().last_technique() != platformer::Technique::kCornerCorrect);
}

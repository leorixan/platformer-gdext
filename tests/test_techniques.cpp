#include "third_party/doctest.h"
#include "test_world.h"

using platformer::Mode;
using platformer::Technique;
using platformer::Vec2;

// None of these are separate moves in the implementation. They all fall out of
// three rules -- the dash slide, the super jump, and ducking -- so what is worth
// asserting is the speed each one lands on, to one decimal place, against the
// values measured from the reference.
namespace {

constexpr int kFloorRow = 10;
constexpr float kFloorY = kFloorRow * 8.0f; // 80

void make_arena(testing::World &r_world) {
	r_world.build(testing::room_with_floor(80, 30, kFloorRow), Vec2(64.0f, kFloorY));
}

} // namespace

TEST_CASE("a ground dash cancelled into a jump is a Super") {
	testing::World world;
	make_arena(world);
	world.settle();
	world.run(testing::hold(1.0f), 25);

	world.press_dash(testing::hold(1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f)));
	CHECK(world.player().velocity().x == doctest::Approx(240.0f));

	world.press_jump(testing::hold(1.0f, 0.0f, true), 1);

	CHECK(world.player().last_technique() == Technique::kSuper);
	CHECK(world.player().velocity().x == doctest::Approx(260.0f)); // SuperJumpH
	CHECK(world.player().velocity().y == doctest::Approx(-105.0f)); // JumpSpeed, not a fresh jump
	CHECK(world.player().mode() == Mode::kNormal);
	CHECK_FALSE(world.player().ducking());
}

TEST_CASE("a ground down-diagonal dash cancelled into a jump is a Hyper") {
	testing::World world;
	make_arena(world);
	world.settle();
	world.run(testing::hold(1.0f), 25);

	world.press_dash(testing::hold(1.0f, 1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f, 1.0f)));
	CHECK(world.player().ducking()); // the dash slide already happened

	world.press_jump(testing::hold(1.0f, 0.0f, true), 1);

	// The only difference from a Super is that the player was ducking when the
	// jump was taken, so the pair of multipliers applies.
	CHECK(world.player().last_technique() == Technique::kHyper);
	CHECK(world.player().velocity().x == doctest::Approx(325.0f)); // 260 * 1.25
	CHECK(world.player().velocity().y == doctest::Approx(-52.5f)); // -105 * 0.5
	CHECK_FALSE(world.player().ducking());
}

TEST_CASE("an airborne down-diagonal dash that lands is an Ultra") {
	// Same code path as a Hyper; the only thing that separates them is that this
	// dash started in the air, so the landing has to convert it into a slide.
	testing::World world;
	world.build(testing::room_with_floor(120, 60, 20), Vec2(160.0f, 140.0f)); // floor top y = 160

	world.press_dash(testing::hold(1.0f, 1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f, 1.0f)));
	CHECK_FALSE(world.player().dash_started_on_ground());

	// Landing is what converts an airborne diagonal dash into a slide, so the wait
	// is for the duck that the slide sets.
	bool landed = false;
	for (int i = 0; i < 20; ++i) {
		world.player().step(testing::hold(1.0f, 1.0f), testing::kDt);
		if (world.player().ducking()) {
			landed = true;
			break;
		}
	}
	CHECK(landed);
	CHECK(world.player().position().y == 160.0f); // on the floor
	CHECK(world.player().mode() == Mode::kDash); // still dashing: the slide is inside the dash
	// The slide speed survives the landing -- nothing zeroes the horizontal speed.
	CHECK(world.player().velocity().x == doctest::Approx(240.0f / std::sqrt(2.0f) * 1.2f));

	world.press_jump(testing::hold(1.0f, 0.0f, true), 1);

	CHECK(world.player().last_technique() == Technique::kUltra);
	CHECK(world.player().velocity().x == doctest::Approx(325.0f));
	CHECK(world.player().velocity().y == doctest::Approx(-52.5f));
}

TEST_CASE("turning around mid-dash points the Super and Hyper the other way") {
	// Facing keeps following moveX during a dash, which is the whole reason a
	// reverse Hyper exists at all.
	testing::World world;
	make_arena(world);
	world.settle();
	world.run(testing::hold(1.0f), 25);

	world.press_dash(testing::hold(1.0f, 1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f, 1.0f)));
	CHECK(world.player().facing() == doctest::Approx(1.0f));

	world.run(testing::hold(-1.0f), 1);
	CHECK(world.player().facing() == doctest::Approx(-1.0f));

	world.press_jump(testing::hold(-1.0f, 0.0f, true), 1);

	CHECK(world.player().last_technique() == Technique::kHyper);
	CHECK(world.player().velocity().x == doctest::Approx(-325.0f));
	CHECK(world.player().velocity().y == doctest::Approx(-52.5f));
}

TEST_CASE("an upward dash caught on a platform edge is a SuperWallJump") {
	// A one-brick platform floating above the floor. The player dashes straight up
	// just clear of its left face, so the wall only appears beside the body once
	// they have risen level with it.
	std::vector<std::string> rows = testing::room_with_floor(80, 30, kFloorRow);
	testing::fill_rect(rows, 40, 5, 40, 5, '#'); // x in [320, 328), y in [40, 48)

	testing::World world;
	world.build(rows, Vec2(315.0f, kFloorY)); // body [311, 319]: one pixel clear of it
	world.settle();

	world.press_dash(testing::hold(0.0f, -1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(0.0f, -1.0f)));
	CHECK(world.player().dash_dir() == Vec2(0.0f, -1.0f));
	CHECK(world.player().wall_direction() == 0); // nothing beside the body yet

	// Rise until the platform face is beside the body. What makes this a *super*
	// wall jump is the upward dash direction; the wall only has to be there.
	int frames = 0;
	while (frames < 12 && world.player().wall_direction() == 0) {
		world.player().step(testing::hold(0.0f, -1.0f), testing::kDt);
		++frames;
	}
	CHECK(world.player().wall_direction() != 0);

	world.press_jump(testing::hold(0.0f, -1.0f, true), 1);

	CHECK(world.player().last_technique() == Technique::kSuperWallJump);
	CHECK(world.player().velocity().x == doctest::Approx(-170.0f)); // SuperWallJumpH, away from it
	CHECK(world.player().velocity().y == doctest::Approx(-160.0f)); // SuperWallJumpSpeed
}

TEST_CASE("a dash that only goes up turns any wall into a SuperWallJump") {
	// The same rule without the geometry: what makes it a *super* wall jump is the
	// dash direction, not the platform. A side wall works just as well.
	std::vector<std::string> rows = testing::room_with_floor(80, 30, kFloorRow);
	testing::fill_rect(rows, 0, 0, 12, 29, '#'); // wall, right face at x = 104

	testing::World world;
	world.build(rows, Vec2(108.0f, kFloorY)); // body [104, 112], flush
	world.settle();

	world.press_dash(testing::hold(0.0f, -1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(0.0f, -1.0f)));
	CHECK(world.player().wall_direction() == -1); // the wall is behind the player

	world.press_jump(testing::hold(0.0f, -1.0f, true), 1);

	CHECK(world.player().last_technique() == Technique::kSuperWallJump);
	CHECK(world.player().velocity().x == doctest::Approx(170.0f)); // away from the wall
	CHECK(world.player().velocity().y == doctest::Approx(-160.0f));
}

TEST_CASE("a Super bleeds back to the run cap instead of being clipped to it") {
	// Over-speed is reduced at RunReduce rather than snapped down, so a Super
	// decays over a dozen or so frames and the player keeps most of it for the
	// first part of the arc. Clipping it to MaxRun on the first airborne frame is
	// the failure this guards against.
	testing::World world;
	make_arena(world);
	world.settle();
	world.run(testing::hold(1.0f), 25);

	world.press_dash(testing::hold(1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f)));
	world.press_jump(testing::hold(1.0f, 0.0f, true), 1);
	CHECK(world.player().velocity().x == doctest::Approx(260.0f));

	world.run(testing::hold(1.0f), 4);
	CHECK(world.player().velocity().x < 260.0f);
	CHECK(world.player().velocity().x > 90.0f); // still well above the cap

	world.run(testing::hold(1.0f), 60);
	CHECK(world.player().velocity().x == doctest::Approx(90.0f)); // and settles at MaxRun
}

TEST_CASE("a landing does not eat the run speed before the jump can use it") {
	// The jump is evaluated after the ground friction step, so a jump taken on the
	// frame the player lands adds JumpHBoost to the speed they landed with. This
	// is what makes a chain of hops hold speed rather than stall on touchdown.
	testing::World world;
	make_arena(world);
	world.settle();
	world.run(testing::hold(1.0f), 25);

	world.press_dash(testing::hold(1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f)));
	world.press_jump(testing::hold(1.0f, 0.0f, true), 1);
	CHECK(world.player().velocity().x == doctest::Approx(260.0f));

	// Fly the whole Super out and come back down, still holding right.
	bool landed = false;
	for (int i = 0; i < 120; ++i) {
		world.player().step(testing::hold(1.0f, 0.0f, true), testing::kDt);
		if (world.player().position().y == kFloorY && world.player().velocity().y == 0.0f) {
			landed = true;
			break;
		}
	}
	CHECK(landed);
	CHECK(world.player().velocity().x == doctest::Approx(90.0f)); // back at the cap

	world.press_jump(testing::hold(1.0f, 0.0f, true), 1);
	CHECK(world.player().velocity().x == doctest::Approx(130.0f)); // MaxRun + JumpHBoost
}


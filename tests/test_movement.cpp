#include "third_party/doctest.h"
#include "test_world.h"

using platformer::Vec2;

namespace {

// A wide room with a floor at row 10 (top edge y = 80) and a second floor far below,
// so "airborne" tests have somewhere to fall to.
constexpr int kFloorRow = 10;
constexpr float kFloorY = kFloorRow * 8.0f; // 80

void make_grounded_world(testing::World &r_world) {
	r_world.build(testing::room_with_floor(60, 30, kFloorRow), Vec2(64.0f, kFloorY));
}

// Deep enough that nothing reaches the floor while a fall cap is being measured.
void make_airborne_world(testing::World &r_world) {
	r_world.build(testing::room_with_floor(80, 200, 180), Vec2(64.0f, kFloorY));
}

} // namespace

TEST_CASE("running reaches and holds the run speed cap") {
	testing::World world;
	make_grounded_world(world);
	const platformer::InputFrame right = testing::hold(1.0f);

	// One step of ground acceleration: RunAccel * dt.
	world.run(right, 1);
	CHECK(world.player().velocity().x == doctest::Approx(1000.0f / 60.0f));

	world.run(right, 60);
	CHECK(world.player().velocity().x == doctest::Approx(90.0f)); // MaxRun, not a pixel more
}

TEST_CASE("running the other way turns around at the same rate") {
	testing::World world;
	make_grounded_world(world);
	world.run(testing::hold(1.0f), 60);
	CHECK(world.player().velocity().x == doctest::Approx(90.0f));

	world.run(testing::hold(-1.0f), 1);
	CHECK(world.player().velocity().x == doctest::Approx(90.0f - 1000.0f / 60.0f));
	world.player().facing();
	CHECK(world.player().facing() == -1.0f);
}

TEST_CASE("air acceleration is a fraction of the ground rate") {
	testing::World world;
	make_airborne_world(world);
	world.run(testing::hold(1.0f), 1);
	CHECK(world.player().velocity().x == doctest::Approx(1000.0f * 0.65f / 60.0f));
}

TEST_CASE("speed above the cap bleeds off instead of accelerating") {
	// Ground dash into a Super jump: 260px/s, well above MaxRun.
	testing::World world;
	make_grounded_world(world);
	world.press_dash(testing::hold(1.0f), 4); // through the freeze, dash is live
	world.press_jump(testing::hold(1.0f), 1); // Super
	CHECK(world.player().velocity().x == doctest::Approx(260.0f));

	world.run(testing::hold(1.0f), 1);
	// Airborne, so the reduce rate is scaled by AirMult: 400 * 0.65 / 60.
	CHECK(world.player().velocity().x == doctest::Approx(260.0f - 400.0f * 0.65f / 60.0f));
}

TEST_CASE("gravity accelerates towards the fall cap and stops there") {
	testing::World world;
	make_airborne_world(world);
	world.run(testing::hold(0.0f), 1);
	CHECK(world.player().velocity().y == doctest::Approx(900.0f / 60.0f));

	world.run(testing::hold(0.0f), 60);
	CHECK(world.player().velocity().y == doctest::Approx(160.0f)); // MaxFall
}

TEST_CASE("holding down drifts the fall cap towards FastMaxFall") {
	testing::World world;
	make_airborne_world(world);
	// Long enough for the cap itself to travel 160 -> 240 at FastMaxAccel.
	world.run(testing::hold(0.0f, 1.0f), 60);
	CHECK(world.player().velocity().y == doctest::Approx(240.0f));
	CHECK(world.player().velocity().y < 240.0f + 0.001f);
}

TEST_CASE("the fall cap comes back when the down input is released") {
	testing::World world;
	make_airborne_world(world);
	world.run(testing::hold(0.0f, 1.0f), 60);
	CHECK(world.player().velocity().y == doctest::Approx(240.0f));

	world.run(testing::hold(0.0f), 60);
	CHECK(world.player().velocity().y == doctest::Approx(160.0f));
}

TEST_CASE("a rising player is not treated as standing on the ground") {
	// The ground probe points down, so a body on its way up is never grounded even
	// with solid floor directly beneath it. Without that, a jump would keep drawing
	// the ground acceleration rate and a rising dash would count as a ground dash.
	testing::World world;
	make_grounded_world(world);
	world.settle();
	world.press_jump(testing::hold(1.0f, 0.0f, true), 1);
	CHECK(world.player().velocity().y < 0.0f);
	// The launch frame still ran the ground step, then the jump added JumpHBoost.
	CHECK(world.player().velocity().x == doctest::Approx(1000.0f / 60.0f + 40.0f));

	world.run(testing::hold(1.0f, 0.0f, true), 1);
	// AirMult now applies: the same input adds 1000 * 0.65 / 60, not the ground rate.
	CHECK(world.player().velocity().x == doctest::Approx(1000.0f / 60.0f + 40.0f + 1000.0f * 0.65f / 60.0f));
}

TEST_CASE("gravity is halved near the apex while jump is held") {
	const float frame = 900.0f / 60.0f;

	testing::World held;
	make_airborne_world(held);
	held.run(testing::hold(0.0f, 0.0f, true), 1);
	CHECK(held.player().velocity().y == doctest::Approx(frame * 0.5f));

	testing::World free_fall;
	make_airborne_world(free_fall);
	free_fall.run(testing::hold(0.0f), 1);
	CHECK(free_fall.player().velocity().y == doctest::Approx(frame));

	// The band is HalfGravThreshold: the halving stops as soon as the speed passes
	// 40, which after 6 halved frames lands on 45.
	held.run(testing::hold(0.0f, 0.0f, true), 6);
	CHECK(held.player().velocity().y == doctest::Approx(60.0f));
	free_fall.run(testing::hold(0.0f), 6);
	CHECK(free_fall.player().velocity().y == doctest::Approx(105.0f));
}

TEST_CASE("ducking keeps the feet planted and lowers the head") {
	testing::World world;
	make_grounded_world(world);
	CHECK_FALSE(world.player().ducking());
	const float feet = world.player().position().y;

	world.run(testing::hold(0.0f, 1.0f), 1);
	CHECK(world.player().ducking());
	CHECK(world.player().position().y == feet);

	world.run(testing::hold(0.0f), 1);
	CHECK_FALSE(world.player().ducking());
	CHECK(world.player().position().y == feet);
}

TEST_CASE("ducking is only possible on the ground") {
	testing::World world;
	make_airborne_world(world);
	world.run(testing::hold(0.0f, 1.0f), 10);
	CHECK_FALSE(world.player().ducking());
}

TEST_CASE("a ducked player slides with friction and a standing one does not") {
	testing::World world;
	make_grounded_world(world);
	world.run(testing::hold(1.0f), 60);
	CHECK(world.player().velocity().x == doctest::Approx(90.0f));

	// DuckFriction 500 applies instead of acceleration, so the input is ignored.
	world.run(testing::hold(1.0f, 1.0f), 1);
	CHECK(world.player().ducking());
	CHECK(world.player().velocity().x == doctest::Approx(90.0f - 500.0f / 60.0f));

	world.run(testing::hold(1.0f, 1.0f), 60);
	CHECK(world.player().velocity().x == doctest::Approx(0.0f));
}

TEST_CASE("standing up is refused while there is no headroom") {
	std::vector<std::string> rows = testing::room_with_floor(20, 20, kFloorRow);
	testing::fill_rect(rows, 0, 8, 19, 8, '#'); // one brick of headroom everywhere

	testing::World world;
	world.build(rows, Vec2(64.0f, kFloorY));
	world.run(testing::hold(0.0f, 1.0f), 2);
	CHECK(world.player().ducking());

	// Releasing down cannot stand the player up: the 11px box does not fit.
	world.run(testing::hold(0.0f), 10);
	CHECK(world.player().ducking());
}

TEST_CASE("a low corridor is crossed on momentum, never on foot") {
	// A ducked player on the ground is under DuckFriction and has no drive at all,
	// so a one-brick corridor can only be crossed by arriving with speed. The dash
	// slide does that and leaves the player ducked for the whole traverse.
	std::vector<std::string> rows = testing::room_with_floor(20, 20, kFloorRow);
	testing::fill_rect(rows, 4, 8, 7, 8, '#'); // ceiling, x in [32, 64)

	testing::World world;
	world.build(rows, Vec2(24.0f, kFloorY)); // on open floor, short of the corridor
	world.press_dash(testing::hold(1.0f, 1.0f), 12); // down-right dash becomes a slide

	CHECK(world.player().ducking());
	CHECK(world.player().position().x > 32.0f); // inside the corridor
	CHECK(world.player().position().x < 64.0f);

	// Release everything: friction carries the slide out the far end, and only then
	// is there headroom to stand up again.
	world.run(testing::hold(0.0f), 60);
	CHECK(world.player().position().x > 64.0f);
	CHECK_FALSE(world.player().ducking());
	CHECK(world.player().position().y == kFloorY);
}

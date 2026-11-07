#include "third_party/doctest.h"
#include "test_world.h"

using platformer::Mode;
using platformer::Vec2;

namespace {

constexpr int kFloorRow = 10;
constexpr float kFloorY = kFloorRow * 8.0f; // 80

// Room with room to dash into: floor at row 10, nothing else in the way. The caller
// owns the world and passes it in, because a World holds a player that points at its
// own grid and so must never be copied.
void make_arena(testing::World &r_world) {
	r_world.build(testing::room_with_floor(80, 30, kFloorRow), Vec2(64.0f, kFloorY));
}

} // namespace

TEST_CASE("a dash freezes the body before it goes anywhere") {
	testing::World world;
	make_arena(world);
	world.settle();
	const Vec2 start = world.player().position();

	world.press_dash(testing::hold(1.0f), 1);
	CHECK(world.player().mode() == Mode::kDash);
	CHECK(world.player().dash_dir() == Vec2()); // no aim yet: the freeze has not ended
	CHECK(world.player().velocity() == Vec2()); // and the speed is zeroed on the spot
	CHECK(world.player().position() == start);

	// The freeze is Celeste.Freeze(0.05): three frames at 1/60, the press included.
	world.run(testing::hold(1.0f), 1);
	CHECK(world.player().position() == start);
	world.run(testing::hold(1.0f), 1);
	CHECK(world.player().position() == start);

	world.run(testing::hold(1.0f), 1);
	CHECK(world.player().position().x > start.x); // the third frame is the launch
}

TEST_CASE("the dash aim is read when the freeze ends, not when the key goes down") {
	// This is what makes the eight-way aim feel instant: the dash key can arrive
	// before, with, or a frame after the direction and still be aimed correctly.
	testing::World world;
	make_arena(world);
	world.settle();

	world.press_dash(testing::hold(1.0f), 1); // aimed right at press time
	CHECK(world.wait_for_dash_launch(testing::hold(-1.0f))); // but pushed left during the freeze
	CHECK(world.player().dash_dir() == Vec2(-1.0f, 0.0f));
	CHECK(world.player().velocity().x == doctest::Approx(-240.0f));
}

TEST_CASE("a dash aimed during the freeze is accepted") {
	testing::World world;
	make_arena(world);
	world.settle();

	world.press_dash(testing::hold(0.0f), 1); // no direction held at all
	CHECK(world.player().dash_dir() == Vec2());

	CHECK(world.wait_for_dash_launch(testing::hold(0.0f, -1.0f))); // press up while frozen
	CHECK(world.player().dash_dir().y == doctest::Approx(-1.0f));
	CHECK(world.player().dash_dir().x == doctest::Approx(0.0f));
	CHECK(world.player().velocity().y == doctest::Approx(-240.0f)); // DashSpeed, upward
}

TEST_CASE("a dash with no aim falls back to the facing direction") {
	testing::World world;
	make_arena(world);
	world.settle();
	world.run(testing::hold(-1.0f), 2); // face left
	CHECK(world.player().facing() == -1.0f);

	world.press_dash(testing::hold(0.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(0.0f)));
	CHECK(world.player().dash_dir() == Vec2(-1.0f, 0.0f));
	CHECK(world.player().velocity().x == doctest::Approx(-240.0f));
}

TEST_CASE("a dash overwrites the run speed instead of adding to it") {
	testing::World world;
	make_arena(world);
	world.run(testing::hold(1.0f), 60);
	CHECK(world.player().velocity().x == doctest::Approx(90.0f)); // MaxRun

	world.press_dash(testing::hold(1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f)));
	CHECK(world.player().velocity().x == doctest::Approx(240.0f));
}

TEST_CASE("a dash holds DashSpeed for its duration and hands over at EndDashSpeed") {
	testing::World world;
	make_arena(world);
	world.settle();

	world.press_dash(testing::hold(1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f)));

	// The speed is written once and never recomputed, so it is still exactly
	// DashSpeed on the last frame the dash is live.
	CHECK(world.wait_for_dash_end(testing::hold(1.0f)));
	CHECK(world.player().last_technique() == platformer::Technique::kDashEnd);
	CHECK(world.player().velocity().x == doctest::Approx(160.0f)); // EndDashSpeed
	CHECK(world.player().velocity().x > 90.0f); // and still faster than a run
}

TEST_CASE("an upward dash keeps three quarters of its end speed") {
	testing::World world;
	make_arena(world);
	world.settle();

	world.press_dash(testing::hold(0.0f, -1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(0.0f, -1.0f)));
	CHECK(world.player().velocity().y == doctest::Approx(-240.0f));

	CHECK(world.wait_for_dash_end(testing::hold(0.0f, -1.0f)));
	CHECK(world.player().velocity().y == doctest::Approx(-120.0f)); // EndDashSpeed * EndDashUpMult
}

TEST_CASE("a dash remembers whether it started on the ground") {
	testing::World grounded;
	make_arena(grounded);
	grounded.settle();
	grounded.press_dash(testing::hold(1.0f), 1);
	grounded.wait_for_dash_launch(testing::hold(1.0f));
	CHECK(grounded.player().dash_started_on_ground());

	// Dropped into the air, the same dash is an airborne one. The distinction is
	// what separates a Hyper from an Ultra and what enables the dash corner
	// correction, so it comes from the geometric probe only -- never from coyote.
	testing::World airborne;
	airborne.build(testing::room_with_floor(80, 120, 100), Vec2(64.0f, 80.0f));
	airborne.run(testing::hold(0.0f), 12);
	CHECK_FALSE(airborne.player().position().y == kFloorY);
	airborne.press_dash(testing::hold(1.0f), 1);
	airborne.wait_for_dash_launch(testing::hold(1.0f));
	CHECK_FALSE(airborne.player().dash_started_on_ground());
}

TEST_CASE("a dash taken on the way up counts as an airborne dash") {
	// Same probe as above, from the other side: rising over a solid floor is still
	// airborne. This is the difference between a Hyper and an Ultra, and it also
	// decides whether the dash corner correction is allowed to run.
	testing::World world;
	make_arena(world);
	world.settle();
	world.press_jump(testing::hold(1.0f, 0.0f, true), 1);
	CHECK(world.player().velocity().y < 0.0f);
	CHECK(world.player().position().y < kFloorY); // off the floor already, but rising

	world.press_dash(testing::hold(1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f)));

	CHECK_FALSE(world.player().dash_started_on_ground());
}

TEST_CASE("a dash count is spent and comes back only after the refill cooldown") {
	// DashRefillCooldown is what makes an extended dash possible: dash again inside
	// the window after landing and the dash stays spent.
	testing::World world;
	make_arena(world);
	world.settle();
	CHECK(world.player().dash_count() == 1);

	world.press_dash(testing::hold(1.0f), 1);
	CHECK(world.player().dash_count() == 0);

	// Still standing on the floor, but the cooldown has not run out yet.
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f)));
	CHECK(world.player().dash_count() == 0);

	world.run(testing::hold(1.0f), 8);
	CHECK(world.player().dash_count() == 1); // 0.10s of grounded frames later
}

TEST_CASE("a second dash is refused while the count is spent") {
	testing::World world;
	make_arena(world);
	world.settle();
	world.press_dash(testing::hold(1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f)));
	CHECK(world.player().dash_count() == 0);

	// The refill only lands on grounded frames, so take off before it does: this is
	// the extended dash, and the dash is gone for the whole flight.
	world.press_jump(testing::hold(1.0f, 0.0f, true), 1);
	CHECK(world.player().last_technique() == platformer::Technique::kSuper);
	world.run(testing::hold(0.0f, 0.0f, true), 6);
	CHECK_FALSE(world.player().mode() == Mode::kDash);
	CHECK(world.player().dash_count() == 0);

	world.press_dash(testing::hold(1.0f), 1);
	CHECK(world.player().mode() != Mode::kDash); // refused
	CHECK(world.player().velocity().x < 240.0f);
}

TEST_CASE("a ground dash counts as standing on the floor even with no downward motion") {
	// The geometric probe, not a landing flag: a horizontal dash has vy = 0 and no
	// downward move at all, yet coyote time and the dash refill must both keep
	// running. Without this, a Super never gets its dash back.
	testing::World world;
	make_arena(world);
	world.settle();

	world.press_dash(testing::hold(1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f)));
	CHECK(world.player().velocity().y == doctest::Approx(0.0f));

	// Still inside the refill cooldown, so the count is spent...
	CHECK(world.player().dash_count() == 0);
	// ...but once it lapses the grounded probe hands the dash straight back.
	world.run(testing::hold(1.0f), 8);
	CHECK(world.player().dash_count() == 1);
	CHECK(world.player().position().y == kFloorY);
}

TEST_CASE("a down-diagonal dash on the ground becomes a dash slide") {
	// The slide is the whole of Hyper and Ultra: the diagonal dash converts to a
	// horizontal one, speeds up, and ducks. Nothing about it is a special case.
	testing::World world;
	make_arena(world);
	world.settle();
	world.run(testing::hold(1.0f), 25);
	const float run_speed = world.player().velocity().x;

	world.press_dash(testing::hold(1.0f, 1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f, 1.0f)));

	CHECK(world.player().ducking());
	CHECK(world.player().dash_dir() == Vec2(1.0f, 0.0f)); // straightened out
	CHECK(world.player().velocity().y == doctest::Approx(0.0f));
	// The diagonal is normalised, so the slide is DashSpeed / sqrt(2) * DodgeSlide.
	CHECK(world.player().velocity().x == doctest::Approx(240.0f / std::sqrt(2.0f) * 1.2f));
	CHECK(world.player().velocity().x > run_speed);
}

TEST_CASE("a dash slide is handed over as a run, not as a stop") {
	// finish_dash rewrites the speed whenever DashDir.Y <= 0, and a slide has
	// already straightened its direction to horizontal -- so it comes out at
	// EndDashSpeed rather than at zero. Ducking survives too, which is what a
	// Hyper or an Ultra launches from.
	testing::World world;
	make_arena(world);
	world.settle();
	world.run(testing::hold(1.0f), 25);

	world.press_dash(testing::hold(1.0f, 1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(1.0f, 1.0f)));
	const float slide_speed = world.player().velocity().x;
	CHECK(slide_speed > 160.0f); // the slide itself is faster than EndDashSpeed

	CHECK(world.wait_for_dash_end(testing::hold(1.0f, 1.0f)));

	CHECK(world.player().velocity().x == doctest::Approx(160.0f)); // EndDashSpeed, not 0
	CHECK(world.player().velocity().x > 90.0f); // still faster than a run
	CHECK(world.player().ducking());
}

TEST_CASE("a down dash that never landed keeps its downward speed") {
	// The other half of the same rule: DashDir.Y is still 1, so the tail of the
	// dash does not rewrite the speed at all and the fall continues at DashSpeed.
	// No slide happens in the air, so nothing ducks either.
	testing::World world;
	world.build(testing::room_with_floor(80, 120, 100), Vec2(64.0f, 80.0f));
	world.run(testing::hold(0.0f), 6);

	world.press_dash(testing::hold(0.0f, 1.0f), 1);
	CHECK(world.wait_for_dash_launch(testing::hold(0.0f, 1.0f)));
	CHECK_FALSE(world.player().ducking());
	CHECK(world.player().dash_dir() == Vec2(0.0f, 1.0f));

	CHECK(world.wait_for_dash_end(testing::hold(0.0f, 1.0f)));
	CHECK(world.player().velocity().y == doctest::Approx(240.0f)); // DashSpeed downward, EndDashUpMult untouched
}

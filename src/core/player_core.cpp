#include "player_core.h"

#include <algorithm>
#include <cmath>

namespace platformer {

const char *technique_name(Technique p_technique) {
	switch (p_technique) {
		case Technique::kJump: return "Jump";
		case Technique::kSuper: return "Super";
		case Technique::kHyper: return "Hyper";
		case Technique::kUltra: return "Ultra";
		case Technique::kWallJump: return "WallJump";
		case Technique::kSuperWallJump: return "SuperWallJump";
		case Technique::kDash: return "Dash";
		case Technique::kDashSlide: return "DashSlide";
		case Technique::kDashEnd: return "DashEnd";
		case Technique::kCornerCorrect: return "CornerCorrect";
		case Technique::kDashCorner: return "DashCorner";
		default: return "None";
	}
}

const char *mode_name(Mode p_mode) {
	return p_mode == Mode::kDash ? "DASH" : "NORMAL";
}

PlayerCore::PlayerCore(const PlayerConfig &p_config, CollisionQuery *p_query) :
		config_(p_config), query_(p_query) {
	reset(Vec2());
}

void PlayerCore::reset(const Vec2 &p_position, float p_facing) {
	position_ = p_position;
	velocity_ = Vec2();
	pre_move_velocity_ = Vec2();
	counter_x_ = 0.0f;
	counter_y_ = 0.0f;
	mode_ = Mode::kNormal;
	dash_count_ = config_.dash.max_count;
	facing_ = p_facing >= 0.0f ? 1.0f : -1.0f;
	ducking_ = false;

	raw_move_x_ = 0.0f;
	move_x_ = 0.0f;
	move_y_ = 0.0f;
	jump_held_ = false;
	force_move_x_ = 0.0f;
	force_move_x_timer_ = 0.0f;

	jump_buffer_ = 0.0f;
	dash_buffer_ = 0.0f;
	dash_freeze_ = 0.0f;
	dash_timer_ = 0.0f;
	dash_cooldown_timer_ = 0.0f;
	dash_refill_cooldown_timer_ = 0.0f;
	dash_attack_timer_ = 0.0f;
	coyote_ = 0.0f;
	var_jump_timer_ = 0.0f;
	var_jump_speed_ = 0.0f;
	max_fall_ = config_.gravity.max_fall;

	dash_dir_ = Vec2();
	dash_started_on_ground_ = false;
	before_dash_speed_ = Vec2();
	on_ceiling_ = false;
	floor_last_frame_ = on_ground();
	last_technique_ = Technique::kNone;
}

void PlayerCore::press_jump() {
	jump_buffer_ = config_.jump.buffer_time;
}

void PlayerCore::press_dash() {
	dash_buffer_ = config_.dash.buffer_time;
}

void PlayerCore::step(const InputFrame &p_input, float p_dt) {
	poll_input(p_input, p_dt);
	tick_timers(p_dt);
	update_facing();
	if (mode_ == Mode::kDash) {
		dash_update();
	} else {
		normal_update(p_dt);
	}
	pre_move_velocity_ = velocity_;
	on_ceiling_ = false;
	move(p_dt);
	resolve_collisions();
}

void PlayerCore::poll_input(const InputFrame &p_input, float p_dt) {
	if (p_input.jump_pressed) {
		jump_buffer_ = config_.jump.buffer_time;
	}
	if (p_input.dash_pressed) {
		dash_buffer_ = config_.dash.buffer_time;
	}
	raw_move_x_ = p_input.move_x;
	move_y_ = p_input.move_y;
	jump_held_ = p_input.jump_held;

	// forceMoveX: for a moment after a wall jump the horizontal input is overridden,
	// so the player cannot be yanked straight back into the wall they just left.
	// Everything downstream reads move_x_, never the raw axis.
	if (force_move_x_timer_ > 0.0f) {
		force_move_x_timer_ = std::max(0.0f, force_move_x_timer_ - p_dt);
		move_x_ = force_move_x_;
	} else {
		move_x_ = raw_move_x_;
	}
}

void PlayerCore::tick_timers(float p_dt) {
	// The buffers keep running while frozen, so a jump pressed during the dash freeze
	// still comes out on the frame the freeze ends.
	jump_buffer_ = std::max(0.0f, jump_buffer_ - p_dt);
	dash_buffer_ = std::max(0.0f, dash_buffer_ - p_dt);

	// Celeste.Freeze: while frozen only input runs, every game timer stops.
	if (dash_freeze_ > 0.0f) {
		dash_freeze_ = std::max(0.0f, dash_freeze_ - p_dt);
		return;
	}
	coyote_ = std::max(0.0f, coyote_ - p_dt);
	var_jump_timer_ = std::max(0.0f, var_jump_timer_ - p_dt);
	dash_attack_timer_ = std::max(0.0f, dash_attack_timer_ - p_dt);
	dash_cooldown_timer_ = std::max(0.0f, dash_cooldown_timer_ - p_dt);
	dash_refill_cooldown_timer_ = std::max(0.0f, dash_refill_cooldown_timer_ - p_dt);
	if (mode_ == Mode::kDash) {
		dash_timer_ = std::max(0.0f, dash_timer_ - p_dt);
	}

	// Falling with headroom: stand back up without waiting for anything.
	if (ducking_ && velocity_.y > 0.0f && !on_ground() && can_unduck()) {
		set_duck(false);
	}
}

void PlayerCore::update_facing() {
	// A dash tracks the input too: that is the only reason a reverse Super or a
	// reverse Hyper can be aimed mid-dash.
	if (move_x_ != 0.0f) {
		facing_ = move_x_;
	}
}

void PlayerCore::normal_update(float p_dt) {
	// A dash taken this frame replaces the whole normal update, it does not run
	// alongside it.
	if (try_start_dash()) {
		return;
	}
	update_ducking(p_dt);
	apply_horizontal(p_dt);
	apply_gravity(p_dt);
	// Jump last: the launch speed is written after ground friction has had its turn,
	// so a bunny hop keeps the momentum it landed with.
	try_jump();
}

void PlayerCore::update_ducking(float p_dt) {
	if (ducking_) {
		if (!on_ground() || move_y_ > 0.0f) {
			return;
		}
		if (can_unduck()) {
			set_duck(false);
			return;
		}
		if (velocity_.x != 0.0f) {
			return;
		}
		// Stuck under a ceiling and standing still: shuffle sideways towards the
		// first spot with headroom.
		for (int i = config_.movement.duck_correct_check; i > 0; --i) {
			if (can_unduck_at(position_ + Vec2(static_cast<float>(i), 0.0f))) {
				slide_h(config_.movement.duck_correct_slide * p_dt);
				return;
			}
			if (can_unduck_at(position_ - Vec2(static_cast<float>(i), 0.0f))) {
				slide_h(-config_.movement.duck_correct_slide * p_dt);
				return;
			}
		}
	} else if (on_ground() && move_y_ > 0.0f && velocity_.y >= 0.0f) {
		set_duck(true);
	}
}

void PlayerCore::apply_horizontal(float p_dt) {
	const bool grounded = on_ground();
	if (ducking_ && grounded) {
		velocity_.x = approach(velocity_.x, 0.0f, config_.movement.duck_friction * p_dt);
		return;
	}

	float mult = 1.0f;
	if (!grounded) {
		mult = config_.movement.air_accel_mult;
	}

	// Above the run cap and still pushing: bleed off with RunReduce instead of
	// accelerating, so a Super or a Hyper decays rather than being clipped.
	float rate = config_.movement.acceleration;
	const bool over_the_cap = std::fabs(velocity_.x) > config_.movement.max_speed;
	const bool still_pushing = signf(velocity_.x) == signf(move_x_);
	if (over_the_cap && still_pushing) {
		rate = config_.movement.over_speed_decel;
	}
	velocity_.x = approach(velocity_.x, config_.movement.max_speed * move_x_, rate * mult * p_dt);
}

void PlayerCore::apply_gravity(float p_dt) {
	// Holding down past MaxFall drifts the cap towards FastMaxFall rather than
	// snapping to it, so fastfall has a ramp instead of a step.
	float target_max_fall = config_.gravity.max_fall;
	if (move_y_ > 0.0f && velocity_.y >= config_.gravity.max_fall) {
		target_max_fall = config_.gravity.fast_max_fall;
	}
	max_fall_ = approach(max_fall_, target_max_fall, config_.gravity.fast_max_accel * p_dt);

	if (!on_ground()) {
		// Near the top of the arc with jump still held, gravity is halved: the extra
		// hang time is what makes the apex feel controllable.
		float mult = 1.0f;
		const bool near_the_apex = std::fabs(velocity_.y) < config_.gravity.half_threshold;
		if (near_the_apex && jump_held_) {
			mult = 0.5f;
		}
		velocity_.y = approach(velocity_.y, max_fall_, config_.gravity.accel * mult * p_dt);
	}

	// Variable jump height: hold the launch speed while jump is held and drop it the
	// instant it is released. This, not a cut multiplier, is where jump height comes
	// from -- which is also why the same rule shortens a wall jump.
	if (var_jump_timer_ > 0.0f) {
		if (jump_held_) {
			velocity_.y = std::min(velocity_.y, var_jump_speed_);
		} else {
			var_jump_timer_ = 0.0f;
		}
	}
}

void PlayerCore::try_jump() {
	if (jump_buffer_ <= 0.0f) {
		return;
	}
	if (coyote_ > 0.0f) {
		jump();
		return;
	}
	// A wall jump needs room to stand up, otherwise the body would be shoved into the
	// ceiling it is ducked under. Right side checked first, like the reference.
	if (!can_unduck()) {
		return;
	}
	if (wall_direction() == 1) {
		jump_off_wall(1);
	} else if (wall_direction() == -1) {
		jump_off_wall(-1);
	}
}

void PlayerCore::jump_off_wall(int p_wall) {
	// A dash attack that was aimed straight up turns a wall hit into a super wall
	// jump; everything else is a plain wall jump.
	if (dash_attack_timer_ > 0.0f && dash_dir_.x == 0.0f && dash_dir_.y < 0.0f) {
		wall_jump(-p_wall, true);
	} else {
		wall_jump(-p_wall, false);
	}
}

void PlayerCore::jump() {
	consume_jump();
	set_duck(false);
	velocity_.y = -config_.jump.speed;
	velocity_.x += config_.jump.speed_boost * move_x_;
	var_jump_speed_ = velocity_.y;
	set_technique(Technique::kJump);
}

void PlayerCore::super_jump() {
	consume_jump();
	velocity_.x = config_.dash.super_jump_speed * facing_;
	velocity_.y = -config_.jump.speed;
	// Ducking is the entire difference between a Super and a Hyper or an Ultra.
	if (ducking_) {
		set_duck(false);
		velocity_.x *= config_.dash.duck_super_x_mult;
		velocity_.y *= config_.dash.duck_super_y_mult;
		set_technique(dash_started_on_ground_ ? Technique::kHyper : Technique::kUltra);
	} else {
		set_technique(Technique::kSuper);
	}
	var_jump_speed_ = velocity_.y;
	mode_ = Mode::kNormal;
}

void PlayerCore::wall_jump(int p_direction, bool p_super_wall) {
	consume_jump();
	set_duck(false);
	if (p_super_wall) {
		velocity_.x = config_.wall.super_horizontal * p_direction;
		velocity_.y = -config_.wall.super_speed;
	} else {
		velocity_.x = config_.wall.jump_speed * p_direction;
		velocity_.y = -config_.jump.speed;
	}
	var_jump_speed_ = velocity_.y;
	facing_ = static_cast<float>(p_direction);
	// Take over the horizontal input for a moment, otherwise the player is pulled
	// back onto the wall they just pushed off.
	if (move_x_ != 0.0f) {
		force_move_x_ = static_cast<float>(p_direction);
		force_move_x_timer_ = p_super_wall ? config_.wall.super_force_time : config_.wall.jump_force_time;
	}
	set_technique(p_super_wall ? Technique::kSuperWallJump : Technique::kWallJump);
	mode_ = Mode::kNormal;
}

void PlayerCore::consume_jump() {
	jump_buffer_ = 0.0f;
	coyote_ = 0.0f;
	dash_attack_timer_ = 0.0f;
	var_jump_timer_ = config_.jump.var_time;
}

bool PlayerCore::try_start_dash() {
	if (dash_buffer_ <= 0.0f || dash_count_ <= 0 || dash_cooldown_timer_ > 0.0f) {
		return false;
	}
	dash_buffer_ = 0.0f;
	dash_count_ -= 1;
	// Grounded is sampled here, from the geometric probe only -- adding coyote time
	// would misread "ran off a ledge then dashed down" as a Hyper, and would disable
	// the dash corner correction that is reserved for airborne dashes.
	dash_started_on_ground_ = on_ground();
	before_dash_speed_ = velocity_;
	dash_dir_ = Vec2();
	velocity_ = Vec2();
	dash_timer_ = config_.dash.duration;
	dash_cooldown_timer_ = config_.dash.cooldown;
	dash_attack_timer_ = config_.dash.attack_time;
	dash_freeze_ = config_.dash.freeze_time;
	dash_refill_cooldown_timer_ = config_.dash.refill_cooldown;
	mode_ = Mode::kDash;
	if (!on_ground() && can_unduck()) {
		set_duck(false);
	}
	set_technique(Technique::kDash);
	return true;
}

void PlayerCore::dash_update() {
	// DashBegin freezes the game for a few frames with the body parked. Only input
	// keeps running, which is what gives the aim a chance to arrive late.
	if (dash_freeze_ > 0.0f) {
		velocity_ = Vec2();
		return;
	}
	// Direction is sampled after the freeze, so a direction pressed on the same frame
	// as the dash key -- or a frame or two later -- still lands correctly.
	if (dash_dir_.x == 0.0f && dash_dir_.y == 0.0f) {
		launch_dash();
	}
	if (jump_buffer_ <= 0.0f) {
		return;
	}
	if (dash_dir_.y == 0.0f && coyote_ > 0.0f) {
		super_jump();
		return;
	}
	try_dash_wall_jump();
}

bool PlayerCore::try_dash_wall_jump() {
	// A dash that only goes up turns a wall hit into a super wall jump.
	const bool super_wall = dash_dir_.x == 0.0f && dash_dir_.y < 0.0f;
	const int wall = wall_direction();
	if (wall == 0) {
		return false;
	}
	wall_jump(-wall, super_wall);
	return true;
}

void PlayerCore::launch_dash() {
	// The dash writes its velocity exactly once. Recomputing it every frame would
	// erase the dash slide speed-up the moment it is applied.
	dash_dir_ = dash_direction_from_input();
	Vec2 new_speed = dash_dir_ * config_.dash.speed;
	// Same direction and already faster: keep the old horizontal speed, never slow
	// down into a dash. This is what lets a Super be chained out of a run.
	const bool same_direction = signf(before_dash_speed_.x) == signf(new_speed.x);
	const bool already_faster = std::fabs(before_dash_speed_.x) > std::fabs(new_speed.x);
	if (same_direction && already_faster) {
		new_speed.x = before_dash_speed_.x;
	}
	velocity_ = new_speed;
	if (dash_dir_.x != 0.0f) {
		facing_ = signf(dash_dir_.x);
	}
	if (on_ground() && dash_dir_.x != 0.0f && dash_dir_.y > 0.0f && velocity_.y > 0.0f) {
		dash_slide();
	}
}

void PlayerCore::dash_slide() {
	// A down-diagonal dash that reaches the ground becomes a horizontal one and
	// speeds up. The horizontal speed is never cleared, which is why the slide
	// carries through the end of the dash.
	dash_dir_ = Vec2(signf(dash_dir_.x), 0.0f);
	velocity_.y = 0.0f;
	velocity_.x *= config_.dash.dodge_slide_mult;
	set_duck(true);
	set_technique(Technique::kDashSlide);
}

void PlayerCore::finish_dash() {
	// Only a dash that was not heading down rewrites the speed; a down dash that slid
	// already converted its direction to horizontal and keeps its momentum.
	if (dash_dir_.y <= 0.0f) {
		velocity_ = dash_dir_ * config_.dash.end_speed;
	}
	if (velocity_.y < 0.0f) {
		velocity_.y *= config_.dash.end_up_mult;
	}
	mode_ = Mode::kNormal;
	set_technique(Technique::kDashEnd);
}

int PlayerCore::advance(float &p_counter, float p_amount) const {
	// The reference moves in whole pixels and carries the remainder, which keeps the
	// body on the integer lattice. That matters more here than it does there: the
	// probes are exact, so a body resting at x.5 against a wall would read as
	// overlapping it.
	p_counter += p_amount;
	const int steps = static_cast<int>(std::lround(p_counter));
	p_counter -= static_cast<float>(steps);
	return steps;
}

bool PlayerCore::move_x_exact(int p_steps, bool p_body_move) {
	if (p_steps == 0) {
		return false;
	}
	const int direction = p_steps > 0 ? 1 : -1;
	const int count = p_steps > 0 ? p_steps : -p_steps;
	for (int i = 0; i < count; ++i) {
		if (box_is_solid(position_ + Vec2(static_cast<float>(direction), 0.0f))) {
			if (p_body_move) {
				velocity_.x = 0.0f;
			}
			return true;
		}
		position_.x += static_cast<float>(direction);
	}
	return false;
}

bool PlayerCore::move_y_exact(int p_steps, bool p_body_move) {
	if (p_steps == 0) {
		return false;
	}
	const int direction = p_steps > 0 ? 1 : -1;
	const int count = p_steps > 0 ? p_steps : -p_steps;
	for (int i = 0; i < count; ++i) {
		if (box_is_solid(position_ + Vec2(0.0f, static_cast<float>(direction)))) {
			if (p_body_move) {
				velocity_.y = 0.0f;
				if (direction < 0) {
					on_ceiling_ = true;
				}
			}
			return true;
		}
		position_.y += static_cast<float>(direction);
	}
	return false;
}

void PlayerCore::move(float p_dt) {
	// X then Y, one pixel at a time, stopping at the first blocked pixel: the
	// reference's MoveH / MoveV.
	move_x_exact(advance(counter_x_, velocity_.x * p_dt), true);
	move_y_exact(advance(counter_y_, velocity_.y * p_dt), true);
}

void PlayerCore::resolve_collisions() {
	const bool grounded_probe = on_ground();
	const bool landed = grounded_probe && !floor_last_frame_;
	bool corrected = false;

	if (pre_move_velocity_.y < 0.0f && on_ceiling_) {
		upward_corner_correction();
	} else if (landed && pre_move_velocity_.y > 0.0f && mode_ == Mode::kDash) {
		// An airborne dash that clips the corner of a platform slides around it;
		// failing that, hitting the ground mid-dash becomes a dash slide.
		if (!dash_started_on_ground_ && dash_corner_correction()) {
			corrected = true;
		} else if (dash_dir_.x != 0.0f && dash_dir_.y > 0.0f) {
			dash_slide();
		}
	}

	const bool grounded = grounded_probe && !corrected;
	if (grounded) {
		coyote_ = config_.jump.coyote_time;
		// Refilling a dash waits out DashRefillCooldown. Take off inside that window
		// and the dash stays spent until the next landing -- that is extended dash.
		if (dash_refill_cooldown_timer_ <= 0.0f) {
			dash_count_ = config_.dash.max_count;
		}
	}
	if (mode_ == Mode::kDash && dash_timer_ <= 0.0f) {
		finish_dash();
	}
	floor_last_frame_ = grounded;
}

void PlayerCore::upward_corner_correction() {
	// Rising into a ceiling: try shifting sideways by up to a few pixels, one pixel at
	// a time, and take the first spot where the whole box fits one pixel higher. That
	// last part is what lets a jump through a gap exactly body-wide work at all -- a
	// swept test would clip the corners on both sides and always fail.
	int directions[2];
	const int direction_count = correction_directions(directions);
	for (int d = 0; d < direction_count; ++d) {
		for (int i = 1; i <= config_.feel.corner_correction_px; ++i) {
			const Vec2 at = position_ + Vec2(static_cast<float>(directions[d] * i), -1.0f);
			if (box_fits_at(at)) {
				position_ = at;
				velocity_.y = pre_move_velocity_.y;
				set_technique(Technique::kCornerCorrect);
				return;
			}
		}
	}
	// No room anywhere: the jump was cut short by a ceiling, so drop the remaining
	// variable jump window unless it only just started.
	if (var_jump_timer_ < config_.jump.var_time - config_.jump.ceiling_var_jump_grace) {
		var_jump_timer_ = 0.0f;
	}
}

int PlayerCore::correction_directions(int p_out[2]) const {
	// Which ways a corner correction may search, travel direction first: going with
	// the motion avoids shoving the player backwards through a wall they just passed.
	// A player standing still (no horizontal speed at all) may search both ways,
	// which is what the reference's two independent `if`s amount to.
	int count = 0;
	if (pre_move_velocity_.x <= 0.0f) {
		p_out[count] = -1;
		++count;
	}
	if (pre_move_velocity_.x >= 0.0f) {
		p_out[count] = 1;
		++count;
	}
	return count;
}

bool PlayerCore::dash_corner_correction() {
	int directions[2];
	const int direction_count = correction_directions(directions);
	for (int d = 0; d < direction_count; ++d) {
		for (int i = 1; i <= config_.feel.dash_corner_correction_px; ++i) {
			const Vec2 at = position_ + Vec2(static_cast<float>(directions[d] * i), 1.0f);
			if (box_fits_at(at)) {
				position_ = at;
				velocity_ = pre_move_velocity_;
				set_technique(Technique::kDashCorner);
				return true;
			}
		}
	}
	return false;
}

bool PlayerCore::box_is_solid_at(const Vec2 &p_at, float p_height) const {
	const float half_width = config_.body.width * 0.5f;
	return query_->overlaps_box(Vec2(p_at.x - half_width, p_at.y - p_height), Vec2(p_at.x + half_width, p_at.y));
}

bool PlayerCore::box_is_solid(const Vec2 &p_at) const {
	return box_is_solid_at(p_at, body_height());
}

bool PlayerCore::box_fits_at(const Vec2 &p_at) const {
	return !box_is_solid(p_at);
}

bool PlayerCore::test_move(const Vec2 &p_motion) const {
	return box_is_solid(position_ + p_motion);
}

bool PlayerCore::on_ground() const {
	if (velocity_.y < 0.0f) {
		return false;
	}
	// Probe one pixel below the feet rather than asking the engine whether it landed
	// this step. A horizontal dash or a dash slide has no downward motion at all, so a
	// contact flag would read false and the dash refill and coyote time would never
	// come back after a Super or a Hyper.
	return box_is_solid(position_ + Vec2(0.0f, 1.0f));
}

bool PlayerCore::can_unduck() const {
	return can_unduck_at(position_);
}

bool PlayerCore::can_unduck_at(const Vec2 &p_at) const {
	if (!ducking_) {
		return true;
	}
	// Same feet position, standing box: is there headroom?
	return !box_is_solid_at(p_at, config_.body.stand_height);
}

void PlayerCore::set_duck(bool p_ducking) {
	// The feet are the origin, so changing stance never displaces the player: the box
	// simply grows or shrinks upward.
	ducking_ = p_ducking;
}

void PlayerCore::slide_h(float p_amount) {
	// DuckCorrectSlide is authored as a speed; it goes through the same pixel stepper
	// so the position never leaves the lattice the probes rely on.
	move_x_exact(advance(counter_x_, p_amount), false);
}

Vec2 PlayerCore::dash_direction_from_input() const {
	// Raw input, bypassing forceMoveX: a wall jump must not be able to aim a dash.
	const Vec2 direction(raw_move_x_, move_y_);
	return direction != Vec2() ? direction.normalized() : Vec2(facing_, 0.0f);
}

int PlayerCore::wall_direction() const {
	if (test_move(Vec2(config_.wall.check_distance, 0.0f))) {
		return 1;
	}
	if (test_move(Vec2(-config_.wall.check_distance, 0.0f))) {
		return -1;
	}
	return 0;
}

bool PlayerCore::is_attacking() const {
	return dash_attack_timer_ > 0.0f || velocity_.length() >= config_.dash.attack_speed_threshold;
}

} // namespace platformer

#pragma once

#include "collision_query.h"
#include "player_config.h"

namespace platformer {

// The reference implementation's state machine is just an enum plus a switch, and
// the player object is the single owner of the simulation. Kept identical here: no
// node tree, no state objects, exactly one place that writes velocity.
enum class Mode {
	kNormal,
	kDash,
};

// Named techniques, reported so a test or a debug overlay can see which mechanic a
// frame produced instead of guessing it from the velocity.
enum class Technique {
	kNone,
	kJump,
	kSuper,
	kHyper,
	kUltra,
	kWallJump,
	kSuperWallJump,
	kDash,
	kDashSlide,
	kDashEnd,
	kCornerCorrect,
	kDashCorner,
};

const char *technique_name(Technique p_technique);
const char *mode_name(Mode p_mode);

// One fixed step of sampled input. Edge flags are true only on the step where the
// button went down; held flags mirror the key state.
struct InputFrame {
	float move_x = 0.0f; // -1 / 0 / +1
	float move_y = 0.0f; // -1 up, +1 down
	bool jump_pressed = false;
	bool dash_pressed = false;
	bool jump_held = false;
};

// The whole character controller, with no engine dependency beyond CollisionQuery.
//
// Frame order is fixed and matches the GDScript version this was ported from:
//
//   1. sample input          (buffers, forceMoveX)
//   2. tick timers           (freeze gates everything below it)
//   3. update facing
//   4. mode update           (movement / gravity / jump decisions)
//   5. move                  (one pass, one pixel at a time)
//   6. resolve collisions    (landing, corner correction, dash slide)
//
// Nothing outside step 5 touches position, and nothing outside step 4 touches
// velocity, which is what makes the derived techniques reproducible.
class PlayerCore {
public:
	PlayerCore(const PlayerConfig &p_config, CollisionQuery *p_query);

	// Places the player and restores the state it starts a life with.
	void reset(const Vec2 &p_position, float p_facing = 1.0f);

	// Tuning is owned by value, so a caller can reload it at runtime without
	// worrying about who keeps the old one alive.
	const PlayerConfig &config() const { return config_; }
	void set_config(const PlayerConfig &p_config) { config_ = p_config; }

	// Event-driven capture. Call these from the input callback so a press shorter
	// than one physics step is never dropped; InputFrame::*_pressed feeds the same
	// buffers, so a headless driver works without synthesising events.
	void press_jump();
	void press_dash();

	// Advances the simulation by one fixed step.
	void step(const InputFrame &p_input, float p_dt);

	Vec2 position() const { return position_; }
	Vec2 velocity() const { return velocity_; }
	Mode mode() const { return mode_; }
	float facing() const { return facing_; }
	bool ducking() const { return ducking_; }
	int dash_count() const { return dash_count_; }
	Vec2 dash_dir() const { return dash_dir_; }
	bool dash_started_on_ground() const { return dash_started_on_ground_; }
	Technique last_technique() const { return last_technique_; }

	// Aim of a dash started this frame: raw input, falling back to facing. Raw on
	// purpose -- forceMoveX must not steer a dash.
	Vec2 dash_direction_from_input() const;

	// Wall on either side within WallJumpCheckDist, right side first. 0 if none.
	int wall_direction() const;

	// Break/damage gate: a dash attack window is open, or the body is fast enough.
	bool is_attacking() const;

	// Would shifting the whole body by this vector put it inside solid? The
	// reference's CollideCheck<Solid>(Position + offset), and the probe the wall
	// rules are written in terms of -- a debug overlay wants it too.
	bool test_move(const Vec2 &p_motion) const;

private:
	// Frame stages, in the order listed above.
	void poll_input(const InputFrame &p_input, float p_dt);
	void tick_timers(float p_dt);
	void update_facing();
	void normal_update(float p_dt);
	void dash_update();
	void move(float p_dt);
	void resolve_collisions();

	// Mode updates.
	void update_ducking(float p_dt);
	void apply_horizontal(float p_dt);
	void apply_gravity(float p_dt);
	void try_jump();
	void jump_off_wall(int p_wall);
	bool try_dash_wall_jump();
	bool try_start_dash();
	void launch_dash();
	void finish_dash();
	void dash_slide();
	void upward_corner_correction();
	bool dash_corner_correction();

	// Fills p_out with the directions a corner correction may search, travel
	// direction first, and returns how many were written (1 or 2).
	int correction_directions(int p_out[2]) const;

	// Jumps.
	void jump();
	void super_jump();
	void wall_jump(int p_direction, bool p_super_wall);
	void consume_jump();

	// Queries against the injected world. All of them take the body origin -- the
	// bottom centre of the box -- and are exact: no margin, no shape mutation.
	bool box_is_solid_at(const Vec2 &p_at, float p_height) const;
	bool box_is_solid(const Vec2 &p_at) const;
	bool box_fits_at(const Vec2 &p_at) const;

	// Ground contact is a geometric probe, not the engine's "did I land this step"
	// flag: a horizontal dash or a dash slide has no downward motion at all, so a
	// contact flag reads false and dash count and coyote time would never be refilled.
	bool on_ground() const;
	bool can_unduck() const;
	bool can_unduck_at(const Vec2 &p_at) const;

	// Movement. Positions stay on the integer lattice: the leftover of every move is
	// carried in an accumulator and consumed one whole pixel at a time, which is what
	// the reference does (MoveHExact / MoveVExact) and what makes exact geometry
	// sufficient for the probes.
	int advance(float &p_counter, float p_amount) const;
	bool move_x_exact(int p_steps, bool p_body_move);
	bool move_y_exact(int p_steps, bool p_body_move);
	void slide_h(float p_amount);

	void set_duck(bool p_ducking);
	float body_height() const { return config_.body_height(ducking_); }
	void set_technique(Technique p_technique) { last_technique_ = p_technique; }

	PlayerConfig config_;
	CollisionQuery *query_;

	// --- simulation state ---
	Vec2 position_;
	Vec2 velocity_;
	Vec2 pre_move_velocity_; // velocity as it was before the move, for the corrections
	float counter_x_ = 0.0f;
	float counter_y_ = 0.0f;
	Mode mode_ = Mode::kNormal;
	int dash_count_ = 1;
	float facing_ = 1.0f;
	bool ducking_ = false;

	// --- sampled input ---
	float raw_move_x_ = 0.0f;
	float move_x_ = 0.0f; // raw_move_x_ after the forceMoveX override
	float move_y_ = 0.0f;
	bool jump_held_ = false;
	float force_move_x_ = 0.0f;
	float force_move_x_timer_ = 0.0f;

	// --- timers ---
	float jump_buffer_ = 0.0f;
	float dash_buffer_ = 0.0f;
	float dash_freeze_ = 0.0f;
	float dash_timer_ = 0.0f;
	float dash_cooldown_timer_ = 0.0f;
	float dash_refill_cooldown_timer_ = 0.0f;
	float dash_attack_timer_ = 0.0f;
	float coyote_ = 0.0f;
	float var_jump_timer_ = 0.0f;
	float var_jump_speed_ = 0.0f;
	float max_fall_ = 160.0f; // current cap, approaches FastMaxFall while down is held

	// --- dash state ---
	Vec2 dash_dir_; // (0,0) until the freeze ends, then sampled and never recomputed
	bool dash_started_on_ground_ = false;
	Vec2 before_dash_speed_;

	// --- contact facts of the last move ---
	bool on_ceiling_ = false;
	bool floor_last_frame_ = false;

	Technique last_technique_ = Technique::kNone;
};

} // namespace platformer

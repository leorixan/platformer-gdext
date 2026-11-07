#pragma once

namespace platformer {

// Every tunable of the controller, grouped by the subsystem that reads it. The
// values are the reference implementation's own numbers, unscaled: 1 brick = 8px,
// standing box 8x11, ducking box 8x6. The trailing comment on each field is the
// name of the constant it was taken from, so a value can always be traced back to
// the behaviour it came from.
struct PlayerConfig {
	struct Body {
		float width = 8.0f; // normalHitbox width
		float stand_height = 11.0f; // normalHitbox height
		float duck_height = 6.0f; // duckHitbox height
	} body;

	struct Movement {
		float max_speed = 90.0f; // MaxRun
		float acceleration = 1000.0f; // RunAccel
		float over_speed_decel = 400.0f; // RunReduce: decay while above MaxRun and still pushing
		float air_accel_mult = 0.65f; // AirMult
		float duck_friction = 500.0f; // DuckFriction
		int duck_correct_check = 4; // DuckCorrectCheck: sideways probe range, px
		float duck_correct_slide = 50.0f; // DuckCorrectSlide: px/s
	} movement;

	struct Jump {
		float speed = 105.0f; // JumpSpeed
		float speed_boost = 40.0f; // JumpHBoost
		float var_time = 0.20f; // VarJumpTime: how long holding jump keeps the launch speed
		float coyote_time = 0.10f; // JumpGraceTime
		float buffer_time = 0.12f; // jump input buffer (reference: 0.08)
		float ceiling_var_jump_grace = 0.05f; // CeilingVarJumpGrace
	} jump;

	struct Gravity {
		float accel = 900.0f; // gravity
		float max_fall = 160.0f; // MaxFall
		float fast_max_fall = 240.0f; // FastMaxFall: cap while holding down
		float fast_max_accel = 300.0f; // FastMaxAccel: how fast the cap itself moves
		float half_threshold = 40.0f; // HalfGravThreshold: apex hang while holding jump
	} gravity;

	struct Dash {
		int max_count = 1;
		float speed = 240.0f; // DashSpeed
		float end_speed = 160.0f; // EndDashSpeed, only written when the aim is not downward
		float end_up_mult = 0.75f; // EndDashUpMult
		float duration = 0.15f; // DashTime
		float freeze_time = 0.05f; // Celeste.Freeze in DashBegin; also the Super/Hyper input window
		float cooldown = 0.20f; // DashCooldown
		float refill_cooldown = 0.10f; // DashRefillCooldown: source of extended dash
		float attack_time = 0.30f; // DashAttackTime
		float buffer_time = 0.12f; // dash input buffer
		float super_jump_speed = 260.0f; // SuperJumpH
		float dodge_slide_mult = 1.2f; // DodgeSlideSpeedMult
		float duck_super_x_mult = 1.25f; // DuckSuperJumpXMult
		float duck_super_y_mult = 0.5f; // DuckSuperJumpYMult
		float attack_speed_threshold = 240.0f; // is_attacking() speed gate
	} dash;

	struct Wall {
		float jump_speed = 130.0f; // WallJumpHSpeed = MaxRun + JumpHBoost
		float check_distance = 3.0f; // WallJumpCheckDist
		float jump_force_time = 0.16f; // WallJumpForceTime
		float super_speed = 160.0f; // SuperWallJumpSpeed
		float super_horizontal = 170.0f; // SuperWallJumpH = MaxRun + JumpHBoost * 2
		float super_force_time = 0.20f; // SuperWallJumpForceTime
	} wall;

	struct Feel {
		int corner_correction_px = 4; // UpwardCornerCorrection
		int dash_corner_correction_px = 4; // DashCornerCorrection
	} feel;

	// Height of the body box for the current stance. The feet are the origin, so
	// switching stance never displaces the player.
	float body_height(bool p_ducking) const {
		return p_ducking ? body.duck_height : body.stand_height;
	}
};

} // namespace platformer

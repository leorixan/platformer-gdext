#include "player_body_2d.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace platformer {

namespace {

// The simulation runs at a fixed 60Hz and never at the render rate. Everything in
// the reference implementation is expressed in frames -- a 3-frame dash freeze, a
// 12-frame wallboost window -- so the step size is part of the behaviour, not a
// performance knob.
const float kFixedStep = 1.0f / 60.0f;

// If a frame took so long that the simulation would need more than this many steps
// to catch up, the backlog is dropped rather than simulated. Otherwise one hitch
// turns into a burst of steps, which turns into the next hitch.
const int kMaxStepsPerFrame = 5;

// Where the parameter sheet lives. docs/ holds the published copy; the one shipped
// next to the extension is the fallback, so a host project that copies only the
// addon folder still gets a working default.
const char *kPrimaryParamsPath = "res://docs/player_params.csv";
const char *kFallbackParamsPath = "res://addons/platformer_core/player_params.csv";

// The body box is drawn in this colour when debug_draw is on.
const Color kBodyColor(0.35f, 0.85f, 1.0f, 0.9f);
const Color kDuckColor(1.0f, 0.75f, 0.25f, 0.9f);

} // namespace

PlayerBody2D::PlayerBody2D() {
	core_ = nullptr;
	params_path_ = String(kPrimaryParamsPath);
	solid_mask_ = 1;
	debug_draw_ = false;
	accumulator_ = 0.0f;
}

PlayerBody2D::~PlayerBody2D() {
	if (core_ != nullptr) {
		delete core_;
		core_ = nullptr;
	}
}

void PlayerBody2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_params_path", "path"), &PlayerBody2D::set_params_path);
	ClassDB::bind_method(D_METHOD("get_params_path"), &PlayerBody2D::get_params_path);
	ClassDB::bind_method(D_METHOD("reload_params"), &PlayerBody2D::reload_params);
	ClassDB::bind_method(D_METHOD("set_solid_mask", "mask"), &PlayerBody2D::set_solid_mask);
	ClassDB::bind_method(D_METHOD("get_solid_mask"), &PlayerBody2D::get_solid_mask);
	ClassDB::bind_method(D_METHOD("set_debug_draw", "enabled"), &PlayerBody2D::set_debug_draw);
	ClassDB::bind_method(D_METHOD("get_debug_draw"), &PlayerBody2D::get_debug_draw);

	ClassDB::bind_method(D_METHOD("teleport_to", "position"), &PlayerBody2D::teleport_to);
	ClassDB::bind_method(D_METHOD("respawn"), &PlayerBody2D::respawn);

	ClassDB::bind_method(D_METHOD("get_technique"), &PlayerBody2D::get_technique);
	ClassDB::bind_method(D_METHOD("get_state"), &PlayerBody2D::get_state);
	ClassDB::bind_method(D_METHOD("get_core_position"), &PlayerBody2D::get_core_position);
	ClassDB::bind_method(D_METHOD("get_core_velocity"), &PlayerBody2D::get_core_velocity);
	ClassDB::bind_method(D_METHOD("get_facing"), &PlayerBody2D::get_facing);
	ClassDB::bind_method(D_METHOD("is_ducking"), &PlayerBody2D::is_ducking);
	ClassDB::bind_method(D_METHOD("get_dash_count"), &PlayerBody2D::get_dash_count);
	ClassDB::bind_method(D_METHOD("is_on_wall"), &PlayerBody2D::is_on_wall);
	ClassDB::bind_method(D_METHOD("get_wall_direction"), &PlayerBody2D::get_wall_direction);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "params_path", PROPERTY_HINT_FILE, "*.csv"),
			"set_params_path", "get_params_path");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "solid_mask"), "set_solid_mask", "get_solid_mask");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_draw"), "set_debug_draw", "get_debug_draw");
}

void PlayerBody2D::_ready() {
	spawn_position_ = get_global_position();

	query_.set_body(get_rid());
	query_.set_collision_mask(solid_mask_);

	PlayerConfig config;
	load_config_file(config, pick_params_file());

	core_ = new PlayerCore(config, &query_);
	core_->reset(Vec2(spawn_position_.x, spawn_position_.y));
}

void PlayerBody2D::_physics_process(double p_delta) {
	if (core_ == nullptr) {
		return;
	}
	update_space_query();

	// Input is sampled once per physics frame, then replayed to every step that
	// frame has to run. Only the press flags are cleared after the first step: a
	// press lasts one step, while holding a direction lasts the whole frame.
	InputFrame frame;
	read_input(frame);

	accumulator_ += (float)p_delta;
	int steps = (int)(accumulator_ / kFixedStep);
	if (steps > kMaxStepsPerFrame) {
		steps = kMaxStepsPerFrame;
		accumulator_ = 0.0f;
	} else {
		accumulator_ -= (float)steps * kFixedStep;
	}

	for (int i = 0; i < steps; ++i) {
		core_->step(frame, kFixedStep);
		frame.jump_pressed = false;
		frame.dash_pressed = false;
	}

	push_transform_to_node();
}

void PlayerBody2D::_unhandled_input(const Ref<InputEvent> &p_event) {
	if (core_ == nullptr || p_event.is_null()) {
		return;
	}
	// Presses are buffered here rather than only polled, so a tap shorter than one
	// physics step is still seen. Polling alone would drop it.
	if (p_event->is_action_pressed("jump", false)) {
		core_->press_jump();
	} else if (p_event->is_action_pressed("dash", false)) {
		core_->press_dash();
	} else if (p_event->is_action_pressed("reload_config", false)) {
		reload_params();
	}
}

void PlayerBody2D::_draw() {
	if (core_ == nullptr || !debug_draw_) {
		return;
	}

	const PlayerConfig &config = core_->config();
	const float half_width = config.body.width * 0.5f;
	const float height = config.body_height(core_->ducking());

	Color colour = kBodyColor;
	if (core_->ducking()) {
		colour = kDuckColor;
	}

	// Local space: the node origin is the feet, so the box reaches upwards.
	const Rect2 box(-half_width, -height, config.body.width, height);
	draw_rect(box, colour, true);
}

void PlayerBody2D::update_space_query() {
	const Ref<World2D> world = get_world_2d();
	if (world.is_valid()) {
		query_.set_space(world->get_direct_space_state());
	} else {
		query_.set_space(nullptr);
	}
}

void PlayerBody2D::read_input(InputFrame &r_frame) const {
	Input *input = Input::get_singleton();
	if (input == nullptr) {
		return;
	}

	// The axis is read as a sign, exactly like the GDScript version: the reference
	// treats the stick as a three-way switch, so a half-tilted stick is full speed.
	const float horizontal = input->get_axis("move_left", "move_right");
	r_frame.move_x = 0.0f;
	if (horizontal > 0.0f) {
		r_frame.move_x = 1.0f;
	} else if (horizontal < 0.0f) {
		r_frame.move_x = -1.0f;
	}

	const float vertical = input->get_axis("move_up", "move_down");
	r_frame.move_y = 0.0f;
	if (vertical > 0.0f) {
		r_frame.move_y = 1.0f;
	} else if (vertical < 0.0f) {
		r_frame.move_y = -1.0f;
	}

	r_frame.jump_pressed = input->is_action_just_pressed("jump");
	r_frame.dash_pressed = input->is_action_just_pressed("dash");
	r_frame.jump_held = input->is_action_pressed("jump");
}

void PlayerBody2D::push_transform_to_node() {
	set_global_position(Vector2(core_->position().x, core_->position().y));
	if (debug_draw_) {
		queue_redraw();
	}
}

// --- tuning -----------------------------------------------------------------

void PlayerBody2D::set_params_path(const String &p_path) {
	params_path_ = p_path;
}

String PlayerBody2D::get_params_path() const {
	return params_path_;
}

void PlayerBody2D::reload_params() {
	if (core_ == nullptr) {
		return;
	}

	// Loaded into a copy first, so a broken file cannot leave the player half
	// tuned. The copy is only handed over once the read has succeeded.
	PlayerConfig config = core_->config();
	if (load_config_file(config, pick_params_file())) {
		core_->set_config(config);
	}
}

void PlayerBody2D::set_solid_mask(int p_mask) {
	solid_mask_ = (uint32_t)p_mask;
	query_.set_collision_mask(solid_mask_);
}

int PlayerBody2D::get_solid_mask() const {
	return (int)solid_mask_;
}

void PlayerBody2D::set_debug_draw(bool p_enabled) {
	debug_draw_ = p_enabled;
	queue_redraw();
}

bool PlayerBody2D::get_debug_draw() const {
	return debug_draw_;
}

String PlayerBody2D::pick_params_file() const {
	if (FileAccess::file_exists(params_path_)) {
		return params_path_;
	}
	const String fallback(kFallbackParamsPath);
	if (FileAccess::file_exists(fallback)) {
		return fallback;
	}
	return params_path_;
}

int PlayerBody2D::to_int(float p_value) {
	// Every integer parameter in the sheet is a small positive count, so a plain
	// round-half-up is all that is needed.
	return (int)(p_value + 0.5f);
}

bool PlayerBody2D::load_config_file(PlayerConfig &r_config, const String &p_path) const {
	if (!FileAccess::file_exists(p_path)) {
		WARN_PRINT("PlayerBody2D: parameter sheet not found at " + p_path);
		return false;
	}

	const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		WARN_PRINT("PlayerBody2D: could not open " + p_path);
		return false;
	}

	file->get_csv_line(); // header: category,name,value,type,description

	int unknown = 0;
	while (!file->eof_reached()) {
		const PackedStringArray line = file->get_csv_line();
		if (line.size() < 3) {
			continue;
		}

		const String category = line[0].strip_edges();
		const String name = line[1].strip_edges();
		if (category.is_empty() || category.begins_with("#")) {
			continue;
		}

		const float value = line[2].strip_edges().to_float();
		bool matched = false;
		if (category == "Body") {
			matched = apply_body_param(r_config, name, value);
		} else if (category == "Movement") {
			matched = apply_movement_param(r_config, name, value);
		} else if (category == "Jump") {
			matched = apply_jump_param(r_config, name, value);
		} else if (category == "Gravity") {
			matched = apply_gravity_param(r_config, name, value);
		} else if (category == "Dash") {
			matched = apply_dash_param(r_config, name, value);
		} else if (category == "Wall" || category == "Climb") {
			// The wall-jump values sit under the Climb heading in the parameter sheet.
			// The rows for climbing itself have nowhere left to go and are reported.
			matched = apply_wall_param(r_config, name, value);
		} else if (category == "Feel") {
			matched = apply_feel_param(r_config, name, value);
		} else {
			// Rows for features the C++ controller does not implement (carrying an
			// item, for one) are skipped on purpose rather than treated as an error.
			continue;
		}

		if (!matched) {
			++unknown;
			WARN_PRINT("PlayerBody2D: unknown parameter " + category + "/" + name);
		}
	}
	file->close();

	// Unreadable rows are reported but not fatal: the sheet and this loader are kept
	// in step by hand, and one stale row should not stop the player from spawning.
	return true;
}

bool PlayerBody2D::apply_body_param(PlayerConfig &r_config, const String &p_name, float p_value) {
	if (p_name == "body_width") {
		r_config.body.width = p_value;
	} else if (p_name == "stand_height") {
		r_config.body.stand_height = p_value;
	} else if (p_name == "duck_height") {
		r_config.body.duck_height = p_value;
	} else {
		return false;
	}
	return true;
}

bool PlayerBody2D::apply_movement_param(PlayerConfig &r_config, const String &p_name, float p_value) {
	if (p_name == "max_speed") {
		r_config.movement.max_speed = p_value;
	} else if (p_name == "acceleration") {
		r_config.movement.acceleration = p_value;
	} else if (p_name == "over_speed_decel") {
		r_config.movement.over_speed_decel = p_value;
	} else if (p_name == "air_accel_mult") {
		r_config.movement.air_accel_mult = p_value;
	} else if (p_name == "duck_friction") {
		r_config.movement.duck_friction = p_value;
	} else if (p_name == "duck_correct_check") {
		r_config.movement.duck_correct_check = to_int(p_value);
	} else if (p_name == "duck_correct_slide") {
		r_config.movement.duck_correct_slide = p_value;
	} else {
		return false;
	}
	return true;
}

bool PlayerBody2D::apply_jump_param(PlayerConfig &r_config, const String &p_name, float p_value) {
	if (p_name == "jump_speed") {
		r_config.jump.speed = p_value;
	} else if (p_name == "jump_speed_boost") {
		r_config.jump.speed_boost = p_value;
	} else if (p_name == "var_jump_time") {
		r_config.jump.var_time = p_value;
	} else if (p_name == "coyote_time") {
		r_config.jump.coyote_time = p_value;
	} else if (p_name == "jump_buffer_time") {
		r_config.jump.buffer_time = p_value;
	} else if (p_name == "ceiling_var_jump_grace") {
		r_config.jump.ceiling_var_jump_grace = p_value;
	} else {
		return false;
	}
	return true;
}

bool PlayerBody2D::apply_gravity_param(PlayerConfig &r_config, const String &p_name, float p_value) {
	if (p_name == "gravity") {
		r_config.gravity.accel = p_value;
	} else if (p_name == "max_fall_speed") {
		r_config.gravity.max_fall = p_value;
	} else if (p_name == "fast_max_fall_speed") {
		r_config.gravity.fast_max_fall = p_value;
	} else if (p_name == "fast_max_accel") {
		r_config.gravity.fast_max_accel = p_value;
	} else if (p_name == "half_gravity_threshold") {
		r_config.gravity.half_threshold = p_value;
	} else {
		return false;
	}
	return true;
}

bool PlayerBody2D::apply_dash_param(PlayerConfig &r_config, const String &p_name, float p_value) {
	if (p_name == "max_dashes") {
		r_config.dash.max_count = to_int(p_value);
	} else if (p_name == "dash_speed") {
		r_config.dash.speed = p_value;
	} else if (p_name == "dash_end_speed") {
		r_config.dash.end_speed = p_value;
	} else if (p_name == "end_dash_up_mult") {
		r_config.dash.end_up_mult = p_value;
	} else if (p_name == "dash_duration") {
		r_config.dash.duration = p_value;
	} else if (p_name == "dash_freeze_time") {
		r_config.dash.freeze_time = p_value;
	} else if (p_name == "dash_cooldown") {
		r_config.dash.cooldown = p_value;
	} else if (p_name == "dash_refill_cooldown") {
		r_config.dash.refill_cooldown = p_value;
	} else if (p_name == "dash_attack_time") {
		r_config.dash.attack_time = p_value;
	} else if (p_name == "dash_buffer_time") {
		r_config.dash.buffer_time = p_value;
	} else if (p_name == "super_jump_speed") {
		r_config.dash.super_jump_speed = p_value;
	} else if (p_name == "dodge_slide_speed_mult") {
		r_config.dash.dodge_slide_mult = p_value;
	} else if (p_name == "duck_super_jump_x_mult") {
		r_config.dash.duck_super_x_mult = p_value;
	} else if (p_name == "duck_super_jump_y_mult") {
		r_config.dash.duck_super_y_mult = p_value;
	} else {
		return false;
	}
	return true;
}

bool PlayerBody2D::apply_wall_param(PlayerConfig &r_config, const String &p_name, float p_value) {
	if (p_name == "wall_check_distance") {
		r_config.wall.check_distance = p_value;
	} else if (p_name == "wall_jump_speed") {
		r_config.wall.jump_speed = p_value;
	} else if (p_name == "wall_jump_force_time") {
		r_config.wall.jump_force_time = p_value;
	} else if (p_name == "super_wall_jump_speed") {
		r_config.wall.super_speed = p_value;
	} else if (p_name == "super_wall_jump_horizontal") {
		r_config.wall.super_horizontal = p_value;
	} else if (p_name == "super_wall_jump_force_time") {
		r_config.wall.super_force_time = p_value;
	} else {
		return false;
	}
	return true;
}

bool PlayerBody2D::apply_feel_param(PlayerConfig &r_config, const String &p_name, float p_value) {
	if (p_name == "corner_correction_px") {
		r_config.feel.corner_correction_px = to_int(p_value);
	} else if (p_name == "dash_corner_correction_px") {
		r_config.feel.dash_corner_correction_px = to_int(p_value);
	} else {
		return false;
	}
	return true;
}

// --- readouts ---------------------------------------------------------------

String PlayerBody2D::get_technique() const {
	if (core_ == nullptr) {
		return String("none");
	}
	return String(technique_name(core_->last_technique()));
}

String PlayerBody2D::get_state() const {
	if (core_ == nullptr) {
		return String("none");
	}
	return String(mode_name(core_->mode()));
}

Vector2 PlayerBody2D::get_core_position() const {
	if (core_ == nullptr) {
		return Vector2();
	}
	return Vector2(core_->position().x, core_->position().y);
}

Vector2 PlayerBody2D::get_core_velocity() const {
	if (core_ == nullptr) {
		return Vector2();
	}
	return Vector2(core_->velocity().x, core_->velocity().y);
}

float PlayerBody2D::get_facing() const {
	if (core_ == nullptr) {
		return 1.0f;
	}
	return core_->facing();
}

bool PlayerBody2D::is_ducking() const {
	if (core_ == nullptr) {
		return false;
	}
	return core_->ducking();
}

int PlayerBody2D::get_dash_count() const {
	if (core_ == nullptr) {
		return 0;
	}
	return core_->dash_count();
}

bool PlayerBody2D::is_on_wall() const {
	if (core_ == nullptr) {
		return false;
	}
	return core_->wall_direction() != 0;
}

int PlayerBody2D::get_wall_direction() const {
	if (core_ == nullptr) {
		return 0;
	}
	return core_->wall_direction();
}

// --- placement --------------------------------------------------------------

void PlayerBody2D::teleport_to(const Vector2 &p_position) {
	if (core_ == nullptr) {
		return;
	}
	core_->reset(Vec2(p_position.x, p_position.y));
	push_transform_to_node();
}

void PlayerBody2D::respawn() {
	teleport_to(spawn_position_);
}

} // namespace platformer

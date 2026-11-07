#pragma once

#include <godot_cpp/classes/character_body_2d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "player_config.h"
#include "player_core.h"
#include "space_query.h"

namespace platformer {

// The thin adapter between Godot and PlayerCore. It owns three things and nothing
// else: a SpaceQuery, a PlayerCore, and the fixed-step accumulator that decides how
// many times per frame the core is stepped.
//
// It deliberately does not use move_and_slide. The core moves the body itself, one
// whole pixel at a time, and this node only publishes the result to
// global_position. Godot's own collision resolution would fight the core's exact
// geometry, and the whole point of the port is that the simulation is the same one
// the unit tests run.
//
// The node's origin is the bottom centre of the body box -- the feet -- which is
// the same origin the core uses for its position, so global_position maps straight
// across with no offset.
class PlayerBody2D : public godot::CharacterBody2D {
	GDCLASS(PlayerBody2D, godot::CharacterBody2D)

protected:
	static void _bind_methods();

public:
	PlayerBody2D();
	~PlayerBody2D() override;

	void _ready() override;
	void _physics_process(double p_delta) override;
	void _unhandled_input(const godot::Ref<godot::InputEvent> &p_event) override;
	void _draw() override;

	// --- tuning -----------------------------------------------------------
	void set_params_path(const godot::String &p_path);
	godot::String get_params_path() const;

	// Re-reads the CSV and hands the result to the core. Safe to call at any time;
	// a missing or unreadable file leaves the current tuning untouched.
	void reload_params();

	// Which physics layers the core treats as solid.
	void set_solid_mask(int p_mask);
	int get_solid_mask() const;

	void set_debug_draw(bool p_enabled);
	bool get_debug_draw() const;

	// --- runtime readouts, for a debug label or a GDScript harness --------
	godot::String get_technique() const;
	godot::String get_state() const;
	godot::Vector2 get_core_position() const;
	godot::Vector2 get_core_velocity() const;
	float get_facing() const;
	bool is_ducking() const;
	int get_dash_count() const;
	bool is_on_wall() const;
	int get_wall_direction() const;

	// Moves the player without touching the simulation state, and forgets the old
	// spot: respawn() puts the player back where the node sat in the editor.
	void teleport_to(const godot::Vector2 &p_position);
	void respawn();

private:
	void update_space_query();
	void read_input(InputFrame &r_frame) const;
	void push_transform_to_node();

	// CSV loading. The file is the same one the GDScript ConfigLoader reads, so the
	// two implementations are tuned from one source.
	godot::String pick_params_file() const;
	bool load_config_file(PlayerConfig &r_config, const godot::String &p_path) const;
	static int to_int(float p_value);
	static bool apply_body_param(PlayerConfig &r_config, const godot::String &p_name, float p_value);
	static bool apply_movement_param(PlayerConfig &r_config, const godot::String &p_name, float p_value);
	static bool apply_jump_param(PlayerConfig &r_config, const godot::String &p_name, float p_value);
	static bool apply_gravity_param(PlayerConfig &r_config, const godot::String &p_name, float p_value);
	static bool apply_dash_param(PlayerConfig &r_config, const godot::String &p_name, float p_value);
	static bool apply_wall_param(PlayerConfig &r_config, const godot::String &p_name, float p_value);
	static bool apply_feel_param(PlayerConfig &r_config, const godot::String &p_name, float p_value);

	SpaceQuery query_;
	PlayerCore *core_;

	godot::String params_path_;
	godot::Vector2 spawn_position_;
	uint32_t solid_mask_;
	bool debug_draw_;

	// Leftover time between physics frames. The simulation only ever advances in
	// whole 1/60 steps, so rendering at 144Hz and rendering at 30Hz produce the
	// same trajectory.
	float accumulator_;
};

} // namespace platformer

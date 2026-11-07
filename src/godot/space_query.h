#pragma once

#include <godot_cpp/classes/physics_direct_space_state2d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters2d.hpp>
#include <godot_cpp/classes/rectangle_shape2d.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "collision_query.h"

namespace platformer {

// The engine side of CollisionQuery: the same probe the tests run against the tile
// grid, answered here by Godot's PhysicsDirectSpaceState2D.
//
// Everything the core knows about the world goes through this class, which is what
// keeps src/core/ free of engine headers: the simulation never sees a Node, a
// Shape2D or a Dictionary, only "is this box solid".
//
// The probe is a RectangleShape2D query, shrunk by kContactEpsilon on every side so
// that the engine's answer agrees with the interface's exact-edge convention. It does
// not use a collision shape attached to the player -- the player body has no collider
// at all, because the core decides where the body ends up instead of asking
// move_and_slide. The body's own RID is put in the exclude list so a stray shape on
// the node could never block the player.
class SpaceQuery : public CollisionQuery {
public:
	SpaceQuery();
	~SpaceQuery() override;

	// The state to ask. Nullable on purpose: before _ready(), or in a headless
	// context with no world, every probe answers false instead of crashing.
	void set_space(godot::PhysicsDirectSpaceState2D *p_space);

	// The body to leave out of every query, by RID.
	void set_body(const godot::RID &p_body);

	// Which physics layers count as solid. One layer by default.
	void set_collision_mask(uint32_t p_mask);

	bool overlaps_box(const Vec2 &p_min, const Vec2 &p_max) const override;

private:
	godot::PhysicsDirectSpaceState2D *space_;

	godot::Ref<godot::RectangleShape2D> shape_;
	godot::Ref<godot::PhysicsShapeQueryParameters2D> box_params_;

	godot::TypedArray<godot::RID> exclude_;
	uint32_t collision_mask_;
};

} // namespace platformer

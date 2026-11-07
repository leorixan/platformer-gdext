#include "space_query.h"

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/vector2.hpp>

using namespace godot;

namespace platformer {

SpaceQuery::SpaceQuery() {
	space_ = nullptr;
	collision_mask_ = 1;

	// One shape, resized on every probe. Allocating a RectangleShape2D per query
	// would be a heap allocation several times a frame -- the corner correction
	// alone can run a dozen probes in a single step.
	shape_.instantiate();
	shape_->set_size(Vector2(8.0f, 8.0f));

	box_params_.instantiate();
	box_params_->set_shape(shape_);
	box_params_->set_collide_with_bodies(true);
	box_params_->set_collide_with_areas(false);
	// Godot's default margin is 0.08px, which would make a box that is a hair away
	// from a wall read as a hit. Zero it: the interface's convention is decided here,
	// not by the engine's idea of a comfortable touching distance.
	box_params_->set_margin(0.0f);
}

SpaceQuery::~SpaceQuery() {
}

void SpaceQuery::set_space(PhysicsDirectSpaceState2D *p_space) {
	space_ = p_space;
}

void SpaceQuery::set_body(const RID &p_body) {
	exclude_.clear();
	if (p_body.is_valid()) {
		exclude_.push_back(p_body);
	}
}

void SpaceQuery::set_collision_mask(uint32_t p_mask) {
	collision_mask_ = p_mask;
}

bool SpaceQuery::overlaps_box(const Vec2 &p_min, const Vec2 &p_max) const {
	if (space_ == nullptr) {
		return false;
	}

	// CollisionQuery counts a shared edge as a miss; a raw shape query counts it as
	// a hit. Shrinking the query box by kContactEpsilon on all four sides turns
	// "the boxes overlap" into "the shrunken boxes overlap", which is precisely the
	// interface convention. At 0.0001px it can never hide real geometry -- a body
	// that is one whole pixel inside a wall still overlaps by 0.9998px.
	const Vector2 min(p_min.x + kContactEpsilon, p_min.y + kContactEpsilon);
	const Vector2 max(p_max.x - kContactEpsilon, p_max.y - kContactEpsilon);
	if (max.x <= min.x || max.y <= min.y) {
		return false;
	}

	// A shape query is centred on its transform origin, so the box is described by
	// its centre plus its size.
	const Vector2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
	const Vector2 size(max.x - min.x, max.y - min.y);

	shape_->set_size(size);
	box_params_->set_transform(Transform2D(0.0f, centre));
	box_params_->set_exclude(exclude_);
	box_params_->set_collision_mask(collision_mask_);

	// One result is enough: the only question asked is whether anything is there.
	const TypedArray<Dictionary> hits = space_->intersect_shape(box_params_, 1);
	return hits.size() > 0;
}

} // namespace platformer

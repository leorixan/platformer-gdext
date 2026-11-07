#pragma once

#include "math2d.h"

namespace platformer {

// Everything the simulation is allowed to know about the world. It is the only
// engine-facing dependency of the core, and it is inverted: the core calls the
// engine (Godot's physics server) and the test binary (a plain tile grid) through
// the exact same function, so one simulation serves both.
//
// Contract: `overlaps_box` returns true if and only if the two boxes share a
// *positive* area, measured in pixels. Touching edges do not count as a hit. This is
// the whole reason the port dropped the two epsilon patches the GDScript version had
// accumulated: there, an 8px-wide body could not pass an 8px gap because the engine
// rejects zero-clearance motion, so the collider had to be shrunk before every query
// and wall snapping needed a margin of its own. Here the convention is stated once,
// in the interface, and every implementation honours it; the collider is never
// mutated, and a body fits through a gap exactly its own width.
//
// Implementations must agree with it to within kContactEpsilon of the boundary.
constexpr float kContactEpsilon = 1e-4f;

class CollisionQuery {
public:
	virtual ~CollisionQuery() = default;

	// Solid material overlapping the axis-aligned box [p_min, p_max]?
	virtual bool overlaps_box(const Vec2 &p_min, const Vec2 &p_max) const = 0;
};

} // namespace platformer

#pragma once

#include <string>
#include <vector>

#include "player_core.h"
#include "tile_grid.h"

namespace testing {

// Every world in these tests is 8px-per-brick and every step is 1/60s, the same
// numbers the game runs on, so the values asserted here are the values the game
// produces.
constexpr float kDt = 1.0f / 60.0f;
constexpr float kTile = platformer::TileGrid::kTileSize;

// A tile grid plus a player, driven one fixed step at a time. The player is placed
// by feet position in pixels, which is also the origin the collision boxes use.
//
// The player is a plain member rather than something allocated on the heap, so a
// world is built by calling build() on a local:
//
//     testing::World world;
//     world.build(rows, spawn);
//
// The player holds a pointer to this world's grid, so a world must never be copied
// or returned by value -- it would leave the player probing the grid it was copied
// from. Helpers fill a world the caller owns instead.
class World {
public:
	World() : player_(config_, &grid_) {
		spawn_ = platformer::Vec2();
	}

	void build(const std::vector<std::string> &p_rows, platformer::Vec2 p_spawn, float p_facing = 1.0f) {
		grid_.load(p_rows);
		spawn_ = p_spawn;
		player_.reset(p_spawn, p_facing);
	}

	void respawn() { player_.reset(spawn_); }

	// One idle step. Coyote time is armed by landing, not by spawning, so a player
	// that has just been placed cannot jump until it has stood on something for a
	// frame. Worlds that jump straight from a spawn need this first.
	void settle() { player_.step(platformer::InputFrame(), kDt); }

	platformer::PlayerCore &player() { return player_; }
	platformer::TileGrid &grid() { return grid_; }

	// Runs `p_steps` fixed steps with the same input held throughout.
	void run(const platformer::InputFrame &p_input, int p_steps) {
		for (int i = 0; i < p_steps; ++i) {
			player_.step(p_input, kDt);
		}
	}

	// One step with an edge-triggered press on the first step only.
	void press_jump(const platformer::InputFrame &p_input, int p_steps = 1) {
		platformer::InputFrame frame = p_input;
		frame.jump_pressed = true;
		player_.step(frame, kDt);
		frame.jump_pressed = false;
		for (int i = 1; i < p_steps; ++i) {
			player_.step(frame, kDt);
		}
	}

	void press_dash(const platformer::InputFrame &p_input, int p_steps = 1) {
		platformer::InputFrame frame = p_input;
		frame.dash_pressed = true;
		player_.step(frame, kDt);
		frame.dash_pressed = false;
		for (int i = 1; i < p_steps; ++i) {
			player_.step(frame, kDt);
		}
	}

	// Stops as soon as the given state has been reached, so a test can wait for a
	// specific moment without knowing which frame it lands on. Written as three
	// small waiters rather than one general "wait until X" so that no test has to
	// hand the harness a condition of its own.
	//
	// Returns true if the state was reached inside the step budget.

	// Waits for the dash to have sampled its aim and written its speed. It freezes
	// for a few frames first, so this is always at least three steps.
	bool wait_for_dash_launch(const platformer::InputFrame &p_input, int p_max_steps = 12) {
		for (int i = 0; i < p_max_steps; ++i) {
			if (player_.dash_dir() != platformer::Vec2()) {
				return true;
			}
			player_.step(p_input, kDt);
		}
		return player_.dash_dir() != platformer::Vec2();
	}

	// Waits for the dash to be over, whichever way it ends.
	bool wait_for_dash_end(const platformer::InputFrame &p_input, int p_max_steps = 30) {
		for (int i = 0; i < p_max_steps; ++i) {
			if (player_.mode() != platformer::Mode::kDash) {
				return true;
			}
			player_.step(p_input, kDt);
		}
		return player_.mode() != platformer::Mode::kDash;
	}

	// Waits for the player to be past a given x, used to walk off a ledge.
	bool wait_for_x_above(float p_x, const platformer::InputFrame &p_input, int p_max_steps = 120) {
		for (int i = 0; i < p_max_steps; ++i) {
			if (player_.position().x >= p_x) {
				return true;
			}
			player_.step(p_input, kDt);
		}
		return player_.position().x >= p_x;
	}

	// Convenience: the top row of the ground, as a feet coordinate.
	static float row_top(int p_row) { return static_cast<float>(p_row) * kTile; }

private:
	// Declaration order matters: the player is constructed with a pointer to the
	// grid, so the grid has to exist first.
	platformer::TileGrid grid_;
	platformer::PlayerConfig config_;
	platformer::PlayerCore player_;
	platformer::Vec2 spawn_;
};

// Input helpers. `hold` keeps a button down for the whole run; edges are set by
// World::press_jump / press_dash.
inline platformer::InputFrame hold(float p_x, float p_y = 0.0f, bool p_jump = false) {
	platformer::InputFrame input;
	input.move_x = p_x;
	input.move_y = p_y;
	input.jump_held = p_jump;
	return input;
}

inline void fill_rect(std::vector<std::string> &p_rows, int p_col0, int p_row0, int p_col1, int p_row1, char p_tile) {
	for (int row = p_row0; row <= p_row1; ++row) {
		for (int col = p_col0; col <= p_col1; ++col) {
			p_rows[static_cast<size_t>(row)][static_cast<size_t>(col)] = p_tile;
		}
	}
}

// A room with a floor: `p_width` x `p_height` tiles, solid from `p_floor_row` down.
inline std::vector<std::string> room_with_floor(int p_width, int p_height, int p_floor_row) {
	std::vector<std::string> rows;
	for (int row = 0; row < p_height; ++row) {
		rows.push_back(std::string(static_cast<size_t>(p_width), row >= p_floor_row ? '#' : '.'));
	}
	return rows;
}

// A floor row with a gap: tiles in [p_gap0, p_gap1] are removed for the full height
// below `p_floor_row`.
inline std::vector<std::string> floor_with_gap(int p_width, int p_height, int p_floor_row, int p_gap0, int p_gap1) {
	std::vector<std::string> rows = room_with_floor(p_width, p_height, p_floor_row);
	for (int row = p_floor_row; row < p_height; ++row) {
		for (int col = p_gap0; col <= p_gap1; ++col) {
			rows[static_cast<size_t>(row)][static_cast<size_t>(col)] = '.';
		}
	}
	return rows;
}

} // namespace testing

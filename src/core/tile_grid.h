#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "collision_query.h"

namespace platformer {

// Test-side collision world: a uniform grid of 8px tiles, the same brick size the
// game uses. Broad phase is an index range derived directly from the query box, so
// a probe is a handful of integer comparisons instead of a physics-server round
// trip -- that is what lets the whole physics regression run in milliseconds with
// no engine attached.
//
// Tile (col, row) covers x in [col * 8, col * 8 + 8), y in [row * 8, row * 8 + 8).
// Screen convention: y grows downward, row 0 is the top.
class TileGrid : public CollisionQuery {
public:
	static constexpr float kTileSize = 8.0f;

	// ASCII map: '#' is solid, anything else is empty. Row 0 is the top row.
	void load(const std::vector<std::string> &p_rows);

	void set_solid(int p_col, int p_row, bool p_solid);
	void fill_rect(int p_col0, int p_row0, int p_col1, int p_row1, bool p_solid = true);
	bool is_solid(int p_col, int p_row) const;

	int cols() const { return cols_; }
	int rows() const { return rows_; }

	bool overlaps_box(const Vec2 &p_min, const Vec2 &p_max) const override;

private:
	int cols_ = 0;
	int rows_ = 0;
	std::vector<uint8_t> solid_;
};

} // namespace platformer

#include "tile_grid.h"

#include <algorithm>
#include <cmath>

namespace platformer {

void TileGrid::load(const std::vector<std::string> &p_rows) {
	rows_ = static_cast<int>(p_rows.size());
	cols_ = 0;
	for (int i = 0; i < static_cast<int>(p_rows.size()); ++i) {
		cols_ = std::max(cols_, static_cast<int>(p_rows[static_cast<size_t>(i)].size()));
	}
	solid_.assign(static_cast<size_t>(cols_) * static_cast<size_t>(rows_), 0);
	for (int row = 0; row < rows_; ++row) {
		const std::string &line = p_rows[static_cast<size_t>(row)];
		for (int col = 0; col < static_cast<int>(line.size()); ++col) {
			solid_[static_cast<size_t>(row) * cols_ + col] = line[static_cast<size_t>(col)] == '#' ? 1 : 0;
		}
	}
}

void TileGrid::set_solid(int p_col, int p_row, bool p_solid) {
	if (p_col < 0 || p_col >= cols_ || p_row < 0 || p_row >= rows_) {
		return;
	}
	solid_[static_cast<size_t>(p_row) * cols_ + p_col] = p_solid ? 1 : 0;
}

void TileGrid::fill_rect(int p_col0, int p_row0, int p_col1, int p_row1, bool p_solid) {
	for (int row = p_row0; row <= p_row1; ++row) {
		for (int col = p_col0; col <= p_col1; ++col) {
			set_solid(col, row, p_solid);
		}
	}
}

bool TileGrid::is_solid(int p_col, int p_row) const {
	// Out of bounds is empty space, not an invisible wall: tests build the level
	// they mean, and nothing stops a player from running off a ledge.
	if (p_col < 0 || p_col >= cols_ || p_row < 0 || p_row >= rows_) {
		return false;
	}
	return solid_[static_cast<size_t>(p_row) * cols_ + p_col] != 0;
}

bool TileGrid::overlaps_box(const Vec2 &p_min, const Vec2 &p_max) const {
	if (p_max.x <= p_min.x || p_max.y <= p_min.y) {
		return false;
	}
	// Any tile the box can reach: the one its low edge sits in, up to the one its
	// high edge sits in. A high edge exactly on a boundary adds a tile whose
	// overlap is zero, which the per-tile test below rejects.
	const int col0 = static_cast<int>(std::floor(p_min.x / kTileSize));
	const int col1 = static_cast<int>(std::floor(p_max.x / kTileSize));
	const int row0 = static_cast<int>(std::floor(p_min.y / kTileSize));
	const int row1 = static_cast<int>(std::floor(p_max.y / kTileSize));

	for (int row = row0; row <= row1; ++row) {
		for (int col = col0; col <= col1; ++col) {
			if (!is_solid(col, row)) {
				continue;
			}
			const float overlap_x = std::min(p_max.x, (col + 1) * kTileSize) - std::max(p_min.x, col * kTileSize);
			const float overlap_y = std::min(p_max.y, (row + 1) * kTileSize) - std::max(p_min.y, row * kTileSize);
			if (overlap_x > kContactEpsilon && overlap_y > kContactEpsilon) {
				return true;
			}
		}
	}
	return false;
}

} // namespace platformer

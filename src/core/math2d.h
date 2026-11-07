#pragma once

#include <cmath>

// Minimal 2D vector math. The simulation deliberately avoids engine types so the
// exact same code runs inside Godot and inside a plain unit-test binary.
namespace platformer {

// Neutral element for float comparisons. The world unit is one pixel and positions
// stay on the integer lattice, so this only ever absorbs float round-off.
constexpr float kEpsilon = 1e-6f;

struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;

	Vec2() = default;
	Vec2(float p_x, float p_y) : x(p_x), y(p_y) {}

	Vec2 operator+(const Vec2 &p_other) const { return Vec2(x + p_other.x, y + p_other.y); }
	Vec2 operator-(const Vec2 &p_other) const { return Vec2(x - p_other.x, y - p_other.y); }
	Vec2 operator*(float p_scale) const { return Vec2(x * p_scale, y * p_scale); }
	Vec2 operator-() const { return Vec2(-x, -y); }
	Vec2 &operator+=(const Vec2 &p_other) {
		x += p_other.x;
		y += p_other.y;
		return *this;
	}
	Vec2 &operator-=(const Vec2 &p_other) {
		x -= p_other.x;
		y -= p_other.y;
		return *this;
	}
	bool operator==(const Vec2 &p_other) const { return x == p_other.x && y == p_other.y; }
	bool operator!=(const Vec2 &p_other) const { return !(*this == p_other); }

	float length() const { return std::sqrt(x * x + y * y); }

	Vec2 normalized() const {
		const float len = length();
		return len > kEpsilon ? Vec2(x / len, y / len) : Vec2();
	}
};

inline float signf(float p_value) {
	return p_value > 0.0f ? 1.0f : (p_value < 0.0f ? -1.0f : 0.0f);
}

// Godot's move_toward / Celeste's Calc.Approach. The reference relies on the
// "speed up *and* slow down" behaviour: a falling cap that drops has to be able to
// pull the current speed down with it, which is what fastfall is.
inline float approach(float p_from, float p_to, float p_delta) {
	if (std::fabs(p_to - p_from) <= p_delta) {
		return p_to;
	}
	return p_from + signf(p_to - p_from) * p_delta;
}

} // namespace platformer

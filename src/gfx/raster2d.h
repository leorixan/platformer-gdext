#pragma once

#include <cstddef>
#include <vector>

namespace gfx {

// A software 2D rasterizer: it turns lines and triangles into pixels with no
// graphics API underneath. The module has no dependency on the simulation next to
// it and none on any engine, which is why it carries its own vector and colour
// types -- it can be lifted out of this repository and dropped into another project
// as it stands.
//
// Screen convention: x grows right, y grows down, and the origin is the top-left
// corner of the framebuffer. One unit is one pixel.
struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;

	Vec2() = default;
	Vec2(float p_x, float p_y) : x(p_x), y(p_y) {}

	Vec2 operator+(const Vec2 &p_other) const { return Vec2(x + p_other.x, y + p_other.y); }
	Vec2 operator-(const Vec2 &p_other) const { return Vec2(x - p_other.x, y - p_other.y); }
	Vec2 operator*(float p_scale) const { return Vec2(x * p_scale, y * p_scale); }

	// 2D cross product: the (signed) area of the parallelogram the two vectors
	// span. Positive means the second vector is clockwise from the first on screen,
	// because y points down.
	float cross(const Vec2 &p_other) const { return x * p_other.y - y * p_other.x; }

	Vec2 rotated(float p_radians) const;
};

// Straight (non-premultiplied) RGBA, each channel in 0..1. Floats rather than a
// packed integer because the rasterizer interpolates and blends every channel of
// every pixel it writes, and reading that arithmetic is worth more here than the
// memory a packed buffer would save.
struct Color {
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float a = 0.0f;

	Color() = default;
	Color(float p_r, float p_g, float p_b, float p_a = 1.0f) : r(p_r), g(p_g), b(p_b), a(p_a) {}

	Color operator*(float p_scale) const { return Color(r * p_scale, g * p_scale, b * p_scale, a * p_scale); }
	// Channel-wise, which is what a tint is: a white tint leaves the source alone.
	Color operator*(const Color &p_tint) const;
	Color operator+(const Color &p_other) const;
};

// Source-over compositing: p_src is laid on top of p_dst. This single blend is all
// a 2D rasterizer needs -- every sprite, line and triangle goes through it.
Color blend_over(const Color &p_src, const Color &p_dst);

// An image to sample from. Bilinear filtering with edge clamping, so a triangle
// that runs past the texture edge smears the border texel instead of wrapping.
class Texture {
public:
	Texture(int p_width, int p_height, const std::vector<Color> &p_pixels);

	int width() const { return width_; }
	int height() const { return height_; }

	// p_uv is in 0..1, v growing downward.
	Color sample(const Vec2 &p_uv) const;

	// A generated image, so a caller can rasterize something without loading a file
	// from disk: the sample has no image loader, and no dependency to add one.
	static Texture checkerboard(int p_size, int p_check, const Color &p_a, const Color &p_b);

private:
	// No bounds check: every caller clamps its coordinates against the size first.
	const Color &texel(int p_x, int p_y) const { return pixels_[static_cast<std::size_t>(p_y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(p_x)]; }

	int width_;
	int height_;
	std::vector<Color> pixels_;
};

// The render target. Everything the rasterizer draws lands in a plain array of
// pixels, which is what makes the output testable without a window or a GPU: a
// test can assert the exact colour of a given pixel after a draw call.
class Framebuffer {
public:
	Framebuffer(int p_width, int p_height);

	int width() const { return width_; }
	int height() const { return height_; }

	void clear(const Color &p_color);
	Color pixel(int p_x, int p_y) const;

	// Writes the colour as it is; the alpha channel is stored, not applied.
	void set_pixel(int p_x, int p_y, const Color &p_color);

	// Composites the colour over what is already there. Covers the pixel only when
	// the incoming alpha is 1.
	void blend_pixel(int p_x, int p_y, const Color &p_color);

	// Bresenham: one pixel per step along the major axis, no gaps and no double
	// writes, using only integer arithmetic.
	void draw_line(const Vec2 &p_a, const Vec2 &p_b, const Color &p_color);

	// Flat-shaded triangle, rasterized by testing pixel centres against the three
	// half-plane edges. Either winding works, and a pixel on a shared edge goes to
	// exactly one of the two triangles.
	void fill_triangle(const Vec2 &p_a, const Vec2 &p_b, const Vec2 &p_c, const Color &p_color);

	// The same triangle with a texture mapped onto it: the barycentric weights that
	// decide coverage also interpolate the texture coordinates.
	void fill_triangle_textured(const Vec2 &p_a, const Vec2 &p_b, const Vec2 &p_c,
			const Vec2 &p_uv_a, const Vec2 &p_uv_b, const Vec2 &p_uv_c,
			const Texture &p_texture, const Color &p_tint);

private:
	bool contains(int p_x, int p_y) const;

	int width_;
	int height_;
	std::vector<Color> pixels_;
};

} // namespace gfx

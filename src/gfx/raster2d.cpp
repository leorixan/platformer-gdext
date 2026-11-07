#include "raster2d.h"

#include <algorithm>
#include <cmath>
#include <utility>

// A framebuffer is just an array of pixels, so drawing is ordinary code and the
// result is ordinary data. Nothing here opens a window, talks to a GPU or needs an
// image file: a caller builds a Framebuffer, draws into it, and reads the pixels
// back. That is what lets a test assert the exact colour of a pixel after a draw
// call, the same way the movement tests assert an exact velocity.
//
//     gfx::Framebuffer target(320, 180);
//     target.clear(gfx::Color(0.05f, 0.05f, 0.08f));
//     target.fill_triangle(gfx::Vec2(40.0f, 20.0f), gfx::Vec2(280.0f, 60.0f),
//             gfx::Vec2(160.0f, 160.0f), gfx::Color(0.9f, 0.4f, 0.2f));
//     target.draw_line(gfx::Vec2(0.0f, 0.0f), gfx::Vec2(319.0f, 179.0f),
//             gfx::Color(1.0f, 1.0f, 1.0f, 0.5f));

namespace gfx {

namespace {

// The half-plane function: it is positive on one side of the line a->b, negative on
// the other and exactly zero on it. Twice the signed area of the triangle (a, b,
// point), which is why dividing the three of them by the triangle's own area gives
// barycentric weights that add up to one.
float edge(const Vec2 &p_a, const Vec2 &p_b, const Vec2 &p_point) {
	return (p_b.x - p_a.x) * (p_point.y - p_a.y) - (p_b.y - p_a.y) * (p_point.x - p_a.x);
}

float clamp01(float p_value) {
	return std::max(0.0f, std::min(1.0f, p_value));
}

int clamp_int(int p_value, int p_low, int p_high) {
	return std::max(p_low, std::min(p_high, p_value));
}

// The top-left fill rule. A pixel centre that lands exactly on an edge belongs to
// the triangle only when that edge is one of its top or left edges. Two triangles
// that share an edge see it from opposite sides, so this picks exactly one of them:
// the shared pixels are covered once, not twice and not zero times. Without a rule
// like this a diagonal seam shows up as soon as the colour has any transparency,
// because the pixels along it get blended twice.
bool edge_accepts(float p_weight, const Vec2 &p_a, const Vec2 &p_b) {
	if (p_weight > 0.0f) {
		return true; // strictly inside this edge
	}
	if (p_weight < 0.0f) {
		return false;
	}
	const bool is_top = p_a.y == p_b.y && p_b.x > p_a.x;
	const bool is_left = p_b.y > p_a.y;
	return is_top || is_left;
}

} // namespace

Vec2 Vec2::rotated(float p_radians) const {
	// The standard rotation matrix. With y pointing down this turns clockwise on
	// screen, which is the direction a positive angle reads in screen space.
	const float c = std::cos(p_radians);
	const float s = std::sin(p_radians);
	return Vec2(x * c - y * s, x * s + y * c);
}

Color Color::operator*(const Color &p_tint) const {
	return Color(r * p_tint.r, g * p_tint.g, b * p_tint.b, a * p_tint.a);
}

Color Color::operator+(const Color &p_other) const {
	return Color(r + p_other.r, g + p_other.g, b + p_other.b, a + p_other.a);
}

Color blend_over(const Color &p_src, const Color &p_dst) {
	// Straight alpha: the result's alpha is the union of the two coverages, and the
	// colour is each source's contribution divided back out by that coverage. That
	// division is what keeps a half-transparent red over blue from washing out the
	// alpha channel of the pixels around it.
	const float out_a = p_src.a + p_dst.a * (1.0f - p_src.a);
	if (out_a <= 0.0f) {
		return Color(0.0f, 0.0f, 0.0f, 0.0f);
	}
	Color out;
	out.r = (p_src.r * p_src.a + p_dst.r * p_dst.a * (1.0f - p_src.a)) / out_a;
	out.g = (p_src.g * p_src.a + p_dst.g * p_dst.a * (1.0f - p_src.a)) / out_a;
	out.b = (p_src.b * p_src.a + p_dst.b * p_dst.a * (1.0f - p_src.a)) / out_a;
	out.a = out_a;
	return out;
}

// --- texture ----------------------------------------------------------------

Texture::Texture(int p_width, int p_height, const std::vector<Color> &p_pixels) :
		width_(p_width), height_(p_height), pixels_(p_pixels) {
	// A texture with no pixels is useless rather than fatal, and a short pixel array
	// is padded instead of read past its end. Both are the caller's mistake, but a
	// renderer should not be the thing that crashes on it.
	width_ = std::max(1, width_);
	height_ = std::max(1, height_);
	pixels_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_));
}

Color Texture::sample(const Vec2 &p_uv) const {
	// Texel centres sit at (i + 0.5) / size in uv space, so the uv range maps onto
	// [0, size - 1] once scaled.
	const float u = clamp01(p_uv.x) * static_cast<float>(width_ - 1);
	const float v = clamp01(p_uv.y) * static_cast<float>(height_ - 1);

	const int x0 = static_cast<int>(std::floor(u));
	const int y0 = static_cast<int>(std::floor(v));
	const int x1 = std::min(x0 + 1, width_ - 1);
	const int y1 = std::min(y0 + 1, height_ - 1);

	// How far the sample sits between the four surrounding texels, in each axis.
	const float fx = u - static_cast<float>(x0);
	const float fy = v - static_cast<float>(y0);

	// Bilinear: weight each texel by the area of the sub-rectangle opposite it, then
	// add. At fx = fy = 0 three of the four weights are zero and the nearest texel
	// comes through untouched.
	const float w00 = (1.0f - fx) * (1.0f - fy);
	const float w10 = fx * (1.0f - fy);
	const float w01 = (1.0f - fx) * fy;
	const float w11 = fx * fy;

	return texel(x0, y0) * w00 + texel(x1, y0) * w10 + texel(x0, y1) * w01 + texel(x1, y1) * w11;
}

Texture Texture::checkerboard(int p_size, int p_check, const Color &p_a, const Color &p_b) {
	const int size = std::max(1, p_size);
	const int check = std::max(1, p_check);
	std::vector<Color> pixels;
	pixels.reserve(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			const bool odd = ((x / check) + (y / check)) % 2 == 1;
			pixels.push_back(odd ? p_b : p_a);
		}
	}
	return Texture(size, size, pixels);
}

// --- framebuffer ------------------------------------------------------------

Framebuffer::Framebuffer(int p_width, int p_height) :
		width_(std::max(1, p_width)), height_(std::max(1, p_height)) {
	pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), Color());
}

bool Framebuffer::contains(int p_x, int p_y) const {
	return p_x >= 0 && p_x < width_ && p_y >= 0 && p_y < height_;
}

void Framebuffer::clear(const Color &p_color) {
	for (std::size_t i = 0; i < pixels_.size(); ++i) {
		pixels_[i] = p_color;
	}
}

Color Framebuffer::pixel(int p_x, int p_y) const {
	// Off the target reads as fully transparent rather than out of bounds, so a
	// caller can ask about a pixel the geometry may not have reached.
	if (!contains(p_x, p_y)) {
		return Color();
	}
	return pixels_[static_cast<std::size_t>(p_y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(p_x)];
}

void Framebuffer::set_pixel(int p_x, int p_y, const Color &p_color) {
	if (!contains(p_x, p_y)) {
		return;
	}
	pixels_[static_cast<std::size_t>(p_y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(p_x)] = p_color;
}

void Framebuffer::blend_pixel(int p_x, int p_y, const Color &p_color) {
	if (!contains(p_x, p_y)) {
		return;
	}
	const std::size_t index = static_cast<std::size_t>(p_y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(p_x);
	pixels_[index] = blend_over(p_color, pixels_[index]);
}

void Framebuffer::draw_line(const Vec2 &p_a, const Vec2 &p_b, const Color &p_color) {
	// Bresenham in its integer form. The error term tracks how far the line has
	// drifted from the ideal one; it is compared against the two axis deltas to
	// decide when to step on the minor axis. One pixel per column or row, so the
	// line has no gaps and no pixel is written twice.
	int x0 = static_cast<int>(std::lround(p_a.x));
	int y0 = static_cast<int>(std::lround(p_a.y));
	const int x1 = static_cast<int>(std::lround(p_b.x));
	const int y1 = static_cast<int>(std::lround(p_b.y));

	const int dx = std::abs(x1 - x0);
	const int dy = -std::abs(y1 - y0);
	const int step_x = x0 < x1 ? 1 : -1;
	const int step_y = y0 < y1 ? 1 : -1;
	int error = dx + dy;

	for (;;) {
		blend_pixel(x0, y0, p_color);
		if (x0 == x1 && y0 == y1) {
			return;
		}
		const int doubled = 2 * error;
		if (doubled >= dy) {
			error += dy;
			x0 += step_x;
		}
		if (doubled <= dx) {
			error += dx;
			y0 += step_y;
		}
	}
}

void Framebuffer::fill_triangle(const Vec2 &p_a, const Vec2 &p_b, const Vec2 &p_c, const Color &p_color) {
	// Work on copies: the corners may be swapped below, and the winding has to be the
	// same in both fill functions for the fill rule to assign shared edges the same
	// way. Swapping flips the sign of the area without moving the triangle.
	Vec2 a = p_a;
	Vec2 b = p_b;
	Vec2 c = p_c;
	float area = edge(a, b, c);
	if (area == 0.0f) {
		return; // the three points are collinear: there is nothing to cover
	}
	if (area < 0.0f) {
		std::swap(b, c);
		area = -area;
	}

	// Only the pixels whose centres fall inside the bounding box can possibly be
	// covered, and the rest of the target is skipped without a test per pixel.
	const int min_x = clamp_int(static_cast<int>(std::floor(std::min(a.x, std::min(b.x, c.x)))), 0, width_ - 1);
	const int max_x = clamp_int(static_cast<int>(std::ceil(std::max(a.x, std::max(b.x, c.x)))), 0, width_ - 1);
	const int min_y = clamp_int(static_cast<int>(std::floor(std::min(a.y, std::min(b.y, c.y)))), 0, height_ - 1);
	const int max_y = clamp_int(static_cast<int>(std::ceil(std::max(a.y, std::max(b.y, c.y)))), 0, height_ - 1);

	for (int y = min_y; y <= max_y; ++y) {
		for (int x = min_x; x <= max_x; ++x) {
			// Testing the pixel centre rather than its corner is what keeps the
			// boundary consistent: a pixel belongs to the triangle when its middle does.
			const Vec2 centre(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
			const float w0 = edge(b, c, centre) / area;
			const float w1 = edge(c, a, centre) / area;
			const float w2 = edge(a, b, centre) / area;
			if (!edge_accepts(w0, b, c) || !edge_accepts(w1, c, a) || !edge_accepts(w2, a, b)) {
				continue;
			}
			blend_pixel(x, y, p_color);
		}
	}
}

void Framebuffer::fill_triangle_textured(const Vec2 &p_a, const Vec2 &p_b, const Vec2 &p_c,
		const Vec2 &p_uv_a, const Vec2 &p_uv_b, const Vec2 &p_uv_c,
		const Texture &p_texture, const Color &p_tint) {
	// Same normalization as the flat fill, and the texture coordinates are swapped
	// alongside the corners they belong to so the image stays where it was.
	Vec2 a = p_a;
	Vec2 b = p_b;
	Vec2 c = p_c;
	Vec2 uv_a = p_uv_a;
	Vec2 uv_b = p_uv_b;
	Vec2 uv_c = p_uv_c;
	float area = edge(a, b, c);
	if (area == 0.0f) {
		return;
	}
	if (area < 0.0f) {
		std::swap(b, c);
		std::swap(uv_b, uv_c);
		area = -area;
	}

	const int min_x = clamp_int(static_cast<int>(std::floor(std::min(a.x, std::min(b.x, c.x)))), 0, width_ - 1);
	const int max_x = clamp_int(static_cast<int>(std::ceil(std::max(a.x, std::max(b.x, c.x)))), 0, width_ - 1);
	const int min_y = clamp_int(static_cast<int>(std::floor(std::min(a.y, std::min(b.y, c.y)))), 0, height_ - 1);
	const int max_y = clamp_int(static_cast<int>(std::ceil(std::max(a.y, std::max(b.y, c.y)))), 0, height_ - 1);

	for (int y = min_y; y <= max_y; ++y) {
		for (int x = min_x; x <= max_x; ++x) {
			const Vec2 centre(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
			const float w0 = edge(b, c, centre) / area;
			const float w1 = edge(c, a, centre) / area;
			const float w2 = edge(a, b, centre) / area;
			if (!edge_accepts(w0, b, c) || !edge_accepts(w1, c, a) || !edge_accepts(w2, a, b)) {
				continue;
			}

			// The same weights that decided coverage say how much each corner of the
			// triangle contributes here, so the texture coordinate -- and any other
			// per-corner value -- interpolates across the surface for free. 2D needs
			// no perspective divide: the mapping is affine.
			const Vec2 uv = uv_a * w0 + uv_b * w1 + uv_c * w2;
			blend_pixel(x, y, p_texture.sample(uv) * p_tint);
		}
	}
}

} // namespace gfx

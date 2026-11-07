#include "third_party/doctest.h"

#include "raster2d.h"

using gfx::Color;
using gfx::Framebuffer;
using gfx::Texture;
using gfx::Vec2;

// The rasterizer writes into a plain array of pixels, so a test can read the exact
// colour of one back. That is the whole reason the module is built this way: the
// arithmetic is checked without a window, a GPU or a screenshot to compare against.

namespace {

// Every channel within a thousandth of the expected value. Rasterization is float
// arithmetic, so exact equality is the wrong question; the tolerance is small enough
// that a wrong formula still fails.
bool near(float p_a, float p_b) {
	const float diff = p_a - p_b;
	return diff < 0.001f && diff > -0.001f;
}

int count_covered(const Framebuffer &p_target) {
	int covered = 0;
	for (int y = 0; y < p_target.height(); ++y) {
		for (int x = 0; x < p_target.width(); ++x) {
			if (p_target.pixel(x, y).a > 0.0f) {
				++covered;
			}
		}
	}
	return covered;
}

} // namespace

// --- vectors and colours ----------------------------------------------------

TEST_CASE("a quarter turn rotates a vector onto the next axis") {
	const Vec2 turned = Vec2(1.0f, 0.0f).rotated(3.14159265f * 0.5f);

	// y points down, so +x turns towards +y rather than away from it.
	CHECK(near(turned.x, 0.0f));
	CHECK(near(turned.y, 1.0f));
}

TEST_CASE("the cross product reports the side a vector is on") {
	// Positive when the second vector is clockwise from the first on screen.
	CHECK(Vec2(1.0f, 0.0f).cross(Vec2(0.0f, 1.0f)) == doctest::Approx(1.0f));
	CHECK(Vec2(0.0f, 1.0f).cross(Vec2(1.0f, 0.0f)) == doctest::Approx(-1.0f));
	CHECK(Vec2(2.0f, 2.0f).cross(Vec2(2.0f, 2.0f)) == doctest::Approx(0.0f));
}

TEST_CASE("a tint multiplies the channels and a white tint changes nothing") {
	const Color source(0.5f, 0.8f, 0.2f, 1.0f);
	const Color white(1.0f, 1.0f, 1.0f, 1.0f);
	const Color half_blue(0.0f, 0.0f, 0.5f, 1.0f);

	const Color untouched = source * white;
	CHECK(untouched.r == doctest::Approx(0.5f));
	CHECK(untouched.g == doctest::Approx(0.8f));
	CHECK(untouched.b == doctest::Approx(0.2f));

	const Color tinted = source * half_blue;
	CHECK(tinted.r == doctest::Approx(0.0f));
	CHECK(tinted.g == doctest::Approx(0.0f));
	CHECK(tinted.b == doctest::Approx(0.1f));
}

// --- compositing ------------------------------------------------------------

TEST_CASE("an opaque source replaces what is underneath it") {
	const Color result = gfx::blend_over(Color(1.0f, 0.0f, 0.0f, 1.0f), Color(0.0f, 0.0f, 1.0f, 1.0f));

	CHECK(result.r == doctest::Approx(1.0f));
	CHECK(result.b == doctest::Approx(0.0f));
	CHECK(result.a == doctest::Approx(1.0f));
}

TEST_CASE("a half transparent source lands halfway between the two colours") {
	const Color result = gfx::blend_over(Color(1.0f, 0.0f, 0.0f, 0.5f), Color(0.0f, 0.0f, 1.0f, 1.0f));

	// Red covers half of the pixel, the opaque blue underneath fills the rest, so
	// the result is fully covered and both colours are halved.
	CHECK(result.r == doctest::Approx(0.5f));
	CHECK(result.b == doctest::Approx(0.5f));
	CHECK(result.a == doctest::Approx(1.0f));
}

TEST_CASE("compositing onto nothing keeps the source colour") {
	const Color result = gfx::blend_over(Color(0.2f, 0.4f, 0.6f, 0.25f), Color(0.0f, 0.0f, 0.0f, 0.0f));

	CHECK(result.r == doctest::Approx(0.2f));
	CHECK(result.g == doctest::Approx(0.4f));
	CHECK(result.b == doctest::Approx(0.6f));
	CHECK(result.a == doctest::Approx(0.25f));
}

TEST_CASE("a fully transparent source leaves the destination alone") {
	const Color destination(0.3f, 0.6f, 0.9f, 0.8f);
	const Color result = gfx::blend_over(Color(1.0f, 1.0f, 1.0f, 0.0f), destination);

	CHECK(result.r == doctest::Approx(destination.r));
	CHECK(result.g == doctest::Approx(destination.g));
	CHECK(result.b == doctest::Approx(destination.b));
	CHECK(result.a == doctest::Approx(destination.a));
}

// --- framebuffer ------------------------------------------------------------

TEST_CASE("a fresh framebuffer is transparent all over") {
	const Framebuffer target(8, 4);

	CHECK(target.width() == 8);
	CHECK(target.height() == 4);
	CHECK(target.pixel(0, 0).a == 0.0f);
	CHECK(target.pixel(7, 3).a == 0.0f);
	CHECK(count_covered(target) == 0);
}

TEST_CASE("clear fills every pixel of the target") {
	Framebuffer target(6, 5);
	target.clear(Color(0.1f, 0.2f, 0.3f, 1.0f));

	CHECK(count_covered(target) == 30);
	CHECK(target.pixel(5, 4).r == doctest::Approx(0.1f));
	CHECK(target.pixel(5, 4).b == doctest::Approx(0.3f));
}

TEST_CASE("set_pixel stores the alpha instead of applying it") {
	Framebuffer target(4, 4);
	target.set_pixel(2, 1, Color(1.0f, 1.0f, 1.0f, 0.25f));

	// The colour is written as it came in: nothing underneath was consulted.
	CHECK(target.pixel(2, 1).r == doctest::Approx(1.0f));
	CHECK(target.pixel(2, 1).a == doctest::Approx(0.25f));
	CHECK(count_covered(target) == 1);
}

TEST_CASE("blend_pixel composites over the pixel that is already there") {
	Framebuffer target(4, 4);
	target.clear(Color(0.0f, 0.0f, 1.0f, 1.0f));
	target.blend_pixel(1, 1, Color(1.0f, 0.0f, 0.0f, 0.5f));

	CHECK(target.pixel(1, 1).r == doctest::Approx(0.5f));
	CHECK(target.pixel(1, 1).b == doctest::Approx(0.5f));
	// The neighbours were left untouched.
	CHECK(target.pixel(0, 1).r == doctest::Approx(0.0f));
}

TEST_CASE("a draw outside the target is dropped rather than wrapped") {
	Framebuffer target(4, 4);
	target.set_pixel(-1, 0, Color(1.0f, 1.0f, 1.0f));
	target.set_pixel(4, 0, Color(1.0f, 1.0f, 1.0f));
	target.set_pixel(0, 4, Color(1.0f, 1.0f, 1.0f));

	// Reading off the target is a miss, not a crash and not the nearest pixel.
	CHECK(target.pixel(-1, 0).a == 0.0f);
	CHECK(target.pixel(4, 0).a == 0.0f);
	CHECK(count_covered(target) == 0);
}

// --- lines ------------------------------------------------------------------

TEST_CASE("a horizontal line covers its two ends and everything between") {
	Framebuffer target(8, 4);
	target.draw_line(Vec2(1.0f, 2.0f), Vec2(6.0f, 2.0f), Color(1.0f, 1.0f, 1.0f));

	for (int x = 1; x <= 6; ++x) {
		CHECK(target.pixel(x, 2).a == doctest::Approx(1.0f));
	}
	CHECK(target.pixel(0, 2).a == 0.0f);
	CHECK(target.pixel(7, 2).a == 0.0f);
	CHECK(count_covered(target) == 6);
}

TEST_CASE("a diagonal line steps one pixel at a time") {
	Framebuffer target(8, 8);
	target.draw_line(Vec2(0.0f, 0.0f), Vec2(5.0f, 5.0f), Color(1.0f, 1.0f, 1.0f));

	// Exactly one pixel per column and per row: six pixels, no gaps, no doubles.
	CHECK(count_covered(target) == 6);
	for (int i = 0; i <= 5; ++i) {
		CHECK(target.pixel(i, i).a == doctest::Approx(1.0f));
	}
}

TEST_CASE("a steep line still leaves no gaps") {
	Framebuffer target(4, 8);
	target.draw_line(Vec2(0.0f, 0.0f), Vec2(2.0f, 6.0f), Color(1.0f, 1.0f, 1.0f));

	// The line is steeper than it is wide, so it is walked one row at a time. Every
	// row it passes through must have a pixel: a gap would show as a dotted line.
	for (int y = 0; y <= 6; ++y) {
		int in_row = 0;
		for (int x = 0; x < 4; ++x) {
			if (target.pixel(x, y).a > 0.0f) {
				++in_row;
			}
		}
		CHECK(in_row == 1);
	}
	CHECK(count_covered(target) == 7);
}

TEST_CASE("a line from a point to itself is a single pixel") {
	Framebuffer target(4, 4);
	target.draw_line(Vec2(2.0f, 3.0f), Vec2(2.0f, 3.0f), Color(1.0f, 1.0f, 1.0f));

	CHECK(count_covered(target) == 1);
	CHECK(target.pixel(2, 3).a == doctest::Approx(1.0f));
}

TEST_CASE("a line running off the target is clipped at the border") {
	Framebuffer target(8, 4);
	target.draw_line(Vec2(-20.0f, 1.0f), Vec2(20.0f, 1.0f), Color(1.0f, 1.0f, 1.0f));

	// The visible part is the whole row, and nothing was written outside it.
	CHECK(count_covered(target) == 8);
	CHECK(target.pixel(0, 1).a == doctest::Approx(1.0f));
	CHECK(target.pixel(7, 1).a == doctest::Approx(1.0f));
}

// --- triangles --------------------------------------------------------------

TEST_CASE("a triangle covers the inside of its three edges") {
	Framebuffer target(8, 8);
	target.fill_triangle(Vec2(1.0f, 1.0f), Vec2(7.0f, 1.0f), Vec2(1.0f, 7.0f), Color(1.0f, 1.0f, 1.0f));

	// Well inside the right angle.
	CHECK(target.pixel(2, 2).a == doctest::Approx(1.0f));
	CHECK(target.pixel(3, 1).a == doctest::Approx(1.0f));
	CHECK(target.pixel(1, 5).a == doctest::Approx(1.0f));
	// The other side of the diagonal, and outside the bounding box entirely.
	CHECK(target.pixel(6, 6).a == 0.0f);
	CHECK(target.pixel(0, 0).a == 0.0f);
	CHECK(target.pixel(7, 7).a == 0.0f);
}

TEST_CASE("the winding of the corners does not change the covered pixels") {
	Framebuffer clockwise(8, 8);
	Framebuffer counter(8, 8);
	clockwise.fill_triangle(Vec2(1.0f, 1.0f), Vec2(7.0f, 1.0f), Vec2(1.0f, 7.0f), Color(1.0f, 1.0f, 1.0f));
	counter.fill_triangle(Vec2(1.0f, 1.0f), Vec2(1.0f, 7.0f), Vec2(7.0f, 1.0f), Color(1.0f, 1.0f, 1.0f));

	CHECK(count_covered(clockwise) == count_covered(counter));
	CHECK(count_covered(clockwise) > 0);
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			CHECK(clockwise.pixel(x, y).a == doctest::Approx(counter.pixel(x, y).a));
		}
	}
}

TEST_CASE("three points in a line cover nothing") {
	Framebuffer target(8, 8);
	target.fill_triangle(Vec2(0.0f, 0.0f), Vec2(3.0f, 3.0f), Vec2(6.0f, 6.0f), Color(1.0f, 1.0f, 1.0f));

	CHECK(count_covered(target) == 0);
}

TEST_CASE("two triangles sharing a diagonal cover each pixel exactly once") {
	// A 4x4 square split along its diagonal. The pixel centres sit exactly on that
	// diagonal, which is the case the fill rule exists for: if both triangles
	// claimed them, every pixel would be blended twice and read 0.75 alpha instead
	// of 0.5 -- a visible seam wherever the colour is not opaque.
	Framebuffer target(4, 4);
	const Color half_white(1.0f, 1.0f, 1.0f, 0.5f);
	target.fill_triangle(Vec2(0.0f, 0.0f), Vec2(4.0f, 0.0f), Vec2(4.0f, 4.0f), half_white);
	target.fill_triangle(Vec2(0.0f, 0.0f), Vec2(4.0f, 4.0f), Vec2(0.0f, 4.0f), half_white);

	CHECK(count_covered(target) == 16);
	for (int y = 0; y < 4; ++y) {
		for (int x = 0; x < 4; ++x) {
			CHECK(target.pixel(x, y).a == doctest::Approx(0.5f));
		}
	}
}

// --- textures ---------------------------------------------------------------

TEST_CASE("a texture pads a pixel array that is too short") {
	const std::vector<Color> pixels(2, Color(1.0f, 0.0f, 0.0f, 1.0f));
	const Texture texture(2, 2, pixels);

	CHECK(texture.width() == 2);
	CHECK(texture.height() == 2);
	// The two missing texels read as transparent rather than past the end of the array.
	CHECK(texture.sample(Vec2(0.0f, 0.0f)).a == doctest::Approx(1.0f));
	CHECK(texture.sample(Vec2(1.0f, 1.0f)).a == doctest::Approx(0.0f));
}

TEST_CASE("a texture of no size is still a valid one by one texture") {
	const Texture texture(0, 0, std::vector<Color>());

	CHECK(texture.width() == 1);
	CHECK(texture.height() == 1);
	CHECK(texture.sample(Vec2(0.5f, 0.5f)).a == 0.0f);
}

TEST_CASE("a checkerboard alternates every check pixels") {
	const Color black(0.0f, 0.0f, 0.0f, 1.0f);
	const Color white(1.0f, 1.0f, 1.0f, 1.0f);
	const Texture texture = Texture::checkerboard(4, 2, black, white);

	CHECK(texture.width() == 4);
	// The first check is the first colour, the one beside it the second, and the
	// row below is offset -- that offset is what makes it a checkerboard and not
	// a set of stripes.
	CHECK(texture.sample(Vec2(0.0f, 0.0f)).r == doctest::Approx(0.0f));
	CHECK(texture.sample(Vec2(1.0f, 0.0f)).r == doctest::Approx(1.0f));
	CHECK(texture.sample(Vec2(0.0f, 1.0f)).r == doctest::Approx(1.0f));
	CHECK(texture.sample(Vec2(1.0f, 1.0f)).r == doctest::Approx(0.0f));
}

TEST_CASE("sampling a texture at its corners returns the corner texels") {
	const Color black(0.0f, 0.0f, 0.0f, 1.0f);
	const Color white(1.0f, 1.0f, 1.0f, 1.0f);
	const Texture texture = Texture::checkerboard(2, 1, black, white);

	CHECK(texture.sample(Vec2(0.0f, 0.0f)).r == doctest::Approx(0.0f));
	CHECK(texture.sample(Vec2(1.0f, 0.0f)).r == doctest::Approx(1.0f));
	CHECK(texture.sample(Vec2(0.0f, 1.0f)).r == doctest::Approx(1.0f));
	CHECK(texture.sample(Vec2(1.0f, 1.0f)).r == doctest::Approx(0.0f));
}

TEST_CASE("a sample between four texels averages them") {
	const Color black(0.0f, 0.0f, 0.0f, 1.0f);
	const Color white(1.0f, 1.0f, 1.0f, 1.0f);
	const Texture texture = Texture::checkerboard(2, 1, black, white);

	// Dead centre of the two by two image: two black texels and two white ones, so
	// bilinear filtering lands on the midpoint.
	const Color middle = texture.sample(Vec2(0.5f, 0.5f));
	CHECK(middle.r == doctest::Approx(0.5f));
	CHECK(middle.a == doctest::Approx(1.0f));
}

TEST_CASE("sampling outside the texture clamps to its edge") {
	const Color black(0.0f, 0.0f, 0.0f, 1.0f);
	const Color white(1.0f, 1.0f, 1.0f, 1.0f);
	const Texture texture = Texture::checkerboard(2, 1, black, white);

	// Repeating the border texel is the alternative to wrapping: a triangle that
	// runs past the edge of the image smears it rather than tiling it.
	CHECK(texture.sample(Vec2(-3.0f, 0.0f)).r == doctest::Approx(0.0f));
	CHECK(texture.sample(Vec2(4.0f, 1.0f)).r == doctest::Approx(0.0f));
}

// --- textured triangles -----------------------------------------------------

TEST_CASE("a textured triangle puts the texel for a corner at that corner") {
	Framebuffer target(8, 8);
	const Texture texture(2, 2, std::vector<Color>(4, Color(0.0f, 1.0f, 0.0f, 1.0f)));
	const Color opaque_white(1.0f, 1.0f, 1.0f, 1.0f);

	// The uv coordinates match the corners one to one, so each corner of the
	// triangle picks up the texel at the matching corner of the image.
	target.fill_triangle_textured(Vec2(0.0f, 0.0f), Vec2(8.0f, 0.0f), Vec2(0.0f, 8.0f),
			Vec2(0.0f, 0.0f), Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f), texture, opaque_white);

	// The image is a flat green, so every covered pixel reads green: the uv values
	// themselves are checked through the tint below.
	CHECK(target.pixel(1, 1).g == doctest::Approx(1.0f));
	CHECK(target.pixel(1, 1).a == doctest::Approx(1.0f));
	CHECK(target.pixel(7, 7).a == 0.0f);
}

TEST_CASE("the tint multiplies the sampled texel") {
	Framebuffer target(8, 8);
	const Texture texture(2, 2, std::vector<Color>(4, Color(1.0f, 1.0f, 1.0f, 1.0f)));

	target.fill_triangle_textured(Vec2(0.0f, 0.0f), Vec2(8.0f, 0.0f), Vec2(0.0f, 8.0f),
			Vec2(0.0f, 0.0f), Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f), texture, Color(0.25f, 0.5f, 0.0f, 1.0f));

	const Color drawn = target.pixel(1, 1);
	CHECK(drawn.r == doctest::Approx(0.25f));
	CHECK(drawn.g == doctest::Approx(0.5f));
	CHECK(drawn.b == doctest::Approx(0.0f));
}

TEST_CASE("a texture coordinate is interpolated across the triangle") {
	Framebuffer target(8, 8);
	// Two black texels on the top row, two white ones on the bottom row, so the
	// green channel of a sample says where it came from vertically.
	const std::vector<Color> pixels = {
		Color(0.0f, 0.0f, 0.0f, 1.0f), Color(0.0f, 0.0f, 0.0f, 1.0f),
		Color(0.0f, 1.0f, 0.0f, 1.0f), Color(0.0f, 1.0f, 0.0f, 1.0f)
	};
	const Texture texture(2, 2, pixels);

	// The triangle spans the whole image, top edge at v = 0 and bottom at v = 1.
	target.fill_triangle_textured(Vec2(0.0f, 0.0f), Vec2(8.0f, 0.0f), Vec2(0.0f, 8.0f),
			Vec2(0.0f, 0.0f), Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f), texture, Color(1.0f, 1.0f, 1.0f, 1.0f));

	// A pixel near the top of the triangle samples the top of the image, and one
	// near the bottom samples the bottom, with the blend moving smoothly between.
	const float near_top = target.pixel(1, 0).g;
	const float near_bottom = target.pixel(1, 5).g;
	CHECK(near_top < 0.3f);
	CHECK(near_bottom > 0.6f);
	CHECK(near_top < near_bottom);
}

# platformer-core

A 2D platformer character controller written in C++17 and shipped to Godot as a
GDExtension. It is a study port of the movement system in *Celeste*: dash, dash
slide, Super / Hyper / Ultra, wall jump, and the corner corrections that make a body
fit through a gap exactly its own size.

The tuning values and the frame order come from the reference implementation and
were checked against a working GDScript version of the same controller, which is
still the one the game runs while this port is being brought up.

The repository holds two modules that share nothing but a build file: the
simulation above, and a software 2D rasterizer in `src/gfx`. Neither includes a
header from the other, and both are covered by the same headless test binary.

## Layout

```
src/core/      the simulation. No Godot headers, no engine types.
  math2d.h          Vec2 and the small numeric helpers
  player_config.h   every tunable, grouped as the parameter sheet groups them
  collision_query.h the one thing the simulation knows about the world
  player_core.h/cpp the frame order, the mode machine, the movement rules
  tile_grid.h/cpp   a tile-grid world, used by the tests
src/gfx/       a software 2D rasterizer. No engine either, and no window.
  raster2d.h/cpp    framebuffer, lines, triangles, texture sampling, blending
src/godot/     the adapter. The only part that includes Godot.
  space_query.h/cpp   CollisionQuery answered by the physics server
  player_body_2d.*    a CharacterBody2D that drives a PlayerCore
  register_types.*    what the engine calls on load
tests/         a headless test binary. No engine, no window, milliseconds.
  third_party/doctest.h  the test framework (MIT), vendored
addons/platformer_core/  the .gdextension manifest and the parameter sheet
demo/main.tscn           a floor, two walls and one PlayerBody2D
docs/test-output.txt     a recorded run of the test binary
```

## The split

`src/core` never includes a Godot header. It reaches the world through one
interface:

```cpp
class CollisionQuery {
public:
	virtual ~CollisionQuery() = default;
	virtual bool overlaps_box(const Vec2 &p_min, const Vec2 &p_max) const = 0;
};
```

The engine answers that question with `PhysicsDirectSpaceState2D`. The tests answer
it with a tile grid. Both drive the *same* `PlayerCore`, so a rule that passes in
the test binary behaves identically in the game, and the physics regression runs in
milliseconds with no engine attached.

The contract is deliberately exact: **two boxes that share only an edge do not
overlap.** This is the reason the port exists. The GDScript version had to shrink
the collider before every query (`SHAPE_INSET`) and add a wall-snapping margin
(`CONTACT_MARGIN`) because the engine refuses zero-clearance motion, and after
those patches a body still could not pass a gap exactly its own width. Stating the
convention once, in the interface, removes both patches and the whole class of bug
they were papering over.

## Frame order

One fixed step, 1/60s, in this order — the order is part of the behaviour, not an
implementation detail:

1. sample input (presses are buffered, `forceMoveX` is applied)
2. tick timers (a dash freeze gates everything below it)
3. update facing
4. mode update — movement, gravity, jump decisions
5. move — one pass, one whole pixel at a time
6. resolve collisions — landing, corner correction, dash slide

Nothing outside step 5 touches position and nothing outside step 4 touches
velocity, which is what makes the advanced techniques reproducible rather than
tuned. Super, Hyper, Ultra and reverse Hyper are not separate moves in the code:
they fall out of three rules — the dash slide, the super jump, and ducking —
interacting with the order above.

Positions stay on the integer lattice. Every move is split into whole pixels with
the remainder carried in an accumulator, mirroring the reference's `MoveHExact` /
`MoveVExact`. That is what lets the collision probes be exact: a body at `x.5`
resting against a wall would read as overlapping it.

## Building

Requires Python 3 with SCons and a C++17 compiler. `godot-cpp` is a submodule and
is built by the same SCons invocation:

```sh
git clone --recurse-submodules https://github.com/leorixan/platformer-gdext.git
cd platformer-gdext
scons                    # builds the extension into the Godot project's addon folder
scons tests              # builds the test binary
scons tests run_tests=yes
```

The shared library is written to `addons/platformer_core/bin/`, which is where
`addons/platformer_core/platformer_core.gdextension` looks for it, so the
`project.godot` in this repository loads it with no copy step. To build into a
different project instead, override the output directory:

```sh
scons plugin_bin=../your-game/addons/platformer_core/bin
```

The test binary links nothing from godot-cpp. It is `src/core` and `src/gfx`
compiled a second time, and it builds on a machine that has never built the engine
bindings. The command it runs is:

```sh
g++ -std=c++17 -Wall -Wextra -I src/core -I src/gfx -I tests \
    tests/*.cpp src/core/*.cpp src/gfx/*.cpp -o platformer_tests
```

## Using it

Add a `PlayerBody2D` node to a scene, on a physics layer set to the same value as
the node's `solid_mask` property. The node's origin is the **bottom centre** of the
body box — the feet — and it maps straight onto the core's position.

It deliberately does not call `move_and_slide`. The core moves the body itself, and
the node only publishes the result to `global_position`; letting the engine resolve
collisions as well would fight the exact geometry above.

```
params_path    where to read the parameter sheet from (CSV)
solid_mask     which physics layers count as solid
debug_draw     outline the body box
technique      the last technique the core produced, for a debug label
state          NORMAL / DASH
dash_count, facing, is_ducking
teleport_to(), respawn()
```

Input actions: `move_left`, `move_right`, `move_up`, `move_down`, `jump`, `dash`,
`reload_config` — defined in `project.godot`.

## The rasterizer

`src/gfx` is a small software renderer with no dependency on the simulation beside
it and none on any engine — it carries its own vector and colour types so it can be
lifted out of this repository as it stands. It draws into a plain array of pixels,
which is what makes the output testable: a test asserts the exact colour of a
pixel after a draw call, with no window, no GPU and no screenshot to compare
against.

```cpp
gfx::Framebuffer target(320, 180);
target.clear(gfx::Color(0.05f, 0.05f, 0.08f));
target.fill_triangle(gfx::Vec2(40.0f, 20.0f), gfx::Vec2(280.0f, 60.0f),
		gfx::Vec2(160.0f, 160.0f), gfx::Color(0.9f, 0.4f, 0.2f));
```

It covers lines (Bresenham), flat and textured triangles (barycentric coverage
with the top-left fill rule, so two triangles sharing an edge cover each pixel once
and the seam does not darken), bilinear texture sampling with edge clamping, and
source-over alpha compositing.

## Tests

doctest, in `tests/`. Each file covers one area — movement and gravity, jumping,
dashing, the derived techniques, the tile grid, the rasterizer — and asserts against
values written as literals, taken from the reference or from measurement, not against
whatever the code happens to produce.

83 test cases, 400 assertions, all passing; the binary runs in a few milliseconds.
Building it takes about thirty seconds on one core, and needs nothing but a C++17
compiler — no engine, no bindings:

```sh
g++ -std=c++17 -Wall -Wextra -I src/core -I src/gfx -I tests \
    tests/*.cpp src/core/*.cpp src/gfx/*.cpp -o platformer_tests
./platformer_tests
```

The suite has been mutation-checked by hand: eight breaks were introduced into the
core, seven were caught, and the one that was not (swapping the order of the X and Y
move) turned out to be behaviour-preserving for the geometries the tests build. A
recorded run is in `docs/test-output.txt`.

## Provenance

The movement parameters and the frame order follow the player controller of
*Celeste* (Maddy Thorson, Noel Berry; the C# reference used here is Noel Berry's).
*Celeste* is not open source; its code is publicly readable for study and is not
redistributed with this project.

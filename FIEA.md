<!--
  FIEA · Programming Portfolio · code sample cover note

  HOW TO USE THIS FILE
  Everything marked TODO is a blank only you can fill. Do not paste text from an
  assistant here: the portfolio is checked for authorship, and this file is where a
  reviewer looks first to see whether the voice matches the code.
  Keep it under one page per sample. Plain sentences beat adjectives.
-->

# Sample 1 — platformer core (C++17, engine-free simulation)

**What this is.** A 2D platformer character controller in C++17, shipped to Godot as a
GDExtension. The simulation (`src/core`) contains no Godot header; the engine adapter
(`src/godot`) is the only part that includes `godot_cpp`.

**Where the code is.**

| What | File |
|---|---|
| The one interface the simulation knows about the world | `src/core/collision_query.h` |
| Frame order and the movement rules | `src/core/player_core.cpp` |
| Every tunable, grouped and traced to its source | `src/core/player_config.h` |
| The engine-side implementation of the interface | `src/godot/space_query.cpp` |
| The node that drives the core | `src/godot/player_body_2d.cpp` |
| Headless tests | `tests/` |

---

## 1. The problem / task

<!-- TODO(you): 4–6 sentences. Answer: what did the GDScript version of this controller
     do wrong, and what made it worth solving in C++ instead of patching it further?
     Concrete symptoms are better than abstractions: which move failed, in which
     geometry, how often you hit it. -->

## 2. My approach

<!-- TODO(you): the decisions only you can explain. Suggested points, not prose:
     - why the collision query was inverted into an interface;
     - why the body moves one whole pixel at a time and where the remainder goes;
     - why the frame order is a fixed contract rather than a sequence of calls;
     - how the tests drive the same core without an engine;
     - one thing you tried first that did not work. -->

**The part I would point at first:** the collision contract in
`src/core/collision_query.h` says two boxes that share only an edge do not overlap.
Everything else follows from stating that once: the probes become exact, the
collider is never shrunk before a query, and the tests can use a tile grid instead of
a physics server.

## 3. What came from the reference, and what is mine

The movement constants and the frame order are taken from the publicly readable
*Celeste* player controller (Maddy Thorson, Noel Berry; the C# reference used here is
Noel Berry's). That code is not open source and is not redistributed here. The C++
design — the module split, the `CollisionQuery` interface, the integer-lattice
movement, the headless test binary and the software rasterizer — is mine.

<!-- TODO(you): one or two sentences on why you chose to port rather than write a
     fresh controller, and what that taught you. This is the question a reviewer is
     most likely to ask. -->

## 4. How to verify it

```sh
g++ -std=c++17 -Wall -Wextra -I src/core -I src/gfx -I tests \
    tests/*.cpp src/core/*.cpp src/gfx/*.cpp -o platformer_tests
./platformer_tests
```

Actual output of the suite on 2026-11-07 (also in `docs/test-output.txt`):

```
[doctest] test cases:  83 |  83 passed | 0 failed | 0 skipped
[doctest] assertions: 400 | 400 passed | 0 failed |
[doctest] Status: SUCCESS!
```

The assertions use values written as literals, taken from the reference and from
measurements, not read back from the implementation. Eight deliberate breaks were
introduced into the core to check that the suite notices: seven were caught, one
(swapping the order of the X and Y move) was not, and its behaviour turns out to be
equivalent in the geometries the tests build.

<!-- TODO(you): update this paragraph if you re-run the mutation check, and keep the
     honest one that got away — it reads as competence, not as a weakness. -->

## 5. Known limitations

- The Godot adapter (`src/godot`) is not covered by the test binary; it needs an
  engine to run. Only the simulation and the rasterizer are engine-free.
- The wall-jump and dash-corner techniques are exercised indirectly through velocity
  assertions, not through their named `Technique` labels.
- The build needs `godot-cpp` as a submodule; see `README.md`.

## 6. Reading order

1. `src/core/collision_query.h` — the contract (32 lines).
2. `src/core/player_core.h` — the state and the frame order (212 lines).
3. `src/core/player_core.cpp` — the rules, in the order they run (~660 lines).
4. `tests/test_techniques.cpp` — what a Super / Hyper / Ultra actually is.
5. `src/godot/space_query.cpp` — how the engine answers the one question.

#!/usr/bin/env python
"""
Build file for the platformer controller GDExtension.

    scons                     build the extension into the Godot project
    scons tests               build the headless unit test binary
    scons tests run_tests=yes build it and immediately run it

The simulation lives in src/core and knows nothing about Godot. src/godot is the
adapter that turns it into a node, and it is the only half that needs godot-cpp.
That split is what lets the test binary be built and run with no engine present.

src/gfx is the software rasterizer. It is a module of its own: it does not take part
in the extension, and it is compiled into the test binary only.
"""

import os

# godot-cpp sets up the platform, target, optimization level and compiler flags, and
# hands back an environment with its own headers and static library already on it.
env = SConscript("godot-cpp/SConstruct")

env.Append(CPPPATH=["src/core", "src/godot"])
sources = Glob("src/core/*.cpp") + Glob("src/godot/*.cpp")

# The library is written straight into the Godot project's addon folder, so editing
# C++ and rebuilding is enough -- there is no copy step to forget. The path is
# relative to this file.
plugin_bin = ARGUMENTS.get("plugin_bin", "addons/platformer_core/bin")

if env["platform"] == "macos":
    library = env.SharedLibrary(
        plugin_bin + "/libplatformer_core.{}.{}.framework/libplatformer_core.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
elif env["platform"] == "ios":
    if env["ios_simulator"]:
        library = env.StaticLibrary(
            plugin_bin + "/libplatformer_core.{}.{}.simulator.a".format(env["platform"], env["target"]),
            source=sources,
        )
    else:
        library = env.StaticLibrary(
            plugin_bin + "/libplatformer_core.{}.{}.a".format(env["platform"], env["target"]),
            source=sources,
        )
else:
    prefix = "" if env["platform"] == "windows" else "lib"
    library = env.SharedLibrary(
        plugin_bin + "/" + prefix + "platformer_core" + env["suffix"] + env["SHLIBSUFFIX"],
        source=sources,
    )

Default(library)

# --- headless tests ---------------------------------------------------------
#
# The same src/core that ships in the extension, compiled a second time against the
# doctest binary. godot-cpp's library is dropped from the link line: nothing here
# includes a Godot header, and the tests must keep running on a machine that has
# never built the engine bindings.

test_env = env.Clone()
test_env["LIBS"] = []
test_env["LIBPATH"] = []
test_env.Append(CPPPATH=["src/core", "src/gfx", "tests", "tests/third_party"])

if "MSVC_VERSION" in test_env:
    test_env.Append(CXXFLAGS=["/W3"])
else:
    test_env.Append(CXXFLAGS=["-Wall", "-Wextra", "-Wno-unused-parameter"])

test_sources = Glob("tests/*.cpp") + Glob("src/core/*.cpp") + Glob("src/gfx/*.cpp")
test_binary = test_env.Program("build/tests/platformer_tests", test_sources)

test_alias = Alias("tests", test_binary)
AlwaysBuild(test_alias)

if ARGUMENTS.get("run_tests", "no") == "yes":
    test_env.AddPostAction(test_binary, test_binary[0].abspath)

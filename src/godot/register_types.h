#pragma once

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

// Called once when the library is loaded and once when it is unloaded. The only
// level this extension cares about is SCENE, which is where node classes become
// available to the engine.
void initialize_platformer_core(godot::ModuleInitializationLevel p_level);
void uninitialize_platformer_core(godot::ModuleInitializationLevel p_level);

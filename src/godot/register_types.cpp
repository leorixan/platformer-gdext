#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "player_body_2d.h"

using namespace godot;

void initialize_platformer_core(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(platformer::PlayerBody2D);
}

void uninitialize_platformer_core(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
// The symbol named in platformer_core.gdextension. Everything the engine needs to
// know is handed back through the initialization object: which levels this library
// wants, and which functions set them up.
GDExtensionBool GDE_EXPORT platformer_core_library_init(
		GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library,
		GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_object(p_get_proc_address, p_library, r_initialization);

	init_object.register_initializer(initialize_platformer_core);
	init_object.register_terminator(uninitialize_platformer_core);
	init_object.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_object.init();
}
}

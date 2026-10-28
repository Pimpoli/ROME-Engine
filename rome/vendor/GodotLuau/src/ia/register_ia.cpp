// ════════════════════════════════════════════════════════════════════
//  Punto de entrada de la GDExtension OPCIONAL "GodotLuau IA".
//
//  Es un binario aparte del motor a proposito: llama.cpp pesa ~10 MB y no
//  tiene por que pagarlo quien no vaya a usar el chat. Si esta carpeta no
//  existe, el panel simplemente ofrece descargarla.
// ════════════════════════════════════════════════════════════════════
#include "gl_ia.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void gl_ia_init(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    ClassDB::register_class<GLLocalAI>();
    UtilityFunctions::print("[GodotLuau IA] Motor local listo (llama.cpp).");
}

void gl_ia_uninit(ModuleInitializationLevel) {}

extern "C" {
GDExtensionBool GDE_EXPORT godot_luau_ia_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    const GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization* r_initialization)
{
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(gl_ia_init);
    init_obj.register_terminator(gl_ia_uninit);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
}

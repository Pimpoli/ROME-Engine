#include "gl_cabeceras.h"

// Registro de clases (10/11): tween, sonido, interacción, entrada y animación.
// Ver el final de gl_cabeceras.h.
void gl_registro_juego() {
    // TweenService
    //// TweenService
    ClassDB::register_class<RobloxTween>();
    ClassDB::register_class<TweenService>();

    // Sound
    //// Sonido
    ClassDB::register_internal_class<RobloxSound>();  ClassDB::register_class<Sound>();
    ClassDB::register_internal_class<RobloxSoundGroup>();  ClassDB::register_class<SoundGroup>();

    // Interactive
    //// Elementos interactivos
    ClassDB::register_class<ClickDetector>();
    ClassDB::register_class<ProximityPrompt>();
    ClassDB::register_class<SpawnLocation>();

    // Billboard / Surface / Motor6D / Tool
    //// Billboard / Surface / Motor6D / Herramienta
    ClassDB::register_class<BillboardGui>();
    ClassDB::register_class<SurfaceGui>();
    ClassDB::register_class<Motor6D>();
    ClassDB::register_internal_class<RobloxTool>();  ClassDB::register_class<Tool>();
    ClassDB::register_class<Backpack>();

    // Input / Collection
    //// Entrada / Colección
    ClassDB::register_class<UserInputService>();
    ClassDB::register_class<CollectionService>();
    ClassDB::register_class<TextService>();

    // Animation
    //// Animación
    ClassDB::register_internal_class<GLAnimador>();
    ClassDB::register_class<AnimationTrack>();
    ClassDB::register_class<AnimationObject>();
}

// Registro de clases (11/11): el resto de clases estilo Roblox. Ver el final de
// gl_cabeceras.h.
void gl_registro_extra() {
    // Clases "estilo Roblox" que faltaban (versión funcional básica)
    //// Ver src/core/roblox_extra.h — nodos puente con _gl_bridge
    gl_register_extra_classes();
    gl_register_behavior_classes();
    gl_register_effects_classes();
    gl_register_viewport_classes();   // ViewportFrame, Camera y WorldModel (gl_viewport_frame.h)

    // EditableImage / EditableMesh (Object de Roblox, no nodos) y su gestor
    // de subidas. Ver gl_puente_editables.h.
    ClassDB::register_internal_class<GLEditableImage>();
    ClassDB::register_internal_class<GLEditableMesh>();
    ClassDB::register_internal_class<GLEditablesGestor>();
}

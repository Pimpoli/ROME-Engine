#ifndef ROBLOX_EXTRA_H
#define ROBLOX_EXTRA_H

// ════════════════════════════════════════════════════════════════════
//  Clases "estilo Roblox" que faltaban, en versión FUNCIONAL BÁSICA.
//  Cada una es un nodo real, creable con Instance.new(...), que guarda sus
//  propiedades por el puente genérico "_gl_bridge" (ver luau_api.h). El
//  comportamiento visual/físico avanzado queda como "por mejorar".
//
//  - Modificadores de UI  → base Node   (no renderizan solos)
//  - Efectos / mundo 3D   → base Node3D (posicionables)
//  - Constraints modernos → base Node3D
//  - Efectos de sonido    → base Node
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/sub_viewport_container.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "gl_runtime.h"   // gl_xf/gl_pos: transform seguro fuera del arbol
#include "roblox_part.h"  // la malla de una pieza ya no es un nodo hijo

using namespace godot;

// Macro: define un nodo puente mínimo (guarda propiedades vía _gl_bridge).
#define GL_BRIDGE_NODE(NAME, BASE)                       \
    class NAME : public BASE {                           \
        GDCLASS(NAME, BASE);                             \
    protected:                                           \
        static void _bind_methods() {}                   \
    public:                                              \
        NAME() { set_meta("_gl_bridge", true); }         \
    };

// ── Modificadores y contenedores de UI ────────────────────────────────
// (UICorner, UIListLayout, UIStroke, UIScale, UIAspectRatioConstraint y
//  UIGridLayout tienen comportamiento real en roblox_behavior.h)
// UITableLayout y UITextSizeConstraint son funcionales en gl_ui_modifiers.h
// UIGradient dejó de ser stub: es funcional en roblox_behavior.h (pinta el fondo).
// ViewportFrame (GuiObject entero que dibuja su contenido 3D, y el retrato del
// menu con Subject) vive en src/ui/gl_viewport_frame.h.
GL_BRIDGE_NODE(VideoFrame, Node)
// RobloxCanvasGroup ahora es funcional en roblox_behavior.h

// ── Efectos visuales / objetos de mundo ───────────────────────────────
// (ParticleEmitter tiene comportamiento real en roblox_behavior.h)
// Beam, Trail (roblox_cintas.h) y Highlight (roblox_highlight.h) ya dibujan.
// Smoke, Fire, Sparkles, Explosion → funcionales en roblox_effects.h
GL_BRIDGE_NODE(SelectionBox, Node3D)
GL_BRIDGE_NODE(SelectionSphere, Node3D)
GL_BRIDGE_NODE(Clouds, Node3D)
GL_BRIDGE_NODE(RobloxDecal, Node3D)       // "Decal" colisiona con Godot
GL_BRIDGE_NODE(RobloxTexture, Node3D)     // "Texture" colisiona con Godot
// MaterialVariant: sus propiedades se guardan como metas (las del archivo con su
// nombre, las de un script con _glp_); quien las lee es gl_variantes.h. Al
// entrar en el arbol avisa a las piezas que la usan: una variante creada por un
// script DESPUES de ponerle el nombre a la pieza tiene que verse igual.
inline void gl_variantes_refrescar(Node* variante);   // gl_variantes.h
inline void gl_variantes_apuntar(Node* variante);
class MaterialVariant : public Node {
    GDCLASS(MaterialVariant, Node);
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_refrescar_piezas"), &MaterialVariant::_gl_refrescar_piezas);
    }
public:
    // Las que entran en el mismo frame se repintan juntas: la primera llamada
    // diferida atiende a todas y las demas no encuentran nada pendiente.
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE && !Engine::get_singleton()->is_editor_hint()) {
            gl_variantes_apuntar(this);
            call_deferred("_gl_refrescar_piezas");
        }
    }
    void _gl_refrescar_piezas() { gl_variantes_refrescar(nullptr); }
    MaterialVariant() { set_meta("_gl_bridge", true); }
};

// ── Constraints modernos (reemplazan a los BodyMovers) ────────────────
// (VectorForce, Torque, LinearVelocity, AngularVelocity, AlignPosition,
//  AlignOrientation y LineForce tienen física real en roblox_behavior.h)
// RopeConstraint → funcional en roblox_effects.h
GL_BRIDGE_NODE(PrismaticConstraint, Node3D)
GL_BRIDGE_NODE(CylindricalConstraint, Node3D)
GL_BRIDGE_NODE(PlaneConstraint, Node3D)
GL_BRIDGE_NODE(TorsionSpringConstraint, Node3D)
GL_BRIDGE_NODE(RigidConstraint, Node3D)
GL_BRIDGE_NODE(NoCollisionConstraint, Node3D)
GL_BRIDGE_NODE(UniversalConstraint, Node3D)

// ── Efectos de sonido y nueva API de audio ────────────────────────────
// (Reverb/Echo/Equalizer/Distortion/Compressor/PitchShift/Chorus SoundEffect
//  tienen efecto de audio real en roblox_behavior.h)
// FlangeSoundEffect ahora es funcional en roblox_behavior.h
GL_BRIDGE_NODE(TremoloSoundEffect, Node)
GL_BRIDGE_NODE(AudioPlayer, Node)
GL_BRIDGE_NODE(AudioEmitter, Node)
// AudioListener ahora es funcional en roblox_behavior.h
GL_BRIDGE_NODE(AudioFader, Node)
GL_BRIDGE_NODE(AudioDeviceInput, Node)
GL_BRIDGE_NODE(AudioDeviceOutput, Node)
GL_BRIDGE_NODE(Wire, Node)

// ── Asientos ──────────────────────────────────────────────────────────
GL_BRIDGE_NODE(Seat, Node3D)
GL_BRIDGE_NODE(VehicleSeat, Node3D)

#undef GL_BRIDGE_NODE

// Registro de todas las clases de este header (llamado desde register_types)
inline void gl_register_extra_classes() {
    // UIGradient se registra en gl_register_behavior_classes (ya no es stub).
    // ViewportFrame, Camera y WorldModel: gl_register_viewport_classes.
    ClassDB::register_class<VideoFrame>();

    ClassDB::register_class<SelectionBox>();
    ClassDB::register_class<SelectionSphere>();
    ClassDB::register_class<Clouds>();
    ClassDB::register_class<RobloxDecal>();
    ClassDB::register_class<RobloxTexture>();
    ClassDB::register_class<MaterialVariant>();

    ClassDB::register_class<PrismaticConstraint>();
    ClassDB::register_class<CylindricalConstraint>();
    ClassDB::register_class<PlaneConstraint>();
    ClassDB::register_class<TorsionSpringConstraint>();
    ClassDB::register_class<RigidConstraint>();
    ClassDB::register_class<NoCollisionConstraint>();
    ClassDB::register_class<UniversalConstraint>();

    ClassDB::register_class<TremoloSoundEffect>();
    ClassDB::register_class<AudioPlayer>();
    ClassDB::register_class<AudioEmitter>();
    ClassDB::register_class<AudioFader>();
    ClassDB::register_class<AudioDeviceInput>();
    ClassDB::register_class<AudioDeviceOutput>();
    ClassDB::register_class<Wire>();

    ClassDB::register_class<Seat>();
    ClassDB::register_class<VehicleSeat>();
}

#endif // ROBLOX_EXTRA_H

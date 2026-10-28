#ifndef GL_AVATAR_H
#define GL_AVATAR_H

// ════════════════════════════════════════════════════════════════════
//  El personaje por defecto: StarterCharacter si el juego lo trae y, si no,
//  el avatar R6 o R15 propio (gl_avatar_rig.h, hecho en Blender), con sus
//  Motor6D de Roblox. Lo anima el Animate de StarterCharacterScripts. Aqui
//  tambien: la capsula a su medida y el aplanado estilo Roblox.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node3d.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/capsule_shape3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <vector>
#include "gl_debug.h"
#include "gl_animador.h"
#include "roblox_motor6d.h"
#include "gl_avatar_rig.h"   // los avatares R6/R15 de Blender (F8)

using namespace godot;

// Busca StarterPlayer/StarterCharacter desde cualquier nodo del arbol.
inline Node3D* gl_clone_starter_character(Node* ctx) {
    if (!ctx || !ctx->is_inside_tree()) return nullptr;
    Node* root = (Node*)ctx->get_tree()->get_root();
    // StarterPlayer puede estar en cualquier DataModel de la escena
    struct Finder {
        static Node* find(Node* n) {
            if (!n) return nullptr;
            if (n->get_name() == StringName("StarterPlayer")) return n;
            for (int i = 0; i < n->get_child_count(); i++)
                if (Node* r = find(n->get_child(i))) return r;
            return nullptr;
        }
    };
    Node* sp = Finder::find(root);
    if (!sp) return nullptr;
    Node* sc = sp->get_node_or_null("StarterCharacter");
    if (!sc) return nullptr;
    Node* dup = sc->duplicate();
    if (!dup) return nullptr;
    Node3D* d3 = gl_cast<Node3D>(dup);
    if (!d3) {
        // Envolver en un Node3D si la raiz clonada no es 3D
        Node3D* wrap = memnew(Node3D);
        wrap->add_child(dup);
        d3 = wrap;
    }
    d3->set_name("Character");
    d3->set_position(Vector3());
    GL_DEBUG_PRINT("[GodotLuau] StarterCharacter encontrado: usado como personaje.");
    return d3;
}

// Personaje por defecto: StarterCharacter > avatar R15/R6 (el que pida el
// juego, gl_avatar_rig.h) > nullptr (el caller pone una capsula).
inline Node3D* gl_build_character(Node* ctx) {
    if (Node3D* sc = gl_clone_starter_character(ctx)) return sc;
    return gl_build_rig(gl_rig_es_r15(ctx));
}

// La capsula de fisica y el Humanoid a la medida del avatar: del suelo a lo
// alto de la cabeza, con la HRP en el origen (como en Roblox). Devuelve lo que
// hay del origen a los pies (para subir el punto de aparicion); 1 si el
// personaje no es uno de estos avatares (la capsula de 2 de siempre).
inline float gl_ajustar_al_avatar(Node* personaje, CollisionShape3D* forma, Node* humanoide) {
    if (!personaje) return 1.0f;
    const String rig = personaje->has_meta("_gl_rig") ? String(personaje->get_meta("_gl_rig")) : String();
    if (rig.is_empty()) return 1.0f;
    const GLRigDatos* d = gl_rig_datos(rig == "R15");
    if (!d) return 1.0f;
    if (forma) {
        Ref<CapsuleShape3D> cs = forma->get_shape();
        if (cs.is_valid()) {
            // Ancho de la HRP de Roblox (2 studs): los huecos por los que se
            // pasa alli son los mismos aqui.
            cs->set_radius(1.0f);
            cs->set_height(d->pies + d->cima);
        }
        forma->set_position(Vector3(0.0f, (d->cima - d->pies) * 0.5f, 0.0f));
    }
    if (humanoide) {
        humanoide->call("set_rig_type", rig == "R15" ? 1 : 0);
        humanoide->call("set_hip_height", rig == "R15" ? d->hip : 0.0f);
    }
    return d->pies;
}

// StarterPlayer manda sobre los valores por defecto del Humanoid, como en
// Roblox: un place con CharacterWalkSpeed = 30 arranca a 30, y uno con
// CharacterJumpHeight 14,4 y UseJumpPower en false salta eso (Build a Mountain).
inline void gl_aplicar_starter_player(Node* sp, Node* hum) {
    if (!sp || !hum) return;
    static const char* kNum[][2] = {
        { "CharacterWalkSpeed", "set_walk_speed" }, { "CharacterJumpPower", "set_jump_power" },
        { "CharacterJumpHeight", "set_jump_height" }, { "CharacterMaxSlopeAngle", "set_max_slope_angle" } };
    for (const auto& k : kNum) {
        const Variant v = gl_starter_valor(sp, k[0]);
        if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) hum->call(k[1], (float)v);
    }
    const Variant uj = gl_starter_valor(sp, "CharacterUseJumpPower");
    if (uj.get_type() == Variant::BOOL) hum->call("set_use_jump_power", (bool)uj);
}

// ── Aplanado estilo Roblox ───────────────────────────────────────────
// En Roblox el Character es UN Model plano: HumanoidRootPart y las partes
// (Head/Torso/Left Arm/...) son hijos DIRECTOS. El rig R6 propio los tenía
// anidados (Character > *Pivot > Mesh) y HumanoidRootPart bajo el rig, así que
// Character:FindFirstChild("HumanoidRootPart") daba nil y rompía frameworks
// portados de Roblox. Esto sube las partes + HRP a hijos directos del "wrapper"
// (el RobloxPlayer local, que es lo que devuelve .Character) y descarta el root
// vacío. reparent(keep_global) hornea la escala/offset del rig → SIN cambio
// visual. Solo actúa sobre el rig R6 propio (meta _gl_r6rig); un StarterCharacter
// del usuario se deja intacto.
inline void gl_flatten_r6_character(Node* wrapper, Node* character) {
    if (!wrapper || !character) return;
    if (!character->has_meta("_gl_r6rig")) return;
    if (!wrapper->is_inside_tree() || !character->is_inside_tree()) return;
    std::vector<Node*> kids;
    for (int i = 0; i < character->get_child_count(); i++)
        if (Node* c = character->get_child(i)) kids.push_back(c);
    for (Node* c : kids) c->reparent(wrapper, true);   // keep_global: hornea la transform del rig
    character->queue_free();
}

// Para el muñeco del jugador remoto: sube SOLO el HumanoidRootPart del rig
// visual a hijo directo del cuerpo (para que .Character:FindFirstChild(
// "HumanoidRootPart") resuelva en el servidor), dejando el rig "Visual" intacto
// (animador y bob del muñeco siguen colgando de "Visual").
inline void gl_lift_hrp(Node* wrapper, Node* visual) {
    if (!wrapper || !visual || !wrapper->is_inside_tree()) return;
    if (Node* hrp = visual->get_node_or_null("HumanoidRootPart")) hrp->reparent(wrapper, true);
}

#endif // GL_AVATAR_H

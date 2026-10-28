#ifndef ROBLOX_ANIMATION_H
#define ROBLOX_ANIMATION_H

// ════════════════════════════════════════════════════════════════════
//  Animation (AnimationObject) y la replica de animaciones a los munecos.
//  AnimationTrack y el animador por capas viven en gl_animador.h (F7).
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/core/class_db.hpp>
#include "gl_animador.h"

using namespace godot;

// Reproduce (o para) una animacion en un personaje CUALQUIERA —tipicamente el
// muneco de otro jugador—, por el mismo camino que LoadAnimation, para que se
// vea lo mismo que hizo su dueño (1.14.12; F7: con el animador por capas).
inline void gl_anim_apply(Node* character, const String& anim_id, const String& anim_name,
                          float speed, bool looped, bool play) {
    if (!character || anim_id.is_empty()) return;
    Node* hum = character->get_node_or_null(NodePath("Humanoid"));
    if (!hum) return;
    GLAnimador* a = gl_animador_de(hum, play);
    if (!a) return;
    AnimationTrack* t = nullptr;
    for (const ObjectID& id : a->pistas) {
        AnimationTrack* x = gl_cast<AnimationTrack>(ObjectDB::get_instance(id));
        if (x && x->anim_id == anim_id) { t = x; break; }
    }
    if (!play) {
        if (t) t->gl_stop(0.1f);
        return;
    }
    if (!t) t = gl_cargar_animacion(hum, nullptr, anim_id, anim_name, anim_name);
    if (!t) return;
    t->gl_poner_bucle(looped);
    t->gl_play(0.1f, 1.0f, speed);
}

// ────────────────────────────────────────────────────────────────────
//  AnimationObject — la Animation de Roblox (AnimationId).
//  Uso: local anim = Instance.new("Animation")
//       anim.AnimationId = "rbxassetid://123"
//       animator:LoadAnimation(anim)
// ────────────────────────────────────────────────────────────────────
class AnimationObject : public Node {
    GDCLASS(AnimationObject, Node);

    String anim_id   = "";
    String anim_name = "default";

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_animation_id", "id"), &AnimationObject::set_animation_id);
        ClassDB::bind_method(D_METHOD("get_animation_id"),       &AnimationObject::get_animation_id);
        ClassDB::bind_method(D_METHOD("set_anim_name", "n"),    &AnimationObject::set_anim_name);
        ClassDB::bind_method(D_METHOD("get_anim_name"),          &AnimationObject::get_anim_name);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "AnimationId"), "set_animation_id", "get_animation_id");
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "Name"),        "set_anim_name",    "get_anim_name");
    }

public:
    void set_animation_id(const String& id)  { anim_id = id; }
    String get_animation_id() const          { return anim_id; }
    void set_anim_name(const String& n)      { anim_name = n; }
    String get_anim_name() const             { return anim_name; }
};

#endif // ROBLOX_ANIMATION_H

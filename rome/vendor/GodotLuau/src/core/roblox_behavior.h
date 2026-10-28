#ifndef ROBLOX_BEHAVIOR_H
#define ROBLOX_BEHAVIOR_H

// ════════════════════════════════════════════════════════════════════
//  Clases nuevas con COMPORTAMIENTO real (no solo holders).
//  - UICorner    : redondea las esquinas del GUI padre
//  - UIListLayout: ordena los hijos Control del padre en lista
//  CornerRadius/Padding aceptan número o UDim (se usa el Offset) — ver el
//  puente en luau_api.h.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/control.hpp>
#include "gl_stream.h"
#include "gl_servicios.h"   // gl_en_workspace: solo se dibuja lo del mundo
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include "roblox_bodymovers.h"   // gl_cuerpo_que_mueve / gl_empujar_humanoid
#include "roblox_gui.h"          // RobloxFrame, la base de CanvasGroup
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/style_box_texture.hpp>
#include <godot_cpp/classes/gradient.hpp>
#include <godot_cpp/classes/gradient_texture2d.hpp>
#include <godot_cpp/classes/gpu_particles3d.hpp>
#include <godot_cpp/classes/particle_process_material.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_effect_reverb.hpp>
#include <godot_cpp/classes/audio_effect_delay.hpp>
#include <godot_cpp/classes/audio_effect_eq6.hpp>
#include <godot_cpp/classes/audio_effect_distortion.hpp>
#include <godot_cpp/classes/audio_effect_compressor.hpp>
#include <godot_cpp/classes/audio_effect_pitch_shift.hpp>
#include <godot_cpp/classes/audio_effect_chorus.hpp>
#include <godot_cpp/classes/audio_listener3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include "roblox_sound.h"
#include "gl_ui_modifiers.h"   // UICorner/UIListLayout/UIPadding/... (semantica real de Roblox)

using namespace godot;

// ParticleEmitter vive en roblox_particulas.h (las propiedades de Roblox enteras).
#include "roblox_particulas.h"

// ── *SoundEffect ──────────────────────────────────────────────────────
//  Al entrar en escena, añade el AudioEffect de Godot correspondiente al bus de
//  su padre: el de su SoundGroup, o uno propio si cuelga de un Sound (antes iba
//  a Master y cambiaba el audio de TODO el juego). Se apunta el bus por nombre:
//  quitar otros buses corre los indices.
inline String gl_bus_de_efecto(Node* padre) {
    if (RobloxSoundGroup* g = gl_cast<RobloxSoundGroup>(padre)) return g->gl_bus();
    if (RobloxSound* s = gl_cast<RobloxSound>(padre)) return s->gl_bus_para_efecto();
    return String("Master");
}
inline void gl_bus_quitar_efecto(const String& bus, const Ref<AudioEffect>& fx) {
    AudioServer* as = AudioServer::get_singleton();
    const int bi = as ? as->get_bus_index(bus) : -1;
    if (bi < 0 || fx.is_null()) return;
    for (int i = as->get_bus_effect_count(bi) - 1; i >= 0; --i)
        if (as->get_bus_effect(bi, i).ptr() == fx.ptr()) { as->remove_bus_effect(bi, i); break; }
}
#define GL_SOUND_EFFECT(NAME, EFFECTTYPE)                                    \
    class NAME : public Node {                                               \
        GDCLASS(NAME, Node);                                                 \
        String gl_bus;                                                       \
        Ref<EFFECTTYPE> gl_fx;                                               \
    protected: static void _bind_methods() {}                               \
        void _notification(int p_what) {                                     \
            if (Engine::get_singleton()->is_editor_hint()) return;          \
            if (p_what == NOTIFICATION_ENTER_TREE) {                         \
                gl_bus = gl_bus_de_efecto(get_parent());                     \
                AudioServer* as = AudioServer::get_singleton();             \
                int bi = as->get_bus_index(gl_bus); if (bi < 0) { bi = 0; gl_bus = "Master"; } \
                if (gl_fx.is_null()) gl_fx.instantiate();                    \
                as->add_bus_effect(bi, gl_fx);                               \
            } else if (p_what == NOTIFICATION_EXIT_TREE) {                   \
                /* Sin esto el efecto se quedaba en el bus PARA SIEMPRE: cada \
                   clon apilaba otro reverb/echo y el audio se degradaba. */ \
                gl_bus_quitar_efecto(gl_bus, gl_fx);                         \
            }                                                                \
        }                                                                    \
    public:                                                                   \
        NAME() { set_meta("_gl_bridge", true); }                            \
    };
GL_SOUND_EFFECT(ReverbSoundEffect,     AudioEffectReverb)
GL_SOUND_EFFECT(EchoSoundEffect,       AudioEffectDelay)
GL_SOUND_EFFECT(EqualizerSoundEffect,  AudioEffectEQ6)
GL_SOUND_EFFECT(DistortionSoundEffect, AudioEffectDistortion)
GL_SOUND_EFFECT(CompressorSoundEffect, AudioEffectCompressor)
GL_SOUND_EFFECT(PitchShiftSoundEffect, AudioEffectPitchShift)
GL_SOUND_EFFECT(ChorusSoundEffect,     AudioEffectChorus)
// Flange, aproximado con chorus. Antes iba aparte y no se quitaba nunca del bus.
GL_SOUND_EFFECT(FlangeSoundEffect,     AudioEffectChorus)
#undef GL_SOUND_EFFECT

// ── Constraints modernos con física REAL ─────────────────────────────
//  Como en Roblox, empujan la pieza de su Attachment0 (y usan ese attachment
//  como punto y como marco de RelativeTo). Antes solo valia el padre del
//  constraint y los attachments no existian para ellos: un LinearVelocity en
//  la carpeta de efectos con Attachment0 en la HRP (el dash tipico) no hacia
//  nada. Sin Attachment0 sigue valiendo el padre, como antes. Llevan la meta
//  _gl_mover: su Position/CFrame es el OBJETIVO, no donde esta el nodo.
//  Enum.ActuatorRelativeTo: 0 Attachment0, 1 Attachment1, 2 World.
struct GLObjetivoMover {
    Node*   pieza = nullptr;   // a quien se empuja
    Node3D* att0  = nullptr;
    Node3D* att1  = nullptr;
};
inline GLObjetivoMover gl_objetivo_mover(const Node* c) {
    static const StringName k0s("_glo_Attachment0"), k0i("Attachment0");
    static const StringName k1s("_glo_Attachment1"), k1i("Attachment1");
    GLObjetivoMover o;
    o.att0 = gl_cast<Node3D>(gl_ref_de(c, k0s, k0i));
    o.att1 = gl_cast<Node3D>(gl_ref_de(c, k1s, k1i));
    o.pieza = o.att0 ? o.att0->get_parent() : c->get_parent();
    return o;
}
inline Basis gl_marco_relativo(const GLObjetivoMover& o, int relativo) {
    if (relativo == 0 && o.att0 && o.att0->is_inside_tree()) return o.att0->get_global_transform().basis.orthonormalized();
    if (relativo == 1 && o.att1 && o.att1->is_inside_tree()) return o.att1->get_global_transform().basis.orthonormalized();
    return Basis();
}
// Velocidad actual de lo que se empuja: el cuerpo (raiz del conjunto) o, si es
// la HRP de un personaje, el personaje.
inline bool gl_vel_objetivo(Node* pieza, Vector3& v, float& masa) {
    if (Node3D* pers = gl_character_root_of_hrp(pieza)) {
        if (CharacterBody3D* cb = gl_cast<CharacterBody3D>(pers)) {
            v = cb->get_velocity();
            masa = GL_MASA_PERSONAJE;
            return true;
        }
    }
    RigidBody3D* rb = gl_cuerpo_que_mueve(pieza);
    if (!rb || rb->is_freeze_enabled()) return false;
    v = rb->get_linear_velocity();
    masa = Math::max(rb->get_mass(), 0.001f);
    return true;
}
// Pone la velocidad nueva: al cuerpo o, en un personaje, a traves de su Humanoid.
inline void gl_poner_vel(Node* pieza, const Vector3& v) {
    if (gl_humanoid_de_hrp(pieza) && !gl_cast<RigidBody3D>(pieza)) {
        gl_empujar_humanoid(pieza, v, Vector3(1, 1, 1));
        return;
    }
    if (RigidBody3D* rb = gl_cuerpo_que_mueve(pieza)) {
        rb->set_linear_velocity(v);
        gl_empujar_humanoid(pieza, v, Vector3(1, 1, 1));   // NPC: que su Humanoid no lo pise
    }
}
// Acerca v a `objetivo` sin pasar de la fuerza maxima (MaxForce / masa por paso).
inline Vector3 gl_hacia_con_fuerza(const Vector3& v, const Vector3& objetivo, float fuerza_max, float masa, double delta) {
    if (fuerza_max >= 1e9f) return objetivo;
    Vector3 dv = objetivo - v;
    const float tope = fuerza_max / Math::max(masa, 0.001f) * (float)delta;
    if (dv.length() > tope) dv = dv.normalized() * tope;
    return v + dv;
}

#define GL_MOVER_COMUN(CLASE)                                                             \
    ClassDB::bind_method(D_METHOD("set_Enabled", "v"), &CLASE::set_Enabled);              \
    ClassDB::bind_method(D_METHOD("get_Enabled"),      &CLASE::get_Enabled);              \
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Enabled"), "set_Enabled", "get_Enabled");

// VectorForce: fuerza constante (por defecto en el marco de su Attachment0)
class VectorForce : public Node3D {
    GDCLASS(VectorForce, Node3D);
    Vector3 force = Vector3(0, 0, 0);
    bool enabled = true;
    int  relative_to = 0;
    bool en_centro = false;
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_Force", "v"), &VectorForce::set_Force);
        ClassDB::bind_method(D_METHOD("get_Force"),      &VectorForce::get_Force);
        ClassDB::bind_method(D_METHOD("set_RelativeTo", "v"), &VectorForce::set_RelativeTo);
        ClassDB::bind_method(D_METHOD("get_RelativeTo"),      &VectorForce::get_RelativeTo);
        ClassDB::bind_method(D_METHOD("set_ApplyAtCenterOfMass", "v"), &VectorForce::set_ApplyAtCenterOfMass);
        ClassDB::bind_method(D_METHOD("get_ApplyAtCenterOfMass"),      &VectorForce::get_ApplyAtCenterOfMass);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "Force"), "set_Force", "get_Force");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "RelativeTo", PROPERTY_HINT_ENUM, "Attachment0,Attachment1,World"), "set_RelativeTo", "get_RelativeTo");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ApplyAtCenterOfMass"), "set_ApplyAtCenterOfMass", "get_ApplyAtCenterOfMass");
        GL_MOVER_COMUN(VectorForce)
    }
public:
    void set_Force(Vector3 v) { force = v; }
    Vector3 get_Force() const { return force; }
    void set_RelativeTo(int v) { relative_to = CLAMP(v, 0, 2); }
    int  get_RelativeTo() const { return relative_to; }
    void set_ApplyAtCenterOfMass(bool v) { en_centro = v; }
    bool get_ApplyAtCenterOfMass() const { return en_centro; }
    void set_Enabled(bool v) { enabled = v; }
    bool get_Enabled() const { return enabled; }
    void _physics_process(double) override {
        if (!enabled) return;
        const GLObjetivoMover o = gl_objetivo_mover(this);
        RigidBody3D* rb = gl_cuerpo_que_mueve(o.pieza);
        if (!rb || rb->is_freeze_enabled()) return;
        const Vector3 f = gl_marco_relativo(o, relative_to).xform(force);
        if (en_centro || !o.att0) rb->apply_central_force(f);
        else rb->apply_force(f, o.att0->get_global_position() - rb->get_global_position());
    }
    VectorForce() { set_meta("_gl_bridge", true); set_meta("_gl_mover", true); set_physics_process(true); }
};

// Torque: par constante (por defecto en el marco de su Attachment0)
class Torque : public Node3D {
    GDCLASS(Torque, Node3D);
    Vector3 torque = Vector3(0, 0, 0);
    bool enabled = true;
    int  relative_to = 0;
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_Torque", "v"), &Torque::set_Torque);
        ClassDB::bind_method(D_METHOD("get_Torque"),      &Torque::get_Torque);
        ClassDB::bind_method(D_METHOD("set_RelativeTo", "v"), &Torque::set_RelativeTo);
        ClassDB::bind_method(D_METHOD("get_RelativeTo"),      &Torque::get_RelativeTo);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "Torque"), "set_Torque", "get_Torque");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "RelativeTo", PROPERTY_HINT_ENUM, "Attachment0,Attachment1,World"), "set_RelativeTo", "get_RelativeTo");
        GL_MOVER_COMUN(Torque)
    }
public:
    void set_Torque(Vector3 v) { torque = v; }
    Vector3 get_Torque() const { return torque; }
    void set_RelativeTo(int v) { relative_to = CLAMP(v, 0, 2); }
    int  get_RelativeTo() const { return relative_to; }
    void set_Enabled(bool v) { enabled = v; }
    bool get_Enabled() const { return enabled; }
    void _physics_process(double) override {
        if (!enabled) return;
        const GLObjetivoMover o = gl_objetivo_mover(this);
        RigidBody3D* rb = gl_cuerpo_que_mueve(o.pieza);
        if (rb && !rb->is_freeze_enabled()) rb->apply_torque(gl_marco_relativo(o, relative_to).xform(torque));
    }
    Torque() { set_meta("_gl_bridge", true); set_meta("_gl_mover", true); set_physics_process(true); }
};

// LinearVelocity: mantiene una velocidad lineal. VelocityConstraintMode
// (Vector 0, Line 1, Plane 2): con Line/Plane solo manda en esa direccion o
// en ese plano y lo demas queda libre (la gravedad sigue actuando).
class LinearVelocity : public Node3D {
    GDCLASS(LinearVelocity, Node3D);
    Vector3 vel = Vector3(0, 0, 0);
    bool    enabled = true;
    int     relative_to = 2;
    int     modo = 0;
    Vector3 linea_dir = Vector3(1, 0, 0);
    float   linea_vel = 0.0f;
    Vector2 plano_vel;
    Vector3 tangente1 = Vector3(1, 0, 0);
    Vector3 tangente2 = Vector3(0, 0, 1);
    float   max_force = 1000.0f;
    bool    con_limite = true;
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_VectorVelocity", "v"), &LinearVelocity::set_VectorVelocity);
        ClassDB::bind_method(D_METHOD("get_VectorVelocity"),      &LinearVelocity::get_VectorVelocity);
        ClassDB::bind_method(D_METHOD("set_RelativeTo", "v"),     &LinearVelocity::set_RelativeTo);
        ClassDB::bind_method(D_METHOD("get_RelativeTo"),          &LinearVelocity::get_RelativeTo);
        ClassDB::bind_method(D_METHOD("set_VelocityConstraintMode", "v"), &LinearVelocity::set_VelocityConstraintMode);
        ClassDB::bind_method(D_METHOD("get_VelocityConstraintMode"),      &LinearVelocity::get_VelocityConstraintMode);
        ClassDB::bind_method(D_METHOD("set_LineDirection", "v"),  &LinearVelocity::set_LineDirection);
        ClassDB::bind_method(D_METHOD("get_LineDirection"),       &LinearVelocity::get_LineDirection);
        ClassDB::bind_method(D_METHOD("set_LineVelocity", "v"),   &LinearVelocity::set_LineVelocity);
        ClassDB::bind_method(D_METHOD("get_LineVelocity"),        &LinearVelocity::get_LineVelocity);
        ClassDB::bind_method(D_METHOD("set_PlaneVelocity", "v"),  &LinearVelocity::set_PlaneVelocity);
        ClassDB::bind_method(D_METHOD("get_PlaneVelocity"),       &LinearVelocity::get_PlaneVelocity);
        ClassDB::bind_method(D_METHOD("set_PrimaryTangentAxis", "v"),   &LinearVelocity::set_PrimaryTangentAxis);
        ClassDB::bind_method(D_METHOD("get_PrimaryTangentAxis"),        &LinearVelocity::get_PrimaryTangentAxis);
        ClassDB::bind_method(D_METHOD("set_SecondaryTangentAxis", "v"), &LinearVelocity::set_SecondaryTangentAxis);
        ClassDB::bind_method(D_METHOD("get_SecondaryTangentAxis"),      &LinearVelocity::get_SecondaryTangentAxis);
        ClassDB::bind_method(D_METHOD("set_MaxForce", "v"),       &LinearVelocity::set_MaxForce);
        ClassDB::bind_method(D_METHOD("get_MaxForce"),            &LinearVelocity::get_MaxForce);
        ClassDB::bind_method(D_METHOD("set_ForceLimitsEnabled", "v"), &LinearVelocity::set_ForceLimitsEnabled);
        ClassDB::bind_method(D_METHOD("get_ForceLimitsEnabled"),      &LinearVelocity::get_ForceLimitsEnabled);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "VectorVelocity"), "set_VectorVelocity", "get_VectorVelocity");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "RelativeTo", PROPERTY_HINT_ENUM, "Attachment0,Attachment1,World"), "set_RelativeTo", "get_RelativeTo");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "VelocityConstraintMode", PROPERTY_HINT_ENUM, "Vector,Line,Plane"), "set_VelocityConstraintMode", "get_VelocityConstraintMode");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "LineDirection"),  "set_LineDirection", "get_LineDirection");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,   "LineVelocity"),   "set_LineVelocity", "get_LineVelocity");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "PlaneVelocity"),  "set_PlaneVelocity", "get_PlaneVelocity");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "PrimaryTangentAxis"),   "set_PrimaryTangentAxis", "get_PrimaryTangentAxis");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "SecondaryTangentAxis"), "set_SecondaryTangentAxis", "get_SecondaryTangentAxis");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,   "MaxForce"),       "set_MaxForce", "get_MaxForce");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,    "ForceLimitsEnabled"), "set_ForceLimitsEnabled", "get_ForceLimitsEnabled");
        GL_MOVER_COMUN(LinearVelocity)
    }
public:
    void set_VectorVelocity(Vector3 v) { vel = v; }
    Vector3 get_VectorVelocity() const { return vel; }
    void set_RelativeTo(int v) { relative_to = CLAMP(v, 0, 2); }
    int  get_RelativeTo() const { return relative_to; }
    void set_VelocityConstraintMode(int v) { modo = CLAMP(v, 0, 2); }
    int  get_VelocityConstraintMode() const { return modo; }
    void set_LineDirection(Vector3 v) { linea_dir = v; }
    Vector3 get_LineDirection() const { return linea_dir; }
    void set_LineVelocity(float v) { linea_vel = v; }
    float get_LineVelocity() const { return linea_vel; }
    void set_PlaneVelocity(Vector2 v) { plano_vel = v; }
    Vector2 get_PlaneVelocity() const { return plano_vel; }
    void set_PrimaryTangentAxis(Vector3 v) { tangente1 = v; }
    Vector3 get_PrimaryTangentAxis() const { return tangente1; }
    void set_SecondaryTangentAxis(Vector3 v) { tangente2 = v; }
    Vector3 get_SecondaryTangentAxis() const { return tangente2; }
    void set_MaxForce(float v) { max_force = Math::max(v, 0.0f); }
    float get_MaxForce() const { return max_force; }
    void set_ForceLimitsEnabled(bool v) { con_limite = v; }
    bool get_ForceLimitsEnabled() const { return con_limite; }
    void set_Enabled(bool v) { enabled = v; }
    bool get_Enabled() const { return enabled; }
    void _physics_process(double delta) override {
        if (!enabled) return;
        const GLObjetivoMover o = gl_objetivo_mover(this);
        Vector3 v;
        float masa = 1.0f;
        if (!gl_vel_objetivo(o.pieza, v, masa)) return;
        const Basis m = gl_marco_relativo(o, relative_to);
        Vector3 objetivo;
        if (modo == 1) {          // Line: solo la componente sobre la linea
            const Vector3 d = m.xform(linea_dir).normalized();
            objetivo = v + d * (linea_vel - v.dot(d));
        } else if (modo == 2) {   // Plane: solo las dos tangentes
            const Vector3 t1 = m.xform(tangente1).normalized();
            const Vector3 t2 = m.xform(tangente2).normalized();
            objetivo = v - t1 * v.dot(t1) - t2 * v.dot(t2) + t1 * plano_vel.x + t2 * plano_vel.y;
        } else {
            objetivo = m.xform(vel);
        }
        gl_poner_vel(o.pieza, gl_hacia_con_fuerza(v, objetivo, con_limite ? max_force : 1e30f, masa, delta));
    }
    LinearVelocity() { set_meta("_gl_bridge", true); set_meta("_gl_mover", true); set_physics_process(true); }
};

// AngularVelocity: mantiene una velocidad angular (MaxTorque 0 = sin tope,
// como se comportaba hasta ahora)
class AngularVelocity : public Node3D {
    GDCLASS(AngularVelocity, Node3D);
    Vector3 av = Vector3(0, 0, 0);
    bool  enabled = true;
    int   relative_to = 2;
    float max_torque = 0.0f;
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_AngularVelocity", "v"), &AngularVelocity::set_AngularVelocity);
        ClassDB::bind_method(D_METHOD("get_AngularVelocity"),      &AngularVelocity::get_AngularVelocity);
        ClassDB::bind_method(D_METHOD("set_RelativeTo", "v"),      &AngularVelocity::set_RelativeTo);
        ClassDB::bind_method(D_METHOD("get_RelativeTo"),           &AngularVelocity::get_RelativeTo);
        ClassDB::bind_method(D_METHOD("set_MaxTorque", "v"),       &AngularVelocity::set_MaxTorque);
        ClassDB::bind_method(D_METHOD("get_MaxTorque"),            &AngularVelocity::get_MaxTorque);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "AngularVelocity"), "set_AngularVelocity", "get_AngularVelocity");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "RelativeTo", PROPERTY_HINT_ENUM, "Attachment0,Attachment1,World"), "set_RelativeTo", "get_RelativeTo");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "MaxTorque"), "set_MaxTorque", "get_MaxTorque");
        GL_MOVER_COMUN(AngularVelocity)
    }
public:
    void set_AngularVelocity(Vector3 v) { av = v; }
    Vector3 get_AngularVelocity() const { return av; }
    void set_RelativeTo(int v) { relative_to = CLAMP(v, 0, 2); }
    int  get_RelativeTo() const { return relative_to; }
    void set_MaxTorque(float v) { max_torque = Math::max(v, 0.0f); }
    float get_MaxTorque() const { return max_torque; }
    void set_Enabled(bool v) { enabled = v; }
    bool get_Enabled() const { return enabled; }
    void _physics_process(double delta) override {
        if (!enabled) return;
        const GLObjetivoMover o = gl_objetivo_mover(this);
        RigidBody3D* rb = gl_cuerpo_que_mueve(o.pieza);
        if (!rb || rb->is_freeze_enabled()) return;
        const Vector3 objetivo = gl_marco_relativo(o, relative_to).xform(av);
        rb->set_angular_velocity(gl_hacia_con_fuerza(rb->get_angular_velocity(), objetivo,
                                                     max_torque > 0.0f ? max_torque : 1e30f, rb->get_mass(), delta));
    }
    AngularVelocity() { set_meta("_gl_bridge", true); set_meta("_gl_mover", true); set_physics_process(true); }
};

// AlignPosition: lleva el punto de Attachment0 hasta Attachment1 (Mode
// TwoAttachment, el de serie) o hasta Position (OneAttachment). Con
// RigidityEnabled llega en un paso; si no, se acerca segun Responsiveness, sin
// pasar de MaxForce ni de MaxVelocity. Enum.PositionAlignmentMode: 0 One, 1 Two.
class AlignPosition : public Node3D {
    GDCLASS(AlignPosition, Node3D);
    Vector3 target = Vector3(0, 0, 0);
    double  responsiveness = 10.0;
    bool    enabled = true;
    int     modo = 1;
    float   max_force = 10000.0f;
    float   max_velocity = 1e30f;
    bool    rigido = false;
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_Position", "v"), &AlignPosition::set_Position);
        ClassDB::bind_method(D_METHOD("get_Position"),      &AlignPosition::get_Position);
        ClassDB::bind_method(D_METHOD("set_Responsiveness", "v"), &AlignPosition::set_Responsiveness);
        ClassDB::bind_method(D_METHOD("get_Responsiveness"),      &AlignPosition::get_Responsiveness);
        ClassDB::bind_method(D_METHOD("set_Mode", "v"),           &AlignPosition::set_Mode);
        ClassDB::bind_method(D_METHOD("get_Mode"),                &AlignPosition::get_Mode);
        ClassDB::bind_method(D_METHOD("set_MaxForce", "v"),       &AlignPosition::set_MaxForce);
        ClassDB::bind_method(D_METHOD("get_MaxForce"),            &AlignPosition::get_MaxForce);
        ClassDB::bind_method(D_METHOD("set_MaxVelocity", "v"),    &AlignPosition::set_MaxVelocity);
        ClassDB::bind_method(D_METHOD("get_MaxVelocity"),         &AlignPosition::get_MaxVelocity);
        ClassDB::bind_method(D_METHOD("set_RigidityEnabled", "v"), &AlignPosition::set_RigidityEnabled);
        ClassDB::bind_method(D_METHOD("get_RigidityEnabled"),      &AlignPosition::get_RigidityEnabled);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "Position"),       "set_Position",       "get_Position");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,   "Responsiveness"), "set_Responsiveness", "get_Responsiveness");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "Mode", PROPERTY_HINT_ENUM, "OneAttachment,TwoAttachment"), "set_Mode", "get_Mode");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "MaxForce"),    "set_MaxForce", "get_MaxForce");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "MaxVelocity"), "set_MaxVelocity", "get_MaxVelocity");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "RigidityEnabled"), "set_RigidityEnabled", "get_RigidityEnabled");
        GL_MOVER_COMUN(AlignPosition)
    }
public:
    void    set_Position(Vector3 v) { target = v; }
    Vector3 get_Position() const { return target; }
    void    set_Responsiveness(double v) { responsiveness = v > 0.1 ? v : 0.1; }
    double  get_Responsiveness() const { return responsiveness; }
    void set_Mode(int v) { modo = CLAMP(v, 0, 1); }
    int  get_Mode() const { return modo; }
    void set_MaxForce(float v) { max_force = Math::max(v, 0.0f); }
    float get_MaxForce() const { return max_force; }
    void set_MaxVelocity(float v) { max_velocity = Math::max(v, 0.0f); }
    float get_MaxVelocity() const { return max_velocity; }
    void set_RigidityEnabled(bool v) { rigido = v; }
    bool get_RigidityEnabled() const { return rigido; }
    void set_Enabled(bool v) { enabled = v; }
    bool get_Enabled() const { return enabled; }
    void _physics_process(double delta) override {
        if (!enabled) return;
        const GLObjetivoMover o = gl_objetivo_mover(this);
        Vector3 v;
        float masa = 1.0f;
        if (!gl_vel_objetivo(o.pieza, v, masa)) return;
        Node3D* pieza3 = gl_cast<Node3D>(o.pieza);
        if (!pieza3 || !pieza3->is_inside_tree()) return;
        const Vector3 punto = o.att0 ? o.att0->get_global_position() : pieza3->get_global_position();
        const Vector3 meta = (modo == 1 && o.att1 && o.att1->is_inside_tree()) ? o.att1->get_global_position() : target;
        const float dt = (float)Math::max(delta, 0.0001);
        Vector3 deseada = (meta - punto) * (rigido ? 1.0f / dt : (float)responsiveness);
        if (deseada.length() > max_velocity) deseada = deseada.normalized() * max_velocity;
        gl_poner_vel(o.pieza, rigido ? deseada : gl_hacia_con_fuerza(v, deseada, max_force, masa, delta));
    }
    AlignPosition() { set_meta("_gl_bridge", true); set_meta("_gl_mover", true); set_physics_process(true); }
};

// AlignOrientation: gira Attachment0 hasta la orientacion de Attachment1 (Mode
// TwoAttachment, el de serie) o hasta CFrame (OneAttachment). Orientation (en
// grados) es el nombre viejo de GodotLuau y se sigue aceptando. En un
// personaje, lo que se gira es su cuerpo (solo en horizontal).
class AlignOrientation : public Node3D {
    GDCLASS(AlignOrientation, Node3D);
    Vector3     orient_deg = Vector3(0, 0, 0);
    Transform3D meta_cf;
    bool        meta_cf_puesta = false;
    double      responsiveness = 10.0;
    bool        enabled = true;
    int         modo = 1;
    float       max_ang_vel = 1e30f;
    bool        rigido = false;
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_Orientation", "v"), &AlignOrientation::set_Orientation);
        ClassDB::bind_method(D_METHOD("get_Orientation"),      &AlignOrientation::get_Orientation);
        ClassDB::bind_method(D_METHOD("set_CFrame", "v"),      &AlignOrientation::set_CFrame);
        ClassDB::bind_method(D_METHOD("get_CFrame"),           &AlignOrientation::get_CFrame);
        ClassDB::bind_method(D_METHOD("set_Responsiveness", "v"), &AlignOrientation::set_Responsiveness);
        ClassDB::bind_method(D_METHOD("get_Responsiveness"),      &AlignOrientation::get_Responsiveness);
        ClassDB::bind_method(D_METHOD("set_Mode", "v"),           &AlignOrientation::set_Mode);
        ClassDB::bind_method(D_METHOD("get_Mode"),                &AlignOrientation::get_Mode);
        ClassDB::bind_method(D_METHOD("set_MaxAngularVelocity", "v"), &AlignOrientation::set_MaxAngularVelocity);
        ClassDB::bind_method(D_METHOD("get_MaxAngularVelocity"),      &AlignOrientation::get_MaxAngularVelocity);
        ClassDB::bind_method(D_METHOD("set_RigidityEnabled", "v"), &AlignOrientation::set_RigidityEnabled);
        ClassDB::bind_method(D_METHOD("get_RigidityEnabled"),      &AlignOrientation::get_RigidityEnabled);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "Orientation"),    "set_Orientation",    "get_Orientation");
        ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "CFrame"),     "set_CFrame",         "get_CFrame");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,   "Responsiveness"), "set_Responsiveness", "get_Responsiveness");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "Mode", PROPERTY_HINT_ENUM, "OneAttachment,TwoAttachment"), "set_Mode", "get_Mode");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "MaxAngularVelocity"), "set_MaxAngularVelocity", "get_MaxAngularVelocity");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "RigidityEnabled"), "set_RigidityEnabled", "get_RigidityEnabled");
        GL_MOVER_COMUN(AlignOrientation)
    }
public:
    void    set_Orientation(Vector3 v) { orient_deg = v; meta_cf_puesta = false; }
    Vector3 get_Orientation() const { return orient_deg; }
    void    set_CFrame(const Transform3D& t) { meta_cf = t; meta_cf_puesta = true; }
    Transform3D get_CFrame() const { return meta_cf; }
    void    set_Responsiveness(double v) { responsiveness = v > 0.1 ? v : 0.1; }
    double  get_Responsiveness() const { return responsiveness; }
    void set_Mode(int v) { modo = CLAMP(v, 0, 1); }
    int  get_Mode() const { return modo; }
    void set_MaxAngularVelocity(float v) { max_ang_vel = Math::max(v, 0.0f); }
    float get_MaxAngularVelocity() const { return max_ang_vel; }
    void set_RigidityEnabled(bool v) { rigido = v; }
    bool get_RigidityEnabled() const { return rigido; }
    void set_Enabled(bool v) { enabled = v; }
    bool get_Enabled() const { return enabled; }
    void _physics_process(double delta) override {
        if (!enabled) return;
        const GLObjetivoMover o = gl_objetivo_mover(this);
        Basis meta;
        if (modo == 1 && o.att1 && o.att1->is_inside_tree()) meta = o.att1->get_global_transform().basis.orthonormalized();
        else if (meta_cf_puesta) meta = meta_cf.basis.orthonormalized();
        else meta = Basis::from_euler(orient_deg * (float)(Math_PI / 180.0));
        const float dt = (float)Math::max(delta, 0.0001);
        // Personaje: se gira su cuerpo hacia donde mira la meta (en horizontal).
        if (Node3D* pers = gl_character_root_of_hrp(o.pieza)) {
            if (gl_cast<CharacterBody3D>(pers)) {
                const Vector3 fwd = -meta.get_column(2);
                if (Vector2(fwd.x, fwd.z).length_squared() < 1e-6f) return;
                Vector3 rot = pers->get_rotation();
                const float t = rigido ? 1.0f : 1.0f - Math::exp(-(float)responsiveness * dt);
                rot.y = (float)Math::lerp_angle((double)rot.y, (double)Math::atan2(-fwd.x, -fwd.z), (double)t);
                pers->set_rotation(rot);
                return;
            }
        }
        RigidBody3D* rb = gl_cuerpo_que_mueve(o.pieza);
        if (!rb || rb->is_freeze_enabled()) return;
        const Basis actual = (o.att0 && o.att0->is_inside_tree()) ? o.att0->get_global_transform().basis.orthonormalized()
                                                                  : rb->get_global_transform().basis.orthonormalized();
        Quaternion q(meta * actual.inverse());
        if (q.w < 0.0f) q = Quaternion(-q.x, -q.y, -q.z, -q.w);
        const float s = Vector3(q.x, q.y, q.z).length();
        Vector3 w;
        if (s > 1e-6f) {
            const float angulo = 2.0f * Math::atan2(s, q.w);
            w = Vector3(q.x, q.y, q.z) / s * angulo * (rigido ? 1.0f / dt : (float)responsiveness);
        }
        if (w.length() > max_ang_vel) w = w.normalized() * max_ang_vel;
        rb->set_angular_velocity(w);
    }
    AlignOrientation() { set_meta("_gl_bridge", true); set_meta("_gl_mover", true); set_physics_process(true); }
};

// LineForce: fuerza a lo largo de la linea hacia Attachment1 (o TargetPosition,
// el nombre viejo de GodotLuau).
class LineForce : public Node3D {
    GDCLASS(LineForce, Node3D);
    double  magnitude = 0.0;
    Vector3 target = Vector3(0, 0, 0);
    bool    enabled = true;
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_Magnitude", "v"), &LineForce::set_Magnitude);
        ClassDB::bind_method(D_METHOD("get_Magnitude"),      &LineForce::get_Magnitude);
        ClassDB::bind_method(D_METHOD("set_TargetPosition", "v"), &LineForce::set_TargetPosition);
        ClassDB::bind_method(D_METHOD("get_TargetPosition"),      &LineForce::get_TargetPosition);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,   "Magnitude"),      "set_Magnitude",      "get_Magnitude");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "TargetPosition"), "set_TargetPosition", "get_TargetPosition");
        GL_MOVER_COMUN(LineForce)
    }
public:
    void    set_Magnitude(double v) { magnitude = v; }
    double  get_Magnitude() const { return magnitude; }
    void    set_TargetPosition(Vector3 v) { target = v; }
    Vector3 get_TargetPosition() const { return target; }
    void set_Enabled(bool v) { enabled = v; }
    bool get_Enabled() const { return enabled; }
    void _physics_process(double) override {
        if (!enabled) return;
        const GLObjetivoMover o = gl_objetivo_mover(this);
        RigidBody3D* rb = gl_cuerpo_que_mueve(o.pieza);
        if (!rb || rb->is_freeze_enabled()) return;
        const Vector3 desde = o.att0 ? o.att0->get_global_position() : gl_pos(rb);
        const Vector3 hasta = (o.att1 && o.att1->is_inside_tree()) ? o.att1->get_global_position() : target;
        const Vector3 dir = hasta - desde;
        const float len = dir.length();
        if (len > 1e-4f) rb->apply_force(dir / len * (float)magnitude, desde - rb->get_global_position());
    }
    LineForce() { set_meta("_gl_bridge", true); set_meta("_gl_mover", true); set_physics_process(true); }
};
#undef GL_MOVER_COMUN

// ── CanvasGroup: el subárbol se pinta como UNA capa recortada ─────────
// Es un Frame (GuiObject entero: Position/Size UDim2, AnchorPoint, fondo...).
// En Roblox el grupo se pinta en una textura de su tamaño: lo de dentro no se
// sale de su rectángulo (girado y con UICorner) y GroupTransparency y
// GroupColor3 se aplican a la capa entera, una sola vez. Antes solo se
// modulaba: no recortaba nada, y el Shatter de Motor2D (cada corte en ángulo
// es un CanvasGroup girado) dejaba volar las letras enteras en vez de sus
// mitades. Ahora es un canvas group de Godot: el "panel" del control pasa a
// ser la MÁSCARA (opaca, con el redondeo del UICorner) y el fondo de verdad
// se pinta en un canvas item hijo, debajo de todo y dentro del grupo.
//  - En pantalla (ScreenGui), sin material: CLIP_ONLY, que solo copia su rect.
//  - UI de mundo (lienzo transparente) o con UIGradient: TRANSPARENT, una
//    capa propia; el degradado multiplica la capa entera, como en Roblox.
// Godot no anida canvas groups: un CanvasGroup dentro de otro solo modula.
class RobloxCanvasGroup : public RobloxFrame {
    GDCLASS(RobloxCanvasGroup, RobloxFrame);
    double group_transparency = 0.0;
    Color  group_color = Color(1, 1, 1);
    RID _fondo;
    Ref<StyleBoxFlat> _mascara;
    Ref<StyleBoxFlat> _estilo_fondo;
    int _modo = RenderingServer::CANVAS_GROUP_MODE_DISABLED;

    bool _anidado() const {
        for (Node* p = get_parent(); p; p = p->get_parent()) {
            if (gl_cast<Viewport>(p) || gl_cast<CanvasLayer>(p)) return false;
            const RobloxCanvasGroup* g = gl_cast<RobloxCanvasGroup>(p);
            if (g && g->_modo != RenderingServer::CANVAS_GROUP_MODE_DISABLED) return true;
        }
        return false;
    }
    bool _tiene_uicorner() const {
        for (int i = 0; i < get_child_count(); i++)
            if (get_child(i)->get_class() == "UICorner") return true;
        return false;
    }
    void _apply() {
        const float a = 1.0f - (float)group_transparency;
        if (_modo == RenderingServer::CANVAS_GROUP_MODE_CLIP_ONLY) {
            // La máscara toma el color de la copia (fondo + hijos): el tinte va a
            // los hijos y la transparencia solo a la máscara.
            set_modulate(Color(group_color.r, group_color.g, group_color.b, 1.0f));
            set_self_modulate(Color(1, 1, 1, a));
        } else if (_modo == RenderingServer::CANVAS_GROUP_MODE_TRANSPARENT) {
            // La máscara multiplica la capa entera: tinte y transparencia van en ella.
            set_modulate(Color(1, 1, 1, 1));
            set_self_modulate(Color(group_color.r, group_color.g, group_color.b, a));
        } else {
            set_modulate(Color(group_color.r, group_color.g, group_color.b, a));
            set_self_modulate(Color(1, 1, 1, 1));
        }
    }
    void _pintar_fondo() {
        RenderingServer* rs = RenderingServer::get_singleton();
        rs->canvas_item_clear(_fondo);
        rs->canvas_item_set_visibility_layer(_fondo, get_visibility_layer());   // la misma capa que el grupo
        if (_modo == RenderingServer::CANVAS_GROUP_MODE_DISABLED) return;
        const float a = 1.0f - get_BackgroundTransparency();
        if (a <= 0.0f) return;
        if (_estilo_fondo.is_null()) _estilo_fondo.instantiate();
        const Color bg = get_BackgroundColor3(), borde = get_BorderColor3();
        _estilo_fondo->set_bg_color(Color(bg.r, bg.g, bg.b, a));
        _estilo_fondo->set_border_color(Color(borde.r, borde.g, borde.b, a));
        _estilo_fondo->set_border_width_all(gl_borde_px(this, get_BorderSizePixel()));
        _estilo_fondo->set_corner_radius_all(_mascara->get_corner_radius(CORNER_TOP_LEFT));
        _estilo_fondo->draw(_fondo, Rect2(Vector2(), get_size()));
    }

protected:
    // RobloxFrame acaba de poner su fondo en el "panel": con grupo, el panel es
    // la máscara (el radio lo lleva la propia máscara, que es la que toca el
    // UICorner) y el fondo se pinta aparte.
    void _gl_estilo_puesto() override {
        if (_modo == RenderingServer::CANVAS_GROUP_MODE_DISABLED) return;
        if (!_tiene_uicorner()) _mascara->set_corner_radius_all(0);
        add_theme_stylebox_override("panel", _mascara);
    }
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _gl_decidir_grupo();
        else if (p_what == NOTIFICATION_DRAW) _pintar_fondo();
    }
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_GroupTransparency", "v"), &RobloxCanvasGroup::set_GroupTransparency);
        ClassDB::bind_method(D_METHOD("get_GroupTransparency"),      &RobloxCanvasGroup::get_GroupTransparency);
        ClassDB::bind_method(D_METHOD("set_GroupColor3", "v"),       &RobloxCanvasGroup::set_GroupColor3);
        ClassDB::bind_method(D_METHOD("get_GroupColor3"),            &RobloxCanvasGroup::get_GroupColor3);
        ClassDB::bind_method(D_METHOD("_apply"), &RobloxCanvasGroup::_apply);
        ClassDB::bind_method(D_METHOD("_gl_decidir_grupo"), &RobloxCanvasGroup::_gl_decidir_grupo);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "GroupTransparency"), "set_GroupTransparency", "get_GroupTransparency");
        ADD_PROPERTY(PropertyInfo(Variant::COLOR, "GroupColor3"),       "set_GroupColor3",       "get_GroupColor3");
    }
public:
    void   set_GroupTransparency(double v) { group_transparency = v; _apply(); }
    double get_GroupTransparency() const { return group_transparency; }
    void   set_GroupColor3(Color c) { group_color = c; _apply(); }
    Color  get_GroupColor3() const { return group_color; }
    // Modo del canvas group de Godot. Se rehace al entrar en el árbol (ahí se
    // sabe si va anidado y en qué lienzo) y cuando un UIGradient le pone material.
    void _gl_decidir_grupo() {
        int modo = RenderingServer::CANVAS_GROUP_MODE_DISABLED;
        if (is_inside_tree() && !_anidado()) {
            Viewport* vp = get_viewport();
            const bool en_pantalla = get_visibility_layer() == 1u && vp && !vp->has_transparent_background();
            modo = (en_pantalla && get_material().is_null()) ? RenderingServer::CANVAS_GROUP_MODE_CLIP_ONLY
                                                             : RenderingServer::CANVAS_GROUP_MODE_TRANSPARENT;
        }
        if (modo != _modo) {
            _modo = modo;
            // Margen de 4 px en lo que se copia: el borde suavizado de la máscara
            // pisa un pixel fuera del rect y sin margen leía basura (raya negra).
            RenderingServer::get_singleton()->canvas_item_set_canvas_group_mode(
                get_canvas_item(), (RenderingServer::CanvasGroupMode)modo, 4.0f);
            // Sin grupo, el panel vuelve a ser el fondo de siempre.
            if (_modo == RenderingServer::CANVAS_GROUP_MODE_DISABLED) set_BackgroundTransparency(get_BackgroundTransparency());
        }
        gl_gradiente_para_grupo(this, _modo != RenderingServer::CANVAS_GROUP_MODE_DISABLED);
        if (is_inside_tree()) _gl_estilo_puesto();
        _apply();
        queue_redraw();
    }
    void _ready() override { _apply(); }
    RobloxCanvasGroup() {
        set_meta("_gl_bridge", true);
        set_ClipsDescendants(true);   // el valor de Roblox para un CanvasGroup nuevo
        _mascara.instantiate();
        _mascara->set_bg_color(Color(1, 1, 1, 1));
        RenderingServer* rs = RenderingServer::get_singleton();
        _fondo = rs->canvas_item_create();
        rs->canvas_item_set_parent(_fondo, get_canvas_item());
        rs->canvas_item_set_draw_index(_fondo, INT32_MIN);   // debajo de todos los hijos
    }
    ~RobloxCanvasGroup() {
        if (_fondo.is_valid() && RenderingServer::get_singleton()) RenderingServer::get_singleton()->free_rid(_fondo);
    }
};

// ── AudioListener: fija el listener 3D del audio ─────────────────────
class AudioListener : public Node3D {
    GDCLASS(AudioListener, Node3D);
    AudioListener3D* listener = nullptr;
protected:
    static void _bind_methods() {}
public:
    void _ready() override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        listener = memnew(AudioListener3D);
        add_child(listener);
        listener->make_current();
    }
    AudioListener() { set_meta("_gl_bridge", true); }
};

inline void gl_register_behavior_classes() {
    gl_register_ui_modifier_classes();
    ClassDB::register_class<RobloxCanvasGroup>();
    ClassDB::register_class<AudioListener>();
    ClassDB::register_class<FlangeSoundEffect>();
    ClassDB::register_class<ParticleEmitter>();
    ClassDB::register_class<ReverbSoundEffect>();
    ClassDB::register_class<EchoSoundEffect>();
    ClassDB::register_class<EqualizerSoundEffect>();
    ClassDB::register_class<DistortionSoundEffect>();
    ClassDB::register_class<CompressorSoundEffect>();
    ClassDB::register_class<PitchShiftSoundEffect>();
    ClassDB::register_class<ChorusSoundEffect>();
    ClassDB::register_class<VectorForce>();
    ClassDB::register_class<Torque>();
    ClassDB::register_class<LinearVelocity>();
    ClassDB::register_class<AngularVelocity>();
    ClassDB::register_class<AlignPosition>();
    ClassDB::register_class<AlignOrientation>();
    ClassDB::register_class<LineForce>();
}

#endif // ROBLOX_BEHAVIOR_H

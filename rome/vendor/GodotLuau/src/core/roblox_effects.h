#ifndef ROBLOX_EFFECTS_H
#define ROBLOX_EFFECTS_H

// Clases de efectos funcionales (autoría paralela verificada por build).

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/classes/gpu_particles3d.hpp>
#include <godot_cpp/classes/particle_process_material.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

// ── Smoke ──
class Smoke : public GPUParticles3D {
    GDCLASS(Smoke, GPUParticles3D);
    Ref<ParticleProcessMaterial> pm; Ref<StandardMaterial3D> mm;
protected:
    static void _bind_methods() {}
public:
    void _ready() override {
        if (pm.is_null()) pm.instantiate();
        pm->set_direction(Vector3(0,1,0));
        pm->set_spread(15.0f);
        pm->set_gravity(Vector3(0,0.5f,0));
        pm->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.2f);
        pm->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.6f);
        pm->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 1.0f);
        pm->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 2.0f);
        set_process_material(pm);
        if (mm.is_null()) mm.instantiate();
        mm->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        mm->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        mm->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
        mm->set_albedo(Color(0.55f,0.55f,0.55f,0.45f));
        Ref<QuadMesh> qm; qm.instantiate(); qm->set_size(Vector2(0.3f,0.3f)); qm->set_material(mm);
        set_draw_pass_mesh(0, qm);
        set_amount(30); set_lifetime(3.0); set_emitting(true);
    }
    Smoke() { set_meta("_gl_bridge", true); }
};

// ── Fire ──
class Fire : public GPUParticles3D {
    GDCLASS(Fire, GPUParticles3D);
    Ref<ParticleProcessMaterial> pm; Ref<StandardMaterial3D> mm;
protected:
    static void _bind_methods() {}
public:
    void _ready() override {
        if (pm.is_null()) pm.instantiate();
        pm->set_direction(Vector3(0,1,0));
        pm->set_spread(12.0f);
        pm->set_gravity(Vector3(0,1.0f,0));
        pm->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 1.0f);
        pm->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 2.0f);
        pm->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.5f);
        pm->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.0f);
        set_process_material(pm);
        if (mm.is_null()) mm.instantiate();
        mm->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        mm->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        mm->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
        mm->set_albedo(Color(1.0f,0.4f,0.1f,0.9f));
        Ref<QuadMesh> qm; qm.instantiate(); qm->set_size(Vector2(0.3f,0.3f)); qm->set_material(mm);
        set_draw_pass_mesh(0, qm);
        set_amount(40); set_lifetime(1.0); set_emitting(true);
    }
    Fire() { set_meta("_gl_bridge", true); }
};

// ── Sparkles ──
class Sparkles : public GPUParticles3D {
    GDCLASS(Sparkles, GPUParticles3D);
    Ref<ParticleProcessMaterial> pm; Ref<StandardMaterial3D> mm;
protected:
    static void _bind_methods() {}
public:
    void _ready() override {
        if (pm.is_null()) pm.instantiate();
        pm->set_direction(Vector3(0,1,0));
        pm->set_spread(45.0f);
        pm->set_gravity(Vector3(0,-1.0f,0));
        pm->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.5f);
        pm->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 1.5f);
        pm->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.15f);
        pm->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 0.35f);
        set_process_material(pm);
        if (mm.is_null()) mm.instantiate();
        mm->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        mm->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        mm->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
        mm->set_albedo(Color(1.0f,1.0f,0.6f,1.0f));
        Ref<QuadMesh> qm; qm.instantiate(); qm->set_size(Vector2(0.3f,0.3f)); qm->set_material(mm);
        set_draw_pass_mesh(0, qm);
        set_amount(25); set_lifetime(1.5); set_emitting(true);
    }
    Sparkles() { set_meta("_gl_bridge", true); }
};

// ── Explosion ──
class Explosion : public GPUParticles3D {
    GDCLASS(Explosion, GPUParticles3D);
    Ref<ParticleProcessMaterial> pm; Ref<StandardMaterial3D> mm;
    float blast_radius = 8.0f; float blast_pressure = 500.0f;
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_blast_radius", "value"), &Explosion::set_blast_radius);
        ClassDB::bind_method(D_METHOD("get_blast_radius"), &Explosion::get_blast_radius);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "BlastRadius"), "set_blast_radius", "get_blast_radius");
        ClassDB::bind_method(D_METHOD("set_blast_pressure", "value"), &Explosion::set_blast_pressure);
        ClassDB::bind_method(D_METHOD("get_blast_pressure"), &Explosion::get_blast_pressure);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "BlastPressure"), "set_blast_pressure", "get_blast_pressure");
    }
public:
    void set_blast_radius(float value) { blast_radius = value; }
    float get_blast_radius() const { return blast_radius; }
    void set_blast_pressure(float value) { blast_pressure = value; }
    float get_blast_pressure() const { return blast_pressure; }
    void _ready() override {
        if (pm.is_null()) pm.instantiate();
        pm->set_direction(Vector3(0,1,0));
        pm->set_spread(90.0f);
        pm->set_gravity(Vector3(0,-2.0f,0));
        pm->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 4.0f);
        pm->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 8.0f);
        pm->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.5f);
        pm->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.5f);
        set_process_material(pm);
        if (mm.is_null()) mm.instantiate();
        mm->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        mm->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        mm->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
        mm->set_albedo(Color(1.0f,0.5f,0.15f,0.95f));
        Ref<QuadMesh> qm; qm.instantiate(); qm->set_size(Vector2(0.3f,0.3f)); qm->set_material(mm);
        set_draw_pass_mesh(0, qm);
        set_amount(80); set_lifetime(1.0); set_one_shot(true); set_explosiveness_ratio(1.0f); set_emitting(true);
    }
    Explosion() { set_meta("_gl_bridge", true); }
};

// (RopeConstraint vive en roblox_constraints.h: la cuerda de verdad, entre
//  Attachment0 y Attachment1. La de aqui tiraba del padre hacia un
//  TargetPosition que Roblox no tiene.)

inline void gl_register_effects_classes() {
    ClassDB::register_class<Smoke>();
    ClassDB::register_class<Fire>();
    ClassDB::register_class<Sparkles>();
    ClassDB::register_class<Explosion>();
}

#endif // ROBLOX_EFFECTS_H

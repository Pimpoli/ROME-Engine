#ifndef ROBLOX_HIGHLIGHT_H
#define ROBLOX_HIGHLIGHT_H

// ════════════════════════════════════════════════════════════════════
//  Highlight de Roblox: relleno y contorno sobre su Adornee (o su padre).
//  Antes era un nodo vacio. Se hace con el material_overlay de cada malla del
//  objetivo: el relleno, y en su next_pass el contorno (casco invertido que se
//  infla un poco). DepthMode AlwaysOnTop = se ve a traves de las paredes.
//  Se repasan las mallas cada 0,5 s: a un personaje le llegan accesorios y
//  piezas despues de crear el Highlight.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node3d.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include "gl_runtime.h"     // gl_ref_de

using namespace godot;

class Highlight : public Node3D {
    GDCLASS(Highlight, Node3D);

    Color fill_color = Color(1, 0, 0);
    float fill_transparency = 0.5f;
    Color outline_color = Color(1, 1, 1);
    float outline_transparency = 0.0f;
    int   depth_mode = 0;        // Enum.HighlightDepthMode: AlwaysOnTop, Occluded
    bool  enabled = true;

    Ref<StandardMaterial3D> relleno, contorno;
    LocalVector<uint64_t> mallas;   // GeometryInstance3D con nuestro overlay puesto
    uint64_t objetivo_visto = 0;
    double   espera = 0.0;

    Node* _objetivo() const {
        static const StringName ks("_glo_Adornee"), ki("Adornee");
        if (Node* a = gl_ref_de(this, ks, ki)) return a;
        return get_parent();
    }

    void _materiales() {
        if (relleno.is_null()) { relleno.instantiate(); contorno.instantiate(); }
        const bool encima = depth_mode == 0;
        Color f = fill_color;    f.a = Math::clamp(1.0f - fill_transparency, 0.0f, 1.0f);
        Color o = outline_color; o.a = Math::clamp(1.0f - outline_transparency, 0.0f, 1.0f);
        relleno->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        relleno->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        relleno->set_albedo(f);
        relleno->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, encima);
        relleno->set_render_priority(10);
        contorno->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        contorno->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        contorno->set_albedo(o);
        contorno->set_cull_mode(BaseMaterial3D::CULL_FRONT);
        contorno->set_grow_enabled(true);
        contorno->set_grow(0.06f);
        contorno->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, encima);
        contorno->set_render_priority(9);
        relleno->set_next_pass(contorno);
    }

    void _quitar() {
        for (uint32_t i = 0; i < mallas.size(); i++)
            if (GeometryInstance3D* g = gl_cast<GeometryInstance3D>(ObjectDB::get_instance(ObjectID(mallas[i]))))
                if (g->get_material_overlay().ptr() == relleno.ptr()) g->set_material_overlay(Ref<Material>());
        mallas.clear();
    }
    // Con los hijos internos: la malla de cada pieza lo es.
    void _recoger(Node* n, LocalVector<uint64_t>& v) {
        if (GeometryInstance3D* g = gl_cast<GeometryInstance3D>(n))
            if (gl_cast<MeshInstance3D>(g)) v.push_back((uint64_t)g->get_instance_id());
        const int k = n->get_child_count(true);
        for (int i = 0; i < k; i++) _recoger(n->get_child(i, true), v);
    }
    void _aplicar() {
        if (Engine::get_singleton()->is_editor_hint()) return;
        Node* obj = enabled && is_inside_tree() ? _objetivo() : nullptr;
        if (!obj) { _quitar(); objetivo_visto = 0; return; }
        _materiales();
        LocalVector<uint64_t> nuevas;
        _recoger(obj, nuevas);
        // Mismas mallas y mismo objetivo: nada que tocar (es el repaso de cada 0,5 s).
        if ((uint64_t)obj->get_instance_id() == objetivo_visto && nuevas.size() == mallas.size()) return;
        _quitar();
        objetivo_visto = (uint64_t)obj->get_instance_id();
        for (uint32_t i = 0; i < nuevas.size(); i++)
            if (GeometryInstance3D* g = gl_cast<GeometryInstance3D>(ObjectDB::get_instance(ObjectID(nuevas[i])))) {
                if (g->get_material_overlay().is_valid() && g->get_material_overlay().ptr() != relleno.ptr()) continue;   // de otro Highlight
                g->set_material_overlay(relleno);
                mallas.push_back(nuevas[i]);
            }
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_FillColor", "v"), &Highlight::set_fill_color);
        ClassDB::bind_method(D_METHOD("get_FillColor"), &Highlight::get_fill_color);
        ClassDB::bind_method(D_METHOD("set_FillTransparency", "v"), &Highlight::set_fill_transparency);
        ClassDB::bind_method(D_METHOD("get_FillTransparency"), &Highlight::get_fill_transparency);
        ClassDB::bind_method(D_METHOD("set_OutlineColor", "v"), &Highlight::set_outline_color);
        ClassDB::bind_method(D_METHOD("get_OutlineColor"), &Highlight::get_outline_color);
        ClassDB::bind_method(D_METHOD("set_OutlineTransparency", "v"), &Highlight::set_outline_transparency);
        ClassDB::bind_method(D_METHOD("get_OutlineTransparency"), &Highlight::get_outline_transparency);
        ClassDB::bind_method(D_METHOD("set_DepthMode", "v"), &Highlight::set_depth_mode);
        ClassDB::bind_method(D_METHOD("get_DepthMode"), &Highlight::get_depth_mode);
        ClassDB::bind_method(D_METHOD("set_Enabled", "v"), &Highlight::set_enabled);
        ClassDB::bind_method(D_METHOD("get_Enabled"), &Highlight::get_enabled);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR, "FillColor"), "set_FillColor", "get_FillColor");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "FillTransparency"), "set_FillTransparency", "get_FillTransparency");
        ADD_PROPERTY(PropertyInfo(Variant::COLOR, "OutlineColor"), "set_OutlineColor", "get_OutlineColor");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "OutlineTransparency"), "set_OutlineTransparency", "get_OutlineTransparency");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "DepthMode", PROPERTY_HINT_ENUM, "AlwaysOnTop,Occluded"), "set_DepthMode", "get_DepthMode");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Enabled"), "set_Enabled", "get_Enabled");
    }
    void _notification(int p_what) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (p_what == NOTIFICATION_ENTER_TREE) { set_process(true); espera = 0.0; _aplicar(); }
        else if (p_what == NOTIFICATION_EXIT_TREE) { set_process(false); _quitar(); objetivo_visto = 0; }
        else if (p_what == NOTIFICATION_PROCESS) {
            espera += get_process_delta_time();
            if (espera >= 0.5) { espera = 0.0; _aplicar(); }
        }
    }

public:
    void  set_fill_color(const Color& v)   { fill_color = v; _materiales(); }
    Color get_fill_color() const           { return fill_color; }
    void  set_fill_transparency(float v)   { fill_transparency = v; _materiales(); }
    float get_fill_transparency() const    { return fill_transparency; }
    void  set_outline_color(const Color& v){ outline_color = v; _materiales(); }
    Color get_outline_color() const        { return outline_color; }
    void  set_outline_transparency(float v){ outline_transparency = v; _materiales(); }
    float get_outline_transparency() const { return outline_transparency; }
    void  set_depth_mode(int v)            { depth_mode = CLAMP(v, 0, 1); _materiales(); }
    int   get_depth_mode() const           { return depth_mode; }
    void  set_enabled(bool v)              { enabled = v; _aplicar(); }
    bool  get_enabled() const              { return enabled; }
    // Adornee lo escribe el puente generico (meta _glo_Adornee): se mira en el
    // repaso de cada 0,5 s, o al momento si alguien llama a esto.
    void  gl_refrescar() { objetivo_visto = 0; _aplicar(); }
    Highlight() { set_meta("_gl_bridge", true); set_meta("_gl_efecto", true); }
};

#endif // ROBLOX_HIGHLIGHT_H

#ifndef ROBLOX_CINTAS_H
#define ROBLOX_CINTAS_H

// ════════════════════════════════════════════════════════════════════
//  Beam y Trail de Roblox: cintas entre dos Attachment.
//  Antes eran nodos vacios que guardaban las propiedades y no dibujaban nada.
//    · Beam: una cinta de Attachment0 a Attachment1, curvada con CurveSize0/1.
//    · Trail: la estela que dejan los dos attachments al moverse.
//  Se dibujan con una malla en coordenadas de mundo y un shader comun
//  (textura que se desliza, color y transparencia por vertice).
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node3d.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include "gl_runtime.h"     // gl_ref_de: Attachment0/1 por script o importados
#include "gl_servicios.h"   // gl_en_workspace
#include "gl_secuencias.h"
#include "gl_clone.h"       // GL_META_INTERNO
#include "roblox_attachment.h"
#include "roblox_gui.h"     // gl_gui_load_image, gl_tex_blanca

using namespace godot;

// Shader de las cintas. Sin luz con LightInfluence = 0 y aditivo con
// LightEmission (los valores por defecto de Roblox son 0 y 0).
inline Ref<Shader>& gl_shader_cinta_ref(int i) { static Ref<Shader> s[4]; return s[i]; }
inline Ref<Shader> gl_shader_cinta(bool sin_luz, bool aditivo) {
    const int i = (sin_luz ? 1 : 0) + (aditivo ? 2 : 0);
    Ref<Shader>& s = gl_shader_cinta_ref(i);
    if (s.is_null()) {
        String modos = "cull_disabled, depth_draw_never";
        if (sin_luz) modos += ", unshaded";
        modos += aditivo ? ", blend_add" : ", blend_mix";
        s.instantiate();
        s->set_code(String("shader_type spatial;\nrender_mode ") + modos + ";\n"
            "uniform sampler2D tex : source_color, repeat_enable, filter_linear_mipmap;\n"
            "uniform float velocidad = 0.0;\n"
            "uniform float brillo = 1.0;\n"
            "void fragment() {\n"
            "    vec4 t = texture(tex, vec2(UV.x, UV.y - TIME * velocidad));\n"
            "    ALBEDO = COLOR.rgb * t.rgb * brillo;\n"
            "    ALPHA = COLOR.a * t.a;\n"
            "}\n");
    }
    return s;
}
inline void gl_shader_cinta_libera() { for (int i = 0; i < 4; i++) gl_shader_cinta_ref(i).unref(); }

// El eje (Axis) y el eje secundario (SecondaryAxis) de un Attachment en el
// mundo; si no es un Attachment, los de su base.
inline Vector3 gl_eje_att(Node3D* a, int columna) {
    if (Attachment* at = gl_cast<Attachment>(a)) {
        const Vector3 guardado = columna == 0 ? at->get_axis() : at->get_secondary_axis();
        const Vector3 defecto = columna == 0 ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
        Node3D* pieza = gl_cast<Node3D>(at->get_parent());
        if (guardado != defecto && pieza) return pieza->get_global_transform().basis.xform(guardado).normalized();
    }
    return a->get_global_transform().basis.get_column(columna).normalized();
}

// Lo comun: propiedades de aspecto, material y la malla interna.
class GLCinta : public Node3D {
    GDCLASS(GLCinta, Node3D);
protected:
    bool    enabled = true;
    Variant color_v;
    Variant transparency_v;
    String  texture;
    int     texture_mode = 0;      // Enum.TextureMode: Stretch, Wrap, Static
    float   texture_length = 1.0f;
    float   light_emission = 0.0f, light_influence = 0.0f, brightness = 1.0f;
    bool    face_camera = false;

    MeshInstance3D*     malla = nullptr;
    Ref<ArrayMesh>      am;
    Ref<ShaderMaterial> mat;
    bool sucio = true;             // hay que rehacer la malla aunque nada se mueva

    // La tira de vertices de este frame y la superficie donde se sube. Antes
    // era una ImmediateMesh: cada cambio (y las flechas del camino de Build a
    // Mountain cambian cada frame, siguen al personaje) creaba buffers nuevos
    // en la GPU y, en la Intel, la CPU se quedaba esperando al driver
    // (Beam::_actualizar, 0,12 ms por frame). Ahora los vertices se escriben
    // encima de los de antes y la superficie solo se rehace si no caben.
    LocalVector<Vector3> v_pos;
    LocalVector<Color>   v_col;
    LocalVector<Vector2> v_uv;
    PackedByteArray bytes_v, bytes_a;
    int      cabe = 0;             // vertices que caben en la superficie de ahora
    bool     encima = false;       // el formato es el esperado: se escribe encima
    uint32_t paso_v = 0, paso_a = 0, off_col = 0, off_uv = 0;

    Node3D* _att(int cual) const {
        static const StringName k0s("_glo_Attachment0"), k0i("Attachment0");
        static const StringName k1s("_glo_Attachment1"), k1i("Attachment1");
        Node3D* a = gl_cast<Node3D>(cual == 0 ? gl_ref_de(this, k0s, k0i) : gl_ref_de(this, k1s, k1i));
        return (a && a->is_inside_tree()) ? a : nullptr;
    }
    Vector3 _ojo() const {
        Viewport* vp = get_viewport();
        Camera3D* c = vp ? vp->get_camera_3d() : nullptr;
        return c ? c->get_global_position() : Vector3();
    }
    virtual float _velocidad_textura() const { return 0.0f; }

    void _rehacer_material() {
        if (mat.is_null()) mat.instantiate();
        mat->set_shader(gl_shader_cinta(light_influence < 0.5f, light_emission >= 0.5f));
        Ref<Texture2D> tx;
        if (!texture.is_empty() && !texture.begins_with("rbxasset://")) tx = gl_gui_load_image(this, texture);
        // Sin textura, blanca (como Roblox); si viene de camino por streaming,
        // nada hasta que llega (_gl_textura_lista), para no dar un destello.
        if (tx.is_null()) {
            const bool en_camino = !texture.is_empty() && gl_tex_streaming_activo() &&
                                   (!gl_local_asset_file(texture).is_empty() || !gl_local_asset_path(texture).is_empty());
            tx = en_camino ? gl_tex_vacia() : gl_tex_blanca();
        }
        mat->set_shader_parameter("tex", tx);
        mat->set_shader_parameter("velocidad", _velocidad_textura());
        mat->set_shader_parameter("brillo", brightness);
        sucio = true;
    }
    void _asegurar_malla() {
        if (malla) return;
        am.instantiate();
        malla = memnew(MeshInstance3D);
        malla->set_meta(GL_META_INTERNO, true);
        malla->set_as_top_level(true);   // los vertices van en coordenadas de mundo
        malla->set_mesh(am);
        malla->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        add_child(malla, false, Node::INTERNAL_MODE_BACK);
        _rehacer_material();
    }
    void _empezar() { v_pos.clear(); v_col.clear(); v_uv.clear(); }
    // Un vertice de la cinta.
    void _vertice(const Vector3& p, const Color& c, const Vector2& uv) {
        v_pos.push_back(p);
        v_col.push_back(c);
        v_uv.push_back(uv);
    }
    void _vaciar() { am->clear_surfaces(); cabe = 0; }

    // Sube la tira. Lo que sobra de la superficie repite el ultimo vertice:
    // triangulos sin area, que no se dibujan.
    void _subir() {
        const int n = (int)v_pos.size();
        if (n < 3) { _vaciar(); return; }
        RenderingServer* rs = RenderingServer::get_singleton();
        if (n > cabe || !encima) {
            cabe = encima ? MAX(n, cabe + cabe / 2) : n;
            PackedVector3Array pv; PackedColorArray pc; PackedVector2Array pu;
            pv.resize(cabe); pc.resize(cabe); pu.resize(cabe);
            for (int i = 0; i < cabe; i++) {
                const int k = MIN(i, n - 1);
                pv.set(i, v_pos[k]); pc.set(i, v_col[k]); pu.set(i, v_uv[k]);
            }
            Array arr;
            arr.resize(Mesh::ARRAY_MAX);
            arr[Mesh::ARRAY_VERTEX] = pv;
            arr[Mesh::ARRAY_COLOR] = pc;
            arr[Mesh::ARRAY_TEX_UV] = pu;
            am->clear_surfaces();
            am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLE_STRIP, arr, TypedArray<Array>(), Dictionary(),
                                        Mesh::ARRAY_FLAG_USE_DYNAMIC_UPDATE);
            am->surface_set_material(0, mat);
            // El formato de Godot sin comprimir: posicion en 3 floats; color en
            // 4 bytes y UV en 2 floats en el flujo de atributos. Si no sale asi,
            // se queda rehaciendo la superficie como antes.
            const BitField<RenderingServer::ArrayFormat> fmt((int64_t)am->surface_get_format(0));
            paso_v = rs->mesh_surface_get_format_vertex_stride(fmt, cabe);
            paso_a = rs->mesh_surface_get_format_attribute_stride(fmt, cabe);
            off_col = rs->mesh_surface_get_format_offset(fmt, cabe, RenderingServer::ARRAY_COLOR);
            off_uv = rs->mesh_surface_get_format_offset(fmt, cabe, RenderingServer::ARRAY_TEX_UV);
            encima = paso_v == 12 && off_col + 4 <= paso_a && off_uv + 8 <= paso_a &&
                     (off_uv >= off_col + 4 || off_col >= off_uv + 8);
        } else {
            bytes_v.resize((int64_t)cabe * paso_v);
            bytes_a.resize((int64_t)cabe * paso_a);
            uint8_t* w = bytes_v.ptrw();
            uint8_t* a = bytes_a.ptrw();
            for (int i = 0; i < cabe; i++) {
                const int k = MIN(i, n - 1);
                const float pos[3] = { v_pos[k].x, v_pos[k].y, v_pos[k].z };
                memcpy(w + (size_t)i * paso_v, pos, sizeof(pos));
                const Color& c = v_col[k];
                const uint8_t c8[4] = { (uint8_t)CLAMP(c.r * 255.0, 0.0, 255.0), (uint8_t)CLAMP(c.g * 255.0, 0.0, 255.0),
                                        (uint8_t)CLAMP(c.b * 255.0, 0.0, 255.0), (uint8_t)CLAMP(c.a * 255.0, 0.0, 255.0) };
                memcpy(a + (size_t)i * paso_a + off_col, c8, sizeof(c8));
                const float uv[2] = { v_uv[k].x, v_uv[k].y };
                memcpy(a + (size_t)i * paso_a + off_uv, uv, sizeof(uv));
            }
            rs->mesh_surface_update_vertex_region(am->get_rid(), 0, 0, bytes_v);
            rs->mesh_surface_update_attribute_region(am->get_rid(), 0, 0, bytes_a);
        }
        // La caja para el recorte: la de la superficie es la del dia que se creo.
        AABB caja(v_pos[0], Vector3());
        for (int i = 1; i < n; i++) caja.expand_to(v_pos[i]);
        rs->mesh_set_custom_aabb(am->get_rid(), caja);
    }

    virtual void _actualizar() {}

    void _notification(int p_what) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (p_what == NOTIFICATION_ENTER_TREE) {
            // Solo late lo que esta en el mundo: una plantilla guardada en
            // ReplicatedStorage no dibuja ni cuesta.
            const bool vivo = gl_en_workspace(this);
            set_process(vivo);
            if (vivo) { _asegurar_malla(); sucio = true; }
        } else if (p_what == NOTIFICATION_EXIT_TREE) {
            set_process(false);
        } else if (p_what == NOTIFICATION_PROCESS) {
            _actualizar();
        }
    }
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_textura_lista"), &GLCinta::_gl_textura_lista);
        ClassDB::bind_method(D_METHOD("set_Enabled", "v"), &GLCinta::set_enabled);
        ClassDB::bind_method(D_METHOD("get_Enabled"), &GLCinta::get_enabled);
        ClassDB::bind_method(D_METHOD("set_Color", "v"), &GLCinta::set_color_seq);
        ClassDB::bind_method(D_METHOD("get_Color"), &GLCinta::get_color_seq);
        ClassDB::bind_method(D_METHOD("set_Transparency", "v"), &GLCinta::set_transparency);
        ClassDB::bind_method(D_METHOD("get_Transparency"), &GLCinta::get_transparency);
        ClassDB::bind_method(D_METHOD("set_Texture", "v"), &GLCinta::set_texture);
        ClassDB::bind_method(D_METHOD("get_Texture"), &GLCinta::get_texture);
        ClassDB::bind_method(D_METHOD("set_TextureMode", "v"), &GLCinta::set_texture_mode);
        ClassDB::bind_method(D_METHOD("get_TextureMode"), &GLCinta::get_texture_mode);
        ClassDB::bind_method(D_METHOD("set_TextureLength", "v"), &GLCinta::set_texture_length);
        ClassDB::bind_method(D_METHOD("get_TextureLength"), &GLCinta::get_texture_length);
        ClassDB::bind_method(D_METHOD("set_LightEmission", "v"), &GLCinta::set_light_emission);
        ClassDB::bind_method(D_METHOD("get_LightEmission"), &GLCinta::get_light_emission);
        ClassDB::bind_method(D_METHOD("set_LightInfluence", "v"), &GLCinta::set_light_influence);
        ClassDB::bind_method(D_METHOD("get_LightInfluence"), &GLCinta::get_light_influence);
        ClassDB::bind_method(D_METHOD("set_Brightness", "v"), &GLCinta::set_brightness);
        ClassDB::bind_method(D_METHOD("get_Brightness"), &GLCinta::get_brightness);
        ClassDB::bind_method(D_METHOD("set_FaceCamera", "v"), &GLCinta::set_face_camera);
        ClassDB::bind_method(D_METHOD("get_FaceCamera"), &GLCinta::get_face_camera);
        ClassDB::bind_method(D_METHOD("set_image", "id"), &GLCinta::set_image);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Enabled"), "set_Enabled", "get_Enabled");
        ADD_PROPERTY(PropertyInfo(Variant::NIL, "Color", PROPERTY_HINT_NONE, "",
                     PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT), "set_Color", "get_Color");
        ADD_PROPERTY(PropertyInfo(Variant::NIL, "Transparency", PROPERTY_HINT_NONE, "",
                     PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT), "set_Transparency", "get_Transparency");
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "Texture"), "set_Texture", "get_Texture");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "TextureMode", PROPERTY_HINT_ENUM, "Stretch,Wrap,Static"), "set_TextureMode", "get_TextureMode");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "TextureLength"), "set_TextureLength", "get_TextureLength");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "LightEmission"), "set_LightEmission", "get_LightEmission");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "LightInfluence"), "set_LightInfluence", "get_LightInfluence");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Brightness"), "set_Brightness", "get_Brightness");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "FaceCamera"), "set_FaceCamera", "get_FaceCamera");
    }

public:
    void  set_enabled(bool v)            { enabled = v; sucio = true; }
    bool  get_enabled() const            { return enabled; }
    void  set_color_seq(const Variant& v){ color_v = v; sucio = true; }
    void  set_transparency(const Variant& v) { transparency_v = v; sucio = true; }
    Variant get_color_seq() const    { return color_v.get_type() == Variant::NIL ? Variant(gl_sec_color_const(Color(1, 1, 1))) : color_v; }
    Variant get_transparency() const { return transparency_v.get_type() == Variant::NIL ? Variant(gl_sec_num_const(0.5f)) : transparency_v; }
    void  set_texture(const String& v)   { texture = v; if (malla) _rehacer_material(); }
    String get_texture() const           { return texture; }
    void  set_texture_mode(int v)        { texture_mode = CLAMP(v, 0, 2); sucio = true; }
    int   get_texture_mode() const       { return texture_mode; }
    void  set_texture_length(float v)    { texture_length = Math::max(v, 0.001f); sucio = true; if (malla) _rehacer_material(); }
    float get_texture_length() const     { return texture_length; }
    void  set_light_emission(float v)    { light_emission = v; if (malla) _rehacer_material(); }
    float get_light_emission() const     { return light_emission; }
    void  set_light_influence(float v)   { light_influence = v; if (malla) _rehacer_material(); }
    float get_light_influence() const    { return light_influence; }
    void  set_brightness(float v)        { brightness = v; if (malla) _rehacer_material(); }
    float get_brightness() const         { return brightness; }
    void  set_face_camera(bool v)        { face_camera = v; sucio = true; }
    bool  get_face_camera() const        { return face_camera; }
    // El fetcher de assets llama set_image al terminar la descarga.
    void  set_image(const String& id)    { if (id == texture && malla) _rehacer_material(); }
    // Llego la textura pedida por streaming (gl_streaming_texturas.h).
    void  _gl_textura_lista()            { if (malla) _rehacer_material(); }
};

// ────────────────────────────────────────────────────────────────────
//  Beam
// ────────────────────────────────────────────────────────────────────
class Beam : public GLCinta {
    GDCLASS(Beam, GLCinta);

    float width0 = 1.0f, width1 = 1.0f;
    int   segments = 10;
    float curve0 = 0.0f, curve1 = 0.0f;
    float texture_speed = 1.0f;
    float z_offset = 0.0f;

    Transform3D ult0, ult1;
    Vector3     ult_ojo;

    float _velocidad_textura() const override { return texture_speed; }

    void _actualizar() override {
        Node3D* a0 = _att(0);
        Node3D* a1 = _att(1);
        if (!malla) return;
        if (!enabled || !a0 || !a1) { malla->set_visible(false); return; }
        const Transform3D t0 = a0->get_global_transform(), t1 = a1->get_global_transform();
        const Vector3 ojo = face_camera ? _ojo() : Vector3();
        // Nada se movio: la malla de antes sigue valiendo (lo normal en un rayo
        // de tutorial quieto, que es el caso comun).
        if (!sucio && t0 == ult0 && t1 == ult1 && (!face_camera || ojo.distance_squared_to(ult_ojo) < 1e-6f)) return;
        sucio = false; ult0 = t0; ult1 = t1; ult_ojo = ojo;
        malla->set_visible(true);

        // Bezier: los puntos de control salen por el eje (Axis) de cada attachment.
        const Vector3 p0 = t0.origin, p3 = t1.origin;
        const Vector3 p1 = p0 + gl_eje_att(a0, 0) * curve0;
        const Vector3 p2 = p3 - gl_eje_att(a1, 0) * curve1;
        const Vector3 arriba0 = gl_eje_att(a0, 1), arriba1 = gl_eje_att(a1, 1);
        const GLSecColor col = gl_sec_color(color_v, Color(1, 1, 1));
        const GLSecNum tr = gl_sec_num(transparency_v, 0.5f);
        const int n = CLAMP(segments, 1, 1000);
        float recorrido = 0.0f;
        Vector3 previo = p0;

        _empezar();
        for (int i = 0; i <= n; i++) {
            const float t = (float)i / (float)n, u = 1.0f - t;
            const Vector3 p = p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
            Vector3 tang = (p1 - p0) * (3 * u * u) + (p2 - p1) * (6 * u * t) + (p3 - p2) * (3 * t * t);
            if (tang.length_squared() < 1e-10f) tang = p3 - p0;
            tang = tang.normalized();
            recorrido += p.distance_to(previo);
            previo = p;
            Vector3 lado = face_camera ? tang.cross(ojo - p) : tang.cross(arriba0.lerp(arriba1, t));
            if (lado.length_squared() < 1e-10f) lado = tang.cross(Math::abs(tang.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0));
            lado = lado.normalized() * (Math::lerp(width0, width1, t) * 0.5f);
            Color c = col.en(t);
            c.a = Math::clamp(1.0f - tr.en(t), 0.0f, 1.0f);
            // Stretch: TextureLength repeticiones a lo largo; Wrap/Static: una
            // cada TextureLength studs. Como Roblox (medido en Studio): el eje
            // VERTICAL de la imagen va a lo largo, con la parte de arriba en el
            // Attachment0, y el horizontal a lo ancho. Con la imagen tumbada, las
            // flechas del camino de Build a Mountain salian atravesadas.
            const float vv = texture_mode == 0 ? t * texture_length : recorrido / texture_length;
            _vertice(p + lado, c, Vector2(0.0f, vv));
            _vertice(p - lado, c, Vector2(1.0f, vv));
        }
        _subir();
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_Width0", "v"), &Beam::set_width0);
        ClassDB::bind_method(D_METHOD("get_Width0"), &Beam::get_width0);
        ClassDB::bind_method(D_METHOD("set_Width1", "v"), &Beam::set_width1);
        ClassDB::bind_method(D_METHOD("get_Width1"), &Beam::get_width1);
        ClassDB::bind_method(D_METHOD("set_Segments", "v"), &Beam::set_segments);
        ClassDB::bind_method(D_METHOD("get_Segments"), &Beam::get_segments);
        ClassDB::bind_method(D_METHOD("set_CurveSize0", "v"), &Beam::set_curve0);
        ClassDB::bind_method(D_METHOD("get_CurveSize0"), &Beam::get_curve0);
        ClassDB::bind_method(D_METHOD("set_CurveSize1", "v"), &Beam::set_curve1);
        ClassDB::bind_method(D_METHOD("get_CurveSize1"), &Beam::get_curve1);
        ClassDB::bind_method(D_METHOD("set_TextureSpeed", "v"), &Beam::set_texture_speed);
        ClassDB::bind_method(D_METHOD("get_TextureSpeed"), &Beam::get_texture_speed);
        ClassDB::bind_method(D_METHOD("set_ZOffset", "v"), &Beam::set_z_offset);
        ClassDB::bind_method(D_METHOD("get_ZOffset"), &Beam::get_z_offset);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Width0"), "set_Width0", "get_Width0");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Width1"), "set_Width1", "get_Width1");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "Segments"), "set_Segments", "get_Segments");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "CurveSize0"), "set_CurveSize0", "get_CurveSize0");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "CurveSize1"), "set_CurveSize1", "get_CurveSize1");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "TextureSpeed"), "set_TextureSpeed", "get_TextureSpeed");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ZOffset"), "set_ZOffset", "get_ZOffset");
    }

public:
    void  set_width0(float v)        { width0 = v; sucio = true; }
    float get_width0() const         { return width0; }
    void  set_width1(float v)        { width1 = v; sucio = true; }
    float get_width1() const         { return width1; }
    void  set_segments(int v)        { segments = CLAMP(v, 1, 1000); sucio = true; }
    int   get_segments() const       { return segments; }
    void  set_curve0(float v)        { curve0 = v; sucio = true; }
    float get_curve0() const         { return curve0; }
    void  set_curve1(float v)        { curve1 = v; sucio = true; }
    float get_curve1() const         { return curve1; }
    void  set_texture_speed(float v) { texture_speed = v; if (malla) _rehacer_material(); }
    float get_texture_speed() const  { return texture_speed; }
    void  set_z_offset(float v)      { z_offset = v; }
    float get_z_offset() const       { return z_offset; }
    Beam() { set_meta("_gl_bridge", true); set_meta("_gl_efecto", true); }
};

// ────────────────────────────────────────────────────────────────────
//  Trail: cada frame se apunta donde estan los dos attachments (si se movieron
//  al menos MinLength) y la cinta recorre esas muestras. Lo mas viejo que
//  Lifetime se cae. En las secuencias, el tiempo 0 es el extremo nuevo.
// ────────────────────────────────────────────────────────────────────
class Trail : public GLCinta {
    GDCLASS(Trail, GLCinta);

    float   lifetime = 2.0f;
    float   min_length = 0.1f;
    float   max_length = 0.0f;
    Variant width_scale_v;

    struct Muestra { Vector3 a, b; double t; };
    LocalVector<Muestra> muestras;   // de la mas vieja a la mas nueva

    void _actualizar() override {
        if (!malla) return;
        const double ahora = (double)Time::get_singleton()->get_ticks_usec() / 1e6;
        Node3D* a0 = _att(0);
        Node3D* a1 = _att(1);
        bool cambio = sucio;
        // Las muestras que pasaron de Lifetime se caen (tambien con Enabled
        // apagado: la estela que ya hay se termina de borrar sola).
        uint32_t quitar = 0;
        while (quitar < muestras.size() && ahora - muestras[quitar].t > lifetime) quitar++;
        if (quitar) {
            for (uint32_t i = quitar; i < muestras.size(); i++) muestras[i - quitar] = muestras[i];
            muestras.resize(muestras.size() - quitar);
            cambio = true;
        }
        Vector3 cabeza_a, cabeza_b;
        const bool hay_cabeza = enabled && a0 && a1;
        if (hay_cabeza) {
            cabeza_a = a0->get_global_position();
            cabeza_b = a1->get_global_position();
            const Vector3 medio = (cabeza_a + cabeza_b) * 0.5f;
            if (muestras.is_empty() ||
                medio.distance_to((muestras[muestras.size() - 1].a + muestras[muestras.size() - 1].b) * 0.5f) >= min_length) {
                muestras.push_back({ cabeza_a, cabeza_b, ahora });
                cambio = true;
            }
        }
        if (!cambio && muestras.size() < 2) return;
        sucio = false;
        if (muestras.size() < 2) { _vaciar(); return; }

        const GLSecColor col = gl_sec_color(color_v, Color(1, 1, 1));
        const GLSecNum tr = gl_sec_num(transparency_v, 0.5f);
        const GLSecNum ancho = gl_sec_num(width_scale_v, 1.0f);
        _empezar();
        float recorrido = 0.0f, total = 0.0f;
        int primera = 0;
        // MaxLength: se recorta por el extremo viejo.
        for (int i = (int)muestras.size() - 1; i > 0; i--) {
            total += ((muestras[i].a + muestras[i].b) - (muestras[i - 1].a + muestras[i - 1].b)).length() * 0.5f;
            if (max_length > 0.0f && total > max_length) { primera = i; break; }
        }
        for (int i = (int)muestras.size() - 1; i >= primera; i--) {
            const Muestra& m = muestras[i];
            if (i < (int)muestras.size() - 1)
                recorrido += ((m.a + m.b) - (muestras[i + 1].a + muestras[i + 1].b)).length() * 0.5f;
            const float t = Math::clamp((float)((ahora - m.t) / Math::max(lifetime, 0.001f)), 0.0f, 1.0f);
            const Vector3 medio = (m.a + m.b) * 0.5f;
            const Vector3 media = (m.b - m.a) * 0.5f * ancho.en(t);
            Color c = col.en(t);
            c.a = Math::clamp(1.0f - tr.en(t), 0.0f, 1.0f);
            const float uu = texture_mode == 0 ? t * texture_length : recorrido / texture_length;
            _vertice(medio - media, c, Vector2(uu, 0.0f));
            _vertice(medio + media, c, Vector2(uu, 1.0f));
        }
        _subir();
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_Lifetime", "v"), &Trail::set_lifetime_rbx);
        ClassDB::bind_method(D_METHOD("get_Lifetime"), &Trail::get_lifetime_rbx);
        ClassDB::bind_method(D_METHOD("set_MinLength", "v"), &Trail::set_min_length);
        ClassDB::bind_method(D_METHOD("get_MinLength"), &Trail::get_min_length);
        ClassDB::bind_method(D_METHOD("set_MaxLength", "v"), &Trail::set_max_length);
        ClassDB::bind_method(D_METHOD("get_MaxLength"), &Trail::get_max_length);
        ClassDB::bind_method(D_METHOD("set_WidthScale", "v"), &Trail::set_width_scale);
        ClassDB::bind_method(D_METHOD("get_WidthScale"), &Trail::get_width_scale);
        ClassDB::bind_method(D_METHOD("Clear"), &Trail::Clear);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Lifetime"), "set_Lifetime", "get_Lifetime");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "MinLength"), "set_MinLength", "get_MinLength");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "MaxLength"), "set_MaxLength", "get_MaxLength");
        ADD_PROPERTY(PropertyInfo(Variant::NIL, "WidthScale", PROPERTY_HINT_NONE, "",
                     PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT), "set_WidthScale", "get_WidthScale");
    }

public:
    void  set_lifetime_rbx(float v)  { lifetime = Math::clamp(v, 0.001f, 20.0f); }
    float get_lifetime_rbx() const   { return lifetime; }
    void  set_min_length(float v)    { min_length = Math::max(v, 0.0f); }
    float get_min_length() const     { return min_length; }
    void  set_max_length(float v)    { max_length = Math::max(v, 0.0f); sucio = true; }
    float get_max_length() const     { return max_length; }
    void  set_width_scale(const Variant& v) { width_scale_v = v; sucio = true; }
    Variant get_width_scale() const  { return width_scale_v.get_type() == Variant::NIL ? Variant(gl_sec_num_const(1.0f)) : width_scale_v; }
    void  Clear() { muestras.clear(); sucio = true; if (am.is_valid()) _vaciar(); }
    Trail() { set_meta("_gl_bridge", true); set_meta("_gl_efecto", true); }
};

#endif // ROBLOX_CINTAS_H

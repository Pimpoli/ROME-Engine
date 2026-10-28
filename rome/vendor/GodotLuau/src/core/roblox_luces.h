#ifndef ROBLOX_LUCES_H
#define ROBLOX_LUCES_H

// ════════════════════════════════════════════════════════════════════
//  PointLight, SpotLight y SurfaceLight de Roblox.
//  Antes Instance.new daba un OmniLight3D/SpotLight3D pelado (sin Brightness,
//  Range, Color ni Shadows) y el importador no las conocia: las luces de los
//  places se perdian. Cuelgan de una pieza (o de un Attachment) y alumbran
//  desde ahi; las de foco miran hacia su cara (Face).
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/spot_light3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include "gl_servicios.h"   // gl_en_workspace
#include "gl_caras.h"
#include "gl_luz_propia.h"  // gl_luces_registro: la luz propia de las piezas las lee de ahi

using namespace godot;

// Lo comun a las tres. Una luz guardada en ReplicatedStorage (plantilla que el
// juego clona) no alumbra: en Roblox solo existe lo del Workspace, y aqui su
// Node3D sin padre espacial acabaria alumbrando desde el origen del mundo.
//
// ── Brillo, alcance y forma, MEDIDOS (2026-10-08) ─────────────────────────
// Antorchas de la mina de Build a Mountain (Brightness 1.6, Range 14, a 0,7
// studs de la pared): en Roblox la pared se ilumina casi igual a 1,5 que a 7
// studs, aunque la luz le llega de refilon (N.L de 0,4 a 0,1). Las luces sin
// sombra de Roblox (estilo Soft) alumbran por volumen, casi sin mirar la
// orientacion, sin la caida 1/d y sin brillo especular. Con el Lambert y la
// caida de Godot la pared apenas cambiaba (+13 de rojo junto a la antorcha;
// Roblox +77). Asi sale como en Roblox (luz anadida, lineal, a 3,5/4,6/5,7
// studs: 0,221/0,176/0,160 contra 0,220/0,177/0,165):
//  * energia 0.62 x Brightness y sin caida por distancia (atenuacion 0): solo
//    la ventana de Godot, con el alcance x1.1 para que la cola llegue a Range;
//  * tamaño de la luz = Range: Godot suma 1 - 1/sqrt(1 + (tamaño/d)^2) a N.L
//    (la envoltura de una luz esferica), que es lo que hace parejo el volumen;
//  * sin especular: con la envoltura, el brillo de la antorcha se reflejaba en
//    la cara negra del tunel de la mina (salia marron, 91,62,43 contra 19,18,27).
// Con Shadows = true se queda la luz por pixel (sin envoltura, que ademas
// emborronaria la sombra) y con especular. El color, en lineal gamma 2 como el
// de las piezas (la luz medida va 1 : 0,60 : 0,22, el color 1 : 0,59 : 0,22).
// En el arbol o fuera: la luz propia de las piezas solo mira las apuntadas.
inline void gl_luz_apuntar(Node* n, int p_what) {
    if (p_what == Node::NOTIFICATION_ENTER_TREE) gl_luces_registro()[(uint64_t)n->get_instance_id()] = false;
    else if (p_what == Node::NOTIFICATION_EXIT_TREE) gl_luces_registro().erase((uint64_t)n->get_instance_id());
}

#define GL_LUZ_COMUN(CLASE)                                                              \
    float brillo = 1.0f, alcance = 8.0f;                                                 \
    Color color = Color(1, 1, 1);                                                        \
    bool  sombras = false, encendida = true;                                             \
    void _aplicar_comun() {                                                              \
        const float r = Math::clamp(alcance, 0.0f, 60.0f);                               \
        set_param(Light3D::PARAM_ENERGY, Math::max(brillo, 0.0f) * 0.62f);               \
        set_param(Light3D::PARAM_RANGE, r * 1.1f);                                       \
        set_param(Light3D::PARAM_ATTENUATION, 0.0f);                                     \
        set_param(Light3D::PARAM_SIZE, sombras ? 0.0f : r);                              \
        set_param(Light3D::PARAM_SPECULAR, sombras ? 1.0f : 0.0f);                       \
        set_color(Color(color.r * color.r, color.g * color.g, color.b * color.b).linear_to_srgb()); \
        set_shadow(sombras);                                                             \
        /* Con la luz propia la pinta nuestro shader (gl_sonda.h la elige): para   */    \
        /* Godot no existe, y asi no paga agruparla cada frame (0,12 ms con 61).   */    \
        const bool alumbra = encendida && (!is_inside_tree() || gl_en_workspace(this));  \
        set_visible(alumbra && !gl_luz_propia_activa());                                 \
        if (bool* e = gl_luces_registro().getptr((uint64_t)get_instance_id()))          \
            *e = alumbra && is_inside_tree();                                            \
    }                                                                                    \
    static void _bind_comun() {                                                          \
        ClassDB::bind_method(D_METHOD("set_Brightness", "v"), &CLASE::set_brillo);       \
        ClassDB::bind_method(D_METHOD("get_Brightness"), &CLASE::get_brillo);            \
        ClassDB::bind_method(D_METHOD("set_Range", "v"), &CLASE::set_alcance);           \
        ClassDB::bind_method(D_METHOD("get_Range"), &CLASE::get_alcance);                \
        ClassDB::bind_method(D_METHOD("set_Color", "v"), &CLASE::set_color_rbx);         \
        ClassDB::bind_method(D_METHOD("get_Color"), &CLASE::get_color_rbx);              \
        ClassDB::bind_method(D_METHOD("set_Shadows", "v"), &CLASE::set_sombras);         \
        ClassDB::bind_method(D_METHOD("get_Shadows"), &CLASE::get_sombras);              \
        ClassDB::bind_method(D_METHOD("set_Enabled", "v"), &CLASE::set_encendida);       \
        ClassDB::bind_method(D_METHOD("get_Enabled"), &CLASE::get_encendida);            \
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Brightness"), "set_Brightness", "get_Brightness"); \
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Range", PROPERTY_HINT_RANGE, "0,60,0.1"), "set_Range", "get_Range"); \
        ADD_PROPERTY(PropertyInfo(Variant::COLOR, "Color"), "set_Color", "get_Color");   \
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Shadows"), "set_Shadows", "get_Shadows"); \
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Enabled"), "set_Enabled", "get_Enabled"); \
    }                                                                                    \
public:                                                                                  \
    void  set_brillo(float v)          { brillo = v; _aplicar(); }                       \
    float get_brillo() const           { return brillo; }                                \
    void  set_alcance(float v)         { alcance = v; _aplicar(); }                      \
    float get_alcance() const          { return alcance; }                               \
    void  set_color_rbx(const Color& c){ color = c; _aplicar(); }                        \
    Color get_color_rbx() const        { return color; }                                 \
    void  set_sombras(bool v)          { sombras = v; _aplicar(); }                      \
    bool  get_sombras() const          { return sombras; }                               \
    void  set_encendida(bool v)        { encendida = v; _aplicar(); }                    \
    bool  get_encendida() const        { return encendida; }

class PointLight : public OmniLight3D {
    GDCLASS(PointLight, OmniLight3D);
    void _aplicar() { _aplicar_comun(); }
protected:
    static void _bind_methods() { _bind_comun(); }
    void _notification(int p_what) { gl_luz_apuntar(this, p_what); if (p_what == NOTIFICATION_ENTER_TREE) _aplicar(); }
    GL_LUZ_COMUN(PointLight)
    PointLight() { set_meta("_gl_bridge", true); set_meta("_gl_efecto", true); _aplicar(); }
};

// Foco: Angle es el cono ENTERO (Godot pide el medio angulo) y Face la cara de
// la pieza hacia la que alumbra (Front por defecto).
#define GL_LUZ_FOCO(CLASE)                                                               \
    float angulo = 90.0f;                                                                \
    int   cara = 5;                                                                      \
    static void _bind_foco() {                                                           \
        ClassDB::bind_method(D_METHOD("set_Angle", "v"), &CLASE::set_angulo);            \
        ClassDB::bind_method(D_METHOD("get_Angle"), &CLASE::get_angulo);                 \
        ClassDB::bind_method(D_METHOD("set_Face", "v"), &CLASE::set_cara);               \
        ClassDB::bind_method(D_METHOD("get_Face"), &CLASE::get_cara);                    \
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Angle", PROPERTY_HINT_RANGE, "0,180,0.1"), "set_Angle", "get_Angle"); \
        ADD_PROPERTY(PropertyInfo(Variant::INT, "Face", PROPERTY_HINT_ENUM,              \
                     "Right,Top,Back,Left,Bottom,Front"), "set_Face", "get_Face");       \
    }                                                                                    \
public:                                                                                  \
    void  set_angulo(float v) { angulo = Math::clamp(v, 0.0f, 180.0f); _aplicar(); }    \
    float get_angulo() const  { return angulo; }                                         \
    void  set_cara(int v)     { cara = CLAMP(v, 0, 5); _aplicar(); }                     \
    int   get_cara() const    { return cara; }

class SpotLight : public SpotLight3D {
    GDCLASS(SpotLight, SpotLight3D);
    void _aplicar() {
        _aplicar_comun();
        set_param(Light3D::PARAM_SPOT_ANGLE, Math::clamp(angulo * 0.5f, 0.1f, 90.0f));
        set_transform(Transform3D(gl_base_mirando_a_cara(cara), Vector3()));
    }
protected:
    static void _bind_methods() { _bind_comun(); _bind_foco(); }
    void _notification(int p_what) { gl_luz_apuntar(this, p_what); if (p_what == NOTIFICATION_ENTER_TREE) _aplicar(); }
    GL_LUZ_COMUN(SpotLight)
    GL_LUZ_FOCO(SpotLight)
    SpotLight() { set_meta("_gl_bridge", true); set_meta("_gl_efecto", true); _aplicar(); }
};

// SurfaceLight: alumbra desde la CARA de la pieza, no desde su centro. Se hace
// con un foco puesto sobre esa cara.
class SurfaceLight : public SpotLight3D {
    GDCLASS(SurfaceLight, SpotLight3D);
    void _aplicar() {
        _aplicar_comun();
        set_param(Light3D::PARAM_SPOT_ANGLE, Math::clamp(angulo * 0.5f, 0.1f, 90.0f));
        Vector3 tam;
        if (Node* p = get_parent())
            if (p->is_class("RobloxPart")) tam = (Vector3)p->get("Size");
        set_transform(Transform3D(gl_base_mirando_a_cara(cara),
                                  gl_normal_de_cara(cara) * gl_media_cara(tam, cara)));
    }
protected:
    static void _bind_methods() { _bind_comun(); _bind_foco(); }
    void _notification(int p_what) { gl_luz_apuntar(this, p_what); if (p_what == NOTIFICATION_ENTER_TREE) _aplicar(); }
    GL_LUZ_COMUN(SurfaceLight)
    GL_LUZ_FOCO(SurfaceLight)
    SurfaceLight() { set_meta("_gl_bridge", true); set_meta("_gl_efecto", true); _aplicar(); }
};

#undef GL_LUZ_COMUN
#undef GL_LUZ_FOCO

#endif // ROBLOX_LUCES_H

#ifndef GL_ADORNOS_H
#define GL_ADORNOS_H

// ════════════════════════════════════════════════════════════════════
//  HandleAdornment de Roblox: BoxHandleAdornment, SphereHandleAdornment,
//  CylinderHandleAdornment, ConeHandleAdornment y LineHandleAdornment.
//
//  Antes no existian (solo sus nombres en las tablas generadas). En Build a
//  Mountain el aro de alcance lo crea el cliente (BuildAMountainClient.Alcance)
//  con Instance.new("CylinderHandleAdornment") y lo retoca en cada
//  RenderStepped (Radius con latido, InnerRadius, Color3, Transparency): aqui
//  daba "Clase no encontrada", el pcall se lo tragaba y el aro no salia.
//
//  Lo que dice la documentacion publica (create.roblox.com y robloxapi):
//   - se dibuja en el centro de su Adornee; SizeRelativeOffset lo mueve en
//     fracciones de medio Size del Adornee (1 = el borde) y despues va CFrame;
//   - Shading (AdornShading): Default sin luz y tapado por la escena como un
//     objeto normal; Shaded/XRayShaded con la luz de la escena; XRay y
//     XRayShaded se ven a traves de lo que los tapa; AlwaysOnTop por encima de
//     todo sin prueba de profundidad;
//   - la propiedad vieja AlwaysOnTop solo cuenta con Shading Default, y con
//     ZIndex -1 se ignora (se ordena por profundidad); ZIndex 0..10 ordena;
//   - MouseButton1Down/Up y MouseEnter/Leave solo saltan si cuelga de un
//     PlayerGui o del CoreGui;
//   - valores por defecto: Box Size 1,1,1; Sphere Radius 1; Cylinder Radius 1,
//     Height 1, InnerRadius 0, Angle 360; Cone Radius 0.5, Height 2, Hollow
//     false; Line Length 5, Thickness 1 (PIXELES).
//  Lo que la documentacion NO dice y se eligio aqui (no se puede medir sin
//  Studio; pedido en el informe):
//   - el cilindro va a lo largo de Z y centrado: el aro de BAM, girado 90 en
//     X, queda tumbado en el suelo como en la foto de Roblox (rb_1);
//   - Angle empieza en +X y gira hacia +Y;
//   - el cono tiene la base en el CFrame y la punta hacia -Z (LookVector), y
//     la linea sale del CFrame hacia -Z: con CFrame.lookAt(a, b) apuntan a b.
//
//  El dibujo es una MeshInstance3D interna (sin sombra) con un ShaderMaterial
//  propio. El cilindro (aro, tubo, sector) es UNA malla compartida que coloca
//  el shader con Radius/InnerRadius/Height/Angle: cambiar Radius cada frame
//  (el latido del aro) es tocar un uniform, sin rehacer la malla.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <vector>

#include "lua.h"
#include "gl_cast.h"
#include "gl_runtime.h"        // gl_ref_de, gl_state_alive, GodotObjectWrapper
#include "gl_servicios.h"      // gl_en_workspace
#include "gl_clone.h"          // GL_META_INTERNO
#include "gl_tono_roblox.h"    // la curva de tonos de Roblox (para deshacerla)
#include "roblox_lighting_fx.h"   // _lx_get_env, ColorCorrectionEffect
#include "roblox_part.h"
#include "rbx_instances.h"     // RBXModel

using namespace godot;

// ── El color sin luz ─────────────────────────────────────────────────────────
// MEDIDO en la foto de Roblox rb_1 (el aro de BAM: Color3 170,176,186,
// Transparency 0.3, sobre cesped que sale 135,217,98): el centro del aro sale
// (168,196,168). Su luminancia (184) queda POR ENCIMA del propio Color3 (175)
// y del cesped (179), asi que no es la mezcla de los dos tal cual: el color va
// a lineal con gamma 2 (c*c, como el de las piezas) y se ve como sRGB(c*c),
// SIN curva de tonos (con ella, R saldria 186 en vez de 168). En Godot todo el
// 3D pasa por la curva de Roblox (gl_tono_roblox.h) y por los
// ColorCorrection de Lighting, asi que aqui se deshacen: se pinta en el HDR el
// valor que, tras exposicion, curva y correcciones, da sRGB(c*c) en pantalla.
// Medido con el mismo aro y la misma camara: aro menos cesped de al lado
// (29,-22,71) aqui frente a (33,-22,71) en Roblox. Sin deshacer la curva, el
// rojo del Color3 (170) saldria a 198 en vez de 177 (cuenta con la curva).
struct GLAdornoTono {
    uint64_t frame = UINT64_MAX;
    uint64_t mundo = 0;
    float exposicion = 1.0f;
    float tope = 1e9f;   // por debajo del umbral del Bloom (ver gl_adorno_tono)
    LocalVector<GLCorreccion> cc;
    uint32_t version = 0;
};
inline GLAdornoTono& gl_adorno_tono_estado() { static GLAdornoTono t; return t; }

// Exposicion y ColorCorrection activos del mundo de `n`, leidos una vez por
// frame para todos los adornos. `version` cambia si cambio algo.
inline const GLAdornoTono& gl_adorno_tono(Node* n) {
    GLAdornoTono& t = gl_adorno_tono_estado();
    const uint64_t f = Engine::get_singleton()->get_process_frames();
    Node* mundo = _lx_mundo_de(n);
    const uint64_t m = mundo ? (uint64_t)mundo->get_instance_id() : 0;
    if (t.frame == f && t.mundo == m) return t;
    t.frame = f;
    t.mundo = m;
    float e = 1.0f;
    // En Roblox el adorno no da resplandor (va despues del posproceso), pero
    // aqui un blanco o un rojo puro piden un HDR de 2 (el white de la curva)
    // y el Bloom de BAM empieza en 1,6. Se queda justo por debajo del umbral:
    // como mucho ~5/255 menos en los colores casi blancos, y nunca baja de 1.
    float tope = 1e9f;
    const Ref<Environment> env = _lx_get_env(n);
    if (env.is_valid()) {
        e = env->get_tonemap_exposure();
        if (env->is_glow_enabled()) tope = Math::max(1.0f, env->get_glow_hdr_bleed_threshold() * 0.98f);
    }
    LocalVector<GLCorreccion> cc;
    static const NodePath k_lighting("Lighting");
    Node* l = mundo ? mundo->get_node_or_null(k_lighting) : nullptr;
    if (l) {
        for (int i = 0; i < l->get_child_count(); i++) {
            const ColorCorrectionEffect* c = gl_cast<ColorCorrectionEffect>(l->get_child(i));
            if (c && c->gl_activa()) cc.push_back(c->gl_correccion());
        }
    }
    bool igual = e == t.exposicion && tope == t.tope && cc.size() == t.cc.size();
    for (uint32_t i = 0; igual && i < cc.size(); i++)
        igual = cc[i].brillo == t.cc[i].brillo && cc[i].contraste == t.cc[i].contraste &&
                cc[i].saturacion == t.cc[i].saturacion && cc[i].tinte == t.cc[i].tinte;
    if (!igual) {
        t.exposicion = e;
        t.tope = tope;
        t.cc = cc;
        t.version++;
    }
    return t;
}

// gl_tono_aces es monotona: se invierte por biseccion (sigue a la curva aunque
// cambie su white o su formula).
inline float gl_adorno_aces_inversa(float a) {
    a = Math::clamp(a, 0.0f, 1.0f);
    if (a <= 0.0f) return 0.0f;
    float lo = 0.0f, hi = 1.0f;
    while (gl_tono_aces(hi) < a && hi < 4096.0f) hi *= 2.0f;
    for (int i = 0; i < 40; i++) {
        const float mid = 0.5f * (lo + hi);
        if (gl_tono_aces(mid) < a) lo = mid; else hi = mid;
    }
    return 0.5f * (lo + hi);
}

// La inversa de gl_tono_correccion (sin los recortes a 0..1).
inline void gl_adorno_cc_inversa(const GLCorreccion& k, float* c) {
    const float tin[3] = { k.tinte.r, k.tinte.g, k.tinte.b };
    for (int i = 0; i < 3; i++) if (tin[i] > 0.0001f) c[i] /= tin[i];
    // La saturacion deja igual la luminancia (los pesos suman 1).
    const float l = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
    const float s = 1.0f + k.saturacion;
    if (Math::abs(s) > 0.0001f) for (int i = 0; i < 3; i++) c[i] = l + (c[i] - l) / s;
    const float co = 1.0f + k.contraste;
    if (Math::abs(co) > 0.0001f) for (int i = 0; i < 3; i++) c[i] = (c[i] - 0.5f - k.brillo) / co + 0.5f;
    for (int i = 0; i < 3; i++) c[i] = Math::clamp(c[i], 0.0f, 1.0f);
}

inline Color gl_adorno_color_hdr(const Color& c, const GLAdornoTono& t) {
    float d[3] = { gl_tono_srgb(c.r * c.r), gl_tono_srgb(c.g * c.g), gl_tono_srgb(c.b * c.b) };
    for (int i = (int)t.cc.size() - 1; i >= 0; i--) gl_adorno_cc_inversa(t.cc[i], d);
    const float e = Math::max(t.exposicion, 0.0001f);
    return Color(Math::min(gl_adorno_aces_inversa(gl_tono_lineal(d[0])) / e, t.tope),
                 Math::min(gl_adorno_aces_inversa(gl_tono_lineal(d[1])) / e, t.tope),
                 Math::min(gl_adorno_aces_inversa(gl_tono_lineal(d[2])) / e, t.tope));
}

// ── Shaders ─────────────────────────────────────────────────────────────────
// Variante por bits: 1 con luz, 2 sin prueba de profundidad, 4 transparente,
// 8 a dos caras, 16..48 tipo de malla (0 normal, 1 tubo, 2 linea).
enum {
    GL_ADORNO_LUZ = 1, GL_ADORNO_ENCIMA = 2, GL_ADORNO_TRANSP = 4, GL_ADORNO_DOBLE = 8,
    GL_ADORNO_TUBO = 16, GL_ADORNO_LINEA = 32
};
inline Ref<Shader>& gl_adorno_shader_ref(int v) { static Ref<Shader> s[64]; return s[v & 63]; }

inline Ref<Shader> gl_adorno_shader(int v) {
    Ref<Shader>& s = gl_adorno_shader_ref(v);
    if (s.is_valid()) return s;
    const bool luz = v & GL_ADORNO_LUZ;
    // Sin niebla: en Roblox el adorno se pinta tal cual (ver el color arriba).
    String modos = String("blend_mix, shadows_disabled, fog_disabled, ") +
                   ((v & GL_ADORNO_DOBLE) ? "cull_disabled" : "cull_back");
    modos += luz ? ", diffuse_lambert, specular_schlick_ggx" : ", unshaded";
    if (v & GL_ADORNO_ENCIMA) modos += ", depth_test_disabled";
    String c = String("shader_type spatial;\nrender_mode ") + modos + ";\n"
        // Ya en lineal: sin luz es el valor HDR calculado en C++ (gl_adorno_color_hdr);
        // con luz, el albedo c*c como el de las piezas.
        "uniform vec3 color = vec3(1.0);\n"
        "uniform float alfa = 1.0;\n";
    if (v & GL_ADORNO_TUBO) {
        // UV.x = fraccion de la vuelta, UV.y = 0 dentro / 1 fuera,
        // UV2.x = -0.5 abajo / 0.5 arriba, UV2.y = cara: 0 pared de fuera,
        // 1 pared de dentro, 2 tapa de arriba, 3 tapa de abajo, 4 extremo del
        // principio, 5 extremo del final.
        c += "uniform float radio = 1.0;\n"
             "uniform float radio_int = 0.0;\n"
             "uniform float alto = 1.0;\n"
             "uniform float angulo = 6.2831853;\n"
             "void vertex() {\n"
             "    float ri = clamp(radio_int, 0.0, radio);\n"
             "    float a = UV.x * angulo;\n"
             "    vec2 cs = vec2(cos(a), sin(a));\n"
             "    VERTEX = vec3(cs * mix(ri, radio, UV.y), UV2.x * alto);\n"
             "    int cara = int(UV2.y + 0.5);\n"
             "    if (cara == 0) NORMAL = vec3(cs, 0.0);\n"
             "    else if (cara == 1) NORMAL = vec3(-cs, 0.0);\n"
             "    else if (cara == 2) NORMAL = vec3(0.0, 0.0, 1.0);\n"
             "    else if (cara == 3) NORMAL = vec3(0.0, 0.0, -1.0);\n"
             "    else if (cara == 4) NORMAL = vec3(0.0, -1.0, 0.0);\n"
             "    else NORMAL = vec3(-sin(angulo), cos(angulo), 0.0);\n"
             // Vuelta entera: los dos extremos quedan dentro y no deben verse
             // (con transparencia o encima de todo se veria un tabique).
             "    if (cara >= 4 && angulo > 6.2830) VERTEX = vec3(0.0);\n"
             "}\n";
    } else if (v & GL_ADORNO_LINEA) {
        // Thickness va en PIXELES: el ancho se pone en pantalla. UV.x = 0 el
        // principio (el CFrame) / 1 el final (Length hacia -Z); UV.y = lado.
        c += "uniform float largo = 5.0;\n"
             "uniform float grosor = 1.0;\n"
             "void vertex() {\n"
             "    vec4 a = PROJECTION_MATRIX * (MODELVIEW_MATRIX * vec4(0.0, 0.0, 0.0, 1.0));\n"
             "    vec4 b = PROJECTION_MATRIX * (MODELVIEW_MATRIX * vec4(0.0, 0.0, -largo, 1.0));\n"
             "    vec2 sa = a.xy / max(abs(a.w), 0.0001) * VIEWPORT_SIZE;\n"
             "    vec2 sb = b.xy / max(abs(b.w), 0.0001) * VIEWPORT_SIZE;\n"
             "    vec2 d = sb - sa;\n"
             "    vec2 dir = length(d) > 0.0001 ? normalize(d) : vec2(1.0, 0.0);\n"
             "    vec2 n = vec2(-dir.y, dir.x);\n"
             "    vec4 p = UV.x < 0.5 ? a : b;\n"
             "    p.xy += n * UV.y * max(grosor, 1.0) / VIEWPORT_SIZE * p.w;\n"
             "    POSITION = p;\n"
             "}\n";
    }
    c += "void fragment() {\n"
         "    ALBEDO = color;\n";
    if (luz) c += "    ROUGHNESS = 1.0;\n";
    if (v & GL_ADORNO_TRANSP) c += "    ALPHA = alfa;\n";
    c += "}\n";
    s.instantiate();
    s->set_code(c);
    return s;
}

// ── Mallas compartidas ──────────────────────────────────────────────────────
// 0 caja unidad, 1 esfera de radio 1, 2 cono macizo, 3 cono hueco, 4 tubo, 5 linea.
inline Ref<Mesh>& gl_adorno_malla_ref(int i) { static Ref<Mesh> m[6]; return m[i]; }

// Un triangulo del tubo: se orienta con su normal de fuera en una forma de
// referencia (radio 1, interior 0.5, alto 1, media vuelta) y se escribe en el
// sentido HORARIO visto desde fuera, que es el que Godot dibuja de frente
// (mismo criterio que gl_malla_de_triangulos, gl_cunas.h).
struct GLAdornoVert { float u, r, h, cara; };
inline Vector3 _gl_adorno_ref(const GLAdornoVert& p) {
    const float a = p.u * (float)Math_PI;
    const float r = 0.5f + 0.5f * p.r;
    return Vector3(r * Math::cos(a), r * Math::sin(a), p.h);
}
inline void _gl_adorno_tri(PackedVector3Array& v, PackedVector2Array& uv, PackedVector2Array& uv2,
                           GLAdornoVert a, GLAdornoVert b, GLAdornoVert c, const Vector3& fuera) {
    const Vector3 pa = _gl_adorno_ref(a), pb = _gl_adorno_ref(b), pc = _gl_adorno_ref(c);
    // (b-a)x(c-a) hacia fuera = antihorario visto desde fuera: se cambia b por c.
    if ((pb - pa).cross(pc - pa).dot(fuera) >= 0.0f) { GLAdornoVert t = b; b = c; c = t; }
    const GLAdornoVert l[3] = { a, b, c };
    for (int i = 0; i < 3; i++) {
        v.push_back(_gl_adorno_ref(l[i]));
        uv.push_back(Vector2(l[i].u, l[i].r));
        uv2.push_back(Vector2(l[i].h, l[i].cara));
    }
}
inline void _gl_adorno_quad(PackedVector3Array& v, PackedVector2Array& uv, PackedVector2Array& uv2,
                            const GLAdornoVert& a, const GLAdornoVert& b, const GLAdornoVert& c,
                            const GLAdornoVert& d, const Vector3& fuera) {
    _gl_adorno_tri(v, uv, uv2, a, b, c, fuera);
    _gl_adorno_tri(v, uv, uv2, a, c, d, fuera);
}

// 64 tramos: el aro de BAM (radio 35) se aparta del circulo 0,04 studs.
inline Ref<Mesh> gl_adorno_malla_tubo() {
    Ref<Mesh>& m = gl_adorno_malla_ref(4);
    if (m.is_valid()) return m;
    const int N = 64;
    PackedVector3Array v;
    PackedVector2Array uv, uv2;
    for (int i = 0; i < N; i++) {
        const float u0 = (float)i / N, u1 = (float)(i + 1) / N;
        const float am = 0.5f * (u0 + u1) * (float)Math_PI;
        const Vector3 radial(Math::cos(am), Math::sin(am), 0.0f);
        _gl_adorno_quad(v, uv, uv2, { u0, 1, -0.5f, 0 }, { u1, 1, -0.5f, 0 }, { u1, 1, 0.5f, 0 }, { u0, 1, 0.5f, 0 }, radial);
        _gl_adorno_quad(v, uv, uv2, { u0, 0, -0.5f, 1 }, { u1, 0, -0.5f, 1 }, { u1, 0, 0.5f, 1 }, { u0, 0, 0.5f, 1 }, -radial);
        _gl_adorno_quad(v, uv, uv2, { u0, 0, 0.5f, 2 }, { u1, 0, 0.5f, 2 }, { u1, 1, 0.5f, 2 }, { u0, 1, 0.5f, 2 }, Vector3(0, 0, 1));
        _gl_adorno_quad(v, uv, uv2, { u0, 0, -0.5f, 3 }, { u1, 0, -0.5f, 3 }, { u1, 1, -0.5f, 3 }, { u0, 1, -0.5f, 3 }, Vector3(0, 0, -1));
    }
    // Los dos extremos del sector (con la media vuelta de referencia, los dos
    // miran a -Y).
    _gl_adorno_quad(v, uv, uv2, { 0, 0, -0.5f, 4 }, { 0, 1, -0.5f, 4 }, { 0, 1, 0.5f, 4 }, { 0, 0, 0.5f, 4 }, Vector3(0, -1, 0));
    _gl_adorno_quad(v, uv, uv2, { 1, 0, -0.5f, 5 }, { 1, 1, -0.5f, 5 }, { 1, 1, 0.5f, 5 }, { 1, 0, 0.5f, 5 }, Vector3(0, -1, 0));
    Array arr;
    arr.resize(ArrayMesh::ARRAY_MAX);
    arr[ArrayMesh::ARRAY_VERTEX] = v;
    arr[ArrayMesh::ARRAY_TEX_UV] = uv;
    arr[ArrayMesh::ARRAY_TEX_UV2] = uv2;
    Ref<ArrayMesh> am;
    am.instantiate();
    am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
    m = am;
    return m;
}

inline Ref<Mesh> gl_adorno_malla_linea() {
    Ref<Mesh>& m = gl_adorno_malla_ref(5);
    if (m.is_valid()) return m;
    PackedVector3Array v;
    PackedVector2Array uv;
    const Vector2 q[6] = { Vector2(0, -1), Vector2(1, -1), Vector2(1, 1), Vector2(0, -1), Vector2(1, 1), Vector2(0, 1) };
    for (int i = 0; i < 6; i++) {
        v.push_back(Vector3(0, 0, -q[i].x));   // la colocacion de verdad la hace el shader
        uv.push_back(q[i]);
    }
    Array arr;
    arr.resize(ArrayMesh::ARRAY_MAX);
    arr[ArrayMesh::ARRAY_VERTEX] = v;
    arr[ArrayMesh::ARRAY_TEX_UV] = uv;
    Ref<ArrayMesh> am;
    am.instantiate();
    am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
    m = am;
    return m;
}

inline Ref<Mesh> gl_adorno_malla_caja() {
    Ref<Mesh>& m = gl_adorno_malla_ref(0);
    if (m.is_null()) {
        Ref<BoxMesh> b;
        b.instantiate();
        b->set_size(Vector3(1, 1, 1));
        m = b;
    }
    return m;
}
inline Ref<Mesh> gl_adorno_malla_esfera() {
    Ref<Mesh>& m = gl_adorno_malla_ref(1);
    if (m.is_null()) {
        Ref<SphereMesh> e;
        e.instantiate();
        e->set_radius(1.0f);
        e->set_height(2.0f);
        e->set_radial_segments(48);
        e->set_rings(24);
        m = e;
    }
    return m;
}
// Cono de la CylinderMesh de Godot: eje Y, centrado, punta arriba. El giro y
// el desplazamiento a "base en el CFrame, punta hacia -Z" van en _local().
inline Ref<Mesh> gl_adorno_malla_cono(bool hueco) {
    Ref<Mesh>& m = gl_adorno_malla_ref(hueco ? 3 : 2);
    if (m.is_null()) {
        Ref<CylinderMesh> c;
        c.instantiate();
        c->set_top_radius(0.0f);
        c->set_bottom_radius(1.0f);
        c->set_height(1.0f);
        c->set_radial_segments(32);
        c->set_rings(1);
        c->set_cap_top(false);
        c->set_cap_bottom(!hueco);
        m = c;
    }
    return m;
}

inline void gl_adornos_liberar() {
    for (int i = 0; i < 64; i++) gl_adorno_shader_ref(i).unref();
    for (int i = 0; i < 6; i++) gl_adorno_malla_ref(i).unref();
}

// Rayo contra la caja [mn, mx]: el tramo [t0, t1] dentro (false si no la toca).
inline bool _gl_adorno_rayo_caja(const Vector3& o, const Vector3& d, const Vector3& mn, const Vector3& mx,
                                 float& t0, float& t1) {
    t0 = 0.0f;
    t1 = 1e30f;
    for (int i = 0; i < 3; i++) {
        if (Math::abs(d[i]) < 1e-8f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return false;
            continue;
        }
        float a = (mn[i] - o[i]) / d[i], b = (mx[i] - o[i]) / d[i];
        if (a > b) { const float t = a; a = b; b = t; }
        t0 = Math::max(t0, a);
        t1 = Math::min(t1, b);
        if (t0 > t1) return false;
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════
//  HandleAdornment (abstracta)
// ════════════════════════════════════════════════════════════════════
class HandleAdornment : public Node {
    GDCLASS(HandleAdornment, Node);

public:
    struct LuaCallback { lua_State* main_L; int ref; bool active = true; };

protected:
    // ── Propiedades de Roblox (HandleAdornment, PVAdornment, GuiBase3d) ──
    bool always_on_top = false;
    Transform3D cframe;
    Vector3 size_relative_offset;
    int z_index = -1;
    int adorn_culling_mode = 0;   // Enum.AdornCullingMode: Automatic, Never
    Color color3 = Color(13.0f / 255.0f, 105.0f / 255.0f, 172.0f / 255.0f);   // Bright blue
    float transparency = 0.0f;
    bool visible = true;
    int shading = 0;              // Enum.AdornShading (solo Box, Sphere, Cylinder y Cone)

    // ── Dibujo ──
    MeshInstance3D* malla = nullptr;
    Ref<ShaderMaterial> mat;
    int variante = -1;
    uint32_t tono_visto = UINT32_MAX;
    bool material_sucio = true;
    bool forma_sucia = true;
    bool en_mundo = false;        // cuelga de Workspace, de un PlayerGui o del CoreGui
    bool en_gui = false;          // de un PlayerGui o del CoreGui: escucha al raton
    bool xf_valido = false;
    Transform3D xf_puesto;

    // ── Raton ──
    enum { EV_DOWN = 0, EV_UP, EV_ENTER, EV_LEAVE, EV_N };
    std::vector<LuaCallback> cbs[EV_N];
    bool raton_dentro = false;
    bool boton_antes = false;

    // ── Lo que pone cada forma ──
    virtual int _variante_forma() const { return 0; }
    virtual bool _usa_shading() const { return true; }
    virtual Ref<Mesh> _malla() const { return Ref<Mesh>(); }
    // Giro y escala de la malla dentro del marco del adorno.
    virtual Transform3D _local() const { return Transform3D(); }
    virtual void _uniformes() {}
    // Caja de recorte para las mallas que coloca el shader (tubo, linea).
    virtual bool _caja(AABB& c) const { return false; }
    // Rayo en el marco del adorno (sin _local). `cam` solo para la linea.
    virtual bool _toca(const Vector3& o, const Vector3& d, Camera3D* cam, const Transform3D& marco,
                       const Vector2& raton) const { return false; }

    void _cambio_forma() { forma_sucia = true; }
    void _cambio_material() { material_sucio = true; }

    Node3D* _adornee() const {
        static const StringName ks("_glo_Adornee"), ki("Adornee");
        Node3D* a = gl_cast<Node3D>(gl_ref_de(this, ks, ki));
        return (a && a->is_inside_tree()) ? a : nullptr;
    }

    // El Adornee que trae el importador va como NodePath en la meta "Adornee".
    // En marcha pasa a la meta del puente (ObjectID): asi `adorno.Adornee` lo
    // lee desde Luau, y ponerlo a nil no resucita la ruta vieja.
    void _adoptar_adornee_importado() {
        static const StringName ks("_glo_Adornee"), ki("Adornee");
        if (has_meta(ks) || !has_meta(ki)) return;
        const Variant v = get_meta(ki);
        if (v.get_type() != Variant::NODE_PATH) return;
        Node* a = get_node_or_null((NodePath)v);
        if (!a) return;
        set_meta(ks, (int64_t)a->get_instance_id());
        remove_meta(ki);
    }

    bool _bajo_gui() const {
        static const StringName k_pg("PlayerGui"), k_cg("CoreGui");
        for (const Node* p = get_parent(); p; p = p->get_parent()) {
            if (p->get_name() == k_pg || p->get_name() == k_cg || p->is_class("PlayerGui")) return true;
        }
        return false;
    }

    // Centro del Adornee y su tamaño: el de una pieza, o la caja de un Model.
    void _centro_adornee(Node3D* ad, Transform3D& centro, Vector3& tam) const {
        if (RobloxPart* p = gl_cast<RobloxPart>(ad)) {
            centro = p->get_global_transform();
            centro.basis.orthonormalize();
            tam = p->get_size();
            return;
        }
        if (RBXModel* m = gl_cast<RBXModel>(ad)) {
            const AABB c = m->get_bounding_box();
            if (c.size != Vector3()) {
                centro = Transform3D(Basis(), c.get_center());
                tam = c.size;
                return;
            }
        }
        centro = ad->get_global_transform();
        centro.basis.orthonormalize();
        tam = Vector3();
    }

    Transform3D _marco(Node3D* ad) const {
        Transform3D centro;
        Vector3 tam;
        _centro_adornee(ad, centro, tam);
        return centro * Transform3D(Basis(), size_relative_offset * tam * 0.5f) * cframe;
    }

    void _ocultar() {
        if (malla && malla->is_visible()) malla->set_visible(false);
    }

    void _asegurar_malla() {
        if (malla) return;
        malla = memnew(MeshInstance3D);
        malla->set_meta(GL_META_INTERNO, true);
        malla->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        malla->set_gi_mode(GeometryInstance3D::GI_MODE_DISABLED);
        malla->set_visible(false);
        add_child(malla, false, Node::INTERNAL_MODE_BACK);
        mat.instantiate();
        malla->set_material_override(mat);
        variante = -1;
        forma_sucia = material_sucio = true;
        xf_valido = false;
    }

    void _aplicar_material(const GLAdornoTono& t) {
        const int s = _usa_shading() ? shading : 0;
        // AlwaysOnTop viejo: solo con Shading Default, y ZIndex -1 lo anula.
        const bool encima_viejo = s == 0 && always_on_top && z_index != -1;
        const bool luz = s == 1 || s == 3;
        const bool sin_prof = s == 2 || s == 3 || s == 4 || encima_viejo;
        const bool transp = transparency > 0.001f || sin_prof;
        const int v = _variante_forma() | (luz ? GL_ADORNO_LUZ : 0) | (sin_prof ? GL_ADORNO_ENCIMA : 0) |
                      (transp ? GL_ADORNO_TRANSP : 0);
        if (v != variante) {
            mat->set_shader(gl_adorno_shader(v));
            variante = v;
            forma_sucia = true;   // los uniforms de la forma, otra vez
        }
        // Encima de todo: despues de lo demas y por ZIndex (0..10).
        int prio = 0;
        if (s == 4 || encima_viejo) prio = 64 + CLAMP(z_index, 0, 10) * 4;
        else if (sin_prof) prio = 32;
        if (mat->get_render_priority() != prio) mat->set_render_priority(prio);
        static const StringName k_color("color"), k_alfa("alfa");
        const Color c = luz ? Color(color3.r * color3.r, color3.g * color3.g, color3.b * color3.b)
                            : gl_adorno_color_hdr(color3, t);
        mat->set_shader_parameter(k_color, Vector3(c.r, c.g, c.b));
        mat->set_shader_parameter(k_alfa, Math::clamp(1.0f - transparency, 0.0f, 1.0f));
        tono_visto = t.version;
        material_sucio = false;
    }

    void _aplicar_forma() {
        const Ref<Mesh> m = _malla();
        if (malla->get_mesh() != m) malla->set_mesh(m);
        AABB c;
        malla->set_custom_aabb(_caja(c) ? c : AABB());
        _uniformes();
        forma_sucia = false;
        xf_valido = false;
    }

    bool _hay_oyentes() const {
        for (int i = 0; i < EV_N; i++) if (!cbs[i].empty()) return true;
        return false;
    }

    void _disparar(int ev) {
        static const char* nombres[EV_N] = { "MouseButton1Down", "MouseButton1Up", "MouseEnter", "MouseLeave" };
        emit_signal(nombres[ev]);
        std::vector<LuaCallback>& lista = cbs[ev];
        if (lista.empty()) return;
        // Copia: un manejador puede conectar o desconectar y realojar el vector.
        const std::vector<LuaCallback> copia = lista;
        for (const LuaCallback& cb : copia) {
            if (!cb.active || !gl_state_alive(cb.main_L)) continue;
            lua_State* th = lua_newthread(cb.main_L);
            lua_rawgeti(cb.main_L, LUA_REGISTRYINDEX, cb.ref);
            if (lua_isfunction(cb.main_L, -1)) {
                lua_xmove(cb.main_L, th, 1);
                gl_resume_handler(th, 0);
            } else {
                lua_pop(cb.main_L, 1);
            }
            lua_pop(cb.main_L, 1);
        }
        for (int i = (int)lista.size() - 1; i >= 0; --i)
            if (!lista[i].active || !gl_state_alive(lista[i].main_L)) lista.erase(lista.begin() + i);
    }

    // Solo colgado de un PlayerGui o del CoreGui, como en Roblox.
    void _raton(const Transform3D& marco, bool se_ve) {
        bool dentro = false;
        Viewport* vp = get_viewport();
        Camera3D* cam = vp ? vp->get_camera_3d() : nullptr;
        if (se_ve && cam) {
            const Vector2 rp = vp->get_mouse_position();
            const Transform3D inv = marco.affine_inverse();
            const Vector3 o = inv.xform(cam->project_ray_origin(rp));
            const Vector3 d = inv.basis.xform(cam->project_ray_normal(rp)).normalized();
            dentro = _toca(o, d, cam, marco, rp);
        }
        const bool boton = Input::get_singleton()->is_mouse_button_pressed(MOUSE_BUTTON_LEFT);
        if (dentro != raton_dentro) {
            raton_dentro = dentro;
            _disparar(dentro ? EV_ENTER : EV_LEAVE);
        }
        if (dentro && boton && !boton_antes) _disparar(EV_DOWN);
        if (dentro && !boton && boton_antes) _disparar(EV_UP);
        boton_antes = boton;
    }

    void _tick() {
        Node3D* ad = _adornee();
        const bool se_ve = visible && transparency < 0.999f && ad != nullptr;
        if (!se_ve) {
            _ocultar();
            if (en_gui && raton_dentro) { raton_dentro = false; _disparar(EV_LEAVE); }
            return;
        }
        _asegurar_malla();
        const GLAdornoTono& t = gl_adorno_tono(this);
        if (material_sucio || t.version != tono_visto) _aplicar_material(t);
        if (forma_sucia) _aplicar_forma();
        const Transform3D marco = _marco(ad);
        const Transform3D xf = marco * _local();
        if (!xf_valido || !xf.is_equal_approx(xf_puesto)) {
            malla->set_transform(xf);   // su padre no es un Node3D: es su transform de mundo
            xf_puesto = xf;
            xf_valido = true;
        }
        if (!malla->is_visible()) malla->set_visible(true);
        if (en_gui && _hay_oyentes()) _raton(marco, true);
    }

    void _notification(int p_what) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (p_what == NOTIFICATION_ENTER_TREE) {
            en_gui = _bajo_gui();
            // Una plantilla en ReplicatedStorage no se dibuja ni cuesta.
            en_mundo = en_gui || gl_en_workspace(this);
            _adoptar_adornee_importado();
            // Despues de RunService (prioridad 0), que dispara RenderStepped:
            // el aro de BAM mueve su Adornee ahi y asi lo sigue sin un frame de
            // retraso.
            set_process_priority(100);
            set_process(en_mundo);
            if (!en_mundo) _ocultar();
        } else if (p_what == NOTIFICATION_EXIT_TREE) {
            set_process(false);
            _ocultar();
            raton_dentro = false;
        } else if (p_what == NOTIFICATION_PROCESS) {
            _tick();
        }
    }

    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_AlwaysOnTop", "v"), &HandleAdornment::set_always_on_top);
        ClassDB::bind_method(D_METHOD("get_AlwaysOnTop"), &HandleAdornment::get_always_on_top);
        ClassDB::bind_method(D_METHOD("set_CFrame", "v"), &HandleAdornment::set_cframe);
        ClassDB::bind_method(D_METHOD("get_CFrame"), &HandleAdornment::get_cframe);
        ClassDB::bind_method(D_METHOD("set_SizeRelativeOffset", "v"), &HandleAdornment::set_size_relative_offset);
        ClassDB::bind_method(D_METHOD("get_SizeRelativeOffset"), &HandleAdornment::get_size_relative_offset);
        ClassDB::bind_method(D_METHOD("set_ZIndex", "v"), &HandleAdornment::set_z_index);
        ClassDB::bind_method(D_METHOD("get_ZIndex"), &HandleAdornment::get_z_index);
        ClassDB::bind_method(D_METHOD("set_AdornCullingMode", "v"), &HandleAdornment::set_adorn_culling_mode);
        ClassDB::bind_method(D_METHOD("get_AdornCullingMode"), &HandleAdornment::get_adorn_culling_mode);
        ClassDB::bind_method(D_METHOD("set_Color3", "v"), &HandleAdornment::set_color3);
        ClassDB::bind_method(D_METHOD("get_Color3"), &HandleAdornment::get_color3);
        ClassDB::bind_method(D_METHOD("set_Transparency", "v"), &HandleAdornment::set_transparency);
        ClassDB::bind_method(D_METHOD("get_Transparency"), &HandleAdornment::get_transparency);
        ClassDB::bind_method(D_METHOD("set_Visible", "v"), &HandleAdornment::set_visible);
        ClassDB::bind_method(D_METHOD("get_Visible"), &HandleAdornment::get_visible);
        ClassDB::bind_method(D_METHOD("_gl_conectar", "evento", "ref", "vm"), &HandleAdornment::_gl_conectar);
        ClassDB::bind_method(D_METHOD("_gl_disconnect", "ref", "vm"), &HandleAdornment::_gl_disconnect);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "AlwaysOnTop"), "set_AlwaysOnTop", "get_AlwaysOnTop");
        ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "CFrame"), "set_CFrame", "get_CFrame");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "SizeRelativeOffset"), "set_SizeRelativeOffset", "get_SizeRelativeOffset");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "ZIndex", PROPERTY_HINT_RANGE, "-1,10,1"), "set_ZIndex", "get_ZIndex");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "AdornCullingMode", PROPERTY_HINT_ENUM, "Automatic,Never"),
                     "set_AdornCullingMode", "get_AdornCullingMode");
        ADD_PROPERTY(PropertyInfo(Variant::COLOR, "Color3"), "set_Color3", "get_Color3");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Transparency", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_Transparency", "get_Transparency");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Visible"), "set_Visible", "get_Visible");
        ADD_SIGNAL(MethodInfo("MouseButton1Down"));
        ADD_SIGNAL(MethodInfo("MouseButton1Up"));
        ADD_SIGNAL(MethodInfo("MouseEnter"));
        ADD_SIGNAL(MethodInfo("MouseLeave"));
    }

    void _poner_shading(int v) {
        v = CLAMP(v, 0, 4);
        if (v == shading) return;
        shading = v;
        _cambio_material();
    }

public:
    // ── Setters: en BAM se escriben Radius, InnerRadius, Color3 y Transparency
    // EN CADA FRAME; repetir el mismo valor no cuesta nada.
    void set_always_on_top(bool v) { if (v != always_on_top) { always_on_top = v; _cambio_material(); } }
    bool get_always_on_top() const { return always_on_top; }
    void set_cframe(const Transform3D& v) {
        Transform3D t = v;
        t.basis.orthonormalize();   // un CFrame de Roblox no lleva escala
        if (!t.is_equal_approx(cframe)) { cframe = t; xf_valido = false; }
    }
    Transform3D get_cframe() const { return cframe; }
    void set_size_relative_offset(const Vector3& v) { if (v != size_relative_offset) { size_relative_offset = v; xf_valido = false; } }
    Vector3 get_size_relative_offset() const { return size_relative_offset; }
    void set_z_index(int v) { v = CLAMP(v, -1, 10); if (v != z_index) { z_index = v; _cambio_material(); } }
    int get_z_index() const { return z_index; }
    void set_adorn_culling_mode(int v) { adorn_culling_mode = CLAMP(v, 0, 1); }
    int get_adorn_culling_mode() const { return adorn_culling_mode; }
    void set_color3(const Color& v) {
        const Color c(v.r, v.g, v.b, 1.0f);
        if (c != color3) { color3 = c; _cambio_material(); }
    }
    Color get_color3() const { return color3; }
    void set_transparency(float v) { if (v != transparency) { transparency = v; _cambio_material(); } }
    float get_transparency() const { return transparency; }
    void set_visible(bool v) { visible = v; }
    bool get_visible() const { return visible; }

    // Conexiones de Luau (el puente las crea con _gl_conectar; ver el informe
    // de la ola) y Disconnect (_gl_push_connection llama a _gl_disconnect).
    void _gl_conectar(const String& evento, int ref, int64_t vm) {
        static const char* nombres[EV_N] = { "MouseButton1Down", "MouseButton1Up", "MouseEnter", "MouseLeave" };
        for (int i = 0; i < EV_N; i++) {
            if (evento == nombres[i]) {
                cbs[i].push_back({ (lua_State*)(intptr_t)vm, ref, true });
                return;
            }
        }
    }
    void _gl_disconnect(int ref, int64_t vm) {
        for (int i = 0; i < EV_N; i++)
            for (LuaCallback& cb : cbs[i])
                if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
    }

    HandleAdornment() { set_meta("_gl_bridge", true); }
};

// Shading (AdornShading) solo lo tienen Box, Sphere, Cylinder y Cone: se liga
// en cada una con su propio metodo (ligar el de la base desde cada hija lo
// registraria repetido en la base).
#define GL_ADORNO_SHADING(C)                                                                   \
public:                                                                                        \
    void set_Shading(int v) { _poner_shading(v); }                                             \
    int get_Shading() const { return shading; }                                                \
protected:                                                                                     \
    static void _ligar_shading() {                                                             \
        ClassDB::bind_method(D_METHOD("set_Shading", "v"), &C::set_Shading);                   \
        ClassDB::bind_method(D_METHOD("get_Shading"), &C::get_Shading);                        \
        ADD_PROPERTY(PropertyInfo(Variant::INT, "Shading", PROPERTY_HINT_ENUM,                 \
                     "Default,Shaded,XRay,XRayShaded,AlwaysOnTop"), "set_Shading", "get_Shading"); \
    }

// ════════════════════════════════════════════════════════════════════
//  BoxHandleAdornment
// ════════════════════════════════════════════════════════════════════
class BoxHandleAdornment : public HandleAdornment {
    GDCLASS(BoxHandleAdornment, HandleAdornment);
    GL_ADORNO_SHADING(BoxHandleAdornment)

    Vector3 size = Vector3(1, 1, 1);

protected:
    static void _bind_methods() {
        _ligar_shading();
        ClassDB::bind_method(D_METHOD("set_Size", "v"), &BoxHandleAdornment::set_size);
        ClassDB::bind_method(D_METHOD("get_Size"), &BoxHandleAdornment::get_size);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "Size"), "set_Size", "get_Size");
    }
    Ref<Mesh> _malla() const override { return gl_adorno_malla_caja(); }
    Transform3D _local() const override {
        return Transform3D(Basis().scaled(size.max(Vector3(0.0001f, 0.0001f, 0.0001f))), Vector3());
    }
    bool _toca(const Vector3& o, const Vector3& d, Camera3D*, const Transform3D&, const Vector2&) const override {
        float t0, t1;
        return _gl_adorno_rayo_caja(o, d, -size * 0.5f, size * 0.5f, t0, t1);
    }

public:
    void set_size(const Vector3& v) { if (v != size) { size = v; xf_valido = false; } }
    Vector3 get_size() const { return size; }
};

// ════════════════════════════════════════════════════════════════════
//  SphereHandleAdornment
// ════════════════════════════════════════════════════════════════════
class SphereHandleAdornment : public HandleAdornment {
    GDCLASS(SphereHandleAdornment, HandleAdornment);
    GL_ADORNO_SHADING(SphereHandleAdornment)

    float radius = 1.0f;

protected:
    static void _bind_methods() {
        _ligar_shading();
        ClassDB::bind_method(D_METHOD("set_Radius", "v"), &SphereHandleAdornment::set_radius);
        ClassDB::bind_method(D_METHOD("get_Radius"), &SphereHandleAdornment::get_radius);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Radius"), "set_Radius", "get_Radius");
    }
    Ref<Mesh> _malla() const override { return gl_adorno_malla_esfera(); }
    Transform3D _local() const override {
        const float r = Math::max(radius, 0.0001f);
        return Transform3D(Basis().scaled(Vector3(r, r, r)), Vector3());
    }
    bool _toca(const Vector3& o, const Vector3& d, Camera3D*, const Transform3D&, const Vector2&) const override {
        const float b = o.dot(d), c = o.dot(o) - radius * radius;
        const float disc = b * b - c;
        return disc >= 0.0f && (-b + Math::sqrt(disc)) >= 0.0f;
    }

public:
    void set_radius(float v) { if (v != radius) { radius = v; xf_valido = false; } }
    float get_radius() const { return radius; }
};

// ════════════════════════════════════════════════════════════════════
//  CylinderHandleAdornment (el aro de alcance de Build a Mountain)
// ════════════════════════════════════════════════════════════════════
class CylinderHandleAdornment : public HandleAdornment {
    GDCLASS(CylinderHandleAdornment, HandleAdornment);
    GL_ADORNO_SHADING(CylinderHandleAdornment)

    float radius = 1.0f;
    float height = 1.0f;
    float inner_radius = 0.0f;
    float angle = 360.0f;

protected:
    static void _bind_methods() {
        _ligar_shading();
        ClassDB::bind_method(D_METHOD("set_Radius", "v"), &CylinderHandleAdornment::set_radius);
        ClassDB::bind_method(D_METHOD("get_Radius"), &CylinderHandleAdornment::get_radius);
        ClassDB::bind_method(D_METHOD("set_Height", "v"), &CylinderHandleAdornment::set_height);
        ClassDB::bind_method(D_METHOD("get_Height"), &CylinderHandleAdornment::get_height);
        ClassDB::bind_method(D_METHOD("set_InnerRadius", "v"), &CylinderHandleAdornment::set_inner_radius);
        ClassDB::bind_method(D_METHOD("get_InnerRadius"), &CylinderHandleAdornment::get_inner_radius);
        ClassDB::bind_method(D_METHOD("set_Angle", "v"), &CylinderHandleAdornment::set_angle);
        ClassDB::bind_method(D_METHOD("get_Angle"), &CylinderHandleAdornment::get_angle);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Radius"), "set_Radius", "get_Radius");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Height"), "set_Height", "get_Height");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "InnerRadius"), "set_InnerRadius", "get_InnerRadius");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Angle", PROPERTY_HINT_RANGE, "0,360,0.1"), "set_Angle", "get_Angle");
    }
    int _variante_forma() const override { return GL_ADORNO_TUBO; }
    Ref<Mesh> _malla() const override { return gl_adorno_malla_tubo(); }
    void _uniformes() override {
        static const StringName k_r("radio"), k_ri("radio_int"), k_h("alto"), k_a("angulo");
        mat->set_shader_parameter(k_r, Math::max(radius, 0.0f));
        mat->set_shader_parameter(k_ri, Math::max(inner_radius, 0.0f));
        mat->set_shader_parameter(k_h, Math::max(height, 0.0f));
        mat->set_shader_parameter(k_a, Math::deg_to_rad(Math::clamp(angle, 0.0f, 360.0f)));
    }
    bool _caja(AABB& c) const override {
        const float r = Math::max(radius, 0.0001f), h = Math::max(height, 0.0001f);
        c = AABB(Vector3(-r, -r, -h * 0.5f), Vector3(2 * r, 2 * r, h));
        return true;
    }
    // Dentro del tubo: entre los dos radios, dentro del alto y del sector.
    bool _dentro(const Vector3& p) const {
        if (Math::abs(p.z) > height * 0.5f) return false;
        const float r = Vector2(p.x, p.y).length();
        if (r > radius || r < Math::min(inner_radius, radius)) return false;
        if (angle >= 360.0f) return true;
        float a = Math::atan2(p.y, p.x);
        if (a < 0.0f) a += (float)Math_TAU;
        return a <= Math::deg_to_rad(angle);
    }
    bool _toca(const Vector3& o, const Vector3& d, Camera3D*, const Transform3D&, const Vector2&) const override {
        float t0, t1;
        const Vector3 e(radius, radius, height * 0.5f);
        if (!_gl_adorno_rayo_caja(o, d, -e, e, t0, t1)) return false;
        for (int i = 0; i <= 48; i++)
            if (_dentro(o + d * Math::lerp(t0, t1, i / 48.0f))) return true;
        return false;
    }

public:
    void set_radius(float v) { if (v != radius) { radius = v; _cambio_forma(); } }
    float get_radius() const { return radius; }
    void set_height(float v) { if (v != height) { height = v; _cambio_forma(); } }
    float get_height() const { return height; }
    void set_inner_radius(float v) { if (v != inner_radius) { inner_radius = v; _cambio_forma(); } }
    float get_inner_radius() const { return inner_radius; }
    void set_angle(float v) { if (v != angle) { angle = v; _cambio_forma(); } }
    float get_angle() const { return angle; }
};

// ════════════════════════════════════════════════════════════════════
//  ConeHandleAdornment
// ════════════════════════════════════════════════════════════════════
class ConeHandleAdornment : public HandleAdornment {
    GDCLASS(ConeHandleAdornment, HandleAdornment);
    GL_ADORNO_SHADING(ConeHandleAdornment)

    float radius = 0.5f;
    float height = 2.0f;
    bool hollow = false;

protected:
    static void _bind_methods() {
        _ligar_shading();
        ClassDB::bind_method(D_METHOD("set_Radius", "v"), &ConeHandleAdornment::set_radius);
        ClassDB::bind_method(D_METHOD("get_Radius"), &ConeHandleAdornment::get_radius);
        ClassDB::bind_method(D_METHOD("set_Height", "v"), &ConeHandleAdornment::set_height);
        ClassDB::bind_method(D_METHOD("get_Height"), &ConeHandleAdornment::get_height);
        ClassDB::bind_method(D_METHOD("set_Hollow", "v"), &ConeHandleAdornment::set_hollow);
        ClassDB::bind_method(D_METHOD("get_Hollow"), &ConeHandleAdornment::get_hollow);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Radius"), "set_Radius", "get_Radius");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Height"), "set_Height", "get_Height");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Hollow"), "set_Hollow", "get_Hollow");
    }
    // Hueco = sin la tapa de la base: se ve por dentro, a dos caras.
    int _variante_forma() const override { return hollow ? GL_ADORNO_DOBLE : 0; }
    Ref<Mesh> _malla() const override { return gl_adorno_malla_cono(hollow); }
    // La malla de Godot va por Y, centrada y con la punta arriba: se tumba
    // (+Y pasa a -Z) y se adelanta medio alto para dejar la base en el CFrame.
    Transform3D _local() const override {
        const float r = Math::max(radius, 0.0001f), h = Math::max(height, 0.0001f);
        const Basis giro(Vector3(1, 0, 0), -(float)Math_PI * 0.5f);
        return Transform3D(giro.scaled_local(Vector3(r, h, r)), Vector3(0, 0, -h * 0.5f));
    }
    bool _toca(const Vector3& o, const Vector3& d, Camera3D*, const Transform3D&, const Vector2&) const override {
        float t0, t1;
        if (!_gl_adorno_rayo_caja(o, d, Vector3(-radius, -radius, -height), Vector3(radius, radius, 0), t0, t1)) return false;
        for (int i = 0; i <= 48; i++) {
            const Vector3 p = o + d * Math::lerp(t0, t1, i / 48.0f);
            if (Vector2(p.x, p.y).length() <= radius * (1.0f + p.z / Math::max(height, 0.0001f))) return true;
        }
        return false;
    }

public:
    void set_radius(float v) { if (v != radius) { radius = v; xf_valido = false; } }
    float get_radius() const { return radius; }
    void set_height(float v) { if (v != height) { height = v; xf_valido = false; } }
    float get_height() const { return height; }
    void set_hollow(bool v) { if (v != hollow) { hollow = v; _cambio_forma(); _cambio_material(); } }
    bool get_hollow() const { return hollow; }
};

// ════════════════════════════════════════════════════════════════════
//  LineHandleAdornment (sin Shading en Roblox)
// ════════════════════════════════════════════════════════════════════
class LineHandleAdornment : public HandleAdornment {
    GDCLASS(LineHandleAdornment, HandleAdornment);

    float length = 5.0f;
    float thickness = 1.0f;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_Length", "v"), &LineHandleAdornment::set_length);
        ClassDB::bind_method(D_METHOD("get_Length"), &LineHandleAdornment::get_length);
        ClassDB::bind_method(D_METHOD("set_Thickness", "v"), &LineHandleAdornment::set_thickness);
        ClassDB::bind_method(D_METHOD("get_Thickness"), &LineHandleAdornment::get_thickness);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Length"), "set_Length", "get_Length");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Thickness"), "set_Thickness", "get_Thickness");
    }
    bool _usa_shading() const override { return false; }
    int _variante_forma() const override { return GL_ADORNO_LINEA | GL_ADORNO_DOBLE; }
    Ref<Mesh> _malla() const override { return gl_adorno_malla_linea(); }
    void _uniformes() override {
        static const StringName k_l("largo"), k_g("grosor");
        mat->set_shader_parameter(k_l, length);
        mat->set_shader_parameter(k_g, thickness);
    }
    bool _caja(AABB& c) const override {
        c = AABB(Vector3(-0.05f, -0.05f, -Math::max(length, 0.0f) - 0.05f), Vector3(0.1f, 0.1f, Math::max(length, 0.0f) + 0.1f));
        return true;
    }
    // En pantalla: a menos de medio grueso (y al menos 3 px) del segmento.
    bool _toca(const Vector3&, const Vector3&, Camera3D* cam, const Transform3D& marco, const Vector2& rp) const override {
        const Vector3 a3 = marco.origin, b3 = marco.xform(Vector3(0, 0, -length));
        if (cam->is_position_behind(a3) || cam->is_position_behind(b3)) return false;
        const Vector2 a = cam->unproject_position(a3), b = cam->unproject_position(b3);
        const Vector2 ab = b - a;
        const float l2 = ab.length_squared();
        const float t = l2 > 0.0f ? Math::clamp((rp - a).dot(ab) / l2, 0.0f, 1.0f) : 0.0f;
        return (a + ab * t).distance_to(rp) <= Math::max(thickness * 0.5f, 3.0f);
    }

public:
    void set_length(float v) { if (v != length) { length = v; _cambio_forma(); } }
    float get_length() const { return length; }
    void set_thickness(float v) { if (v != thickness) { thickness = v; _cambio_forma(); } }
    float get_thickness() const { return thickness; }
};

#undef GL_ADORNO_SHADING

// La base es abstracta como en Roblox: Instance.new("HandleAdornment") no crea nada.
inline void gl_registro_adornos() {
    ClassDB::register_abstract_class<HandleAdornment>();
    ClassDB::register_class<BoxHandleAdornment>();
    ClassDB::register_class<SphereHandleAdornment>();
    ClassDB::register_class<CylinderHandleAdornment>();
    ClassDB::register_class<ConeHandleAdornment>();
    ClassDB::register_class<LineHandleAdornment>();
}

#endif // GL_ADORNOS_H

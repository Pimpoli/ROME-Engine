#ifndef GL_AVATAR_RIG_H
#define GL_AVATAR_RIG_H

// ════════════════════════════════════════════════════════════════════
//  Avatares R6 y R15 (F8 de la v1.17)
//  ----------------------------------------------------------------
//  Hechos en Blender: piezas propias, un esqueleto con un hueso por junta y
//  animaciones propias (gl_clips_integrados.h). Ni las mallas ni las curvas
//  son de Roblox (el repo es publico); de Roblox solo se toman las MEDIDAS del
//  rig (tamanos, juntas, attachments), que es lo que necesitan sus scripts y
//  sus animaciones para encajar.
//
//  Se leen de GodotLuau/assets/avatars/<r6|r15>/ (rig.json, mallas/*.obj) o
//  de la copia empotrada en la DLL (gl_avatares_gen.h). A escala real: el
//  personaje mide lo que en Roblox (unos 5 studs), como ya median los NPC.
//
//  rig.json (espacio Roblox: studs, Y arriba, mira a -Z, HRP en el origen):
//   { rig, hipHeight, cara, cara_malla,
//     partes: [ { nombre, tamano, cframe[12], color, transparencia, malla } ],
//     juntas: [ { nombre, clase, padre, parte0, parte1, c0[12], c1[12] } ],
//     attachments: [ { nombre, parte, cframe[12] } ] }
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/array_mesh.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/json.hpp>
#include "gl_superficies.h"   // gl_color_pieza
#include <godot_cpp/templates/hash_map.hpp>
#include <cstdlib>
#include <memory>
#include <vector>
#include "gl_animador.h"
#include "roblox_motor6d.h"
#include "roblox_attachment.h"

using namespace godot;

struct GLRigParte { String nombre; Vector3 tamano; Transform3D cf; Color color; float transparencia = 0.0f; String malla; };
struct GLRigJunta { String nombre, padre, p0, p1; Transform3D c0, c1; };
struct GLRigAtt { String nombre, parte; Transform3D cf; };
struct GLRigDatos {
    bool r15 = false;
    float hip = 0.0f;          // HipHeight del Humanoid
    float pies = 3.0f;         // del centro de la HRP a la planta de los pies
    float cima = 2.0f;         // del centro de la HRP a lo alto de la cabeza
    String cara_malla;
    std::vector<GLRigParte> partes;
    std::vector<GLRigJunta> juntas;
    std::vector<GLRigAtt> atts;
};

inline Transform3D gl_ava_cf(const Variant& v) { return gl_anim_cf_json(v); }

// ── OBJ -> ArrayMesh ──────────────────────────────────────────────────
// Espacio Roblox = espacio Godot (los dos Y arriba, mirando a -Z). El OBJ
// viene con las caras en sentido antihorario (lo normal) y Godot toma como
// cara de delante la horaria: se le da la vuelta a cada triangulo. La V de las
// UV va hacia arriba en el OBJ y hacia abajo en Godot.
inline Ref<ArrayMesh> gl_ava_obj(const String& texto) {
    if (texto.is_empty()) return Ref<ArrayMesh>();
    const CharString cs = texto.utf8();
    const char* s = cs.get_data();
    std::vector<Vector3> v, vn;
    std::vector<Vector2> vt;
    PackedVector3Array pos, nor;
    PackedVector2Array uv;
    PackedInt32Array idx;
    HashMap<String, int> unicos;
    auto vertice = [&](const char*& p) -> int {
        int a = (int)strtol(p, (char**)&p, 10), b = 0, c = 0;
        if (*p == '/') {
            p++;
            if (*p != '/') b = (int)strtol(p, (char**)&p, 10);
            if (*p == '/') { p++; c = (int)strtol(p, (char**)&p, 10); }
        }
        const String clave = String::num_int64(a) + "/" + String::num_int64(b) + "/" + String::num_int64(c);
        if (const int* i = unicos.getptr(clave)) return *i;
        const int i = pos.size();
        pos.push_back(a > 0 && a <= (int)v.size() ? v[a - 1] : Vector3());
        uv.push_back(b > 0 && b <= (int)vt.size() ? vt[b - 1] : Vector2());
        nor.push_back(c > 0 && c <= (int)vn.size() ? vn[c - 1] : Vector3(0, 1, 0));
        unicos.insert(clave, i);
        return i;
    };
    while (*s) {
        const char* fin = s;
        while (*fin && *fin != '\n') fin++;
        if (s[0] == 'v' && s[1] == ' ') {
            const char* p = s + 2;
            const float x = strtof(p, (char**)&p), y = strtof(p, (char**)&p), z = strtof(p, (char**)&p);
            v.push_back(Vector3(x, y, z));
        } else if (s[0] == 'v' && s[1] == 't') {
            const char* p = s + 3;
            const float x = strtof(p, (char**)&p), y = strtof(p, (char**)&p);
            vt.push_back(Vector2(x, 1.0f - y));
        } else if (s[0] == 'v' && s[1] == 'n') {
            const char* p = s + 3;
            const float x = strtof(p, (char**)&p), y = strtof(p, (char**)&p), z = strtof(p, (char**)&p);
            vn.push_back(Vector3(x, y, z));
        } else if (s[0] == 'f' && s[1] == ' ') {
            const char* p = s + 2;
            std::vector<int> cara;
            while (p < fin) {
                while (p < fin && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
                if (p >= fin) break;
                cara.push_back(vertice(p));
            }
            for (size_t k = 1; k + 1 < cara.size(); k++) {   // abanico
                idx.push_back(cara[0]);
                idx.push_back(cara[k + 1]);
                idx.push_back(cara[k]);
            }
        }
        s = *fin ? fin + 1 : fin;
    }
    if (idx.is_empty()) return Ref<ArrayMesh>();
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = pos;
    arrays[Mesh::ARRAY_NORMAL] = nor;
    arrays[Mesh::ARRAY_TEX_UV] = uv;
    arrays[Mesh::ARRAY_INDEX] = idx;
    Ref<ArrayMesh> m;
    m.instantiate();
    m->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    return m;
}

// ── Caches (se vacian al descargar la extension, con el RenderingServer vivo) ──
inline HashMap<String, Ref<ArrayMesh>>& gl_ava_mallas() { static HashMap<String, Ref<ArrayMesh>> m; return m; }
inline HashMap<uint32_t, Ref<StandardMaterial3D>>& gl_ava_materiales() { static HashMap<uint32_t, Ref<StandardMaterial3D>> m; return m; }
inline Ref<StandardMaterial3D>& gl_ava_mat_cara() { static Ref<StandardMaterial3D> m; return m; }
inline std::unique_ptr<GLRigDatos>* gl_ava_rigs() { static std::unique_ptr<GLRigDatos> r[2]; return r; }
inline bool* gl_ava_rigs_probados() { static bool p[2] = { false, false }; return p; }

inline void gl_avatar_rig_limpiar() {
    gl_ava_mallas().clear();
    gl_ava_materiales().clear();
    gl_ava_mat_cara().unref();
    gl_ava_rigs()[0].reset(); gl_ava_rigs()[1].reset();
    gl_ava_rigs_probados()[0] = gl_ava_rigs_probados()[1] = false;
}

inline Ref<ArrayMesh> gl_ava_malla(bool r15, const String& rel_malla) {
    const String rel = String(r15 ? "r15/" : "r6/") + rel_malla;
    if (Ref<ArrayMesh>* m = gl_ava_mallas().getptr(rel)) return *m;
    Ref<ArrayMesh> m = gl_ava_obj(gl_ava_texto(rel));
    gl_ava_mallas().insert(rel, m);
    return m;
}

// Material del cuerpo por color (uno por color, compartido por todos los
// personajes: cada miembro no pide el suyo).
inline Ref<StandardMaterial3D> gl_ava_material(const Color& c) {
    const uint32_t k = c.to_rgba32();
    if (Ref<StandardMaterial3D>* m = gl_ava_materiales().getptr(k)) return *m;
    Ref<StandardMaterial3D> m;
    m.instantiate();
    m->set_albedo(gl_color_pieza(c));   // gamma 2, como el Color de una pieza
    m->set_roughness(0.85f);
    m->set_specular(0.3f);
    gl_ava_materiales().insert(k, m);
    return m;
}

inline Ref<StandardMaterial3D> gl_ava_material_cara() {
    Ref<StandardMaterial3D>& m = gl_ava_mat_cara();
    if (m.is_valid()) return m;
    PackedByteArray png;
    const String ruta = "res://GodotLuau/assets/avatars/cara.png";
    if (FileAccess::file_exists(ruta)) png = FileAccess::get_file_as_bytes(ruta);
    if (png.is_empty()) {
        png.resize(GL_AVA_CARA_PNG_N);
        memcpy(png.ptrw(), GL_AVA_CARA_PNG, (size_t)GL_AVA_CARA_PNG_N);
    }
    Ref<Image> img;
    img.instantiate();
    if (img->load_png_from_buffer(png) != OK) return Ref<StandardMaterial3D>();
    img->generate_mipmaps();
    m.instantiate();
    m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, ImageTexture::create_from_image(img));
    m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    m->set_roughness(1.0f);
    m->set_render_priority(1);
    return m;
}

// ── rig.json ──────────────────────────────────────────────────────────
inline const GLRigDatos* gl_rig_datos(bool r15) {
    const int k = r15 ? 1 : 0;
    if (gl_ava_rigs_probados()[k]) return gl_ava_rigs()[k].get();
    gl_ava_rigs_probados()[k] = true;
    const Variant v = JSON::parse_string(gl_ava_texto(String(r15 ? "r15" : "r6") + "/rig.json"));
    if (v.get_type() != Variant::DICTIONARY) return nullptr;
    const Dictionary d = v;
    std::unique_ptr<GLRigDatos> r(new GLRigDatos());
    r->r15 = r15;
    r->hip = (float)(double)d.get("hipHeight", 0.0);
    r->cara_malla = d.get("cara_malla", "");
    const Array partes = d.get("partes", Array());
    float bajo = 0.0f, alto = 0.0f;
    for (int i = 0; i < partes.size(); i++) {
        const Dictionary p = partes[i];
        GLRigParte x;
        x.nombre = p.get("nombre", "");
        const Array t = p.get("tamano", Array());
        if (t.size() >= 3) x.tamano = Vector3((float)(double)t[0], (float)(double)t[1], (float)(double)t[2]);
        x.cf = gl_ava_cf(p.get("cframe", Variant()));
        const Array c = p.get("color", Array());
        x.color = c.size() >= 3 ? Color((float)(double)c[0] / 255.0f, (float)(double)c[1] / 255.0f, (float)(double)c[2] / 255.0f)
                                : Color(163.0f / 255.0f, 162.0f / 255.0f, 165.0f / 255.0f);
        x.transparencia = (float)(double)p.get("transparencia", 0.0);
        x.malla = p.get("malla", "");
        if (x.transparencia < 1.0f) {
            bajo = MIN(bajo, x.cf.origin.y - x.tamano.y * 0.5f);
            alto = MAX(alto, x.cf.origin.y + x.tamano.y * 0.5f);
        }
        r->partes.push_back(x);
    }
    r->pies = -bajo;
    r->cima = alto;
    const Array juntas = d.get("juntas", Array());
    for (int i = 0; i < juntas.size(); i++) {
        const Dictionary j = juntas[i];
        r->juntas.push_back({ j.get("nombre", ""), j.get("padre", ""), j.get("parte0", ""), j.get("parte1", ""),
                              gl_ava_cf(j.get("c0", Variant())), gl_ava_cf(j.get("c1", Variant())) });
    }
    const Array atts = d.get("attachments", Array());
    for (int i = 0; i < atts.size(); i++) {
        const Dictionary a = atts[i];
        r->atts.push_back({ a.get("nombre", ""), a.get("parte", ""), gl_ava_cf(a.get("cframe", Variant())) });
    }
    if (r->partes.empty() || r->juntas.empty()) return nullptr;
    gl_ava_rigs()[k] = std::move(r);
    return gl_ava_rigs()[k].get();
}

// ── Una prop de StarterPlayer ─────────────────────────────────────────
// Lo que escribio un script (_glp_), lo que trajo el place (meta con su
// nombre: asi la dejan el .rbxl y el .gdlu) o la propiedad de la clase. Nil si
// nadie la puso.
inline Variant gl_starter_valor(Node* sp, const char* k) {
    if (!sp) return Variant();
    const String mk = String("_glp_") + k;
    if (sp->has_meta(mk)) return sp->get_meta(mk);
    if (sp->has_meta(k)) return sp->get_meta(k);
    return sp->get(k);
}

// ── Que rig usa el juego ──────────────────────────────────────────────
// Como Roblox: el del ajuste de avatar del juego (StarterPlayer.
// GameSettingsAvatar en el .rbxl: 0 = R6, 1 = R15, 2 = a eleccion del
// jugador -> R15). Sin ajuste, R15, que es el de un juego nuevo en Roblox.
inline bool gl_rig_es_r15(Node* ctx) {
    if (!ctx || !ctx->is_inside_tree()) return true;
    Node* raiz = (Node*)ctx->get_tree()->get_root();
    std::vector<Node*> cola;
    cola.push_back(raiz);
    for (size_t i = 0; i < cola.size() && i < 4096; i++) {
        Node* n = cola[i];
        if (n->get_name() == StringName("StarterPlayer")) {
            const Variant p = gl_starter_valor(n, "GameSettingsAvatar");
            if (p.get_type() == Variant::INT || p.get_type() == Variant::FLOAT) return (int)(int64_t)p != 0;
            return true;
        }
        for (int c = 0; c < n->get_child_count(); c++) cola.push_back(n->get_child(c));
    }
    return true;
}

// ── Montar el personaje ───────────────────────────────────────────────
// Raiz "Character" con una pieza (Node3D) por parte, sus juntas Motor6D y sus
// attachments; la HRP en el origen. Al aplanar (gl_flatten_r6_character) todo
// pasa a ser hijo directo del personaje, como el Model plano de Roblox.
inline Node3D* gl_build_rig(bool r15) {
    const GLRigDatos* d = gl_rig_datos(r15);
    if (!d) return nullptr;
    Node3D* root = memnew(Node3D);
    root->set_name("Character");
    root->set_meta("_gl_r6rig", true);   // aplanable (el nombre viene del R6; vale para los dos)
    root->set_meta("_gl_rig", String(r15 ? "R15" : "R6"));
    HashMap<String, Node3D*> partes;
    for (const GLRigParte& p : d->partes) {
        Node3D* n = memnew(Node3D);
        n->set_name(p.nombre);
        n->set_transform(p.cf);
        n->set_meta("_gl_rig_tamano", p.tamano);
        // Para los scripts es una pieza de Roblox: Part (la HRP y las del R6) o
        // MeshPart (las del R15). IsA("BasePart") y ClassName salen de aqui; sus
        // propiedades las contesta el puente con estas metas (zona "pieza del
        // rig" de luau_api_indice_personaje.h).
        n->set_meta("__rbx_class", String(r15 && p.nombre != "HumanoidRootPart" ? "MeshPart" : "Part"));
        n->set_meta("_gl_rig_color", p.color);
        n->set_meta("_gl_rig_transp", p.transparencia);
        if (!p.malla.is_empty() && p.transparencia < 1.0f) {
            Ref<ArrayMesh> m = gl_ava_malla(r15, p.malla);
            if (m.is_valid()) {
                MeshInstance3D* mi = memnew(MeshInstance3D);
                mi->set_name("Malla");
                mi->set_mesh(m);
                mi->set_material_override(gl_luz_si_toca(gl_ava_material(p.color)));   // luz propia si esta activa
                mi->set_layer_mask(GL_CAPA_DINAMICA);   // se mueve: mapa de sombra de cada frame
                n->add_child(mi, false, Node::INTERNAL_MODE_BACK);
            }
        }
        root->add_child(n);
        partes.insert(p.nombre, n);
    }
    // La cara: un parche sobre la cabeza con la imagen; y un Decal "face" para
    // los scripts que la buscan (Head.face), como el de Roblox.
    if (Node3D** cabeza = partes.getptr("Head")) {
        if (!d->cara_malla.is_empty()) {
            Ref<ArrayMesh> m = gl_ava_malla(r15, d->cara_malla);
            Ref<StandardMaterial3D> mat = gl_ava_material_cara();
            if (m.is_valid() && mat.is_valid()) {
                MeshInstance3D* mi = memnew(MeshInstance3D);
                mi->set_name("Cara");
                mi->set_mesh(m);
                mi->set_material_override(gl_luz_si_toca(mat));
                mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
                mi->set_layer_mask(GL_CAPA_SIN_SOMBRA);
                (*cabeza)->add_child(mi, false, Node::INTERNAL_MODE_BACK);
            }
        }
        if (Node* face = gl_cast<Node>(ClassDB::instantiate(StringName("RBXDecal")))) {
            face->set_name("face");
            face->set("Face", 5);   // Front
            (*cabeza)->add_child(face);
        }
    }
    // Profundidad de cada pieza en la cadena: las juntas se mueven en orden
    // (de la HRP hacia las manos y los pies), si no la mano iria un frame tarde.
    HashMap<String, int> hondo;
    hondo.insert("HumanoidRootPart", 0);
    for (int vuelta = 0; vuelta < 8; vuelta++)
        for (const GLRigJunta& j : d->juntas)
            if (const int* h = hondo.getptr(j.p0)) hondo.insert(j.p1, *h + 1);
    for (const GLRigJunta& j : d->juntas) {
        Node3D** en = partes.getptr(j.padre);
        Node3D** p0 = partes.getptr(j.p0);
        Node3D** p1 = partes.getptr(j.p1);
        if (!en || !p0 || !p1) continue;
        Motor6D* m = memnew(Motor6D);
        m->set_name(j.nombre);
        m->set_c0(j.c0);
        m->set_c1(j.c1);
        (*en)->add_child(m);
        m->set_part0(*p0);
        m->set_part1(*p1);
        const int* h = hondo.getptr(j.p1);
        m->set_physics_process_priority(-20 + (h ? *h : 10));
    }
    for (const GLRigAtt& a : d->atts) {
        Node3D** en = partes.getptr(a.parte);
        if (!en) continue;
        Attachment* at = memnew(Attachment);
        at->set_name(a.nombre);
        at->set_transform(a.cf);
        (*en)->add_child(at);
    }
    return root;
}

#endif // GL_AVATAR_RIG_H

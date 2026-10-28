#ifndef GL_EDITABLE_MALLA_H
#define GL_EDITABLE_MALLA_H

// ════════════════════════════════════════════════════════════════════
//  EditableMesh de Roblox: una malla que los scripts construyen y editan en
//  tiempo de ejecucion. Se ve en las MeshPart que la enlazan
//  (AssetService:CreateMeshPartAsync, MeshPart:ApplyMesh) y se actualiza EN
//  VIVO al editarla.
//  https://create.roblox.com/docs/reference/engine/classes/EditableMesh
//
//  Como en Roblox:
//   · Los ids son ESTABLES: borrar un vertice no renumera los demas. Llevan
//     el tipo (vertice, cara, normal, uv, color) y una version, asi que un id
//     de otro tipo o ya borrado da error en vez de tocar otra cosa.
//   · El vertice solo tiene posicion; normal, uv y color van por ESQUINA de
//     cara (cada cara guarda sus ids). Una cara nueva reutiliza los de otra
//     cara que ya use ese vertice (sombreado suave), o crea unos nuevos.
//   · Una cara es un triangulo o un quad (cuenta como dos triangulos).
//   · Limite de 60.000 vertices y 20.000 triangulos.
//
//  Lo que se dibuja: cada MeshPart enlazada tiene SU ArrayMesh (una "vista").
//  Editar solo marca la malla; las vistas se rehacen una vez por frame como
//  mucho (gl_editables_vaciar). Cada CreateMeshPartAsync da una vista nueva:
//  la colision de la pieza se calcula con la malla de ese momento y se cachea
//  por malla, asi que un ApplyMesh posterior recalcula de verdad.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/templates/hash_map.hpp>

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <unordered_map>

using namespace godot;

inline constexpr int GL_EM_MAX_VERTICES = 60000;
inline constexpr int GL_EM_MAX_TRIANGULOS = 20000;

// Tipos de id. El id es tipo * 2^44 + version * 2^24 + (indice + 1): cabe
// entero en un double (< 2^53), que es lo que ve Luau.
enum GLEMTipo { GL_EM_VERT = 1, GL_EM_CARA = 2, GL_EM_NORMAL = 3, GL_EM_UV = 4, GL_EM_COLOR = 5 };

inline int64_t& gl_em_vivas() { static int64_t n = 0; return n; }

class GLEditableMesh;
inline void gl_editables_apuntar_malla(GLEditableMesh* em);

struct GLEMVert   { Vector3 p; uint32_t ver = 0; bool vivo = false; std::vector<uint32_t> caras; };
struct GLEMNormal { Vector3 n; uint32_t ver = 0; bool vivo = false; bool autom = true; };
struct GLEMUV     { Vector2 uv; uint32_t ver = 0; bool vivo = false; };
struct GLEMColor  { Color c = Color(1, 1, 1, 1); uint32_t ver = 0; bool vivo = false; };
struct GLEMCara {
    uint8_t n = 3;                       // 3 triangulo, 4 quad
    uint32_t v[4] = {}, nn[4] = {}, uv[4] = {}, col[4] = {};
    uint32_t ver = 0;
    bool vivo = false;
};

// Resultado de un rayo o de la busqueda del punto mas cercano.
struct GLEMImpacto {
    bool hay = false;
    uint32_t cara = 0;
    Vector3 punto, bari;
    uint32_t v[3] = {};
    float dist = 1e30f;
};

class GLEditableMesh : public RefCounted {
    GDCLASS(GLEditableMesh, RefCounted);

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("gl_num_vertices"), &GLEditableMesh::gl_num_vertices);
        ClassDB::bind_method(D_METHOD("gl_num_triangulos"), &GLEditableMesh::gl_num_triangulos);
        ClassDB::bind_method(D_METHOD("gl_num_vistas"), &GLEditableMesh::gl_num_vistas);
    }

public:
    std::vector<GLEMVert> verts;
    std::vector<GLEMNormal> normales;
    std::vector<GLEMUV> uvs;
    std::vector<GLEMColor> colores;
    std::vector<GLEMCara> caras;
    std::vector<uint32_t> libres_v, libres_n, libres_uv, libres_c, libres_f;
    int n_verts = 0, n_tris = 0;
    bool fija = false;               // FixedSize
    bool destruida = false;
    bool normales_sucias = true;
    bool sucia = false, apuntada = false;
    std::vector<Ref<ArrayMesh>> vistas;

    GLEditableMesh() { gl_em_vivas()++; }
    ~GLEditableMesh() { gl_em_vivas()--; }

    int gl_num_vertices() const { return n_verts; }
    int gl_num_triangulos() const { return n_tris; }
    int gl_num_vistas() const { return (int)vistas.size(); }

    // ── Ids ────────────────────────────────────────────────────────
    static double hacer_id(int tipo, uint32_t ver, uint32_t idx) {
        const uint64_t v = ((uint64_t)tipo << 44) | ((uint64_t)(ver & 0xFFFFFu) << 24) | (uint64_t)(idx + 1);
        return (double)v;
    }
    static bool partir_id(double d, int tipo, uint32_t& idx, uint32_t& ver) {
        if (!(d >= 1.0) || d >= 9007199254740992.0 || d != std::floor(d)) return false;
        const uint64_t v = (uint64_t)d;
        if ((int)(v >> 44) != tipo) return false;
        const uint64_t i = v & 0xFFFFFFu;
        if (i == 0) return false;
        idx = (uint32_t)(i - 1);
        ver = (uint32_t)((v >> 24) & 0xFFFFFu);
        return true;
    }
    template <typename T>
    static bool valido(const std::vector<T>& vec, double d, int tipo, uint32_t& idx) {
        uint32_t ver = 0;
        if (!partir_id(d, tipo, idx, ver)) return false;
        return idx < vec.size() && vec[idx].vivo && (vec[idx].ver & 0xFFFFFu) == ver;
    }
    bool vert_ok(double d, uint32_t& i) const { return valido(verts, d, GL_EM_VERT, i); }
    bool cara_ok(double d, uint32_t& i) const { return valido(caras, d, GL_EM_CARA, i); }
    bool normal_ok(double d, uint32_t& i) const { return valido(normales, d, GL_EM_NORMAL, i); }
    bool uv_ok(double d, uint32_t& i) const { return valido(uvs, d, GL_EM_UV, i); }
    bool color_ok(double d, uint32_t& i) const { return valido(colores, d, GL_EM_COLOR, i); }
    double id_vert(uint32_t i) const { return hacer_id(GL_EM_VERT, verts[i].ver, i); }
    double id_cara(uint32_t i) const { return hacer_id(GL_EM_CARA, caras[i].ver, i); }
    double id_normal(uint32_t i) const { return hacer_id(GL_EM_NORMAL, normales[i].ver, i); }
    double id_uv(uint32_t i) const { return hacer_id(GL_EM_UV, uvs[i].ver, i); }
    double id_color(uint32_t i) const { return hacer_id(GL_EM_COLOR, colores[i].ver, i); }

    template <typename T>
    static uint32_t ocupar(std::vector<T>& vec, std::vector<uint32_t>& libres) {
        if (!libres.empty()) {
            const uint32_t i = libres.back();
            libres.pop_back();
            const uint32_t ver = vec[i].ver;
            vec[i] = T();
            vec[i].ver = ver;
            vec[i].vivo = true;
            return i;
        }
        vec.push_back(T());
        vec.back().vivo = true;
        return (uint32_t)(vec.size() - 1);
    }
    template <typename T>
    static void soltar(std::vector<T>& vec, std::vector<uint32_t>& libres, uint32_t i) {
        vec[i].vivo = false;
        vec[i].ver = (vec[i].ver + 1) & 0xFFFFFu;   // los ids viejos dejan de valer
        libres.push_back(i);
    }

    // ── Marcar cambios ──────────────────────────────────────────────
    void marcar(bool geometria = true) {
        if (geometria) normales_sucias = true;
        if (vistas.empty() || destruida) return;
        sucia = true;
        if (!apuntada) gl_editables_apuntar_malla(this);
    }

    // ── Altas ──────────────────────────────────────────────────────
    uint32_t nuevo_vertice(const Vector3& p) {
        const uint32_t i = ocupar(verts, libres_v);
        verts[i].p = p;
        n_verts++;
        return i;
    }
    uint32_t nueva_normal(const Vector3* n) {
        const uint32_t i = ocupar(normales, libres_n);
        if (n) { normales[i].n = *n; normales[i].autom = false; }
        normales_sucias = true;
        return i;
    }
    uint32_t nuevo_uv(const Vector2& uv) {
        const uint32_t i = ocupar(uvs, libres_uv);
        uvs[i].uv = uv;
        return i;
    }
    uint32_t nuevo_color(const Color& c) {
        const uint32_t i = ocupar(colores, libres_c);
        colores[i].c = c;
        return i;
    }
    // Atributos de la esquina de un vertice: los de otra cara que ya lo use
    // (sombreado y uv continuos) o unos nuevos (normal automatica, uv (0, 0),
    // blanco opaco).
    void atributos_para(uint32_t v, uint32_t& n, uint32_t& uv, uint32_t& c) {
        for (uint32_t f : verts[v].caras) {
            const GLEMCara& cf = caras[f];
            for (int k = 0; k < cf.n; k++) {
                if (cf.v[k] != v) continue;
                n = cf.nn[k]; uv = cf.uv[k]; c = cf.col[k];
                return;
            }
        }
        n = nueva_normal(nullptr);
        uv = nuevo_uv(Vector2());
        c = nuevo_color(Color(1, 1, 1, 1));
    }
    uint32_t nueva_cara(const uint32_t* vs, int cuantos) {
        const uint32_t f = ocupar(caras, libres_f);
        GLEMCara& cf = caras[f];
        cf.n = (uint8_t)cuantos;
        for (int k = 0; k < cuantos; k++) {
            uint32_t n, uv, c;
            atributos_para(vs[k], n, uv, c);
            GLEMCara& cf2 = caras[f];   // atributos_para puede crecer vectores, no caras
            cf2.v[k] = vs[k]; cf2.nn[k] = n; cf2.uv[k] = uv; cf2.col[k] = c;
        }
        for (int k = 0; k < cuantos; k++) {
            std::vector<uint32_t>& lista = verts[vs[k]].caras;
            if (std::find(lista.begin(), lista.end(), f) == lista.end()) lista.push_back(f);
        }
        n_tris += cuantos == 4 ? 2 : 1;
        marcar();
        return f;
    }
    void quitar_cara(uint32_t f) {
        GLEMCara& cf = caras[f];
        for (int k = 0; k < cf.n; k++) {
            std::vector<uint32_t>& lista = verts[cf.v[k]].caras;
            lista.erase(std::remove(lista.begin(), lista.end(), f), lista.end());
        }
        n_tris -= cf.n == 4 ? 2 : 1;
        soltar(caras, libres_f, f);
        marcar();
    }

    // ── Normales automaticas ───────────────────────────────────────
    Vector3 normal_de_cara(const GLEMCara& cf) const {
        // Newell: vale para triangulos y quads (no planos incluidos). Sin
        // normalizar: pesa por area. Antihorario visto de frente = normal
        // hacia quien mira, como en Roblox.
        Vector3 n;
        for (int k = 0; k < cf.n; k++) {
            const Vector3& a = verts[cf.v[k]].p;
            const Vector3& b = verts[cf.v[(k + 1) % cf.n]].p;
            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
        }
        return n * 0.5f;
    }
    void calcular_normales() {
        if (!normales_sucias) return;
        normales_sucias = false;
        for (GLEMNormal& n : normales) if (n.vivo && n.autom) n.n = Vector3();
        for (const GLEMCara& cf : caras) {
            if (!cf.vivo) continue;
            const Vector3 fn = normal_de_cara(cf);
            for (int k = 0; k < cf.n; k++) {
                GLEMNormal& n = normales[cf.nn[k]];
                if (n.autom) n.n += fn;
            }
        }
        for (GLEMNormal& n : normales) {
            if (!n.vivo || !n.autom) continue;
            const float l = n.n.length();
            n.n = l > 1e-12f ? n.n / l : Vector3(0, 1, 0);
        }
    }
    Vector3 normal_de(uint32_t i) {
        if (normales[i].autom) calcular_normales();
        return normales[i].n;
    }

    // ── Consultas ──────────────────────────────────────────────────
    AABB caja() const {
        bool primero = true;
        AABB b;
        for (const GLEMVert& v : verts) {
            if (!v.vivo) continue;
            if (primero) { b = AABB(v.p, Vector3()); primero = false; }
            else b.expand_to(v.p);
        }
        return b;
    }
    // Los triangulos de una cara (un quad da dos, por la diagonal mas corta).
    int triangulos_de(const GLEMCara& cf, int t[2][3]) const {
        if (cf.n == 3) { t[0][0] = 0; t[0][1] = 1; t[0][2] = 2; return 1; }
        const float d02 = verts[cf.v[0]].p.distance_squared_to(verts[cf.v[2]].p);
        const float d13 = verts[cf.v[1]].p.distance_squared_to(verts[cf.v[3]].p);
        if (d02 <= d13) {
            t[0][0] = 0; t[0][1] = 1; t[0][2] = 2;
            t[1][0] = 0; t[1][1] = 2; t[1][2] = 3;
        } else {
            t[0][0] = 0; t[0][1] = 1; t[0][2] = 3;
            t[1][0] = 1; t[1][1] = 2; t[1][2] = 3;
        }
        return 2;
    }
    // Rayo infinito desde `o` (Moller-Trumbore, las dos caras).
    GLEMImpacto rayo(const Vector3& o, const Vector3& dir) const {
        GLEMImpacto mejor;
        const float largo = dir.length();
        if (largo < 1e-12f) return mejor;
        const Vector3 d = dir / largo;
        for (uint32_t f = 0; f < caras.size(); f++) {
            const GLEMCara& cf = caras[f];
            if (!cf.vivo) continue;
            int t[2][3];
            const int nt = triangulos_de(cf, t);
            for (int q = 0; q < nt; q++) {
                const Vector3& a = verts[cf.v[t[q][0]]].p;
                const Vector3& b = verts[cf.v[t[q][1]]].p;
                const Vector3& c = verts[cf.v[t[q][2]]].p;
                const Vector3 e1 = b - a, e2 = c - a;
                const Vector3 pv = d.cross(e2);
                const float det = e1.dot(pv);
                if (std::fabs(det) < 1e-12f) continue;
                const float inv = 1.0f / det;
                const Vector3 tv = o - a;
                const float u = tv.dot(pv) * inv;
                if (u < 0.0f || u > 1.0f) continue;
                const Vector3 qv = tv.cross(e1);
                const float v = d.dot(qv) * inv;
                if (v < 0.0f || u + v > 1.0f) continue;
                const float dist = e2.dot(qv) * inv;
                if (dist < 0.0f || dist >= mejor.dist) continue;
                mejor.hay = true;
                mejor.dist = dist;
                mejor.cara = f;
                mejor.punto = o + d * dist;
                mejor.bari = Vector3(1.0f - u - v, u, v);
                mejor.v[0] = cf.v[t[q][0]]; mejor.v[1] = cf.v[t[q][1]]; mejor.v[2] = cf.v[t[q][2]];
            }
        }
        return mejor;
    }
    // Punto mas cercano de un triangulo (Ericson, Real-Time Collision
    // Detection 5.1.5) y sus coordenadas baricentricas.
    static Vector3 cercano_en_triangulo(const Vector3& p, const Vector3& a, const Vector3& b, const Vector3& c, Vector3& bari) {
        const Vector3 ab = b - a, ac = c - a, ap = p - a;
        const float d1 = ab.dot(ap), d2 = ac.dot(ap);
        if (d1 <= 0 && d2 <= 0) { bari = Vector3(1, 0, 0); return a; }
        const Vector3 bp = p - b;
        const float d3 = ab.dot(bp), d4 = ac.dot(bp);
        if (d3 >= 0 && d4 <= d3) { bari = Vector3(0, 1, 0); return b; }
        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0 && d1 >= 0 && d3 <= 0) {
            const float v = d1 / (d1 - d3);
            bari = Vector3(1 - v, v, 0);
            return a + ab * v;
        }
        const Vector3 cp = p - c;
        const float d5 = ab.dot(cp), d6 = ac.dot(cp);
        if (d6 >= 0 && d5 <= d6) { bari = Vector3(0, 0, 1); return c; }
        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0 && d2 >= 0 && d6 <= 0) {
            const float w = d2 / (d2 - d6);
            bari = Vector3(1 - w, 0, w);
            return a + ac * w;
        }
        const float va = d3 * d6 - d5 * d4;
        if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
            const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            bari = Vector3(0, 1 - w, w);
            return b + (c - b) * w;
        }
        const float den = 1.0f / (va + vb + vc);
        const float v = vb * den, w = vc * den;
        bari = Vector3(1 - v - w, v, w);
        return a + ab * v + ac * w;
    }
    GLEMImpacto mas_cercano(const Vector3& p) const {
        GLEMImpacto mejor;
        for (uint32_t f = 0; f < caras.size(); f++) {
            const GLEMCara& cf = caras[f];
            if (!cf.vivo) continue;
            int t[2][3];
            const int nt = triangulos_de(cf, t);
            for (int q = 0; q < nt; q++) {
                Vector3 bari;
                const Vector3 x = cercano_en_triangulo(p, verts[cf.v[t[q][0]]].p, verts[cf.v[t[q][1]]].p,
                                                       verts[cf.v[t[q][2]]].p, bari);
                const float d = x.distance_squared_to(p);
                if (d >= mejor.dist) continue;
                mejor.hay = true;
                mejor.dist = d;
                mejor.cara = f;
                mejor.punto = x;
                mejor.bari = bari;
                mejor.v[0] = cf.v[t[q][0]]; mejor.v[1] = cf.v[t[q][1]]; mejor.v[2] = cf.v[t[q][2]];
            }
        }
        return mejor;
    }

    // ── Lo que se dibuja ───────────────────────────────────────────
    // Arrays de Godot: una esquina por vertice de Godot, sin indices. Godot
    // pinta de frente los triangulos en sentido HORARIO y Roblox los
    // antihorarios: se emiten al reves. `con_colores`: algun color no es
    // blanco opaco (entonces la pieza pinta con los colores de los vertices).
    bool arrays(Array& out, bool& con_colores) {
        calcular_normales();
        PackedVector3Array pos, nor;
        PackedVector2Array tex;
        PackedColorArray col;
        const int64_t total = (int64_t)n_tris * 3;
        pos.resize(total); nor.resize(total); tex.resize(total); col.resize(total);
        Vector3* pp = pos.ptrw();
        Vector3* pn = nor.ptrw();
        Vector2* pt = tex.ptrw();
        Color* pc = col.ptrw();
        int64_t k = 0;
        con_colores = false;
        for (const GLEMCara& cf : caras) {
            if (!cf.vivo) continue;
            int t[2][3];
            const int nt = triangulos_de(cf, t);
            for (int q = 0; q < nt && k + 3 <= total; q++) {
                static const int orden[3] = { 0, 2, 1 };
                for (int e = 0; e < 3; e++) {
                    const int esq = t[q][orden[e]];
                    pp[k] = verts[cf.v[esq]].p;
                    pn[k] = normales[cf.nn[esq]].n;
                    pt[k] = uvs[cf.uv[esq]].uv;
                    const Color c = colores[cf.col[esq]].c;
                    pc[k] = c;
                    if (c.r < 0.999f || c.g < 0.999f || c.b < 0.999f || c.a < 0.999f) con_colores = true;
                    k++;
                }
            }
        }
        if (k == 0) return false;
        if (k < total) { pos.resize(k); nor.resize(k); tex.resize(k); col.resize(k); }
        out.clear();
        out.resize(Mesh::ARRAY_MAX);
        out[Mesh::ARRAY_VERTEX] = pos;
        out[Mesh::ARRAY_NORMAL] = nor;
        out[Mesh::ARRAY_TEX_UV] = tex;
        out[Mesh::ARRAY_COLOR] = col;
        return true;
    }
    bool tiene_colores = false;
    void llenar(const Ref<ArrayMesh>& m, const Array& a, bool hay) {
        m->clear_surfaces();
        if (hay) m->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, a);
    }
    // Vista nueva para una MeshPart (CreateMeshPartAsync), con la malla de ahora.
    Ref<ArrayMesh> nueva_vista() {
        Ref<ArrayMesh> m;
        m.instantiate();
        Array a;
        bool con = false;
        const bool hay = arrays(a, con);
        tiene_colores = con;
        llenar(m, a, hay);
        vistas.push_back(m);
        return m;
    }
    // Lo llama el gestor una vez por frame. Las vistas que ya no usa nadie
    // (solo las sujeta esta malla) se sueltan aqui.
    void subir() {
        vistas.erase(std::remove_if(vistas.begin(), vistas.end(),
                                    [](const Ref<ArrayMesh>& m) { return m.is_null() || m->get_reference_count() <= 1; }),
                     vistas.end());
        if (!sucia) return;
        sucia = false;
        if (vistas.empty() || destruida) return;
        Array a;
        bool con = false;
        const bool hay = arrays(a, con);
        tiene_colores = con;
        for (const Ref<ArrayMesh>& m : vistas) llenar(m, a, hay);
    }

    // ── Copias ─────────────────────────────────────────────────────
    // CreateEditableMeshAsync(Content.fromObject(otra)): la misma malla con
    // los mismos ids.
    void copiar_de(const GLEditableMesh& o) {
        verts = o.verts; normales = o.normales; uvs = o.uvs; colores = o.colores; caras = o.caras;
        libres_v = o.libres_v; libres_n = o.libres_n; libres_uv = o.libres_uv;
        libres_c = o.libres_c; libres_f = o.libres_f;
        n_verts = o.n_verts; n_tris = o.n_tris;
        normales_sucias = true;
    }
    // Desde una malla de Godot (la de una MeshPart o un asset local). Las
    // posiciones iguales se juntan en un vertice (la topologia de Roblox:
    // las costuras de uv o de normales son atributos, no vertices). Godot
    // guarda los triangulos en sentido horario: se dan la vuelta.
    // Devuelve false si se pasa de los limites.
    bool cargar_de(const Ref<Mesh>& m) {
        if (m.is_null()) return false;
        struct Clave { uint32_t x, y, z; bool operator==(const Clave& o) const { return x == o.x && y == o.y && z == o.z; } };
        struct Hash { size_t operator()(const Clave& k) const { return ((size_t)k.x * 73856093u) ^ ((size_t)k.y * 19349663u) ^ ((size_t)k.z * 83492791u); } };
        auto bits = [](float f) { uint32_t u; memcpy(&u, &f, 4); return f == 0.0f ? 0u : u; };
        std::unordered_map<Clave, uint32_t, Hash> por_pos;
        const Ref<ArrayMesh> am = m;   // las PrimitiveMesh son siempre triangulos
        for (int s = 0; s < m->get_surface_count(); s++) {
            if (am.is_valid() && am->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES) continue;
            const Array a = m->surface_get_arrays(s);
            if (a.size() < Mesh::ARRAY_MAX || a[Mesh::ARRAY_VERTEX].get_type() != Variant::PACKED_VECTOR3_ARRAY) continue;
            const PackedVector3Array pos = a[Mesh::ARRAY_VERTEX];
            if (pos.is_empty()) continue;
            PackedVector3Array nor;
            PackedVector2Array tex;
            PackedColorArray col;
            PackedInt32Array idx;
            if (a[Mesh::ARRAY_NORMAL].get_type() == Variant::PACKED_VECTOR3_ARRAY) nor = a[Mesh::ARRAY_NORMAL];
            if (a[Mesh::ARRAY_TEX_UV].get_type() == Variant::PACKED_VECTOR2_ARRAY) tex = a[Mesh::ARRAY_TEX_UV];
            if (a[Mesh::ARRAY_COLOR].get_type() == Variant::PACKED_COLOR_ARRAY) col = a[Mesh::ARRAY_COLOR];
            if (a[Mesh::ARRAY_INDEX].get_type() == Variant::PACKED_INT32_ARRAY) idx = a[Mesh::ARRAY_INDEX];
            if (idx.is_empty()) {
                idx.resize(pos.size());
                for (int i = 0; i < pos.size(); i++) idx.set(i, i);
            }
            // Un juego de atributos por vertice de Godot.
            std::vector<uint32_t> vert_de(pos.size(), UINT32_MAX), nor_de(pos.size(), UINT32_MAX),
                uv_de(pos.size(), UINT32_MAX), col_de(pos.size(), UINT32_MAX);
            auto preparar = [&](int gi) -> bool {
                if (vert_de[gi] != UINT32_MAX) return true;
                const Vector3 p = pos[gi];
                const Clave c{ bits(p.x), bits(p.y), bits(p.z) };
                auto it = por_pos.find(c);
                if (it == por_pos.end()) {
                    if (n_verts >= GL_EM_MAX_VERTICES) return false;
                    const uint32_t v = nuevo_vertice(p);
                    it = por_pos.emplace(c, v).first;
                }
                vert_de[gi] = it->second;
                if (gi < nor.size()) { const Vector3 nv = nor[gi]; nor_de[gi] = nueva_normal(&nv); }
                else nor_de[gi] = nueva_normal(nullptr);
                uv_de[gi] = nuevo_uv(gi < tex.size() ? tex[gi] : Vector2());
                col_de[gi] = nuevo_color(gi < col.size() ? col[gi] : Color(1, 1, 1, 1));
                return true;
            };
            for (int t = 0; t + 2 < idx.size(); t += 3) {
                const int g[3] = { idx[t], idx[t + 2], idx[t + 1] };   // vuelta: antihorario
                bool ok = true;
                for (int e = 0; e < 3; e++) ok = ok && g[e] >= 0 && g[e] < pos.size() && preparar(g[e]);
                if (!ok) return false;
                if (n_tris >= GL_EM_MAX_TRIANGULOS) return false;
                const uint32_t f = ocupar(caras, libres_f);
                GLEMCara& cf = caras[f];
                cf.n = 3;
                for (int e = 0; e < 3; e++) {
                    cf.v[e] = vert_de[g[e]]; cf.nn[e] = nor_de[g[e]]; cf.uv[e] = uv_de[g[e]]; cf.col[e] = col_de[g[e]];
                }
                for (int e = 0; e < 3; e++) {
                    std::vector<uint32_t>& lista = verts[cf.v[e]].caras;
                    if (std::find(lista.begin(), lista.end(), f) == lista.end()) lista.push_back(f);
                }
                n_tris++;
            }
        }
        normales_sucias = true;
        return true;
    }

    void vaciar_todo() {
        verts.clear(); normales.clear(); uvs.clear(); colores.clear(); caras.clear();
        libres_v.clear(); libres_n.clear(); libres_uv.clear(); libres_c.clear(); libres_f.clear();
        n_verts = 0; n_tris = 0;
        marcar();
    }
    // Destroy: se suelta todo y las piezas enlazadas dejan de dibujarla.
    void destruir() {
        if (destruida) return;
        for (const Ref<ArrayMesh>& m : vistas) if (m.is_valid()) m->clear_surfaces();
        vistas.clear();
        vaciar_todo();
        std::vector<GLEMVert>().swap(verts);
        std::vector<GLEMCara>().swap(caras);
        destruida = true;
    }
};

#endif // GL_EDITABLE_MALLA_H

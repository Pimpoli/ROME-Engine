#ifndef GL_TERRENO_H
#define GL_TERRENO_H

// ════════════════════════════════════════════════════════════════════
//  Terreno de voxeles de Roblox (Terrain.SmoothGrid), como malla.
//
//  Formato (rbx-native, verificado byte a byte contra places reales; ver
//  github.com/chteau/rbx-native, crates/rbx_terrain/src/smooth_grid.rs):
//   · byte 0 = version (1), byte 1 = log2 del lado del chunk (5): 32^3.
//   · Luego chunks hasta el final. Cada uno empieza con su desplazamiento
//     desde el anterior (el primero desde 0,0,0): tres enteros de 32 bits
//     big-endian con los bytes entrelazados (el alto de X, Y, Z, luego el
//     siguiente de cada uno...).
//   · Y "runs" hasta llenar sus 32^3 voxeles, X primero, luego Z, luego Y.
//     Byte de cabeza: material en los 6 bits bajos (0 Air, 1 Water, 2 Grass...
//     23 en total); bit 6 = sigue un byte de ocupacion (si no: Air vacio y lo
//     demas lleno); bit 7 = sigue la cuenta, run = cuenta + 1. Cuenta 0 es un
//     voxel con un byte mas, la parte de agua que comparte con el solido.
//   · Un voxel mide 4 studs; el voxel i va de 4i a 4i+4. Ocupacion: un
//     voxel no-Air esta (byte + 1) / 256 lleno.
//
//  La malla: Surface Nets sobre la ocupacion, un vertice por celda que corta
//  la superficie (0,5) y un quad por arista cortada. Cada chunk se procesa con
//  un borde de un voxel de sus vecinos, asi que no hay costuras. Color de
//  vertice = MaterialColors del material dominante; agua aparte, translucida
//  y sin colision. Cada chunk con superficie es un StaticBody3D interno.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include "gl_capas.h"       // capas de las sombras propias
#include "gl_luz_propia.h"  // luz propia del suelo y el agua
#include "gl_graphics.h"   // gl_view_studs_actual
#include <atomic>
#include <thread>
#include <vector>

using namespace godot;

static const int GL_TER_LADO = 32;
static const int GL_TER_CELDAS = 32 * 32 * 32;
static const float GL_TER_STUDS = 4.0f;

// Enum.Material de Roblox de cada material del terreno, en el orden del formato.
inline int gl_terreno_enum_material(int slot) {
    static const int T[23] = { 1792, 2048, 1280, 800, 816, 848, 1296, 528, 896, 1552, 1328, 912,
                               1344, 788, 1360, 804, 1376, 880, 1536, 1284, 1392, 820, 836 };
    return (slot >= 0 && slot < 23) ? T[slot] : 1792;
}
// Los colores por defecto de Roblox (Terrain:GetMaterialColor de un place nuevo),
// para un place que no traiga MaterialColors.
inline Color gl_terreno_color_defecto(int slot) {
    static const uint8_t C[23][3] = {
        {0,0,0}, {12,84,92}, {106,127,63}, {63,127,107}, {127,102,63}, {138,86,62},
        {143,126,95}, {139,109,79}, {102,108,111}, {101,176,234}, {195,199,218}, {137,90,71},
        {58,46,36}, {30,30,37}, {102,92,59}, {232,156,74}, {115,123,107}, {132,123,90},
        {129,194,224}, {115,132,74}, {198,189,181}, {206,173,148}, {148,148,140} };
    if (slot < 0 || slot >= 23) return Color(0.5f, 0.5f, 0.5f);
    return Color(C[slot][0] / 255.0f, C[slot][1] / 255.0f, C[slot][2] / 255.0f);
}

struct GLTerrenoChunk {
    int x = 0, y = 0, z = 0;
    int uniforme = 0;        // slot de material si TODO el chunk es igual y lleno (0 = aire); -1 = mezclado
    PackedByteArray mat;     // 32^3, solo si mezclado
    PackedByteArray occ;
    PackedByteArray liq;     // solo si algun voxel comparte agua
};

struct GLTerreno {
    LocalVector<GLTerrenoChunk> chunks;
    HashMap<int64_t, int> indice;
    static int64_t clave(int x, int y, int z) {
        return ((int64_t)(x + 32768) << 32) | ((int64_t)(y + 32768) << 16) | (int64_t)(z + 32768);
    }
    const GLTerrenoChunk* chunk(int x, int y, int z) const {
        const int* i = indice.getptr(clave(x, y, z));
        return i ? &chunks[*i] : nullptr;
    }
};

// Decodifica un SmoothGrid. false (y err) si no es un formato que se conozca.
inline bool gl_terreno_decodificar(const PackedByteArray& b, GLTerreno& out, String& err) {
    const int64_t n = b.size();
    const uint8_t* p = b.ptr();
    if (n < 2) { err = "SmoothGrid vacio"; return false; }
    if (p[0] != 1) { err = "SmoothGrid version " + itos(p[0]) + " (solo se conoce la 1)"; return false; }
    if (p[1] != 5) { err = "SmoothGrid con chunks de 2^" + itos(p[1]) + " (se esperaba 2^5)"; return false; }
    int64_t at = 2;
    int32_t clave[3] = { 0, 0, 0 };
    while (at < n) {
        if (at + 12 > n) { err = "SmoothGrid cortado en una cabecera de chunk"; return false; }
        uint32_t delta[3] = { 0, 0, 0 };
        for (int s = 0; s < 4; s++)
            for (int e = 0; e < 3; e++) delta[e] = (delta[e] << 8) | p[at++];
        for (int e = 0; e < 3; e++) clave[e] = (int32_t)((uint32_t)clave[e] + delta[e]);
        if (Math::abs(clave[0]) > 300 || Math::abs(clave[1]) > 300 || Math::abs(clave[2]) > 300) {
            err = "SmoothGrid con un chunk fuera del mundo";
            return false;
        }
        GLTerrenoChunk c;
        c.x = clave[0]; c.y = clave[1]; c.z = clave[2];
        uint8_t* mat = nullptr;
        uint8_t* occ = nullptr;
        uint8_t* liq = nullptr;
        int lleno = 0;
        int primer_mat = -1;
        bool igual = true;
        while (lleno < GL_TER_CELDAS) {
            if (at >= n) { err = "SmoothGrid cortado dentro de un chunk"; return false; }
            const uint8_t cabeza = p[at++];
            const int m = cabeza & 0x3F;
            if (m >= 23) { err = "SmoothGrid con material " + itos(m); return false; }
            int o = (m == 0) ? 0 : 255;
            if (cabeza & 0x40) { if (at >= n) { err = "SmoothGrid cortado"; return false; } o = p[at++]; }
            int run = 1, l = 0;
            if (cabeza & 0x80) {
                if (at >= n) { err = "SmoothGrid cortado"; return false; }
                const int cuenta = p[at++];
                if (cuenta == 0) { if (at >= n) { err = "SmoothGrid cortado"; return false; } l = p[at++]; }
                else run = cuenta + 1;
            }
            if (lleno + run > GL_TER_CELDAS) { err = "SmoothGrid con un run que se sale del chunk"; return false; }
            if (m == 0) o = 0;
            if (primer_mat < 0) primer_mat = m;
            const bool uniforme_aqui = (m == primer_mat) && (m == 0 || o == 255) && l == 0;
            if (igual && !uniforme_aqui) {
                // Deja de ser uniforme: se reservan los arrays y se rellena lo ya leido.
                igual = false;
                c.mat.resize(GL_TER_CELDAS); c.occ.resize(GL_TER_CELDAS);
                mat = c.mat.ptrw(); occ = c.occ.ptrw();
                const int om = (primer_mat == 0) ? 0 : 255;
                for (int i = 0; i < lleno; i++) { mat[i] = (uint8_t)primer_mat; occ[i] = (uint8_t)om; }
            }
            if (!igual) {
                for (int i = 0; i < run; i++) { mat[lleno + i] = (uint8_t)m; occ[lleno + i] = (uint8_t)o; }
                if (l != 0) {
                    if (!liq) { c.liq.resize(GL_TER_CELDAS); liq = c.liq.ptrw(); memset(liq, 0, GL_TER_CELDAS); }
                    liq[lleno] = (uint8_t)l;
                }
            }
            lleno += run;
        }
        c.uniforme = igual ? primer_mat : -1;
        out.indice[GLTerreno::clave(c.x, c.y, c.z)] = (int)out.chunks.size();
        out.chunks.push_back(c);
    }
    return true;
}

// Muestra (solido 0..1, agua 0..1, material) de un voxel en coordenadas de chunk
// relativas al chunk central; `vec` son los 27 chunks de alrededor.
struct GLTerMuestra { float solido; float agua; uint8_t mat; };
inline GLTerMuestra gl_terreno_muestra(const GLTerrenoChunk* const* vec, int lx, int ly, int lz) {
    const int ox = lx < 0 ? 0 : (lx >= GL_TER_LADO ? 2 : 1);
    const int oy = ly < 0 ? 0 : (ly >= GL_TER_LADO ? 2 : 1);
    const int oz = lz < 0 ? 0 : (lz >= GL_TER_LADO ? 2 : 1);
    const GLTerrenoChunk* c = vec[ox + oy * 3 + oz * 9];
    GLTerMuestra r{ 0.0f, 0.0f, 0 };
    if (!c) return r;
    int m, o, l = 0;
    if (c->uniforme >= 0) { m = c->uniforme; o = (m == 0) ? 0 : 255; }
    else {
        const int x = lx - (ox - 1) * GL_TER_LADO, y = ly - (oy - 1) * GL_TER_LADO, z = lz - (oz - 1) * GL_TER_LADO;
        const int i = x + z * GL_TER_LADO + y * GL_TER_LADO * GL_TER_LADO;
        m = c->mat[i]; o = c->occ[i];
        if (c->liq.size()) l = c->liq[i];
    }
    r.mat = (uint8_t)m;
    if (m == 0) return r;
    const float f = (o + 1) / 256.0f;
    if (m == 1) { r.agua = f; return r; }
    r.solido = f;
    if (l) r.agua = Math::min(l / 255.0f, 1.0f - f);
    return r;
}

// Surface Nets sobre un campo de 34^3 muestras (el chunk y un voxel de borde).
// `campo` en [0..1]; iso 0,5. Devuelve triangulos indexados con normales
// suaves y, si `colores`, el color del material dominante de cada celda.
struct GLTerMalla {
    PackedVector3Array v, n;
    PackedColorArray c;
    PackedInt32Array i;
};
inline void gl_terreno_nets(const float* campo, const uint8_t* mats, const Color* paleta,
                            const Vector3& origen, const bool* neg_vacio, GLTerMalla& out) {
    const int N = GL_TER_LADO + 2;        // muestras por eje (con borde)
    const int C = N - 1;                  // celdas por eje
    auto M = [N](int x, int y, int z) { return x + y * N + z * N * N; };
    LocalVector<int> vid;
    vid.resize(C * C * C);
    for (uint32_t k = 0; k < vid.size(); k++) vid[k] = -1;
    static const int E[12][2] = { {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7} };
    for (int z = 0; z < C; z++) for (int y = 0; y < C; y++) for (int x = 0; x < C; x++) {
        float d[8];
        int mascara = 0;
        for (int k = 0; k < 8; k++) {
            d[k] = campo[M(x + (k & 1), y + ((k >> 1) & 1), z + ((k >> 2) & 1))];
            if (d[k] >= 0.5f) mascara |= 1 << k;
        }
        if (mascara == 0 || mascara == 255) continue;
        Vector3 suma;
        int cuantos = 0;
        for (int e = 0; e < 12; e++) {
            const int a = E[e][0], b = E[e][1];
            if ((d[a] >= 0.5f) == (d[b] >= 0.5f)) continue;
            const float t = (0.5f - d[a]) / (d[b] - d[a]);
            const Vector3 pa((float)(a & 1), (float)((a >> 1) & 1), (float)((a >> 2) & 1));
            const Vector3 pb((float)(b & 1), (float)((b >> 1) & 1), (float)((b >> 2) & 1));
            suma += pa + (pb - pa) * t;
            cuantos++;
        }
        const Vector3 local = suma / (float)cuantos;
        // La muestra x es el voxel x-1 del chunk, cuyo centro esta en 4(x - 1 + 0,5).
        out.v.push_back(origen + (Vector3((float)x, (float)y, (float)z) - Vector3(0.5f, 0.5f, 0.5f) + local) * GL_TER_STUDS);
        out.n.push_back(Vector3());
        if (paleta) {
            int mejor = 0;
            for (int k = 1; k < 8; k++) if (d[k] > d[mejor]) mejor = k;
            out.c.push_back(paleta[mats[M(x + (mejor & 1), y + ((mejor >> 1) & 1), z + ((mejor >> 2) & 1))]]);
        }
        vid[x + y * C + z * C * C] = (int)out.v.size() - 1;
    }
    // Un quad por arista cortada cuyo voxel de inicio es del chunk (muestras
    // 1..32): las del borde negativo las pone el chunk vecino, salvo que no
    // exista (neg_vacio): entonces la arista que entra desde el es de este.
    auto tri = [&out](int a, int b, int c, const Vector3& fuera) {
        const Vector3& pa = out.v[a]; const Vector3& pb = out.v[b]; const Vector3& pc = out.v[c];
        const Vector3 nn = (pb - pa).cross(pc - pa);
        if (nn.length_squared() < 1e-6f) return;   // degenerado: dos vertices coinciden
        if (nn.dot(fuera) >= 0.0f) { out.i.push_back(a); out.i.push_back(b); out.i.push_back(c); }
        else { out.i.push_back(a); out.i.push_back(c); out.i.push_back(b); }
        out.n.set(a, out.n[a] + nn); out.n.set(b, out.n[b] + nn); out.n.set(c, out.n[c] + nn);
    };
    for (int z = 0; z <= GL_TER_LADO; z++) for (int y = 0; y <= GL_TER_LADO; y++) for (int x = 0; x <= GL_TER_LADO; x++) {
        const bool s0 = campo[M(x, y, z)] >= 0.5f;
        for (int eje = 0; eje < 3; eje++) {
            const int pos[3] = { x, y, z };
            bool mio = true;
            for (int e = 0; e < 3; e++)
                if (pos[e] == 0 && !(e == eje && neg_vacio[e])) mio = false;
            if (!mio) continue;
            const int x1 = x + (eje == 0), y1 = y + (eje == 1), z1 = z + (eje == 2);
            if (x1 >= N || y1 >= N || z1 >= N) continue;
            const bool s1 = campo[M(x1, y1, z1)] >= 0.5f;
            if (s0 == s1) continue;
            // Las 4 celdas que comparten la arista.
            int q[4];
            bool ok = true;
            for (int k = 0; k < 4; k++) {
                int cx = x, cy = y, cz = z;
                const int da = (k & 1) ? 1 : 0, db = (k & 2) ? 1 : 0;
                if (eje == 0) { cy -= da; cz -= db; }
                else if (eje == 1) { cx -= da; cz -= db; }
                else { cx -= da; cy -= db; }
                if (cx < 0 || cy < 0 || cz < 0 || cx >= C || cy >= C || cz >= C) { ok = false; break; }
                q[k] = vid[cx + cy * C + cz * C * C];
                if (q[k] < 0) { ok = false; break; }
            }
            if (!ok) continue;
            Vector3 fuera;
            fuera[eje] = s0 ? 1.0f : -1.0f;   // de lo solido hacia lo vacio
            tri(q[0], q[1], q[3], fuera);
            tri(q[0], q[3], q[2], fuera);
        }
    }
    for (int k = 0; k < out.n.size(); k++) {
        const Vector3 nn = out.n[k];
        out.n.set(k, nn.length_squared() > 0.0f ? nn.normalized() : Vector3(0, 1, 0));
    }
}

inline Ref<ArrayMesh> gl_terreno_malla(const GLTerMalla& m) {
    if (m.i.is_empty()) return Ref<ArrayMesh>();
    Array arr;
    arr.resize(Mesh::ARRAY_MAX);
    arr[Mesh::ARRAY_VERTEX] = m.v;
    arr[Mesh::ARRAY_NORMAL] = m.n;
    if (!m.c.is_empty()) arr[Mesh::ARRAY_COLOR] = m.c;
    arr[Mesh::ARRAY_INDEX] = m.i;
    Ref<ArrayMesh> malla;
    malla.instantiate();
    malla->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
    return malla;
}

// Lo que sale de un chunk: su malla de suelo y la de agua (vacias si no hay).
struct GLTerResultado {
    GLTerMalla suelo, agua;
    bool hecho = false;
};

// Calcula las mallas de un chunk. Solo toca memoria propia: se puede llamar
// desde varios hilos a la vez (cada uno con sus buffers).
inline void gl_terreno_calcular(const GLTerreno& ter, uint32_t ci, const Color* paleta,
                                LocalVector<float>& solido, LocalVector<float>& agua,
                                LocalVector<uint8_t>& mats, GLTerResultado& out) {
    const int N = GL_TER_LADO + 2;
    const GLTerrenoChunk& ch = ter.chunks[ci];
    const GLTerrenoChunk* vec[27];
    for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++)
        vec[(dx + 1) + (dy + 1) * 3 + (dz + 1) * 9] = ter.chunk(ch.x + dx, ch.y + dy, ch.z + dz);
    // Un chunk entero de aire cuyos vecinos tambien lo son no tiene nada.
    if (ch.uniforme == 0) {
        bool todo_aire = true;
        for (int k = 0; k < 27 && todo_aire; k++) if (vec[k] && vec[k]->uniforme != 0) todo_aire = false;
        if (todo_aire) return;
    }
    bool hay_solido = false, hay_vacio = false, hay_agua = false;
    for (int z = 0; z < N; z++) for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        const GLTerMuestra m = gl_terreno_muestra(vec, x - 1, y - 1, z - 1);
        const int i = x + y * N + z * N * N;
        solido[i] = m.solido; agua[i] = m.agua; mats[i] = m.mat;
        if (m.solido >= 0.5f) hay_solido = true; else hay_vacio = true;
        if (m.agua >= 0.5f) hay_agua = true;
    }
    // Esquina del chunk: el voxel local v tiene su centro en 4(v + 0,5).
    const Vector3 origen = Vector3((float)ch.x, (float)ch.y, (float)ch.z) * (float)GL_TER_LADO * GL_TER_STUDS;
    // Las caras que dan a un vecino que no existe (aire) por el lado negativo
    // no las pone nadie mas: las pone este chunk.
    const bool neg_vacio[3] = { vec[12] == nullptr, vec[10] == nullptr, vec[4] == nullptr };
    if (hay_solido && hay_vacio) gl_terreno_nets(solido.ptr(), mats.ptr(), paleta, origen, neg_vacio, out.suelo);
    if (hay_agua) gl_terreno_nets(agua.ptr(), mats.ptr(), nullptr, origen, neg_vacio, out.agua);
    out.hecho = true;
}

// Monta el terreno como hijos internos de `padre`. Devuelve cuantos triangulos
// solidos salieron (para el informe), o -1 si los datos no se entienden.
// El calculo (muestras y Surface Nets, ~1 s en un mundo de 1.099 chunks) va
// repartido entre los nucleos; crear mallas, formas y nodos tiene que ir en el
// hilo principal.
inline int64_t gl_terreno_construir(Node3D* padre, const PackedByteArray& datos, const PackedColorArray& colores,
                                    const Color& agua_color, float agua_transparencia) {
    const bool traza = OS::get_singleton()->has_environment("GL_TERRENO_TRAZA");
    const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
    GLTerreno ter;
    String err;
    if (!gl_terreno_decodificar(datos, ter, err)) {
        UtilityFunctions::push_warning("Terrain: ", err);
        return -1;
    }
    Color paleta[23];
    for (int k = 0; k < 23; k++) {
        paleta[k] = gl_terreno_color_defecto(k);
        if (k < colores.size() && colores[k] != Color(0, 0, 0)) paleta[k] = colores[k];
    }
    const uint64_t t1 = Time::get_singleton()->get_ticks_usec();

    // ── 1) Calculo, repartido ─────────────────────────────────────────
    LocalVector<GLTerResultado> res;
    res.resize(ter.chunks.size());
    std::atomic<uint32_t> siguiente(0);
    auto trabajar = [&]() {
        const int N = GL_TER_LADO + 2;
        LocalVector<float> solido, agua;
        LocalVector<uint8_t> mats;
        solido.resize(N * N * N); agua.resize(N * N * N); mats.resize(N * N * N);
        for (;;) {
            const uint32_t ci = siguiente.fetch_add(1);
            if (ci >= ter.chunks.size()) break;
            gl_terreno_calcular(ter, ci, paleta, solido, agua, mats, res[ci]);
        }
    };
    const unsigned nucleos = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::thread> hilos;
    for (unsigned k = 1; k < nucleos; k++) hilos.emplace_back(trabajar);
    trabajar();
    for (std::thread& h : hilos) h.join();
    const uint64_t t2 = Time::get_singleton()->get_ticks_usec();

    // ── 2) Mallas, formas y nodos, en el principal ────────────────────
    Ref<StandardMaterial3D> mat_suelo;
    mat_suelo.instantiate();
    mat_suelo->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    mat_suelo->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
    mat_suelo->set_roughness(0.9f);
    Ref<StandardMaterial3D> mat_agua;
    mat_agua.instantiate();
    mat_agua->set_albedo(Color(agua_color.r, agua_color.g, agua_color.b, 1.0f - agua_transparencia));
    mat_agua->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    mat_agua->set_roughness(0.05f);
    mat_agua->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
    // Hasta donde se ve, como las piezas (gl_view_studs_actual), mas medio chunk
    // de margen: el centro del chunk puede quedar lejos aunque su borde no.
    const float alcance = gl_view_studs_actual() + GL_TER_LADO * GL_TER_STUDS * 0.9f;

    int64_t triangulos = 0;
    for (uint32_t ci = 0; ci < ter.chunks.size(); ci++) {
        GLTerResultado& r = res[ci];
        if (!r.hecho) continue;
        const GLTerrenoChunk& ch = ter.chunks[ci];
        const String sufijo = itos(ch.x) + "_" + itos(ch.y) + "_" + itos(ch.z);
        const Ref<ArrayMesh> malla = gl_terreno_malla(r.suelo);
        if (malla.is_valid()) {
            StaticBody3D* cuerpo = memnew(StaticBody3D);
            cuerpo->set_name(String("GLTerreno_") + sufijo);
            MeshInstance3D* mi = memnew(MeshInstance3D);
            mi->set_mesh(malla);
            mi->set_material_override(gl_luz_si_toca(mat_suelo));   // luz propia si esta activa
            mi->set_layer_mask(GL_CAPA_MUNDO);   // quieto: al mapa de sombra guardado (gl_capas.h)
            mi->set_visibility_range_end(alcance);
            cuerpo->add_child(mi);
            PackedVector3Array caras;
            caras.resize(r.suelo.i.size());
            Vector3* pc = caras.ptrw();
            const Vector3* pv = r.suelo.v.ptr();
            const int32_t* pi = r.suelo.i.ptr();
            for (int k = 0; k < r.suelo.i.size(); k++) pc[k] = pv[pi[k]];
            if (caras.size() >= 3) {
                Ref<ConcavePolygonShape3D> forma;
                forma.instantiate();
                forma->set_faces(caras);
                // El terreno es un volumen solido: choca por las dos caras.
                forma->set_backface_collision_enabled(true);
                CollisionShape3D* cs = memnew(CollisionShape3D);
                cs->set_shape(forma);
                cuerpo->add_child(cs);
            }
            cuerpo->set_meta("_gl_terreno", true);
            padre->add_child(cuerpo, false, Node::INTERNAL_MODE_BACK);
            triangulos += r.suelo.i.size() / 3;
        }
        const Ref<ArrayMesh> malla_agua = gl_terreno_malla(r.agua);
        if (malla_agua.is_valid()) {
            MeshInstance3D* mi = memnew(MeshInstance3D);
            mi->set_name(String("GLAgua_") + sufijo);
            mi->set_mesh(malla_agua);
            mi->set_material_override(gl_luz_si_toca(mat_agua));
            mi->set_layer_mask(GL_CAPA_SIN_SOMBRA);
            mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
            mi->set_visibility_range_end(alcance);
            mi->set_meta("_gl_terreno", true);
            padre->add_child(mi, false, Node::INTERNAL_MODE_BACK);
        }
        r = GLTerResultado();   // suelta la memoria del calculo cuanto antes
    }
    if (traza)
        UtilityFunctions::print("[TERRENO] total ", (int64_t)((Time::get_singleton()->get_ticks_usec() - t0) / 1000),
                                " ms | decodificar ", (int64_t)((t1 - t0) / 1000), " | calculo (", (int64_t)nucleos,
                                " hilos) ", (int64_t)((t2 - t1) / 1000), " | mallas y nodos ",
                                (int64_t)((Time::get_singleton()->get_ticks_usec() - t2) / 1000));
    return triangulos;
}

#endif // GL_TERRENO_H

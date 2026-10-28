#ifndef GL_SUPERFICIES_H
#define GL_SUPERFICIES_H

// ── Superficies y materiales de Roblox ────────────────────────────────
// Dos cosas que faltaban para que una pieza importada se parezca a la de
// Roblox y que aqui se resuelven SIN pedir un solo archivo a la nube:
//
//  1) Los STUDS. En Roblox cada cara de una Part tiene un SurfaceType, y
//     Studs/Inlet dibujan los tetones (o los agujeros) uno por stud. Se hacen
//     con un shader compartido: la pieza sigue siendo una caja, los tetones
//     salen en el pixel. Un shader para todas y una sola llamada de dibujo,
//     en vez de miles de cilindros de geometria.
//
//  2) Las TEXTURAS DE MATERIAL (Wood, Brick, Grass, Slate...). Hasta ahora
//     Material solo cambiaba la rugosidad, asi que un muro de ladrillo se
//     veia como plastico del color del ladrillo. Se generan aqui, en el
//     arranque y solo las que el mapa usa, como imagenes procedurales
//     repetibles, y se proyectan en triplanar igual que Roblox.
//
// Todo va por CACHE global: la textura de "Brick" se calcula una vez y la
// comparten las 3.000 piezas de ladrillo del mapa.

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/core/math.hpp>
#include "gl_servicios.h"   // gl_purgar_cache
#include "gl_luz_propia.h"  // el shader de caras con la luz propia

using namespace godot;

// ── Color de pieza 1:1 con Roblox ─────────────────────────────────────
// Roblox pasa el Color de una pieza a lineal con gamma 2 (c*c), no con la
// curva sRGB que aplica Godot al albedo. Medido con una carta de grises en
// Play (2026-10-08, solo sol, sin efectos): con sRGB los grises medios daban
// un 10-19 % menos luz que en Roblox; con c*c, menos de un 3,5 %. Lo mismo
// vale para Ambient y OutdoorAmbient (ver Lighting::_apply_env).
// Se le da a Godot el color sRGB que, al pasarlo el a lineal, vale c*c.
inline Color gl_color_pieza(const Color& c) {
    return Color(c.r * c.r, c.g * c.g, c.b * c.b, c.a).linear_to_srgb();
}

// ── Ruido repetible ───────────────────────────────────────────────────
// Value noise con periodo: el pixel del borde derecho enlaza con el del
// izquierdo, asi la textura se puede repetir sin costura visible.
inline float gls_hash(int x, int y) {
    uint32_t h = (uint32_t)(x * 374761393) + (uint32_t)(y * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return (float)(h & 0xFFFFFFu) / (float)0xFFFFFFu;
}

inline float gls_valor(float x, float y, int px, int py) {
    int xi = (int)Math::floor(x), yi = (int)Math::floor(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    int x0 = ((xi % px) + px) % px, x1 = (x0 + 1) % px;
    int y0 = ((yi % py) + py) % py, y1 = (y0 + 1) % py;
    float a = gls_hash(x0, y0), b = gls_hash(x1, y0);
    float c = gls_hash(x0, y1), d = gls_hash(x1, y1);
    return Math::lerp(Math::lerp(a, b, fx), Math::lerp(c, d, fx), fy);
}

// fBm sobre uv en [0,1). px/py son las celdas de la octava base: distintos
// entre si = ruido estirado (vetas de madera, metal cepillado).
inline float gls_fbm(float u, float v, int px, int py, int oct) {
    float s = 0.0f, amp = 0.5f, tot = 0.0f;
    for (int i = 0; i < oct; i++) {
        s += gls_valor(u * px, v * py, px, py) * amp;
        tot += amp;
        amp *= 0.5f;
        px *= 2; py *= 2;
    }
    return tot > 0.0f ? s / tot : 0.0f;
}

// Onda 0..1 con periodo 1 (Math::sin devuelve double y ensucia las plantillas).
inline float gls_onda(float x) {
    return (float)Math::sin(Math_TAU * (double)x) * 0.5f + 0.5f;
}

// ── Patrones ──────────────────────────────────────────────────────────
enum {
    GLS_LISO = 0,   // sin textura (plastico, neon, cristal)
    GLS_RUIDO,      // piedra, hormigon, arena, nieve
    GLS_VETA,       // madera
    GLS_TABLAS,     // madera en tablones
    GLS_LADRILLO,   // ladrillo, adoquinado regular
    GLS_CELDAS,     // canto rodado, lava agrietada (voronoi)
    GLS_HIERBA,     // cesped
    GLS_RAYAS,      // metal cepillado, papel de aluminio
    GLS_DIAMANTE,   // chapa antideslizante
    GLS_TELA,       // tejido
    GLS_CAPAS       // pizarra, marmol, arenisca (vetas horizontales)
};

struct GLSMat {
    uint8_t patron;
    float   studs;    // cuantos studs cubre un mosaico
    float   fuerza;   // cuanto oscurece el dibujo al color de la pieza
    float   relieve;  // fuerza del mapa de normales
};

// Tabla por Material de Roblox (los indices son los de RobloxPart).
inline const GLSMat& gls_tabla(int m) {
    static const GLSMat T[37] = {
        { GLS_LISO,     1, 0.00f, 0.0f },  // 0  Plastic
        { GLS_LISO,     1, 0.00f, 0.0f },  // 1  SmoothPlastic
        { GLS_LISO,     1, 0.00f, 0.0f },  // 2  Neon
        { GLS_VETA,     8, 0.30f, 0.5f },  // 3  Wood
        { GLS_TABLAS,   8, 0.34f, 0.8f },  // 4  WoodPlanks
        { GLS_CAPAS,    8, 0.20f, 0.2f },  // 5  Marble
        { GLS_CAPAS,    6, 0.30f, 0.7f },  // 6  Slate
        { GLS_RUIDO,    6, 0.18f, 0.5f },  // 7  Concrete
        { GLS_RUIDO,    3, 0.26f, 0.4f },  // 8  Granite
        { GLS_LADRILLO, 6, 0.38f, 1.0f },  // 9  Brick
        { GLS_RAYAS,    4, 0.10f, 0.2f },  // 10 Metal
        { GLS_RUIDO,    4, 0.34f, 0.8f },  // 11 CorrodedMetal
        { GLS_DIAMANTE, 4, 0.28f, 1.0f },  // 12 DiamondPlate
        { GLS_RAYAS,    3, 0.12f, 0.3f },  // 13 Foil
        { GLS_HIERBA,   3, 0.30f, 0.6f },  // 14 Grass
        { GLS_RUIDO,    8, 0.08f, 0.2f },  // 15 Ice
        { GLS_LISO,     1, 0.00f, 0.0f },  // 16 Glass
        { GLS_RUIDO,    2, 0.18f, 0.5f },  // 17 Sand
        { GLS_TELA,     2, 0.16f, 0.4f },  // 18 Fabric
        { GLS_RUIDO,    8, 0.34f, 0.9f },  // 19 Rock
        { GLS_RUIDO,    4, 0.10f, 0.3f },  // 20 Snow
        { GLS_CELDAS,   6, 0.38f, 1.0f },  // 21 Cobblestone
        { GLS_CELDAS,   3, 0.30f, 0.7f },  // 22 Pebble
        { GLS_RUIDO,    5, 0.28f, 0.6f },  // 23 Ground
        { GLS_RUIDO,    6, 0.24f, 0.5f },  // 24 Mud
        { GLS_CAPAS,    6, 0.20f, 0.4f },  // 25 Sandstone
        { GLS_RUIDO,    6, 0.28f, 0.6f },  // 26 Basalt
        { GLS_CELDAS,   6, 0.44f, 0.9f },  // 27 CrackedLava
        { GLS_CAPAS,    8, 0.12f, 0.3f },  // 28 Glacier
        { GLS_HIERBA,   4, 0.34f, 0.7f },  // 29 LeafyGrass
        { GLS_RUIDO,    3, 0.14f, 0.3f },  // 30 Salt
        { GLS_RUIDO,    6, 0.18f, 0.4f },  // 31 Limestone
        { GLS_LADRILLO, 8, 0.22f, 0.5f },  // 32 Pavement
        { GLS_RUIDO,    4, 0.20f, 0.3f },  // 33 Asphalt
        { GLS_LISO,     1, 0.00f, 0.0f },  // 34 ForceField
        { GLS_LISO,     1, 0.00f, 0.0f },  // 35 Water
        { GLS_LISO,     1, 0.00f, 0.0f },  // 36 Air
    };
    static const GLSMat LISO = { GLS_LISO, 1, 0.0f, 0.0f };
    if (m < 0 || m >= 37) return LISO;
    return T[m];
}

// Altura (0..1) del patron en el punto (u,v) del mosaico.
inline float gls_altura(int patron, float u, float v) {
    switch (patron) {
        case GLS_RUIDO:
            return gls_fbm(u, v, 8, 8, 4);

        case GLS_VETA: {
            // Vetas: ruido MUY estirado en X + anillos de crecimiento.
            const float g = gls_fbm(u, v, 2, 24, 4);
            const float anillo = gls_onda(v * 5.0f + g * 2.0f);
            return Math::clamp(0.35f + 0.45f * anillo + 0.20f * g, 0.0f, 1.0f);
        }
        case GLS_TABLAS: {
            const float filas = 4.0f;
            const int   tabla = (int)Math::floor(v * filas);
            const float dentro = v * filas - (float)tabla;
            const float off = gls_hash(tabla * 17, 3);
            const float g = gls_fbm(u + off, v, 2, 24, 4);
            float h = Math::clamp(0.40f + 0.45f * gls_onda(v * 12.0f + g * 2.0f), 0.0f, 1.0f);
            h *= 0.85f + 0.15f * off;                       // cada tabla, su tono
            if (dentro < 0.025f || dentro > 0.975f) h *= 0.35f;   // junta entre tablas
            return h;
        }
        case GLS_LADRILLO: {
            const float filas = 5.0f, cols = 2.5f;
            const float fy = v * filas;
            const int   fila = (int)Math::floor(fy);
            const float ry = fy - (float)fila;
            const float fx = u * cols + ((fila & 1) ? 0.5f : 0.0f);
            const float rx = fx - Math::floor(fx);
            const bool junta = ry < 0.07f || ry > 0.93f || rx < 0.035f || rx > 0.965f;
            if (junta) return 0.25f;
            const float tono = gls_hash(fila * 31 + (int)Math::floor(fx) * 7, 11);
            return Math::clamp(0.72f + 0.18f * tono + 0.15f * gls_fbm(u, v, 16, 16, 3), 0.0f, 1.0f);
        }
        case GLS_CELDAS: {
            // Voronoi con envoltura: la piedra mas cercana manda.
            const int N = 5;
            float mejor = 9.0f;
            const float cx = u * N, cy = v * N;
            const int ix = (int)Math::floor(cx), iy = (int)Math::floor(cy);
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    const int gx = ix + dx, gy = iy + dy;
                    const int wx = ((gx % N) + N) % N, wy = ((gy % N) + N) % N;
                    const float px = (float)gx + 0.15f + 0.7f * gls_hash(wx, wy);
                    const float py = (float)gy + 0.15f + 0.7f * gls_hash(wx + 71, wy + 13);
                    const float d = Math::sqrt((cx - px) * (cx - px) + (cy - py) * (cy - py));
                    if (d < mejor) mejor = d;
                }
            }
            const float borde = Math::clamp(mejor * 1.9f, 0.0f, 1.0f);
            return Math::clamp(1.0f - borde * borde + 0.12f * gls_fbm(u, v, 16, 16, 2) - 0.06f, 0.0f, 1.0f);
        }
        case GLS_HIERBA: {
            const float fino = gls_fbm(u, v, 24, 24, 3);
            const float briznas = gls_fbm(u, v, 32, 4, 2);   // estiradas en vertical
            return Math::clamp(0.30f + 0.45f * fino + 0.35f * briznas, 0.0f, 1.0f);
        }
        case GLS_RAYAS:
            return Math::clamp(0.45f + 0.55f * gls_fbm(u, v, 64, 2, 3), 0.0f, 1.0f);

        case GLS_DIAMANTE: {
            // Barritas en diagonal que alternan de sentido por fila.
            const float s = 4.0f;
            const int fila = (int)Math::floor(v * s * 2.0f);
            const float dir = (fila & 1) ? -1.0f : 1.0f;
            const float a = (u * s + dir * v * s);
            const float ra = a - Math::floor(a);
            const float ry = v * s * 2.0f - (float)fila;
            const float barra = Math::clamp(1.0f - Math::abs(ra - 0.5f) * 3.2f, 0.0f, 1.0f)
                              * Math::clamp(1.0f - Math::abs(ry - 0.5f) * 2.4f, 0.0f, 1.0f);
            return 0.30f + 0.70f * barra;
        }
        case GLS_TELA: {
            const float n = 12.0f;
            const float trama = (gls_onda(u * n) + gls_onda(v * n)) * 0.5f;
            return Math::clamp(0.45f + 0.40f * trama + 0.15f * gls_fbm(u, v, 32, 32, 2), 0.0f, 1.0f);
        }
        case GLS_CAPAS: {
            const float n = gls_fbm(u, v, 6, 24, 4);
            const float veta = gls_onda(v * 4.0f + n * 1.6f);
            return Math::clamp(0.35f + 0.50f * veta + 0.15f * n, 0.0f, 1.0f);
        }
        default:
            return 1.0f;
    }
}

// ── Texturas por Material ─────────────────────────────────────────────
inline HashMap<int, Ref<ImageTexture>>& gls_cache_albedo() {
    static HashMap<int, Ref<ImageTexture>> c;
    return c;
}
inline HashMap<int, Ref<ImageTexture>>& gls_cache_normal() {
    static HashMap<int, Ref<ImageTexture>> c;
    return c;
}

// Genera (una vez) el par albedo + normales del material. Devuelve false si
// el material es liso y no lleva textura.
inline bool gls_generar(int m) {
    const GLSMat& t = gls_tabla(m);
    if (t.patron == GLS_LISO) return false;
    if (gls_cache_albedo().has(m)) return true;

    const int L = 128;
    // Campo de alturas primero: el mapa de normales sale de su pendiente y
    // el albedo de su valor, asi los dos cuentan lo mismo.
    LocalVector<float> h;
    h.resize(L * L);
    for (int y = 0; y < L; y++) {
        for (int x = 0; x < L; x++) {
            h[y * L + x] = gls_altura(t.patron, (float)x / (float)L, (float)y / (float)L);
        }
    }

    PackedByteArray alb; alb.resize(L * L * 3);
    PackedByteArray nrm; nrm.resize(L * L * 3);
    uint8_t* pa = alb.ptrw();
    uint8_t* pn = nrm.ptrw();
    for (int y = 0; y < L; y++) {
        for (int x = 0; x < L; x++) {
            const int i = y * L + x;
            // Albedo: un multiplicador, nunca un color. El color de la pieza
            // manda; la textura solo lo oscurece donde hay hueco.
            const float f = 1.0f - t.fuerza * (1.0f - h[i]);
            const uint8_t v = (uint8_t)Math::clamp(f * 255.0f, 0.0f, 255.0f);
            pa[i * 3 + 0] = v; pa[i * 3 + 1] = v; pa[i * 3 + 2] = v;

            const int xd = (x + 1) % L, xi = (x + L - 1) % L;
            const int yd = (y + 1) % L, yi = (y + L - 1) % L;
            // El factor tiene que ser suave: con un patron de escalones (chapa
            // antideslizante) un gradiente grande deja la normal casi tumbada,
            // la luz no le llega y la pieza sale NEGRA.
            const float gx = (h[y * L + xd] - h[y * L + xi]) * t.relieve * 1.2f;
            const float gy = (h[yd * L + x] - h[yi * L + x]) * t.relieve * 1.2f;
            const float len = Math::sqrt(gx * gx + gy * gy + 1.0f);
            pn[i * 3 + 0] = (uint8_t)Math::clamp((-gx / len * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f);
            pn[i * 3 + 1] = (uint8_t)Math::clamp((gy / len * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f);
            pn[i * 3 + 2] = (uint8_t)Math::clamp((1.0f / len * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f);
        }
    }

    Ref<Image> ia = Image::create_from_data(L, L, false, Image::FORMAT_RGB8, alb);
    Ref<Image> in = Image::create_from_data(L, L, false, Image::FORMAT_RGB8, nrm);
    if (ia.is_valid()) ia->generate_mipmaps();
    if (in.is_valid()) in->generate_mipmaps();
    gls_cache_albedo()[m] = ImageTexture::create_from_image(ia);
    gls_cache_normal()[m] = ImageTexture::create_from_image(in);
    return true;
}

inline Ref<ImageTexture> gls_tex_albedo(int m) {
    if (!gls_generar(m)) return Ref<ImageTexture>();
    Ref<ImageTexture>* f = gls_cache_albedo().getptr(m);
    return f ? *f : Ref<ImageTexture>();
}
inline Ref<ImageTexture> gls_tex_normal(int m) {
    if (!gls_generar(m)) return Ref<ImageTexture>();
    Ref<ImageTexture>* f = gls_cache_normal().getptr(m);
    return f ? *f : Ref<ImageTexture>();
}

// Aplica la textura procedural del Material a un StandardMaterial3D. Se
// proyecta en TRIPLANAR (las tres caras a la vez) porque asi no hay que tocar
// las UV de la malla y una caja de 40x1x40 no estira el dibujo.
inline void gls_vestir_material(const Ref<StandardMaterial3D>& mat, int m) {
    if (!mat.is_valid()) return;
    // Interruptor de medida (A/B): GL_SIN_TEXTURA_MATERIAL=1 deja las piezas sin
    // la textura del Material, para saber cuanto cuesta en la GPU.
    static const bool sin_textura = OS::get_singleton() &&
        OS::get_singleton()->get_environment("GL_SIN_TEXTURA_MATERIAL") == "1";
    if (sin_textura) return;
    Ref<ImageTexture> alb = gls_tex_albedo(m);
    if (!alb.is_valid()) return;
    const float e = 1.0f / Math::max(gls_tabla(m).studs, 0.25f);
    mat->set_texture(StandardMaterial3D::TEXTURE_ALBEDO, alb);
    mat->set_uv1_scale(Vector3(e, e, e));
    mat->set_flag(BaseMaterial3D::FLAG_UV1_USE_TRIPLANAR, true);
    // Anisotropo: visto de canto, sin el la textura se emborrona (ver gl_tex).
    mat->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC);
    Ref<ImageTexture> nor = gls_tex_normal(m);
    if (nor.is_valid() && gls_tabla(m).relieve > 0.0f) {
        mat->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
        mat->set_texture(StandardMaterial3D::TEXTURE_NORMAL, nor);
        mat->set_normal_scale(1.0f);
    }
}


// ── LA TEXTURA DE UN STUD ─────────────────────────────────────────────
// Los studs se dibujaban con una formula dentro del shader. Eso sale nitido de
// cerca, pero NO TIENE MIPMAPS: a distancia, o mirando el suelo de canto, cada
// stud ocupa menos de un pixel y el patron degenera en moire — rombos, rejillas
// que se mueven, "efectos raros" que en Roblox no pasan. Y no hay desvanecido
// que lo arregle del todo, porque el problema es el MUESTREO, no el brillo.
//
// Roblox dibuja los studs con una TEXTURA, y una textura trae mipmaps: la GPU
// elige sola una version mas pequeña y ya promediada segun lo que ocupa en
// pantalla, en cada pixel y para cualquier angulo. Esa es la solucion de fondo.
//
// Un mosaico = UN stud, y el teton es CUADRADO.
inline HashMap<int, Ref<ImageTexture>>& gls_cache_stud() {
    static HashMap<int, Ref<ImageTexture>> c;
    return c;
}

// cual: 0 = multiplicador de color, 1 = mapa de normales
inline Ref<ImageTexture> gls_tex_stud(int cual) {
    if (Ref<ImageTexture>* f = gls_cache_stud().getptr(cual)) return *f;

    const int   L     = 128;
    const float LADO  = 0.30f;   // medio lado del teton (fraccion del stud)
    const float BISEL = 0.055f;  // chaflan

    LocalVector<float> h;
    h.resize(L * L);
    for (int y = 0; y < L; y++) {
        for (int x = 0; x < L; x++) {
            const float u = ((float)x + 0.5f) / (float)L - 0.5f;
            const float v = ((float)y + 0.5f) / (float)L - 0.5f;
            // Distancia del CUADRADO (la mayor de las dos), no la del circulo.
            const float d = Math::max(Math::abs(u), Math::abs(v));
            h[y * L + x] = 1.0f - Math::clamp((d - (LADO - BISEL)) / BISEL, 0.0f, 1.0f);
        }
    }

    PackedByteArray datos;
    datos.resize(L * L * 3);
    uint8_t* p = datos.ptrw();
    for (int y = 0; y < L; y++) {
        for (int x = 0; x < L; x++) {
            const int i = y * L + x;
            if (cual == 0) {
                const float u = ((float)x + 0.5f) / (float)L - 0.5f;
                const float v = ((float)y + 0.5f) / (float)L - 0.5f;
                const float d = Math::max(Math::abs(u), Math::abs(v));
                float f = 1.0f;
                if (h[i] > 0.5f)             f = 1.05f;   // meseta del teton
                else if (d < LADO + 0.05f)   f = 0.88f;   // aro de contacto
                const uint8_t c8 = (uint8_t)Math::clamp(f * 200.0f, 0.0f, 255.0f);
                p[i * 3 + 0] = c8; p[i * 3 + 1] = c8; p[i * 3 + 2] = c8;
            } else {
                const int xd = (x + 1) % L, xi = (x + L - 1) % L;
                const int yd = (y + 1) % L, yi = (y + L - 1) % L;
                const float gx = (h[y * L + xd] - h[y * L + xi]) * 2.2f;
                const float gy = (h[yd * L + x] - h[yi * L + x]) * 2.2f;
                const float len = Math::sqrt(gx * gx + gy * gy + 1.0f);
                p[i * 3 + 0] = (uint8_t)Math::clamp((-gx / len * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f);
                p[i * 3 + 1] = (uint8_t)Math::clamp(( gy / len * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f);
                p[i * 3 + 2] = (uint8_t)Math::clamp(( 1.0f / len * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f);
            }
        }
    }
    Ref<Image> img = Image::create_from_data(L, L, false, Image::FORMAT_RGB8, datos);
    if (img.is_valid()) img->generate_mipmaps();   // <- esto es lo que mata el moire
    Ref<ImageTexture> tex = ImageTexture::create_from_image(img);
    gls_cache_stud()[cual] = tex;
    return tex;
}

// ── Studs / Inlet ─────────────────────────────────────────────────────
// SurfaceType de Roblox -> lo que dibuja el shader: 0 nada, 1 teton, 2 hueco.
inline float gls_tipo_cara(int surf) {
    switch (surf) {
        case 3: return 1.0f;   // Studs
        case 5: return 1.0f;   // Universal
        case 4: return 2.0f;   // Inlet
        default: return 0.0f;  // Smooth, Glue, Weld, Hinge, Motor...
    }
}

// El shader. Uno opaco y otro con alfa: si un shader escribe ALPHA, Godot
// manda la pieza a la cola de transparentes (sin sombras y ordenando por
// distancia), y eso NO puede pasarle a un muro opaco.
inline Ref<Shader>* gls_shaders() {
    static Ref<Shader> dos[2];
    return dos;
}

inline Ref<Shader> gls_shader(bool con_alfa) {
    Ref<Shader>& dest = gls_shaders()[con_alfa ? 1 : 0];
    if (dest.is_valid()) return dest;

    String src =
        "shader_type spatial;\n"
        "render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_lambert, specular_schlick_ggx;\n"
        "uniform vec4  gl_col : source_color = vec4(1.0);\n"
        "uniform float gl_rug : hint_range(0.0, 1.0) = 0.55;\n"
        "uniform float gl_met : hint_range(0.0, 1.0) = 0.0;\n"
        "uniform float gl_esp : hint_range(0.0, 1.0) = 0.5;\n"
        "uniform vec3  gl_emi : source_color = vec3(0.0);\n"
        "uniform float gl_emi_f = 0.0;\n"
        // Top, Bottom, Front / Back, Left, Right
        "uniform vec3  gl_sa = vec3(0.0);\n"
        "uniform vec3  gl_sb = vec3(0.0);\n"
        // ANISOTROPO, como los studs: sin el, un suelo o un borde visto de canto
        // (el borde de tablones del camino de Build a Mountain) coge un mipmap
        // tan pequeño que la textura se deshace en una mancha, y en Roblox se
        // ve nitida hasta lejos.
        "uniform sampler2D gl_tex : source_color, hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;\n"
        "uniform sampler2D gl_studs   : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;\n"
        "uniform sampler2D gl_studs_n : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;\n"
        "uniform float gl_tex_esc = 0.0;\n"
        // TODO lo de la cara se decide en vertex(), no por pixel: la malla es una
        // caja UNITARIA (24 vertices, 4 por cara con la misma normal), asi que
        // la cara, su coordenada en studs y sus ejes son los mismos en los 4
        // vertices y `p` es lineal dentro de la cara. Hacerlo por pixel (con
        // ramas y 7 varyings) costaba 0,73 ms de GPU en Build a Mountain.
        // Y tiene que ser en vertex() por otra razon: en una pieza dibujada por
        // el agrupado estatico (MultiMesh), MODEL_MATRIX solo trae la
        // transformacion de la instancia en vertex(); en fragment() es la del
        // nodo del MultiMesh (la identidad), y cada pieza agrupada "medía"
        // 1x1x1 y estiraba un trozo de su textura a todo lo largo (el borde de
        // tablones del camino de Build a Mountain salia como una mancha beige).
        "varying vec2 v_p;\n"            // coordenada en la cara, en studs
        "varying vec3 v_nmu;\n"          // normal en el mundo
        "varying flat vec3 v_t1;\n"      // ejes de la cara en el mundo (relieve)
        "varying flat vec3 v_t2;\n"
        "varying flat float v_tipo;\n"   // studs (1) o inlet (2) de la cara
        "void vertex() {\n"
        "  v_nmu = MODEL_NORMAL_MATRIX * NORMAL;\n"
        "  vec3 ex = MODEL_MATRIX[0].xyz; vec3 ey = MODEL_MATRIX[1].xyz; vec3 ez = MODEL_MATRIX[2].xyz;\n"
        "  vec3 tam = vec3(length(ex), length(ey), length(ez));\n"
        "  vec3 ax = ex / max(tam.x, 0.0001); vec3 ay = ey / max(tam.y, 0.0001); vec3 az = ez / max(tam.z, 0.0001);\n"
        "  vec3 n = NORMAL;\n"
        "  vec3 an = abs(n);\n"
        "  if (an.y >= an.x && an.y >= an.z) {\n"
        "    v_p = vec2((VERTEX.x + 0.5) * tam.x, (VERTEX.z + 0.5) * tam.z);\n"
        "    v_t1 = ax; v_t2 = az;\n"
        "    v_tipo = n.y > 0.0 ? gl_sa.x : gl_sa.y;\n"
        "  } else if (an.z >= an.x) {\n"
        "    v_p = vec2((VERTEX.x + 0.5) * tam.x, (VERTEX.y + 0.5) * tam.y);\n"
        "    v_t1 = ax; v_t2 = ay;\n"
        "    v_tipo = n.z > 0.0 ? gl_sb.x : gl_sa.z;\n"
        "  } else {\n"
        "    v_p = vec2((VERTEX.z + 0.5) * tam.z, (VERTEX.y + 0.5) * tam.y);\n"
        "    v_t1 = az; v_t2 = ay;\n"
        "    v_tipo = n.x > 0.0 ? gl_sb.z : gl_sb.y;\n"
        "  }\n"
        "}\n"
        "void fragment() {\n"
        "  vec2 p = v_p; vec3 t1 = v_t1; vec3 t2 = v_t2; float tipo = v_tipo;\n"
        "  vec3 col = gl_col.rgb;\n";
    // Interruptor de medida (A/B): GL_VAR_SIN_MUESTREO=1 deja el shader igual
    // pero sin leer la textura, para separar su coste del del shader.
    static const bool sin_muestreo = OS::get_singleton() &&
        OS::get_singleton()->get_environment("GL_VAR_SIN_MUESTREO") == "1";
    if (!sin_muestreo) src += "  if (gl_tex_esc > 0.0) { col *= texture(gl_tex, p * gl_tex_esc).rgb; }\n";
    src +=
        "  vec3 nn = normalize(v_nmu);\n"
        "  if (tipo > 0.5) {\n"
        // Un mosaico = un stud. Como `p` ya esta EN STUDS, sirve tal cual de
        // coordenada: la GPU repite la textura y elige el mipmap ella sola, en
        // cada pixel y para cualquier angulo.
        "    float signo = tipo > 1.5 ? -1.0 : 1.0;\n"
        "    float mul = texture(gl_studs, p).r / 0.784;\n"
        "    vec3  nt  = texture(gl_studs_n, p).rgb * 2.0 - 1.0;\n"
        // Inlet = el mismo teton pero hundido: se invierte el relieve y la
        // meseta oscurece en vez de aclarar.
        "    if (signo < 0.0) { nt.xy = -nt.xy; mul = 2.0 - mul; }\n"
        "    nn = normalize(nn * nt.z + t1 * nt.x + t2 * nt.y);\n"
        "    col *= mul;\n"
        "  }\n"
        "  ALBEDO = col;\n"
        "  ROUGHNESS = gl_rug;\n"
        "  METALLIC = gl_met;\n"
        "  SPECULAR = gl_esp;\n"
        "  NORMAL = normalize((VIEW_MATRIX * vec4(nn, 0.0)).xyz);\n"
        "  if (gl_emi_f > 0.0) { EMISSION = gl_emi * gl_emi_f; }\n";
    src += con_alfa ? "  ALPHA = gl_col.a;\n}\n" : "}\n";

    dest.instantiate();
    dest->set_code(src);
    return dest;
}

inline HashMap<uint64_t, Ref<ShaderMaterial>>& gls_cache_mats() {
    static HashMap<uint64_t, Ref<ShaderMaterial>> c;
    return c;
}

// Devuelve el material de superficies equivalente a `base`, o null si la
// pieza no tiene ninguna cara con relieve (el caso normal: se queda con su
// StandardMaterial3D compartido, que es mas barato).
// Con `variante` (el ColorMap de una MaterialVariant, ver gl_variantes.h) la
// pieza pasa SIEMPRE a este shader aunque no tenga studs: la textura de la
// variante sustituye a la del Material, en mosaico de `var_studs` studs.
// Con `propia`, la version con la luz propia (gl_luz_propia.h); sin ella, la de
// Godot (la de las piezas de un ViewportFrame, que tiene sus propias luces).
inline Ref<ShaderMaterial> gls_material_superficie(const Ref<StandardMaterial3D>& base,
                                                   uint64_t clave_base, int mat_rbx,
                                                   int top, int bot, int fro,
                                                   int bac, int lef, int rig,
                                                   const Ref<Texture2D>& variante = Ref<Texture2D>(),
                                                   float var_studs = 10.0f, bool propia = false) {
    if (!base.is_valid()) return Ref<ShaderMaterial>();
    const float sa_x = gls_tipo_cara(top), sa_y = gls_tipo_cara(bot), sa_z = gls_tipo_cara(fro);
    const float sb_x = gls_tipo_cara(bac), sb_y = gls_tipo_cara(lef), sb_z = gls_tipo_cara(rig);
    const bool con_variante = variante.is_valid();
    if (!con_variante && sa_x + sa_y + sa_z + sb_x + sb_y + sb_z <= 0.0f) return Ref<ShaderMaterial>();

    // Igual que en roblox_part.h: la clave era un String armado con seis
    // formateos y seis concatenaciones, en el camino de cada re-pintado de
    // pieza. Ahora se mezclan los seis tipos de cara (3 bits cada uno) con la
    // clave visual, que ya llega empaquetada.
    uint64_t caras = 0;
    const int tipos[6] = { (int)sa_x, (int)sa_y, (int)sa_z, (int)sb_x, (int)sb_y, (int)sb_z };
    for (int i = 0; i < 6; i++) caras = (caras << 3) | (uint64_t)(tipos[i] & 0x7);
    // La clave visual ya ocupa sus 64 bits, asi que las caras se MEZCLAN en vez
    // de concatenarse (mismo truco que el hash de estilos de la GUI).
    uint64_t clave = clave_base ^ (caras * 0x9E3779B97F4A7C15ULL);
    if (con_variante) {
        // La textura (por su id de objeto) y su escala tambien distinguen.
        clave ^= ((uint64_t)variante->get_instance_id() + 0x632BE59BD9B4E019ULL) * 0xC2B2AE3D27D4EB4FULL;
        clave ^= (uint64_t)(var_studs * 1024.0f) * 0x165667B19E3779F9ULL;
    }
    if (propia) clave ^= 0xD6E8FEB86659FD93ULL;
    HashMap<uint64_t, Ref<ShaderMaterial>>& cache = gls_cache_mats();
    if (Ref<ShaderMaterial>* f = cache.getptr(clave)) return *f;

    const bool alfa = base->get_transparency() != BaseMaterial3D::TRANSPARENCY_DISABLED;
    Ref<ShaderMaterial> sm;
    sm.instantiate();
    sm->set_shader(propia ? gl_luz_shader(GL_LUZ_CARAS | (alfa ? GL_LUZ_ALFA : 0u)) : gls_shader(alfa));
    sm->set_shader_parameter("gl_col", base->get_albedo());
    sm->set_shader_parameter("gl_rug", base->get_roughness());
    sm->set_shader_parameter("gl_met", base->get_metallic());
    sm->set_shader_parameter("gl_esp", base->get_specular());
    sm->set_shader_parameter("gl_sa", Vector3(sa_x, sa_y, sa_z));
    sm->set_shader_parameter("gl_sb", Vector3(sb_x, sb_y, sb_z));
    if (base->get_feature(BaseMaterial3D::FEATURE_EMISSION)) {
        sm->set_shader_parameter("gl_emi", base->get_emission());
        sm->set_shader_parameter("gl_emi_f", base->get_emission_energy_multiplier());
    }
    sm->set_shader_parameter("gl_studs",   gls_tex_stud(0));
    sm->set_shader_parameter("gl_studs_n", gls_tex_stud(1));
    if (con_variante) {
        // Roblox: ColorMap x Color de la pieza, una baldosa cada StudsPerTile.
        sm->set_shader_parameter("gl_tex", variante);
        sm->set_shader_parameter("gl_tex_esc", 1.0f / Math::max(var_studs, 0.05f));
    } else {
        Ref<ImageTexture> tex = gls_tex_albedo(mat_rbx);
        if (tex.is_valid()) {
            sm->set_shader_parameter("gl_tex", tex);
            sm->set_shader_parameter("gl_tex_esc", 1.0f / Math::max(gls_tabla(mat_rbx).studs, 0.25f));
        }
    }
    cache[clave] = sm;
    // Sin esto el cache crece sin fin: cada combinacion de material y caras
    // deja un ShaderMaterial dentro para siempre. Ver gl_purgar_cache.
    gl_purgar_cache(cache, 4096);
    return sm;
}

// Al cerrar: los caches son estaticos y sueltan Recursos del motor, asi que
// hay que vaciarlos antes de que el servidor de render se apague (mismo motivo
// que gl_shared_materials().clear()).
inline void gls_liberar() {
    gls_cache_mats().clear();
    gls_cache_albedo().clear();
    gls_cache_normal().clear();
    gls_cache_stud().clear();
    gls_shaders()[0] = Ref<Shader>();
    gls_shaders()[1] = Ref<Shader>();
}

#endif // GL_SUPERFICIES_H

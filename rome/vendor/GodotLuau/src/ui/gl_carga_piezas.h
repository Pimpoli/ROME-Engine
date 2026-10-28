#ifndef GL_CARGA_PIEZAS_H
#define GL_CARGA_PIEZAS_H

// Lo que usa la pantalla de carga por defecto (gl_carga_defecto.h) y no
// depende de ella: recursos, cortes de poligonos para la rotura y particulas.
// Las curvas de la animacion estan en los shaders (gl_carga_shaders.h). Las
// cifras salen del splash de Click the Button (spash_screen.tscn) y de su
// version de Roblox en Just Press it (SplashClient.lua, Motor2D
// Effects.Shatter), en pixeles de su lienzo de 640x360.

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/vector2.hpp>

using namespace godot;

// Lo que va dentro de la DLL (core/gl_carga_logos.cpp), en el orden del
// generador (tools/generar_logos_carga.py).
enum GLCargaRecurso {
    GLC_REC_LOGO_RM = 0,
    GLC_REC_LOGO_GL,
    GLC_REC_BORDE,
    GLC_REC_HUMO,
    GLC_REC_ESTRELLA,
    GLC_REC_SON_IMPACTO,
    GLC_REC_SON_LETRA,
    GLC_REC_SON_ARCOIRIS,
    GLC_REC_SON_ROTURA,
};
const unsigned char* gl_carga_recurso(int i, size_t& n);

// Cronometro de la traza: suma a `acc` los us que dura su ambito.
struct GLCargaCrono {
    uint64_t& acc;
    uint64_t t0;
    explicit GLCargaCrono(uint64_t& a) : acc(a), t0(Time::get_singleton()->get_ticks_usec()) {}
    ~GLCargaCrono() { acc += Time::get_singleton()->get_ticks_usec() - t0; }
};

// La × de la fila: la X de la fuente pixel (el signo × tiene los pixeles unidos
// solo por las esquinas y con el borde parecia un tablero). Su tinta ocupa los
// pixeles 0 a 6 de los 8 del em: su centro cae a 3,5/8 del em de la esquina.
#define GLC_ASPA_LETRA "X"
inline Vector2 gl_carga_aspa_origen(const Vector2& centro, float em, float esc) {
    return centro - Vector2(em, em) * (0.4375f * esc);
}

// El ease() de Godot (las transiciones de las claves de un AnimationPlayer).
inline float gl_carga_ease(float x, float c) {
    x = CLAMP(x, 0.0f, 1.0f);
    if (c > 0.0f) {
        if (c < 1.0f) return 1.0f - Math::pow(1.0f - x, 1.0f / c);
        return Math::pow(x, c);
    }
    if (c < 0.0f) {
        if (x < 0.5f) return Math::pow(x * 2.0f, -c) * 0.5f;
        return (1.0f - Math::pow(1.0f - (x - 0.5f) * 2.0f, -c)) * 0.5f + 0.5f;
    }
    return 0.0f;
}

// Parte un poligono convexo por la recta que pasa por p con normal n.
inline void gl_carga_cortar(const LocalVector<Vector2>& poli, const Vector2& p, const Vector2& n,
                            LocalVector<Vector2>& a, LocalVector<Vector2>& b) {
    a.clear();
    b.clear();
    const uint32_t m = poli.size();
    for (uint32_t i = 0; i < m; i++) {
        const Vector2& c = poli[i];
        const Vector2& d = poli[(i + 1) % m];
        const float dc = n.dot(c - p), dd = n.dot(d - p);
        if (dc >= 0.0f) a.push_back(c); else b.push_back(c);
        if ((dc >= 0.0f) != (dd >= 0.0f)) {
            const Vector2 x = c + (d - c) * (dc / (dc - dd));
            a.push_back(x);
            b.push_back(x);
        }
    }
}

// Un trozo de la rotura (Effects.Shatter): tiembla `grieta` s y salta.
struct GLCargaPieza {
    LocalVector<Vector2> p;    // puntos, en px de pantalla, respecto a `casa`
    LocalVector<Vector2> uv;
    Vector2 casa, vel;          // px y px/s de pantalla
    float giro = 0.0f;          // grados/s
    float grieta = 0.14f;       // s temblando antes de saltar
    float temblor = 1.5f;       // px de pantalla
};

// Una particula de humo o estrella (los GPUParticles2D del original, a mano).
struct GLCargaParticula {
    Vector2 pos, vel;
    float ang = 0.0f, giro = 0.0f;   // radianes y radianes/s
    float vida = 1.0f, edad = 0.0f;
    float escala = 1.0f;
    float freno = 0.0f;              // estrellas: amortiguacion
    bool estrella = false;
};

#endif // GL_CARGA_PIEZAS_H

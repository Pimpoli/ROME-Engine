#ifndef GL_COLGROUPS_H
#define GL_COLGROUPS_H

// ════════════════════════════════════════════════════════════════════
//  GRUPOS DE COLISION (PhysicsService de Roblox), DE VERDAD
//
//  Antes esto era contabilidad y nada mas: PhysicsService vivia entero en Luau,
//  `SetPartCollisionGroup` escribia un atributo y `CollisionGroupsAreCollidable`
//  respondia de una tabla. NADA de eso llegaba al motor, asi que dos piezas de
//  grupos declarados "no colisionables" seguian chocando tan tranquilas. Un
//  place que use grupos (casi cualquiera con NPCs, proyectiles o plataformas
//  que se atraviesan) se comportaba mal y sin dar ni un error.
//
//  Ahora se cablea a las capas y mascaras de Godot. Encaja exacto: Godot tiene
//  32 capas de colision y Roblox admite 32 grupos.
//
//    · cada grupo ocupa UN bit (Default es el 0 y siempre existe)
//    · la capa de una pieza es el bit de su grupo
//    · la mascara de una pieza es "con que bits choca mi grupo"
//
//  La matriz es simetrica, como en Roblox: si A no choca con B, B tampoco con A.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>

using namespace godot;

static const int GL_MAX_GRUPOS_COL = 32;

// La capa 32 (bit 31) no es un grupo: es la de las piezas que NO chocan pero
// se pueden consultar (CanCollide = false, CanQuery = true). Ninguna mascara de
// cuerpo la incluye, asi que nada choca con ellas; los rayos y las consultas de
// zona (mascara completa) si las ven, como en Roblox. Quedan 31 grupos.
inline constexpr uint32_t GL_CAPA_CONSULTA = 1u << 31;

// GL_SIN_FANTASMAS=1: las piezas que no chocan vuelven a no tener forma (como
// antes de la v1.17). Solo para medir lo que cuesta la capa de consultas.
inline bool gl_sin_fantasmas() {
    static const bool on = godot::OS::get_singleton()
        && godot::OS::get_singleton()->get_environment("GL_SIN_FANTASMAS") == "1";
    return on;
}

struct GLGruposColision {
    HashMap<String, int> indice;                 // nombre -> 0..31
    LocalVector<String>  nombres;
    uint32_t             mascara[GL_MAX_GRUPOS_COL];
    // Sube en cada cambio de la matriz: las piezas comparan contra ella para
    // saber si su mascara esta al dia.
    uint32_t             generacion = 1;
};

inline GLGruposColision& gl_colgrupos() {
    static GLGruposColision g;
    if (g.nombres.is_empty()) {
        // De serie todos chocan con todos, como en Roblox (menos con la capa
        // de consultas, que no es de nadie).
        for (int i = 0; i < GL_MAX_GRUPOS_COL; i++) g.mascara[i] = ~GL_CAPA_CONSULTA;
        g.nombres.push_back(String("Default"));
        g.indice[String("Default")] = 0;
    }
    return g;
}

// Devuelve el indice del grupo, registrandolo si es nuevo. Si ya no caben mas
// (32 en Roblox tambien), se cae a Default en vez de romper.
inline int gl_colgrupo_indice(const String& nombre) {
    GLGruposColision& g = gl_colgrupos();
    if (nombre.is_empty()) return 0;
    if (const int* hay = g.indice.getptr(nombre)) return *hay;
    if ((int)g.nombres.size() >= GL_MAX_GRUPOS_COL - 1) return 0;   // el bit 31 es GL_CAPA_CONSULTA
    const int idx = (int)g.nombres.size();
    g.nombres.push_back(nombre);
    g.indice[nombre] = idx;
    return idx;
}

inline bool gl_colgrupo_existe(const String& nombre) {
    return gl_colgrupos().indice.has(nombre);
}

inline String gl_colgrupo_nombre(int idx) {
    GLGruposColision& g = gl_colgrupos();
    if (idx < 0 || idx >= (int)g.nombres.size()) return String("Default");
    return g.nombres[idx];
}

inline void gl_colgrupo_quitar(const String& nombre) {
    GLGruposColision& g = gl_colgrupos();
    const int* hay = g.indice.getptr(nombre);
    if (!hay || *hay == 0) return;             // Default no se quita
    const int idx = *hay;
    // Se libera el bit: vuelve a chocar con todo y el nombre se olvida. Las
    // piezas que estuvieran en el pasan a Default en su siguiente repaso.
    for (int i = 0; i < GL_MAX_GRUPOS_COL; i++) {
        g.mascara[i] |= (1u << idx);
        g.mascara[idx] |= (1u << i);
    }
    g.indice.erase(nombre);
    g.nombres[idx] = String();                 // hueco: el indice no se reutiliza
    g.generacion++;
}

inline void gl_colgrupo_colisionable(const String& a, const String& b, bool si) {
    GLGruposColision& g = gl_colgrupos();
    const int ia = gl_colgrupo_indice(a);
    const int ib = gl_colgrupo_indice(b);
    if (si) {
        g.mascara[ia] |= (1u << ib);
        g.mascara[ib] |= (1u << ia);
    } else {
        g.mascara[ia] &= ~(1u << ib);
        g.mascara[ib] &= ~(1u << ia);
    }
    g.generacion++;
}

inline bool gl_colgrupos_colisionan(const String& a, const String& b) {
    GLGruposColision& g = gl_colgrupos();
    const int ia = gl_colgrupo_indice(a);
    const int ib = gl_colgrupo_indice(b);
    return (g.mascara[ia] & (1u << ib)) != 0;
}

inline uint32_t gl_colgrupo_mascara(int idx) {
    if (idx < 0 || idx >= GL_MAX_GRUPOS_COL) idx = 0;
    return gl_colgrupos().mascara[idx] & ~GL_CAPA_CONSULTA;
}

#endif // GL_COLGROUPS_H

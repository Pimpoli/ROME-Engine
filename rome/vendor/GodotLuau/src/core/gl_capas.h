#ifndef GL_CAPAS_H
#define GL_CAPAS_H

// Capas 3D (VisualInstance3D::layers) y estado compartido de las sombras
// propias (ui/gl_sombras.h). La camara del juego ve todas las capas; las del
// sol no:
//  - la del mapa guardado (baldosas) ve GL_CAPA_MUNDO: lo que no se mueve;
//  - la del mapa de cada frame ve solo GL_CAPA_DINAMICA: lo que se mueve
//    (piezas sueltas, personajes);
//  - GL_CAPA_SIN_SOMBRA no la ve ninguna: lo que no proyecta (CastShadow false,
//    efectos, la cara del avatar...);
//  - las dos de la sonda solo las ve la camara de la sonda.
#include <cstdint>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/vector3.hpp>

// La de lo quieto NO es la 1 (la de todo lo demas por defecto): asi el mapa
// guardado no recoge carteles (un BillboardGui dibujado desde el sol mira al
// sol y da una sombra del tamaño del cartel entero), particulas, rayos, textos
// ni nada que en Roblox no proyecte.
static const uint32_t GL_CAPA_MUNDO      = 1u << 15;
static const uint32_t GL_CAPA_DINAMICA   = 1u << 16;
static const uint32_t GL_CAPA_SIN_SOMBRA = 1u << 17;
static const uint32_t GL_CAPA_SONDA_SOL  = 1u << 18;
static const uint32_t GL_CAPA_SONDA      = 1u << 19;

// ¿Ya pintan las sombras propias? Mientras no, manda la sombra de Godot.
inline bool& gl_sombras_propias_listas() { static bool b = false; return b; }

// Sube cada vez que el Lighting cambia la luz: la sonda vuelve a medir.
inline uint32_t& gl_luz_version() { static uint32_t v = 0; return v; }

// Algo quieto cambio (aparece, se va, cambia de tamaño...): su baldosa del
// mapa guardado se redibuja. Lo recoge GLSombras en su siguiente frame. Se
// apunta SIEMPRE (tambien antes de que las sombras esten listas o con ellas
// apagadas): un aviso perdido deja la sombra de algo que ya no esta. Si hay
// demasiados, se redibuja todo.
struct GLSombraToque { godot::Vector3 centro; float radio; };
inline godot::LocalVector<GLSombraToque>& gl_sombras_toques() {
    static godot::LocalVector<GLSombraToque> v;
    return v;
}
inline bool& gl_sombras_todo_sucio() { static bool b = false; return b; }
inline void gl_sombras_tocar(const godot::Vector3& centro, float radio) {
    godot::LocalVector<GLSombraToque>& v = gl_sombras_toques();
    if (v.size() < 4096) v.push_back({centro, radio});
    else gl_sombras_todo_sucio() = true;
}

// Piezas ancladas que un script esta moviendo (CFrame, PivotTo, un tween):
// mientras se mueven van al mapa de sombra de cada frame; GLSombras las
// devuelve al guardado cuando llevan un rato quietas.
inline godot::HashSet<uint64_t>& gl_sombras_moviles() {
    static godot::HashSet<uint64_t> s;
    return s;
}

#endif // GL_CAPAS_H

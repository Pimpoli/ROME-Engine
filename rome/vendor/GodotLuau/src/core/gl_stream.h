#ifndef GL_STREAM_H
#define GL_STREAM_H

// ════════════════════════════════════════════════════════════════════
//  STREAMING POR DISTANCIA (estilo StreamingEnabled de Roblox)
//
//  Antes esto se hacia recorriendo el arbol ENTERO cada vez que la camara se
//  movia 60 unidades (o cada 5 s): 60.000 nodos de golpe, ~10 ms clavados en un
//  solo frame. Justo mientras el jugador anda, que es cuando mas se nota.
//
//  Ahora cada pieza y cada emisor se apunta en una lista plana al entrar al
//  arbol, y el Workspace repasa solo UN TROZO por frame. El trabajo por frame es
//  constante y pequeño, no depende del tamaño del mapa, y las cosas aparecen y
//  desaparecen "de a poco" en vez de todas a la vez.
//
//  La lista NO se compacta al salir del arbol (seria O(n) por baja, y una tanda
//  de brainrots da cientos de bajas de golpe). Se limpia sola: cuando el barrido
//  llega a una entrada muerta la saca con swap-and-pop, que es O(1).
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/hash_map.hpp>

using namespace godot;

inline LocalVector<uint64_t>& gl_stream_lista() {
    static LocalVector<uint64_t> v;
    return v;
}

// Quien ya esta apuntado. Va aparte de la lista (y no como un bool dentro de
// cada clase) para que dar de alta no obligue a conocer el tipo: asi una Part y
// un emisor de particulas usan el mismo camino sin que un fichero tenga que
// incluir al otro.
inline HashMap<uint64_t, bool>& gl_stream_dentro() {
    static HashMap<uint64_t, bool> m;
    return m;
}

// Alta idempotente: re-entrar al arbol no duplica la entrada.
inline void gl_stream_alta(Object* o) {
    if (!o) return;
    const uint64_t id = (uint64_t)o->get_instance_id();
    if (gl_stream_dentro().has(id)) return;
    gl_stream_dentro()[id] = true;
    gl_stream_lista().push_back(id);
}

// La llama el barrido cuando saca una entrada muerta o fuera del arbol.
inline void gl_stream_olvidar(uint64_t id) { gl_stream_dentro().erase(id); }

// ── ¿se esta montando el place por trozos? ────────────────────────────
// Mientras dure, los scripts NO arrancan. Si arrancan viendo el mundo a medias,
// un FindFirstChild devuelve nil donde el juego da por hecho que hay algo, y
// hay juegos que se quedan colgados esperando lo que todavia no existe (probado:
// el place de prueba se quedaba muerto). Roblox hace lo mismo — entrega el
// DataModel completo y entonces corre los scripts.
inline bool& gl_montando_mundo() {
    static bool m = false;
    return m;
}

#endif // GL_STREAM_H

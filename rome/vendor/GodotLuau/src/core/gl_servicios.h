#ifndef GL_SERVICIOS_H
#define GL_SERVICIOS_H

// ════════════════════════════════════════════════════════════════════
//  BUSCAR UN SERVICIO SIN RECORRER EL MUNDO
//
//  Por todo el motor habia el mismo patron: para dar con un servicio
//  (Players, NetworkService, Teams...) se recorria el ARBOL ENTERO llamando
//  is_class() en cada nodo. En un place importado son ~60.000 nodos y los
//  servicios estan casi al final del recorrido.
//
//  Medido con un micro-banco DENTRO del juego (2026-08-09):
//      Players:GetPlayers() .............. 32.903 us   <- 33 MILISEGUNDOS
//      FindFirstChildOfClass(Humanoid) ....... 9,65 us
//      hrp.Position .......................... 0,68 us
//
//  Una sola llamada a GetPlayers se comia el frame entero. Y el place de
//  prueba la llama dentro de un Heartbeat, una vez por NPC: ahi estaban los
//  30 FPS clavados que ningun A/B grafico explicaba.
//
//  Aqui el servicio se APUNTA la primera vez y despues es una busqueda en
//  tabla. Se guarda el ObjectID y no el puntero: si el servicio muere (cambio
//  de escena, place recargado), ObjectDB devuelve null y se vuelve a buscar,
//  en vez de quedarse con memoria liberada.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

// (static local en funcion, NO global: ver el gotcha del orden de
// inicializacion en gl_errors.h)
inline HashMap<String, ObjectID>& gl_serv_cache() {
    static HashMap<String, ObjectID> c;
    return c;
}

// Los servicios cuelgan del DataModel (raiz > juego > servicio): con 4 niveles
// sobra, y uno que no existe ya no recorre la escena entera (~40.000 nodos).
inline Node* gl_serv_buscar_a_pelo(Node* n, const char* cls, int prof = 0) {
    if (!n || prof > 4) return nullptr;
    if (n->is_class(cls)) return n;
    for (int i = 0; i < n->get_child_count(); i++)
        if (Node* r = gl_serv_buscar_a_pelo(n->get_child(i), cls, prof + 1)) return r;
    return nullptr;
}

// Los que no estaban, con el frame en que se busco: un servicio que no existe
// (NetworkService jugando solo) se volvia a buscar por TODO el arbol en cada
// llamada. Ahora, como mucho una vez por frame, por si alguien lo crea luego.
inline HashMap<String, uint64_t>& gl_serv_ausentes() {
    static HashMap<String, uint64_t> c;
    return c;
}

// El servicio de esa clase, o null. `desde` es cualquier nodo del arbol.
inline Node* gl_servicio(Node* desde, const char* cls) {
    if (!desde || !desde->is_inside_tree()) return nullptr;
    const String clave = String(cls);
    HashMap<String, ObjectID>& cache = gl_serv_cache();
    if (ObjectID* id = cache.getptr(clave)) {
        if (Object* o = ObjectDB::get_instance(*id)) {
            Node* n = gl_cast<Node>(o);
            if (n && n->is_inside_tree()) return n;
        }
    }
    const uint64_t frame = Engine::get_singleton()->get_process_frames();
    if (const uint64_t* f = gl_serv_ausentes().getptr(clave))
        if (*f == frame) return nullptr;
    SceneTree* st = desde->get_tree();
    Node* encontrado = gl_serv_buscar_a_pelo(st ? (Node*)st->get_root() : nullptr, cls);
    cache[clave] = encontrado ? encontrado->get_instance_id() : ObjectID();
    if (encontrado) gl_serv_ausentes().erase(clave);
    else gl_serv_ausentes()[clave] = frame;
    return encontrado;
}

// Al cerrar, o al cambiar de mundo: el cache guarda ObjectID, no referencias,
// asi que no retiene nada; se vacia por limpieza y para que un place nuevo no
// herede los apuntes del anterior.
inline void gl_serv_olvidar_todo() {
    gl_serv_cache().clear();
    gl_serv_ausentes().clear();
}

// ── ¿ESTO ESTA EN EL MUNDO? ──────────────────────────────────────────
// En Roblox SOLO se dibuja lo que cuelga de Workspace. Lo que hay en
// ReplicatedStorage, ServerStorage, StarterGui... son PLANTILLAS: existen, los
// scripts las clonan, pero no se ven.
//
// Aqui no basta con apagar el nodo del servicio: ReplicatedStorage y compañia
// derivan de Node (no de Node3D), y la visibilidad de Godot solo se hereda
// entre Node3D. Un Node pelado en medio ROMPE la cadena, asi que cada cosa que
// se dibuje tiene que preguntarlo.
//
// Se decide por el NOMBRE del contenedor, que es el que Roblox garantiza.
inline bool gl_en_workspace(const Node* n) {
    for (const Node* p = n ? n->get_parent() : nullptr; p; p = p->get_parent()) {
        const StringName nm = p->get_name();
        // Construidas una vez, no en cada llamada (ver gl_runtime.h).
        static const StringName SN_WS("Workspace"), SN_RS("ReplicatedStorage"),
            SN_SS("ServerStorage"), SN_RF("ReplicatedFirst"), SN_SG("StarterGui"),
            SN_SP("StarterPack"), SN_SPL("StarterPlayer"), SN_SSS("ServerScriptService");
        if (nm == SN_WS) return true;
        if (nm == SN_RS || nm == SN_SS || nm == SN_RF || nm == SN_SG ||
            nm == SN_SP || nm == SN_SPL || nm == SN_SSS)
            return false;
    }
    return false;   // sin Workspace por encima: no es del mundo
}

// ── PURGA DE LOS CACHES: LA FUGA QUE TIRABA EL JUEGO ─────────────────
// Estos caches NO tenian tope ni desalojo. Cada tamaño distinto de pieza deja
// dentro una malla y una forma PARA SIEMPRE: los brainrots nacen con tamaños
// variados, mueren... y su malla se queda. Medido en el place con los nodos ya
// estables (oscilando entre 67.000 y 69.500, o sea SIN fuga de nodos):
//     RAM  766 -> 895 MB      VRAM 647,7 -> 672,7 MB   en 100 s
// Eso son ~15 MB/min. En una hora, cerca de 1 GB en una grafica integrada que
// comparte memoria con el sistema: el juego acababa con
// VK_ERROR_OUT_OF_HOST_MEMORY, miles de "Can't create buffer of size: 192" y
// un SIGSEGV. Y de paso un log de 77 GB, que es lo que llenaba el disco.
//
// LA REGLA PARA DIAGNOSTICARLO: si la memoria sube con el numero de nodos
// PLANO, no son objetos — son recursos que nadie libera.
//
// El desalojo es seguro y no hace falta LRU: un `Ref` guardado aqui es UNA
// REFERENCIA MAS. Si la cuenta es 1, el unico que tiene ese recurso es el
// propio cache y tirarlo no puede dejar a ninguna pieza sin malla; si alguna lo
// esta usando, la cuenta es mayor y se queda. El refcount de Godot ya sabe
// quien lo usa, asi que no hay que llevar la cuenta a mano ni arriesgarse a
// desalojar algo vivo.
//
// Se barre solo al pasar del tope, asi que en un mapa normal (donde el numero
// de variantes se estabiliza) no se ejecuta nunca.
template <typename K, typename T>
inline void gl_purgar_cache(HashMap<K, Ref<T>>& c, int tope) {
    if ((int)c.size() <= tope) return;
    LocalVector<K> sobran;
    for (const KeyValue<K, Ref<T>>& kv : c)
        if (kv.value.is_valid() && kv.value->get_reference_count() <= 1)
            sobran.push_back(kv.key);
    for (uint32_t i = 0; i < sobran.size(); i++) c.erase(sobran[i]);
}

#endif // GL_SERVICIOS_H

#ifndef GL_REPLICA_H
#define GL_REPLICA_H
#include "gl_cast.h"

// ════════════════════════════════════════════════════════════════════
//  Replicación automática (1.14.5) — lado SERVIDOR + aplicación en cliente
//  Engancha en godot_object_newindex (propiedad / Parent), method_destroy,
//  SetAttribute y CollectionService para difundir deltas del mundo a los
//  clientes (NetworkService::rep_* en roblox_network.h). Coste cero fuera de un
//  servidor de red: la guarda gl_net_role()==1 corta al instante en
//  single-player/cliente.
//
//  Se incluye desde luau_api.h, en mitad del archivo: usa
//  godot_object_newindex_impl, wrap_node y gl_net_decode, definidos antes.
//
//  F5 de la v1.17 añadió:
//   · atributos (SetAttribute) y etiquetas (CollectionService), con su propio
//     mensaje y dentro del snapshot de cada instancia nueva;
//   · el subárbol de Players: leaderstats y lo que cuelgue del Player, la
//     Backpack (a todos) y la PlayerGui (solo a su dueño). PlayerScripts y
//     StarterGear no salen del servidor, como en Roblox;
//   · Lighting: sus propiedades y lo que cuelgue de él.
// ════════════════════════════════════════════════════════════════════
// GL_REP_SHADOW vive en gl_runtime.h: lo comparten este hook y el NetworkService.

inline NetworkService* gl_rep_service() {
    if (gl_net_role() != 1) return nullptr;
    return gl_cast<NetworkService>(ObjectDB::get_instance(gl_net_service_id()));
}

// ¿el nodo cuelga de algo que se replica? Sube por los padres comparando por
// NOMBRE: los contenedores virtuales pueden ser Node genérico, así que is_class
// no siempre basta.
inline bool gl_rep_is_replicable(Node* n) {
    Node* hijo = n;
    for (Node* p = n ? n->get_parent() : nullptr; p; hijo = p, p = p->get_parent()) {
        StringName nm = p->get_name();
        static const StringName SN_SS("ServerStorage"), SN_SSS("ServerScriptService");
        if (nm == SN_SS || nm == SN_SSS) return false;
        // Teams (1.14.10): en Roblox los equipos se replican, así que un Team
        // creado por el servidor tiene que aparecer solo en los clientes.
        static const StringName SN_WS("Workspace"), SN_RS("ReplicatedStorage"), SN_T("Teams"), SN_LI("Lighting");
        if (nm == SN_WS || nm == SN_RS || nm == SN_T || nm == SN_LI) return true;
        if (p->is_class("RobloxWorkspace") || p->is_class("RobloxWorkspace2D")) return true;
        // Lo que cuelga de un Player (F5): leaderstats, Backpack, PlayerGui...
        // salvo PlayerScripts y StarterGear, que no salen del servidor.
        if (gl_cast<PlayerObject>(p)) {
            static const StringName SN_PS("PlayerScripts"), SN_SG("StarterGear");
            const StringName hn = hijo->get_name();
            return hijo == n || (hn != SN_PS && hn != SN_SG);
        }
    }
    return false;
}

// Ref del padre para el protocolo: "#netid" si el padre ya está replicado, o su
// ruta absoluta si es un contenedor (Workspace/ReplicatedStorage ya existen en
// cada ventana por el place cargado). El Player y sus contenedores tienen netid
// propio (gl_player_netid), así que salen siempre por "#".
inline String gl_rep_parent_ref(Node* parent) {
    if (parent && parent->has_meta("_gl_netid"))
        return String("#") + String::num_int64((int64_t)parent->get_meta("_gl_netid"));
    if (parent) return String(parent->get_path());
    return String();
}

// Replica el nodo y su subárbol como creaciones nuevas (asigna netId + snapshot
// vía NetworkService::rep_build_props, que resuelve el transform global).
inline void gl_rep_replicate_subtree(NetworkService* ns, Node* n, int destino) {
    if (!ns || !n || n->has_meta("_gl_netid")) return;
    int64_t id = ns->rep_next_id();
    n->set_meta("_gl_netid", id);
    if (destino != 0) n->set_meta("_gl_netpeer", destino);
    else if (n->has_meta("_gl_netpeer")) n->remove_meta("_gl_netpeer");
    ns->rep_map(id, n);
    ns->rep_send_new(id, n->get_class(), String(n->get_name()), gl_rep_parent_ref(n->get_parent()), ns->rep_build_props(n), destino);
    for (int i = 0; i < n->get_child_count(); i++)
        gl_rep_replicate_subtree(ns, n->get_child(i), destino);
}
inline void gl_rep_replicate_subtree(NetworkService* ns, Node* n) {
    gl_rep_replicate_subtree(ns, n, ns ? ns->rep_destino_de(n) : 0);
}

// Hook tras aplicar una escritura. Pila: [1]=obj [2]=key [3]=valor.
inline void gl_rep_on_newindex(lua_State* L) {
    NetworkService* ns = gl_rep_service();
    if (!ns) return;
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    Node* n = w ? gow_node(w) : nullptr;
    if (!n || !lua_isstring(L, 2)) return;
    String key = String(lua_tostring(L, 2));
    if (key == "Parent") {
        if (gl_rep_is_replicable(n)) {
            const int destino = ns->rep_destino_de(n);
            if (!n->has_meta("_gl_netid")) {
                gl_rep_replicate_subtree(ns, n, destino);   // entra al árbol replicado → crear
            } else if (gl_rep_destino(n) == destino) {
                ns->rep_send_reparent((int64_t)n->get_meta("_gl_netid"), gl_rep_parent_ref(n->get_parent()), destino);   // ya replicado → mover
            } else {
                // Cambia quién lo ve (p.ej. de ReplicatedStorage a una PlayerGui):
                // se quita donde estaba y se crea donde va.
                ns->rep_unreplicate(n);
                gl_rep_replicate_subtree(ns, n, destino);
            }
        } else if (n->has_meta("_gl_netid")) {
            ns->rep_unreplicate(n);   // salió del árbol replicado → clientes destruyen su copia + olvida el subárbol (el nodo del servidor sigue vivo)
        }
        return;
    }
    // Las propiedades del Player no se replican (Character, Team... tienen su
    // propio camino); sus atributos sí, por gl_rep_on_attr.
    if (gl_cast<PlayerObject>(n)) return;
    // El valor (idx 3) puede ser userdata (Vector3/Color3) o tabla tipada (CFrame,
    // UDim2…): gl_net_encode captura ambos como Variant transmisible.
    Variant enc = gl_net_encode(L, 3);
    n->set_meta(StringName(String(GL_REP_SHADOW) + key), enc);   // shadow para el snapshot al parentar
    if (n->has_meta("_gl_netid"))
        ns->rep_send_prop((int64_t)n->get_meta("_gl_netid"), key, enc, gl_rep_destino(n));
}

// SetAttribute en el servidor (F5). Lo que aún no está replicado no manda nada:
// su snapshot llevará todos sus atributos cuando lo esté.
inline void gl_rep_on_attr(Node* n, const String& key) {
    NetworkService* ns = gl_rep_service();
    if (!ns || !n || !n->has_meta("_gl_netid")) return;
    const int64_t id = (int64_t)n->get_meta("_gl_netid");
    const Variant v = n->has_meta(key) ? n->get_meta(key) : Variant();
    if (gl_is_static_netid((uint64_t)id)) {
        // Nodos del place, Lighting y Players: el que entra tarde recibe lo que
        // cambió (el shadow). nil = se quitó.
        Dictionary d = n->has_meta("_glrep___glattrs") ? (Dictionary)n->get_meta("_glrep___glattrs") : Dictionary();
        d[key] = v;
        n->set_meta("_glrep___glattrs", d);
    }
    ns->rep_send_attr(id, key, v, gl_rep_destino(n));
}

// AddTag/RemoveTag en el servidor (F5), desde CollectionService.
inline void gl_rep_on_tag(Node* n, const String& tag, bool puesta) {
    NetworkService* ns = gl_rep_service();
    if (!ns || !n || !n->has_meta("_gl_netid")) return;
    const int64_t id = (int64_t)n->get_meta("_gl_netid");
    if (gl_is_static_netid((uint64_t)id))
        n->set_meta("_glrep___gltags", n->has_meta("__rbx_tags") ? n->get_meta("__rbx_tags") : Variant(PackedStringArray()));
    ns->rep_send_tag(id, tag, puesta, gl_rep_destino(n));
}

// Aplica una propiedad replicada en el CLIENTE reentrando al __newindex real.
// Instalado en gl_apply_prop_hook(). Hilo Luau fresco: el impl lee índices 1/2/3.
inline void gl_rep_apply_prop(Node* target, const String& key, const Variant& enc) {
    if (!target) return;
    lua_State* L = gl_any_state();
    if (!L) {
        // Sin NINGUNA VM Luau viva (place sin LocalScripts) la réplica se perdía
        // en silencio (huecos #6/#9 de 1.14.5). Los tipos simples se pueden
        // aplicar directo al objeto sin pasar por Luau; los compuestos (CFrame,
        // Instances…) llegan como Dictionary y sí necesitan el decodificador.
        switch (enc.get_type()) {
            case Variant::BOOL: case Variant::INT: case Variant::FLOAT:
            case Variant::STRING: case Variant::VECTOR3: case Variant::COLOR:
                target->set(StringName(key), enc);
                break;
            default: break;
        }
        return;
    }
    lua_State* th = lua_newthread(L);
    wrap_node(th, target);                          // idx 1
    lua_pushstring(th, key.utf8().get_data());      // idx 2
    gl_net_decode(th, enc, target);                 // idx 3
    godot_object_newindex_impl(th);                 // aplica SIN volver a replicar
    lua_pop(L, 1);                                  // quita el hilo
    // Lo cambio la red: en Roblox eso tambien dispara Changed en el cliente.
    gl_fire_prop_changed(target, key);
}

#endif // GL_REPLICA_H

#ifndef ROBLOX_NETWORK_REPLICA_H
#define ROBLOX_NETWORK_REPLICA_H
#include "gl_cast.h"

// Metodos de NetworkService: replicacion. netId de los nodos del place,
// dueno de la fisica (Network Ownership) e instancias, propiedades,
// atributos y etiquetas que el servidor manda a los clientes.
// Va aparte de roblox_network.h (pasaba de 2.000 lineas) y se incluye
// desde alli, justo despues de la clase.

// ── netId de los nodos del PLACE (1.14.10) ───────────────────────────
// Hasta ahora solo lo creado en RUNTIME tenía netId, así que una parte
// puesta en el EDITOR no sincronizaba nada: ni su física ni sus props.
// Aquí se les sella un id determinista por ruta (gl_static_netid), igual en
// todas las ventanas porque todas cargan el mismo place. Con eso entran en
// net_instances y _phys_tick/_phys_auto_tick las recogen sin más cambios.
// Idempotente: se puede re-llamar (p.ej. al reconectar).
inline void NetworkService::_stamp_subtree(Node* n, int depth, int& count) {
    if (!n || depth > 64) return;
    // Solo lo que viene en el place: Godot le pone owner a los nodos
    // guardados en la escena. Lo creado en runtime (personaje local,
    // muñecos, lo que hagan los scripts) tiene owner nulo y ya recibe su
    // netId por el camino normal — sellarlo sería un desastre: cada ventana
    // mapearía el mismo id a SU propio personaje local.
    bool from_place = (n->get_owner() != nullptr);
    bool has_runtime_id = n->has_meta("_gl_netid")
                       && !gl_is_static_netid((uint64_t)(int64_t)n->get_meta("_gl_netid"));
    if (from_place && !has_runtime_id) {
        uint64_t id = gl_static_netid(String(n->get_path()));
        // Colisión de hash (astronómicamente improbable): perturbar de forma
        // DETERMINISTA, para que todas las ventanas lleguen al mismo id.
        for (int guard = 1; guard <= 16; guard++) {
            auto it = net_instances.find(id);
            if (it == net_instances.end() || it->second == (uint64_t)n->get_instance_id()) break;
            id = gl_static_netid(String(n->get_path()) + String("#") + String::num_int64(guard));
        }
        n->set_meta("_gl_netid", (int64_t)id);
        net_instances[id] = (uint64_t)n->get_instance_id();
        count++;
    }
    for (int i = 0; i < n->get_child_count(); i++) _stamp_subtree(n->get_child(i), depth + 1, count);
}

// Marca lo que viene en el PLACE (1.14.13). Se recorre TODO el árbol, no solo
// lo replicable: las plantillas que se clonan suelen vivir en
// ReplicatedStorage o ServerStorage. Los :Clone() heredan la marca (duplicate
// copia las metas), y es lo que permite saber que hay que mandar su estado
// REAL en vez de solo el shadow — sin cobrarle ese coste a las instancias
// creadas por script, que sí tienen shadow y pueden ser miles (mundo voxel).
inline void NetworkService::_mark_place_subtree(Node* n, int depth) {
    if (!n || depth > 64) return;
    if (n->get_owner() != nullptr) n->set_meta("_gl_from_place", true);
    for (int i = 0; i < n->get_child_count(); i++) _mark_place_subtree(n->get_child(i), depth + 1);
}

inline void NetworkService::_stamp_static_ids() {
    if (static_ids_done || !is_inside_tree()) return;
    // El place se esta montando por trozos: sellarlo ahora dejaria fuera todo
    // lo que aun no ha entrado (se vio: "registrado para replicacion: 5
    // nodos"). Se reintenta el proximo frame.
    if (gl_montando_mundo()) return;
    Node* root = (Node*)get_tree()->get_root();
    _mark_place_subtree(root);
    Node* ws = _find_by_class(root, "RobloxWorkspace");
    if (!ws) ws = _find_by_class(root, "RobloxWorkspace2D");
    Node* rs = _find_by_name(root, "ReplicatedStorage");
    Node* tm = _find_by_name(root, "Teams");   // los Team del editor también (1.14.10)
    if (!ws && !rs) return;   // árbol aún sin montar: reintentar el próximo frame
    // Los contenedores en sí NO se sellan (ya se referencian por ruta), solo
    // lo que cuelga de ellos.
    int count = 0;
    if (ws) for (int i = 0; i < ws->get_child_count(); i++) _stamp_subtree(ws->get_child(i), 0, count);
    if (rs) for (int i = 0; i < rs->get_child_count(); i++) _stamp_subtree(rs->get_child(i), 0, count);
    if (tm) for (int i = 0; i < tm->get_child_count(); i++) _stamp_subtree(tm->get_child(i), 0, count);
    // Lighting (F5): el servicio entero se replica, el y lo que cuelga de el.
    if (Node* li = gl_servicio(this, "Lighting")) {
        _sellar(li, gl_static_netid(String("@Lighting")));
        for (int i = 0; i < li->get_child_count(); i++) _stamp_subtree(li->get_child(i), 0, count);
    }
    static_ids_done = true;
    // Se registra el número: si saliera 0 con un place lleno, querría decir
    // que el filtro por owner no reconoce estos nodos, y el fallo sería mudo.
    gl_mp_log(String("place registrado para replicacion: ") + String::num_int64(count) + String(" nodos"));
}

inline void NetworkService::rep_map(int64_t id, Object* node) {
    Node* n = gl_cast<Node>(node);
    if (n) net_instances[(uint64_t)id] = (uint64_t)n->get_instance_id();
}

inline Node* NetworkService::_rep_node(uint64_t id) {
    auto it = net_instances.find(id);
    if (it == net_instances.end()) return nullptr;
    return gl_cast<Node>(ObjectDB::get_instance(it->second));
}

// Como _rep_node, pero un id de Player (gl_player_netid) que aun no se
// conoce crea ese Player: el leaderstats de alguien puede llegar antes que
// su personaje. Solo desde los receptores de red, nunca recorriendo
// net_instances (lo modifica).
inline Node* NetworkService::_rep_node_red(uint64_t id) {
    Node* n = _rep_node(id);
    if (n || !gl_is_player_netid(id) || mode == 0) return n;
    const int peer = (int)((id >> 3) & 0xFFFFFFFFULL);
    PlayerObject* po = (peer == get_peer_id()) ? _local_player_obj() : _ensure_peer_player(peer);
    if (!po) return nullptr;
    _sellar_jugador(po, peer);
    return _rep_node(id);
}

inline void NetworkService::_sellar(Node* n, uint64_t id) {
    if (!n) return;
    if (n->has_meta("_gl_netid")) {
        const uint64_t viejo = (uint64_t)(int64_t)n->get_meta("_gl_netid");
        if (viejo == id && net_instances.count(id)) return;
        if (viejo != id) net_instances.erase(viejo);
    }
    n->set_meta("_gl_netid", (int64_t)id);
    net_instances[id] = (uint64_t)n->get_instance_id();
}

// El Player de `peer` y sus contenedores, con el mismo id en todas las
// ventanas (F5): asi sus hijos y sus atributos viajan como los de cualquier
// instancia replicada.
inline void NetworkService::_sellar_jugador(PlayerObject* po, int peer) {
    if (!po || peer <= 0 || mode == 0) return;
    _sellar(po, gl_player_netid(peer, 0));
    for (int c = 1; c <= 4; c++)
        _sellar(po->get_node_or_null(NodePath(gl_player_contenedor(c))), gl_player_netid(peer, c));
}

inline Node* NetworkService::_rep_resolve_parent(const String& ref) {
    if (ref.begins_with("#")) return _rep_node_red((uint64_t)ref.substr(1).to_int());
    if (!is_inside_tree()) return nullptr;
    return get_node_or_null(NodePath(ref));   // ruta absoluta (mismo place en cada ventana)
}

// ── Física en red / Network Ownership (1.14.7) ───────────────────────
// ¿Esta máquina simula esta parte? owner 0 = servidor; owner N = ese peer.
inline bool NetworkService::_phys_authority(Node* part) {
    int owner = part->has_meta("_gl_owner") ? (int)part->get_meta("_gl_owner") : 0;
    if (owner == 0) return is_server();
    return owner == get_peer_id();
}

inline bool NetworkService::net_is_owner(int64_t netId) {
    Node* n = _rep_node((uint64_t)netId);
    return n ? _phys_authority(n) : true;
}

// Fijar dueño (SetNetworkOwner/Auto, solo servidor): aplicar local + difundir.
inline void NetworkService::net_set_owner(int64_t netId, int owner_peer, bool is_auto) {
    _apply_owner((uint64_t)netId, owner_peer, is_auto);
    rpc("_phys_owner", netId, owner_peer, is_auto);
}

// La red solo deshace SUS congelaciones, que van en modo KINEMATIC. Las
// demas van en STATIC y tienen otro dueño: Anchored, miembro de un
// conjunto, raiz de un conjunto anclado, el LOD. Soltarlas todas tiraba al
// suelo, cada 0,05 s, cualquier pieza soldada a una anclada.
inline void NetworkService::_red_soltar(RobloxPart* part) {
    if (!part->is_freeze_enabled() || part->get_freeze_mode() != RigidBody3D::FREEZE_MODE_KINEMATIC) return;
    part->set_freeze_enabled(false);
    part->set_freeze_mode(RigidBody3D::FREEZE_MODE_STATIC);
}

inline void NetworkService::_red_congelar(RobloxPart* part) {
    if (part->is_freeze_enabled()) return;   // ya la congelo otro: no se le cambia el modo
    part->set_freeze_mode(RigidBody3D::FREEZE_MODE_KINEMATIC);
    part->set_freeze_enabled(true);
}

inline void NetworkService::_apply_owner(uint64_t netId, int owner_peer, bool is_auto) {
    RobloxPart* part = gl_cast<RobloxPart>(_rep_node(netId));
    if (!part) return;
    part->set_meta("_gl_owner", owner_peer);
    part->set_meta("_gl_owner_auto", is_auto);
    if (_phys_authority(part)) {
        _red_soltar(part);                                 // yo simulo
        phys_targets.erase(netId);
    } else if (!part->get_anchored()) {
        _red_congelar(part);                               // para no pelear con el stream
    }
}

inline void NetworkService::_phys_owner(int64_t netId, int owner_peer, bool is_auto) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo el servidor
    _apply_owner((uint64_t)netId, owner_peer, is_auto);
}

inline void NetworkService::_phys_apply_remote(RobloxPart* part, uint64_t netId,
    const Transform3D& t, const Vector3& lv, const Vector3& av) {
    _red_congelar(part);
    PhysTarget& pt = phys_targets[netId];
    pt.xform = t; pt.lvel = lv; pt.avel = av; pt.has = true;
    pt.age = 0.0;   // llegó estado fresco: reiniciar la ventana de extrapolación
}

inline void NetworkService::_phys_state(int64_t netId, Transform3D t, Vector3 lv, Vector3 av) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // autoridad-relay = servidor
    RobloxPart* part = gl_cast<RobloxPart>(_rep_node((uint64_t)netId));
    if (!part || _phys_authority(part)) return;
    _phys_apply_remote(part, (uint64_t)netId, t, lv, av);
}

inline void NetworkService::_phys_owned(int64_t netId, Transform3D t, Vector3 lv, Vector3 av) {
    if (!is_server()) return;
    int sender = get_multiplayer()->get_remote_sender_id();
    RobloxPart* part = gl_cast<RobloxPart>(_rep_node((uint64_t)netId));
    if (!part) return;
    int owner = part->has_meta("_gl_owner") ? (int)part->get_meta("_gl_owner") : 0;
    if (owner != sender) return;   // solo el dueño real puede mandar su estado
    _phys_apply_remote(part, (uint64_t)netId, t, lv, av);   // el server tambien lo muestra
    // Relay a los demás, con el mismo culling por distancia (1.14.14). Al
    // dueño no se le devuelve: él es la autoridad de esa parte.
    _phys_send_culled((uint64_t)netId, t, lv, av, _peer_positions(), sender);
}

inline auto NetworkService::_peer_positions() -> std::vector<PeerPos> {
    std::vector<PeerPos> out;
    TypedArray<Node> roster = get_players_roster();
    for (int i = 0; i < roster.size(); i++) {
        PlayerObject* po = gl_cast<PlayerObject>(roster[i]);
        if (!po) continue;
        if (Node3D* ch = gl_cast<Node3D>(po->get_character()))
            out.push_back({ peer_for_player_node(po), gl_pos(ch) });
    }
    return out;
}

// Culling de ancho de banda (1.14.14): el estado de una parte solo se manda a
// quien la tiene CERCA. Antes iba a todos por broadcast: con muchas partes es
// ancho de banda tirado, porque nadie ve caer una caja a 300 studs.
// Si de un peer no se conoce su posición se le manda igual: mejor gastar de
// más que dejarle las partes congeladas sin que nada avise.
inline void NetworkService::_phys_send_culled(uint64_t netId, const Transform3D& t,
    const Vector3& lv, const Vector3& av, const std::vector<PeerPos>& plrs, int skip_peer) {
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_null() || !m->has_multiplayer_peer()) return;
    const float RANGE2 = 250.0f * 250.0f;
    PackedInt32Array peers = m->get_peers();
    for (int i = 0; i < peers.size(); i++) {
        int pid = peers[i];
        if (pid == skip_peer) continue;
        bool found = false, near_it = false;
        for (auto& pp : plrs) {
            if (pp.peer != pid) continue;
            found = true;
            near_it = pp.pos.distance_squared_to(t.origin) <= RANGE2;
            break;
        }
        if (!found || near_it) rpc_id(pid, "_phys_state", (int64_t)netId, t, lv, av);
    }
}

inline void NetworkService::_phys_tick() {
    std::vector<PeerPos> plrs = is_server() ? _peer_positions() : std::vector<PeerPos>();
    for (auto& kv : net_instances) {
        RobloxPart* part = gl_cast<RobloxPart>(_rep_node(kv.first));
        if (!part || part->get_anchored()) continue;
        if (_phys_authority(part)) {
            _red_soltar(part);
            phys_targets.erase(kv.first);
            // Congelada por otro (miembro de un conjunto, LOD): no la simula
            // nadie, no hay estado que mandar.
            if (part->is_sleeping() || part->is_freeze_enabled()) continue;
            Transform3D t = gl_xf(part);
            Vector3 lv = part->get_linear_velocity(), av = part->get_angular_velocity();
            if (is_server()) _phys_send_culled(kv.first, t, lv, av, plrs, 0);
            else             rpc_id(1, "_phys_owned", (int64_t)kv.first, t, lv, av);
        } else {
            _red_congelar(part);
        }
    }
}

inline void NetworkService::_phys_lerp(double dt) {
    if (phys_targets.empty()) return;
    float a = 1.0f - (float)Math::pow(0.0001, dt);
    for (auto it = phys_targets.begin(); it != phys_targets.end(); ++it) {
        if (!it->second.has) continue;
        RobloxPart* part = gl_cast<RobloxPart>(_rep_node(it->first));
        if (!part) continue;
        // Predicción / dead reckoning (1.14.14): entre estado y estado se
        // avanza el objetivo con la velocidad recibida. lvel/avel se guardaban
        // desde la 1.14.7 y NO se usaban: el objeto iba SIEMPRE por detrás de
        // donde de verdad estaba (los ~50ms entre estados + el retraso de la
        // interpolación), y con latencia real eso se nota mucho. Se corta a
        // 0.5s: si el dueño dejó de mandar, extrapolar más lo mandaría a volar.
        PhysTarget& T = it->second;
        T.age += dt;
        if (T.age < 0.5) {
            T.xform.origin += T.lvel * (float)dt;
            float aspeed = T.avel.length();
            if (aspeed > 0.0001f)
                T.xform.basis = Basis(T.avel / aspeed, aspeed * (float)dt) * T.xform.basis;
        }
        Transform3D cur = gl_xf(part);
        const Transform3D& tgt = it->second.xform;
        cur.origin = cur.origin.lerp(tgt.origin, a);
        Quaternion q = cur.basis.get_rotation_quaternion().slerp(tgt.basis.get_rotation_quaternion(), a);
        cur.basis = Basis(q);
        part->set_global_transform(cur);
    }
}

// Auto ownership (como Roblox): el servidor da cada parte no anclada en modo
// Auto al jugador MÁS CERCANO dentro de rango (o al servidor si nadie cerca).
// Con histéresis + throttle (~2Hz) para no oscilar en las fronteras.
inline void NetworkService::_phys_auto_tick() {
    if (!is_server()) return;
    std::vector<PeerPos> plrs = _peer_positions();
    const float RANGE2 = 150.0f * 150.0f;   // fuera de rango → servidor
    for (auto& kv : net_instances) {
        RobloxPart* part = gl_cast<RobloxPart>(_rep_node(kv.first));
        if (!part || part->get_anchored()) continue;
        bool is_auto = !part->has_meta("_gl_owner_auto") || (bool)part->get_meta("_gl_owner_auto");
        if (!is_auto) continue;   // dueño fijado a mano → no tocar
        Vector3 pp = gl_pos(part);
        int best = 0; float best_d = RANGE2;
        for (auto& pl : plrs) { float d = pl.pos.distance_squared_to(pp); if (d < best_d) { best_d = d; best = pl.peer; } }
        int cur = part->has_meta("_gl_owner") ? (int)part->get_meta("_gl_owner") : 0;
        if (best == cur) continue;
        // Histéresis: si ya hay un dueño-jugador, solo cambiar si el nuevo está
        // >25% más cerca (evita flip-flop y spam de RPC en la frontera).
        if (cur != 0 && best != 0) {
            for (auto& pl : plrs) if (pl.peer == cur) {
                if (best_d > pl.pos.distance_squared_to(pp) * 0.75f) best = cur;
                break;
            }
        }
        if (best == cur) continue;
        _apply_owner(kv.first, best, true);
        rpc("_phys_owner", (int64_t)kv.first, best, true);
    }
}

// Snapshot de propiedades a replicar (shadow "_glrep_*"). Para Node3D con
// transform tocada, manda la posición/rotación GLOBAL resuelta en vez del
// valor crudo: el cliente la aplica siempre en-árbol (add_child antes de
// props) → set_global_*, así coincide sin importar el orden de escritura ni
// el offset del ancestro (fix del bug local/global).
// Propiedades REALES del nodo (hueco #5 de 1.14.5). Las props AUTORADAS EN EL
// EDITOR nunca pasan por el newindex, así que no tienen shadow: un :Clone() de
// una plantilla del editor llegaba a los clientes con los valores POR DEFECTO
// (parte gris del tamaño de fábrica). Aquí se leen del objeto.
// Discriminador: el motor registra las props estilo Roblox en PascalCase y las
// internas de Godot van en snake_case.
// Solo tipos que sobreviven el viaje intactos: gl_net_decode convierte
// cualquier otro en nil, y aplicar nil rompería la propiedad.
inline bool NetworkService::_rep_prop_type_ok(const Variant& v) {
    switch (v.get_type()) {
        case Variant::BOOL: case Variant::INT: case Variant::FLOAT:
        case Variant::STRING: case Variant::VECTOR3: case Variant::COLOR:
            return true;
        default: return false;
    }
}

inline void NetworkService::_rep_real_props(Node* n, Dictionary& out) {
    TypedArray<Dictionary> pl = n->get_property_list();
    for (int i = 0; i < pl.size(); i++) {
        Dictionary p = pl[i];
        if (!((int)p.get("usage", 0) & PROPERTY_USAGE_STORAGE)) continue;   // categorías/grupos
        String name = p.get("name", String());
        if (name.is_empty()) continue;
        char32_t c0 = name[0];
        if (c0 < 'A' || c0 > 'Z') continue;   // snake_case → prop interna de Godot
        Variant v = n->get(StringName(name));
        if (!_rep_prop_type_ok(v)) continue;
        out[name] = v;
    }
}

inline Dictionary NetworkService::_rep_props(Node* n, bool include_real) {
    Dictionary props;
    // Solo los nodos venidos del place (y sus clones) necesitan el estado
    // real; lo creado por script ya lo tiene todo en el shadow.
    if (include_real && n->has_meta("_gl_from_place")) _rep_real_props(n, props);   // el shadow manda sobre esto
    TypedArray<StringName> metas = n->get_meta_list();
    Dictionary atributos;
    for (int i = 0; i < metas.size(); i++) {
        String mk = String(metas[i]);
        if (mk.begins_with("_glrep_")) props[mk.substr(7)] = n->get_meta(StringName(mk));
        else if (include_real && gl_meta_es_atributo(mk)) atributos[mk] = n->get_meta(StringName(mk));
    }
    // Instancia nueva: TODOS sus atributos y etiquetas (F5). Un clon de una
    // plantilla trae los suyos del place, que no pasaron por SetAttribute.
    if (include_real) {
        props.erase("__glattrs");
        props.erase("__gltags");
        if (!atributos.is_empty()) props["__glattrs"] = atributos;
        if (n->has_meta("__rbx_tags")) {
            PackedStringArray t = n->get_meta("__rbx_tags");
            if (t.size() > 0) props["__gltags"] = t;
        }
    }
    if (Node3D* n3 = gl_cast<Node3D>(n)) {
        if (props.has("Position") || props.has("Rotation") || props.has("CFrame")) {
            props.erase("Position"); props.erase("Rotation"); props.erase("CFrame");
            props["Position"] = gl_pos(n3);
            props["Rotation"] = n3->get_global_rotation_degrees();
        }
    }
    // Subtipo de Value: gclass es "RobloxValue" para todos; el subtipo real
    // (IntValue/StringValue/…) vive en roblox_class → replicarlo (#10).
    if (RobloxValue* rv = gl_cast<RobloxValue>(n)) props["__glvclass"] = rv->roblox_class;
    return props;
}

// (servidor) difundir creación / propiedad / re-parent / destrucción
// destino: 0 = a todos los clientes; N = solo al peer N (lo de una PlayerGui,
// ver gl_replica.h). Al propio servidor (peer 1) no se le manda nada.
inline bool NetworkService::_rep_a_uno(int destino) {
    if (destino == 1) return false;
    Ref<MultiplayerAPI> m = _mp();
    return m.is_valid() && m->has_multiplayer_peer() && m->get_peers().has(destino);
}

inline void NetworkService::rep_send_new(int64_t id, const String& gclass, const String& name,
    const String& parent_ref, const Dictionary& props, int destino) {
    if (mode != 1) return;
    if (destino == 0) rpc("_rep_new", id, gclass, name, parent_ref, props);
    else if (_rep_a_uno(destino)) rpc_id(destino, "_rep_new", id, gclass, name, parent_ref, props);
}

inline void NetworkService::rep_send_prop(int64_t id, const String& key, const Variant& enc,
    int destino) {
    if (mode != 1) return;
    if (destino == 0) rpc("_rep_prop", id, key, enc);
    else if (_rep_a_uno(destino)) rpc_id(destino, "_rep_prop", id, key, enc);
}

inline void NetworkService::rep_send_reparent(int64_t id, const String& parent_ref, int destino) {
    if (mode != 1) return;
    if (destino == 0) rpc("_rep_reparent", id, parent_ref);
    else if (_rep_a_uno(destino)) rpc_id(destino, "_rep_reparent", id, parent_ref);
}

// Atributos y etiquetas (F5). Un atributo quitado viaja como nil.
inline void NetworkService::rep_send_attr(int64_t id, const String& key, const Variant& v,
    int destino) {
    if (mode != 1) return;
    if (destino == 0) rpc("_rep_attr", id, key, v);
    else if (_rep_a_uno(destino)) rpc_id(destino, "_rep_attr", id, key, v);
}

inline void NetworkService::rep_send_tag(int64_t id, const String& tag, bool puesta, int destino) {
    if (mode != 1) return;
    if (destino == 0) rpc("_rep_tag", id, tag, puesta);
    else if (_rep_a_uno(destino)) rpc_id(destino, "_rep_tag", id, tag, puesta);
}

// Lo de la PlayerGui de un jugador solo lo ve ese jugador; lo demas, todos.
inline int NetworkService::rep_destino_de(Node* n) {
    Node* hijo = n;
    for (Node* p = n ? n->get_parent() : nullptr; p; hijo = p, p = p->get_parent()) {
        PlayerObject* po = gl_cast<PlayerObject>(p);
        if (!po) continue;
        if (hijo != n && hijo == po->get_node_or_null(NodePath("PlayerGui")))
            return po->is_local ? get_peer_id() : po->peer_id;
        return 0;
    }
    return 0;
}

// Olvida el netId de TODO el subárbol (raíz + hijos) y quita sus metas.
inline void NetworkService::_rep_collect_forget(Node* n) {
    if (!n) return;
    if (n->has_meta("_gl_netid")) {
        net_instances.erase((uint64_t)(int64_t)n->get_meta("_gl_netid"));
        n->remove_meta("_gl_netid");
    }
    if (n->has_meta("_gl_netpeer")) n->remove_meta("_gl_netpeer");
    for (int i = 0; i < n->get_child_count(); i++) _rep_collect_forget(n->get_child(i));
}

// (servidor) el subárbol deja de replicarse: los clientes destruyen su copia
// y el servidor olvida los netId. NO libera el nodo del servidor (lo usa tanto
// Destroy —que libera aparte— como mover a ServerStorage —que lo conserva—).
inline void NetworkService::rep_unreplicate(Node* n) {
    if (mode != 1 || !n || !n->has_meta("_gl_netid")) return;
    int64_t root = (int64_t)n->get_meta("_gl_netid");
    _rep_collect_forget(n);
    rpc("_rep_destroy", root);
}

// (servidor) mandar el estado ya replicado a un peer recién conectado. Los
// clientes ya no corren ServerScripts → dependen de esto para ver lo que el
// servidor creó ANTES de que ellos conectaran (late-join).
inline void NetworkService::_rep_sync_peer(int id) {
    // Orden TOPOLÓGICO (padre replicado antes que hijo), no por netId: un nodo
    // re-parentado bajo un padre de netId mayor rompería el orden ascendente y
    // el cliente lo descartaría por padre inexistente. Punto fijo multipasada.
    std::vector<std::pair<uint64_t, Node*>> live;
    std::vector<std::pair<uint64_t, Node*>> statics;
    for (auto& kv : net_instances) {
        Node* n = gl_cast<Node>(ObjectDB::get_instance(kv.second));
        if (!n) continue;
        // Los nodos del place (1.14.10) NO se re-crean: el cliente ya los
        // tiene, cargó el mismo place y les sella el MISMO id. Mandarles un
        // _rep_new sería un diluvio inútil de RPCs en cada join.
        if (gl_is_static_netid(kv.first)) statics.push_back(std::pair<uint64_t, Node*>(kv.first, n));
        else if (gl_rep_destino(n) == 0 || gl_rep_destino(n) == id)   // la PlayerGui de otro, no
            live.push_back(std::pair<uint64_t, Node*>(kv.first, n));
    }
    std::unordered_set<uint64_t> sent;
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto& pr : live) {
            if (sent.count(pr.first)) continue;
            Node* n = pr.second;
            Node* parent = n->get_parent();
            if (parent && parent->has_meta("_gl_netid")) {
                uint64_t pid = (uint64_t)(int64_t)parent->get_meta("_gl_netid");
                // Padre aún no enviado. Uno del place (o un Player) el
                // cliente ya lo tiene: antes se esperaba por él para siempre
                // y lo creado bajo un modelo del place no llegaba nunca.
                if (!gl_is_static_netid(pid) && net_instances.count(pid) && !sent.count(pid)) continue;
            }
            String parent_ref;
            if (parent && parent->has_meta("_gl_netid"))
                parent_ref = String("#") + String::num_int64((int64_t)parent->get_meta("_gl_netid"));
            else if (parent)
                parent_ref = String(parent->get_path());
            rpc_id(id, "_rep_new", (int64_t)pr.first, n->get_class(), String(n->get_name()), parent_ref, rep_build_props(n));
            sent.insert(pr.first);
            progress = true;
        }
    }
    // Nodos del place: solo las props que el servidor CAMBIÓ (el shadow), que
    // es justo lo que el cliente no puede saber por su cuenta. Si el servidor
    // no tocó nada, no se manda nada.
    for (auto& pr : statics) {
        // SOLO el shadow: el cliente ya tiene estos nodos con sus valores del
        // editor. Mandarles el estado completo sería el diluvio que evitó la
        // 1.14.9.2, ahora con props además de con instancias.
        Dictionary p = rep_build_shadow_props(pr.second);
        p.erase("__glvclass");   // el subtipo de Value ya viene en el place
        Array keys = p.keys();
        for (int i = 0; i < keys.size(); i++)
            rpc_id(id, "_rep_prop", (int64_t)pr.first, String(keys[i]), p[keys[i]]);
    }
    // Equipos (1.14.10): el que entra necesita saber en qué equipo está cada
    // uno; los Team ya le llegaron arriba como instancias replicadas.
    TypedArray<Node> roster = get_players_roster();
    for (int i = 0; i < roster.size(); i++) {
        PlayerObject* po = gl_cast<PlayerObject>(roster[i]);
        if (!po || !po->team_id) continue;
        Node* t = gl_cast<Node>(ObjectDB::get_instance(po->team_id));
        if (t && t->has_meta("_gl_netid"))
            rpc_id(id, "_set_team", peer_for_player_node(po), (int64_t)t->get_meta("_gl_netid"));
    }
    // Animaciones en curso (1.14.12): sin esto, una animación en bucle que ya
    // estaba sonando sería invisible para el que acaba de entrar.
    for (auto& kv : peer_anim) {
        if (!kv.second.play || kv.first == id) continue;
        rpc_id(id, "_recv_anim", kv.first, kv.second.id, kv.second.name,
               kv.second.speed, kv.second.looped, true);
    }
}

// (cliente) receptores — solo aceptan del servidor (peer 1)
inline void NetworkService::_rep_new(int64_t id, String gclass, String name, String parent_ref,
    Dictionary props) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;
    if (net_instances.count((uint64_t)id)) return;   // ya existe (idempotente ante re-sync)
    Object* o = ClassDB::instantiate(StringName(gclass));
    Node* n = gl_cast<Node>(o);
    if (!n) { if (o) memdelete(o); return; }
    n->set_name(name);
    n->set_meta("_gl_netid", id);
    net_instances[(uint64_t)id] = (uint64_t)n->get_instance_id();
    Node* parent = _rep_resolve_parent(parent_ref);
    if (!parent) { net_instances.erase((uint64_t)id); memdelete(n); return; }
    _rep_marcas_nuevas(n, props);   // atributos y etiquetas ANTES de entrar al arbol
    parent->add_child(n);
    // Restaurar el subtipo de Value (#10) antes de aplicar props (no es una prop del hook).
    if (RobloxValue* rv = gl_cast<RobloxValue>(n)) {
        if (props.has("__glvclass")) { rv->roblox_class = props["__glvclass"]; props.erase("__glvclass"); }
    }
    if (gl_apply_prop_hook()) {
        Array keys = props.keys();
        for (int i = 0; i < keys.size(); i++) {
            String k = keys[i];
            gl_apply_prop_hook()(n, k, props[k]);
        }
    }
}

inline void NetworkService::_rep_prop(int64_t id, String key, Variant enc) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;
    Node* n = _rep_node_red((uint64_t)id);
    if (!n) return;
    // Late-join de un nodo del place o de un Player: lo que el servidor
    // cambio de sus atributos y sus etiquetas (gl_replica.h).
    if (key == "__glattrs" && enc.get_type() == Variant::DICTIONARY) {
        Dictionary a = enc;
        Array ks = a.keys();
        for (int i = 0; i < ks.size(); i++) _rep_attr_aqui(n, String(ks[i]), a[ks[i]]);
        return;
    }
    if (key == "__gltags") { _rep_tags_aqui(n, enc); return; }
    if (gl_apply_prop_hook()) gl_apply_prop_hook()(n, key, enc);
}

inline void NetworkService::_rep_attr(int64_t id, String key, Variant v) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;
    if (Node* n = _rep_node_red((uint64_t)id)) _rep_attr_aqui(n, key, v);
}

inline void NetworkService::_rep_tag(int64_t id, String tag, bool puesta) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;
    if (Node* n = _rep_node_red((uint64_t)id)) _rep_tag_aqui(n, tag, puesta);
}

// Un snapshot nuevo: atributos y etiquetas como metas, sin avisar (el nodo
// aun no esta en el arbol; CollectionService recoge las etiquetas al entrar).
inline void NetworkService::_rep_marcas_nuevas(Node* n, Dictionary& props) {
    if (props.has("__glattrs")) {
        Dictionary a = props["__glattrs"];
        Array ks = a.keys();
        for (int i = 0; i < ks.size(); i++)
            if (a[ks[i]].get_type() != Variant::NIL) n->set_meta(StringName(String(ks[i])), a[ks[i]]);
        props.erase("__glattrs");
    }
    if (props.has("__gltags")) {
        n->set_meta("__rbx_tags", (PackedStringArray)props["__gltags"]);
        props.erase("__gltags");
    }
}

inline void NetworkService::_rep_attr_aqui(Node* n, const String& k, const Variant& v) {
    const bool habia = n->has_meta(k);
    if (v.get_type() == Variant::NIL) {
        if (!habia) return;
        n->remove_meta(k);
    } else {
        if (habia && n->get_meta(k) == v) return;
        n->set_meta(k, v);
    }
    gl_fire_attribute_changed(n, k);
}

// Por el CollectionService de este mundo, para que GetTagged y sus señales
// lo vean. Si aun no existe, basta la meta: al crearse barre las metas.
inline void NetworkService::_rep_tag_aqui(Node* n, const String& tag, bool puesta) {
    if (Node* cs = gl_servicio(this, "CollectionService")) {
        cs->call(puesta ? "add_tag" : "remove_tag", n, tag);
        return;
    }
    PackedStringArray t = n->has_meta("__rbx_tags") ? (PackedStringArray)n->get_meta("__rbx_tags") : PackedStringArray();
    const int64_t idx = t.find(tag);
    if (puesta && idx < 0) t.push_back(tag);
    if (!puesta && idx >= 0) t.remove_at(idx);
    n->set_meta("__rbx_tags", t);
}

// La lista ENTERA de etiquetas (late-join): quitar las que sobran y poner
// las que faltan.
inline void NetworkService::_rep_tags_aqui(Node* n, const PackedStringArray& quiero) {
    PackedStringArray tengo = n->has_meta("__rbx_tags") ? (PackedStringArray)n->get_meta("__rbx_tags") : PackedStringArray();
    for (int i = 0; i < tengo.size(); i++) if (!quiero.has(tengo[i])) _rep_tag_aqui(n, tengo[i], false);
    for (int i = 0; i < quiero.size(); i++) if (!tengo.has(quiero[i])) _rep_tag_aqui(n, quiero[i], true);
}

inline void NetworkService::_rep_reparent(int64_t id, String parent_ref) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;
    Node* n = _rep_node_red((uint64_t)id);
    if (!n) return;
    Node* parent = _rep_resolve_parent(parent_ref);
    if (!parent) return;
    if (n->get_parent()) n->get_parent()->remove_child(n);
    parent->add_child(n);
}

inline void NetworkService::_rep_destroy(int64_t id) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;
    Node* n = _rep_node((uint64_t)id);
    if (n) { _rep_collect_forget(n); if (n->get_parent()) n->get_parent()->remove_child(n); n->queue_free(); }
    else net_instances.erase((uint64_t)id);
}

#endif // ROBLOX_NETWORK_REPLICA_H

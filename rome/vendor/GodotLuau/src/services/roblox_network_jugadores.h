#ifndef ROBLOX_NETWORK_JUGADORES_H
#define ROBLOX_NETWORK_JUGADORES_H
#include "gl_cast.h"

// Metodos de NetworkService: jugadores y munecos. Buscar nodos y al
// jugador local, el Player de cada peer, dispositivo y ventanas, RPCs,
// los munecos de los otros jugadores y su vida, y el envio del estado local.
// Va aparte de roblox_network.h (pasaba de 2.000 lineas) y se incluye
// desde alli, justo despues de la clase.

inline Node* NetworkService::_find_by_class(Node* n, const char* cls) {
    if (!n) return nullptr;
    if (n->is_class(cls)) return n;
    for (int i = 0; i < n->get_child_count(); i++) {
        Node* r = _find_by_class(n->get_child(i), cls);
        if (r) return r;
    }
    return nullptr;
}

// Los contenedores virtuales (ReplicatedStorage…) pueden ser un Node
// genérico, así que se buscan por NOMBRE y no por clase.
inline Node* NetworkService::_find_by_name(Node* n, const char* nm) {
    if (!n) return nullptr;
    if (n->get_name() == StringName(nm)) return n;
    for (int i = 0; i < n->get_child_count(); i++) {
        Node* r = _find_by_name(n->get_child(i), nm);
        if (r) return r;
    }
    return nullptr;
}

// Mi peer id, o 0 si aún no hay conexión de verdad (1.14.17).
// `has_multiplayer_peer()` sigue diciendo true mientras el peer CONECTA o ya
// se cayó, pero `get_unique_id()` de ENet exige que esté ACTIVO y si no lanza
// "The multiplayer instance isn't currently active" en consola. Se repetía en
// 6 sitios; ahora se pregunta por el estado real de la conexión en uno solo.
inline int NetworkService::_my_peer_id() {
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_null() || !m->has_multiplayer_peer()) return 0;
    Ref<MultiplayerPeer> p = m->get_multiplayer_peer();
    if (p.is_null() || p->get_connection_status() != MultiplayerPeer::CONNECTION_CONNECTED) return 0;
    return m->get_unique_id();
}

// El personaje local. También fija is_2d según lo que exista en el mundo.
inline Node* NetworkService::_local_player() {
    if (!is_inside_tree()) return nullptr;
    Node* root = (Node*)get_tree()->get_root();
    Node* p = _find_by_class(root, "RobloxPlayer");
    if (p) { is_2d = false; return p; }
    p = _find_by_class(root, "RobloxPlayer2D");
    if (p) { is_2d = true; return p; }
    return nullptr;
}

inline Node* NetworkService::_workspace() {
    if (!is_inside_tree()) return nullptr;
    Node* root = (Node*)get_tree()->get_root();
    Node* w = _find_by_class(root, is_2d ? "RobloxWorkspace2D" : "RobloxWorkspace");
    return w ? w : root;
}

// ── Objetos Player (Player ≠ Character) ──────────────────────────────
inline Players* NetworkService::_players_service() {
    // Antes esto recorria el arbol entero en CADA llamada, y lo llama
    // get_players_roster(), o sea Players:GetPlayers(). Ver gl_servicios.h.
    return gl_cast<Players>(gl_servicio(this, "Players"));
}

inline PlayerObject* NetworkService::_local_player_obj() {
    Players* ps = _players_service();
    return ps ? ps->ensure_local_player_obj() : nullptr;
}

inline PlayerObject* NetworkService::_peer_player(int peer) {
    auto it = peer_player_ids.find(peer);
    if (it == peer_player_ids.end()) return nullptr;
    return gl_cast<PlayerObject>(ObjectDB::get_instance(it->second));
}

// Crea (si falta) el objeto Player de un peer remoto, hijo del servicio Players.
inline PlayerObject* NetworkService::_ensure_peer_player(int peer) {
    PlayerObject* po = _peer_player(peer);
    if (po) return po;
    Players* ps = _players_service();
    if (!ps) return nullptr;
    po = memnew(PlayerObject);
    po->peer_id = peer;
    // UserId = el secuencial asignado por el servidor (host=1, 2,3,...). Si aún
    // no llegó la asignación (carrera), cae al peerId como provisional.
    auto uit = peer_seq_uid.find(peer);
    po->user_id = (uit != peer_seq_uid.end()) ? uit->second : (int64_t)peer;
    String pname = String("Player_") + String::num_int64(peer);
    po->set_name(pname);
    po->display_name = pname;
    ps->add_child(po);
    ps->ensure_player_containers(po);   // PlayerGui/Backpack/PlayerScripts (1.14.10)
    peer_player_ids[peer] = (uint64_t)po->get_instance_id();
    _sellar_jugador(po, peer);          // sus hijos y atributos viajan (F5)
    return po;
}

// (cliente) el servidor (peer 1) comunica el UserId secuencial de un peer.
// Mantiene todas las ventanas con el mismo UserId por jugador y fija el del
// jugador LOCAL cuando el peer soy yo.
inline void NetworkService::_gl_peer_uid(int peer, int uid) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo el servidor asigna
    peer_seq_uid[peer] = (int64_t)uid;
    int myid = _my_peer_id();
    if (peer == myid) {
        gl_local_user_id() = (int64_t)uid;
        if (PlayerObject* lp = _local_player_obj()) lp->user_id = (int64_t)uid;
    }
    if (PlayerObject* po = _peer_player(peer)) po->user_id = (int64_t)uid;   // corregir si ya existía
}

inline void NetworkService::_name_local_player(int display_num) {
    String pname = String("Player") + String::num_int64(display_num);
    Node* p = _local_player();
    if (p) {
        // Solo DisplayName (NO renombrar el nodo: el nombre "LocalPlayer" lo
        // usa el fallback de workspace.CurrentCamera; renombrarlo lo dejaría nil).
        p->set("DisplayName", pname);
        p->set_meta("PlayerIndex", display_num);
    }
    // El chat firma con el nombre del jugador (como en Roblox)
    Node* tcs = is_inside_tree() ? _find_by_class((Node*)get_tree()->get_root(), "TextChatService") : nullptr;
    if (tcs) tcs->call("set_player_name", pname);
}

// Spawns separados: que los jugadores no nazcan uno dentro de otro
inline void NetworkService::_offset_spawn(int idx) {
    Node* lp = _local_player();
    if (!lp) return;
    if (Node3D* n3 = gl_cast<Node3D>(lp))
        n3->set_global_position(gl_pos(n3) + Vector3((float)(idx - 2) * 2.5f, 0.0f, 0.0f));
    else if (Node2D* n2 = gl_cast<Node2D>(lp))
        n2->set_global_position(n2->get_global_position() + Vector2((float)(idx - 2) * 90.0f, 0.0f));
}

// La ventana del SERVIDOR no tiene personaje (como en Roblox Studio):
// el server observa con la camara libre; los jugadores son los clientes.
inline void NetworkService::_remove_local_player() {
    Node* p = _local_player();
    if (p) { gl_mp_log("ventana de servidor: sin personaje (solo observa)"); p->queue_free(); }
}

inline void NetworkService::_apply_device() {
    if (!is_inside_tree()) return;
    Window* w = get_tree()->get_root();
    if (!w) return;
    if (device == "Mobile") {
        // Telefono: ventana vertical (el joystick tactil lo pone RobloxPlayer)
        w->set_size(Vector2i(400, 820));
    } else if (device == "Console") {
        // Consola: pantalla 16:9 tipo TV; se juega con gamepad (ya soportado)
        w->set_size(Vector2i(1280, 720));
    } else if (device == "VR") {
        // VR: vista previa con dos "ojos" lado a lado
        w->set_size(Vector2i(1220, 640));
        CanvasLayer* cl = memnew(CanvasLayer);
        cl->set_name("GL_VRPreview");
        cl->set_layer(90);
        add_child(cl);
        GLVRPreview* vr = memnew(GLVRPreview);
        cl->add_child(vr);
    }
}

// Título + mosaico. Server (idx 1) = "Server View" (suele estar embebida en
// el editor); clientes (idx 2..N+1) = "Player1..N" repartidos en pantalla.
// Antes todas las ventanas abrían apiladas y parecían menos.
inline void NetworkService::_apply_window_layout(int idx, int cnt) {
    if (!is_inside_tree()) return;
    Window* w = get_tree()->get_root();
    if (!w) return;
    if (idx == 1) { w->set_title("Server View - GodotLuau"); return; }
    w->set_title(String("Player") + String::num_int64(idx - 1));
    DisplayServer* ds = DisplayServer::get_singleton();
    if (!ds) return;
    Rect2i ur = ds->screen_get_usable_rect();
    if (ur.size.x < 320 || ur.size.y < 240) return;   // headless / pantalla rara
    int cols = (cnt <= 2) ? cnt : ((cnt <= 4) ? 2 : 3);
    int rows = (cnt + cols - 1) / cols;
    Vector2i cell(ur.size.x / cols, ur.size.y / rows);
    int i0 = Math::clamp(idx - 2, 0, cnt - 1);   // celda del cliente
    Vector2i pos = ur.position + Vector2i((i0 % cols) * cell.x, (i0 / cols) * cell.y);
    if (device == "Mobile" || device == "Console" || device == "VR") {
        // Estos modos fijan su propio tamano de ventana; solo se posicionan
        w->set_position(pos + Vector2i(24, 40));
    } else {
        w->set_size(Vector2i(Math::max(cell.x - 24, 480), Math::max(cell.y - 56, 320)));
        w->set_position(pos + Vector2i(12, 40));
    }
}

// Vigila res://GodotLuau/.gl_view_cmd: el botón "Server View" del panel Game del
// editor escribe ahí "server|N" o "client|N"; la instancia host (o la única
// en un jugador) aplica el cambio de perspectiva.
inline void NetworkService::_poll_view_cmd(double dt) {
    view_poll += dt;
    if (view_poll < 0.4) return;
    view_poll = 0.0;
    Ref<FileAccess> f = FileAccess::open(GL_VIEW_CMD_PATH, FileAccess::READ);
    if (f.is_null()) return;
    PackedStringArray parts = f->get_as_text().strip_edges().split("|");
    f->close();
    if (parts.size() < 2) return;
    int n = String(parts[1]).to_int();
    if (n == view_last_n) return;
    if (player_index > 1) { view_last_n = n; return; }   // clientes: ignoran
    bool want_server = (String(parts[0]) == "server");
    // La ventana del SERVIDOR de una prueba multijugador no puede volverse
    // cliente (no tiene personaje) — como en Roblox Studio.
    if (!want_server && player_index == 1 && mode == 1) {
        gl_mp_log("la ventana del servidor siempre queda en Server View");
        view_last_n = n;
        return;
    }
    if (_set_view(want_server)) view_last_n = n;   // si aún no hay cámara, reintenta
}

inline Ref<MultiplayerAPI> NetworkService::_mp() {
    if (!is_inside_tree()) return Ref<MultiplayerAPI>();
    return get_tree()->get_multiplayer();
}

inline void NetworkService::_bind_mp_signals() {
    if (signals_bound) return;
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_null()) return;
    m->connect("peer_connected",      Callable(this, "_on_peer_connected"));
    m->connect("peer_disconnected",   Callable(this, "_on_peer_disconnected"));
    m->connect("connected_to_server", Callable(this, "_on_connected_ok"));
    m->connect("connection_failed",   Callable(this, "_on_connect_failed"));
    m->connect("server_disconnected", Callable(this, "_on_server_disconnected"));
    signals_bound = true;
}

inline void NetworkService::_setup_rpcs() {
    Dictionary u;
    u["rpc_mode"]      = MultiplayerAPI::RPC_MODE_ANY_PEER;
    u["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE;
    u["call_local"]    = false;
    u["channel"]       = 0;
    Dictionary r = u.duplicate();
    r["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
    rpc_config("_recv_state",    u);
    rpc_config("_relay_state",   u);
    rpc_config("_recv_state2d",  u);
    rpc_config("_relay_state2d", u);
    rpc_config("_recv_remove",   r);
    rpc_config("_relay_chat",    r);   // chat: confiable (no se pierde ningun mensaje)
    rpc_config("_recv_chat",     r);
    rpc_config("_relay_cmd",     r);   // un TextChatCommand de un cliente (F5)
    rpc_config("_ping_req",      u);   // medicion de ping (para el menu de ajustes)
    rpc_config("_ping_res",      u);
    rpc_config("_ping_report",   u);   // el cliente reporta su ping al servidor (GetNetworkPing 1.14.10)
    rpc_config("_kicked",        r);   // Kick: el motivo NO se puede perder (1.14.10)
    rpc_config("_set_team",      r);   // equipo de un jugador (1.14.10)
    rpc_config("_load_character", r);  // LoadCharacter de otro jugador (1.14.10)
    rpc_config("_set_char_health", r); // salud autoritativa del servidor (1.14.11)
    rpc_config("_gl_teleport",   r);   // TeleportService: cambiar de mundo (1.14.17)
    rpc_config("_relay_anim",    r);   // animaciones: NO se pueden perder (1.14.12)
    rpc_config("_recv_anim",     r);
    // RemoteEvent por red (Fase 2): confiables y no-confiables, ambos sentidos
    rpc_config("_re_deliver_server",  r);
    rpc_config("_re_deliver_client",  r);
    rpc_config("_ure_deliver_server", u);
    rpc_config("_ure_deliver_client", u);
    rpc_config("_gl_assign",          r);   // coordinador → cliente: ve a este mundo
    rpc_config("_rep_new",            r);   // replicación (1.14.5): crear instancia
    rpc_config("_rep_prop",           r);   // replicación: cambio de propiedad
    rpc_config("_rep_reparent",       r);   // replicación: mover de padre
    rpc_config("_rep_destroy",        r);   // replicación: destruir instancia
    rpc_config("_rep_attr",           r);   // replicación: atributo (F5)
    rpc_config("_rep_tag",            r);   // replicación: etiqueta de CollectionService (F5)
    rpc_config("_gl_peer_uid",        r);   // UserId secuencial: servidor → clientes
    rpc_config("_rf_invoke_server",   r);   // RemoteFunction (1.14.6): invocar server
    rpc_config("_rf_invoke_client",   r);   // RemoteFunction: invocar cliente
    rpc_config("_rf_return",          r);   // RemoteFunction: respuesta al invocador
    rpc_config("_mps_red",            r);   // MarketplaceService: ventana de compra y su resultado
    rpc_config("_phys_state",         u);   // física en red (1.14.7): estado (no confiable, último gana)
    rpc_config("_phys_owned",         u);   // física: estado del cliente-dueño al servidor
    rpc_config("_phys_owner",         r);   // física: cambio de dueño (confiable)
    rpc_config("_time_req",           u);   // reloj sincronizado (1.14.8)
    rpc_config("_time_res",           u);
}

// Dispara los callbacks Luau (cada uno en su propio hilo)
inline void NetworkService::_fire(std::vector<NCB>& cbs, bool has_id, int id) {
    // Copia defensiva: un callback Luau puede hacer :Connect (add_*_cb ->
    // push_back) DURANTE el dispatch y reasignar el vector, invalidando
    // iteradores/referencias. Iterar sobre una copia elimina esa reentrancia
    // (las conexiones nuevas disparan en el SIGUIENTE evento, como en Roblox).
    std::vector<NCB> snapshot = cbs;
    for (auto& cb : snapshot) {
        if (!cb.active || !gl_state_alive(cb.L)) continue;
        lua_State* th = lua_newthread(cb.L);
        lua_rawgeti(cb.L, LUA_REGISTRYINDEX, cb.ref);
        if (lua_isfunction(cb.L, -1)) {
            lua_xmove(cb.L, th, 1);
            int nargs = 0;
            if (has_id) { lua_pushinteger(th, id); nargs = 1; }
            gl_resume_handler(th, nargs);
        } else lua_pop(cb.L, 1);
        lua_pop(cb.L, 1);
    }
}

// ── Muñecos (representación visual de los otros jugadores) ────────────
inline Node3D* NetworkService::_make_puppet3d(int idx) {
    AnimatableBody3D* body = memnew(AnimatableBody3D);
    // CRITICO: con sync_to_physics (default) la fisica REVIERTE cualquier
    // set_global_position y el muñeco queda clavado donde nacio.
    body->set_sync_to_physics(false);

    CollisionShape3D* col = memnew(CollisionShape3D);
    Ref<CapsuleShape3D> cs; cs.instantiate();
    cs->set_radius(0.5f);
    cs->set_height(2.0f);
    col->set_shape(cs);
    body->add_child(col);

    // Visual: el MISMO personaje que el local (StarterCharacter o avatar R6
    // con animaciones); si falta todo, capsula azulada.
    Node3D* vis = gl_build_character(this);
    float cima = 1.0f;
    if (vis) {
        vis->set_name("Visual");
        // Avatar a escala real (como el local): capsula a su medida y el
        // cartel del nombre sobre su cabeza.
        if (vis->has_meta("_gl_rig")) {
            body->set_meta("_gl_rig", vis->get_meta("_gl_rig"));
            gl_ajustar_al_avatar(body, col, nullptr);
            if (const GLRigDatos* d = gl_rig_datos(String(vis->get_meta("_gl_rig")) == "R15")) cima = d->cima;
        }
        body->add_child(vis);
    } else {
        MeshInstance3D* m = memnew(MeshInstance3D);
        m->set_name("Visual");
        Ref<CapsuleMesh> cm; cm.instantiate();
        m->set_mesh(cm);
        Ref<StandardMaterial3D> mat; mat.instantiate();
        mat->set_albedo(Color(0.40f, 0.60f, 0.95f));   // otros jugadores en azul
        mat->set_roughness(0.95f);
        m->set_material_override(mat);
        body->add_child(m);
    }

    Label3D* tag = memnew(Label3D);
    tag->set_text(String("Player") + String::num_int64(idx));
    tag->set_position(Vector3(0.0f, cima + 1.4f, 0.0f));
    tag->set_pixel_size(0.012f);
    tag->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
    tag->set_draw_flag(Label3D::FLAG_DISABLE_DEPTH_TEST, true);   // se ve a través de paredes
    body->add_child(tag);
    return body;
}

inline Node2D* NetworkService::_make_puppet2d(int idx) {
    Node2D* body = memnew(Node2D);
    ColorRect* r = memnew(ColorRect);
    r->set_size(Vector2(40.0f, 80.0f));
    r->set_position(Vector2(-20.0f, -80.0f));
    r->set_color(Color(0.40f, 0.60f, 0.95f));
    body->add_child(r);
    Label* tag = memnew(Label);
    tag->set_text(String("Player") + String::num_int64(idx));
    tag->set_position(Vector2(-20.0f, -104.0f));
    body->add_child(tag);
    return body;
}

// Humanoid del personaje remoto (1.14.11). NO lo mueve: Humanoid::
// _physics_process hace cast_to<CharacterBody3D>(get_parent()) y sale, y el
// muñeco es un AnimatableBody3D → queda solo como objeto de API (Health,
// WalkSpeed, TakeDamage, Died, HealthChanged...), que es justo lo que hace
// falta. La posición la sigue mandando la red.
inline void NetworkService::_ensure_puppet_humanoid(Node* pup) {
    if (!pup || pup->get_node_or_null(NodePath("Humanoid"))) return;
    Humanoid* hum = memnew(Humanoid);
    hum->set_name("Humanoid");
    pup->add_child(hum);
    gl_asegurar_animator(hum);
    gl_ajustar_al_avatar(pup, nullptr, hum);   // RigType y HipHeight del avatar
}

inline void NetworkService::_update_puppet3d(int key, int idx, Vector3 pos, float yaw, float hp,
    float sp, bool air) {
    // El estado 3D que llega DEFINE la dimensión del mundo: no dependemos del
    // is_2d cacheado (que puede no estar puesto si el jugador local aún no nació).
    is_2d = false;
    auto it = puppets.find(key);
    if (it == puppets.end() || !_valid(it->second.node)) {
        Node* wsp = _workspace();
        if (!wsp) return;
        Node3D* pup = _make_puppet3d(idx);
        pup->set_name(String("RemotePlayer_") + String::num_int64(key));
        wsp->add_child(pup);
        pup->set_global_position(pos);
        // ── Personaje remoto REAL (1.14.11) ──────────────────────────
        // Antes esto era un muñeco: el rig entero colgaba de "Visual" y solo
        // se subía el HumanoidRootPart. Resultado: `hit.Parent:FindFirstChild
        // ("Humanoid")` daba nil con otros jugadores y NADA de daño/detección
        // funcionaba contra ellos (la causa real de los warnings "Humanoid no
        // encontrado"). Ahora se aplana igual que el personaje local: las
        // partes llevan su nombre Roblox y son hijas DIRECTAS del personaje.
        gl_flatten_r6_character(pup, pup->get_node_or_null("Visual"));
        // Si no era un rig R6 (cápsula de fallback o StarterCharacter), el
        // aplanado no aplica y "Visual" sigue ahí: se sube al menos el HRP.
        gl_lift_hrp(pup, pup->get_node_or_null("Visual"));
        _ensure_puppet_humanoid(pup);
        // Si el muñeco sale del árbol por fuera (Luau Destroy/ClearAllChildren),
        // quitar la entrada ANTES de que el puntero quede colgando.
        pup->connect("tree_exiting", Callable(this, "_on_puppet_gone").bind(key));
        Puppet P; P.node = pup; P.tpos = pos; P.tyaw = yaw; P.has_target = true;
        P.anim_sp = sp; P.anim_air = air;
        if (Node3D* v = gl_cast<Node3D>(pup->get_node_or_null("Visual")))
            P.vis_base_y = v->get_position().y;
        puppets[key] = P;
        // El muñeco es el .Character del objeto Player de ese peer.
        // Primer personaje → PlayerAdded y LUEGO CharacterAdded (orden Roblox).
        if (PlayerObject* po = _ensure_peer_player(key)) {
            String pn = String("Player") + String::num_int64(idx);
            po->set_name(pn); po->display_name = pn;
            po->set_character_silent(pup);
            if (!po->added_fired) {
                po->added_fired = true;
                if (Players* ps = _players_service()) ps->fire_player_added(po);
            }
            po->fire_character_added();
        }
        gl_mp_log(String("Player") + String::num_int64(idx) + " aparecio en tu mundo (pos " + String(pos) + ")");
        _apply_puppet_health(pup, key, hp);
        // Animación que ya estaba sonando antes de que naciera su muñeco (1.14.12).
        auto ait = peer_anim.find(key);
        if (ait != peer_anim.end() && ait->second.play)
            gl_anim_apply(pup, ait->second.id, ait->second.name, ait->second.speed, ait->second.looped, true);
    } else {
        it->second.tpos = pos; it->second.tyaw = yaw; it->second.has_target = true;
        it->second.anim_sp = sp; it->second.anim_air = air;
        _apply_puppet_health(it->second.node, key, hp);
    }
}

// Salud del personaje remoto (1.14.11). La difunde su DUEÑO junto con la
// posición; se anota lo último recibido para poder distinguir después un
// cambio hecho por el SERVIDOR (ver _health_authority_tick).
inline void NetworkService::_apply_puppet_health(Node* pup, int key, float hp) {
    if (!pup || hp < 0.0f) return;
    Node* hn = pup->get_node_or_null(NodePath("Humanoid"));
    if (!hn) return;
    Humanoid* hum = gl_cast<Humanoid>(hn);
    if (!hum) return;
    hum->set_health(hp);
    peer_net_health[key] = hp;
}

// (servidor) Si un ServerScript cambia la salud del personaje de un jugador
// (`hum:TakeDamage(10)`), ese cambio vive solo en la copia del servidor y su
// dueño lo pisaría con el próximo broadcast. Aquí se detecta la diferencia y
// se le ORDENA al dueño; su siguiente broadcast propaga el valor a todos.
inline void NetworkService::_health_authority_tick() {
    if (!is_server()) return;
    for (auto& kv : puppets) {
        if (!_valid(kv.second.node)) continue;
        Humanoid* hum = gl_cast<Humanoid>(kv.second.node->get_node_or_null(NodePath("Humanoid")));
        if (!hum) continue;
        auto it = peer_net_health.find(kv.first);
        if (it == peer_net_health.end()) continue;
        if (Math::abs((double)(hum->get_health() - it->second)) < 0.01) continue;
        it->second = hum->get_health();
        rpc_id(kv.first, "_set_char_health", hum->get_health());
    }
}

// (dueño) El servidor manda la salud autoritativa de MI personaje.
inline void NetworkService::_set_char_health(float hp) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;
    Node* lp = _local_player();
    if (!lp) return;
    if (Humanoid* hum = gl_cast<Humanoid>(lp->get_node_or_null(NodePath("Humanoid"))))
        hum->set_health(hp);
}

inline void NetworkService::_update_puppet2d(int key, int idx, Vector2 pos, float rot) {
    is_2d = true;
    auto it = puppets.find(key);
    if (it == puppets.end() || !_valid(it->second.node)) {
        Node* wsp = _workspace();
        if (!wsp) return;
        Node2D* pup = _make_puppet2d(idx);
        pup->set_name(String("RemotePlayer_") + String::num_int64(key));
        wsp->add_child(pup);
        pup->set_global_position(pos);
        pup->connect("tree_exiting", Callable(this, "_on_puppet_gone").bind(key));
        Puppet P; P.node = pup; P.tpos2d = pos; P.tyaw = rot; P.has_target = true;
        puppets[key] = P;
        if (PlayerObject* po = _ensure_peer_player(key)) {
            String pn = String("Player") + String::num_int64(idx);
            po->set_name(pn); po->display_name = pn;
            po->set_character_silent(pup);
            if (!po->added_fired) {
                po->added_fired = true;
                if (Players* ps = _players_service()) ps->fire_player_added(po);
            }
            po->fire_character_added();
        }
    } else {
        it->second.tpos2d = pos; it->second.tyaw = rot; it->second.has_target = true;
    }
}

// El muñeco salió del árbol: quitar su entrada del mapa mientras el puntero
// todavía es válido (evita is_instance_valid sobre un Node* colgante -> UAF).
inline void NetworkService::_on_puppet_gone(int key) {
    auto it = puppets.find(key);
    if (it != puppets.end()) puppets.erase(it);
}

inline void NetworkService::_remove_puppet(int key) {
    auto it = puppets.find(key);
    if (it != puppets.end()) {
        if (_valid(it->second.node)) it->second.node->queue_free();
        puppets.erase(it);
    }
}

inline void NetworkService::_clear_puppets() {
    for (auto& kv : puppets) if (_valid(kv.second.node)) kv.second.node->queue_free();
    puppets.clear();
}

// Aplica el estado de los otros jugadores TAL CUAL llega (1.14.17).
// Antes se interpolaba con una constante de tiempo de ~0,2s: el muñeco iba
// por detrás y, sobre todo, SUAVIZABA el movimiento — si el otro se movía a
// trompicones, tú lo veías deslizarse limpio. Y la animación se ADIVINABA de
// cuánto se había movido el muñeco, que es justo por lo que su salto parecía
// durar más de lo que duró. Ahora se copia la posición y el estado de
// animación que mandó su dueño, sin inventar nada.
inline void NetworkService::_lerp_puppets(double dt) {
    for (auto& kv : puppets) {
        Puppet& pu = kv.second;
        if (!_valid(pu.node) || !pu.has_target) continue;
        if (is_2d) {
            Node2D* n = gl_cast<Node2D>(pu.node);
            if (!n) continue;
            n->set_global_position(pu.tpos2d);
            n->set_rotation(pu.tyaw);
        } else {
            Node3D* n = gl_cast<Node3D>(pu.node);
            if (!n) continue;
            Vector3 before = gl_pos(n);
            n->set_global_position(pu.tpos);
            Vector3 r = n->get_rotation();
            r.y = pu.tyaw;
            n->set_rotation(r);
            // El avatar R6/R15 lo anima su Animator (pistas replicadas). Sin rig
            // (capsula o StarterCharacter sin aplanar) queda "Visual": bob simple.
            {
                Node3D* vis = gl_cast<Node3D>(n->get_node_or_null("Visual"));
                if (vis) {
                    if (pu.anim_sp > 0.05f) pu.walk_phase += (float)dt * 12.0f;
                    else                     pu.walk_phase *= 0.85f;
                    Vector3 vp = vis->get_position();
                    vp.y = pu.vis_base_y + Math::abs(Math::sin(pu.walk_phase)) * 0.14f;
                    vis->set_position(vp);
                }
            }
            (void)before;
        }
    }
}

inline bool NetworkService::_diag_tick() {
    // true ~1 vez por segundo (los broadcasts son 20/s; loguear todos seria spam)
    return diag_timer <= 0.0;
}

inline void NetworkService::_broadcast_local() {
    if (mode == 2 && !net_connected) return;   // cliente aún negociando: nada de RPC
    Node* lp = _local_player();
    if (!lp) return;   // la ventana del servidor no tiene personaje: no difunde
    bool server = is_server();
    int display_num = (player_index >= 2) ? player_index - 1 : player_index;   // glindex 2..N+1 → Player1..N
    if (_diag_tick()) {
        Node3D* n3 = gl_cast<Node3D>(lp);
        gl_mp_log(String("TX ") + (server ? "(server broadcast)" : "(a servidor)")
                  + " pos=" + (n3 ? String(gl_pos(n3)) : String("?"))
                  + " num=" + String::num_int64(display_num));
        diag_timer = 1.0;
    }
    if (is_2d) {
        Node2D* n = gl_cast<Node2D>(lp);
        if (!n) return;
        Vector2 p = n->get_global_position();
        float   r = n->get_global_rotation();
        if (server) rpc("_recv_state2d", 1, p, r, display_num);
        else        rpc_id(1, "_relay_state2d", p, r, display_num);
    } else {
        Node3D* n = gl_cast<Node3D>(lp);
        if (!n) return;
        Vector3 p = gl_pos(n);
        float   y = n->get_global_rotation().y;   // yaw GLOBAL (robusto ante padres rotados)
        // Salud del personaje (1.14.11): viaja con el estado para que los demás
        // vean el valor real. -1 = sin Humanoid (no tocar la del muñeco).
        // Animación (1.14.17): viaja el estado REAL de mi Humanoid. Antes cada
        // máquina lo ADIVINABA de cuánto se había movido el muñeco, y por eso
        // el salto de otro jugador se veía más largo de lo que fue.
        float hp = -1.0f, sp = 0.0f;
        bool air = false;
        if (Humanoid* hum = gl_cast<Humanoid>(lp->get_node_or_null(NodePath("Humanoid")))) {
            hp  = hum->get_health();
            sp  = hum->anim_speed01;
            air = hum->anim_airborne;
        }
        if (server) rpc("_recv_state", 1, p, y, display_num, hp, sp, air);
        else        rpc_id(1, "_relay_state", p, y, display_num, hp, sp, air);
    }
}

#endif // ROBLOX_NETWORK_JUGADORES_H

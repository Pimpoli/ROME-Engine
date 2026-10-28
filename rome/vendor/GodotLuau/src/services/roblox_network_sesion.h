#ifndef ROBLOX_NETWORK_SESION_H
#define ROBLOX_NETWORK_SESION_H

// Metodos de NetworkService: la sesion. _ready, _process, el arranque
// automatico, servidor/cliente/desconectar, la lista de servidores, la
// clave del host y el coordinador de mundos.
// Va aparte de roblox_network.h (pasaba de 2.000 lineas) y se incluye
// desde alli, justo despues de la clase.

inline void NetworkService::_gl_disconnect(int ref, int64_t vm) {
    for (std::vector<NCB>* v : { &pc_cbs, &pd_cbs, &conn_cbs, &fail_cbs })
        for (NCB& cb : *v) if (cb.ref == ref && GL_MISMA_VM_L(cb)) cb.active = false;
}

inline void NetworkService::_ready() {
    // Dedup: si ya existe otro NetworkService bajo el mismo padre, este sobra
    // (se puede crear tanto en Workspace._ready como perezosamente en GetService).
    Node* par = get_parent();
    if (par) {
        for (int i = 0; i < par->get_child_count(); i++) {
            Node* c = par->get_child(i);
            if (c != this && c->is_class("NetworkService")) { queue_free(); return; }
        }
    }
    _bind_mp_signals();
    _setup_rpcs();
    call_deferred("_auto_init");
}

inline void NetworkService::_process(double dt) {
    // 0-. Instancia (mundo lanzado por el coordinador): publica su latido
    //     (puerto + nº de jugadores) para que el coordinador sepa su estado.
    if (is_instance) {
        inst_hb_timer += dt;
        if (inst_hb_timer >= 1.0) { inst_hb_timer = 0.0; _inst_write_heartbeat(); }
    }
    // 0-. Cliente: reconectar al MUNDO que asignó el coordinador (reintenta,
    //     el mundo recién arrancado puede tardar en abrir su puerto).
    if (reconnect_try > 0) {
        if (net_connected) { reconnect_try = 0; }
        else {
            reconnect_timer += dt;
            if (reconnect_timer >= 0.6) {
                reconnect_timer = 0.0;
                reconnect_try--;
                connect_server(reconnect_ip, reconnect_port);
                if (reconnect_try == 0 && !net_connected)
                    UtilityFunctions::push_warning("[GodotLuau] No se pudo entrar al mundo asignado por el coordinador.");
            }
        }
    }
    // 0a. La ventana del servidor arranca en camara libre (reintenta hasta
    //     que exista una camara que reemplazar)
    if (boot_server_view && _set_view(true)) boot_server_view = false;
    // 0b. Toggle Server/Client View pedido desde el panel Game del editor
    if (view_watch) _poll_view_cmd(dt);
    // 0c. TRANSFORMACION EN VIVO: una ventana del editor sin rol (la nativa,
    //     incluso si quedo corriendo de una prueba anterior) se convierte en
    //     el SERVIDOR cuando detecta una sesion recien creada por el Play.
    //     Eleccion por puerto: solo UNA gana; si el puerto ya es de otro, nada.
    if (view_watch && mode == 0 && player_index == 0) {
        xform_poll += dt;
        if (xform_poll >= 0.5) {
            xform_poll = 0.0;
            int scnt; String sdev; double sage;
            if (gl_mp_session_read_ex(scnt, sdev, sage) && scnt >= 2 && sage < 30.0) {
                if (start_server(25575, scnt + 2)) {
                    device = sdev;
                    player_index = 1;
                    _remove_local_player();
                    boot_server_view = true;
                    _apply_window_layout(1, scnt);
                    gl_mp_log("sesion multijugador detectada -> esta ventana ahora es el SERVIDOR");
                }
            }
        }
    }
    if (diag_timer > 0.0) diag_timer -= dt;
    // 1. Reintentos de conexión del cliente (hasta que el host abra el puerto)
    if (retry_left > 0) {
        if (net_connected) {
            retry_left = 0;
        } else {
            retry_timer += dt;
            if (retry_timer >= 1.5) {
                retry_timer = 0.0;
                retry_left--;
                // Red de seguridad final: si tras ~6s nadie hostea, el PRIMER
                // jugador se promueve a host (sigue jugando como Player1) para
                // que la sesion SIEMPRE se forme.
                if (retry_left == 16 && player_index == 2 && !net_connected) {
                    if (start_server(25575, 10)) {
                        retry_left = 0;
                        gl_mp_log("nadie hostea; Player1 se promueve a HOST de respaldo (sigue jugando)");
                        return;
                    }
                }
                if (retry_left > 0) connect_server(gl_mp_host(), 25575);
                else if (!net_connected && player_index >= 2 && is_inside_tree()) {
                    gl_mp_log("no se encontro el host; cerrando esta ventana");
                    get_tree()->quit();   // cliente huérfano: el host nunca abrió el puerto
                }
            }
        }
    }
    // 2. Réplica: difundir mi estado y suavizar a los muñecos
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_valid() && m->has_multiplayer_peer()) {
        // Red de seguridad por si al abrir la sesión el árbol aún no estaba
        // montado: se auto-ignora en cuanto el place ya está sellado.
        _stamp_static_ids();
        if (m->get_peers().size() > 0) {
            bcast_timer += dt;
            // 30Hz (1.14.17): al aplicar el estado TAL CUAL (sin suavizar),
            // los 20Hz de antes se verian a saltos. El canal es no confiable
            // y son pocos bytes, asi que sale barato.
            if (bcast_timer >= 0.0333) { bcast_timer = 0.0; _broadcast_local(); }
            // Ping: el cliente pregunta cada segundo
            if (mode == 2 && net_connected) {
                ping_timer += dt;
                if (ping_timer >= 1.0) {
                    ping_timer = 0.0;
                    rpc_id(1, "_ping_req", (double)Time::get_singleton()->get_ticks_msec());
                }
                // Reloj sincronizado (1.14.8): rápido hasta el 1er sync, luego cada ~2.5s
                time_sync_timer += dt;
                if (time_sync_timer >= (time_synced ? 2.5 : 0.4)) {
                    time_sync_timer = 0.0;
                    rpc_id(1, "_time_req",
                           Time::get_singleton()->get_unix_time_from_system(),
                           (double)Time::get_singleton()->get_ticks_usec() / 1000000.0);
                }
            }
        }
        _lerp_puppets(dt);
        // Física en red (1.14.7): la autoridad transmite ~20Hz; todos interpolan.
        phys_timer += dt;
        if (phys_timer >= 0.05) { phys_timer = 0.0; _phys_tick(); }
        _phys_lerp(dt);
        // Auto ownership (1.14.7.1): el servidor reasigna al más cercano ~2Hz.
        if (is_server()) {
            phys_auto_timer += dt;
            if (phys_auto_timer >= 0.5) { phys_auto_timer = 0.0; _phys_auto_tick(); }
        }
        // Salud autoritativa del servidor (1.14.11): ~4Hz, solo manda si un
        // ServerScript cambió la salud de alguien.
        if (is_server()) {
            health_auth_timer += dt;
            if (health_auth_timer >= 0.25) { health_auth_timer = 0.0; _health_authority_tick(); }
        }
        // Purga de RemoteFunction huérfanos (1.14.9.1): una respuesta que llega
        // cuando ya nadie la espera, o un pendiente que nunca se cerró, se
        // quedaba en el mapa PARA SIEMPRE. Nadie los espera pasado un minuto.
        rf_gc_timer += dt;
        if (rf_gc_timer >= 5.0) {
            rf_gc_timer = 0.0;
            for (auto it = rf_expect.begin(); it != rf_expect.end(); ) {
                it->second.age += 5.0;
                if (it->second.age > 60.0) it = rf_expect.erase(it); else ++it;
            }
            auto& rs = gl_net_responses();
            for (auto it = rs.begin(); it != rs.end(); ) {
                it->second.age += 5.0;
                if (it->second.age > 60.0) {
                    if (it->second.local_L && it->second.local_ref >= 0 && gl_state_alive(it->second.local_L))
                        gl_unref(it->second.local_L, it->second.local_ref);
                    it = rs.erase(it);
                } else ++it;
            }
        }
    }
}

// Resuelve el rol de esta instancia y auto-conecta el multijugador local.
//   · ventana NATIVA (sin --glindex) + sesion res:// con count>=2  → SERVIDOR
//     (sin personaje, camara libre — como la vista de servidor de Roblox)
//   · --glindex 2..N+1 → cliente PlayerN (glindex-1)
//   · sin nada → un jugador normal
inline void NetworkService::_auto_init() {
    PackedStringArray args = OS::get_singleton()->get_cmdline_user_args();
    int idx = 0, cnt = 1;
    String direct_connect = "";   // --glconnect <ip>:<port>  (unirse por IP)
    int    dedicated_port = 0;    // --glserver  <port>       (hospedar por IP)
    int    coordinator_port = 0;  // --glhost   <port>        (COORDINADOR de mundos)
    String matchmaking = "";      // --glmatch  <ip>:<port>   (cliente por matchmaking)
    bool   instance_flag = false; // --glinstance             (este server es un MUNDO del coordinador)
    String secret = "";           // --glsecret <key>         (llave del dueño)
    int    maxper = 8;            // --glmax    <n>           (máx jugadores por mundo)
    for (int i = 0; i < args.size(); i++) {
        String a = args[i];
        if      (a == "--glindex"    && i + 1 < args.size()) idx = String(args[i + 1]).to_int();
        else if (a == "--glcount"    && i + 1 < args.size()) cnt = String(args[i + 1]).to_int();
        else if (a == "--gldevice"   && i + 1 < args.size()) device = args[i + 1];
        else if (a == "--glconnect"  && i + 1 < args.size()) direct_connect = args[i + 1];
        else if (a == "--glserver"   && i + 1 < args.size()) dedicated_port = String(args[i + 1]).to_int();
        else if (a == "--glhost"     && i + 1 < args.size()) coordinator_port = String(args[i + 1]).to_int();
        else if (a == "--glmatch"    && i + 1 < args.size()) matchmaking = args[i + 1];
        else if (a == "--glinstance") instance_flag = true;
        else if (a == "--glsecret"   && i + 1 < args.size()) secret = args[i + 1];
        else if (a == "--glmax"      && i + 1 < args.size()) maxper = String(args[i + 1]).to_int();
    }
    if (!secret.is_empty()) host_secret = secret;
    if (maxper > 0) coord_max = maxper;
    String role_src = "user args";
    if (idx == 0) {
        // Ventana nativa: la sesion res:// dice si hay prueba multijugador
        // (count>=2 → esta ventana es el SERVIDOR) y el dispositivo emulado.
        int scnt; String sdev; double sage;
        if (gl_mp_session_read_ex(scnt, sdev, sage)) {
            device = sdev;
            if (scnt >= 2) { idx = 1; cnt = scnt; role_src = GL_MP_SESSION_PATH; }
            gl_mp_log(String("sesion res:// leida: count=") + String::num_int64(scnt)
                      + " device=" + sdev + " edad=" + String::num_int64((int64_t)sage) + "s");
        } else {
            gl_mp_log("sin sesion res:// al arrancar (la vigilo por si aparece)");
        }
    }
    // Diagnostico (solo Modo Debug): con esto se ve EXACTAMENTE que llego
    gl_mp_log(String("user args = [") + String(", ").join(args) + "]");
    gl_mp_log(String("rol: idx=") + String::num_int64(idx) + " count=" + String::num_int64(cnt)
              + " device=" + device + " (fuente: " + role_src + ")");
    player_index = idx;

    // ── COORDINADOR de mundos (lo corre el DUEÑO) ──────────────────────
    //   --glhost <port> [--glmax N]  → reparte jugadores a mundos, respeta N
    if (coordinator_port > 0) {
        start_coordinator(coordinator_port, coord_max);
        view_watch = false; set_process(true); _apply_device();
        return;
    }
    // ── Cliente por MATCHMAKING (auto-join a un mundo con gente) ────────
    //   --glmatch <ip>:<port>  → se conecta al coordinador y este lo redirige
    if (!matchmaking.is_empty()) {
        String host = matchmaking; int cport = 25565;
        int colon = matchmaking.rfind(":");
        if (colon > 0) { host = matchmaking.substr(0, colon); cport = matchmaking.substr(colon + 1).to_int(); }
        join_matchmaking(host, cport);
        gl_mp_log(String("Matchmaking: pidiendo mundo al coordinador ") + host + String(":") + String::num_int64(cport));
        view_watch = false; set_process(true); _apply_device();
        return;
    }
    // ── Auto-join BAKED (juego exportado) ──────────────────────────────
    // El dueño pone res://GodotLuau/gl_match.cfg = "ip:puerto" (su coordinador) ANTES de
    // exportar; el jugador al abrir el juego se une solo a un mundo con gente.
    // Solo en build exportado (en el editor se hace el test local con glindex).
    if (!OS::get_singleton()->has_feature("editor")) {
        Ref<FileAccess> mf = gl_open_user_config("gl_match.cfg");
        if (mf.is_valid()) {
            String line = mf->get_line().strip_edges(); mf->close();
            if (!line.is_empty()) {
                String host = line; int cport = 25565;
                int colon = line.rfind(":");
                if (colon > 0) { host = line.substr(0, colon); cport = line.substr(colon + 1).to_int(); }
                join_matchmaking(host, cport);
                UtilityFunctions::print(String("[GodotLuau] Auto-join al coordinador ") + host + String(":") + String::num_int64(cport));
                view_watch = false; set_process(true); _apply_device();
                return;
            }
        }
    }

    // ── Direct Connect / servidor por argumento (juegos exportados) ─────
    // Permite unir/hospedar por IP entre máquinas distintas sin escribir
    // Lua (Minecraft-style). Tiene prioridad sobre el test local por glindex.
    //   --glconnect <ip>:<port>   → cliente que se une a ese servidor
    //   --glserver  <port>        → host escuchando en ese puerto
    if (!direct_connect.is_empty()) {
        String host = direct_connect; int cport = 25565;
        int colon = direct_connect.rfind(":");
        if (colon > 0) { host = direct_connect.substr(0, colon); cport = direct_connect.substr(colon + 1).to_int(); }
        connect_server(host, cport);
        retry_left = 20;
        gl_mp_log(String("Direct Connect a ") + host + String(":") + String::num_int64(cport));
        view_watch = false; set_process(true); _apply_device();
        return;
    }
    if (dedicated_port > 0) {
        // Servidor DEDICADO (estilo minecraft_server.jar): corre los
        // ServerScripts del creador y acepta clientes, SIN jugador local.
        // Con --headless corre sin ventana; si no, deja cámara libre para verlo.
        if (start_server(dedicated_port, 64)) {
            is_dedicated = true;
            player_index = 1;
            _remove_local_player();     // servidor puro: sin personaje
            boot_server_view = true;    // cámara libre (ignorada en headless)
            if (instance_flag) {        // es un MUNDO del coordinador → publica latido
                is_instance = true;
                coord_port  = dedicated_port;   // su propio puerto (para el latido)
                _inst_write_heartbeat();
            }
            UtilityFunctions::print(String("[GodotLuau] ") + (instance_flag ? String("MUNDO") : String("SERVIDOR DEDICADO"))
                + " escuchando en el puerto " + String::num_int64(dedicated_port) + " (sin jugador local).");
        } else {
            UtilityFunctions::push_warning(String("[GodotLuau] No se pudo abrir el servidor dedicado en el puerto ")
                + String::num_int64(dedicated_port));
        }
        view_watch = false; set_process(true); _apply_device();
        return;
    }

    if (idx >= 1 && cnt >= 2) {
        bool as_client = (idx != 1);
        if (idx == 1) {
            // Eleccion por puerto: si otra instancia ya abrio el 25575,
            // esta se une como cliente (robusto ante un doble servidor).
            if (!start_server(25575, cnt + 2)) { as_client = true; gl_mp_log("puerto 25575 ocupado -> me uno como cliente"); }
        }
        if (as_client) {
            String host = gl_mp_host();
            connect_server(host, 25575);
            retry_left = 20;
            _name_local_player(idx - 1);   // glindex 2..N+1 → Player1..PlayerN
            _offset_spawn(idx);            // que no nazcan uno dentro de otro
            gl_mp_log(String("cliente Player") + String::num_int64(idx - 1) + " conectando a " + host + ":25575...");
        } else {
            // VENTANA DEL SERVIDOR: observa con camara libre, sin personaje
            _remove_local_player();
            boot_server_view = true;   // la camara del jugador puede tardar un frame
            gl_mp_log(String("SERVIDOR escuchando en 25575 (") + String::num_int64(cnt) + " jugadores, dispositivo " + device + ")");
        }
        _apply_window_layout(idx, cnt);
    } else {
        gl_mp_log("modo un jugador");
    }
    // El watcher del toggle Server/Client corre siempre que el juego se
    // lanzo desde el editor (tambien con 1 jugador, como Roblox Studio).
    view_watch = OS::get_singleton()->has_feature("editor");
    set_process(true);
    _apply_device();
}

// ── API para Luau ────────────────────────────────────────────────────
inline bool NetworkService::start_server(int port, int max_players) {
    _bind_mp_signals();
    peer.instantiate();
    Error e = peer->create_server(port, max_players > 0 ? max_players : 32);
    if (e != OK) { UtilityFunctions::push_warning(String("[NetworkService] No se pudo abrir el servidor en el puerto ") + String::num_int64(port)); return false; }
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_valid()) m->set_multiplayer_peer(peer);
    mode = 1;
    gl_net_role() = 1; gl_net_service_id() = (uint64_t)get_instance_id();   // replicación: somos servidor
    // UserId secuencial: el host es SIEMPRE el jugador 1; los peers toman 2,3,...
    peer_seq_uid[1] = 1; next_seq_uid = 2; gl_local_user_id() = 1;
    if (PlayerObject* lp = _local_player_obj()) lp->user_id = 1;
    _sellar_jugador(_local_player_obj(), 1);   // el Player del host (F5)
    _stamp_static_ids();   // antes de correr scripts: que ya vean el place con netId (1.14.10)
    gl_run_deferred_server_scripts();   // si esta ventana era cliente y se promovió a host, corre ahora sus ServerScripts
    set_process(true);
    return true;
}

inline bool NetworkService::connect_server(const String& ip, int port) {
    _bind_mp_signals();
    peer.instantiate();
    // ENet exige una IP numérica: si llega un hostname, se resuelve por DNS.
    String addr = ip;
    if (!ip.is_valid_ip_address()) {
        String resolved = IP::get_singleton()->resolve_hostname(ip, IP::TYPE_ANY);
        if (!resolved.is_empty()) addr = resolved;
        else { UtilityFunctions::push_warning(String("[NetworkService] No se pudo resolver el host ") + ip); return false; }
    }
    target_address = ip + String(":") + String::num_int64(port);
    Error e = peer->create_client(addr, port);
    if (e != OK) { UtilityFunctions::push_warning(String("[NetworkService] No se pudo conectar a ") + ip); target_address = ""; return false; }
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_valid()) m->set_multiplayer_peer(peer);
    mode = 2;
    gl_net_role() = 2; gl_net_service_id() = (uint64_t)get_instance_id();   // replicación: somos cliente
    _strip_server_only();  // lo que solo ve el servidor, fuera de esta ventana (1.14.17)
    _stamp_static_ids();   // mismo place → mismos netId que el servidor (1.14.10)
    set_process(true);
    return true;
}

inline void NetworkService::disconnect_net() {
    _clear_puppets();
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_valid()) m->set_multiplayer_peer(Ref<MultiplayerPeer>());
    peer = Ref<ENetMultiplayerPeer>();
    mode = 0;
    gl_net_role() = 0;   // replicación off
    net_instances.clear();
    static_ids_done = false;   // al reconectar hay que volver a registrar el place (1.14.10)
    peer_seq_uid.clear(); next_seq_uid = 2; gl_local_user_id() = 0;   // UserId: reset al salir
    peer_ping_ms.clear(); ping_ms = -1.0;
    peer_net_health.clear(); health_auth_timer = 0.0;
    peer_anim.clear();
    // RemoteFunction en vuelo: depositar el fallo (NO limpiar las respuestas,
    // que es justo lo que el invocador tiene que recoger para re-lanzarlo).
    _rf_fail_pending(0, "RemoteFunction invoke cancelled: left the game");
    rf_gc_timer = 0.0;
    gl_server_unix_offset() = 0.0; gl_dgt_offset() = 0.0; time_synced = false;   // reloj: volver al local
    net_connected = false;
    target_address = "";
}

// Un cliente lo sabe por su modo: preguntarle al peer de ENet cuando el
// servidor ya se cayo (sigue puesto pero inactivo) escupia "The multiplayer
// instance isn't currently active" en cada llamada.
inline bool NetworkService::is_server() {
    if (mode == 2) return false;
    Ref<MultiplayerAPI> m = _mp();
    return m.is_valid() && m->has_multiplayer_peer() && m->is_server();
}

inline bool NetworkService::is_client() {
    if (mode == 2) return true;
    Ref<MultiplayerAPI> m = _mp();
    return m.is_valid() && m->has_multiplayer_peer() && !m->is_server();
}

inline int NetworkService::get_player_count() {
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_null() || !m->has_multiplayer_peer()) return 1;
    return (int)m->get_peers().size() + 1;   // + uno mismo
}

// IP local (LAN) para que el host se la pase a sus amigos ("hospedar en mi PC").
// Devuelve la primera IPv4 no-loopback; "127.0.0.1" si no hay red.
inline String NetworkService::get_local_ip() {
    PackedStringArray addrs = IP::get_singleton()->get_local_addresses();
    for (int i = 0; i < addrs.size(); i++) {
        String a = addrs[i];
        if (a.is_valid_ip_address() && a.get_slice_count(".") == 4 && a != "127.0.0.1")
            return a;
    }
    return "127.0.0.1";
}

inline Array NetworkService::get_server_list() {
    Ref<FileAccess> f = FileAccess::open(_servers_path(), FileAccess::READ);
    if (f.is_null()) return Array();
    String txt = f->get_as_text();
    f->close();
    Variant v = JSON::parse_string(txt);
    return (v.get_type() == Variant::ARRAY) ? (Array)v : Array();
}

inline void NetworkService::_save_server_list(const Array& list) {
    Ref<FileAccess> f = FileAccess::open(_servers_path(), FileAccess::WRITE);
    if (f.is_valid()) { f->store_string(JSON::stringify(list, "  ")); f->close(); }
}

inline void NetworkService::add_server(const String& name, const String& ip, int port) {
    Array list = get_server_list();
    for (int i = 0; i < list.size(); i++) {   // reemplazar si el nombre ya existe
        Dictionary d = list[i];
        if (String(d.get("name", "")) == name) { list.remove_at(i); break; }
    }
    Dictionary d; d["name"] = name; d["ip"] = ip; d["port"] = port;
    list.push_back(d);
    _save_server_list(list);
}

inline void NetworkService::remove_server(const String& name) {
    Array list = get_server_list();
    for (int i = 0; i < list.size(); i++) {
        Dictionary d = list[i];
        if (String(d.get("name", "")) == name) { list.remove_at(i); _save_server_list(list); return; }
    }
}

inline bool NetworkService::join_server(const String& name) {
    Array list = get_server_list();
    for (int i = 0; i < list.size(); i++) {
        Dictionary d = list[i];
        if (String(d.get("name", "")) == name)
            return connect_server(String(d.get("ip", "127.0.0.1")), (int)d.get("port", 25565));
    }
    return false;
}

inline String NetworkService::_gen_key() {
    static const char* al = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    uint64_t s = (uint64_t)Time::get_singleton()->get_ticks_usec() ^ 0x9E3779B97F4A7C15ULL;
    String k;
    for (int i = 0; i < 24; i++) { s = s * 6364136223846793005ULL + 1442695040888963407ULL; k += String::chr(al[(s >> 33) % 31]); }
    return k;
}

// Devuelve la llave del dueño; la crea la primera vez que él hostea.
inline String NetworkService::read_or_make_host_key() {
    String k = get_host_key();
    if (!k.is_empty()) return k;
    k = _gen_key();
    Ref<FileAccess> w = FileAccess::open(_host_key_path(), FileAccess::WRITE);
    if (w.is_valid()) { w->store_string(k); w->close(); }
    return k;
}

inline String NetworkService::get_host_key() {
    Ref<FileAccess> f = FileAccess::open(_host_key_path(), FileAccess::READ);
    if (f.is_null()) return "";
    String k = f->get_as_text().strip_edges(); f->close(); return k;
}

inline auto NetworkService::_coord_read_instances() -> std::vector<InstInfo> {
    std::vector<InstInfo> out;
    Ref<DirAccess> d = DirAccess::open("user://");
    if (d.is_null()) return out;
    double now = Time::get_singleton()->get_unix_time_from_system();
    PackedStringArray files = d->get_files();
    for (int i = 0; i < files.size(); i++) {
        String fn = files[i];
        if (!fn.begins_with("gl_inst_") || !fn.ends_with(".json")) continue;
        Ref<FileAccess> f = FileAccess::open(String("user://") + fn, FileAccess::READ);
        if (f.is_null()) continue;
        Variant v = JSON::parse_string(f->get_as_text()); f->close();
        if (v.get_type() != Variant::DICTIONARY) continue;
        Dictionary di = v;
        if (now - (double)di.get("t", 0) > 6.0) { d->remove(fn); continue; }  // latido viejo → mundo muerto
        InstInfo ii; ii.port = (int)di.get("port", 0); ii.players = (int)di.get("players", 0);
        if (ii.port > 0) out.push_back(ii);
    }
    return out;
}

inline int NetworkService::_spawn_instance(int iport) {
    String exe = OS::get_singleton()->get_executable_path();
    PackedStringArray a;
    if (OS::get_singleton()->has_feature("editor")) {
        a.push_back("--path");
        a.push_back(ProjectSettings::get_singleton()->globalize_path("res://"));
    }
    a.push_back("--headless");
    a.push_back("--");
    a.push_back("--glserver");   a.push_back(String::num_int64(iport));
    a.push_back("--glinstance");
    a.push_back("--glmax");      a.push_back(String::num_int64(coord_max));
    if (!host_secret.is_empty()) { a.push_back("--glsecret"); a.push_back(host_secret); }
    int pid = OS::get_singleton()->create_process(exe, a);
    UtilityFunctions::print(String("[GodotLuau] Coordinador: nuevo MUNDO en el puerto ")
        + String::num_int64(iport) + " (pid " + String::num_int64(pid) + ")");
    return iport;
}

// Elige un mundo con espacio; si no hay, lanza uno nuevo. Devuelve su puerto.
inline int NetworkService::_coord_pick_or_spawn() {
    std::vector<InstInfo> insts = _coord_read_instances();
    for (auto& ii : insts) if (ii.players < coord_max) return ii.port;   // mundo con hueco
    int iport = coord_port + 1;                                          // puerto libre para uno nuevo
    bool used = true;
    while (used) { used = false; for (auto& ii : insts) if (ii.port == iport) { used = true; iport++; break; } }
    return _spawn_instance(iport);
}

// Arranca el COORDINADOR (lo corre el dueño). Requiere la llave del dueño.
inline bool NetworkService::start_coordinator(int port, int max_per_world) {
    host_secret = read_or_make_host_key();
    if (host_secret.is_empty()) { UtilityFunctions::push_warning("[GodotLuau] No se pudo crear/leer la host.key; el coordinador no arranca."); return false; }
    coord_port = port;
    coord_max = max_per_world > 0 ? max_per_world : 8;
    if (!start_server(port, 512)) return false;   // servidor liviano solo para repartir
    is_coordinator = true;
    _remove_local_player();       // el coordinador no juega
    UtilityFunctions::print(String("[GodotLuau] COORDINADOR activo en el puerto ") + String::num_int64(port)
        + " (máx " + String::num_int64(coord_max) + " por mundo). host.key = " + host_secret);
    return true;
}

// Cliente: unirse por matchmaking (el coordinador te manda a un mundo).
inline bool NetworkService::join_matchmaking(const String& ip, int port) {
    match_ip = ip; match_port = port;
    return connect_server(ip, port);
}

// RPC: el coordinador (peer 1) le dice al cliente a qué mundo ir.
inline void NetworkService::_gl_assign(int iport) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo el coordinador
    reconnect_ip = match_ip.is_empty() ? gl_mp_host() : match_ip;
    reconnect_port = iport;
    reconnect_try = 40; reconnect_timer = 0.0;   // el mundo nuevo puede tardar en abrir
    gl_mp_log(String("el coordinador me asignó el mundo :") + String::num_int64(iport));
    disconnect_net();   // soltar el coordinador; el _process reconecta al mundo
}

// Instancia (mundo): publica su latido para que el coordinador lo vea.
inline void NetworkService::_inst_write_heartbeat() {
    Ref<MultiplayerAPI> m = _mp();
    int players = (m.is_valid() && m->has_multiplayer_peer()) ? (int)m->get_peers().size() : 0;
    Dictionary d; d["port"] = coord_port; d["players"] = players;
    d["t"] = Time::get_singleton()->get_unix_time_from_system();
    Ref<FileAccess> f = FileAccess::open(String("user://gl_inst_") + String::num_int64(coord_port) + ".json", FileAccess::WRITE);
    if (f.is_valid()) { f->store_string(JSON::stringify(d)); f->close(); }
}

// Estado legible: "Server" / "Connecting" / "Connected" / "Disconnected".
inline String NetworkService::get_connection_state() {
    if (mode == 1) return "Server";
    if (mode == 2) return net_connected ? "Connected" : "Connecting";
    return "Disconnected";
}

#endif // ROBLOX_NETWORK_SESION_H

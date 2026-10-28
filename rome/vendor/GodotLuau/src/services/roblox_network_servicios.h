#ifndef ROBLOX_NETWORK_SERVICIOS_H
#define ROBLOX_NETWORK_SERVICIOS_H
#include "gl_cast.h"

// Metodos de NetworkService: lo que viaja como mensaje. Conexion y
// desconexion de peers, RemoteEvent y RemoteFunction, MarketplaceService,
// lista de jugadores, chat, ping, animaciones, TeleportService,
// LoadCharacter, equipos, Kick, la hora del servidor y la vista de servidor.
// Va aparte de roblox_network.h (pasaba de 2.000 lineas) y se incluye
// desde alli, justo despues de la clase.

// ── Callbacks de señales de red ──────────────────────────────────────
inline void NetworkService::_on_peer_connected(int id) {
    // COORDINADOR: no es un mundo; reparte al jugador a una instancia con
    // espacio (o arranca una nueva) y lo redirige. No dispara PlayerAdded.
    if (is_coordinator) {
        int iport = _coord_pick_or_spawn();
        rpc_id(id, "_gl_assign", iport);
        gl_mp_log(String("coordinador: peer ") + String::num_int64(id) + " -> mundo :" + String::num_int64(iport));
        return;
    }
    gl_mp_log(String("otro jugador entro (peer ") + String::num_int64(id) + ")");
    if (mode == 1) {
        // UserId secuencial: primero poner al día al peer nuevo con las
        // asignaciones YA hechas (host + otros peers), luego asignarle el suyo
        // y avisar a TODOS para que todas las ventanas coincidan.
        for (auto& kv : peer_seq_uid) rpc_id(id, "_gl_peer_uid", kv.first, (int)kv.second);
        int64_t uid = next_seq_uid++;
        peer_seq_uid[id] = uid;
        rpc("_gl_peer_uid", id, (int)uid);
        // Replicación (1.14.5): mandarle al peer nuevo el mundo ya replicado. Los
        // clientes ya no corren ServerScripts, así que sin esto no verían lo que el
        // servidor creó antes de que ellos conectaran (late-join).
        _rep_sync_peer(id);
    }
    // NO se dispara Players.PlayerAdded aquí: un peer SIN personaje (p.ej. la
    // ventana SERVIDOR sin jugador) NO es un Player. PlayerAdded se dispara
    // cuando aparece su personaje (muñeco), en _update_puppet*. Aquí solo la
    // señal propia net:PlayerConnected(peerId).
    _fire(pc_cbs, true, id);
}

// Dispara CharacterRemoving + Players.PlayerRemoving y libera el objeto Player
// de un peer. Debe correr en TODAS las instancias que lo vieron entrar (server
// por _on_peer_disconnected, clientes por _recv_remove relayado), si no
// PlayerRemoving quedaría asimétrico con PlayerAdded y el Player se fugaría.
inline void NetworkService::_remove_peer_player(int id) {
    PlayerObject* po = _peer_player(id);
    if (po) {
        po->fire_character_removing();
        if (Players* ps = _players_service()) ps->fire_player_removing(po);
        _rep_collect_forget(po);   // sus netid (el Player, leaderstats...) mueren con el
        po->queue_free();
    }
    peer_player_ids.erase(id);
}

inline void NetworkService::_on_peer_disconnected(int id) {
    _remove_puppet(id);
    if (is_server()) rpc("_recv_remove", id);   // que los clientes también lo quiten
    // Física (1.14.7): partes que poseía ese peer vuelven al servidor.
    if (is_server()) {
        for (auto& kv : net_instances) {
            Node* n = _rep_node(kv.first);
            if (n && n->has_meta("_gl_owner") && (int)n->get_meta("_gl_owner") == id) {
                n->set_meta("_gl_owner", 0);
                rpc("_phys_owner", (int64_t)kv.first, 0, false);
            }
        }
    }
    // RemoteFunction en vuelo hacia ese peer: nunca va a contestar (1.14.9.1).
    _rf_fail_pending(id, "InvokeClient: the player left the game");
    peer_ping_ms.erase(id);
    peer_net_health.erase(id);
    peer_anim.erase(id);
    _remove_peer_player(id);
    _fire(pd_cbs, true, id);
}

inline void NetworkService::_on_connected_ok() {
    net_connected = true;
    _sellar_jugador(_local_player_obj(), get_peer_id());   // ya hay peer id (F5)
    gl_mp_log("conectado al host! sesion compartida activa");
    _fire(conn_cbs, false, 0);
}

inline void NetworkService::_on_server_disconnected() {
    net_connected = false;
    _rf_fail_pending(0, "InvokeServer: lost connection to the server");
    _clear_puppets();
    // Cliente local: si el host (editor) se detiene, cerrar esta ventana para
    // que no queden ventanas huérfanas conectándose a la nada.
    if (player_index >= 2 && is_inside_tree()) get_tree()->quit();
}

// ── RemoteEvent por red (Fase 2) ─────────────────────────────────────
// Lo llama RemoteEventNode vía call(). target: 0 = al servidor,
// -1 = a todos los clientes, >0 = a un peer concreto.
inline void NetworkService::net_send_event(const String& path, const Array& args, int target,
    bool reliable) {
    const char* m_srv = reliable ? "_re_deliver_server" : "_ure_deliver_server";
    const char* m_cli = reliable ? "_re_deliver_client" : "_ure_deliver_client";
    // La red puede no estar lista cuando un script dispara un RemoteEvent al
    // arrancar: en Roblox el cliente ya está replicado antes de correr scripts,
    // pero aquí los scripts corren mientras ENet aún negocia la conexión. Sin
    // esta guarda, rpc_id lanzaría "peer not connected". Ignoramos el envío.
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_null() || !m->has_multiplayer_peer()) return;
    if (target == 0) {
        if (mode == 2 && !net_connected) return;   // cliente aún conectándose al host
        rpc_id(1, m_srv, path, args);
    } else if (target == -1) {
        rpc(m_cli, path, args);                     // servidor → todos (0 peers = no-op)
    } else {
        if (!m->get_peers().has(target)) return;    // ese peer ya no está conectado
        rpc_id(target, m_cli, path, args);
    }
}

inline void NetworkService::_deliver_event(const String& path, const Array& args, bool to_server) {
    if (!is_inside_tree()) return;
    // Autoridad (como Roblox): OnServerEvent solo se dispara en el servidor;
    // OnClientEvent solo si el emisor es el servidor (peer 1). Evita que un
    // cliente modificado falsifique eventos por RPC crudo saltándose el sandbox.
    if (to_server) { if (!is_server()) return; }
    else           { if (get_multiplayer()->get_remote_sender_id() != 1) return; }
    RemoteEventNode* rev = gl_cast<RemoteEventNode>(get_node_or_null(NodePath(path)));
    if (!rev) return;
    if (to_server) {
        int sid = get_multiplayer()->get_remote_sender_id();
        // Como Roblox: OnServerEvent SIEMPRE recibe un Player válido. El objeto
        // Player de un peer se crea perezosamente al llegar su personaje, pero un
        // cliente puede FireServer antes de eso (p.ej. "ClienteListo" al arrancar).
        // _ensure_peer_player lo crea en el acto para que el emisor nunca sea nil.
        rev->deliver_to_server_cbs(args, _ensure_peer_player(sid), this);
    } else {
        rev->deliver_to_client_cbs(args, this);
    }
}

// ── RemoteFunction por red (1.14.6) ──────────────────────────────────
// El invocador (InvokeServer/InvokeClient en luau_api.h) llama estos net_*
// para mandar la petición; corre el handler remoto con soporte de yield vía
// el wrapper Lua __gl_rf_serve y devuelve el resultado con _rf_return, que el
// ScriptNodeBase que cedió recoge de gl_net_responses() en su _process.
// Deposita un fallo SIN pasar por la red, para que el invocador lo re-lance
// en el acto en vez de quedarse colgado esperando una respuesta imposible.
inline void NetworkService::_rf_fail_local(int64_t call_id, const String& msg) {
    GLNetResponse& r = gl_net_responses()[call_id];
    r.rets = Array();
    r.rets.push_back(msg);
    r.ok = false;
    r.ready = true;
    r.age = 0.0;
}

inline void NetworkService::net_invoke_server(String path, int call_id, Array args) {
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_null() || !m->has_multiplayer_peer() || (mode == 2 && !net_connected)) {
        _rf_fail_local((int64_t)call_id, "InvokeServer: not connected to a server");
        return;
    }
    rf_expect[(int64_t)call_id] = { 1, 0.0 };
    rpc_id(1, "_rf_invoke_server", path, call_id, args);
}

inline void NetworkService::net_invoke_client(int peer, String path, int call_id, Array args) {
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_null() || !m->has_multiplayer_peer() || !m->get_peers().has(peer)) {
        _rf_fail_local((int64_t)call_id, "InvokeClient: the player has left the game");
        return;
    }
    rf_expect[(int64_t)call_id] = { peer, 0.0 };
    rpc_id(peer, "_rf_invoke_client", path, call_id, args);
}

// Enviar la respuesta al invocador (peer 1 = servidor, o un cliente concreto).
inline void NetworkService::_rf_send_return(int reply_peer, int call_id, bool ok,
    const Array& rets) {
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_null() || !m->has_multiplayer_peer()) return;
    if (reply_peer == 1 || m->get_peers().has(reply_peer))
        rpc_id(reply_peer, "_rf_return", call_id, ok, rets);
}

// Lo llama __gl_rf_respond (C) cuando el handler termina.
inline void NetworkService::net_rf_respond(int reply_peer, int call_id, bool ok, Array rets) {
    _rf_send_return(reply_peer, call_id, ok, rets);
}

// Responder al invocador con un error (lo re-lanza allí, como Roblox).
inline void NetworkService::_rf_send_error(int reply_peer, int call_id, const String& msg) {
    Array a;
    a.push_back(msg);
    _rf_send_return(reply_peer, call_id, false, a);
}

// Corre el handler (OnServerInvoke/OnClientInvoke) vía __gl_rf_serve, que lo
// envuelve en task.spawn (soporta yield) y responde al terminar.
inline void NetworkService::_rf_run_handler(lua_State* L, int handler_ref, Node* player,
    int call_id, int reply_peer, const Array& args, bool is_server) {
    if (!gl_state_alive(L) || handler_ref == LUA_NOREF) {
        _rf_send_error(reply_peer, call_id, is_server
            ? "InvokeServer: OnServerInvoke is not set for this RemoteFunction"
            : "InvokeClient: OnClientInvoke is not set for this RemoteFunction");
        return;
    }
    lua_State* th = lua_newthread(L);
    lua_getglobal(th, "__gl_rf_serve");
    if (!lua_isfunction(th, -1)) { lua_pop(L, 1); _rf_send_error(reply_peer, call_id, "RemoteFunction handler could not be started"); return; }
    lua_rawgeti(th, LUA_REGISTRYINDEX, handler_ref);   // handler
    if (player) _gl_push_node(th, player); else lua_pushnil(th);   // player
    lua_pushnumber(th, (double)call_id);               // callId
    lua_pushnumber(th, (double)reply_peer);            // replyPeer
    lua_pushboolean(th, is_server ? 1 : 0);            // isServer
    int n = gl_net_decode_args(th, args, this);        // ...args
    gl_resume_handler(th, 5 + n);
    lua_pop(L, 1);   // quitar el hilo wrapper (el handler corre en su propio hilo de task.spawn)
}

inline void NetworkService::_rf_invoke_server(String path, int call_id, Array args) {
    if (!is_server()) return;
    int sender = get_multiplayer()->get_remote_sender_id();
    RemoteFunctionNode* rfn = gl_cast<RemoteFunctionNode>(get_node_or_null(NodePath(path)));
    if (!rfn) { _rf_send_error(sender, call_id, "InvokeServer: RemoteFunction not found on the server (" + path + ")"); return; }
    Node* player = _ensure_peer_player(sender);   // como OnServerEvent: Player válido
    _rf_run_handler(rfn->server_invoke_L, rfn->server_invoke_ref, player, call_id, sender, args, true);
}

inline void NetworkService::_rf_invoke_client(String path, int call_id, Array args) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo el servidor invoca al cliente
    RemoteFunctionNode* rfn = gl_cast<RemoteFunctionNode>(get_node_or_null(NodePath(path)));
    if (!rfn) { _rf_send_error(1, call_id, "InvokeClient: RemoteFunction not found on the client (" + path + ")"); return; }
    _rf_run_handler(rfn->client_invoke_L, rfn->client_invoke_ref, nullptr, call_id, 1, args, false);
}

inline void NetworkService::_rf_return(int call_id, bool ok, Array rets) {
    // Solo vale la respuesta del peer al que realmente invocamos: si no,
    // cualquier cliente podría falsificar el retorno de una llamada ajena.
    int sender = get_multiplayer()->get_remote_sender_id();
    auto it = rf_expect.find((int64_t)call_id);
    if (it == rf_expect.end() || it->second.peer != sender) return;
    rf_expect.erase(it);
    GLNetResponse& r = gl_net_responses()[(int64_t)call_id];
    r.rets = rets; r.ok = ok; r.ready = true; r.age = 0.0;
}

// ── MarketplaceService (gl_tienda.h) ─────────────────────────────────
// estado -1: el servidor pide la ventana de compra al cliente del jugador.
// estado 0/1: el cliente devuelve si compro.
inline void NetworkService::net_mps_enviar(int peer, String tipo, int64_t id, int estado) {
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_null() || !m->has_multiplayer_peer()) return;
    if (peer == 1 || m->get_peers().has(peer)) rpc_id(peer, "_mps_red", tipo, id, estado);
}

inline void NetworkService::_mps_red(String tipo, int64_t id, int estado) {
    const int sender = get_multiplayer()->get_remote_sender_id();
    if (!gl_tienda_red_hook()) return;
    if (estado < 0) { if (sender == 1) gl_tienda_red_hook()(sender, tipo, id, estado); return; }
    if (is_server()) gl_tienda_red_hook()(sender, tipo, id, estado);
}

// Falla las llamadas en vuelo hacia un peer que ya no está (o hacia todos).
inline void NetworkService::_rf_fail_pending(int peer, const String& msg) {
    for (auto it = rf_expect.begin(); it != rf_expect.end(); ) {
        if (peer == 0 || it->second.peer == peer) {
            _rf_fail_local(it->first, msg);
            it = rf_expect.erase(it);
        } else ++it;
    }
}

// ── Roster de jugadores (Players:GetPlayers / FireClient targeting) ───
// v1 pragmático: el "jugador" es el nodo-personaje local o el muñeco
// (Puppet) del peer remoto. Un objeto Player propio + PlayerAdded por red
// llegan en la siguiente tanda.
// Devuelven OBJETOS Player (Player ≠ Character): OnServerEvent y
// Players:GetPlayers reciben Players reales con UserId/Name/.Character.
inline Node* NetworkService::player_node_for_peer(int peer_id) {
    if (peer_id == get_peer_id()) return _local_player_obj();
    return _peer_player(peer_id);
}

inline int NetworkService::peer_for_player_node(Node* player) {
    if (!player) return 0;
    if (player == (Node*)_local_player_obj()) return get_peer_id();
    for (auto& kv : peer_player_ids)
        if ((Node*)gl_cast<PlayerObject>(ObjectDB::get_instance(kv.second)) == player) return kv.first;
    return 0;
}

inline TypedArray<Node> NetworkService::get_players_roster() {
    TypedArray<Node> r;
    // El jugador local solo cuenta si TIENE personaje: la ventana "servidor"
    // (host sin personaje, cámara libre) no debe aparecer como jugador.
    if (PlayerObject* lp = _local_player_obj())
        if (lp->get_character()) r.push_back(lp);
    for (auto& kv : peer_player_ids)
        if (PlayerObject* po = gl_cast<PlayerObject>(ObjectDB::get_instance(kv.second)))
            if (po->get_character()) r.push_back(po);   // solo peers con personaje (no el servidor)
    return r;
}

// Cliente -> servidor: "aquí estoy". El servidor lo muestra y lo reenvía.
inline void NetworkService::_relay_state(Vector3 pos, float yaw, int idx, float hp, float sp,
    bool air) {
    if (!is_server()) return;             // un cliente no retransmite a nadie
    int sid = get_multiplayer()->get_remote_sender_id();
    if (_diag_tick()) { gl_mp_log(String("RX relay de peer ") + String::num_int64(sid) + " pos=" + String(pos)); diag_timer = 1.0; }
    _update_puppet3d(sid, idx, pos, yaw, hp, sp, air);
    rpc("_recv_state", sid, pos, yaw, idx, hp, sp, air);
}

inline void NetworkService::_relay_state2d(Vector2 pos, float rot, int idx) {
    if (!is_server()) return;
    int sid = get_multiplayer()->get_remote_sender_id();
    _update_puppet2d(sid, idx, pos, rot);
    rpc("_recv_state2d", sid, pos, rot, idx);
}

// Servidor -> clientes: estado autoritativo de "who".
inline void NetworkService::_recv_state(int who, Vector3 pos, float yaw, int idx, float hp,
    float sp, bool air) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo el servidor
    int myid = _my_peer_id();
    if (who == myid) return;              // ese soy yo
    if (_diag_tick()) { gl_mp_log(String("RX estado de ") + String::num_int64(who) + " pos=" + String(pos)); diag_timer = 1.0; }
    _update_puppet3d(who, idx, pos, yaw, hp, sp, air);
}

inline void NetworkService::_recv_state2d(int who, Vector2 pos, float rot, int idx) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo el servidor
    int myid = _my_peer_id();
    if (who == myid) return;
    _update_puppet2d(who, idx, pos, rot);
}

inline void NetworkService::_recv_remove(int who) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo el servidor
    _remove_puppet(who); _remove_peer_player(who);
}

// ── CHAT EN RED (como Roblox: todos ven los mensajes de todos) ────────
// El RobloxChat local llama gl_send_chat al enviar; el servidor lo muestra
// y lo reenvia a todos; cada ventana lo agrega a su chat.
// El mensaje de otro entra al TextChatService como uno de ese jugador
// (TextSource, OnIncomingMessage, MessageReceived, burbuja).
inline void NetworkService::_show_chat(int peer, const String& name, const String& text) {
    if (!is_inside_tree()) return;
    if (Node* tcs = gl_servicio(this, "TextChatService"))
        tcs->call("gl_recibir_red", player_node_for_peer(peer), name, text);
    gl_mp_log(String("chat << ") + name + ": " + text);
}

// Un TextChatCommand escrito en un cliente: el servidor dispara sus
// propios Triggered y su Player.Chatted, y no lo reparte como mensaje.
inline void NetworkService::gl_send_cmd(const String& text) {
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_null() || !m->has_multiplayer_peer() || is_server()) return;
    rpc_id(1, "_relay_cmd", text);
}

inline void NetworkService::_relay_cmd(String text) {
    if (!is_server()) return;
    const int sid = get_multiplayer()->get_remote_sender_id();
    PlayerObject* po = _peer_player(sid);
    if (po) po->gl_fire_chatted(text);
    if (Node* tcs = gl_servicio(this, "TextChatService")) tcs->call("gl_comando_red", po, text);
}

inline void NetworkService::gl_send_chat(const String& name, const String& text) {
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_null() || !m->has_multiplayer_peer()) return;   // sin sesion: solo local
    if (is_server()) rpc("_recv_chat", 1, name, text);
    else             rpc_id(1, "_relay_chat", name, text);
    gl_mp_log(String("chat >> ") + name + ": " + text);
}

// Nombre AUTORITATIVO de un peer, el que ve el SERVIDOR. Es con el que se
// firma el chat: el nombre que manda el cliente NO se usa nunca.
inline String NetworkService::_authoritative_peer_name(int peer) {
    if (peer == 1 || peer == _my_peer_id()) {          // el propio host escribiendo
        if (Node* lp = _local_player()) {
            String dn = String(lp->get("DisplayName"));
            if (!dn.is_empty()) return dn;
        }
        return String("Player1");
    }
    if (PlayerObject* po = _peer_player(peer)) {
        if (!po->display_name.is_empty()) return po->display_name;
        return String(po->get_name());
    }
    return String("Player_") + String::num_int64(peer);   // aun sin personaje
}

// `name_cliente` llega en el paquete por compatibilidad de firma, pero se
// IGNORA a proposito: antes se retransmitia tal cual y cualquier cliente
// modificado podia escribir en el chat de todos firmando con el nombre de
// otro jugador. El servidor pone el nombre que le corresponde al emisor.
inline void NetworkService::_relay_chat(String name_cliente, String text) {
    if (!is_server()) return;                  // solo el servidor retransmite
    (void)name_cliente;
    int sid = get_multiplayer()->get_remote_sender_id();
    String name = _authoritative_peer_name(sid);
    PlayerObject* po = _peer_player(sid);
    // Un TextChatCommand que solo existe en el servidor (el cliente no lo
    // conoce y lo mando como mensaje): Triggered aqui y no se reparte.
    Node* tcs = gl_servicio(this, "TextChatService");
    if (tcs && (bool)tcs->call("gl_es_comando", text)) {
        if (po) po->gl_fire_chatted(text);
        tcs->call("gl_comando_red", po, text);
        return;
    }
    _show_chat(sid, name, text);               // el servidor tambien lo ve
    rpc("_recv_chat", sid, name, text);        // reenviar a todos los clientes
    // Player.Chatted del que escribio, en el servidor: ahi escuchan los
    // comandos de admin.
    if (po) po->gl_fire_chatted(text);
}

inline void NetworkService::_recv_chat(int from, String name, String text) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo el servidor difunde
    int myid = _my_peer_id();
    if (from == myid) return;                  // yo ya lo mostre al enviarlo
    _show_chat(from, name, text);
}

// ── PING (para el menu de ajustes, como el de Roblox) ─────────────────
// El cliente manda su reloj al servidor cada segundo; el servidor lo
// devuelve tal cual; el cliente resta contra SU reloj → ida y vuelta en ms.
inline void NetworkService::_ping_req(double t) {
    if (!is_server()) return;             // al ping solo contesta el servidor
    int sid = get_multiplayer()->get_remote_sender_id();
    rpc_id(sid, "_ping_res", t);
}

inline void NetworkService::_ping_res(double t) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo el servidor responde
    ping_ms = (double)Time::get_singleton()->get_ticks_msec() - t;
    rpc_id(1, "_ping_report", ping_ms);   // el servidor no puede medirlo él solo (1.14.10)
}

inline void NetworkService::_ping_report(double ms) {
    if (!is_server()) return;
    peer_ping_ms[get_multiplayer()->get_remote_sender_id()] = ms;
}

// Player:GetNetworkPing() → SEGUNDOS (como Roblox, no milisegundos).
inline double NetworkService::net_ping_for_player(Object* player_node) {
    Node* pn = gl_cast<Node>(player_node);
    if (!pn || mode == 0) return 0.0;
    int target = peer_for_player_node(pn);
    if (target == get_peer_id()) return ping_ms > 0.0 ? ping_ms / 1000.0 : 0.0;   // yo mismo
    if (is_server()) {
        auto it = peer_ping_ms.find(target);
        return it != peer_ping_ms.end() ? it->second / 1000.0 : 0.0;
    }
    return 0.0;   // un cliente no conoce el ping de otro (Roblox tampoco)
}

// ── Animaciones replicadas (1.14.12) ─────────────────────────────────
// Lo llama AnimationTrack::play/stop. Solo se replica MI personaje: la
// animación de cada jugador la decide su dueño (como Roblox).
inline void NetworkService::net_anim_state(Object* character, String anim_id, String anim_name,
    float speed, bool looped, bool play) {
    Node* ch = gl_cast<Node>(character);
    if (!ch || mode == 0) return;
    if (ch != _local_player()) return;   // no re-emitir la animación de un muñeco
    if (mode == 2 && !net_connected) return;
    if (is_server()) {
        peer_anim[1] = { anim_id, anim_name, speed, looped, play };   // la mía, para el late-join
        rpc("_recv_anim", 1, anim_id, anim_name, speed, looped, play);
    } else {
        rpc_id(1, "_relay_anim", anim_id, anim_name, speed, looped, play);
    }
}

inline void NetworkService::_relay_anim(String anim_id, String anim_name, float speed, bool looped,
    bool play) {
    if (!is_server()) return;             // un cliente no retransmite animaciones
    int sid = get_multiplayer()->get_remote_sender_id();
    _apply_anim_puppet(sid, anim_id, anim_name, speed, looped, play);
    rpc("_recv_anim", sid, anim_id, anim_name, speed, looped, play);
}

inline void NetworkService::_recv_anim(int who, String anim_id, String anim_name, float speed,
    bool looped, bool play) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo el servidor
    int myid = _my_peer_id();
    if (who == myid) return;   // ese soy yo
    _apply_anim_puppet(who, anim_id, anim_name, speed, looped, play);
}

inline void NetworkService::_apply_anim_puppet(int who, const String& anim_id,
    const String& anim_name, float speed, bool looped, bool play) {
    // Se recuerda SIEMPRE, aunque el muñeco no exista todavía: se aplicará en
    // cuanto aparezca (_update_puppet3d) y sirve para contárselo a los que
    // entren después.
    peer_anim[who] = { anim_id, anim_name, speed, looped, play };
    auto it = puppets.find(who);
    if (it == puppets.end() || !_valid(it->second.node)) return;
    gl_anim_apply(it->second.node, anim_id, anim_name, speed, looped, play);
}

// ── TeleportService (1.14.17) ────────────────────────────────────────
// Mover un jugador de un MUNDO a otro del mismo coordinador. Aquí un "place"
// es una instancia-mundo: se descubren por los latidos de user:// (los mismos
// que ya usa el coordinador), así que un mundo puede mandarte a otro sin
// hablar con nadie. El cliente reaprovecha el camino de _gl_assign: soltar
// este servidor y reconectar al otro.
// Devuelve el puerto destino, o 0 si no hay a dónde ir.
inline int NetworkService::net_teleport_player(Object* player_node, int dest_port,
    String data_json) {
    if (!is_server()) return 0;
    Node* pn = gl_cast<Node>(player_node);
    if (!pn) return 0;
    int target = peer_for_player_node(pn);
    if (target <= 0 || target == get_peer_id()) return 0;   // al host no se le teletransporta
    if (dest_port <= 0) {
        // Sin destino: el mundo con menos gente que NO sea este.
        std::vector<InstInfo> insts = _coord_read_instances();
        int mejor = 0, mejorN = 1 << 30;
        for (auto& ii : insts) {
            if (ii.port == coord_port) continue;   // este mismo mundo
            if (ii.players < mejorN) { mejor = ii.port; mejorN = ii.players; }
        }
        dest_port = mejor;
    }
    if (dest_port <= 0) {
        UtilityFunctions::push_warning("[GodotLuau] Teleport: no hay otro mundo al que ir (¿coordinador activo?)");
        return 0;
    }
    Ref<MultiplayerAPI> m = _mp();
    if (m.is_null() || !m->get_peers().has(target)) return 0;
    rpc_id(target, "_gl_teleport", dest_port, data_json);
    gl_mp_log(String("teleport: peer ") + String::num_int64(target) + " -> mundo :" + String::num_int64(dest_port));
    return dest_port;
}

// (cliente) El servidor me manda a otro mundo. Los datos del teleport se
// guardan en memoria: el PROCESO sobrevive a la reconexión, así que al llegar
// siguen ahí (por eso no hace falta archivo).
inline void NetworkService::_gl_teleport(int dest_port, String data_json) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo mi servidor
    gl_teleport_data() = data_json;
    gl_teleport_arrived() = true;
    reconnect_ip = target_address.is_empty() ? gl_mp_host() : target_address.split(":")[0];
    reconnect_port = dest_port;
    reconnect_try = 40; reconnect_timer = 0.0;
    gl_mp_log(String("teleport: me mandan al mundo :") + String::num_int64(dest_port));
    call_deferred("disconnect_net");   // soltar este mundo; el _process reconecta al otro
}

inline bool NetworkService::net_teleport_arrived() {
    bool v = gl_teleport_arrived();
    gl_teleport_arrived() = false;   // se consume una sola vez, como Roblox
    return v;
}

// Puertos de los mundos vivos (para ReserveServer/listados).
inline PackedInt32Array NetworkService::net_world_ports() {
    PackedInt32Array out;
    for (auto& ii : _coord_read_instances()) out.push_back(ii.port);
    return out;
}

// ── Contenedores solo-servidor (1.14.17) ─────────────────────────────
// En Roblox el contenido de ServerScriptService y ServerStorage NUNCA llega
// al cliente. Aquí todas las ventanas cargan el MISMO place, así que un
// LocalScript podía leerlos enteros: `game.ServerStorage.LoQueSea` funcionaba.
// Al pasar a cliente se vacían, así que no hay nada que leer.
// LIMITACIÓN honesta: esto corta el acceso desde Luau, pero los datos siguen
// dentro del .pck del cliente. Blindarlo de verdad exige no EMPAQUETARLOS en
// la build de cliente, que es cosa del exportador (no de esta versión).
inline void NetworkService::_strip_server_only() {
    if (!is_inside_tree()) return;
    Node* root = (Node*)get_tree()->get_root();
    const char* solo_server[] = { "ServerScriptService", "ServerStorage", nullptr };
    for (int i = 0; solo_server[i]; i++) {
        Node* c = _find_by_name(root, solo_server[i]);
        if (!c) continue;
        int n = c->get_child_count();
        for (int j = n - 1; j >= 0; j--) {
            Node* ch = c->get_child(j);
            if (!ch) continue;
            c->remove_child(ch);
            ch->queue_free();
        }
        if (n > 0) gl_mp_log(String("cliente: vaciado ") + solo_server[i]
                             + " (" + String::num_int64(n) + " hijos, solo-servidor)");
    }
}

// ── LoadCharacter (1.14.10) ──────────────────────────────────────────
// Cada máquina construye su propio personaje, así que respawnear a OTRO
// jugador es pedirle a SU cliente que lo rehaga.
inline void NetworkService::net_load_character(Object* player_node) {
    Node* pn = gl_cast<Node>(player_node);
    if (!pn) return;
    int target = peer_for_player_node(pn);
    int myid = get_peer_id();
    if (mode == 0 || target == 0 || target == myid) { _do_load_character(); return; }
    if (is_server()) rpc_id(target, "_load_character");
}

inline void NetworkService::_load_character() {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo el servidor lo pide
    _do_load_character();
}

inline void NetworkService::_do_load_character() {
    if (Players* ps = _players_service()) ps->load_local_character();
}

// ── Equipos (1.14.10) ────────────────────────────────────────────────
// El Team en sí se replica solo (Teams es contenedor replicable); lo que
// hay que difundir es a QUÉ equipo pertenece cada jugador, porque el objeto
// Player lo construye cada máquina por su cuenta.
inline void NetworkService::net_set_team(Object* player_node, Object* team_node) {
    Node* pn = gl_cast<Node>(player_node);
    if (!pn) return;
    Node* tn = gl_cast<Node>(team_node);
    int target = peer_for_player_node(pn);
    int64_t tid = (tn && tn->has_meta("_gl_netid")) ? (int64_t)tn->get_meta("_gl_netid") : 0;
    // Aplicar AQUI con el nodo real, no por netid (1.15). Antes esto llamaba a
    // _apply_team(target, tid) y, si el Team todavia no estaba replicado, no
    // tenia _gl_netid -> tid=0 -> _apply_team resolvia nullptr y ponia
    // team_id=0: el equipo recien asignado se borraba a si mismo una linea
    // despues. Por eso "player.Team = equipo" nunca se quedaba puesto.
    if (PlayerObject* po = gl_cast<PlayerObject>(pn))
        po->team_id = tn ? (uint64_t)tn->get_instance_id() : 0;
    if (is_server()) rpc("_set_team", target, tid);
}

inline void NetworkService::_apply_team(int peer, int64_t team_netid) {
    // _ensure_peer_player, NO _peer_player: en el late-join el objeto Player
    // del otro puede no estar construido todavía en esta máquina y el equipo
    // se perdería en silencio.
    PlayerObject* po = (peer == get_peer_id() || peer == 0) ? _local_player_obj() : _ensure_peer_player(peer);
    if (!po) return;
    Node* t = team_netid ? _rep_node((uint64_t)team_netid) : nullptr;
    po->team_id = t ? (uint64_t)t->get_instance_id() : 0;
}

inline void NetworkService::_set_team(int peer, int64_t team_netid) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo el servidor asigna
    _apply_team(peer, team_netid);
}

// ── Kick (1.14.10) ───────────────────────────────────────────────────
// Roblox: el servidor puede expulsar a cualquiera; un cliente, solo a sí
// mismo. Antes esto era literalmente `return 0`: no hacía nada y no avisaba.
inline void NetworkService::net_kick_player(Object* player_node, String msg) {
    Node* pn = gl_cast<Node>(player_node);
    if (!pn) return;
    int target = peer_for_player_node(pn);
    int myid = get_peer_id();
    if (mode == 0 || target == 0) { _kick_self(msg); return; }   // single-player
    if (is_server()) {
        if (target == myid) { _kick_self(msg); return; }         // el host se echa a sí mismo
        Ref<MultiplayerAPI> m = _mp();
        if (m.is_null() || !m->get_peers().has(target)) return;
        rpc_id(target, "_kicked", msg);
        // Desconexión GRACEFUL (now=false): ENet vacía el RPC del motivo
        // antes de cortar. Con now=true el jugador se iría sin saber por qué.
        if (peer.is_valid()) peer->disconnect_peer(target, false);
        return;
    }
    if (target == myid) _kick_self(msg);   // cliente: solo a sí mismo
}

inline void NetworkService::_kick_self(const String& msg) {
    gl_mp_log(String("expulsado del juego: ") + msg);
    UtilityFunctions::push_warning(String("[GodotLuau] Kicked: ") + msg);
    if (player_index >= 2 && is_inside_tree()) get_tree()->quit();   // ventana de jugador: cerrar
    // DIFERIDO: esto puede venir del handler del RPC _kicked, y soltar el
    // peer de ENet en mitad del poll de la red no es seguro.
    else call_deferred("disconnect_net");
}

inline void NetworkService::_kicked(String msg) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo el servidor expulsa
    _kick_self(msg);
}

// Reloj sincronizado (1.14.8). El cliente manda su reloj; el servidor responde
// con el suyo (unix + DGT). El cliente estima el desfase con media vuelta.
inline void NetworkService::_time_req(double c_unix, double c_dgt) {
    if (!is_server()) return;             // el reloj de referencia es el del servidor
    int sid = get_multiplayer()->get_remote_sender_id();
    rpc_id(sid, "_time_res", c_unix, c_dgt,
           Time::get_singleton()->get_unix_time_from_system(),
           (double)Time::get_singleton()->get_ticks_usec() / 1000000.0);
}

inline void NetworkService::_time_res(double c_unix, double c_dgt, double s_unix, double s_dgt) {
    if (get_multiplayer()->get_remote_sender_id() != 1) return;   // solo del servidor
    double now_unix = Time::get_singleton()->get_unix_time_from_system();
    double now_dgt  = (double)Time::get_singleton()->get_ticks_usec() / 1000000.0;
    double half = (now_unix - c_unix) * 0.5;   // rtt/2 en segundos
    if (half < 0.0) half = 0.0;
    double new_unix_off = (s_unix + half) - now_unix;
    double new_dgt_off  = (s_dgt + half) - now_dgt;
    if (!time_synced) {                          // 1er sync: snap
        gl_server_unix_offset() = new_unix_off;
        gl_dgt_offset() = new_dgt_off;
        time_synced = true;
    } else {                                     // suavizar (evita saltos del reloj)
        gl_server_unix_offset() += (new_unix_off - gl_server_unix_offset()) * 0.25;
        gl_dgt_offset()         += (new_dgt_off  - gl_dgt_offset())         * 0.25;
    }
}

inline double NetworkService::get_ping_ms() {
    if (mode == 1) return 0.0;                      // el servidor/host: 0
    if (mode == 2 && net_connected) return ping_ms; // cliente conectado
    return -1.0;                                    // sin sesion
}

// ── Vista de Servidor: poner (true) o quitar (false) la cámara libre ──
// Devuelve false si aún no se puede aplicar (sin cámara todavía) para que
// el watcher reintente en el próximo tick.
inline bool NetworkService::_set_view(bool server) {
    if (server == gl_freecam().active) return true;   // ya está así
    if (server) {
        Viewport* vp = get_viewport();
        Camera3D* cur = vp ? vp->get_camera_3d() : nullptr;
        prevcam_id = cur ? (uint64_t)cur->get_instance_id() : 0;
        GLFreeCamera* fc = memnew(GLFreeCamera);
        fc->set_name("GL_ServerFreeCam");
        get_tree()->get_root()->add_child(fc);
        if (cur) {
            fc->set_global_transform(gl_xf(cur));
        } else {
            // Sin camara previa (ventana del servidor sin personaje):
            // vista aerea del spawn, mirando al centro del mapa
            fc->set_global_position(Vector3(0.0f, 14.0f, 26.0f));
            fc->set_rotation(Vector3(-0.45f, 0.0f, 0.0f));
        }
        fc->make_current();
        freecam_id = (uint64_t)fc->get_instance_id();
        gl_freecam().active = true;
        gl_mp_log("Vista de SERVIDOR: camara libre (WASD + click derecho; el personaje no se mueve)");
    } else {
        gl_freecam().active = false;
        if (Object* o = ObjectDB::get_instance(freecam_id))
            if (Node* n = gl_cast<Node>(o)) n->queue_free();
        freecam_id = 0;
        Camera3D* pc = gl_cast<Camera3D>(ObjectDB::get_instance(prevcam_id));
        if (pc) pc->make_current();
        Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
        gl_mp_log("Vista de CLIENTE: camara del jugador");
    }
    return true;
}

#endif // ROBLOX_NETWORK_SERVICIOS_H

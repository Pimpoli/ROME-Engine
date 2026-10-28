#ifndef LUAU_API_INDICE_MUNDO_H
#define LUAU_API_INDICE_MUNDO_H
#include "gl_cast.h"

// Lectura de propiedades desde Luau (__index de las instancias), NetworkService, RunService, Part, Workspace, RemoteEvent,
// RemoteFunction, BindableEvent, BodyMovers y constraints.
//
// Va aparte de luau_api.h (pasaba de 2.000 lineas) y se incluye desde alli,
// justo antes de godot_object_index_real, que llama a las zonas en orden.
// Devuelve cuantos valores deja en la pila, o -1 si la clave no es de
// esta zona y hay que seguir buscando.

inline int gl_index_mundo(lua_State* L, Node* n, const char* key) {
    // ── RunService ────────────────────────────────────────────────
    // ── NetworkService (multijugador, Fase 0) ─────────────────────────
    NetworkService* nsvc = gl_cast<NetworkService>(n);
    if (nsvc) {
        if (strcmp(key, "StartServer") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                int port = (int)luaL_optnumber(pL, 2, 25565);
                int maxp = (int)luaL_optnumber(pL, 3, 32);
                lua_pushboolean(pL, s && s->start_server(port, maxp)); return 1;
            }, "StartServer", 1); return 1;
        }
        if (strcmp(key, "Connect") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                const char* ip = gl_str(pL, 2);
                int port = (int)luaL_optnumber(pL, 3, 25565);
                lua_pushboolean(pL, s && s->connect_server(String(ip), port)); return 1;
            }, "Connect", 1); return 1;
        }
        if (strcmp(key, "Disconnect") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                if (s) s->disconnect_net(); return 0;
            }, "Disconnect", 1); return 1;
        }
        if (strcmp(key, "IsServer") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushboolean(pL, s && s->is_server()); return 1;
            }, "IsServer", 1); return 1;
        }
        if (strcmp(key, "IsClient") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushboolean(pL, s && s->is_client()); return 1;
            }, "IsClient", 1); return 1;
        }
        if (strcmp(key, "GetPeerId") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushnumber(pL, s ? s->get_peer_id() : 0); return 1;
            }, "GetPeerId", 1); return 1;
        }
        if (strcmp(key, "GetPlayerCount") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushnumber(pL, s ? s->get_player_count() : 1); return 1;
            }, "GetPlayerCount", 1); return 1;
        }
        if (strcmp(key, "GetDevice") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushstring(pL, s ? s->get_device().utf8().get_data() : "PC"); return 1;
            }, "GetDevice", 1); return 1;
        }
        if (strcmp(key, "GetPlayerIndex") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushnumber(pL, s ? s->get_player_index() : 0); return 1;
            }, "GetPlayerIndex", 1); return 1;
        }
        if (strcmp(key, "IsConnected") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushboolean(pL, s && s->is_connected_net()); return 1;
            }, "IsConnected", 1); return 1;
        }
        if (strcmp(key, "GetServerAddress") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushstring(pL, s ? s->get_server_address().utf8().get_data() : ""); return 1;
            }, "GetServerAddress", 1); return 1;
        }
        if (strcmp(key, "GetConnectionState") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushstring(pL, s ? s->get_connection_state().utf8().get_data() : "Disconnected"); return 1;
            }, "GetConnectionState", 1); return 1;
        }
        if (strcmp(key, "IsDedicatedServer") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushboolean(pL, s && s->is_dedicated_server()); return 1;
            }, "IsDedicatedServer", 1); return 1;
        }
        if (strcmp(key, "GetLocalIP") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushstring(pL, s ? s->get_local_ip().utf8().get_data() : "127.0.0.1"); return 1;
            }, "GetLocalIP", 1); return 1;
        }
        // ── Lista de servidores guardados (estilo lista de Minecraft) ──
        if (strcmp(key, "GetServers") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_newtable(pL);
                if (s) {
                    Array list = s->get_server_list();
                    for (int i = 0; i < list.size(); i++) {
                        Dictionary d = list[i];
                        lua_newtable(pL);
                        lua_pushstring(pL, String(d.get("name", "")).utf8().get_data()); lua_setfield(pL, -2, "Name");
                        lua_pushstring(pL, String(d.get("ip", "")).utf8().get_data());   lua_setfield(pL, -2, "IP");
                        lua_pushnumber(pL, (double)(int)d.get("port", 25565));           lua_setfield(pL, -2, "Port");
                        lua_rawseti(pL, -2, i + 1);
                    }
                }
                return 1;
            }, "GetServers", 1); return 1;
        }
        if (strcmp(key, "AddServer") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                const char* name = gl_str(pL, 2);
                const char* ip   = gl_str(pL, 3);
                int port = (int)luaL_optnumber(pL, 4, 25565);
                if (s) s->add_server(String(name), String(ip), port);
                return 0;
            }, "AddServer", 1); return 1;
        }
        if (strcmp(key, "RemoveServer") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                if (s) s->remove_server(String(gl_str(pL, 2)));
                return 0;
            }, "RemoveServer", 1); return 1;
        }
        if (strcmp(key, "JoinServer") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushboolean(pL, s && s->join_server(String(gl_str(pL, 2)))); return 1;
            }, "JoinServer", 1); return 1;
        }
        // ── Coordinador de mundos (matchmaking estilo Roblox) ──
        if (strcmp(key, "StartHost") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                int port = (int)luaL_optnumber(pL, 2, 25565);
                int maxp = (int)luaL_optnumber(pL, 3, 8);
                lua_pushboolean(pL, s && s->start_coordinator(port, maxp)); return 1;
            }, "StartHost", 1); return 1;
        }
        if (strcmp(key, "JoinMatchmaking") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                const char* ip = gl_str(pL, 2);
                int port = (int)luaL_optnumber(pL, 3, 25565);
                lua_pushboolean(pL, s && s->join_matchmaking(String(ip), port)); return 1;
            }, "JoinMatchmaking", 1); return 1;
        }
        if (strcmp(key, "GenerateHostKey") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushstring(pL, s ? s->read_or_make_host_key().utf8().get_data() : ""); return 1;
            }, "GenerateHostKey", 1); return 1;
        }
        if (strcmp(key, "GetHostKey") == 0) {
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushstring(pL, s ? s->get_host_key().utf8().get_data() : ""); return 1;
            }, "GetHostKey", 1); return 1;
        }
        if (strcmp(key, "PlayerConnected") == 0 || strcmp(key, "PlayerDisconnected") == 0 ||
            strcmp(key, "Connected") == 0       || strcmp(key, "ConnectionFailed") == 0) {
            int which = (strcmp(key,"PlayerConnected")==0) ? 0 :
                        (strcmp(key,"PlayerDisconnected")==0) ? 1 :
                        (strcmp(key,"Connected")==0) ? 2 : 3;
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)nsvc);
            lua_pushinteger(L, which);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                NetworkService* s = (NetworkService*)lua_touserdata(pL, lua_upvalueindex(1));
                int w = (int)lua_tointeger(pL, lua_upvalueindex(2));
                if (!s || !lua_isfunction(pL, 2)) { lua_pushnil(pL); return 1; }
                lua_pushvalue(pL, 2); int ref = gl_ref(pL, -1); lua_pop(pL, 1);
                // El estado PRINCIPAL, no la corrutina que conecta: _fire mira
                // gl_state_alive, que solo conoce los principales, y con la
                // corrutina estas cuatro señales no saltaban nunca.
                lua_State* mL = gl_main_of(pL);
                if      (w == 0) s->add_pc_cb(mL, ref);
                else if (w == 1) s->add_pd_cb(mL, ref);
                else if (w == 2) s->add_conn_cb(mL, ref);
                else             s->add_fail_cb(mL, ref);
                _gl_push_connection(pL, s, ref);
                return 1;
            }, "Connect", 2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }
    }

    // ── RunService ────────────────────────────────────────────────
    RunService* rs = gl_cast<RunService>(n);
    if (rs) {
        if (strcmp(key, "IsRunning") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                lua_pushboolean(pL, !Engine::get_singleton()->is_editor_hint()); return 1;
            }, "IsRunning");
            return 1;
        }
        if (strcmp(key, "IsClient") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                lua_getglobal(pL, "__IS_CLIENT"); bool v = lua_toboolean(pL, -1) != 0; lua_pop(pL, 1);
                lua_pushboolean(pL, v); return 1;
            }, "IsClient");
            return 1;
        }
        // IsStudio: el juego lanzado desde el editor de Godot es el "Play" de
        // Studio; un juego exportado no. IsEdit: los scripts no corren en edicion.
        if (strcmp(key, "IsStudio") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                lua_pushboolean(pL, OS::get_singleton()->has_feature("editor") ? 1 : 0); return 1;
            }, "IsStudio");
            return 1;
        }
        if (strcmp(key, "IsEdit") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int { lua_pushboolean(pL, 0); return 1; }, "IsEdit");
            return 1;
        }
        if (strcmp(key, "IsServer") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                lua_getglobal(pL, "__IS_SERVER"); bool v = lua_toboolean(pL, -1) != 0; lua_pop(pL, 1);
                lua_pushboolean(pL, v); return 1;
            }, "IsServer");
            return 1;
        }
        if (strcmp(key, "IsStudio") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                lua_pushboolean(pL, Engine::get_singleton()->is_editor_hint()); return 1;
            }, "IsStudio");
            return 1;
        }
        // RunService:BindToRenderStep(nombre, prioridad, fn) / UnbindFromRenderStep(nombre)
        if (strcmp(key, "BindToRenderStep") == 0) {
            lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)rs->get_instance_id());
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                const uint64_t id = (uint64_t)(uintptr_t)lua_touserdata(pL, lua_upvalueindex(1));
                RunService* r = gl_cast<RunService>(ObjectDB::get_instance(ObjectID(id)));
                const char* nombre = gl_str(pL, 2);
                const int prioridad = (int)gl_num_or(pL, 3, 0);   // numero o Enum.RenderPriority.X
                luaL_checktype(pL, 4, LUA_TFUNCTION);
                if (!r) return 0;
                const int ref = gl_ref(pL, 4);
                r->bind_render_step(String::utf8(nombre), prioridad, gl_main_of(pL), ref);
                return 0;
            }, "BindToRenderStep", 1);
            return 1;
        }
        if (strcmp(key, "UnbindFromRenderStep") == 0) {
            lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)rs->get_instance_id());
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                const uint64_t id = (uint64_t)(uintptr_t)lua_touserdata(pL, lua_upvalueindex(1));
                RunService* r = gl_cast<RunService>(ObjectDB::get_instance(ObjectID(id)));
                if (r) r->unbind_render_step(String::utf8(gl_str(pL, 2)));
                return 0;
            }, "UnbindFromRenderStep", 1);
            return 1;
        }
        // ── Señales por frame ─────────────────────────────────────────
        // Roblox tiene los tres nombres clasicos y sus equivalentes modernos,
        // que son EL MISMO evento con otro nombre (los nuevos son los que
        // recomienda la documentacion, y un place moderno usa esos):
        //     RenderStepped == PreRender
        //     Stepped       == PreSimulation
        //     Heartbeat     == PostSimulation
        // Se traduce aqui al nombre clasico y ya todo lo de abajo (conectar,
        // desconectar, Wait) funciona igual sin duplicar nada.
        const char* canon = key;
        if      (strcmp(key, "PreRender")      == 0) canon = "RenderStepped";
        else if (strcmp(key, "PreSimulation")  == 0) canon = "Stepped";
        else if (strcmp(key, "PostSimulation") == 0) canon = "Heartbeat";
        if (strcmp(canon, "Heartbeat") == 0 || strcmp(canon, "RenderStepped") == 0 || strcmp(canon, "Stepped") == 0) {
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)rs);
            lua_pushstring(L, canon);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RunService* rs_ptr = static_cast<RunService*>(lua_touserdata(pL, lua_upvalueindex(1)));
                const char* ev = lua_tostring(pL, lua_upvalueindex(2));
                int fn_pos = -1;
                for (int ai = 1; ai <= lua_gettop(pL); ai++) {
                    if (lua_isfunction(pL, ai)) { fn_pos = ai; break; }
                }
                if (fn_pos == -1 || !rs_ptr) return 0;
                lua_State* main_L = gl_main_of(pL);
                if (!main_L) main_L = pL;
                lua_pushvalue(pL, fn_pos);
                int ref = gl_ref(pL, -1);
                lua_pop(pL, 1);
                if      (strcmp(ev, "Heartbeat")     == 0) rs_ptr->add_heartbeat(main_L, ref);
                else if (strcmp(ev, "RenderStepped") == 0) rs_ptr->add_render_stepped(main_L, ref);
                else if (strcmp(ev, "Stepped")       == 0) rs_ptr->add_stepped(main_L, ref);
                // Return connection object: { Disconnect(), Connected }
                // Lleva TAMBIEN la VM: el ref solo identifica dentro de su VM
                // (ver disconnect_by_ref).
                lua_newtable(pL);
                lua_pushlightuserdata(pL, (void*)rs_ptr);
                lua_pushstring(pL, ev);
                lua_pushinteger(pL, ref);
                lua_pushlightuserdata(pL, (void*)main_L);
                lua_pushcclosure(pL, [](lua_State* dL) -> int {
                    RunService* rs = static_cast<RunService*>(lua_touserdata(dL, lua_upvalueindex(1)));
                    const char* signal = lua_tostring(dL, lua_upvalueindex(2));
                    int r = (int)lua_tointeger(dL, lua_upvalueindex(3));
                    lua_State* vm = static_cast<lua_State*>(lua_touserdata(dL, lua_upvalueindex(4)));
                    if (rs) rs->disconnect_by_ref(signal, r, vm);
                    return 0;
                }, "Disconnect", 4);
                lua_setfield(pL, -2, "Disconnect");
                lua_getfield(pL, -1, "Disconnect");   // alias legacy :disconnect()
                lua_setfield(pL, -2, "disconnect");
                lua_pushboolean(pL, true);
                lua_setfield(pL, -2, "Connected");
                return 1;
            }, "Connect", 2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            // Once — calls the function only the first time, then disconnects / llama la función solo la primera vez, luego se desconecta
            lua_pushlightuserdata(L, (void*)rs);
            lua_pushstring(L, canon);   // PostSimulation/PreRender -> nombre real
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RunService* rs_ptr = static_cast<RunService*>(lua_touserdata(pL, lua_upvalueindex(1)));
                const char* ev = lua_tostring(pL, lua_upvalueindex(2));
                int fn_pos = -1;
                for (int ai = 1; ai <= lua_gettop(pL); ai++) {
                    if (lua_isfunction(pL, ai)) { fn_pos = ai; break; }
                }
                if (fn_pos == -1 || !rs_ptr) return 0;
                lua_State* main_L = gl_main_of(pL);
                if (!main_L) main_L = pL;
                lua_pushvalue(pL, fn_pos);
                int ref = gl_ref(pL, -1);
                lua_pop(pL, 1);
                if      (strcmp(ev, "Heartbeat")     == 0) rs_ptr->add_heartbeat(main_L, ref, true);
                else if (strcmp(ev, "RenderStepped") == 0) rs_ptr->add_render_stepped(main_L, ref, true);
                else if (strcmp(ev, "Stepped")       == 0) rs_ptr->add_stepped(main_L, ref, true);
                // Return connection object (same shape as Connect for consistency)
                lua_newtable(pL);
                lua_pushlightuserdata(pL, (void*)rs_ptr);
                lua_pushstring(pL, ev);
                lua_pushinteger(pL, ref);
                lua_pushlightuserdata(pL, (void*)main_L);
                lua_pushcclosure(pL, [](lua_State* dL) -> int {
                    RunService* rs = static_cast<RunService*>(lua_touserdata(dL, lua_upvalueindex(1)));
                    const char* signal = lua_tostring(dL, lua_upvalueindex(2));
                    int r = (int)lua_tointeger(dL, lua_upvalueindex(3));
                    lua_State* vm = static_cast<lua_State*>(lua_touserdata(dL, lua_upvalueindex(4)));
                    if (rs) rs->disconnect_by_ref(signal, r, vm);
                    return 0;
                }, "Disconnect", 4);
                lua_setfield(pL, -2, "Disconnect");
                lua_getfield(pL, -1, "Disconnect");   // alias legacy :disconnect()
                lua_setfield(pL, -2, "disconnect");
                lua_pushboolean(pL, true);
                lua_setfield(pL, -2, "Connected");
                return 1;
            }, "Once", 2);
            lua_setfield(L, -2, "Once");
            // Wait() — yields until the next signal fires, returns delta
            lua_pushlightuserdata(L, (void*)rs);
            lua_pushstring(L, canon);   // PostSimulation/PreRender -> nombre real
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RunService* rs_ptr = static_cast<RunService*>(lua_touserdata(pL, lua_upvalueindex(1)));
                const char* ev = lua_tostring(pL, lua_upvalueindex(2));
                if (!rs_ptr) { lua_pushnumber(pL, 0); return 1; }
                lua_State* main_L = gl_main_of(pL);
                if (!main_L) main_L = pL;
                // Register this coroutine for one-shot resumption on next fire
                if      (strcmp(ev, "Heartbeat")     == 0) rs_ptr->add_heartbeat_wait(pL, main_L);
                else if (strcmp(ev, "RenderStepped") == 0) rs_ptr->add_render_stepped_wait(pL, main_L);
                else if (strcmp(ev, "Stepped")       == 0) rs_ptr->add_stepped_wait(pL, main_L);
                // Yield with sentinel so _process won't auto-resume via timer
                return gl_yield_externo(pL, "__WAIT_SIGNAL__");
            }, "Wait", 2);
            lua_setfield(L, -2, "Wait");
            return 1;
        }
    }

    // ── RobloxPart ────────────────────────────────────────────────
    RobloxPart* part = gl_cast<RobloxPart>(n);
    if (part) {
        if (strcmp(key, "Anchored")          == 0) { lua_pushboolean(L, part->get_anchored());      return 1; }
        if (strcmp(key, "CanCollide")        == 0) { lua_pushboolean(L, part->get_can_collide());   return 1; }
        if (strcmp(key, "CanTouch")          == 0) { lua_pushboolean(L, part->get_can_touch());     return 1; }
        if (strcmp(key, "CanQuery")          == 0) { lua_pushboolean(L, part->get_can_query());     return 1; }
        if (strcmp(key, "CollisionGroup")    == 0) { lua_pushstring(L, part->get_collision_group().utf8().get_data()); return 1; }
        if (strcmp(key, "CastShadow")        == 0) { lua_pushboolean(L, part->get_cast_shadow());   return 1; }
        if (strcmp(key, "Locked")            == 0) { lua_pushboolean(L, part->get_locked());        return 1; }
        if (strcmp(key, "Massless")          == 0) { lua_pushboolean(L, part->get_massless());      return 1; }
        if (strcmp(key, "Transparency")      == 0) { lua_pushnumber(L, part->get_transparency());   return 1; }
        if (strcmp(key, "Reflectance")       == 0) { lua_pushnumber(L, part->get_reflectance());    return 1; }
        // EnumItem: `parte.Material.Name` reventaba con el numero.
        if (strcmp(key, "Material")          == 0) { uis_push_enum(L, "Material", part->get_roblox_material()); return 1; }
        // MaterialVariant: el nombre (el importador lo deja en un meta).
        if (strcmp(key, "MaterialVariant")   == 0) {
            static const StringName k("MaterialVariant");
            lua_pushstring(L, part->has_meta(k) ? String(part->get_meta(k)).utf8().get_data() : "");
            return 1;
        }
        if (strcmp(key, "Shape")             == 0) { lua_pushnumber(L, gl_shape_roblox(part->get_roblox_shape())); return 1; }
        if (strcmp(key, "Density")           == 0) { lua_pushnumber(L, part->get_density());        return 1; }
        if (strcmp(key, "Friction")          == 0) { lua_pushnumber(L, part->get_friction_val());   return 1; }
        if (strcmp(key, "Elasticity")        == 0) { lua_pushnumber(L, part->get_elasticity());     return 1; }
        if (strcmp(key, "FrictionWeight")    == 0) { lua_pushnumber(L, part->get_friction_weight());return 1; }
        if (strcmp(key, "ElasticityWeight")  == 0) { lua_pushnumber(L, part->get_elasticity_weight()); return 1; }
        if (strcmp(key, "Mass")              == 0) { lua_pushnumber(L, part->get_mass_roblox());    return 1; }
        if (strcmp(key, "TopSurface")        == 0) { lua_pushnumber(L, part->get_top_surface());    return 1; }
        if (strcmp(key, "BottomSurface")     == 0) { lua_pushnumber(L, part->get_bottom_surface()); return 1; }
        if (strcmp(key, "FrontSurface")      == 0) { lua_pushnumber(L, part->get_front_surface());  return 1; }
        if (strcmp(key, "BackSurface")       == 0) { lua_pushnumber(L, part->get_back_surface());   return 1; }
        if (strcmp(key, "LeftSurface")       == 0) { lua_pushnumber(L, part->get_left_surface());   return 1; }
        if (strcmp(key, "RightSurface")      == 0) { lua_pushnumber(L, part->get_right_surface());  return 1; }

        if (strcmp(key, "Color") == 0 || strcmp(key, "BrickColor") == 0) {
            Color c = part->get_color();
            push_color3(L, c.r, c.g, c.b); return 1;
        }
        if (strcmp(key, "Size") == 0) {
            Vector3 s = part->get_size();
            push_vector3(L, s.x, s.y, s.z); return 1;
        }
        if (strcmp(key, "Position") == 0) {
            Vector3 p = gl_node_xf(part).origin;
            push_vector3(L, p.x, p.y, p.z); return 1;
        }
        if (strcmp(key, "Orientation") == 0) {
            Vector3 r = part->get_rotation_degrees();
            push_vector3(L, r.x, r.y, r.z); return 1;
        }
        if (strcmp(key, "Velocity") == 0) {
            Vector3 v = part->get_linear_velocity();
            push_vector3(L, v.x, v.y, v.z); return 1;
        }
        if (strcmp(key, "AngularVelocity") == 0) {
            Vector3 v = part->get_angular_velocity();
            push_vector3(L, v.x, v.y, v.z); return 1;
        }

        if (strcmp(key, "Touched") == 0 || strcmp(key, "TouchEnded") == 0) {
            const char* sig_name = key; // capture the signal name
            lua_newtable(L);
            lua_pushlightuserdata(L, n);
            lua_pushstring(L, sig_name);
            lua_pushcclosure(L, [](lua_State* L) -> int {
                Node* target = (Node*)lua_touserdata(L, lua_upvalueindex(1));
                const char* sig  = lua_tostring(L, lua_upvalueindex(2));
                if (lua_isfunction(L, 2)) {
                    lua_pushvalue(L, 2);
                    int ref = gl_ref(L, -1);
                    int64_t ptr_L = (int64_t)gl_main_of(L);
                    if (target && target->has_signal(StringName(sig))) {
                        // El contact monitor es caro con miles de parts: se
                        // enciende recien aqui, cuando el juego usa Touched.
                        if (RobloxPart* rp = gl_cast<RobloxPart>(target))
                            rp->_ensure_touch_monitoring();
                        target->connect(StringName(sig),
                            Callable(target, "_on_luau_part_touched").bind(ref, ptr_L));
                    }
                    // ── DEVOLVER LA CONEXION ─────────────────────────
                    // Antes esto devolvia NADA. El patron mas comun de Roblox
                    // para un golpe de un solo uso es
                    //   local c; c = part.Touched:Connect(function() c:Disconnect() end)
                    // y con nil ahi el callback reventaba en cada contacto, no
                    // se desconectaba nunca y lo que tenia que destruirse
                    // seguia vivo. RobloxPart no tiene _gl_disconnect, asi que
                    // no vale _gl_push_connection: hay que deshacer la conexion
                    // de Godot reconstruyendo el mismo Callable con sus binds.
                    lua_newtable(L);
                    lua_pushboolean(L, 1);
                    lua_setfield(L, -2, "Connected");
                    lua_pushlightuserdata(L, (void*)(uintptr_t)(target ? (uint64_t)target->get_instance_id() : 0));
                    lua_pushstring(L, sig);
                    lua_pushinteger(L, ref);
                    lua_pushlightuserdata(L, (void*)(uintptr_t)ptr_L);
                    lua_pushcclosure(L, [](lua_State* dL) -> int {
                        const uint64_t id  = (uint64_t)(uintptr_t)lua_touserdata(dL, lua_upvalueindex(1));
                        const char*    s   = lua_tostring(dL, lua_upvalueindex(2));
                        const int      r   = (int)lua_tointeger(dL, lua_upvalueindex(3));
                        const int64_t  vm  = (int64_t)(uintptr_t)lua_touserdata(dL, lua_upvalueindex(4));
                        Node* n = gl_cast<Node>(ObjectDB::get_instance(id));
                        if (n && s) {
                            const Callable cb = Callable(n, "_on_luau_part_touched").bind(r, vm);
                            if (n->is_connected(StringName(s), cb)) n->disconnect(StringName(s), cb);
                        }
                        if (lua_istable(dL, 1)) { lua_pushboolean(dL, 0); lua_setfield(dL, 1, "Connected"); }
                        return 0;
                    }, "Disconnect", 4);
                    lua_setfield(L, -2, "Disconnect");
                    lua_getfield(L, -1, "Disconnect");   // alias legacy :disconnect()
                    lua_setfield(L, -2, "disconnect");
                    return 1;
                }
                return 0;
            }, "Connect", 2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }

        // ── Propiedades del CONJUNTO (assembly) ──────────────────────
        // En Roblox todas estas hablan del conjunto entero, no de la pieza: da
        // igual a que miembro se las pidas, contestan por la raiz. Una pieza
        // suelta tambien es un conjunto, de un solo miembro.
        if (strcmp(key, "AssemblyLinearVelocity") == 0) {
            RobloxPart* r = gl_raiz_de(part);
            Vector3 v = r->get_linear_velocity();
            push_vector3(L, v.x, v.y, v.z); return 1;
        }
        if (strcmp(key, "AssemblyAngularVelocity") == 0) {
            RobloxPart* r = gl_raiz_de(part);
            Vector3 v = r->get_angular_velocity();
            push_vector3(L, v.x, v.y, v.z); return 1;
        }
        if (strcmp(key, "AssemblyRootPart") == 0) {
            wrap_node(L, gl_raiz_de(part)); return 1;
        }
        if (strcmp(key, "AssemblyMass") == 0) {
            lua_pushnumber(L, (double)gl_raiz_de(part)->get_mass()); return 1;
        }
        if (strcmp(key, "AssemblyCenterOfMass") == 0) {
            RobloxPart* r = gl_raiz_de(part);
            const Vector3 c = r->is_inside_tree()
                ? r->get_global_transform().xform(r->get_center_of_mass())
                : r->get_center_of_mass();
            push_vector3(L, c.x, c.y, c.z); return 1;
        }
        if (strcmp(key, "Mass") == 0) {
            // BasePart.Mass en Roblox es la masa de ESA pieza, no la del conjunto.
            lua_pushnumber(L, (double)part->gl_masa_propia()); return 1;
        }
        if (strcmp(key, "RootPriority") == 0) {
            lua_pushnumber(L, (double)part->get_root_priority()); return 1;
        }

        // Methods
        if (strcmp(key, "ApplyImpulse") == 0) {
            lua_pushlightuserdata(L, (void*)part);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RobloxPart* p = (RobloxPart*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!p) return 0;
                // Un Vector3 de este motor es USERDATA con metatabla "Vector3"
                // (ver push_vector3), NO una tabla: con solo `lua_istable` esta
                // rama no entraba NUNCA y el impulso salia (0,0,0) sin decir ni
                // pio. `part:ApplyImpulse(Vector3.new(0,500,0))` no hacia nada.
                float ix = 0; float iy = 0; float iz = 0;
                if (LuauVector3* v = check_vector3(pL, 2)) {
                    ix = v->x; iy = v->y; iz = v->z;
                } else if (lua_istable(pL,2)) {
                    lua_getfield(pL,2,"X"); ix=(float)lua_tonumber(pL,-1); lua_pop(pL,1);
                    lua_getfield(pL,2,"Y"); iy=(float)lua_tonumber(pL,-1); lua_pop(pL,1);
                    lua_getfield(pL,2,"Z"); iz=(float)lua_tonumber(pL,-1); lua_pop(pL,1);
                }
                // En Roblox el impulso va al CONJUNTO, no a la pieza: darselo a
                // un miembro congelado no hacia absolutamente nada.
                gl_raiz_de(p)->apply_central_impulse(Vector3(ix,iy,iz));
                return 0;
            }, "ApplyImpulse", 1);
            return 1;
        }
        if (strcmp(key, "ApplyAngularImpulse") == 0) {
            lua_pushlightuserdata(L, (void*)part);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RobloxPart* p = (RobloxPart*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!p) return 0;
                // Mismo caso que ApplyImpulse: el Vector3 es userdata.
                float ix = 0; float iy = 0; float iz = 0;
                if (LuauVector3* v = check_vector3(pL, 2)) {
                    ix = v->x; iy = v->y; iz = v->z;
                } else if (lua_istable(pL,2)) {
                    lua_getfield(pL,2,"X"); ix=(float)lua_tonumber(pL,-1); lua_pop(pL,1);
                    lua_getfield(pL,2,"Y"); iy=(float)lua_tonumber(pL,-1); lua_pop(pL,1);
                    lua_getfield(pL,2,"Z"); iz=(float)lua_tonumber(pL,-1); lua_pop(pL,1);
                }
                gl_raiz_de(p)->apply_torque_impulse(Vector3(ix,iy,iz));
                return 0;
            }, "ApplyAngularImpulse", 1);
            return 1;
        }
        // ── Network Ownership (1.14.7) ──
        if (strcmp(key, "SetNetworkOwner") == 0) {
            lua_pushlightuserdata(L, (void*)part);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RobloxPart* p = (RobloxPart*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!p || !p->has_meta("_gl_netid")) return 0;
                Node* ns = _gl_find_network_service(p);
                if (!ns || _gl_net_mode(ns) != 1) return 0;   // solo el servidor asigna dueño
                Node* player = _gl_node_from_lua(pL, 2);       // nil = servidor
                int peer = player ? (int)(ns->call("peer_for_player_node", player)) : 0;
                ns->call("net_set_owner", (int64_t)p->get_meta("_gl_netid"), peer, false);
                return 0;
            }, "SetNetworkOwner", 1);
            return 1;
        }
        if (strcmp(key, "GetNetworkOwner") == 0) {
            lua_pushlightuserdata(L, (void*)part);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RobloxPart* p = (RobloxPart*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!p) { lua_pushnil(pL); return 1; }
                int owner = p->has_meta("_gl_owner") ? (int)p->get_meta("_gl_owner") : 0;
                if (owner == 0) { lua_pushnil(pL); return 1; }   // servidor → nil
                Node* ns = _gl_find_network_service(p);
                Object* o = ns ? (Object*)ns->call("player_node_for_peer", owner) : nullptr;
                Node* plr = gl_cast<Node>(o);
                if (plr) _gl_push_node(pL, plr); else lua_pushnil(pL);
                return 1;
            }, "GetNetworkOwner", 1);
            return 1;
        }
        if (strcmp(key, "SetNetworkOwnershipAuto") == 0) {
            lua_pushlightuserdata(L, (void*)part);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RobloxPart* p = (RobloxPart*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!p || !p->has_meta("_gl_netid")) return 0;
                Node* ns = _gl_find_network_service(p);
                if (!ns || _gl_net_mode(ns) != 1) return 0;
                ns->call("net_set_owner", (int64_t)p->get_meta("_gl_netid"), 0, true);   // auto = servidor (v1)
                return 0;
            }, "SetNetworkOwnershipAuto", 1);
            return 1;
        }
        if (strcmp(key, "GetNetworkOwnershipAuto") == 0) {
            lua_pushlightuserdata(L, (void*)part);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RobloxPart* p = (RobloxPart*)lua_touserdata(pL, lua_upvalueindex(1));
                // Auto es el modo por defecto en Roblox: true si no se fijó dueño a mano.
                bool a = p && (!p->has_meta("_gl_owner_auto") || (bool)p->get_meta("_gl_owner_auto"));
                lua_pushboolean(pL, a ? 1 : 0); return 1;
            }, "GetNetworkOwnershipAuto", 1);
            return 1;
        }
        if (strcmp(key, "IsNetworkOwner") == 0) {
            lua_pushlightuserdata(L, (void*)part);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RobloxPart* p = (RobloxPart*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!p) { lua_pushboolean(pL, 0); return 1; }
                Node* ns = _gl_find_network_service(p);
                if (!ns || _gl_net_mode(ns) == 0) { lua_pushboolean(pL, 1); return 1; }   // single-player: soy dueño
                int64_t netid = p->has_meta("_gl_netid") ? (int64_t)p->get_meta("_gl_netid") : 0;
                bool own = (bool)(ns->call("net_is_owner", netid));
                lua_pushboolean(pL, own ? 1 : 0); return 1;
            }, "IsNetworkOwner", 1);
            return 1;
        }
        if (strcmp(key, "CanSetNetworkOwnership") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int { lua_pushboolean(pL, 1); return 1; }, "CanSetNetworkOwnership");
            return 1;
        }
        if (strcmp(key, "GetMass") == 0) {
            lua_pushlightuserdata(L, (void*)part);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RobloxPart* p = (RobloxPart*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushnumber(pL, p ? p->get_mass_roblox() : 0.0f);
                return 1;
            }, "GetMass", 1);
            return 1;
        }
        if (strcmp(key, "SetNetworkOwner") == 0) {
            lua_pushcclosure(L, [](lua_State* pL) -> int { return 0; }, "SetNetworkOwner", 0);
            return 1;
        }
        if (strcmp(key, "GetNetworkOwner") == 0) {
            lua_pushcclosure(L, [](lua_State* pL) -> int { lua_pushnil(pL); return 1; }, "GetNetworkOwner", 0);
            return 1;
        }
        // ── Seat / VehicleSeat ────────────────────────────────────────
        // Eran Parts sin mas: nadie se sentaba y Occupant no existia, asi que
        // todo vehiculo (seat:GetPropertyChangedSignal("Occupant")) estaba muerto.
        if (gl_es_asiento(part)) {
            if (strcmp(key, "Occupant") == 0) {
                Node* h = gl_ocupante_de(part);
                if (h) wrap_node(L, h); else lua_pushnil(L);
                return 1;
            }
            if (strcmp(key, "Disabled") == 0) { lua_pushboolean(L, gl_asiento_deshabilitado(part) ? 1 : 0); return 1; }
            if (strcmp(key, "Sit") == 0) {
                lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)part->get_instance_id());
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    Node* a = gl_cast<Node>(ObjectDB::get_instance(ObjectID((uint64_t)(uintptr_t)lua_touserdata(pL, lua_upvalueindex(1)))));
                    Humanoid* h = _gl_has_metatable(pL, 2, "GodotObject")
                        ? gl_cast<Humanoid>(gow_node((GodotObjectWrapper*)lua_touserdata(pL, 2)))
                        : nullptr;
                    if (a && h) h->gl_sentar(a);
                    return 0;
                }, "Sit", 1);
                return 1;
            }
            if (gl_es_asiento_vehiculo(part)) {
                auto meta_num = [&](const char* k, double def) -> double {
                    const StringName sn(k);
                    return part->has_meta(sn) ? (double)part->get_meta(sn) : def;
                };
                if (strcmp(key, "Throttle") == 0 || strcmp(key, "ThrottleFloat") == 0) { lua_pushnumber(L, meta_num("_gl_throttle", 0)); return 1; }
                if (strcmp(key, "Steer") == 0    || strcmp(key, "SteerFloat") == 0)    { lua_pushnumber(L, meta_num("_gl_steer", 0));    return 1; }
                if (strcmp(key, "MaxSpeed") == 0)  { lua_pushnumber(L, meta_num("MaxSpeed", 25));  return 1; }
                if (strcmp(key, "Torque") == 0)    { lua_pushnumber(L, meta_num("Torque", 10));    return 1; }
                if (strcmp(key, "TurnSpeed") == 0) { lua_pushnumber(L, meta_num("TurnSpeed", 1));  return 1; }
                if (strcmp(key, "HeadsUpDisplay") == 0) { lua_pushboolean(L, meta_num("HeadsUpDisplay", 1) != 0.0); return 1; }
                if (strcmp(key, "AreHingesDetected") == 0) { lua_pushnumber(L, 0); return 1; }
            }
        }

        // GetTouchingParts: las piezas cuya forma se cruza con la de esta. Antes
        // devolvia siempre {} y todo "¿hay algo encima de la placa?" fallaba.
        if (strcmp(key, "GetTouchingParts") == 0) {
            lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)part->get_instance_id());
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                const uint64_t id = (uint64_t)(uintptr_t)lua_touserdata(pL, lua_upvalueindex(1));
                RobloxPart* p = gl_cast<RobloxPart>(ObjectDB::get_instance(ObjectID(id)));
                lua_newtable(pL);
                if (!p) return 1;
                Vector<Node*> toques;
                p->gl_piezas_tocando(toques);
                for (int i = 0; i < toques.size(); i++) {
                    wrap_node(pL, toques[i]);
                    lua_rawseti(pL, -2, i + 1);
                }
                return 1;
            }, "GetTouchingParts", 1);
            return 1;
        }
        if (strcmp(key, "IntersectsWith") == 0) {
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                lua_newtable(pL); return 1;
            }, key, 0);
            return 1;
        }
    }

    // ── RobloxWorkspace ────────────────────────────────────────────
    RobloxWorkspace* ws = gl_cast<RobloxWorkspace>(n);
    if (ws) {
        if (strcmp(key, "Gravity")                  == 0) { lua_pushnumber(L, ws->get_gravity());                      return 1; }
        if (strcmp(key, "FallenPartsDestroyHeight")  == 0) { lua_pushnumber(L, ws->get_effective_fall_height());        return 1; }
        if (strcmp(key, "AutoFallHeight")            == 0) { lua_pushboolean(L, ws->get_auto_fall_height() ? 1 : 0);    return 1; }
        if (strcmp(key, "FallHeightMargin")          == 0) { lua_pushnumber(L, ws->get_fall_height_margin());           return 1; }
        if (strcmp(key, "StreamingEnabled")          == 0) { lua_pushboolean(L, ws->get_streaming_enabled());           return 1; }
        if (strcmp(key, "AirDensity")                == 0) { lua_pushnumber(L, ws->get_air_density());                  return 1; }
        if (strcmp(key, "TouchesUseCollisionGroups") == 0) { lua_pushboolean(L, ws->get_touches_use_collision_groups());return 1; }
        // Reloj sincronizado (1.14.8)
        if (strcmp(key, "DistributedGameTime")       == 0) { lua_pushnumber(L, gl_distributed_game_time());            return 1; }
        if (strcmp(key, "GetServerTimeNow") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int { lua_pushnumber(pL, gl_server_time_now()); return 1; }, "GetServerTimeNow");
            return 1;
        }
        // workspace:BulkMoveTo(piezas, cframes, modo): como escribir el CFrame de
        // cada una, por el mismo camino (replica y avisos de cambio incluidos).
        if (strcmp(key, "BulkMoveTo") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                if (!lua_istable(pL, 2) || !lua_istable(pL, 3)) return 0;
                const int total = (int)lua_objlen(pL, 2);
                for (int i = 1; i <= total; i++) {
                    lua_rawgeti(pL, 2, i);   // la pieza
                    lua_rawgeti(pL, 3, i);   // su CFrame
                    if (lua_isuserdata(pL, -2) && !lua_isnil(pL, -1)) lua_setfield(pL, -2, "CFrame");
                    else lua_pop(pL, 1);
                    lua_pop(pL, 1);
                }
                return 0;
            }, "BulkMoveTo");
            return 1;
        }
        // ── API del menu en Luau (1.17): metodos ya bindeados en ClassDB ─────
        // El puente funciona por lista explicita, y la pestana Settings del menu
        // .luau se escribio contra estos metodos SIN que estuvieran en la lista:
        // cada clic (cambiar de modo, V-Sync, ajustes Custom...) moria con
        // "X is not a valid member of Workspace". Se exponen con UNA closure
        // generica via callv() en vez de 12 copias a mano.
        static const char* kWsMenuApi[] = {
            "SetGraphicsMode","GetGraphicsMode","SetAutoTargetFPS","GetAutoTargetFPS",
            "GetMaxFPS","GetMasterVolume","SetVSync","GetVSync",
            "SetGraphicsSetting","GetGraphicsSetting","TakeScreenshot","ListScreenshots",
            "DeleteScreenshot","GetEngineCredit","GetEngineLink","OpenEngineLink",
            "GetFPS","SetMasterVolume","GetMemoryMB","ToggleChat","SetMenuOpen","GetMenuOpen","IsMouseDown",
            // 1.17: distancia de vision por nivel (hasta 32768 studs) y LOD de
            // fisica. Estaban bindeados en ClassDB pero NO aqui, o sea que desde
            // Luau daban "is not a valid member of Workspace" — la misma trampa
            // que describe el comentario de arriba, otra vez.
            "SetViewDistance","GetViewDistance","GetViewDistanceNow",
            "SetPhysicsLOD","GetPhysicsLOD",
            // 1.17: anti-aliasing (0=Off 1=FXAA 2=MSAA2x 3=MSAA4x 4=MSAA8x)
            "SetAntiAliasing","GetAntiAliasing",
        };
        for (int mi = 0; mi < (int)(sizeof(kWsMenuApi)/sizeof(kWsMenuApi[0])); mi++) {
            if (strcmp(key, kWsMenuApi[mi]) != 0) continue;
            lua_pushlightuserdata(L, (void*)ws);
            lua_pushstring(L, key);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Node* w = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
                const char* mname = lua_tostring(pL, lua_upvalueindex(2));
                if (!w || !mname) return 0;
                Array args;   // arg 1 es self (llamada con ':'): se salta
                for (int ai = 2; ai <= lua_gettop(pL); ai++) {
                    if      (lua_isboolean(pL, ai)) args.push_back(lua_toboolean(pL, ai) != 0);
                    else if (lua_isnumber(pL, ai))  args.push_back(lua_tonumber(pL, ai));
                    else if (lua_isstring(pL, ai))  args.push_back(String(lua_tostring(pL, ai)));
                }
                Variant ret = w->callv(StringName(mname), args);
                switch (ret.get_type()) {
                    case Variant::BOOL:   lua_pushboolean(pL, (bool)ret ? 1 : 0); return 1;
                    case Variant::INT:    lua_pushnumber(pL, (double)(int64_t)ret); return 1;
                    case Variant::FLOAT:  lua_pushnumber(pL, (double)ret); return 1;
                    case Variant::STRING: lua_pushstring(pL, String(ret).utf8().get_data()); return 1;
                    case Variant::PACKED_STRING_ARRAY: {
                        PackedStringArray a2 = ret;
                        lua_newtable(pL);
                        for (int k = 0; k < a2.size(); k++) {
                            lua_pushstring(pL, String(a2[k]).utf8().get_data());
                            lua_rawseti(pL, -2, k + 1);
                        }
                        return 1;
                    }
                    default: return 0;
                }
            }, key, 2);
            return 1;
        }
        // Calidad gráfica 1..10 (1.15): la usa el módulo de ajustes en Modules.
        if (strcmp(key, "SetGraphicsQuality") == 0) {
            lua_pushlightuserdata(L, (void*)ws);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RobloxWorkspace* w = (RobloxWorkspace*)lua_touserdata(pL, lua_upvalueindex(1));
                if (w) w->set_graphics_quality((int)gl_num(pL, 2));
                return 0;
            }, "SetGraphicsQuality", 1);
            return 1;
        }
        if (strcmp(key, "GetGraphicsQuality") == 0) {
            lua_pushlightuserdata(L, (void*)ws);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RobloxWorkspace* w = (RobloxWorkspace*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushnumber(pL, w ? w->get_graphics_quality() : 8);
                return 1;
            }, "GetGraphicsQuality", 1);
            return 1;
        }
        // ── Menu abierto: la rueda deja de mover la camara ───────────────
        // Sin esto, con el menu .luau abierto la rueda seguia acercando/alejando
        // la camara en vez de desplazar la lista, que es justo lo que hace Roblox.
        // El flag lo leen RobloxPlayer y Humanoid (gl_runtime.h).
        if (strcmp(key, "SetMenuOpen") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                gl_set_menu_open(lua_toboolean(pL, 2) != 0);
                return 0;
            }, "SetMenuOpen");
            return 1;
        }
        if (strcmp(key, "GetMenuOpen") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                lua_pushboolean(pL, gl_menu_open() ? 1 : 0);
                return 1;
            }, "GetMenuOpen");
            return 1;
        }
        // Ajustes globales del motor que el menú (Modules/Menu/Settings) aplica
        // ahora que el menú es Lua editable (1.15). Son globales → no necesitan
        // buscar al jugador. Silenciosos si el valor está fuera de rango.
        if (strcmp(key, "SetMaxFPS") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                int fps = (int)gl_num(pL, 2);   // 0 = ilimitado
                Engine::get_singleton()->set_max_fps(fps < 0 ? 0 : fps);
                return 0;
            }, "SetMaxFPS");
            return 1;
        }
        if (strcmp(key, "SetMasterVolume") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                double v = gl_num(pL, 2);        // 0..1
                if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0;
                AudioServer* as = AudioServer::get_singleton();
                if (as) {
                    as->set_bus_mute(0, v <= 0.005);
                    as->set_bus_volume_db(0, (float)UtilityFunctions::linear_to_db(v <= 0.005 ? 0.0001 : v));
                }
                return 0;
            }, "SetMasterVolume");
            return 1;
        }
    }

    // ── RemoteEventNode ───────────────────────────────────────────────────
    RemoteEventNode* re = gl_cast<RemoteEventNode>(n);
    if (re) {
        if (strcmp(key, "FireServer") == 0) {
            lua_pushlightuserdata(L, (void*)re);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RemoteEventNode* rev = (RemoteEventNode*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!rev) return 0;
                int nargs = lua_gettop(pL) - 1;
                rev->fire_server(pL, 2, nargs < 0 ? 0 : nargs);
                return 0;
            }, "FireServer", 1);
            return 1;
        }
        if (strcmp(key, "FireClient") == 0) {
            lua_pushlightuserdata(L, (void*)re);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RemoteEventNode* rev = (RemoteEventNode*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!rev) return 0;
                // Como Roblox: el 1er argumento DEBE ser un Player (1.14.9).
                Node* fc_plr = _gl_node_from_lua(pL, 2);
                if (!fc_plr || !fc_plr->is_class("PlayerObject")) { luaL_error(pL, "FireClient: first argument must be a Player"); return 0; }
                int nargs = lua_gettop(pL) - 2;
                rev->fire_client(pL, 2, 3, nargs < 0 ? 0 : nargs);
                return 0;
            }, "FireClient", 1);
            return 1;
        }
        if (strcmp(key, "FireAllClients") == 0) {
            lua_pushlightuserdata(L, (void*)re);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RemoteEventNode* rev = (RemoteEventNode*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!rev) return 0;
                int nargs = lua_gettop(pL) - 1;
                rev->fire_all_clients(pL, 2, nargs < 0 ? 0 : nargs);
                return 0;
            }, "FireAllClients", 1);
            return 1;
        }
        if (strcmp(key, "OnServerEvent") == 0 || strcmp(key, "OnClientEvent") == 0) {
            bool is_server = (strcmp(key, "OnServerEvent") == 0);
            lua_newtable(L);
            // Connect(fn) / Once(fn) comparten closure (upvalue2 = ¿once?)
            for (int variant = 0; variant < 2; variant++) {
                lua_pushlightuserdata(L, (void*)re);
                lua_pushboolean(L, variant);            // 0 = Connect, 1 = Once
                lua_pushboolean(L, is_server ? 1 : 0);  // lado
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    RemoteEventNode* rev = (RemoteEventNode*)lua_touserdata(pL, lua_upvalueindex(1));
                    bool once = lua_toboolean(pL, lua_upvalueindex(2));
                    bool srv  = lua_toboolean(pL, lua_upvalueindex(3));
                    int fn_pos = -1;
                    for (int ai = 1; ai <= lua_gettop(pL); ai++)
                        if (lua_isfunction(pL, ai)) { fn_pos = ai; break; }
                    if (fn_pos == -1 || !rev) return 0;
                    lua_State* mL = gl_main_of(pL);
                    if (!mL) mL = pL;
                    int ref = gl_ref(pL, fn_pos);
                    if (srv) rev->add_server_cb(mL, ref, once);
                    else     rev->add_client_cb(mL, ref, once);
                    _gl_push_connection(pL, rev, ref); return 1;
                }, variant ? "Once" : "Connect", 3);
                lua_setfield(L, -2, variant ? "Once" : "Connect");
            }
            // Wait() — cede la corrutina hasta el próximo disparo; devuelve los args
            lua_pushlightuserdata(L, (void*)re);
            lua_pushboolean(L, is_server ? 1 : 0);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RemoteEventNode* rev = (RemoteEventNode*)lua_touserdata(pL, lua_upvalueindex(1));
                bool srv = lua_toboolean(pL, lua_upvalueindex(2));
                if (!rev) { lua_pushnil(pL); return 1; }
                lua_State* mL = gl_main_of(pL);
                if (!mL) mL = pL;
                if (srv) rev->add_server_wait(pL, mL);
                else     rev->add_client_wait(pL, mL);
                return gl_yield_externo(pL, "__WAIT_SIGNAL__");
            }, "Wait", 2);
            lua_setfield(L, -2, "Wait");
            return 1;
        }
    }

    // ── RemoteFunctionNode ────────────────────────────────────────────────
    RemoteFunctionNode* rf = gl_cast<RemoteFunctionNode>(n);
    if (rf) {
        if (strcmp(key, "InvokeServer") == 0) {
            lua_pushlightuserdata(L, (void*)rf);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RemoteFunctionNode* rfn = (RemoteFunctionNode*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!rfn) { lua_pushnil(pL); return 1; }
                int nargs = lua_gettop(pL) - 1; if (nargs < 0) nargs = 0;
                Node* ns = _gl_find_network_service(rfn);
                // Cliente en sesión de red: mandar al servidor, ceder el hilo y
                // reanudar con la respuesta (1.14.6). Host/single: correr local.
                if (_gl_net_mode(ns) == 2 && rfn->is_inside_tree() && gl_add_invoke_wait_hook()) {
                    Node* sn = gl_nodo_script_de(pL);
                    lua_State* main_L = gl_main_of(pL);
                    if (!sn) { lua_pushnil(pL); return 1; }
                    if (!main_L) main_L = pL;
                    int64_t call_id = gl_net_new_call_id();
                    Array args = gl_net_encode_args(pL, 2, nargs);
                    ns->call("net_invoke_server", String(rfn->get_path()), (int)call_id, args);
                    lua_pushthread(pL); int ref = gl_ref(pL, -1); lua_pop(pL, 1);
                    gl_add_invoke_wait_hook()(sn, pL, main_L, ref, call_id, 0.0);   // 0 = esperar como Roblox (el fallo llega si el receptor se cae)
                    return gl_yield_externo(pL, "__WAIT_INVOKE__");
                }
                // Local: el manejador corre como tarea y recibe el Player
                // local de primer argumento, como en Roblox.
                return gl_invocar_local(pL, 2, nargs, rfn->server_invoke_L, rfn->server_invoke_ref,
                                        _gl_find_local_player(rfn), true, "InvokeServer");
            }, "InvokeServer", 1);
            return 1;
        }
        if (strcmp(key, "InvokeClient") == 0) {
            lua_pushlightuserdata(L, (void*)rf);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RemoteFunctionNode* rfn = (RemoteFunctionNode*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!rfn) { lua_pushnil(pL); return 1; }
                int nargs = lua_gettop(pL) - 2; if (nargs < 0) nargs = 0;
                Node* ns = _gl_find_network_service(rfn);
                // Servidor invocando a un cliente concreto por red (1.14.6). Si el
                // destino es el jugador del host, se corre local.
                if (_gl_net_mode(ns) == 1 && rfn->is_inside_tree() && gl_add_invoke_wait_hook()) {
                    Node* player = _gl_node_from_lua(pL, 2);
                    if (!player) { lua_pushnil(pL); return 1; }
                    int peer = (int)(ns->call("peer_for_player_node", player));
                    int myid = (int)(ns->call("get_peer_id"));
                    if (peer == myid && myid != 0)
                        return gl_invocar_local(pL, 3, nargs, rfn->client_invoke_L, rfn->client_invoke_ref,
                                                nullptr, false, "InvokeClient");
                    if (peer <= 0) { lua_pushnil(pL); return 1; }
                    Node* sn = gl_nodo_script_de(pL);
                    lua_State* main_L = gl_main_of(pL);
                    if (!sn) { lua_pushnil(pL); return 1; }
                    if (!main_L) main_L = pL;
                    int64_t call_id = gl_net_new_call_id();
                    Array args = gl_net_encode_args(pL, 3, nargs);
                    ns->call("net_invoke_client", peer, String(rfn->get_path()), (int)call_id, args);
                    lua_pushthread(pL); int ref = gl_ref(pL, -1); lua_pop(pL, 1);
                    gl_add_invoke_wait_hook()(sn, pL, main_L, ref, call_id, 0.0);   // 0 = esperar como Roblox (el fallo llega si el receptor se cae)
                    return gl_yield_externo(pL, "__WAIT_INVOKE__");
                }
                return gl_invocar_local(pL, 3, nargs, rfn->client_invoke_L, rfn->client_invoke_ref,
                                        nullptr, false, "InvokeClient");
            }, "InvokeClient", 1);
            return 1;
        }
    }

    // ── BindableEventNode ─────────────────────────────────────────────────
    BindableEventNode* be = gl_cast<BindableEventNode>(n);
    if (be) {
        if (strcmp(key, "Fire") == 0) {
            lua_pushlightuserdata(L, (void*)be);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                BindableEventNode* bev = (BindableEventNode*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!bev) return 0;
                int nargs = lua_gettop(pL) - 1;
                bev->fire(pL, 2, nargs < 0 ? 0 : nargs);
                return 0;
            }, "Fire", 1);
            return 1;
        }
        if (strcmp(key, "Event") == 0) {
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)be);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                BindableEventNode* bev = (BindableEventNode*)lua_touserdata(pL, lua_upvalueindex(1));
                int fn_pos = -1;
                for (int ai = 1; ai <= lua_gettop(pL); ai++) {
                    if (lua_isfunction(pL, ai)) { fn_pos = ai; break; }
                }
                if (fn_pos == -1 || !bev) return 0;
                lua_State* mL = gl_main_of(pL);
                if (!mL) mL = pL;
                lua_pushvalue(pL, fn_pos);
                int ref = gl_ref(pL, -1);
                lua_pop(pL, 1);
                bev->add_cb(mL, ref);
                _gl_push_connection(pL, bev, ref); return 1;
            }, "Connect", 1);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }
    }

    // ── BodyMovers ─────────────────────────────────────────────────
    BodyVelocity* bv = gl_cast<BodyVelocity>(n);
    if (bv) {
        if (strcmp(key,"Velocity")  == 0) { Vector3 v = bv->get_velocity();  push_vector3(L, v.x, v.y, v.z); return 1; }
        if (strcmp(key,"MaxForce")  == 0) { Vector3 v = bv->get_max_force(); push_vector3(L, v.x, v.y, v.z); return 1; }
        if (strcmp(key,"P")         == 0) { lua_pushnumber(L, bv->get_p()); return 1; }
    }
    BodyPosition* bpos = gl_cast<BodyPosition>(n);
    if (bpos) {
        if (strcmp(key,"Position") == 0) { Vector3 v = bpos->get_position_val(); push_vector3(L, v.x, v.y, v.z); return 1; }
        if (strcmp(key,"MaxForce") == 0) { Vector3 v = bpos->get_max_force();    push_vector3(L, v.x, v.y, v.z); return 1; }
        if (strcmp(key,"P")        == 0) { lua_pushnumber(L, bpos->get_p()); return 1; }
        if (strcmp(key,"D")        == 0) { lua_pushnumber(L, bpos->get_d()); return 1; }
    }
    BodyForce* bf = gl_cast<BodyForce>(n);
    if (bf) {
        if (strcmp(key,"Force")      == 0) { Vector3 v = bf->get_force();  push_vector3(L, v.x, v.y, v.z); return 1; }
        if (strcmp(key,"RelativeTo") == 0) { lua_pushboolean(L, bf->get_relative_to()); return 1; }
    }
    BodyAngularVelocity* bav = gl_cast<BodyAngularVelocity>(n);
    if (bav) {
        if (strcmp(key,"AngularVelocity") == 0) { Vector3 v = bav->get_angular_velocity(); push_vector3(L, v.x, v.y, v.z); return 1; }
        if (strcmp(key,"MaxTorque")       == 0) { Vector3 v = bav->get_max_torque();        push_vector3(L, v.x, v.y, v.z); return 1; }
        if (strcmp(key,"P")               == 0) { lua_pushnumber(L, bav->get_p()); return 1; }
    }
    BodyGyro* bgyro = gl_cast<BodyGyro>(n);
    if (bgyro) {
        if (strcmp(key,"MaxTorque") == 0) { Vector3 v = bgyro->get_max_torque(); push_vector3(L, v.x, v.y, v.z); return 1; }
        if (strcmp(key,"P")        == 0) { lua_pushnumber(L, bgyro->get_p()); return 1; }
        if (strcmp(key,"D")        == 0) { lua_pushnumber(L, bgyro->get_d()); return 1; }
    }

    // ── Constraints ────────────────────────────────────────────────
    WeldConstraint* weld = gl_cast<WeldConstraint>(n);
    if (weld) {
        if (strcmp(key,"Enabled") == 0) { lua_pushboolean(L, weld->get_enabled()); return 1; }
        if (strcmp(key,"Part0")   == 0) { wrap_node(L, weld->get_part0());          return 1; }
        if (strcmp(key,"Part1")   == 0) { wrap_node(L, weld->get_part1());          return 1; }
    }
    // Weld (C0/C1): las piezas aqui; C0, C1 y Enabled por su ADD_PROPERTY.
    if (Weld* wd = gl_cast<Weld>(n)) {
        if (strcmp(key,"Part0")   == 0) { wrap_node(L, wd->get_part0()); return 1; }
        if (strcmp(key,"Part1")   == 0) { wrap_node(L, wd->get_part1()); return 1; }
        if (strcmp(key,"C0")      == 0) { _gl_push_cframe_full(L, wd->get_c0()); return 1; }
        if (strcmp(key,"C1")      == 0) { _gl_push_cframe_full(L, wd->get_c1()); return 1; }
        if (strcmp(key,"Enabled") == 0) { lua_pushboolean(L, wd->get_enabled() ? 1 : 0); return 1; }
    }
    HingeConstraint* hinge = gl_cast<HingeConstraint>(n);
    if (hinge) {
        if (strcmp(key,"Enabled")             == 0) { lua_pushboolean(L, hinge->get_enabled());              return 1; }
        if (strcmp(key,"LimitsEnabled")       == 0) { lua_pushboolean(L, hinge->get_limits_enabled());       return 1; }
        if (strcmp(key,"UpperAngle")          == 0) { lua_pushnumber(L,  hinge->get_upper_angle());          return 1; }
        if (strcmp(key,"LowerAngle")          == 0) { lua_pushnumber(L,  hinge->get_lower_angle());          return 1; }
        if (strcmp(key,"MotorEnabled")        == 0) { lua_pushboolean(L, hinge->get_motor_enabled());        return 1; }
        if (strcmp(key,"MotorMaxTorque")      == 0) { lua_pushnumber(L,  hinge->get_motor_max_torque());     return 1; }
        if (strcmp(key,"MotorTargetVelocity") == 0) { lua_pushnumber(L,  hinge->get_angular_velocity());      return 1; }   // nombre viejo de AngularVelocity
        if (strcmp(key,"CurrentAngle")        == 0) { lua_pushnumber(L,  hinge->get_current_angle());        return 1; }
    }
    RodConstraint* rod = gl_cast<RodConstraint>(n);
    if (rod) {
        if (strcmp(key,"Enabled") == 0) { lua_pushboolean(L, rod->get_enabled()); return 1; }
        if (strcmp(key,"Length")  == 0) { lua_pushnumber(L,  rod->get_length());  return 1; }
    }
    SpringConstraint* spring = gl_cast<SpringConstraint>(n);
    if (spring) {
        if (strcmp(key,"Enabled")    == 0) { lua_pushboolean(L, spring->get_enabled());     return 1; }
        if (strcmp(key,"FreeLength") == 0) { lua_pushnumber(L,  spring->get_free_length()); return 1; }
        if (strcmp(key,"Stiffness")  == 0) { lua_pushnumber(L,  spring->get_stiffness());   return 1; }
        if (strcmp(key,"Damping")    == 0) { lua_pushnumber(L,  spring->get_damping());     return 1; }
    }

    return -1;
}

#endif // LUAU_API_INDICE_MUNDO_H

#ifndef LUAU_API_INDICE_PERSONAJE_H
#define LUAU_API_INDICE_PERSONAJE_H
#include "gl_cast.h"

// Lectura de propiedades desde Luau (__index de las instancias), Players, Mouse, Teams, el chat, Lighting, la HumanoidRootPart, Node3D,
// RobloxPlayer (2D y 3D) y Humanoid (2D y 3D).
//
// Va aparte de luau_api.h (pasaba de 2.000 lineas) y se incluye desde alli,
// justo antes de godot_object_index_real, que llama a las zonas en orden.
// Devuelve cuantos valores deja en la pila, o -1 si la clave no es de
// esta zona y hay que seguir buscando.

inline int gl_index_personaje(lua_State* L, Node* n, const char* key) {
    // ── Players ───────────────────────────────────────────────────
    Players* players_svc = gl_cast<Players>(n);
    if (players_svc) {
        if (strcmp(key, "LocalPlayer") == 0) {
            // El servidor no tiene "jugador local": LocalPlayer = nil en un ServerScript.
            lua_getglobal(L, "__IS_SERVER");
            bool srv = lua_isboolean(L, -1) && (lua_toboolean(L, -1) != 0);
            lua_pop(L, 1);
            if (srv) { lua_pushnil(L); return 1; }
            wrap_node(L, players_svc->get_local_player()); return 1;
        }
        if (strcmp(key, "MaxPlayers") == 0) { lua_pushnumber(L, players_svc->get_max_players()); return 1; }
        // Respawn automatico (1.15): antes ninguna de las dos existia, asi que al
        // morir el jugador se quedaba tirado en el suelo para siempre.
        if (strcmp(key, "RespawnTime") == 0) { lua_pushnumber(L, players_svc->get_respawn_time()); return 1; }
        if (strcmp(key, "CharacterAutoLoads") == 0) { lua_pushboolean(L, players_svc->get_character_auto_loads() ? 1 : 0); return 1; }
        if (strcmp(key, "NumPlayers") == 0) {
            TypedArray<Node> pl = players_svc->get_players();
            lua_pushnumber(L, pl.size()); return 1;
        }
        if (strcmp(key, "GetPlayers") == 0) {
            lua_pushlightuserdata(L, (void*)players_svc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Players* ps = (Players*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_newtable(pL);
                if (ps) {
                    TypedArray<Node> pl = ps->get_players();
                    for (int i = 0; i < pl.size(); i++) {
                        wrap_node(pL, gl_cast<Node>(pl[i]));
                        lua_rawseti(pL, -2, i + 1);
                    }
                }
                return 1;
            }, "GetPlayers", 1);
            return 1;
        }
        if (strcmp(key, "GetPlayerFromCharacter") == 0) {
            lua_pushlightuserdata(L, (void*)players_svc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Players* ps = (Players*)lua_touserdata(pL, lua_upvalueindex(1));
                GodotObjectWrapper* cw = (GodotObjectWrapper*)lua_touserdata(pL, 2);
                if (!ps || !cw || !gow_node(cw)) { lua_pushnil(pL); return 1; }
                Node* character = gow_node(cw);
                TypedArray<Node> all_players = ps->get_players();
                for (int i = 0; i < all_players.size(); i++) {
                    PlayerObject* po = gl_cast<PlayerObject>(all_players[i]);
                    if (po && po->get_character() == character) { wrap_node(pL, po); return 1; }
                }
                lua_pushnil(pL); return 1;
            }, "GetPlayerFromCharacter", 1);
            return 1;
        }
        if (strcmp(key, "GetPlayerByUserId") == 0) {
            lua_pushlightuserdata(L, (void*)players_svc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Players* ps = (Players*)lua_touserdata(pL, lua_upvalueindex(1));
                int uid = (int)gl_num(pL, 2);
                if (!ps) { lua_pushnil(pL); return 1; }
                TypedArray<Node> all_players = ps->get_players();
                for (int i = 0; i < all_players.size(); i++) {
                    PlayerObject* po = gl_cast<PlayerObject>(all_players[i]);
                    if (po && po->user_id == uid) { wrap_node(pL, po); return 1; }
                }
                lua_pushnil(pL); return 1;
            }, "GetPlayerByUserId", 1);
            return 1;
        }
        // ── Consultas al backend de Roblox ────────────────────────────
        // Sin cuenta ni red no hay a quien preguntar, pero TIENEN que existir:
        // van dentro de un pcall que solo comprueba `success`, asi que devolver
        // nil dejaba la foto del jugador en blanco y el nombre vacio en cada
        // cartel de propietario, marcador y ranking (8 usos en el place).
        if (strcmp(key, "GetUserThumbnailAsync") == 0) {
            lua_pushlightuserdata(L, (void*)players_svc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                // (contenido, listo) — el mismo par que devuelve Roblox.
                lua_pushstring(pL, "");
                lua_pushboolean(pL, true);
                return 2;
            }, "GetUserThumbnailAsync", 1);
            return 1;
        }
        if (strcmp(key, "GetNameFromUserIdAsync") == 0) {
            lua_pushlightuserdata(L, (void*)players_svc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Players* ps = (Players*)lua_touserdata(pL, lua_upvalueindex(1));
                const int uid = (int)luaL_optnumber(pL, 2, 0);
                if (ps) {
                    TypedArray<Node> todos = ps->get_players();
                    for (int i = 0; i < todos.size(); i++) {
                        PlayerObject* po = gl_cast<PlayerObject>(todos[i]);
                        if (po && po->user_id == uid) {
                            lua_pushstring(pL, String(po->get_name()).utf8().get_data());
                            return 1;
                        }
                    }
                }
                lua_pushstring(pL, (String("Player") + String::num_int64(uid)).utf8().get_data());
                return 1;
            }, "GetNameFromUserIdAsync", 1);
            return 1;
        }
        if (strcmp(key, "GetUserIdFromNameAsync") == 0) {
            lua_pushlightuserdata(L, (void*)players_svc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Players* ps = (Players*)lua_touserdata(pL, lua_upvalueindex(1));
                const char* nom = luaL_optstring(pL, 2, "");
                if (ps) {
                    TypedArray<Node> todos = ps->get_players();
                    for (int i = 0; i < todos.size(); i++) {
                        PlayerObject* po = gl_cast<PlayerObject>(todos[i]);
                        if (po && String(po->get_name()) == String(nom)) {
                            lua_pushnumber(pL, (double)po->user_id); return 1;
                        }
                    }
                }
                lua_pushnumber(pL, 0); return 1;
            }, "GetUserIdFromNameAsync", 1);
            return 1;
        }
        // Sin backend social no hay lista de amigos: una pagina vacia, que es lo
        // que un `for _, amigo in pages:GetCurrentPage()` sabe recorrer.
        if (strcmp(key, "GetFriendsAsync") == 0) {
            lua_pushlightuserdata(L, (void*)players_svc);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                lua_newtable(pL);
                lua_pushcfunction(pL, [](lua_State* p2) -> int { lua_newtable(p2); return 1; }, "GetCurrentPage");
                lua_setfield(pL, -2, "GetCurrentPage");
                lua_pushcfunction(pL, [](lua_State*) -> int { return 0; }, "AdvanceToNextPageAsync");
                lua_setfield(pL, -2, "AdvanceToNextPageAsync");
                lua_pushboolean(pL, true);
                lua_setfield(pL, -2, "IsFinished");
                return 1;
            }, "GetFriendsAsync", 1);
            return 1;
        }
        // Aqui la membresia no cambia en mitad de la partida: senal que no salta.
        if (strcmp(key, "PlayerMembershipChanged") == 0) { _gl_push_inert_signal(L); return 1; }
        // PlayerAdded / PlayerRemoving signals
        if (strcmp(key, "PlayerAdded") == 0 || strcmp(key, "PlayerRemoving") == 0) {
            const char* sig = key;
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)players_svc);
            lua_pushstring(L, sig);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Players* ps = (Players*)lua_touserdata(pL, lua_upvalueindex(1));
                const char* ev = lua_tostring(pL, lua_upvalueindex(2));
                if (!lua_isfunction(pL, 2) || !ps) return 0;
                lua_pushvalue(pL, 2); int ref = gl_ref(pL, -1); lua_pop(pL, 1);
                lua_State* mL = gl_main_of(pL);
                if (!mL) mL = pL;
                if (strcmp(ev, "PlayerAdded") == 0)    ps->add_player_added_cb(mL, ref);
                else                                    ps->add_player_removing_cb(mL, ref);
                _gl_push_connection(pL, ps, ref); return 1;
            }, "Connect", 2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            // :Once — igual que Connect pero se desconecta tras el primer disparo.
            lua_pushlightuserdata(L, (void*)players_svc);
            lua_pushstring(L, sig);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Players* ps = (Players*)lua_touserdata(pL, lua_upvalueindex(1));
                const char* ev = lua_tostring(pL, lua_upvalueindex(2));
                if (!lua_isfunction(pL, 2) || !ps) return 0;
                lua_pushvalue(pL, 2); int ref = gl_ref(pL, -1); lua_pop(pL, 1);
                lua_State* mL = gl_main_of(pL);
                if (!mL) mL = pL;
                if (strcmp(ev, "PlayerAdded") == 0)    ps->add_player_added_cb(mL, ref, true);
                else                                    ps->add_player_removing_cb(mL, ref, true);
                _gl_push_connection(pL, ps, ref); return 1;
            }, "Once", 2);
            lua_setfield(L, -2, "Once");
            // :Wait — cede el hilo y lo reanuda con el Player del próximo disparo.
            lua_pushlightuserdata(L, (void*)players_svc);
            lua_pushstring(L, sig);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Players* ps = (Players*)lua_touserdata(pL, lua_upvalueindex(1));
                const char* ev = lua_tostring(pL, lua_upvalueindex(2));
                if (!ps) { lua_pushnil(pL); return 1; }
                lua_State* mL = gl_main_of(pL);
                if (!mL) mL = pL;
                if (strcmp(ev, "PlayerAdded") == 0)    ps->add_player_added_wait(pL, mL);
                else                                    ps->add_player_removing_wait(pL, mL);
                return gl_yield_externo(pL, "__WAIT_SIGNAL__");
            }, "Wait", 2);
            lua_setfield(L, -2, "Wait");
            return 1;
        }
    }

    // ── RobloxMouse — Player:GetMouse() (1.14.10 Finish) ──────────
    if (RobloxMouse* mo = gl_cast<RobloxMouse>(n)) {
        if (strcmp(key, "X") == 0)         { lua_pushnumber(L, (double)mo->pos2d().x); return 1; }
        if (strcmp(key, "Y") == 0)         { lua_pushnumber(L, (double)mo->pos2d().y); return 1; }
        if (strcmp(key, "ViewSizeX") == 0) { lua_pushnumber(L, (double)mo->view_size().x); return 1; }
        if (strcmp(key, "ViewSizeY") == 0) { lua_pushnumber(L, (double)mo->view_size().y); return 1; }
        if (strcmp(key, "ClassName") == 0) { lua_pushstring(L, "Mouse"); return 1; }
        // Hit: CFrame en el punto de impacto. Si el rayo no da a nada, Roblox
        // devuelve un punto lejano sobre el rayo (no nil) → mismo comportamiento.
        if (strcmp(key, "Hit") == 0) {
            Dictionary r = mo->cast();
            Vector3 p = r.is_empty() ? (mo->unit_ray_origin() + mo->unit_ray_dir() * 1000.0f)
                                     : (Vector3)r.get("position", Vector3());
            _gl_push_cframe_pos(L, p);
            return 1;
        }
        if (strcmp(key, "Target") == 0) {
            Dictionary r = mo->cast();
            Variant cv = r.get("collider", Variant());
            // La pieza tocada (miembro del conjunto, Terrain...), no el cuerpo.
            Node* hit = (cv.get_type() == Variant::OBJECT)
                ? gl_golpe_a_pieza(gl_cast<Node>(cv.operator Object*()), (int)r.get("shape", -1))
                : nullptr;
            if (hit) wrap_node(L, hit); else lua_pushnil(L);
            return 1;
        }
        if (strcmp(key, "TargetSurface") == 0) {
            // Como Roblox: el Enum.NormalId de la cara tocada, en los ejes de la
            // pieza (antes salia la normal cruda, un Vector3, y comparar con
            // Enum.NormalId.Top no daba nunca true).
            Dictionary r = mo->cast();
            Vector3 nrm = r.get("normal", Vector3(0, 1, 0));
            const Variant cv = r.get("collider", Variant());
            Node3D* pieza = (cv.get_type() == Variant::OBJECT)
                ? gl_cast<Node3D>(gl_golpe_a_pieza(gl_cast<Node>(cv.operator Object*()), (int)r.get("shape", -1)))
                : nullptr;
            if (pieza) nrm = pieza->get_global_transform().basis.inverse().xform(nrm);
            const Vector3 a = nrm.abs();
            int cara;
            if (a.x >= a.y && a.x >= a.z) cara = nrm.x >= 0 ? 0 : 3;        // Right / Left
            else if (a.y >= a.z)          cara = nrm.y >= 0 ? 1 : 4;        // Top / Bottom
            else                          cara = nrm.z >= 0 ? 2 : 5;        // Back / Front
            gl_push_enum_item(L, "NormalId", cara);
            return 1;
        }
        if (strcmp(key, "UnitRay") == 0) {
            lua_getglobal(L, "Ray");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "new");
                if (lua_isfunction(L, -1)) {
                    Vector3 o = mo->unit_ray_origin(), d = mo->unit_ray_dir();
                    push_vector3(L, o.x, o.y, o.z);
                    push_vector3(L, d.x, d.y, d.z);
                    if (lua_pcall(L, 2, 1, 0) == LUA_OK) { lua_remove(L, -2); return 1; }
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            lua_pushnil(L);
            return 1;
        }
        if (strcmp(key, "TargetFilter") == 0) {
            Node* tf = mo->target_filter_id ? gl_cast<Node>(ObjectDB::get_instance(mo->target_filter_id)) : nullptr;
            if (tf) wrap_node(L, tf); else lua_pushnil(L);
            return 1;
        }
        const char* MOUSE_EVENTS[] = { "Button1Down","Button1Up","Button2Down","Button2Up","Move","WheelForward","WheelBackward", nullptr };
        for (int i = 0; MOUSE_EVENTS[i]; i++) {
            if (strcmp(key, MOUSE_EVENTS[i]) != 0) continue;
            lua_newtable(L);
            for (int once = 0; once < 2; once++) {
                lua_pushlightuserdata(L, (void*)mo);
                lua_pushinteger(L, i);
                lua_pushboolean(L, once);
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    RobloxMouse* m = (RobloxMouse*)lua_touserdata(pL, lua_upvalueindex(1));
                    int idx = (int)lua_tointeger(pL, lua_upvalueindex(2));
                    bool once_f = lua_toboolean(pL, lua_upvalueindex(3)) != 0;
                    if (!m || !lua_isfunction(pL, 2)) { lua_pushnil(pL); return 1; }
                    lua_State* mL = gl_main_of(pL);
                    if (!mL) mL = pL;
                    int ref = gl_ref(pL, 2);
                    if (auto* lst = m->cb_list(idx)) m->add_cb(*lst, mL, ref, once_f);
                    _gl_push_connection(pL, m, ref);
                    return 1;
                }, once ? "Once" : "Connect", 3);
                lua_setfield(L, -2, once ? "Once" : "Connect");
            }
            return 1;
        }
    }

    // ── Teams / Team (1.14.10) ────────────────────────────────────
    if (gl_cast<Teams>(n) && strcmp(key, "GetTeams") == 0) {
        lua_pushlightuserdata(L, (void*)n);
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            Teams* ts = gl_cast<Teams>((Node*)lua_touserdata(pL, lua_upvalueindex(1)));
            lua_newtable(pL);
            if (!ts) return 1;
            TypedArray<Node> arr = ts->get_teams();
            for (int i = 0; i < arr.size(); i++) {
                wrap_node(pL, gl_cast<Node>(arr[i]));
                lua_rawseti(pL, -2, i + 1);
            }
            return 1;
        }, "GetTeams", 1);
        return 1;
    }
    if (gl_cast<Team>(n) && strcmp(key, "GetPlayers") == 0) {
        lua_pushlightuserdata(L, (void*)n);
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            Node* team = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
            lua_newtable(pL);
            if (!team) return 1;
            // Team > Teams > DataModel: Players es un hermano de Teams.
            Node* teams_svc = team->get_parent();
            Node* dm = teams_svc ? teams_svc->get_parent() : nullptr;
            Players* ps = dm ? gl_cast<Players>(dm->get_node_or_null("Players")) : nullptr;
            if (!ps) return 1;
            uint64_t tid = (uint64_t)team->get_instance_id();
            int out = 0;
            for (int i = 0; i < ps->get_child_count(); i++) {
                PlayerObject* po = gl_cast<PlayerObject>(ps->get_child(i));
                if (!po || po->team_id != tid) continue;
                wrap_node(pL, po);
                lua_rawseti(pL, -2, ++out);
            }
            return 1;
        }, "GetPlayers", 1);
        return 1;
    }

    // ── TextChatService, TextChannel, TextChatCommand (gl_puente_chat.h) ──
    if (gl_cast<TextChatService>(n) || gl_cast<TextChannel>(n) || gl_cast<TextChatCommand>(n)) {
        const int r = gl_chat_index(L, n, key);
        if (r >= 0) return r;
    }

    // ── Lighting ──────────────────────────────────────────────────
    Lighting* light_svc = gl_cast<Lighting>(n);
    if (light_svc) {
        if (strcmp(key, "Brightness")               == 0) { lua_pushnumber(L,  light_svc->get_brightness());           return 1; }
        if (strcmp(key, "ClockTime")                == 0) { lua_pushnumber(L,  light_svc->get_clock_time());           return 1; }
        if (strcmp(key, "GeographicLatitude")       == 0) { lua_pushnumber(L,  light_svc->get_geographic_latitude());  return 1; }
        if (strcmp(key, "GlobalShadows")            == 0) { lua_pushboolean(L, light_svc->get_global_shadows());       return 1; }
        if (strcmp(key, "ShadowSoftness")           == 0) { lua_pushnumber(L,  light_svc->get_shadow_softness());      return 1; }
        if (strcmp(key, "FogDensity")               == 0) { lua_pushnumber(L,  light_svc->get_fog_density());          return 1; }
        if (strcmp(key, "FogStart")                 == 0) { lua_pushnumber(L,  light_svc->get_fog_start());            return 1; }
        if (strcmp(key, "FogEnd")                   == 0) { lua_pushnumber(L,  light_svc->get_fog_end());              return 1; }
        if (strcmp(key, "ExposureCompensation")     == 0) { lua_pushnumber(L,  light_svc->get_exposure_comp());        return 1; }
        if (strcmp(key, "EnvironmentDiffuseScale")  == 0) { lua_pushnumber(L,  light_svc->get_env_diffuse_scale());    return 1; }
        if (strcmp(key, "EnvironmentSpecularScale") == 0) { lua_pushnumber(L,  light_svc->get_env_specular_scale());   return 1; }
        if (strcmp(key, "Technology")               == 0) { lua_pushnumber(L,  light_svc->get_technology());            return 1; }
        if (strcmp(key, "Ambient")                  == 0) { Color c = light_svc->get_ambient();        push_color3(L, c.r, c.g, c.b); return 1; }
        if (strcmp(key, "OutdoorAmbient")           == 0) { Color c = light_svc->get_outdoor_ambient();push_color3(L, c.r, c.g, c.b); return 1; }
        if (strcmp(key, "FogColor")                 == 0) { Color c = light_svc->get_fog_color();      push_color3(L, c.r, c.g, c.b); return 1; }
        if (strcmp(key, "ColorShift_Top")           == 0) { Color c = light_svc->get_color_shift_top();   push_color3(L, c.r, c.g, c.b); return 1; }
        if (strcmp(key, "ColorShift_Bottom")        == 0) { Color c = light_svc->get_color_shift_bottom();push_color3(L, c.r, c.g, c.b); return 1; }
        // GetSunDirection / GetMoonDirection: la formula de Roblox (ver
        // Lighting::get_sun_direction). Antes no existian y un script que
        // orientaba algo al sol moria al llamarlas.
        if (strcmp(key, "GetSunDirection") == 0 || strcmp(key, "GetMoonDirection") == 0) {
            const bool es_sol = key[3] == 'S';
            lua_pushlightuserdata(L, (void*)light_svc);
            lua_pushboolean(L, es_sol);
            // El nombre de depuracion tiene que ser un literal: Luau guarda el
            // puntero, no lo copia.
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Lighting* l = (Lighting*)lua_touserdata(pL, lua_upvalueindex(1));
                const bool sol = lua_toboolean(pL, lua_upvalueindex(2));
                const Vector3 d = !l ? Vector3() : (sol ? l->get_sun_direction() : l->get_moon_direction());
                push_vector3(pL, d.x, d.y, d.z);
                return 1;
            }, es_sol ? "GetSunDirection" : "GetMoonDirection", 2);
            return 1;
        }
        if (strcmp(key, "TimeOfDay")                == 0) { lua_pushstring(L, light_svc->get_time_of_day().utf8().get_data()); return 1; }
        if (strcmp(key, "DayNightCycle")            == 0) { lua_pushboolean(L, light_svc->get_day_night_cycle());     return 1; }
        if (strcmp(key, "DayLengthMinutes")         == 0) { lua_pushnumber(L,  light_svc->get_day_length_minutes()); return 1; }
        if (strcmp(key, "GetMinutesAfterMidnight")  == 0) {
            lua_pushlightuserdata(L, (void*)light_svc);
            lua_pushcclosure(L, [](lua_State* L) -> int {
                Lighting* lt = (Lighting*)lua_touserdata(L, lua_upvalueindex(1));
                lua_pushnumber(L, lt ? lt->get_minutes_after_midnight() : 0.0);
                return 1;
            }, "GetMinutesAfterMidnight", 1); return 1;
        }
        if (strcmp(key, "SetMinutesAfterMidnight")  == 0) {
            lua_pushlightuserdata(L, (void*)light_svc);
            lua_pushcclosure(L, [](lua_State* L) -> int {
                Lighting* lt = (Lighting*)lua_touserdata(L, lua_upvalueindex(1));
                if (lt) lt->set_minutes_after_midnight((float)gl_num(L, 2));
                return 0;
            }, "SetMinutesAfterMidnight", 1); return 1;
        }
    }

    // ── HumanoidRootPart del jugador ──────────────────────────────
    // Es un Node3D dentro del CharacterBody3D: lo mueve su Humanoid. Dash y
    // knockback leen y escriben su velocidad o le aplican un impulso, y nada de
    // eso existia (el script moria o el empujon no hacia nada).
    if (Node3D* pers_hrp = gl_character_root_of_hrp(n)) {
        CharacterBody3D* cb = gl_cast<CharacterBody3D>(pers_hrp);
        if (cb && !gl_cast<RobloxPart>(n)) {
            if (strcmp(key, "AssemblyLinearVelocity") == 0 || strcmp(key, "Velocity") == 0) {
                const Vector3 v = cb->get_velocity();
                push_vector3(L, v.x, v.y, v.z);
                return 1;
            }
            if (strcmp(key, "AssemblyAngularVelocity") == 0 || strcmp(key, "RotVelocity") == 0) {
                push_vector3(L, 0.0f, 0.0f, 0.0f);
                return 1;
            }
            if (strcmp(key, "AssemblyMass") == 0)     { lua_pushnumber(L, GL_MASA_PERSONAJE); return 1; }
            if (strcmp(key, "AssemblyRootPart") == 0) { wrap_node(L, n); return 1; }
            if (strcmp(key, "ApplyImpulse") == 0) {
                lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)cb->get_instance_id());
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    CharacterBody3D* c = gl_cast<CharacterBody3D>(ObjectDB::get_instance(ObjectID((uint64_t)(uintptr_t)lua_touserdata(pL, lua_upvalueindex(1)))));
                    LuauVector3* v = check_vector3(pL, 2);
                    if (c && v) c->set_velocity(c->get_velocity() + Vector3(v->x, v->y, v->z) / GL_MASA_PERSONAJE);
                    return 0;
                }, "ApplyImpulse", 1);
                return 1;
            }
            if (strcmp(key, "ApplyAngularImpulse") == 0) {
                lua_pushcfunction(L, [](lua_State* pL) -> int { (void)pL; return 0; }, "ApplyAngularImpulse");
                return 1;
            }
        }
    }

    // ── Piezas del rig de avatar (HumanoidRootPart, Head, UpperTorso...) ──
    // Son Node3D con el tamaño de Roblox en la meta _gl_rig_tamano (ver
    // gl_build_rig), y para los scripts son Part/MeshPart. Antes, leer casi
    // cualquier propiedad de BasePart en ellas tumbaba el script ("Size is not
    // a valid member of Node3D"): en Build a Mountain, colocar y recoger
    // bloques calcula los pies con HumanoidRootPart.Size y su pcall se comia el
    // error, asi que el boton nunca pasaba a "Place". Lo que el script escribio
    // vive en metas _glp_ (ver la escritura); lo demas sale como en una pieza de
    // personaje de Roblox.
    static const StringName k_rig_tam("_gl_rig_tamano");
    if (n->has_meta(k_rig_tam) && !gl_cast<RobloxPart>(n) && key[0] >= 'A' && key[0] <= 'Z') {
        const StringName mk(String("_glp_") + String(key));
        if (n->has_meta(mk) && gl_variant_to_luau(L, n->get_meta(mk))) return 1;
        const Vector3 tam = n->get_meta(k_rig_tam);
        if (strcmp(key, "Size") == 0) { push_vector3(L, tam.x, tam.y, tam.z); return 1; }
        if (strcmp(key, "Transparency") == 0) {
            static const StringName k_tr("_gl_rig_transp");
            lua_pushnumber(L, n->has_meta(k_tr) ? (double)(float)n->get_meta(k_tr) : 0.0);
            return 1;
        }
        if (strcmp(key, "LocalTransparencyModifier") == 0) { lua_pushnumber(L, 0.0); return 1; }
        if (strcmp(key, "Color") == 0) {
            static const StringName k_col("_gl_rig_color");
            const Color c = n->has_meta(k_col) ? (Color)n->get_meta(k_col) : Color(0.64f, 0.64f, 0.65f);
            push_color3(L, c.r, c.g, c.b);
            return 1;
        }
        // En Roblox el Humanoid deja chocar el tronco, la cabeza y la raiz; las
        // extremidades, no.
        if (strcmp(key, "CanCollide") == 0) {
            const String nom = n->get_name();
            lua_pushboolean(L, nom == "HumanoidRootPart" || nom == "Head" || nom == "Torso" ||
                               nom == "UpperTorso" || nom == "LowerTorso");
            return 1;
        }
        if (strcmp(key, "CanQuery") == 0 || strcmp(key, "CanTouch") == 0 || strcmp(key, "CastShadow") == 0) {
            lua_pushboolean(L, 1);
            return 1;
        }
        if (strcmp(key, "Massless") == 0 || strcmp(key, "Locked") == 0) { lua_pushboolean(L, 0); return 1; }
        if (strcmp(key, "CollisionGroup") == 0) { lua_pushstring(L, "Default"); return 1; }
        if (strcmp(key, "Material") == 0) { uis_push_enum(L, "Material", 0); return 1; }
        // Plastico: densidad 0.7 de Roblox por el volumen.
        if (strcmp(key, "Mass") == 0) { lua_pushnumber(L, 0.7 * tam.x * tam.y * tam.z); return 1; }
        if (strcmp(key, "GetMass") == 0) {
            lua_pushnumber(L, 0.7 * tam.x * tam.y * tam.z);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                lua_pushvalue(pL, lua_upvalueindex(1));
                return 1;
            }, "GetMass", 1);
            return 1;
        }
        if (strcmp(key, "AssemblyMass") == 0) { lua_pushnumber(L, GL_MASA_PERSONAJE); return 1; }
    }

    // ── Node3D ────────────────────────────────────────────────────
    Node3D* n3d = gl_cast<Node3D>(n);
    if (n3d) {
        if (strcmp(key, "Position") == 0) {
            Vector3 p = gl_node_xf(n3d).origin;
            push_vector3(L, p.x, p.y, p.z); return 1;
        }
        if (strcmp(key, "Rotation") == 0) {
            Vector3 r = n3d->get_rotation_degrees();
            push_vector3(L, r.x, r.y, r.z); return 1;
        }
        if (strcmp(key, "Scale") == 0) {
            Vector3 s = n3d->get_scale();
            push_vector3(L, s.x, s.y, s.z); return 1;
        }
        if (strcmp(key, "CFrame") == 0) {
            // Se construye con el MISMO helper que GetPivot: pasa por
            // CFrame.new, asi que hereda la metatabla y sus accesores
            // (Position/LookVector/... salen como Vector3 de verdad). La tabla
            // que se armaba aqui a mano llevaba un Position suelto SIN la
            // metatabla de Vector3: (cf.Position - otro) daba nil.
            // La de un Attachment es LOCAL (WorldCFrame es la del mundo).
            _gl_push_cframe_full(L, gl_cast<Attachment>(n3d) ? n3d->get_transform() : gl_node_xf(n3d));
            return 1;
        }
        // ── Consultas de zona de Roblox ───────────────────────────────
        // GetPartBoundsInBox / GetPartBoundsInRadius / GetPartsInPart: la forma
        // moderna de "que hay aqui" (zonas, explosiones, hitboxes). Van contra
        // el espacio de fisica, forma contra forma y con el giro del CFrame.
        // Antes recorrian el arbol entero en cada llamada comparando cajas
        // alineadas: sin giro, el radio era un cubo, de los OverlapParams solo
        // se leia MaxParts y los personajes no salian nunca.
        if (strcmp(key, "GetPartBoundsInBox") == 0 ||
            strcmp(key, "GetPartBoundsInRadius") == 0 ||
            strcmp(key, "GetPartsInPart") == 0) {
            lua_pushinteger(L, strcmp(key, "GetPartBoundsInBox") == 0 ? 0
                              : (strcmp(key, "GetPartBoundsInRadius") == 0 ? 1 : 2));
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                const int modo = (int)lua_tointeger(pL, lua_upvalueindex(1));
                GodotObjectWrapper* wr = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Node* ws = wr ? gow_node(wr) : nullptr;
                GLZona z;
                if (modo == 0) {
                    // (cframe, size, overlapParams)
                    LuauVector3* tam = check_vector3(pL, 3);
                    if (!lua_istable(pL, 2) || !tam) { lua_newtable(pL); return 1; }
                    z.xf = _gl_read_cframe_tbl(pL, 2);
                    z.forma = gl_forma_consulta(false, Vector3(tam->x, tam->y, tam->z));
                } else if (modo == 1) {
                    // (posicion, radio, overlapParams)
                    LuauVector3* c = check_vector3(pL, 2);
                    if (!c) { lua_newtable(pL); return 1; }
                    const float d = 2.0f * (float)gl_num_or(pL, 3, 0.0);
                    z.xf.origin = Vector3(c->x, c->y, c->z);
                    z.forma = gl_forma_consulta(true, Vector3(d, d, d));
                } else {
                    // (pieza, overlapParams): su propia forma, como en Roblox
                    RobloxPart* p = _gl_has_metatable(pL, 2, "GodotObject")
                        ? gl_cast<RobloxPart>(gow_node((GodotObjectWrapper*)lua_touserdata(pL, 2)))
                        : nullptr;
                    if (!p || !p->is_inside_tree()) { lua_newtable(pL); return 1; }
                    p->gl_forma_en_mundo(z.forma, z.xf);
                    z.excluir = p;
                }
                gl_zona_consultar(pL, ws, z, modo == 2 ? 3 : 4);
                return 1;
            }, "GetPartBounds", 1);
            return 1;
        }

        // ── workspace:Raycast(origen, direccion, params) ─────────────────
        // El largo del rayo es el de la DIRECCION, como en Roblox. Antes se
        // normalizaba y se alargaba a 1000 studs: el rayo corto de "¿hay suelo
        // bajo mis pies?" golpeaba cosas a cientos de studs. Si un script pone
        // params.MaxDistance (no es de Roblox) se respeta como tope.
        if (strcmp(key, "Raycast") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                GodotObjectWrapper* wr = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                if (!wr || !gow_node(wr)) { lua_pushnil(pL); return 1; }
                LuauVector3* vo = check_vector3(pL, 2);
                LuauVector3* vd = check_vector3(pL, 3);
                if (!vo || !vd) { lua_pushnil(pL); return 1; }
                const Vector3 from(vo->x, vo->y, vo->z);
                Vector3 dir(vd->x, vd->y, vd->z);
                // ── RaycastParams (arg 4) ────────────────────────────
                // FilterDescendantsInstances y FilterType tienen que respetarse:
                // el patron "repetir el rayo metiendo en el filtro lo que se
                // acaba de golpear" colgaba el juego cuando se ignoraban (visto
                // en ClimaEventManager, getGroundHit).
                Vector<Node*> filtro;
                bool incluir = false;   // Enum.RaycastFilterType: 0 Include, 1 Exclude
                bool respetar = false;  // RespectCanCollide
                if (lua_istable(pL, 4)) {
                    lua_getfield(pL, 4, "MaxDistance");
                    if (lua_isnumber(pL, -1)) {
                        const float tope = (float)lua_tonumber(pL, -1);
                        if (tope > 0.0f && dir.length() > tope) dir = dir.normalized() * tope;
                    }
                    lua_pop(pL, 1);
                    lua_getfield(pL, 4, "FilterType");
                    incluir = ((int)gl_num_or(pL, -1, 1) == 0);
                    lua_pop(pL, 1);
                    lua_getfield(pL, 4, "RespectCanCollide");
                    respetar = lua_toboolean(pL, -1) != 0;
                    lua_pop(pL, 1);
                    lua_getfield(pL, 4, "FilterDescendantsInstances");
                    gl_leer_filtro(pL, -1, filtro);
                    lua_pop(pL, 1);
                }
                // Include con la lista vacia no golpea nada (Roblox).
                if (incluir && filtro.is_empty()) { lua_pushnil(pL); return 1; }
                const GLRayo r = gl_ws_rayo(gow_node(wr), from, dir, &filtro, incluir, respetar);
                if (!r.ok) { lua_pushnil(pL); return 1; }
                lua_newtable(pL);
                if (r.hit) wrap_node(pL, r.hit); else lua_pushnil(pL);
                lua_setfield(pL, -2, "Instance");
                push_vector3(pL, r.pos.x, r.pos.y, r.pos.z);          lua_setfield(pL, -2, "Position");
                push_vector3(pL, r.normal.x, r.normal.y, r.normal.z); lua_setfield(pL, -2, "Normal");
                lua_pushnumber(pL, (double)from.distance_to(r.pos));  lua_setfield(pL, -2, "Distance");
                gl_push_material_de(pL, r.hit);                       lua_setfield(pL, -2, "Material");
                return 1;
            }, "Raycast");
            return 1;
        }

        // ── FindPartOnRay / WithIgnoreList / WithWhitelist (la API vieja) ──
        // Reciben un Ray (Ray.new(origen, direccion)) y devuelven CUATRO
        // valores: pieza, punto, normal y material. Antes FindPartOnRay
        // compartia el codigo de Raycast, leia el Ray como si fuera un
        // Vector3 (origen 0,0,0) y devolvia una tabla.
        if (strcmp(key, "FindPartOnRay") == 0 || strcmp(key, "FindPartOnRayWithIgnoreList") == 0 ||
            strcmp(key, "FindPartOnRayWithWhitelist") == 0) {
            const int modo = strcmp(key, "FindPartOnRay") == 0 ? 0
                           : (strcmp(key, "FindPartOnRayWithIgnoreList") == 0 ? 1 : 2);
            lua_pushinteger(L, modo);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                const int m = (int)lua_tointeger(pL, lua_upvalueindex(1));
                GodotObjectWrapper* wr = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Vector3 from;   // OJO: sin "Vector3 from, dir;" (lua_pushcclosure es macro)
                Vector3 dir;
                if (lua_istable(pL, 2)) {
                    lua_getfield(pL, 2, "Origin");
                    if (LuauVector3* v = check_vector3(pL, -1)) from = Vector3(v->x, v->y, v->z);
                    lua_pop(pL, 1);
                    lua_getfield(pL, 2, "Direction");
                    if (LuauVector3* v = check_vector3(pL, -1)) dir = Vector3(v->x, v->y, v->z);
                    lua_pop(pL, 1);
                }
                Vector<Node*> filtro;
                if (m == 0) {
                    if (lua_isuserdata(pL, 3)) {
                        GodotObjectWrapper* fw = (GodotObjectWrapper*)lua_touserdata(pL, 3);
                        if (Node* fn = fw ? gow_node(fw) : nullptr) filtro.push_back(fn);
                    }
                } else {
                    gl_leer_filtro(pL, 3, filtro);
                }
                GLRayo r;
                if (wr && gow_node(wr)) r = gl_ws_rayo(gow_node(wr), from, dir, &filtro, m == 2, false);
                if (m == 2 && filtro.is_empty()) r = GLRayo();   // lista blanca vacia: nada
                if (!r.ok) {
                    // Sin golpe: nil, el final del rayo, normal (0,0,0) y Air.
                    lua_pushnil(pL);
                    const Vector3 fin = from + dir;
                    push_vector3(pL, fin.x, fin.y, fin.z);
                    push_vector3(pL, 0.0f, 0.0f, 0.0f);
                    uis_push_enum(pL, "Material", 36);   // Air
                    return 4;
                }
                if (r.hit) wrap_node(pL, r.hit); else lua_pushnil(pL);
                push_vector3(pL, r.pos.x, r.pos.y, r.pos.z);
                push_vector3(pL, r.normal.x, r.normal.y, r.normal.z);
                gl_push_material_de(pL, r.hit);
                return 4;
            }, key, 1);
            return 1;
        }

        // ── Spherecast / Blockcast / Shapecast ─────────────────────────────
        // Lanzar una forma en vez de un rayo: es como el combate moderno de
        // Roblox detecta golpes. No existian.
        if (strcmp(key, "Spherecast") == 0 || strcmp(key, "Blockcast") == 0 || strcmp(key, "Shapecast") == 0) {
            const int modo = key[0] == 'B' ? 1 : (key[1] == 'p' ? 0 : 2);   // Sphere 0, Block 1, Shape 2
            lua_pushinteger(L, modo);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                const int m = (int)lua_tointeger(pL, lua_upvalueindex(1));
                GodotObjectWrapper* wr = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Node* ws = wr ? gow_node(wr) : nullptr;
                Ref<Shape3D> forma;
                Transform3D xf;
                int i_dir = 4;
                if (m == 0) {          // (posicion, radio, direccion, params)
                    LuauVector3* c = check_vector3(pL, 2);
                    if (!c) { lua_pushnil(pL); return 1; }
                    const float d = 2.0f * (float)gl_num_or(pL, 3, 0.0);
                    xf.origin = Vector3(c->x, c->y, c->z);
                    forma = gl_forma_consulta(true, Vector3(d, d, d));
                } else if (m == 1) {   // (cframe, tamaño, direccion, params)
                    LuauVector3* t = check_vector3(pL, 3);
                    if (!lua_istable(pL, 2) || !t) { lua_pushnil(pL); return 1; }
                    xf = _gl_read_cframe_tbl(pL, 2);
                    forma = gl_forma_consulta(false, Vector3(t->x, t->y, t->z));
                } else {               // (pieza, direccion, params): la que esta ya tocando no cuenta
                    RobloxPart* p = _gl_has_metatable(pL, 2, "GodotObject")
                        ? gl_cast<RobloxPart>(gow_node((GodotObjectWrapper*)lua_touserdata(pL, 2)))
                        : nullptr;
                    if (!p || !p->is_inside_tree()) { lua_pushnil(pL); return 1; }
                    p->gl_forma_en_mundo(forma, xf);
                    i_dir = 3;
                }
                LuauVector3* vd = check_vector3(pL, i_dir);
                if (!vd) { lua_pushnil(pL); return 1; }
                const Vector3 dir(vd->x, vd->y, vd->z);
                Vector<Node*> filtro;
                bool incluir = false;
                bool respetar = false;
                gl_leer_raycastparams(pL, i_dir + 1, filtro, incluir, respetar);
                if (incluir && filtro.is_empty()) { lua_pushnil(pL); return 1; }
                const GLRayo r = gl_ws_barrido(ws, forma, xf, dir, filtro, incluir, respetar);
                gl_push_resultado_rayo(pL, r, r.dist);
                return 1;
            }, key, 1);
            return 1;
        }
    }

    // ── RobloxPlayer ──────────────────────────────────────────────
    RobloxPlayer* rp = gl_cast<RobloxPlayer>(n);
    if (rp) {
        if (strcmp(key, "CameraMode")              == 0) { lua_pushnumber(L, rp->get_camera_mode());               return 1; }
        if (strcmp(key, "MouseSensitivity")        == 0) { lua_pushnumber(L, rp->get_mouse_sensitivity());         return 1; }
        if (strcmp(key, "UserId")                  == 0) { lua_pushnumber(L, rp->get_user_id());                   return 1; }
        if (strcmp(key, "DisplayName")             == 0) { lua_pushstring(L, rp->get_display_name().utf8().get_data()); return 1; }
        if (strcmp(key, "Name")                    == 0) { lua_pushstring(L, rp->get_display_name().utf8().get_data()); return 1; }
        if (strcmp(key, "AccountAge")              == 0) { lua_pushnumber(L, rp->get_account_age());               return 1; }
        if (strcmp(key, "AutoJumpEnabled")         == 0) { lua_pushboolean(L, rp->get_auto_jump_enabled());        return 1; }
        if (strcmp(key, "DevCameraOcclusionMode")  == 0) { lua_pushnumber(L, rp->get_dev_camera_occlusion_mode()); return 1; }
        if (strcmp(key, "DevComputerMovementMode") == 0) { lua_pushnumber(L, rp->get_dev_computer_movement_mode()); return 1; }
        if (strcmp(key, "DevTouchMovementMode")    == 0) { lua_pushnumber(L, rp->get_dev_touch_movement_mode());   return 1; }
        if (strcmp(key, "TeamColor") == 0) {
            Color c = rp->get_team_color();
            push_color3(L, c.r, c.g, c.b); return 1;
        }
        // Character: find the RobloxPlayer node itself as the character model
        if (strcmp(key, "Character") == 0) {
            // The RobloxPlayer IS the character in our implementation
            wrap_node(L, rp); return 1;
        }
        // Character.PrimaryPart = su HumanoidRootPart, como en Roblox. Sin esto
        // el InactivitySystem del place moria con "PrimaryPart is not a valid
        // member of Character". Si el rig aun no lo tiene, el propio cuerpo.
        if (strcmp(key, "PrimaryPart") == 0) {
            Node* hrp = rp->get_node_or_null(NodePath("HumanoidRootPart"));
            wrap_node(L, hrp ? hrp : (Node*)rp);
            return 1;
        }
        if (strcmp(key, "GetCharacterAppearanceInfoAsync") == 0 ||
            strcmp(key, "GetCharacterAppearanceAsync") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int { lua_newtable(pL); return 1; }, key);
            return 1;
        }
        if (strcmp(key, "GetFriendStatus") == 0) {
            // Enum.FriendStatus.NotFriend (antes el numero 0, que es Unknown)
            lua_pushcfunction(L, [](lua_State* pL) -> int { gl_push_enum_item(pL, "FriendStatus", 1); return 1; }, "GetFriendStatus");
            return 1;
        }
        if (strcmp(key, "GetFriendsOnline") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int { lua_newtable(pL); return 1; }, "GetFriendsOnline");
            return 1;
        }
        if (strcmp(key, "Kick") == 0) {
            lua_pushlightuserdata(L, (void*)rp);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RobloxPlayer* p = (RobloxPlayer*)lua_touserdata(pL, lua_upvalueindex(1));
                const char* reason = lua_isstring(pL,2) ? lua_tostring(pL,2) : "Kicked";
                if (p) UtilityFunctions::print("[Player] Kicked: ", String(reason));
                return 0;
            }, "Kick", 1);
            return 1;
        }
        if (strcmp(key, "CharacterAdded") == 0 || strcmp(key, "CharacterRemoving") == 0) {
            const char* sig = key;
            lua_newtable(L);
            lua_pushcclosure(L, [](lua_State* pL) -> int { return 0; }, "Connect", 0);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }
    }

    // ── RobloxPlayer2D ────────────────────────────────────────────
    RobloxPlayer2D* rp2d = gl_cast<RobloxPlayer2D>(n);
    if (rp2d) {
        if (strcmp(key, "CameraMode")    == 0) { lua_pushnumber(L, rp2d->get_camera_mode());    return 1; }
        if (strcmp(key, "MovementType")  == 0) { lua_pushnumber(L, rp2d->get_movement_type());  return 1; }
        if (strcmp(key, "WalkSpeed")     == 0) { lua_pushnumber(L, rp2d->get_walk_speed());     return 1; }
        if (strcmp(key, "JumpPower")     == 0) { lua_pushnumber(L, rp2d->get_jump_power());     return 1; }
    }

    // ── Humanoid 3D ───────────────────────────────────────────────
    Humanoid* hum = gl_cast<Humanoid>(n);
    if (hum) {
        if (strcmp(key, "Health")                 == 0) { lua_pushnumber(L, hum->get_health());                  return 1; }
        if (strcmp(key, "MaxHealth")              == 0) { lua_pushnumber(L, hum->get_max_health());              return 1; }
        if (strcmp(key, "WalkSpeed")              == 0) { lua_pushnumber(L, hum->get_walk_speed());              return 1; }
        if (strcmp(key, "JumpPower")              == 0) { lua_pushnumber(L, hum->get_jump_power());              return 1; }
        if (strcmp(key, "JumpHeight")             == 0) { lua_pushnumber(L, hum->get_jump_height());             return 1; }
        if (strcmp(key, "HipHeight")              == 0) { lua_pushnumber(L, hum->get_hip_height());              return 1; }
        if (strcmp(key, "Gravity")                == 0) { lua_pushnumber(L, hum->get_gravity_val());             return 1; }
        if (strcmp(key, "IsDead")                 == 0) { lua_pushboolean(L, hum->is_character_dead());          return 1; }
        // EnumItem, para que `hum.FloorMaterial == Enum.Material.Air` funcione.
        if (strcmp(key, "FloorMaterial")          == 0) { uis_push_enum(L, "Material", hum->get_floor_material()); return 1; }
        if (strcmp(key, "MoveDirection")          == 0) { Vector3 md = hum->get_move_direction(); push_vector3(L, md.x, md.y, md.z); return 1; }
        if (strcmp(key, "AutoRotate")             == 0) { lua_pushboolean(L, hum->get_auto_rotate());            return 1; }
        if (strcmp(key, "AutoJumpEnabled")        == 0) { lua_pushboolean(L, hum->get_auto_jump_enabled());      return 1; }
        if (strcmp(key, "DisplayName")            == 0) { lua_pushstring(L, hum->get_display_name().utf8().get_data()); return 1; }
        if (strcmp(key, "NameDisplayDistance")    == 0) { lua_pushnumber(L, hum->get_name_display_distance());   return 1; }
        if (strcmp(key, "DisplayDistanceType")    == 0) { lua_pushnumber(L, hum->get_display_distance_type());  return 1; }
        if (strcmp(key, "HealthDisplayType")      == 0) { lua_pushnumber(L, hum->get_health_display_type());    return 1; }
        if (strcmp(key, "HealthDisplayDistance")  == 0) { lua_pushnumber(L, hum->get_health_display_distance()); return 1; }
        if (strcmp(key, "RigType")                == 0) { lua_pushnumber(L, hum->get_rig_type());                return 1; }
        if (strcmp(key, "EvaluateStateMachine")   == 0) { lua_pushboolean(L, hum->get_evaluate_state_machine()); return 1; }
        if (strcmp(key, "BreakJointsOnDeath")     == 0) { lua_pushboolean(L, hum->get_break_joints_on_death());  return 1; }
        if (strcmp(key, "RequiresNeck")           == 0) { lua_pushboolean(L, hum->get_requires_neck());          return 1; }

        if (strcmp(key, "TakeDamage") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                GodotObjectWrapper* w2 = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Humanoid* h = w2 ? gl_cast<Humanoid>(gow_node(w2)) : nullptr;
                if (h) h->take_damage((float)gl_num(pL, 2));
                return 0;
            }, "TakeDamage");
            return 1;
        }
        if (strcmp(key, "Heal") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                GodotObjectWrapper* w2 = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Humanoid* h = w2 ? gl_cast<Humanoid>(gow_node(w2)) : nullptr;
                if (h) h->heal((float)gl_num(pL, 2));
                return 0;
            }, "Heal");
            return 1;
        }
        // ── Additional Humanoid methods ───────────────────────────
        //// ── Métodos adicionales de Humanoid ───────────────────────
        // Jump es una PROPIEDAD bool en Roblox (no un metodo). Antes se leia
        // como un closure que solo cambiaba el numero de estado (sin velocidad
        // vertical: no saltaba nada) y la ESCRITURA se descartaba en silencio.
        if (strcmp(key, "Jump") == 0) {
            lua_pushboolean(L, hum->get_jump_request() || hum->get_state() == 2);
            return 1;
        }
        // Sit y SeatPart son PROPIEDADES en Roblox (Sit era aqui un metodo que
        // solo cambiaba el numero de estado).
        if (strcmp(key, "Sit") == 0) { lua_pushboolean(L, (hum->gl_asiento() || hum->get_state() == 6) ? 1 : 0); return 1; }
        if (strcmp(key, "SeatPart") == 0) {
            Node3D* a = hum->gl_asiento();
            if (a) wrap_node(L, a); else lua_pushnil(L);
            return 1;
        }
        if (strcmp(key, "PlatformStand") == 0) { lua_pushboolean(L, hum->get_platform_stand() ? 1 : 0); return 1; }
        // RootPart: la HumanoidRootPart de su personaje o modelo.
        if (strcmp(key, "RootPart") == 0) {
            Node* p = hum->get_parent();
            Node* r = p ? p->get_node_or_null(NodePath("HumanoidRootPart")) : nullptr;
            if (r) wrap_node(L, r); else lua_pushnil(L);
            return 1;
        }
        if (strcmp(key, "WalkToPoint") == 0) {
            const Vector3 p = hum->gl_walk_to_point();
            push_vector3(L, p.x, p.y, p.z);
            return 1;
        }
        if (strcmp(key, "WalkToPart") == 0) {
            Node3D* pz = hum->gl_moveto_pieza();
            if (pz) wrap_node(L, pz); else lua_pushnil(L);
            return 1;
        }
        // Humanoid:Move(direccion, relativaALaCamara): anda en esa direccion
        // hasta que le digan otra cosa. Antes solo cambiaba el estado.
        if (strcmp(key, "Move") == 0) {
            lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)hum->get_instance_id());
            lua_pushcclosure(L, [](lua_State* pL)->int{
                Humanoid* h = gl_cast<Humanoid>(ObjectDB::get_instance(ObjectID((uint64_t)(uintptr_t)lua_touserdata(pL, lua_upvalueindex(1)))));
                LuauVector3* v = check_vector3(pL, 2);
                if (!h || !v) return 0;
                Vector3 dir(v->x, v->y, v->z);
                if (lua_toboolean(pL, 3) && h->is_inside_tree()) {
                    if (Camera3D* cam = h->get_viewport()->get_camera_3d()) {
                        // Relativo a la camara: adelante es hacia donde mira, en horizontal.
                        Vector3 adelante = -cam->get_global_transform().basis.get_column(2);
                        Vector3 derecha  =  cam->get_global_transform().basis.get_column(0);
                        adelante.y = 0.0f; derecha.y = 0.0f;
                        dir = derecha.normalized() * dir.x - adelante.normalized() * dir.z;
                    }
                }
                h->gl_move(dir);
                return 0;
            },"Move",1); return 1;
        }
        if (strcmp(key, "ApplyDescription") == 0) {
            lua_pushcfunction(L, [](lua_State* pL)->int{ return 0; }, "ApplyDescription"); return 1;
        }
        if (strcmp(key, "GetAppliedDescription") == 0) {
            lua_pushcfunction(L, [](lua_State* pL)->int{ lua_pushnil(pL); return 1; }, "GetAppliedDescription"); return 1;
        }

        if (strcmp(key, "GetState") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                GodotObjectWrapper* w2 = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Humanoid* h = w2 ? gl_cast<Humanoid>(gow_node(w2)) : nullptr;
                // EnumItem: `GetState() == Enum.HumanoidStateType.Freefall` daba
                // siempre false con el numero pelado.
                uis_push_enum(pL, "HumanoidStateType", h ? h->get_state() : 1);
                return 1;
            }, "GetState");
            return 1;
        }
        if (strcmp(key, "ChangeState") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                GodotObjectWrapper* w2 = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Humanoid* h = w2 ? gl_cast<Humanoid>(gow_node(w2)) : nullptr;
                if (h) h->change_state((int)gl_num_or(pL, 2, 0));
                return 0;
            }, "ChangeState");
            return 1;
        }
        if (strcmp(key, "SetStateEnabled") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                GodotObjectWrapper* w2 = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Humanoid* h = w2 ? gl_cast<Humanoid>(gow_node(w2)) : nullptr;
                if (h) h->gl_set_state_enabled((int)gl_num_or(pL, 2, 0), lua_toboolean(pL, 3) != 0);
                return 0;
            }, "SetStateEnabled");
            return 1;
        }
        if (strcmp(key, "GetStateEnabled") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                GodotObjectWrapper* w2 = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Humanoid* h = w2 ? gl_cast<Humanoid>(gow_node(w2)) : nullptr;
                lua_pushboolean(pL, !h || h->gl_get_state_enabled((int)gl_num_or(pL, 2, 0)));
                return 1;
            }, "GetStateEnabled");
            return 1;
        }
        // EquipTool / UnequipTools: en Roblox equipar es mover la Tool del
        // Backpack al personaje (y desequipar, al reves). Es como el servidor
        // pone una herramienta en la mano sin que el jugador la elija.
        if (strcmp(key, "EquipTool") == 0 || strcmp(key, "UnequipTools") == 0) {
            const bool equipar = (strcmp(key, "EquipTool") == 0);
            lua_pushboolean(L, equipar);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                const bool eq = lua_toboolean(pL, lua_upvalueindex(1));
                GodotObjectWrapper* w2 = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Humanoid* h = w2 ? gl_cast<Humanoid>(gow_node(w2)) : nullptr;
                Node* pers = h ? h->get_parent() : nullptr;      // el personaje
                if (!pers || !pers->is_inside_tree()) return 0;
                // El Player duenio del personaje, para saber a que Backpack va.
                Node* jugador = nullptr;
                for (Node* p = pers->get_parent(); p; p = p->get_parent()) {
                    Node* svc = p->get_node_or_null(NodePath("Players"));
                    if (!svc) continue;
                    for (int i = 0; i < svc->get_child_count(); i++) {
                        PlayerObject* po = gl_cast<PlayerObject>(svc->get_child(i));
                        if (po && po->get_character() == pers) { jugador = po; break; }
                    }
                    if (jugador) break;
                }
                Node* mochila = jugador ? jugador->get_node_or_null(NodePath("Backpack")) : nullptr;
                if (eq) {
                    GodotObjectWrapper* wt = (GodotObjectWrapper*)lua_touserdata(pL, 2);
                    Node* tool = wt ? gow_node(wt) : nullptr;
                    if (!tool) return 0;
                    if (tool->get_parent()) tool->get_parent()->remove_child(tool);
                    gl_add_child_rbx(pers, tool);
                    gl_tool_agarrar(pers, tool);
                    if (RobloxTool* rt = gl_cast<RobloxTool>(tool)) rt->equip(jugador);
                } else {
                    for (int i = pers->get_child_count() - 1; i >= 0; i--) {
                        RobloxTool* rt = gl_cast<RobloxTool>(pers->get_child(i));
                        if (!rt) continue;
                        rt->unequip();
                        gl_tool_soltar(rt);
                        if (mochila) { pers->remove_child(rt); gl_add_child_rbx(mochila, rt); }
                    }
                }
                return 0;
            }, "EquipTool", 1);
            return 1;
        }
        // LoadAnimation y GetPlayingAnimationTracks: en la zona de instancia
        // (gl_animador_index), compartidos con Animator y AnimationController.
        // MoveTo es LA api de mover NPCs en Roblox. Estaba cableada a
        // StopNPCMovement, o sea que hacia justo lo CONTRARIO de lo que dice, y
        // encima se tragaba el destino sin dar error: cualquier NPC de un place
        // importado se quedaba clavado y en consola no salia nada. Va con
        // WalkToPoint, que es lo que de verdad significa.
        // MoveTo(pos [, pieza]) es LA api de mover NPCs en Roblox: anda hasta alli
        // y avisa con MoveToFinished (true al llegar, false si a los 8 s no ha
        // llegado). Antes solo fijaba una direccion que no se paraba nunca.
        if (strcmp(key, "MoveTo") == 0) {
            lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)hum->get_instance_id());
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Humanoid* h = gl_cast<Humanoid>(ObjectDB::get_instance(ObjectID((uint64_t)(uintptr_t)lua_touserdata(pL, lua_upvalueindex(1)))));
                LuauVector3* v = check_vector3(pL, 2);
                if (!h || !v) return 0;
                Node3D* pz = nullptr;
                if (_gl_has_metatable(pL, 3, "GodotObject"))
                    pz = gl_cast<Node3D>(gow_node((GodotObjectWrapper*)lua_touserdata(pL, 3)));
                h->gl_move_to(Vector3(v->x, v->y, v->z), pz);
                return 0;
            }, "MoveTo", 1);
            return 1;
        }
        if (strcmp(key, "StopNPCMovement") == 0) {
            lua_pushlightuserdata(L, (void*)hum);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Humanoid* h = (Humanoid*)lua_touserdata(pL, lua_upvalueindex(1));
                if (h) h->stop_npc_movement();
                return 0;
            }, key, 1);
            return 1;
        }
        if (strcmp(key, "GetHumanoidDescriptionFromUserId") == 0 ||
            strcmp(key, "GetHumanoidDescriptionFromOutfitId") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int { lua_newtable(pL); return 1; }, key);
            return 1;
        }

        // ── Humanoid signals ──────────────────────────────────────
        //// ── Señales del Humanoid ──────────────────────────────────
        if (strcmp(key, "Died") == 0) {
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)hum);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Humanoid* h = static_cast<Humanoid*>(lua_touserdata(pL, lua_upvalueindex(1)));
                int fn_pos = -1;
                for (int ai = 1; ai <= lua_gettop(pL); ai++) {
                    if (lua_isfunction(pL, ai)) { fn_pos = ai; break; }
                }
                if (fn_pos == -1 || !h) return 0;
                lua_State* main_L = gl_main_of(pL);
                if (!main_L) main_L = pL;
                lua_pushvalue(pL, fn_pos);
                int ref = gl_ref(pL, -1);
                lua_pop(pL, 1);
                h->add_died_callback(main_L, ref);
                _gl_push_connection(pL, h, ref); return 1;
            }, "Connect", 1);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }
        // ── Senales de movimiento del Humanoid ────────────────────
        // Running / Jumping / FreeFalling / Climbing / Swimming / GettingUp /
        // Seated / MoveToFinished. Todas comparten el mismo cierre: el indice
        // de la lista viaja como upvalue. Sin Running, el Animate.lua que trae
        // cada NPC de un place moria en la primera linea (T-pose).
        {
            // El orden importa: el indice es la lista de callbacks del Humanoid
            // (ver move_signal_list). Las que no tienen disparador propio se
            // aceptan igual para que el script siga vivo, como en Roblox.
            static const char* kMoveSig[] = { "Running", "Jumping", "FreeFalling",
                                              "Climbing", "Swimming", "GettingUp",
                                              "Seated", "MoveToFinished",
                                              "FallingDown", "PlatformStanding",
                                              "Strafing", "StateEnabledChanged",
                                              "Touched", nullptr };
            for (int mi = 0; kMoveSig[mi]; mi++) {
                if (strcmp(key, kMoveSig[mi]) != 0) continue;
                lua_newtable(L);
                lua_pushlightuserdata(L, (void*)hum);
                lua_pushinteger(L, mi);
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    Humanoid* h = static_cast<Humanoid*>(lua_touserdata(pL, lua_upvalueindex(1)));
                    const int which = (int)lua_tointeger(pL, lua_upvalueindex(2));
                    int fn_pos = -1;
                    for (int ai = 1; ai <= lua_gettop(pL); ai++)
                        if (lua_isfunction(pL, ai)) { fn_pos = ai; break; }
                    if (fn_pos == -1 || !h) return 0;
                    lua_State* main_L = gl_main_of(pL);
                    if (!main_L) main_L = pL;
                    lua_pushvalue(pL, fn_pos);
                    int ref = gl_ref(pL, -1);
                    lua_pop(pL, 1);
                    h->add_move_signal_callback(which, main_L, ref);
                    _gl_push_connection(pL, h, ref); return 1;
                }, "Connect", 2);
                lua_setfield(L, -2, "Connect");
                gl_signal_lowercase_aliases(L);
                return 1;
            }
        }
        if (strcmp(key, "HealthChanged") == 0) {
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)hum);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Humanoid* h = static_cast<Humanoid*>(lua_touserdata(pL, lua_upvalueindex(1)));
                int fn_pos = -1;
                for (int ai = 1; ai <= lua_gettop(pL); ai++) {
                    if (lua_isfunction(pL, ai)) { fn_pos = ai; break; }
                }
                if (fn_pos == -1 || !h) return 0;
                lua_State* main_L = gl_main_of(pL);
                if (!main_L) main_L = pL;
                lua_pushvalue(pL, fn_pos);
                int ref = gl_ref(pL, -1);
                lua_pop(pL, 1);
                h->add_health_changed_callback(main_L, ref);
                _gl_push_connection(pL, h, ref); return 1;
            }, "Connect", 1);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }
        if (strcmp(key, "StateChanged") == 0) {
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)hum);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Humanoid* h = static_cast<Humanoid*>(lua_touserdata(pL, lua_upvalueindex(1)));
                int fn_pos = -1;
                for (int ai = 1; ai <= lua_gettop(pL); ai++) {
                    if (lua_isfunction(pL, ai)) { fn_pos = ai; break; }
                }
                if (fn_pos == -1 || !h) return 0;
                lua_State* main_L = gl_main_of(pL);
                if (!main_L) main_L = pL;
                lua_pushvalue(pL, fn_pos);
                int ref = gl_ref(pL, -1);
                lua_pop(pL, 1);
                h->add_state_changed_callback(main_L, ref);
                _gl_push_connection(pL, h, ref); return 1;
            }, "Connect", 1);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }
    }

    // ── Humanoid 2D ───────────────────────────────────────────────
    Humanoid2D* hum2d = gl_cast<Humanoid2D>(n);
    if (hum2d) {
        if (strcmp(key, "Health")    == 0) { lua_pushnumber(L, hum2d->get_health());     return 1; }
        if (strcmp(key, "MaxHealth") == 0) { lua_pushnumber(L, hum2d->get_max_health()); return 1; }
        if (strcmp(key, "IsDead")    == 0) { lua_pushboolean(L, hum2d->get_health() <= 0.0f); return 1; }

        // WalkSpeed/JumpPower delegan al RobloxPlayer2D padre
        if (strcmp(key, "WalkSpeed") == 0) {
            RobloxPlayer2D* parent_player = gl_cast<RobloxPlayer2D>(n->get_parent());
            lua_pushnumber(L, parent_player ? parent_player->get_walk_speed() : 200.0);
            return 1;
        }
        if (strcmp(key, "JumpPower") == 0) {
            RobloxPlayer2D* parent_player = gl_cast<RobloxPlayer2D>(n->get_parent());
            // Return positive even if internally stored as negative (same convention as Roblox) / Devolver positivo aunque internamente sea negativo (misma convención que Roblox)
            lua_pushnumber(L, parent_player ? (double)(-parent_player->get_jump_power()) : 500.0);
            return 1;
        }

        if (strcmp(key, "TakeDamage") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                GodotObjectWrapper* w2 = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Humanoid2D* h = w2 ? gl_cast<Humanoid2D>(gow_node(w2)) : nullptr;
                if (h) h->take_damage((float)gl_num(pL, 2));
                return 0;
            }, "TakeDamage");
            return 1;
        }
        if (strcmp(key, "Heal") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                GodotObjectWrapper* w2 = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Humanoid2D* h = w2 ? gl_cast<Humanoid2D>(gow_node(w2)) : nullptr;
                if (h) h->heal((float)gl_num(pL, 2));
                return 0;
            }, "Heal");
            return 1;
        }
    }

    return -1;
}

#endif // LUAU_API_INDICE_PERSONAJE_H

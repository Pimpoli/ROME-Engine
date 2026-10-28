#ifndef LUAU_API_INDICE_INSTANCIA_H
#define LUAU_API_INDICE_INSTANCIA_H
#include "gl_cast.h"
#include "gl_carga_estado.h"   // RemoveDefaultLoadingScreen

// Lectura de propiedades desde Luau (__index de las instancias), primera zona: lo que vale para toda Instance (efectos, pivote de un
// personaje, OnInvoke, utilidades, StarterGui, Animator, DataModel, Player,
// Terrain, los Value, señales universales y Name/ClassName/Parent).
//
// Va aparte de luau_api.h (pasaba de 2.000 lineas) y se incluye desde alli,
// justo antes de godot_object_index_real, que llama a las zonas en orden.
// Devuelve cuantos valores deja en la pila, o -1 si la clave no es de
// esta zona y hay que seguir buscando.

// StarterPlayer: sus propiedades con el valor por defecto de Roblox (la zoom
// maxima como la del Player, 400). Devuelve 0 si no es suya, 1 numero, 2 booleano.
static inline int gl_starterplayer_prop(const char* key, double* defecto = nullptr) {
    struct D { const char* k; int tipo; double v; };
    static const D defs[] = {
        { "CameraMaxZoomDistance", 1, 400.0 }, { "CameraMinZoomDistance", 1, 0.5 },
        { "CharacterWalkSpeed", 1, 16.0 }, { "CharacterJumpPower", 1, 50.0 },
        { "CharacterJumpHeight", 1, 7.2 }, { "CharacterMaxSlopeAngle", 1, 89.0 },
        { "HealthDisplayDistance", 1, 100.0 }, { "NameDisplayDistance", 1, 100.0 },
        { "CameraMode", 1, 0.0 }, { "DevComputerMovementMode", 1, 0.0 },
        { "DevTouchMovementMode", 1, 0.0 }, { "DevComputerCameraMovementMode", 1, 0.0 },
        { "DevTouchCameraMovementMode", 1, 0.0 }, { "DevCameraOcclusionMode", 1, 0.0 },
        { "CharacterUseJumpPower", 2, 1.0 }, { "AutoJumpEnabled", 2, 1.0 },
        { "LoadCharacterAppearance", 2, 1.0 }, { "EnableMouseLockOption", 2, 1.0 },
        { "UserEmotesEnabled", 2, 1.0 }, { "AllowCustomAnimations", 2, 1.0 },
        { "GameSettingsAvatar", 1, 1.0 }, { "CharacterBreakJointsOnDeath", 2, 1.0 },
        { nullptr, 0, 0.0 } };
    for (int i = 0; defs[i].k; i++) {
        if (strcmp(key, defs[i].k) != 0) continue;
        if (defecto) *defecto = defs[i].v;
        return defs[i].tipo;
    }
    return 0;
}

inline int gl_index_instancia(lua_State* L, Node* n, const char* key) {
    // Efectos (gl_puente_efectos.h): ColorSequence, NumberRange, EnumItem...
    // Va antes de los caminos de Node3D, que se comian Rotation y Orientation.
    {
        const int r = gl_efectos_index(L, n, key);
        if (r >= 0) return r;
    }
    // Propiedades *Content (ImageContent, TextureContent...) y
    // MeshPart:ApplyMesh (gl_puente_editables.h). Antes que el respaldo de las
    // GUI, que leeria ImageContent como texto.
    if ((key[0] == 'A' && strcmp(key, "ApplyMesh") == 0) || gl_clave_content(key)) {
        const int r = gl_index_content(L, n, key);
        if (r >= 0) return r;
    }

    // ── Un PERSONAJE en Roblox ES un Model ────────────────────────────
    // GetPivot/PivotTo valen igual sobre el, y se usan a todas horas
    // (teletransportar, colocar al jugador, mirar donde esta). Aqui Character
    // es su propia clase y no RBXModel, asi que `char:GetPivot()` tumbaba el
    // script. El pivote de un personaje es su HumanoidRootPart, como alla.
    //
    // Va FUERA del switch por primera letra a proposito: ahi la rama 'G' no
    // veria nunca "PivotTo" (empieza por 'P') — me paso, y GetPivot funcionaba
    // mientras PivotTo seguia sin existir.
    {
        if ((strcmp(key, "GetPivot") == 0 || strcmp(key, "PivotTo") == 0) &&
            gl_cast<Node3D>(n) && !n->is_class("RBXModel") &&
            n->get_node_or_null(NodePath("Humanoid"))) {
                lua_pushboolean(L, strcmp(key, "GetPivot") == 0);
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    const bool leer = lua_toboolean(pL, lua_upvalueindex(1)) != 0;
                    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                    Node* nd = w ? gow_node(w) : nullptr;
                    Node3D* raiz = gl_cast<Node3D>(nd);
                    if (!raiz) { if (leer) lua_pushnil(pL); return leer ? 1 : 0; }
                    Node3D* hrp = gl_cast<Node3D>(
                        raiz->get_node_or_null(NodePath("HumanoidRootPart")));
                    Node3D* ref = hrp ? hrp : raiz;
                    if (leer) {
                        _gl_push_cframe_full(pL, ref->is_inside_tree()
                                                     ? ref->get_global_transform()
                                                     : ref->get_transform());
                        return 1;
                    }
                    // PivotTo(cf): mueve el personaje ENTERO, como Roblox.
                    if (!lua_istable(pL, 2)) return 0;
                    auto gf = [&](const char* k) -> float {
                        lua_getfield(pL, 2, k);
                        const float v = (float)lua_tonumber(pL, -1);
                        lua_pop(pL, 1);
                        return v;
                    };
                    Transform3D t;
                    t.origin = Vector3(gf("X"), gf("Y"), gf("Z"));
                    t.basis.rows[0] = Vector3(gf("m00"), gf("m01"), gf("m02"));
                    t.basis.rows[1] = Vector3(gf("m10"), gf("m11"), gf("m12"));
                    t.basis.rows[2] = Vector3(gf("m20"), gf("m21"), gf("m22"));
                    gl_set_part_global_transform(ref, t);
                    return 0;
            }, "GetPivot", 1);
            return 1;
        }
    }

    // GetPivot / PivotTo de una pieza suelta: su CFrame (PVInstance, como Model).
    if ((strcmp(key, "GetPivot") == 0 || strcmp(key, "PivotTo") == 0) && gl_cast<RobloxPart>(n)) {
        lua_pushboolean(L, strcmp(key, "GetPivot") == 0);
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            const bool leer = lua_toboolean(pL, lua_upvalueindex(1)) != 0;
            GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(pL, 1);
            RobloxPart* p = w ? gl_cast<RobloxPart>(gow_node(w)) : nullptr;
            if (!p) { if (leer) lua_pushnil(pL); return leer ? 1 : 0; }
            if (leer) { _gl_push_cframe_full(pL, gl_node_xf(p)); return 1; }
            if (!lua_istable(pL, 2)) return 0;
            auto gf = [&](const char* k) -> float {
                lua_getfield(pL, 2, k);
                const float v = (float)lua_tonumber(pL, -1);
                lua_pop(pL, 1);
                return v;
            };
            Transform3D t;
            t.origin = Vector3(gf("X"), gf("Y"), gf("Z"));
            t.basis.rows[0] = Vector3(gf("m00"), gf("m01"), gf("m02"));
            t.basis.rows[1] = Vector3(gf("m10"), gf("m11"), gf("m12"));
            t.basis.rows[2] = Vector3(gf("m20"), gf("m21"), gf("m22"));
            gl_set_part_global_transform(p, t);
            return 0;
        }, "GetPivot", 1);
        return 1;
    }

    // MeshPart.TextureID es como se llama en Roblox (TextureId es el de
    // SpecialMesh). El MeshPart de Instance.new es un Part con la clase de
    // MeshPart, sin la propiedad: la guarda en un meta.
    if ((strcmp(key, "TextureID") == 0 || strcmp(key, "TextureId") == 0) &&
        strcmp(gl_roblox_classname(n), "MeshPart") == 0) {
        static const StringName k_prop("TextureId");
        static const StringName k_meta("_glp_TextureId");
        String v;
        if (gl_has_property(n, "TextureId")) v = n->get(k_prop);
        else if (n->has_meta(k_meta)) v = n->get_meta(k_meta);
        lua_pushstring(L, v.utf8().get_data());
        return 1;
    }

    // StarterPlayer: lo que un script haya escrito, o lo de Roblox por defecto.
    static const StringName k_starter_player("StarterPlayer");
    if (n->get_name() == k_starter_player && strcmp(gl_roblox_classname(n), "StarterPlayer") == 0) {
        double v = 0.0;
        const int tipo = gl_starterplayer_prop(key, &v);
        if (tipo) {
            const Variant puesto = gl_starter_valor(n, key);
            if (puesto.get_type() != Variant::NIL && gl_variant_to_luau(L, puesto)) return 1;
            if (tipo == 2) lua_pushboolean(L, v != 0.0); else lua_pushnumber(L, v);
            return 1;
        }
    }

    // RemoteFunction.OnServerInvoke / OnClientInvoke: se ESCRIBIAN pero al
    // leerlas reventaba el script. En Roblox se leen (para comprobar si ya hay
    // manejador antes de pisarlo, que es lo que hace cualquier framework).
    if (RemoteFunctionNode* rfr = gl_cast<RemoteFunctionNode>(n)) {
        const bool srv = strcmp(key, "OnServerInvoke") == 0;
        if (srv || strcmp(key, "OnClientInvoke") == 0) {
            lua_State* vm = srv ? rfr->server_invoke_L : rfr->client_invoke_L;
            const int r   = srv ? rfr->server_invoke_ref : rfr->client_invoke_ref;
            // Una FUNCION no puede cruzar de una VM a otra (cada script es su
            // propia VM y los objetos no se comparten). Si quien lee es la
            // misma VM que registro el manejador — el caso normal — se
            // devuelve tal cual; si es otra, se dice que SI hay manejador
            // devolviendo true, que es lo unico honesto que se puede decir.
            // La comparacion va contra el estado PRINCIPAL de quien lee: `L` es
            // la corrutina del script y `vm` el principal donde se anclo el ref.
            // Comparando en crudo nunca coincidian y se devolvia `true` en vez
            // de la funcion, aun leyendola desde el mismo script.
            if (vm && r != LUA_NOREF && gl_state_alive(vm)) {
                if (vm == gl_main_of(L)) lua_rawgeti(L, LUA_REGISTRYINDEX, r);
                else                     lua_pushboolean(L, 1);
            } else lua_pushnil(L);
            return 1;
        }
    }

    // ── BindableFunction: Invoke / OnInvoke ───────────────────────────
    // Un BindableEvent avisa; un BindableFunction PREGUNTA y espera respuesta.
    // Es la via normal para que dos scripts del mismo lado se hablen sin red.
    if (BindableEventNode* bfn = gl_cast<BindableEventNode>(n)) {
        // Leer OnInvoke, igual que OnServerInvoke: un framework mira si ya hay
        // manejador antes de pisarlo. Faltaba y devolvia nil aunque Invoke SI
        // funcionara, que es lo mas confuso posible.
        if (bfn->es_funcion && strcmp(key, "OnInvoke") == 0) {
            if (bfn->inv_L && bfn->inv_ref >= 0 && gl_state_alive(bfn->inv_L)) {
                // Una funcion no cruza de VM a VM: si lee la misma que registro,
                // se devuelve tal cual; si es otra, true (hay manejador).
                if (bfn->inv_L == gl_main_of(L)) lua_rawgeti(L, LUA_REGISTRYINDEX, bfn->inv_ref);
                else                             lua_pushboolean(L, 1);
            } else lua_pushnil(L);
            return 1;
        }
        if (bfn->es_funcion && strcmp(key, "Invoke") == 0) {
            lua_pushlightuserdata(L, (void*)bfn);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                BindableEventNode* b = (BindableEventNode*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!b || b->inv_ref < 0 || !b->inv_L || !gl_state_alive(b->inv_L)) return 0;
                // Como tarea: el manejador puede ceder y su error llega aqui
                // (gl_invocar.h). En la misma VM los valores pasan tal cual.
                const int nargs = lua_gettop(pL) - 1;   // arg 1 = self
                return gl_invocar_local(pL, 2, nargs, b->inv_L, b->inv_ref, nullptr, false,
                                        "BindableFunction:Invoke");
            }, "Invoke", 1);
            return 1;
        }
        if (bfn->es_funcion && strcmp(key, "OnInvoke") == 0) {
            if (bfn->inv_ref >= 0 && bfn->inv_L && gl_state_alive(bfn->inv_L)) {
                if (bfn->inv_L == L) lua_rawgeti(L, LUA_REGISTRYINDEX, bfn->inv_ref);
                else                 lua_pushboolean(L, 1);   // no cruza de VM
            } else lua_pushnil(L);
            return 1;
        }
    }

    // ── Utilidades de Instance (Ola 2) ────────────────────────────────
    if (strcmp(key, "GetDebugId") == 0) {
        lua_pushcfunction(L, [](lua_State* pL) -> int {
            GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(pL, 1);
            Node* nd = w ? gow_node(w) : nullptr;
            String s = String::num_uint64(nd ? (uint64_t)nd->get_instance_id() : 0);
            lua_pushstring(pL, s.utf8().get_data()); return 1;
        }, "GetDebugId"); return 1;
    }
    if (strcmp(key, "GetActor") == 0) {
        lua_pushcfunction(L, [](lua_State* pL) -> int { lua_pushnil(pL); return 1; }, "GetActor"); return 1;
    }

    // ── StarterGui: SetCoreGuiEnabled / SetCore / GetCore ─────────────
    // CASI TODO HUD propio empieza apagando la interfaz "core" de Roblox
    // (mochila, lista de jugadores, chat oficial). Aqui la UI core equivalente
    // es la del MOTOR (chat de roblox_chat.h, barra del menu de Luau),
    // asi que ademas de recordar el estado hay que APLICARLO de verdad: antes
    // solo se guardaba el meta y el chat del motor seguia en pantalla aunque
    // el juego lo hubiera desactivado (ver gl_coregui_push, mas arriba).
    if (n->get_name() == StringName("StarterGui")) {
        if (strcmp(key, "SetCoreGuiEnabled") == 0 || strcmp(key, "SetCoreGuiEnabledAll") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Node* sg = w ? gow_node(w) : nullptr;
                if (!sg) return 0;
                const int base = lua_isuserdata(pL, 1) ? 1 : 0;   // self implicito
                const String which = gl_coregui_which(pL, base + 1);
                const bool on = lua_toboolean(pL, base + 2) != 0;
                // "All" escribe CADA tipo por separado: asi un
                // SetCoreGuiEnabled(Chat, true) posterior re-enciende solo el
                // chat, igual que en Roblox.
                if (which == "All") {
                    for (int i = 0; i < kGLCoreGuiCount; i++) {
                        if (strcmp(kGLCoreGuiNames[i], "All") == 0) continue;
                        sg->set_meta(String("_gl_coregui_") + kGLCoreGuiNames[i], on);
                        gl_coregui_push(sg, String(kGLCoreGuiNames[i]), on);
                    }
                } else {
                    sg->set_meta(String("_gl_coregui_") + which, on);
                    gl_coregui_push(sg, which, on);
                }
                return 0;
            }, "SetCoreGuiEnabled");
            return 1;
        }
        if (strcmp(key, "GetCoreGuiEnabled") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Node* sg = w ? gow_node(w) : nullptr;
                const int base = lua_isuserdata(pL, 1) ? 1 : 0;
                const String which = gl_coregui_which(pL, base + 1);
                bool en = true;
                if (which == "All") {
                    // La doc no fija que devuelve All; el AND de todos es lo
                    // coherente con "Refers to all available core UI":
                    // https://create.roblox.com/docs/reference/engine/enums/CoreGuiType
                    for (int i = 0; i < kGLCoreGuiCount && en; i++)
                        if (strcmp(kGLCoreGuiNames[i], "All") != 0)
                            en = gl_coregui_enabled_meta(sg, String(kGLCoreGuiNames[i]));
                } else {
                    en = gl_coregui_enabled_meta(sg, which);
                }
                lua_pushboolean(pL, en ? 1 : 0);
                return 1;
            }, "GetCoreGuiEnabled");
            return 1;
        }
        if (strcmp(key, "SetCore") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                // "TopbarEnabled" es el UNICO camino oficial para quitar la
                // topbar: "The top bar cannot be disabled using this function
                // [SetCoreGuiEnabled] ... use StarterGui:SetCore with the
                // TopbarEnabled parameter":
                // https://create.roblox.com/docs/reference/engine/classes/StarterGui#SetCoreGuiEnabled
                GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                Node* sg = w ? gow_node(w) : nullptr;
                const int base = lua_isuserdata(pL, 1) ? 1 : 0;
                const String param = lua_isstring(pL, base + 1) ? String(lua_tostring(pL, base + 1)) : String();
                if (sg && param == "TopbarEnabled") {
                    const bool on = lua_toboolean(pL, base + 2) != 0;
                    sg->set_meta("_gl_topbar_enabled", on);
                    // La barra superior es del menu de Luau: se deja el estado
                    // en el meta y el modulo lo consulta.
                }
                // Los demas parametros de SetCore se siguen aceptando sin
                // efecto (mejor tragarlos que matar el script en su Init).
                return 0;
            }, "SetCore");
            return 1;
        }
        if (strcmp(key, "GetCore") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int { lua_pushnil(pL); return 1; }, "GetCore");
            return 1;
        }
    }

    // ── Animator -> delega en el Humanoid padre ───────────────────────
    // (la tabla de propiedades de StarterPlayer, que tambien usa la escritura,
    // esta en gl_starterplayer_prop, arriba del todo)
    // En Roblox lo normal es humanoid.Animator:LoadAnimation(anim). El Animator
    // que trae un place importado es una instancia pelada, asi que se reenvia
    // al Humanoid (sin esto los NPC no animaban: "LoadAnimation is not a valid
    // member of RBXInstance").
    // F7: el Animator, el Humanoid y el AnimationController comparten un
    // animador por rig (gl_animador.h). Primero la clave, que es lo barato:
    // por aqui pasa casi toda lectura.
    if ((key[0] == 'L' && strcmp(key, "LoadAnimation") == 0) ||
        (key[0] == 'G' && strcmp(key, "GetPlayingAnimationTracks") == 0) ||
        (key[0] == 'A' && (strcmp(key, "AnimationPlayed") == 0 || strcmp(key, "ApplyJointVelocities") == 0)) ||
        (key[0] == 'S' && strcmp(key, "StepAnimation") == 0)) {
        if (gl_es_dueno_de_animacion(n)) {
            const int r = gl_animador_index(L, n, key);
            if (r >= 0) return r;
        }
    }

    // ── DataModel (game): PlaceId/JobId/CreatorId/BindToClose… (Ola 2) ──
    if (strcmp(key,"PlaceId")==0 || strcmp(key,"GameId")==0 || strcmp(key,"JobId")==0 ||
        strcmp(key,"CreatorId")==0 || strcmp(key,"CreatorType")==0 || strcmp(key,"PrivateServerId")==0 ||
        strcmp(key,"PrivateServerOwnerId")==0 || strcmp(key,"PlaceVersion")==0 ||
        strcmp(key,"BindToClose")==0 || strcmp(key,"IsLoaded")==0) {
        String _cls = n->get_class();
        bool _es_dm = (_cls == "RobloxGame3D" || _cls == "RobloxGame2D" || _cls == "RobloxTemplate");
        // Raiz de un place importado (Node con meta DataModel) y el Window raiz
        // (donde cae `game` si nadie cumple): tambien son el DataModel. Sin esto
        // game.PlaceId/IsLoaded/PlaceVersion morian en places importados.
        if (!_es_dm && n->has_meta("__rbx_class"))
            _es_dm = (String(n->get_meta("__rbx_class")) == "DataModel");
        if (!_es_dm && n->get_parent() == nullptr && n->is_inside_tree()) _es_dm = true;
        if (_es_dm) {
            if (strcmp(key,"PlaceId")==0 || strcmp(key,"GameId")==0 || strcmp(key,"CreatorId")==0 ||
                strcmp(key,"PrivateServerOwnerId")==0)      { lua_pushnumber(L, 0); return 1; }
            if (strcmp(key,"PlaceVersion")==0)              { lua_pushnumber(L, 1); return 1; }
            if (strcmp(key,"CreatorType")==0)               { gl_push_enum_item(L, "CreatorType", 0); return 1; }   // Enum.CreatorType.User
            // JobId (1.14.16): GUID único de ESTA instancia de servidor. Antes
            // era "" siempre. Sin sesión de red sigue vacío, como en Studio.
            if (strcmp(key,"JobId")==0) {
                lua_pushstring(L, gl_net_role() == 0 ? "" : gl_ensure_job_id().utf8().get_data());
                return 1;
            }
            if (strcmp(key,"PrivateServerId")==0) { lua_pushstring(L, ""); return 1; }
            if (strcmp(key,"IsLoaded")==0) {
                lua_pushcfunction(L, [](lua_State* pL) -> int { lua_pushboolean(pL, 1); return 1; }, "IsLoaded"); return 1;
            }
            if (strcmp(key,"BindToClose")==0) {
                lua_pushcfunction(L, [](lua_State* pL) -> int {
                    // Acepta game:BindToClose(fn) (self+fn) o game.BindToClose(fn)
                    int fnidx = lua_isfunction(pL, 2) ? 2 : (lua_isfunction(pL, 1) ? 1 : 0);
                    if (!fnidx) return 0;
                    lua_State* mL = gl_main_of(pL);
                    if (!mL) mL = pL;
                    int ref = gl_ref(pL, fnidx);   // ref en el registro compartido
                    gl_add_close_cb(mL, ref);
                    return 0;
                }, "BindToClose"); return 1;
            }
        }
    }

    // ── PlayerObject ("Player", Player ≠ Character) ────────────────────────
    {
        PlayerObject* plr = gl_cast<PlayerObject>(n);
        if (plr) {
            if (strcmp(key, "UserId") == 0)      { lua_pushnumber(L, (double)plr->user_id); return 1; }
            if (strcmp(key, "DisplayName") == 0) { lua_pushstring(L, plr->display_name.utf8().get_data()); return 1; }
            if (strcmp(key, "AccountAge") == 0)  { lua_pushnumber(L, (double)plr->account_age); return 1; }
            // Sin cuenta Roblox detras: nadie es Premium (Enum.MembershipType.None).
            if (strcmp(key, "MembershipType") == 0) { uis_push_enum(L, "MembershipType", 0); return 1; }
            if (strcmp(key, "HasRobloxSubscription") == 0) { lua_pushboolean(L, 0); return 1; }
            // El idioma del sistema con el formato de Roblox: "es_ES" -> "es-es".
            if (strcmp(key, "LocaleId") == 0) {
                const String loc = OS::get_singleton()->get_locale().replace("_", "-").to_lower();
                lua_pushstring(L, loc.utf8().get_data());
                return 1;
            }
            // La apariencia es la del personaje por defecto: esta en cuanto hay personaje.
            if (strcmp(key, "HasAppearanceLoaded") == 0) {
                lua_pushcfunction(L, [](lua_State* pL) -> int {
                    GodotObjectWrapper* w2 = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                    PlayerObject* p = w2 ? gl_cast<PlayerObject>(gow_node(w2)) : nullptr;
                    lua_pushboolean(pL, p && p->get_character() != nullptr);
                    return 1;
                }, "HasAppearanceLoaded");
                return 1;
            }
            // Sin cuenta de Roblox detras no hay insignia de verificado.
            if (strcmp(key, "HasVerifiedBadge") == 0) { lua_pushboolean(L, 0); return 1; }
            if (strcmp(key, "ClassName") == 0)   { lua_pushstring(L, "Player"); return 1; }
            // Player.Chatted(mensaje): ver PlayerObject::gl_fire_chatted.
            if (strcmp(key, "Chatted") == 0) {
                lua_newtable(L);
                lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)plr->get_instance_id());
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    const uint64_t id = (uint64_t)(uintptr_t)lua_touserdata(pL, lua_upvalueindex(1));
                    PlayerObject* p = gl_cast<PlayerObject>(ObjectDB::get_instance(ObjectID(id)));
                    if (!p || !lua_isfunction(pL, 2)) { lua_pushnil(pL); return 1; }
                    const int ref = gl_ref(pL, 2);
                    p->add_chatted_cb(gl_main_of(pL), ref);
                    _gl_push_connection(pL, p, ref);
                    return 1;
                }, "Connect", 1);
                lua_setfield(L, -2, "Connect");
                gl_signal_lowercase_aliases(L);
                return 1;
            }
            // Sin servidores de Roblox detras: Idled y OnTeleport no llegan
            // nunca, pero conectarse no debe matar el script.
            if (strcmp(key, "Idled") == 0 || strcmp(key, "OnTeleport") == 0) {
                _gl_push_inert_signal(L);
                return 1;
            }
            if (strcmp(key, "Team") == 0) {
                Node* t = plr->get_team();
                if (t) wrap_node(L, t); else lua_pushnil(L);
                return 1;
            }
            if (strcmp(key, "Neutral") == 0)     { lua_pushboolean(L, plr->get_team() ? 0 : 1); return 1; }
            if (strcmp(key, "TeamColor") == 0) {
                Node* t = plr->get_team();
                push_brickcolor(L, t ? String(t->get("TeamColor")) : plr->team_color);
                return 1;
            }
            if (strcmp(key, "FollowUserId") == 0){ lua_pushnumber(L, 0); return 1; }
            // Miembros reales de Player que antes vivían en el personaje (compat):
            if (strcmp(key, "CameraMode") == 0)  { lua_pushnumber(L, plr->camera_mode); return 1; }
            if (strcmp(key, "CameraMinZoomDistance") == 0) { lua_pushnumber(L, 0.5); return 1; }
            if (strcmp(key, "CameraMaxZoomDistance") == 0) { lua_pushnumber(L, 400); return 1; }
            if (strcmp(key, "AutoJumpEnabled") == 0)       { lua_pushboolean(L, 1); return 1; }
            if (strcmp(key, "DevComputerMovementMode") == 0 || strcmp(key, "DevTouchMovementMode") == 0 ||
                strcmp(key, "DevCameraOcclusionMode") == 0 || strcmp(key, "DevComputerCameraMode") == 0)
                                                 { lua_pushnumber(L, 0); return 1; }
            if (strcmp(key, "GetRankInGroup") == 0) {
                lua_pushcfunction(L, [](lua_State* pL) -> int { lua_pushnumber(pL, 0); return 1; }, "GetRankInGroup"); return 1;
            }
            if (strcmp(key, "GetRoleInGroup") == 0) {
                lua_pushcfunction(L, [](lua_State* pL) -> int { lua_pushstring(pL, "Guest"); return 1; }, "GetRoleInGroup"); return 1;
            }
            if (strcmp(key, "IsInGroup") == 0) {
                lua_pushcfunction(L, [](lua_State* pL) -> int { lua_pushboolean(pL, 0); return 1; }, "IsInGroup"); return 1;
            }
            if (strcmp(key, "GetFriendStatus") == 0) {
                // Enum.FriendStatus.NotFriend (antes la cadena "NotFriend")
                lua_pushcfunction(L, [](lua_State* pL) -> int { gl_push_enum_item(pL, "FriendStatus", 1); return 1; }, "GetFriendStatus"); return 1;
            }
            // Sin backend social nadie es amigo de nadie, pero el metodo debe
            // existir: iba dentro de un pcall que solo mira `success`, asi que
            // el buff por amigos se quedaba a medias sin decir nada.
            if (strcmp(key, "IsFriendsWith") == 0) {
                lua_pushcfunction(L, [](lua_State* pL) -> int { lua_pushboolean(pL, 0); return 1; }, "IsFriendsWith"); return 1;
            }
            if (strcmp(key, "GetFriendsOnline") == 0 || strcmp(key, "GetCharacterAppearanceInfoAsync") == 0 ||
                strcmp(key, "GetCharacterAppearanceAsync") == 0 || strcmp(key, "GetJoinData") == 0) {
                lua_pushcfunction(L, [](lua_State* pL) -> int { lua_newtable(pL); return 1; }, "PlayerStub"); return 1;
            }
            if (strcmp(key, "Character") == 0) {
                Node* c = plr->get_character();
                if (c) wrap_node(L, c); else lua_pushnil(L);
                return 1;
            }
            if (strcmp(key, "GetCharacter") == 0) {
                lua_pushlightuserdata(L, (void*)plr);
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    PlayerObject* p = (PlayerObject*)lua_touserdata(pL, lua_upvalueindex(1));
                    Node* c = p ? p->get_character() : nullptr;
                    if (c) wrap_node(pL, c); else lua_pushnil(pL);
                    return 1;
                }, "GetCharacter", 1); return 1;
            }
            if (strcmp(key, "IsA") == 0) {
                lua_pushcfunction(L, [](lua_State* pL) -> int {
                    const char* q = gl_str(pL, 2);
                    lua_pushboolean(pL, (strcmp(q, "Player") == 0 || strcmp(q, "Instance") == 0) ? 1 : 0);
                    return 1;
                }, "IsA"); return 1;
            }
            if (strcmp(key, "Kick") == 0) {
                lua_pushlightuserdata(L, (void*)plr);
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    PlayerObject* p = (PlayerObject*)lua_touserdata(pL, lua_upvalueindex(1));
                    if (!p) return 0;
                    String msg = lua_isstring(pL, 2) ? String::utf8(lua_tostring(pL, 2)) : String();
                    if (Node* ns = _gl_find_network_service(p)) ns->call("net_kick_player", (Object*)p, msg);
                    return 0;
                }, "Kick", 1);
                return 1;
            }
            // GetMouse (1.14.10 Finish): crea (una vez) el RobloxMouse del
            // jugador y lo devuelve. Solo tiene sentido en el jugador LOCAL:
            // el ratón es de esta máquina (en Roblox igual, es de LocalScript).
            if (strcmp(key, "GetMouse") == 0) {
                lua_pushlightuserdata(L, (void*)plr);
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    PlayerObject* p = (PlayerObject*)lua_touserdata(pL, lua_upvalueindex(1));
                    if (!p || !p->is_inside_tree()) { lua_pushnil(pL); return 1; }
                    if (!p->is_local) {
                        luaL_error(pL, "GetMouse: only the LocalPlayer has a Mouse");
                        return 0;
                    }
                    RobloxMouse* m = gl_cast<RobloxMouse>(p->get_node_or_null(NodePath("Mouse")));
                    if (!m) {
                        m = memnew(Mouse);
                        m->set_name("Mouse");
                        p->add_child(m);
                    }
                    m->character_id = (uint64_t)0;
                    if (Node* ch = p->get_character()) m->character_id = (uint64_t)ch->get_instance_id();
                    wrap_node(pL, m);
                    return 1;
                }, "GetMouse", 1);
                return 1;
            }
            // LoadCharacterAsync es el mismo respawn; en Roblox la unica
            // diferencia es que espera a que el personaje este listo.
            if (strcmp(key, "LoadCharacter") == 0 || strcmp(key, "LoadCharacterAsync") == 0) {
                lua_pushlightuserdata(L, (void*)plr);
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    PlayerObject* p = (PlayerObject*)lua_touserdata(pL, lua_upvalueindex(1));
                    if (!p) return 0;
                    if (Node* ns = _gl_find_network_service(p)) { ns->call("net_load_character", (Object*)p); return 0; }
                    // Sin NetworkService (juego normal sin red): respawn local.
                    Node* ps = p->get_parent();
                    if (ps && ps->has_method("load_local_character")) ps->call("load_local_character");
                    return 0;
                }, "LoadCharacter", 1);
                return 1;
            }
            // Ping en SEGUNDOS (como Roblox). Antes no existía en el runtime pese
            // a estar anunciado en el autocompletado: llamarlo reventaba.
            if (strcmp(key, "GetNetworkPing") == 0) {
                lua_pushlightuserdata(L, (void*)plr);
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    PlayerObject* p = (PlayerObject*)lua_touserdata(pL, lua_upvalueindex(1));
                    Node* ns = p ? _gl_find_network_service(p) : nullptr;
                    lua_pushnumber(pL, ns ? (double)(ns->call("net_ping_for_player", (Object*)p)) : 0.0);
                    return 1;
                }, "GetNetworkPing", 1);
                return 1;
            }
            if (strcmp(key, "CharacterAdded") == 0) {
                lua_newtable(L);
                lua_pushlightuserdata(L, (void*)plr);
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    PlayerObject* p = (PlayerObject*)lua_touserdata(pL, lua_upvalueindex(1));
                    if (!p || !lua_isfunction(pL, 2)) { lua_pushnil(pL); return 1; }
                    lua_State* mL = gl_main_of(pL); if (!mL) mL = pL;
                    int ref = gl_ref(pL, 2); p->add_char_added_cb(mL, ref); _gl_push_connection(pL, p, ref); return 1;
                }, "Connect", 1);
                lua_setfield(L, -2, "Connect");
                gl_signal_lowercase_aliases(L);
                lua_pushlightuserdata(L, (void*)plr);
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    PlayerObject* p = (PlayerObject*)lua_touserdata(pL, lua_upvalueindex(1));
                    if (!p) { lua_pushnil(pL); return 1; }
                    lua_State* mL = gl_main_of(pL); if (!mL) mL = pL;
                    p->add_char_added_wait(pL, mL);
                    return gl_yield_externo(pL, "__WAIT_SIGNAL__");
                }, "Wait", 1);
                lua_setfield(L, -2, "Wait");
                return 1;
            }
            if (strcmp(key, "CharacterRemoving") == 0 || strcmp(key, "CharacterAppearanceLoaded") == 0) {
                bool removing = (strcmp(key, "CharacterRemoving") == 0);
                lua_newtable(L);
                lua_pushlightuserdata(L, (void*)plr);
                lua_pushboolean(L, removing ? 1 : 0);
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    PlayerObject* p = (PlayerObject*)lua_touserdata(pL, lua_upvalueindex(1));
                    bool rem = lua_toboolean(pL, lua_upvalueindex(2));
                    if (!p || !lua_isfunction(pL, 2)) { lua_pushnil(pL); return 1; }
                    lua_State* mL = gl_main_of(pL); if (!mL) mL = pL;
                    int ref = gl_ref(pL, 2);
                    if (rem) p->add_char_removing_cb(mL, ref); else p->add_char_appearance_cb(mL, ref);
                    _gl_push_connection(pL, p, ref); return 1;
                }, "Connect", 2);
                lua_setfield(L, -2, "Connect");
                gl_signal_lowercase_aliases(L);
                return 1;
            }
            // Name, Parent, GetChildren, WaitForChild… caen al manejo genérico.
        }
    }

    // ── RobloxTerrain (Workspace.Terrain): FillBlock/FillBall/FillRegion/Clear ─
    {
        RobloxTerrain* terr = gl_cast<RobloxTerrain>(n);
        if (terr) {
            if (strcmp(key, "FillBlock") == 0) {
                lua_pushlightuserdata(L, (void*)terr);
                lua_pushcclosure(L, [](lua_State* LL) -> int {
                    RobloxTerrain* t = (RobloxTerrain*)lua_touserdata(LL, lua_upvalueindex(1));
                    if (!t) return 0;
                    // arg2 = CFrame (tabla con X/Y/Z), arg3 = size (Vector3), arg4 = material
                    float px = 0; float py = 0; float pz = 0;
                    if (lua_istable(LL, 2)) {
                        lua_getfield(LL, 2, "X"); px = (float)lua_tonumber(LL, -1); lua_pop(LL, 1);
                        lua_getfield(LL, 2, "Y"); py = (float)lua_tonumber(LL, -1); lua_pop(LL, 1);
                        lua_getfield(LL, 2, "Z"); pz = (float)lua_tonumber(LL, -1); lua_pop(LL, 1);
                    }
                    Vector3 size(4, 4, 4);
                    if (LuauVector3* v = check_vector3(LL, 3)) size = Vector3(v->x, v->y, v->z);
                    t->fill_block(Vector3(px, py, pz), size, (int)luaL_optnumber(LL, 4, 0));
                    return 0;
                }, "FillBlock", 1); return 1;
            }
            if (strcmp(key, "FillBall") == 0) {
                lua_pushlightuserdata(L, (void*)terr);
                lua_pushcclosure(L, [](lua_State* LL) -> int {
                    RobloxTerrain* t = (RobloxTerrain*)lua_touserdata(LL, lua_upvalueindex(1));
                    if (!t) return 0;
                    Vector3 c(0, 0, 0);
                    if (LuauVector3* v = check_vector3(LL, 2)) c = Vector3(v->x, v->y, v->z);
                    t->fill_ball(c, (float)luaL_optnumber(LL, 3, 4), (int)luaL_optnumber(LL, 4, 0));
                    return 0;
                }, "FillBall", 1); return 1;
            }
            if (strcmp(key, "FillRegion") == 0) {
                lua_pushlightuserdata(L, (void*)terr);
                lua_pushcclosure(L, [](lua_State* LL) -> int {
                    RobloxTerrain* t = (RobloxTerrain*)lua_touserdata(LL, lua_upvalueindex(1));
                    if (!t) return 0;
                    Vector3 mn(0, 0, 0); Vector3 mx(0, 0, 0);
                    if (LuauVector3* v = check_vector3(LL, 2)) mn = Vector3(v->x, v->y, v->z);
                    if (LuauVector3* v = check_vector3(LL, 3)) mx = Vector3(v->x, v->y, v->z);
                    t->fill_region(mn, mx, (int)luaL_optnumber(LL, 4, 0));
                    return 0;
                }, "FillRegion", 1); return 1;
            }
            if (strcmp(key, "Clear") == 0) {
                lua_pushlightuserdata(L, (void*)terr);
                lua_pushcclosure(L, [](lua_State* LL) -> int {
                    RobloxTerrain* t = (RobloxTerrain*)lua_touserdata(LL, lua_upvalueindex(1));
                    if (t) t->clear_terrain();
                    return 0;
                }, "Clear", 1); return 1;
            }
            // ReadVoxels devuelve (materials, occupancy) vacíos; el resto no-op.
            // (MundoVoxel construye su propia malla; estos evitan que scripts que
            //  los llamen se rompan.)
            if (strcmp(key, "ReadVoxels") == 0) {
                lua_pushcfunction(L, [](lua_State* LL) -> int {
                    lua_newtable(LL); lua_newtable(LL); return 2;
                }, "ReadVoxels"); return 1;
            }
            if (strcmp(key, "WriteVoxels") == 0 || strcmp(key, "SetMaterialColor") == 0 ||
                strcmp(key, "GetMaterialColor") == 0 || strcmp(key, "ReplaceMaterial") == 0) {
                lua_pushcfunction(L, [](lua_State* LL) -> int { return 0; }, "TerrainNoop"); return 1;
            }
        }
    }

    // ── RobloxValue (NumberValue/IntValue/StringValue/…): .Changed, ClassName, IsA ─
    // .Value se resuelve por el puente _gl_bridge más abajo; aquí solo lo específico.
    {
        RobloxValue* rval = gl_cast<RobloxValue>(n);
        if (rval) {
            if (strcmp(key, "ClassName") == 0) {
                lua_pushstring(L, rval->roblox_class.utf8().get_data()); return 1;
            }
            if (strcmp(key, "Changed") == 0) {
                lua_newtable(L);
                lua_pushlightuserdata(L, (void*)rval);
                lua_pushcclosure(L, [](lua_State* LL) -> int {
                    RobloxValue* d = (RobloxValue*)lua_touserdata(LL, lua_upvalueindex(1));
                    if (!d || !lua_isfunction(LL, 2)) { lua_pushnil(LL); return 1; }
                    int ref = gl_ref(LL, 2);
                    d->add_changed_cb(gl_main_of(LL), ref); _gl_push_connection(LL, d, ref); return 1;
                }, "Connect", 1);
                lua_setfield(L, -2, "Connect");
                gl_signal_lowercase_aliases(L); return 1;
            }
            if (strcmp(key, "GetPropertyChangedSignal") == 0) {
                lua_pushlightuserdata(L, (void*)rval);
                lua_pushcclosure(L, [](lua_State* LL) -> int {
                    RobloxValue* d = (RobloxValue*)lua_touserdata(LL, lua_upvalueindex(1));
                    lua_newtable(LL);
                    lua_pushlightuserdata(LL, (void*)d);
                    lua_pushcclosure(LL, [](lua_State* L2) -> int {
                        RobloxValue* d2 = (RobloxValue*)lua_touserdata(L2, lua_upvalueindex(1));
                        if (!d2 || !lua_isfunction(L2, 2)) { lua_pushnil(L2); return 1; }
                        int ref = gl_ref(L2, 2);
                        d2->add_changed_cb(gl_main_of(L2), ref); _gl_push_connection(L2, d2, ref); return 1;
                    }, "Connect", 1);
                    lua_setfield(LL, -2, "Connect");
                    gl_signal_lowercase_aliases(LL); return 1;
                }, "GetPropertyChangedSignal", 1); return 1;
            }
            if (strcmp(key, "IsA") == 0) {
                lua_pushlightuserdata(L, (void*)rval);
                lua_pushcclosure(L, [](lua_State* LL) -> int {
                    RobloxValue* d = (RobloxValue*)lua_touserdata(LL, lua_upvalueindex(1));
                    const char* q = gl_str(LL, 2);
                    bool ok = d && (d->roblox_class == String(q) ||
                                    strcmp(q, "Instance") == 0 || strcmp(q, "ValueBase") == 0);
                    lua_pushboolean(LL, ok ? 1 : 0); return 1;
                }, "IsA", 1); return 1;
            }
        }
    }

    if (strcmp(key, "Destroy")          == 0) { lua_pushcfunction(L, method_destroy,                   "Destroy");                return 1; }
    if (strcmp(key, "Clone")            == 0) { lua_pushcfunction(L, method_clone,                     "Clone");                  return 1; }
    if (strcmp(key, "ClearAllChildren") == 0) { lua_pushcfunction(L, method_clearallchildren,          "ClearAllChildren");       return 1; }
    // WaitForChild NO va aqui: lo implementa el __index local de luau_script.h
    // (suspende la corrutina de verdad, como Roblox).
    if (strcmp(key, "FindFirstChild")   == 0) { lua_pushcfunction(L, method_findfirstchild,            key);                      return 1; }
    if (strcmp(key, "FindFirstChildOfClass") == 0 ||
        strcmp(key, "FindFirstChildWhichIsA")== 0) { lua_pushcfunction(L, method_findfirstchild_whichisa, key);                   return 1; }
    if (strcmp(key, "FindFirstAncestor")     == 0) { lua_pushcfunction(L, method_findfirstancestor,    "FindFirstAncestor");      return 1; }
    if (strcmp(key, "FindFirstAncestorOfClass") == 0 ||
        strcmp(key, "FindFirstAncestorWhichIsA") == 0) { lua_pushcfunction(L, method_findfirstancestorofclass, key);              return 1; }
    if (strcmp(key, "IsAncestorOf")     == 0) { lua_pushcfunction(L, method_isancestorof,             "IsAncestorOf");            return 1; }
    if (strcmp(key, "IsDescendantOf")   == 0) { lua_pushcfunction(L, method_isdescendantof,           "IsDescendantOf");          return 1; }
    if (strcmp(key, "GetFullName")      == 0) { lua_pushcfunction(L, method_getfullname,              "GetFullName");             return 1; }
    if (strcmp(key, "GetChildren")      == 0) { lua_pushcfunction(L, method_getchildren,              "GetChildren");             return 1; }
    if (strcmp(key, "GetDescendants")   == 0) { lua_pushcfunction(L, method_getdescendants,           "GetDescendants");          return 1; }
    if (strcmp(key, "IsA")              == 0) { lua_pushcfunction(L, method_isa,                      "IsA");                     return 1; }
    if (strcmp(key, "SetAttribute")     == 0) { lua_pushcfunction(L, method_setattribute,             "SetAttribute");            return 1; }
    if (strcmp(key, "GetAttribute")     == 0) { lua_pushcfunction(L, method_getattribute,             "GetAttribute");            return 1; }
    if (strcmp(key, "GetAttributes")    == 0) { lua_pushcfunction(L, method_getattributes,            "GetAttributes");           return 1; }
    if (strcmp(key, "GetService")       == 0) { lua_pushcfunction(L, method_getservice,               "GetService");              return 1; }
    // Instance:AddTag/RemoveTag/HasTag/GetTags = los de CollectionService (su
    // shim de Luau, que ademas dispara GetInstanceAddedSignal). No existian en
    // la instancia y `parte:AddTag("Lava")` moria. Al nodo C++ del servicio no
    // se le tapan: los suyos reciben la instancia como argumento.
    if ((strcmp(key, "AddTag") == 0 || strcmp(key, "RemoveTag") == 0 ||
         strcmp(key, "HasTag") == 0 || strcmp(key, "GetTags") == 0) &&
        !gl_cast<CollectionService>(n)) {
        lua_pushstring(L, key);
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            const char* metodo = lua_tostring(pL, lua_upvalueindex(1));
            const int nargs = lua_gettop(pL);            // self [, tag]
            lua_getglobal(pL, "__SVC_CollectionService");
            if (!lua_istable(pL, -1)) { lua_pop(pL, 1); return 0; }
            lua_getfield(pL, -1, metodo);
            if (!lua_isfunction(pL, -1)) { lua_pop(pL, 2); return 0; }
            lua_insert(pL, -2);                          // [args..., fn, svc]
            for (int i = 1; i <= nargs; i++) lua_pushvalue(pL, i);
            lua_call(pL, nargs + 1, 1);
            return 1;
        }, key, 1);
        return 1;
    }
    // ── Propiedades del DataModel que consultan muchos juegos ────────────
    // Sin esto, un DataService que lee game.PlaceId tumbaba todo el modulo. No
    // hay servidores de Roblox detras, asi que se devuelven valores locales
    // coherentes y editables como atributos del nodo Game.
    if (n->is_class("RobloxTemplate") || n->get_name() == StringName("Game")) {
        if (strcmp(key, "PlaceId") == 0) {
            Variant v = n->has_meta("PlaceId") ? n->get_meta("PlaceId") : Variant();
            lua_pushnumber(L, v.get_type() == Variant::NIL ? 0.0 : (double)v);
            return 1;
        }
        if (strcmp(key, "PlaceVersion") == 0) {
            Variant v = n->has_meta("PlaceVersion") ? n->get_meta("PlaceVersion") : Variant();
            lua_pushnumber(L, v.get_type() == Variant::NIL ? 1.0 : (double)v);
            return 1;
        }
        if (strcmp(key, "JobId") == 0) {
            Variant v = n->has_meta("JobId") ? n->get_meta("JobId") : Variant();
            lua_pushstring(L, v.get_type() == Variant::NIL ? "" : String(v).utf8().get_data());
            return 1;
        }
        // game:BindToClose(fn) — Roblox lo llama al apagar el servidor para que el
        // juego guarde datos. Aqui se acepta y se guarda: si no existiera, los
        // modulos de datos (TycoonSystem, DataService) morian al arrancar.
        if (strcmp(key, "BindToClose") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                // Se registra el callback; se ejecuta al cerrar (NOTIFICATION_WM_CLOSE).
                for (int i = 1; i <= lua_gettop(pL); i++) {
                    if (!lua_isfunction(pL, i)) continue;
                    lua_pushvalue(pL, i);
                    gl_bind_to_close_refs().push_back(gl_ref(pL, -1));
                    lua_pop(pL, 1);
                    break;
                }
                return 0;
            }, "BindToClose");
            return 1;
        }
        // El juego ya esta cargado cuando un script puede preguntarlo.
        if (strcmp(key, "IsLoaded") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int { lua_pushboolean(pL, true); return 1; }, "IsLoaded");
            return 1;
        }
    }
    // Instance.Changed — salta al cambiar una propiedad, con su nombre. Lo que
    // avisa es la escritura desde Luau (gl_avisar_cambio_prop), la red y el texto
    // que teclea el usuario en un TextBox; no los atributos (AttributeChanged).
    // Los Value (IntValue...) tienen el suyo, que recibe el valor nuevo.
    if (strcmp(key, "Changed") == 0 && !n->is_class("ValueBase")) {
        _gl_push_watch_signal(L, n, GL_VIGILA_CHANGED, "");
        return 1;
    }

    // AttributeChanged(nombre) — cualquier atributo del objeto.
    if (strcmp(key, "AttributeChanged") == 0) {
        _gl_push_watch_signal(L, n, GL_VIGILA_ATTR_ANY, "");
        return 1;
    }

    // Instance:GetAttributeChangedSignal(nombre) — lo usan bastantes juegos para
    // reaccionar a sus atributos. Los atributos se guardan como metadatos y Godot
    // no avisa cuando cambian: avisa SetAttribute.
    if (strcmp(key, "GetAttributeChangedSignal") == 0) {
        lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)n->get_instance_id());
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            const uint64_t nid = (uint64_t)(uintptr_t)lua_touserdata(pL, lua_upvalueindex(1));
            Node* nd = gl_cast<Node>(ObjectDB::get_instance(nid));
            const char* attr = lua_isstring(pL, -1) ? lua_tostring(pL, -1) : "";
            if (!nd) { _gl_push_inert_signal(pL); return 1; }
            _gl_push_watch_signal(pL, nd, GL_VIGILA_ATTR, attr);
            return 1;
        }, "GetAttributeChangedSignal", 1);
        return 1;
    }

    // game:UpdateStructure() — actualizador incremental (1.15): rellena lo que
    // falte y migra nodos sin borrar ni pisar la config. Mismo efecto que la
    // opción "Actualizar estructura" del inspector; útil para scripts de build.
    if (strcmp(key, "UpdateStructure") == 0 && n->has_method("_gl_update_structure")) {
        lua_pushlightuserdata(L, (void*)n);
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            Node* nd = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
            if (nd && nd->has_method("_gl_update_structure")) nd->call("_gl_update_structure");
            return 0;
        }, "UpdateStructure", 1);
        return 1;
    }

    // game:SetNativeMenuEnabled(bool) — el menú Lua editable (Modules/Menu) llama
    // a esto en su Init para apartar el menú nativo del motor (1.15).
    if (strcmp(key, "SetNativeMenuEnabled") == 0) {
        lua_pushcfunction(L, [](lua_State* pL) -> int {
            gl_set_native_menu_enabled(lua_toboolean(pL, 2) != 0);
            return 0;
        }, "SetNativeMenuEnabled");
        return 1;
    }
    // game:Shutdown() — cierra el juego (botón "Salir" del menú).
    if (strcmp(key, "Shutdown") == 0) {
        lua_pushlightuserdata(L, (void*)n);
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            Node* nd = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
            if (nd && nd->is_inside_tree() && nd->get_tree()) nd->get_tree()->quit();
            return 0;
        }, "Shutdown", 1);
        return 1;
    }

    // ── Universal instance signals ────────────────────────────────
    //// ── Señales de instancia universales ─────────────────────────
    // ChildAdded / ChildRemoved — connect to Godot child_entered_tree / child_exiting_tree
    //// ChildAdded / ChildRemoved — conectan a Godot child_entered_tree / child_exiting_tree
    if (strcmp(key, "ChildAdded") == 0 || strcmp(key, "ChildRemoved") == 0) {
        _gl_push_relay_signal(L, n, strcmp(key, "ChildAdded") == 0 ? GL_REL_HIJO_ENTRA : GL_REL_HIJO_SALE);
        return 1;
    }

    // Destroying — salta justo ANTES de que la instancia deje de existir.
    // Godot no tiene esa señal, pero tree_exiting salta en ese mismo momento;
    // el relay solo la propaga si el nodo (o un ancestro) va camino de morir,
    // así que un simple cambio de Parent NO la dispara, igual que en Roblox.
    // Sin esto, cualquier librería que limpie su estado por instancia (los
    // "janitor" que usa medio Roblox) moría en la primera línea.
    if (strcmp(key, "Destroying") == 0) {
        _gl_push_relay_signal(L, n, GL_REL_DESTRUYE);
        return 1;
    }

    // GetPropertyChangedSignal(prop) → señal que SI salta. Antes era inerte: se
    // conectaba y no avisaba nunca, asi que un script que vigilara, por ejemplo,
    // el Text de una etiqueta se quedaba mirando para siempre. Ahora la escritura
    // de propiedades avisa (ver gl_avisar_cambio_prop).
    if (strcmp(key, "GetPropertyChangedSignal") == 0) {
        lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)n->get_instance_id());
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            const uint64_t nid = (uint64_t)(uintptr_t)lua_touserdata(pL, lua_upvalueindex(1));
            Node* nd = gl_cast<Node>(ObjectDB::get_instance(nid));
            // El nombre de la propiedad es el ultimo argumento (con o sin `self`).
            const char* prop = lua_isstring(pL, -1) ? lua_tostring(pL, -1) : "";
            if (!nd) { _gl_push_inert_signal(pL); return 1; }
            _gl_push_watch_signal(pL, nd, GL_VIGILA_PROP, prop);
            return 1;
        }, "GetPropertyChangedSignal", 1);
        return 1;
    }

    // ── DescendantAdded / DescendantRemoving ──────────────────────────
    // Antes eran señales INERTES: se conectaban y no saltaban nunca. Un juego
    // que vigila su carpeta de objetos con DescendantAdded (patron muy comun
    // para enganchar cosas segun aparecen) no se enteraba de nada.
    //
    // Godot no las tiene por nodo, pero el SceneTree avisa de todo nodo que
    // entra o sale; se escucha ahi y se filtra por ancestro (ver
    // _gl_rel_desc en luau_script.h).
    if (strcmp(key, "DescendantAdded") == 0 || strcmp(key, "DescendantRemoving") == 0) {
        _gl_push_relay_signal(L, n, strcmp(key, "DescendantAdded") == 0 ? GL_REL_DESC_ENTRA : GL_REL_DESC_SALE);
        return 1;
    }

    // AncestryChanged(instancia, padre) — al cambiar el Parent de la instancia
    // o de un ancestro (ver ScriptNodeBase::_gl_rel_ancestros).
    if (strcmp(key, "AncestryChanged") == 0) {
        _gl_push_relay_signal(L, n, GL_REL_ANCESTROS);
        return 1;
    }

    // ── Propiedades de Instance ───────────────────────────────────
    if (strcmp(key, "Name")      == 0) { lua_pushstring(L, gl_rbx_name(n).utf8().get_data()); return 1; }
    if (strcmp(key, "ClassName") == 0) { lua_pushstring(L, gl_roblox_classname(n)); return 1; }
    if (strcmp(key, "Parent")    == 0) { wrap_node(L, n->get_parent()); return 1; }
    // ReplicatedFirst: quita la pantalla de carga por defecto en el acto, como en
    // Roblox (ui/gl_carga_defecto.h); el juego pone la suya.
    if (strcmp(key, "RemoveDefaultLoadingScreen") == 0 && gl_cast<ReplicatedFirst>(n)) {
        lua_pushcfunction(L, [](lua_State*) -> int { gl_carga_defecto_quitar(); return 0; }, "RemoveDefaultLoadingScreen");
        return 1;
    }
    // Archivable: toda Instance lo tiene (true por defecto). No existia y leerlo
    // mataba el script; es el `char.Archivable = true` de antes de clonar.
    if (strcmp(key, "Archivable") == 0) {
        static const StringName k("_gl_no_archivable");
        lua_pushboolean(L, !n->has_meta(k));
        return 1;
    }

    return -1;
}

#endif // LUAU_API_INDICE_INSTANCIA_H

#ifndef LUAU_API_INDICE_JUEGO_H
#define LUAU_API_INDICE_JUEGO_H
#include "gl_cast.h"

// Lectura de propiedades desde Luau (__index de las instancias), ultima zona: cámara, animaciones, sonido, TweenService, ClickDetector,
// ProximityPrompt, Model, SpawnLocation, BillboardGui, Attachment, Motor6D,
// Tool, UserInputService, TextService, CollectionService, UIGradient, el
// puente generico y el respaldo final (que siempre responde).
//
// Va aparte de luau_api.h (pasaba de 2.000 lineas) y se incluye desde alli,
// justo antes de godot_object_index_real, que llama a las zonas en orden.
// Devuelve cuantos valores deja en la pila: esta zona es la ultima y
// siempre responde (o lanza el error de miembro que no existe).

// Las texturas del cielo de Roblox (Sky): aqui el cielo es procedural.
static inline bool gl_cara_del_cielo(const char* key) {
    if (strncmp(key, "Skybox", 6) == 0) {
        const char* c = key + 6;
        return strcmp(c, "Bk") == 0 || strcmp(c, "Dn") == 0 || strcmp(c, "Ft") == 0 ||
               strcmp(c, "Lf") == 0 || strcmp(c, "Rt") == 0 || strcmp(c, "Up") == 0;
    }
    return strcmp(key, "SunTextureId") == 0 || strcmp(key, "MoonTextureId") == 0;
}

// Connect de una señal de una clase puente que guarda sus propias conexiones de
// Luau (metodo _gl_conectar; los HandleAdornment: MouseButton1Down/Up,
// MouseEnter/Leave). La clase llama a la funcion cuando toca y Disconnect le
// llega por _gl_disconnect (ver _gl_push_connection).
inline int gl_evento_puente_connect(lua_State* L) {
    const uint64_t id = (uint64_t)(uintptr_t)lua_touserdata(L, lua_upvalueindex(1));
    const char* ev = lua_tostring(L, lua_upvalueindex(2));
    Node* n = gl_cast<Node>(ObjectDB::get_instance(ObjectID(id)));
    if (!n || !ev || !lua_isfunction(L, 2)) { lua_pushnil(L); return 1; }
    lua_pushvalue(L, 2);
    const int ref = gl_ref(L, -1);
    lua_pop(L, 1);
    lua_State* vm = gl_main_of(L);
    if (!vm) vm = L;
    static const StringName k_con("_gl_conectar");
    n->call(k_con, String::utf8(ev), ref, (int64_t)(intptr_t)vm);
    _gl_push_connection(L, n, ref);
    return 1;
}

inline int gl_index_juego(lua_State* L, Node* n, const char* key) {
    // ── CurrentCamera — workspace.CurrentCamera returns the active camera ─
    //// ── CurrentCamera — workspace.CurrentCamera devuelve la cámara activa ─
    // Looks for Camera3D or Camera2D in the workspace or in the local player
    //// Busca Camera3D o Camera2D en el workspace o en el jugador local
    if (strcmp(key, "CurrentCamera") == 0) {
        // En el Workspace manda la camara ACTIVA del viewport, que es la que
        // devuelve Roblox. Antes ganaba el hijo "CurrentCamera" (la plantilla del
        // editor, que no dibuja) y los scripts movian una camara que nadie veia.
        if (gl_cast<RobloxWorkspace>(n) || n->is_class("RobloxWorkspace2D")) {
            if (Viewport* vp = n->get_viewport()) {
                if (Camera3D* c3 = vp->get_camera_3d()) { wrap_node(L, c3); return 1; }
                if (Camera2D* c2 = vp->get_camera_2d()) { wrap_node(L, c2); return 1; }
            }
        }
        // Search in the current node (Workspace may have CurrentCamera as a child) / Buscar en el nodo actual (Workspace puede tener CurrentCamera como hijo)
        Node* cam = n->get_node_or_null("CurrentCamera");
        if (cam) { wrap_node(L, cam); return 1; }
        // Fallback: look in LocalPlayer / buscar en el LocalPlayer
        Node* lp = n->get_node_or_null("LocalPlayer");
        if (lp) {
            cam = lp->get_node_or_null("Camera3D");
            if (!cam) cam = lp->get_node_or_null("Camera2D");
            if (cam) { wrap_node(L, cam); return 1; }
        }
        // Ultimo recurso y el mas fiel a Roblox: la camara ACTIVA del viewport,
        // sea quien sea y donde este. Es lo que devuelve Roblox de verdad.
        if (Viewport* vp = n->get_viewport()) {
            if (Camera3D* c3 = vp->get_camera_3d()) { wrap_node(L, c3); return 1; }
            if (Camera2D* c2 = vp->get_camera_2d()) { wrap_node(L, c2); return 1; }
        }
        lua_pushnil(L); return 1;
    }

    // ── Camera (gl_puente_camara.h) ──
    if (Camera3D* cam3 = gl_cast<Camera3D>(n)) {
        const int r = gl_camara_index(L, cam3, key);
        if (r >= 0) return r;
    }

    // ── AnimationTrack (gl_puente_animacion.h) ─────────────────────────
    if (AnimationTrack* pista = gl_cast<AnimationTrack>(n)) {
        const int r = gl_animacion_index(L, pista, key);
        if (r >= 0) return r;
    }
    // ── AnimationObject ───────────────────────────────────────────────
    AnimationObject* aobj = gl_cast<AnimationObject>(n);
    if (aobj) {
        if (strcmp(key,"AnimationId") == 0) { lua_pushstring(L, aobj->get_animation_id().utf8().get_data()); return 1; }
    }

    // ── Sound y SoundService (gl_puente_sonido.h) ──
    if (RobloxSound* rsound = gl_cast<RobloxSound>(n)) {
        const int r = gl_sonido_index(L, rsound, key);
        if (r >= 0) return r;
    }
    if (gl_cast<SoundService>(n)) {
        const int r = gl_soundservice_index(L, key);
        if (r >= 0) return r;
    }

    // ── TweenService ─────────────────────────────────────────────────
    TweenService* tws = gl_cast<TweenService>(n);
    if (tws) {
        if (strcmp(key,"Create") == 0) {
            lua_pushlightuserdata(L, (void*)tws);
            lua_pushcclosure(L, [](lua_State* LL) -> int {
                TweenService* ts = (TweenService*)lua_touserdata(LL, lua_upvalueindex(1));
                if (!ts) { lua_pushnil(LL); return 1; }
                // args: (instance, tweenInfo_table, goals_table)
                GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(LL, 1);
                Node* target_node = (w && gow_node(w)) ? gow_node(w) : nullptr;

                // Parse TweenInfo table (arg 2)
                TweenInfo tinfo;
                if (lua_istable(LL, 2)) {
                    lua_getfield(LL, 2, "Time");       if (!lua_isnil(LL,-1)) tinfo.time          = (float)lua_tonumber(LL,-1); lua_pop(LL,1);
                    lua_getfield(LL, 2, "EasingStyle"); if (!lua_isnil(LL,-1)) tinfo.easing_style  = (int)lua_tonumber(LL,-1);  lua_pop(LL,1);
                    lua_getfield(LL, 2, "EasingDirection"); if(!lua_isnil(LL,-1)) tinfo.easing_dir = (int)lua_tonumber(LL,-1);  lua_pop(LL,1);
                    lua_getfield(LL, 2, "RepeatCount");if (!lua_isnil(LL,-1)) tinfo.repeat_count   = (int)lua_tonumber(LL,-1);  lua_pop(LL,1);
                    lua_getfield(LL, 2, "Reverses");   if (!lua_isnil(LL,-1)) tinfo.reverses       = lua_toboolean(LL,-1)!=0;   lua_pop(LL,1);
                    lua_getfield(LL, 2, "DelayTime");  if (!lua_isnil(LL,-1)) tinfo.delay_time     = (float)lua_tonumber(LL,-1);lua_pop(LL,1);
                }

                RobloxTween* tw = ts->create_tween(target_node ? target_node->get_parent() : nullptr);
                tw->info = tinfo;

                // Parse goals table (arg 3) — {PropertyName = targetValue}
                if (lua_istable(LL, 3) && target_node) {
                    lua_pushnil(LL);
                    while (lua_next(LL, 3) != 0) {
                        if (lua_isstring(LL, -2)) {
                            const char* prop = lua_tostring(LL, -2);
                            TweenTrack tr;
                            tr.target = target_node;
                            tr.property = String(prop);
                            tr.from_current = true;
                            // Read target value
                            int vt = lua_type(LL, -1);
                            if (vt == LUA_TNUMBER) {
                                tr.to_val = TweenValue((float)lua_tonumber(LL, -1));
                            } else if (vt == LUA_TTABLE || vt == LUA_TUSERDATA) {
                                // AQUI ESTABA EL BUG: solo se miraba LUA_TTABLE.
                                // Vector3.new() y Color3.new() del stdlib
                                // devuelven USERDATA, no una tabla, asi que el
                                // destino no se leia NUNCA y se quedaba a cero.
                                // Medido: un tween de Position o de Size sobre
                                // una pieza no la movia ni un stud — no era que
                                // fuera "poco fluido", es que estaba muerto.
                                //
                                // Se leen los CAMPOS con lua_getfield en vez de
                                // comparar metatablas: asi vale igual para una
                                // tabla plana que para el userdata (que resuelve
                                // X/Y/Z por su __index). Menos cosas que puedan
                                // no cuadrar.
                                lua_getfield(LL, -1, "R");
                                bool es_color = !lua_isnil(LL, -1);
                                lua_pop(LL, 1);
                                if (!es_color) {
                                    lua_getfield(LL, -1, "r");
                                    es_color = !lua_isnil(LL, -1);
                                    lua_pop(LL, 1);
                                }
                                auto num = [&](const char* a, const char* b) -> float {
                                    lua_getfield(LL, -1, a);
                                    if (lua_isnil(LL, -1)) { lua_pop(LL, 1); lua_getfield(LL, -1, b); }
                                    float r = lua_isnumber(LL, -1) ? (float)lua_tonumber(LL, -1) : 0.0f;
                                    lua_pop(LL, 1);
                                    return r;
                                };
                                if (es_color) {
                                    tr.to_val = TweenValue(Color(num("R","r"), num("G","g"), num("B","b")));
                                } else {
                                    const float x = num("X","x");
                                    const float y = num("Y","y");
                                    lua_getfield(LL, -1, "Z");
                                    const bool hay_z = !lua_isnil(LL, -1);
                                    lua_pop(LL, 1);
                                    if (hay_z) tr.to_val = TweenValue(Vector3(x, y, num("Z","z")));
                                    else       tr.to_val = TweenValue(Vector2(x, y));
                                }
                            }
                            tw->tracks.push_back(tr);
                        }
                        lua_pop(LL, 1);
                    }
                }

                // Return Tween object
                GodotObjectWrapper* tw_wrap = (GodotObjectWrapper*)lua_newuserdata(LL, sizeof(GodotObjectWrapper));
                gow_set(tw_wrap, tw);
                luaL_getmetatable(LL, "GodotObject");
                lua_setmetatable(LL, -2);
                return 1;
            }, "Create", 1); return 1;
        }
    }

    // ── RobloxTween ───────────────────────────────────────────────────
    RobloxTween* rtween = gl_cast<RobloxTween>(n);
    if (rtween) {
        if (strcmp(key,"Play")   == 0) {
            lua_pushlightuserdata(L, (void*)rtween);
            lua_pushcclosure(L, [](lua_State* LL)->int{ RobloxTween* t=(RobloxTween*)lua_touserdata(LL,lua_upvalueindex(1)); if(t)t->play(); return 0; },"Play",1); return 1;
        }
        if (strcmp(key,"Pause")  == 0) {
            lua_pushlightuserdata(L, (void*)rtween);
            lua_pushcclosure(L, [](lua_State* LL)->int{ RobloxTween* t=(RobloxTween*)lua_touserdata(LL,lua_upvalueindex(1)); if(t)t->pause(); return 0; },"Pause",1); return 1;
        }
        if (strcmp(key,"Cancel") == 0) {
            lua_pushlightuserdata(L, (void*)rtween);
            lua_pushcclosure(L, [](lua_State* LL)->int{ RobloxTween* t=(RobloxTween*)lua_touserdata(LL,lua_upvalueindex(1)); if(t)t->cancel(); return 0; },"Cancel",1); return 1;
        }
        if (strcmp(key,"Completed") == 0) {
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)rtween);
            lua_pushcclosure(L, [](lua_State* LL)->int{
                RobloxTween* t=(RobloxTween*)lua_touserdata(LL,lua_upvalueindex(1));
                if (!t || !lua_isfunction(LL,2)) { lua_pushnil(LL); return 1; }
                lua_pushvalue(LL,2); int ref=gl_ref(LL,-1); lua_pop(LL,1);
                t->add_completed_cb(gl_main_of(LL), ref);
                _gl_push_connection(LL, t, ref); return 1;
            }, "Connect", 1);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L); return 1;
        }
        if (strcmp(key,"PlaybackState") == 0) {
            if (rtween->finished)     lua_pushstring(L, "Completed");
            else if (!rtween->playing) lua_pushstring(L, "Paused");
            else                       lua_pushstring(L, "Playing");
            return 1;
        }
    }

    // ── ClickDetector ─────────────────────────────────────────────────
    ClickDetector* cd = gl_cast<ClickDetector>(n);
    if (cd) {
        if (strcmp(key,"MaxActivationDistance") == 0) { lua_pushnumber(L, cd->get_max_distance()); return 1; }
        auto make_click_signal = [&](const char* ev_name, std::vector<ClickDetector::LuaCallback>& cbs) -> int {
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)cd);
            lua_pushlightuserdata(L, (void*)&cbs);
            lua_pushcclosure(L, [](lua_State* LL)->int{
                ClickDetector* d=(ClickDetector*)lua_touserdata(LL,lua_upvalueindex(1));
                auto* c=(std::vector<ClickDetector::LuaCallback>*)lua_touserdata(LL,lua_upvalueindex(2));
                if (!d || !c || !lua_isfunction(LL,2)) { lua_pushnil(LL); return 1; }
                lua_pushvalue(LL,2); int ref=gl_ref(LL,-1); lua_pop(LL,1);
                c->push_back({gl_main_of(LL), ref, true});
                _gl_push_connection(LL, d, ref); return 1;
            }, "Connect", 2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L); return 1;
        };
        if (strcmp(key,"MouseClick")      == 0) return make_click_signal("MouseClick",      cd->mouse_click_cbs);
        if (strcmp(key,"MouseHoverEnter") == 0) return make_click_signal("MouseHoverEnter",  cd->hover_enter_cbs);
        if (strcmp(key,"MouseHoverLeave") == 0) return make_click_signal("MouseHoverLeave",  cd->hover_leave_cbs);
    }

    // ── ProximityPrompt ───────────────────────────────────────────────
    ProximityPrompt* pp = gl_cast<ProximityPrompt>(n);
    if (pp) {
        if (strcmp(key,"ActionText")            == 0) { lua_pushstring(L, pp->get_action_text().utf8().get_data()); return 1; }
        // KeyboardKeyCode se DEVUELVE como numero: los juegos lo comparan contra
        // Enum.KeyCode.F, y un texto nunca era igual.
        if (strcmp(key,"KeyboardKeyCode")       == 0) { lua_pushnumber(L, pp->get_key_code_num()); return 1; }
        if (strcmp(key,"MaxActivationDistance") == 0) { lua_pushnumber(L, pp->get_max_distance()); return 1; }
        if (strcmp(key,"HoldDuration")          == 0) { lua_pushnumber(L, pp->get_hold_duration()); return 1; }
        if (strcmp(key,"Enabled")               == 0) { lua_pushboolean(L, pp->get_enabled()); return 1; }
        // FALTABAN TODAS ESTAS. El ProximityPromptScript de Roblox lee
        // ClickablePrompt al crear el prompt, asi que moria en su linea 331 y
        // NINGUN prompt del place llegaba a existir. Se ve en el log como
        // "ClickablePrompt is not a valid member of ProximityPrompt".
        if (strcmp(key,"ClickablePrompt")       == 0) { lua_pushboolean(L, pp->get_clickable_prompt()); return 1; }
        if (strcmp(key,"ObjectText")            == 0) { lua_pushstring(L, pp->get_object_text().utf8().get_data()); return 1; }
        if (strcmp(key,"RequiresLineOfSight")   == 0) { lua_pushboolean(L, pp->get_requires_line_of_sight()); return 1; }
        if (strcmp(key,"GamepadKeyCode")        == 0) { lua_pushnumber(L, pp->get_gamepad_key_num()); return 1; }
        if (strcmp(key,"Exclusivity")           == 0) { lua_pushnumber(L, pp->get_exclusivity()); return 1; }
        if (strcmp(key,"AutoLocalize")          == 0) { lua_pushboolean(L, pp->get_auto_localize()); return 1; }
        // Sin tablas de traduccion: nil, como un prompt al que nadie le puso una.
        // El ProximityPromptScript oficial lo copia a sus etiquetas.
        if (strcmp(key,"RootLocalizationTable") == 0) { lua_pushnil(L); return 1; }
        if (strcmp(key,"Style")                 == 0) { lua_pushnumber(L, pp->get_style()); return 1; }
        // Roblox lo entrega como Vector2; aqui solo el alto tiene efecto.
        if (strcmp(key,"UIOffset")              == 0) { push_vector2(L, 0.0f, pp->get_ui_offset_y()); return 1; }
        if (strcmp(key,"RootLocalPlayer")       == 0) { lua_pushnil(L); return 1; }
        auto make_pp_signal = [&](std::vector<ProximityPrompt::LuaCallback>& cbs) -> int {
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)pp);
            lua_pushlightuserdata(L, (void*)&cbs);
            lua_pushcclosure(L, [](lua_State* LL)->int{
                ProximityPrompt* d=(ProximityPrompt*)lua_touserdata(LL,lua_upvalueindex(1));
                auto* c=(std::vector<ProximityPrompt::LuaCallback>*)lua_touserdata(LL,lua_upvalueindex(2));
                if (!c || !lua_isfunction(LL,2)) { lua_pushnil(LL); return 1; }
                lua_pushvalue(LL,2); int ref=gl_ref(LL,-1); lua_pop(LL,1);
                c->push_back({gl_main_of(LL), ref, true});
                _gl_push_connection(LL, d, ref); return 1;
            }, "Connect", 2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L); return 1;
        };
        if (strcmp(key,"Style") == 0) { lua_pushnumber(L, pp->get_style()); return 1; }
        // PromptShown/PromptHidden: los usan los sistemas de dialogo para saber
        // cuando el cartel de "pulsa E" aparece o se va. Eran señales INERTES
        // (aceptaban la conexion y no disparaban jamas), asi que cualquier
        // interfaz propia de interaccion se quedaba muda.
        if (strcmp(key,"PromptShown")  == 0) return make_pp_signal(pp->shown_cbs);
        if (strcmp(key,"PromptHidden") == 0) return make_pp_signal(pp->hidden_cbs);
        if (strcmp(key,"PromptButtonHoldBegan") == 0 ||
            strcmp(key,"PromptButtonHoldEnded") == 0 ||
            strcmp(key,"TriggerEnded")  == 0) {
            _gl_push_inert_signal(L);
            return 1;
        }
        if (strcmp(key,"Triggered")  == 0) return make_pp_signal(pp->triggered_cbs);
        if (strcmp(key,"HoldBegan")  == 0) return make_pp_signal(pp->hold_began_cbs);
        if (strcmp(key,"HoldEnded")  == 0) return make_pp_signal(pp->hold_ended_cbs);
    }

    // ── Model (RBXModel importado): PrimaryPart / pivotes / escala ────
    // Los métodos viven bound por ClassDB en rbx_instances.h; aquí solo se
    // puentean (call dinámico, sin acoplar headers).
    if (n->is_class("RBXModel")) {
        if (strcmp(key, "PrimaryPart") == 0) {
            NodePath pp = n->get("PrimaryPart");
            wrap_node(L, pp.is_empty() ? nullptr : n->get_node_or_null(pp));
            return 1;
        }
        if (strcmp(key, "WorldPivot") == 0) {
            _gl_push_cframe_full(L, (Transform3D)n->call("GetPivot"));
            return 1;
        }
        if (strcmp(key, "GetPivot") == 0) {
            lua_pushlightuserdata(L, (void*)n);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Node* m = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!m) { lua_pushnil(pL); return 1; }
                _gl_push_cframe_full(pL, (Transform3D)m->call("GetPivot"));
                return 1;
            }, "GetPivot", 1);
            return 1;
        }
        if (strcmp(key, "PivotTo") == 0) {
            lua_pushlightuserdata(L, (void*)n);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Node* m = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
                if (m && lua_istable(pL, 2)) m->call("PivotTo", _gl_read_cframe_tbl(pL, 2));
                return 0;
            }, "PivotTo", 1);
            return 1;
        }
        if (strcmp(key, "MoveTo") == 0) {
            lua_pushlightuserdata(L, (void*)n);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Node* m = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
                LuauVector3* v = (LuauVector3*)lua_touserdata(pL, 2);
                if (m && v) m->call("MoveTo", Vector3(v->x, v->y, v->z));
                return 0;
            }, "MoveTo", 1);
            return 1;
        }
        // Los dos nombres VIEJOS de PivotTo/GetPivot. Roblox los marco obsoletos
        // pero nunca los quito, y siguen en juegos publicados: sin ellos el
        // sistema de inactividad no podia teletransportar y las alfombras
        // voladoras se quedaban sin centro.
        if (strcmp(key, "SetPrimaryPartCFrame") == 0) {
            lua_pushlightuserdata(L, (void*)n);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Node* m = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
                if (m && lua_istable(pL, 2)) m->call("PivotTo", _gl_read_cframe_tbl(pL, 2));
                return 0;
            }, "SetPrimaryPartCFrame", 1);
            return 1;
        }
        if (strcmp(key, "GetModelCFrame") == 0) {
            lua_pushlightuserdata(L, (void*)n);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Node* m = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!m) { lua_pushnil(pL); return 1; }
                _gl_push_cframe_full(pL, (Transform3D)m->call("GetPivot"));
                return 1;
            }, "GetModelCFrame", 1);
            return 1;
        }
        if (strcmp(key, "GetExtentsSize") == 0) {
            lua_pushlightuserdata(L, (void*)n);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Node* m = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
                Vector3 v = m ? (Vector3)m->call("GetExtentsSize") : Vector3();
                push_vector3(pL, v.x, v.y, v.z);
                return 1;
            }, "GetExtentsSize", 1);
            return 1;
        }
        if (strcmp(key, "GetBoundingBox") == 0) {
            lua_pushlightuserdata(L, (void*)n);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Node* m = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
                if (!m) { lua_pushnil(pL); return 1; }
                AABB b = m->call("GetBoundingBox");
                Transform3D t; t.origin = b.position + b.size * 0.5f;
                _gl_push_cframe_full(pL, t);
                push_vector3(pL, b.size.x, b.size.y, b.size.z);
                return 2;
            }, "GetBoundingBox", 1);
            return 1;
        }
        if (strcmp(key, "GetScale") == 0) {
            lua_pushlightuserdata(L, (void*)n);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Node* m = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushnumber(pL, m ? (double)(float)m->get("ScaleFactor") : 1.0);
                return 1;
            }, "GetScale", 1);
            return 1;
        }
        if (strcmp(key, "ScaleTo") == 0) {
            lua_pushlightuserdata(L, (void*)n);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                Node* m = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
                float s = (float)gl_num(pL, 2);
                if (m && s > 0.0001f) {
                    float prev = (float)m->get("ScaleFactor");
                    if (prev <= 0.0001f) prev = 1.0f;
                    const float f = s / prev;
                    // ── ScaleTo 1:1 con Roblox ────────────────────────────
                    // ANTES: set_scale en el nodo del Model. Nuestros Models
                    // son nodos identidad y las piezas llevan su posicion de
                    // mundo como transform local, asi que escalar el nodo
                    // escalaba tambien las POSICIONES: un NPC clonado con
                    // ScaleTo(15) aterrizaba a 15 veces su sitio (medido:
                    // Z=11.523 → Z=173.970) y toda escritura posterior de
                    // CFrame quedaba envenenada por la escala del padre — por
                    // eso "los NPC no caminan". Roblox escala TAMANOS y
                    // offsets alrededor del pivote del modelo, sin tocar
                    // ningun transform de contenedor. Eso hacemos.
                    if (Math::abs(f - 1.0f) > 0.0001f) {
                        std::vector<Node*> pila; pila.push_back(m);
                        std::vector<RobloxPart*> partes;
                        while (!pila.empty()) {
                            Node* cur = pila.back(); pila.pop_back();
                            for (int i = 0; i < cur->get_child_count(); i++) {
                                Node* h = cur->get_child(i);
                                if (RobloxPart* rp = gl_cast<RobloxPart>(h)) partes.push_back(rp);
                                pila.push_back(h);
                            }
                        }
                        if (!partes.empty()) {
                            // Pivote: PrimaryPart / GetPivot si se puede; si el
                            // clon aun no esta en el arbol, la primera pieza.
                            Vector3 piv;
                            if (m->has_method("GetPivot") && gl_cast<Node3D>(m) &&
                                gl_cast<Node3D>(m)->is_inside_tree()) {
                                piv = ((Transform3D)m->call("GetPivot")).origin;
                            } else {
                                piv = partes[0]->is_inside_tree() ? partes[0]->get_global_position()
                                                                  : partes[0]->get_position();
                            }
                            for (RobloxPart* rp : partes) {
                                Vector3 sz = rp->get("Size");
                                rp->set("Size", sz * f);
                                if (rp->is_inside_tree()) {
                                    Transform3D g = rp->get_global_transform();
                                    g.origin = piv + (g.origin - piv) * f;
                                    rp->set_global_transform(g);
                                } else {
                                    rp->set_position(piv + (rp->get_position() - piv) * f);
                                }
                            }
                        }
                        // La altura de cadera acompana, como en Roblox.
                        if (Node* hum = m->get_node_or_null(NodePath("Humanoid"))) {
                            double hh = (double)hum->get("HipHeight");
                            hum->set("HipHeight", hh * f);
                        }
                    }
                    m->set("ScaleFactor", s);
                }
                return 0;
            }, "ScaleTo", 1);
            return 1;
        }
    }

    // ── SpawnLocation ─────────────────────────────────────────────────
    SpawnLocation* spwn = gl_cast<SpawnLocation>(n);
    if (spwn) {
        if (strcmp(key,"Enabled")  == 0) { lua_pushboolean(L, spwn->get_enabled()); return 1; }
        if (strcmp(key,"Duration") == 0) { lua_pushnumber(L, spwn->get_duration()); return 1; }
        if (strcmp(key,"Neutral")  == 0) { lua_pushboolean(L, spwn->get_neutral()); return 1; }
        if (strcmp(key,"TeamColor") == 0) { push_brickcolor(L, spwn->get_team_color()); return 1; }
        if (strcmp(key,"AllowTeamChangeOnTouch") == 0) { lua_pushboolean(L, spwn->get_allow_team_change()); return 1; }
    }

    // ── Team (1.15): TeamColor es un BrickColor, no un número ─────────
    if (n->is_class("Team")) {
        if (strcmp(key,"TeamColor") == 0) { push_brickcolor(L, String(n->get("TeamColor"))); return 1; }
    }

    // ── BillboardGui ──────────────────────────────────────────────────
    BillboardGui* bbgui = gl_cast<BillboardGui>(n);
    if (bbgui) {
        if (strcmp(key,"Enabled")     == 0) { lua_pushboolean(L, bbgui->get_enabled()); return 1; }
        if (strcmp(key,"Size")        == 0) {
            // UDim2 de verdad: X = {Scale, Offset}, Y = {Scale, Offset}. Antes
            // devolvia un solo numero por eje (la suma ya convertida a studs),
            // asi que `gui.Size.X.Offset` daba nil y cualquier script que
            // ajustara el cartel moria.
            gl_push_udim2(L, bbgui->get_size_x(), bbgui->get_px_x(),
                             bbgui->get_size_y(), bbgui->get_px_y());
            return 1;
        }
        if (strcmp(key,"MaxDistance") == 0) { lua_pushnumber(L, bbgui->get_max_distance()); return 1; }
        if (strcmp(key,"AlwaysOnTop") == 0) { lua_pushboolean(L, bbgui->get_always_on_top()); return 1; }
        // El puente va por LISTA EXPLICITA y aqui solo estaban esas cuatro: el
        // resto se leia como nil aunque la clase las tuviera. Misma trampa que
        // en kWsMenuApi — bindear en C++ no basta, hay que anotarlo aqui.
        if (strcmp(key,"DistanceLowerLimit") == 0) { lua_pushnumber(L, bbgui->get_min_distance()); return 1; }
        if (strcmp(key,"StudsOffset") == 0) {
            const Vector3 so = bbgui->get_studs_offset();
            push_vector3(L, so.x, so.y, so.z);
            return 1;
        }
        if (strcmp(key,"Adornee") == 0) {
            wrap_node(L, gl_cast<Node>(bbgui->get_adornee()));
            return 1;
        }
    }

    // ── Attachment: las propiedades de MUNDO ──────────────────────────
    // WorldPosition / WorldCFrame / WorldAxis son lectura constante en Roblox
    // (VFX, punteria, soldar cosas donde toca). Attachment va por el puente
    // generico "_gl_bridge", que devuelve nil para lo que no conoce, asi que
    // salian nil y cualquier cuenta con ellas daba basura sin avisar.
    if (n->is_class("Attachment")) {
        Node3D* at = gl_cast<Node3D>(n);
        if (at) {
            if (strcmp(key, "WorldPosition") == 0) {
                const Vector3 p = at->is_inside_tree() ? at->get_global_position() : at->get_position();
                push_vector3(L, p.x, p.y, p.z);
                return 1;
            }
            if (strcmp(key, "WorldCFrame") == 0) {
                _gl_push_cframe_full(L, at->is_inside_tree() ? at->get_global_transform()
                                                             : at->get_transform());
                return 1;
            }
            if (strcmp(key, "WorldAxis") == 0) {
                const Basis b = at->is_inside_tree() ? at->get_global_transform().basis
                                                     : at->get_transform().basis;
                const Vector3 x = b.get_column(0);
                push_vector3(L, x.x, x.y, x.z);
                return 1;
            }
        }
    }

    // ── Motor6D ───────────────────────────────────────────────────────
    Motor6D* m6d = gl_cast<Motor6D>(n);
    if (m6d) {
        // C0/C1 SE ESCRIBIAN pero al leerlos devolvian nil, asi que el idioma
        // normal de animar un rig — `m.C0 = m.C0 * CFrame.Angles(...)` — moria
        // en el primer paso. Ahora se devuelven como CFrame de verdad.
        if (strcmp(key,"C0") == 0) { _gl_push_cframe_full(L, m6d->get_c0()); return 1; }
        if (strcmp(key,"C1") == 0) { _gl_push_cframe_full(L, m6d->get_c1()); return 1; }
        // Transform: lo que pone el animador (o un script) entre C0 y C1.
        if (strcmp(key,"Transform") == 0) { _gl_push_cframe_full(L, m6d->get_transform()); return 1; }
        if (strcmp(key,"CurrentAngle") == 0) { lua_pushnumber(L, m6d->get_current_angle()); return 1; }
        if (strcmp(key,"Active") == 0) { lua_pushboolean(L, m6d->gl_activa() ? 1 : 0); return 1; }
        if (strcmp(key,"DesiredAngle") == 0) { lua_pushnumber(L, m6d->get_desired_angle()); return 1; }
        if (strcmp(key,"MaxVelocity")  == 0) { lua_pushnumber(L, m6d->get_max_velocity());  return 1; }
        if (strcmp(key,"Enabled")      == 0) { lua_pushboolean(L, m6d->get_enabled());       return 1; }
        if (strcmp(key,"Part0")        == 0) {
            Node* p = m6d->get_part0(); if (p) wrap_node(L, p); else lua_pushnil(L); return 1;
        }
        if (strcmp(key,"Part1")        == 0) {
            Node* p = m6d->get_part1(); if (p) wrap_node(L, p); else lua_pushnil(L); return 1;
        }
    }

    // ── RobloxTool ────────────────────────────────────────────────────
    RobloxTool* rtool = gl_cast<RobloxTool>(n);
    if (rtool) {
        if (strcmp(key,"ToolTip")      == 0) { lua_pushstring(L, rtool->get_tool_tip().utf8().get_data()); return 1; }
        if (strcmp(key,"CanBeDropped") == 0) { lua_pushboolean(L, rtool->get_can_be_dropped()); return 1; }
        auto make_tool_sig = [&](std::vector<RobloxTool::LuaCallback>& cbs) -> int {
            lua_newtable(L);
            lua_pushlightuserdata(L,(void*)rtool);
            lua_pushlightuserdata(L,(void*)&cbs);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                RobloxTool* d=(RobloxTool*)lua_touserdata(LL,lua_upvalueindex(1));
                auto* c=(std::vector<RobloxTool::LuaCallback>*)lua_touserdata(LL,lua_upvalueindex(2));
                if (!c || !lua_isfunction(LL,2)){lua_pushnil(LL);return 1;}
                lua_pushvalue(LL,2); int ref=gl_ref(LL,-1); lua_pop(LL,1);
                c->push_back({gl_main_of(LL),ref,true});
                _gl_push_connection(LL, d, ref); return 1;
            },"Connect",2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        };
        if (strcmp(key,"Equipped")    == 0) return make_tool_sig(rtool->equipped_cbs);
        if (strcmp(key,"Unequipped")  == 0) return make_tool_sig(rtool->unequipped_cbs);
        if (strcmp(key,"Activated")   == 0) return make_tool_sig(rtool->activated_cbs);
        if (strcmp(key,"Deactivated") == 0) return make_tool_sig(rtool->deactivated_cbs);
        if (strcmp(key,"Activate")    == 0) {
            lua_pushlightuserdata(L,(void*)rtool);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                RobloxTool* t=(RobloxTool*)lua_touserdata(LL,lua_upvalueindex(1));
                if(t) t->activate(); return 0;
            },"Activate",1); return 1;
        }
    }

    // ── UserInputService ──────────────────────────────────────────────
    UserInputService* uis = gl_cast<UserInputService>(n);
    if (uis) {
        if (strcmp(key,"MouseEnabled")    == 0) { lua_pushboolean(L, uis->get_mouse_enabled()?1:0); return 1; }
        if (strcmp(key,"KeyboardEnabled") == 0) { lua_pushboolean(L, uis->get_keyboard_enabled()?1:0); return 1; }
        if (strcmp(key,"TouchEnabled")    == 0) { lua_pushboolean(L, uis->get_touch_enabled()?1:0); return 1; }
        if (strcmp(key,"GamepadEnabled")  == 0) { lua_pushboolean(L, uis->get_gamepad_connected()?1:0); return 1; }
        if (strcmp(key,"VREnabled")       == 0) { lua_pushboolean(L, 0); return 1; }
        if (strcmp(key,"PreferredInput")  == 0) { uis_push_enum(L, "PreferredInput", uis->gl_preferido()); return 1; }
        // Aqui no hay teclado en pantalla del sistema.
        if (strcmp(key,"OnScreenKeyboardVisible")  == 0) { lua_pushboolean(L, 0); return 1; }
        if (strcmp(key,"OnScreenKeyboardPosition") == 0 || strcmp(key,"OnScreenKeyboardSize") == 0) {
            push_vector2(L, 0, 0);
            return 1;
        }
        // La imagen del boton del mando: no hay las de Roblox, cadena vacia (el
        // juego pone su propio texto o nada, como cuando la imagen no carga).
        if (strcmp(key,"GetImageForKeyCode") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int { lua_pushstring(pL, ""); return 1; }, "GetImageForKeyCode");
            return 1;
        }
        if (strcmp(key,"MouseBehavior")   == 0) { uis_push_enum(L, "MouseBehavior", uis->get_mouse_behavior()); return 1; }
        if (strcmp(key,"MouseIconEnabled")== 0) { lua_pushboolean(L, uis->get_mouse_icon_enabled()?1:0); return 1; }
        if (strcmp(key,"IsKeyDown") == 0 || strcmp(key,"IsKeyboardKeyDown") == 0) {
            lua_pushlightuserdata(L,(void*)uis);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                UserInputService* u=(UserInputService*)lua_touserdata(LL,lua_upvalueindex(1));
                if (!u){ lua_pushboolean(LL,0); return 1; }
                int kc = 0;
                if (lua_isnumber(LL,2) || lua_istable(LL,2)) {
                    // Numero o EnumItem (Enum.KeyCode.W): con el EnumItem, que es
                    // lo que pasa todo script de Roblox, daba false siempre.
                    kc = (int)gl_num_or(LL,2,0);
                } else if (lua_isstring(LL,2)) {
                    // Acepta tambien el nombre de la tecla ("W", "LeftShift"…)
                    const char* s = lua_tostring(LL,2);
                    if (strlen(s) == 1) { char c=s[0]; if(c>='a'&&c<='z') c-=32; kc=(int)c; }
                    else if (!strcmp(s,"LeftShift")||!strcmp(s,"RightShift"))     kc=304;
                    else if (!strcmp(s,"Space"))                                  kc=32;
                    else if (!strcmp(s,"LeftControl")||!strcmp(s,"RightControl")) kc=306;
                    else if (!strcmp(s,"LeftAlt")||!strcmp(s,"RightAlt"))         kc=308;
                    else if (!strcmp(s,"Return")||!strcmp(s,"Enter"))            kc=13;
                    else if (!strcmp(s,"Tab"))    kc=9;
                    else if (!strcmp(s,"Escape")) kc=27;
                    else if (!strcmp(s,"Up"))     kc=273;
                    else if (!strcmp(s,"Down"))   kc=274;
                    else if (!strcmp(s,"Left"))   kc=276;
                    else if (!strcmp(s,"Right"))  kc=275;
                }
                lua_pushboolean(LL, (kc!=0 && u->is_key_down(kc))?1:0); return 1;
            },"IsKeyDown",1); return 1;
        }
        if (strcmp(key,"GetMouseDelta") == 0) {
            lua_pushlightuserdata(L,(void*)uis);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                UserInputService* u=(UserInputService*)lua_touserdata(LL,lua_upvalueindex(1));
                Vector2 d = u ? u->get_mouse_delta_v() : Vector2();
                push_vector2(LL, d.x, d.y); return 1;   // Vector2, como Roblox
            },"GetMouseDelta",1); return 1;
        }
        if (strcmp(key,"IsGamepadButtonDown") == 0) {
            lua_pushlightuserdata(L,(void*)uis);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                UserInputService* u=(UserInputService*)lua_touserdata(LL,lua_upvalueindex(1));
                int btn = (int)gl_num_or(LL,3,0);   // numero o Enum.KeyCode.ButtonA
                lua_pushboolean(LL, (u && u->is_gamepad_button_down(0, btn))?1:0); return 1;
            },"IsGamepadButtonDown",1); return 1;
        }
        if (strcmp(key,"GetLastInputType") == 0) {
            lua_pushlightuserdata(L,(void*)uis);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                UserInputService* u=(UserInputService*)lua_touserdata(LL,lua_upvalueindex(1));
                // EnumItem: el juego hace inputType.Name (un numero no tiene).
                uis_push_enum(LL, "UserInputType", u ? u->get_last_input_type() : 0); return 1;
            },"GetLastInputType",1); return 1;
        }
        if (strcmp(key,"IsMouseButtonDown") == 0) {
            lua_pushlightuserdata(L,(void*)uis);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                UserInputService* u=(UserInputService*)lua_touserdata(LL,lua_upvalueindex(1));
                // Enum.UserInputType.MouseButton1..3 valen 1..3 aqui, que son
                // justo los botones izquierdo, derecho y central de Godot.
                const int btn = (int)gl_num_or(LL,2,0);
                if (!u || btn < 1 || btn > 3){ lua_pushboolean(LL,0); return 1; }
                lua_pushboolean(LL, u->is_mouse_button_down(btn)?1:0); return 1;
            },"IsMouseButtonDown",1); return 1;
        }
        if (strcmp(key,"GetMouseLocation") == 0) {
            lua_pushlightuserdata(L,(void*)uis);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                UserInputService* u=(UserInputService*)lua_touserdata(LL,lua_upvalueindex(1));
                if (!u){ lua_pushnil(LL); return 1; }
                Vector2 pos = u->get_mouse_location_v();
                // Vector2 de verdad: la tabla pelada no restaba ni tenia Magnitude.
                push_vector2(LL, pos.x, pos.y);
                return 1;
            },"GetMouseLocation",1); return 1;
        }
        // Signals
        auto make_uis_sig = [&](std::vector<UserInputService::LuaCallback>& cbs) -> int {
            lua_newtable(L);
            lua_pushlightuserdata(L,(void*)uis);
            lua_pushlightuserdata(L,(void*)&cbs);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                UserInputService* d=(UserInputService*)lua_touserdata(LL,lua_upvalueindex(1));
                auto* c=(std::vector<UserInputService::LuaCallback>*)lua_touserdata(LL,lua_upvalueindex(2));
                if(!c||!lua_isfunction(LL,2)){lua_pushnil(LL);return 1;}
                lua_pushvalue(LL,2); int ref=gl_ref(LL,-1); lua_pop(LL,1);
                c->push_back({gl_main_of(LL),ref,true}); _gl_push_connection(LL, d, ref); return 1;
            },"Connect",2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        };
        if (strcmp(key,"InputBegan")   == 0) return make_uis_sig(uis->input_began_cbs);
        if (strcmp(key,"InputEnded")   == 0) return make_uis_sig(uis->input_ended_cbs);
        if (strcmp(key,"InputChanged") == 0) return make_uis_sig(uis->input_changed_cbs);
        if (strcmp(key,"WindowFocused") == 0)        return make_uis_sig(uis->window_focused_cbs);
        if (strcmp(key,"WindowFocusReleased") == 0)  return make_uis_sig(uis->window_focus_released_cbs);
        if (strcmp(key,"JumpRequest") == 0)          return make_uis_sig(uis->jump_request_cbs);
        if (strcmp(key,"LastInputTypeChanged") == 0) return make_uis_sig(uis->last_input_type_changed_cbs);
        if (strcmp(key,"GamepadConnected") == 0)     return make_uis_sig(uis->gamepad_connected_cbs);
        if (strcmp(key,"PointerAction") == 0)        return make_uis_sig(uis->pointer_action_cbs);
        if (strcmp(key,"GamepadDisconnected") == 0)  return make_uis_sig(uis->gamepad_disconnected_cbs);
        if (strcmp(key,"TextBoxFocused") == 0 || strcmp(key,"TextBoxFocusReleased") == 0) {
            // Los TextBox avisan por gl_aviso_foco_texto (se compilan antes que el UIS).
            gl_aviso_foco_texto() = [](Node* tb, bool dentro) {
                UserInputService* u = gl_cast<UserInputService>(
                    ObjectDB::get_instance(UserInputService::gl_uis_id()));
                if (!u || !tb) return;
                const std::vector<UserInputService::LuaCallback> snap =
                    dentro ? u->textbox_focused_cbs : u->textbox_focus_released_cbs;
                for (const auto& cb : snap) {
                    if (!cb.active || !gl_state_alive(cb.main_L)) continue;
                    lua_State* th = lua_newthread(cb.main_L);
                    lua_rawgeti(cb.main_L, LUA_REGISTRYINDEX, cb.ref);
                    if (lua_isfunction(cb.main_L, -1)) {
                        lua_xmove(cb.main_L, th, 1);
                        wrap_node(th, tb);
                        gl_resume_handler(th, 1);
                    } else lua_pop(cb.main_L, 1);
                    lua_pop(cb.main_L, 1);
                }
            };
            return make_uis_sig(strcmp(key,"TextBoxFocused") == 0 ? uis->textbox_focused_cbs
                                                                   : uis->textbox_focus_released_cbs);
        }
        // Tactil: CADA señal con SU lista. Antes todas se reencaminaban a las de
        // InputBegan/Ended/Changed "porque un toque es una entrada mas", y el
        // efecto era que conectarse a TouchTap metia el callback en la lista de
        // InputBegan: cualquier CLIC DE RATON lo disparaba y el juego se creia
        // en un movil. Ahora solo las alimentan los toques de verdad, y con la
        // firma de Roblox (TouchTap recibe {Vector2}, no un InputObject).
        if (strcmp(key,"TouchStarted")   == 0) return make_uis_sig(uis->touch_started_cbs);
        if (strcmp(key,"TouchEnded")     == 0) return make_uis_sig(uis->touch_ended_cbs);
        if (strcmp(key,"TouchMoved")     == 0) return make_uis_sig(uis->touch_moved_cbs);
        if (strcmp(key,"TouchTap")       == 0) return make_uis_sig(uis->touch_tap_cbs);
        if (strcmp(key,"TouchTapInWorld")== 0) return make_uis_sig(uis->touch_tap_world_cbs);
        if (strcmp(key,"TouchLongPress") == 0) return make_uis_sig(uis->touch_long_press_cbs);
        if (strcmp(key,"TouchPan")       == 0) return make_uis_sig(uis->touch_pan_cbs);
        if (strcmp(key,"TouchPinch")     == 0) return make_uis_sig(uis->touch_pinch_cbs);
        if (strcmp(key,"TouchRotate")    == 0) return make_uis_sig(uis->touch_rotate_cbs);
        if (strcmp(key,"TouchSwipe")     == 0) return make_uis_sig(uis->touch_swipe_cbs);
        // La etiqueta que se pinta en un ProximityPrompt ("E", "F", "Space"...).
        // Sin esto el script de prompts de Roblox moria y NINGUN prompt salia.
        if (strcmp(key,"GetStringForKeyCode") == 0) {
            lua_pushcfunction(L, [](lua_State* pL) -> int {
                const int kc = (int)gl_num_or(pL, 2, 0);   // EnumItem o numero
                char uno[2];
                uno[0] = 0;
                uno[1] = 0;
                if (kc >= 33 && kc <= 126) {   // ASCII imprimible: letras y digitos
                    uno[0] = (char)((kc >= 'a' && kc <= 'z') ? (kc - 32) : kc);
                    lua_pushstring(pL, uno); return 1;
                }
                switch (kc) {
                    case 32:  lua_pushstring(pL, "Space");   return 1;
                    case 13:  lua_pushstring(pL, "Enter");   return 1;
                    case 27:  lua_pushstring(pL, "Esc");     return 1;
                    case 8:   lua_pushstring(pL, "Backspace"); return 1;
                    case 9:   lua_pushstring(pL, "Tab");     return 1;
                    case 304: lua_pushstring(pL, "Shift");   return 1;
                    case 306: lua_pushstring(pL, "Ctrl");    return 1;
                    case 308: lua_pushstring(pL, "Alt");     return 1;
                    default:  lua_pushstring(pL, "");        return 1;
                }
            }, "GetStringForKeyCode");
            return 1;
        }
    }

    // ── TextService ───────────────────────────────────────────────────
    // GetTextSize(texto, tamaño, fuente, marco) -> Vector2. La `fuente` se
    // acepta y se ignora: aqui todo se dibuja con la misma tipografia, asi que
    // medir con ella es lo correcto. `marco` da el ancho de ajuste de linea.
    if (TextService* ts = gl_cast<TextService>(n)) {
        if (strcmp(key, "GetTextSize") == 0 || strcmp(key, "GetTextBoundsAsync") == 0) {
            const bool es_async = (strcmp(key, "GetTextBoundsAsync") == 0);
            lua_pushlightuserdata(L, (void*)ts);
            lua_pushboolean(L, es_async ? 1 : 0);
            lua_pushcclosure(L, [](lua_State* LL) -> int {
                TextService* t = (TextService*)lua_touserdata(LL, lua_upvalueindex(1));
                const bool as = lua_toboolean(LL, lua_upvalueindex(2)) != 0;
                if (!t) { push_vector2(LL, 0, 0); return 1; }
                String texto; int tam = 14; float ancho = -1.0f;
                Ref<Font> fuente;
                bool legacy = false;   // LegacyArial: la linea mide 1,5 x TextSize
                if (as && lua_istable(LL, 2)) {
                    // GetTextBoundsParams: {Text=, Size=, Width=, Font = Font}
                    lua_getfield(LL, 2, "Text");  if (lua_isstring(LL,-1)) texto = String::utf8(lua_tostring(LL,-1)); lua_pop(LL,1);
                    lua_getfield(LL, 2, "Size");  if (lua_isnumber(LL,-1)) tam  = (int)lua_tonumber(LL,-1);     lua_pop(LL,1);
                    lua_getfield(LL, 2, "Width"); if (lua_isnumber(LL,-1)) ancho= (float)lua_tonumber(LL,-1);   lua_pop(LL,1);
                    lua_getfield(LL, 2, "Font");
                    if (lua_istable(LL, -1)) {
                        lua_getfield(LL, -1, "Family");
                        const String familia = lua_isstring(LL, -1) ? String::utf8(lua_tostring(LL, -1)) : String();
                        lua_pop(LL, 1);
                        if (!familia.is_empty()) {
                            fuente = gl_fuente(familia, gl_lua_campo_enum(LL, lua_gettop(LL), "Weight", 400),
                                               gl_lua_campo_enum(LL, lua_gettop(LL), "Style", 0));
                            legacy = gl_familia_legacy(familia);
                        }
                    }
                    lua_pop(LL, 1);
                } else {
                    if (lua_isstring(LL, 2)) texto = String::utf8(lua_tostring(LL, 2));
                    if (lua_isnumber(LL, 3)) tam = (int)lua_tonumber(LL, 3);
                    // arg 4 = Enum.Font (EnumItem o numero), arg 5 = Vector2 del marco
                    int v_enum = -1;
                    if (lua_isnumber(LL, 4)) v_enum = (int)lua_tonumber(LL, 4);
                    else if (lua_istable(LL, 4)) {
                        lua_getfield(LL, 4, "Value");
                        if (lua_isnumber(LL, -1)) v_enum = (int)lua_tonumber(LL, -1);
                        lua_pop(LL, 1);
                    }
                    if (v_enum >= 0) {
                        const GLFuenteEnum& e = gl_fuente_enum(v_enum);
                        fuente = gl_fuente(e.familia, e.peso, e.estilo);
                        legacy = gl_familia_legacy(e.familia);
                    }
                    if (lua_istable(LL, 5)) {
                        lua_getfield(LL, 5, "X"); if (lua_isnumber(LL,-1)) ancho = (float)lua_tonumber(LL,-1); lua_pop(LL,1);
                    }
                }
                Vector2 m = t->measure_text_font(texto, tam, ancho, fuente, legacy);
                push_vector2(LL, m.x, m.y);
                return 1;
            }, "GetTextSize", 2);
            return 1;
        }
        if (strcmp(key, "FilterStringAsync") == 0) {
            // Sin moderacion propia: devuelve el texto tal cual, envuelto en un
            // objeto con los metodos que espera Roblox.
            lua_pushcfunction(L, [](lua_State* LL) -> int {
                const char* s = lua_isstring(LL, 2) ? lua_tostring(LL, 2) : "";
                lua_newtable(LL);
                lua_pushstring(LL, s);
                lua_pushcclosure(LL, [](lua_State* dL) -> int {
                    lua_pushstring(dL, lua_tostring(dL, lua_upvalueindex(1))); return 1;
                }, "GetNonChatStringForBroadcastAsync", 1);
                lua_pushvalue(LL, -1); lua_setfield(LL, -3, "GetNonChatStringForBroadcastAsync");
                lua_pushvalue(LL, -1); lua_setfield(LL, -3, "GetNonChatStringForUserAsync");
                lua_setfield(LL, -2, "GetChatForUserAsync");
                return 1;
            }, "FilterStringAsync");
            return 1;
        }
    }

    // ── CollectionService ─────────────────────────────────────────────
    CollectionService* cs = gl_cast<CollectionService>(n);
    if (cs) {
        if (strcmp(key,"AddTag") == 0) {
            lua_pushlightuserdata(L,(void*)cs);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                CollectionService* c=(CollectionService*)lua_touserdata(LL,lua_upvalueindex(1));
                GodotObjectWrapper* w=(GodotObjectWrapper*)lua_touserdata(LL,2);
                const char* tag=gl_str(LL,3);
                if(c&&w&&gow_node(w)) c->add_tag(gow_node(w),String::utf8(tag));
                return 0;
            },"AddTag",1); return 1;
        }
        if (strcmp(key,"RemoveTag") == 0) {
            lua_pushlightuserdata(L,(void*)cs);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                CollectionService* c=(CollectionService*)lua_touserdata(LL,lua_upvalueindex(1));
                GodotObjectWrapper* w=(GodotObjectWrapper*)lua_touserdata(LL,2);
                const char* tag=gl_str(LL,3);
                if(c&&w&&gow_node(w)) c->remove_tag(gow_node(w),String::utf8(tag));
                return 0;
            },"RemoveTag",1); return 1;
        }
        if (strcmp(key,"HasTag") == 0) {
            lua_pushlightuserdata(L,(void*)cs);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                CollectionService* c=(CollectionService*)lua_touserdata(LL,lua_upvalueindex(1));
                GodotObjectWrapper* w=(GodotObjectWrapper*)lua_touserdata(LL,2);
                const char* tag=gl_str(LL,3);
                bool r=false;
                if(c&&w&&gow_node(w)) r=c->has_tag(gow_node(w),String::utf8(tag));
                lua_pushboolean(LL,r?1:0); return 1;
            },"HasTag",1); return 1;
        }
        // GetTagged devuelve un ARRAY de instancias, como en Roblox: el idioma
        // universal es `for _, x in ipairs(CollectionService:GetTagged(t)) do`.
        //
        // Aqui habia un descuadre de la pila de Lua que lo rompia entero:
        // se empujaba un lightuserdata que nadie sacaba, asi que
        //   - con 1 etiquetado, `return 1` devolvia ESE lightuserdata en vez de
        //     la tabla  -> "invalid argument #1 to 'ipairs' (got userdata)";
        //   - con 2 o mas, el `rawseti(-3)` de la 2a vuelta ya no apuntaba a la
        //     tabla sino al lightuserdata -> "attempt to modify a readonly table";
        //   - y la rama else hacia `lua_pop(1)`, que se comia la propia tabla.
        // Ahora lo empuja wrap_node (un solo valor, bien formado) y el indice va
        // con su propio contador, para que saltarse una entrada invalida no deje
        // un hueco en el array.
        if (strcmp(key,"GetTagged") == 0) {
            lua_pushlightuserdata(L,(void*)cs);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                CollectionService* c=(CollectionService*)lua_touserdata(LL,lua_upvalueindex(1));
                if(!c||!lua_isstring(LL,2)){lua_newtable(LL);return 1;}
                String tag = String::utf8(lua_tostring(LL,2));
                TypedArray<Node> arr=c->get_tagged(tag);
                lua_newtable(LL);
                int n=0;
                for(int i=0;i<arr.size();i++){
                    Node* nd=gl_cast<Node>(arr[i]);
                    if(!nd) continue;
                    wrap_node(LL, nd);            // deja EXACTAMENTE 1 valor en el tope
                    lua_rawseti(LL,-2,++n);       // -2 = la tabla, siempre
                }
                return 1;
            },"GetTagged",1); return 1;
        }
        if (strcmp(key,"GetTags") == 0) {
            lua_pushlightuserdata(L,(void*)cs);
            lua_pushcclosure(L,[](lua_State* LL)->int{
                CollectionService* c=(CollectionService*)lua_touserdata(LL,lua_upvalueindex(1));
                GodotObjectWrapper* w=(GodotObjectWrapper*)lua_touserdata(LL,2);
                lua_newtable(LL);
                if(!c||!w||!gow_node(w)) return 1;
                PackedStringArray tags=c->get_tags(gow_node(w));
                for(int i=0;i<tags.size();i++){
                    lua_pushstring(LL,tags[i].utf8().get_data());
                    lua_rawseti(LL,-2,i+1);
                }
                return 1;
            },"GetTags",1); return 1;
        }
    }

    // ── UIGradient.Color (lectura) → ColorSequence ──────────────────────
    // Escribirlo ya funcionaba, pero LEERLO daba "Color is not a valid member of
    // UIGradient" y tumbaba modulos (p.ej. EasyVisuals.Presets.Rainbow, que lee
    // el gradiente para animarlo). Se devuelve un ColorSequence de verdad
    // construido con el ColorSequence.new del stdlib.
    // Con todas sus paradas (antes solo la primera y la ultima), y tambien la
    // Transparency como NumberSequence (antes daba nil).
    if (n->is_class("UIGradient") && (strcmp(key, "Color") == 0 || strcmp(key, "Transparency") == 0)) {
        const bool es_color = key[0] == 'C';
        static const StringName k_t("get_gl_times"), k_c("get_gl_cols"), k_at("get_gl_alpha_times"), k_av("get_gl_alpha_vals");
        const PackedFloat32Array t = n->call(es_color ? k_t : k_at);
        PackedColorArray cs;
        PackedFloat32Array vs;
        if (es_color) cs = n->call(k_c); else vs = n->call(k_av);
        int cuantos = es_color ? MIN(t.size(), cs.size()) : MIN(t.size(), vs.size());
        if (cuantos < 2) cuantos = 0;   // sin poner: constante (blanco u opaco)
        lua_getglobal(L, es_color ? "ColorSequence" : "NumberSequence");
        lua_getglobal(L, es_color ? "ColorSequenceKeypoint" : "NumberSequenceKeypoint");
        if (!lua_istable(L, -2) || !lua_istable(L, -1)) { lua_pop(L, 2); lua_pushnil(L); return 1; }
        lua_getfield(L, -2, "new");   // secuencia.new
        lua_getfield(L, -2, "new");   // keypoint.new
        lua_createtable(L, cuantos ? cuantos : 2, 0);
        for (int i = 0; i < (cuantos ? cuantos : 2); i++) {
            lua_pushvalue(L, -2);
            lua_pushnumber(L, cuantos ? t[i] : (double)i);
            if (es_color) {
                const Color c = cuantos ? cs[i] : Color(1, 1, 1);
                push_color3(L, c.r, c.g, c.b);
            } else lua_pushnumber(L, cuantos ? vs[i] : 0.0);
            lua_call(L, 2, 1);
            lua_rawseti(L, -2, i + 1);
        }
        lua_remove(L, -2);            // fuera keypoint.new
        lua_call(L, 1, 1);            // secuencia.new(tabla)
        lua_replace(L, -3);           // el resultado donde estaba la tabla global
        lua_pop(L, 1);
        return 1;
    }

    // Anchored en un Node3D que no es Part (el HumanoidRootPart del rig del
    // personaje lo es): se guarda como estado propio en vez de reventar.
    if (strcmp(key, "Anchored") == 0 && gl_cast<Node3D>(n) && !gl_has_property(n, "Anchored")) {
        lua_pushboolean(L, n->has_meta("_gl_anchored") ? (bool)n->get_meta("_gl_anchored") : 0);
        return 1;
    }

    // ── Puente genérico de lectura (solo clases nuevas con meta _gl_bridge) ──
    if (n->has_meta("_gl_bridge")) {
        if (gl_has_property(n, String(key))) {
            Variant gv = n->get(String(key));
            if (gl_variant_to_luau(L, gv)) return 1;
        }
        String _mk = String("_glp_") + String(key);
        if (n->has_meta(_mk) && gl_variant_to_luau(L, n->get_meta(_mk))) return 1;
        // Propiedades que apuntan a OTRA instancia (Beam.Attachment0/1,
        // Highlight.Adornee, Trail.Attachment0...). Se guardan por ObjectID, no
        // por puntero, asi que si el destino muere aqui sale nil en vez de
        // memoria liberada. Sin esto, leer beam.Attachment1 daba
        // "Attachment1 is not a valid member of Beam" y tumbaba el script.
        String _ok = String("_glo_") + String(key);
        if (n->has_meta(_ok)) {
            const uint64_t oid = (uint64_t)(int64_t)n->get_meta(_ok);
            wrap_node(L, gl_cast<Node>(ObjectDB::get_instance(oid)));
            return 1;
        }
        // Señales de las clases puente que llevan sus propias conexiones de
        // Luau (ver gl_evento_puente_connect).
        static const StringName k_con("_gl_conectar");
        if (n->has_method(k_con) && n->has_signal(StringName(key))) {
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)n->get_instance_id());
            lua_pushstring(L, key);
            lua_pushcclosure(L, gl_evento_puente_connect, "Connect", 2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }
    }

    Node* child = n->get_node_or_null(NodePath(key));
    if (child) { wrap_node(L, child); return 1; }
    // …y si Godot le cambio el nombre a un hermano repetido, buscarlo por el
    // nombre DE ROBLOX (ver gl_rbx_name). Sin esto, `cache["Caramello"]` daba
    // nil aunque el objeto estuviera ahi, solo que llamado "@RBXModel@13334".
    {
        const StringName buscado(key);
        const int nh = n->get_child_count();
        for (int i = 0; i < nh; i++) {
            Node* c = n->get_child(i);
            if (c && c->has_meta(GL_META_RBX_NAME) && gl_rbx_llamado(c, buscado)) {
                wrap_node(L, c); return 1;
            }
        }
    }

    // Roblox conserva los miembros con INICIAL MINUSCULA por compatibilidad
    // (humanoid.died, part.brickColor, señal:connect, :findFirstChild...). Los
    // juegos publicados hace años los siguen usando, asi que antes de dar el
    // error se reintenta con la inicial en mayuscula. La recursion termina
    // siempre: la clave reintentada ya empieza en mayuscula.
    if (key[0] >= 'a' && key[0] <= 'z') {
        const String upper = String::chr((char32_t)(key[0] - 32)) + String(key).substr(1);
        lua_pushstring(L, upper.utf8().get_data());
        lua_replace(L, 2);
        return godot_object_index_real(L);
    }

    // Propiedades que el importador guardo como meta con su nombre porque la
    // clase de aqui no las tiene (Sky.SkyboxBk, la configuracion de muchas
    // clases sin equivalente...): se leen tal cual. Las caras del cielo, aunque
    // el place no las traiga: el Sky de aqui es procedural.
    if (key[0] >= 'A' && key[0] <= 'Z') {
        const StringName mk(key);
        if (n->has_meta(mk) && gl_variant_to_luau(L, n->get_meta(mk))) return 1;
        if (gl_cara_del_cielo(key) && strcmp(gl_roblox_classname(n), "Sky") == 0) {
            lua_pushstring(L, "");
            return 1;
        }
    }

    // Clases PUENTE (stubs de clases de Roblox que aqui no tienen logica
    // propia): en Roblox todas sus propiedades existen, asi que devolver nil se
    // parece mucho mas a la realidad que reventar. Con el error, leer algo tan
    // normal como beam.Attachment1 antes de asignarlo tumbaba el script entero
    // (118 fallos por ejecucion solo con el rayo del tutorial).
    if (n->has_meta("_gl_bridge")) { lua_pushnil(L); return 1; }

    // ── RESPALDO GENERICO DE LECTURA ────────────────────────────────────
    // Si la clase REGISTRO la propiedad (ADD_PROPERTY), se puede leer. Sin
    // esto habia propiedades que se ESCRIBIAN bien pero al leerlas reventaban
    // el script: frame.Active, boton.AutoButtonColor, .Selectable, .Draggable,
    // .SizeConstraint, .BorderMode... En Roblox toda propiedad que existe se
    // lee. Va al FINAL, asi que no le quita el sitio a ningun caso especial ni
    // cuesta nada en el camino normal; y gl_has_property esta cacheado por
    // clase, o sea una sola consulta por clase en toda la sesion.
    if (gl_has_property(n, String(key))) {
        Variant gv = n->get(String(key));
        if (gl_variant_to_luau(L, gv)) return 1;
    }

    // COMO ROBLOX: indexar un miembro que no existe es un ERROR, no nil.
    // (Para sondear sin error existen FindFirstChild / pcall, igual que alla.)
    luaL_error(L, "%s is not a valid member of %s \"%s\"",
               key, gl_roblox_classname(n), gl_instance_fullname(n).utf8().get_data());
    return 0;
}

#endif // LUAU_API_INDICE_JUEGO_H

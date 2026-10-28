#ifndef GL_PUENTE_ANIMACION_H
#define GL_PUENTE_ANIMACION_H
#include "gl_cast.h"

// ════════════════════════════════════════════════════════════════════
//  Animacion en el puente (F7): AnimationTrack y LoadAnimation /
//  GetPlayingAnimationTracks / AnimationPlayed en Humanoid, Animator y
//  AnimationController. El motor esta en gl_animador.h.
//
//  Los metodos guardan el ObjectID, no el puntero: la pista o el rig pueden
//  destruirse mientras un script guarda la funcion.
// ════════════════════════════════════════════════════════════════════

inline AnimationTrack* gl_pista_de_upvalue(lua_State* L) {
    const uint64_t id = (uint64_t)(uintptr_t)lua_touserdata(L, lua_upvalueindex(1));
    return gl_cast<AnimationTrack>(ObjectDB::get_instance(ObjectID(id)));
}
inline Node* gl_nodo_de_upvalue(lua_State* L) {
    const uint64_t id = (uint64_t)(uintptr_t)lua_touserdata(L, lua_upvalueindex(1));
    return gl_cast<Node>(ObjectDB::get_instance(ObjectID(id)));
}

// Senal de una pista: 0 Stopped, 1 Ended, 2 DidLoop, 3 KeyframeReached, 4 una marca.
inline int gl_pista_senal(lua_State* L, AnimationTrack* t, int cual, const String& marca = String()) {
    lua_newtable(L);
    lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)t->get_instance_id());
    lua_pushinteger(L, cual);
    lua_pushstring(L, marca.utf8().get_data());
    lua_pushcclosure(L, [](lua_State* LL) -> int {
        AnimationTrack* p = gl_pista_de_upvalue(LL);
        const int que = (int)lua_tointeger(LL, lua_upvalueindex(2));
        int fn = -1;
        for (int i = 1; i <= lua_gettop(LL) && fn < 0; i++) if (lua_isfunction(LL, i)) fn = i;
        if (!p || fn < 0) { lua_pushnil(LL); return 1; }
        lua_pushvalue(LL, fn);
        const int ref = gl_ref(LL, -1);
        lua_pop(LL, 1);
        lua_State* mL = gl_main_of(LL);
        if (que == 0) p->stopped_cbs.push_back({ mL, ref, true });
        else if (que == 1) p->ended_cbs.push_back({ mL, ref, true });
        else if (que == 2) p->did_loop_cbs.push_back({ mL, ref, true });
        else if (que == 3) p->keyframe_cbs.push_back({ mL, ref, true });
        else p->marca_cbs.push_back({ mL, ref, true, String::utf8(lua_tostring(LL, lua_upvalueindex(3))) });
        _gl_push_connection(LL, p, ref);
        return 1;
    }, "Connect", 3);
    lua_setfield(L, -2, "Connect");
    gl_signal_lowercase_aliases(L);
    return 1;
}

inline int gl_pista_metodo(lua_State* L, AnimationTrack* t, lua_CFunction f, const char* nombre) {
    lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)t->get_instance_id());
    lua_pushcclosure(L, f, nombre, 1);
    return 1;
}

// ── AnimationTrack: lectura. -1 si la clave no es suya. ──────────────
inline int gl_animacion_index(lua_State* L, AnimationTrack* t, const char* key) {
    if (strcmp(key, "IsPlaying") == 0)     { lua_pushboolean(L, t->sonando ? 1 : 0); return 1; }
    if (strcmp(key, "Length") == 0)        { lua_pushnumber(L, t->gl_duracion()); return 1; }
    if (strcmp(key, "TimePosition") == 0)  { lua_pushnumber(L, t->gl_tiempo()); return 1; }
    if (strcmp(key, "Speed") == 0)         { lua_pushnumber(L, t->velocidad); return 1; }
    if (strcmp(key, "Looped") == 0)        { lua_pushboolean(L, t->bucle ? 1 : 0); return 1; }
    if (strcmp(key, "Priority") == 0)      { uis_push_enum(L, "AnimationPriority", t->prioridad); return 1; }
    if (strcmp(key, "WeightCurrent") == 0) { lua_pushnumber(L, t->peso); return 1; }
    if (strcmp(key, "WeightTarget") == 0)  { lua_pushnumber(L, t->peso_objetivo); return 1; }
    if (strcmp(key, "Animation") == 0) {
        wrap_node(L, t->animacion_id.is_valid() ? gl_cast<Node>(ObjectDB::get_instance(t->animacion_id)) : nullptr);
        return 1;
    }
    if (strcmp(key, "Stopped") == 0)         return gl_pista_senal(L, t, 0);
    if (strcmp(key, "Ended") == 0)           return gl_pista_senal(L, t, 1);
    if (strcmp(key, "DidLoop") == 0)         return gl_pista_senal(L, t, 2);
    if (strcmp(key, "KeyframeReached") == 0) return gl_pista_senal(L, t, 3);
    // Play(fadeTime = 0.1, weight = 1, speed = 1)
    if (strcmp(key, "Play") == 0) return gl_pista_metodo(L, t, [](lua_State* LL) -> int {
        AnimationTrack* p = gl_pista_de_upvalue(LL);
        if (p) p->gl_play((float)luaL_optnumber(LL, 2, 0.1), (float)luaL_optnumber(LL, 3, 1.0),
                          (float)luaL_optnumber(LL, 4, 1.0));
        return 0;
    }, "Play");
    // Stop(fadeTime = 0.1)
    if (strcmp(key, "Stop") == 0) return gl_pista_metodo(L, t, [](lua_State* LL) -> int {
        AnimationTrack* p = gl_pista_de_upvalue(LL);
        if (p) p->gl_stop((float)luaL_optnumber(LL, 2, 0.1));
        return 0;
    }, "Stop");
    if (strcmp(key, "AdjustSpeed") == 0) return gl_pista_metodo(L, t, [](lua_State* LL) -> int {
        AnimationTrack* p = gl_pista_de_upvalue(LL);
        if (p) p->gl_ajustar_velocidad((float)luaL_optnumber(LL, 2, 1.0));
        return 0;
    }, "AdjustSpeed");
    // AdjustWeight(weight = 1, fadeTime = 0.1): el Animate de cualquier NPC lo
    // llama en cada paso para mezclar andar y correr.
    if (strcmp(key, "AdjustWeight") == 0) return gl_pista_metodo(L, t, [](lua_State* LL) -> int {
        AnimationTrack* p = gl_pista_de_upvalue(LL);
        if (p) p->gl_ajustar_peso((float)luaL_optnumber(LL, 2, 1.0), (float)luaL_optnumber(LL, 3, 0.1));
        return 0;
    }, "AdjustWeight");
    if (strcmp(key, "GetMarkerReachedSignal") == 0) return gl_pista_metodo(L, t, [](lua_State* LL) -> int {
        AnimationTrack* p = gl_pista_de_upvalue(LL);
        if (!p) { lua_pushnil(LL); return 1; }
        return gl_pista_senal(LL, p, 4, String::utf8(luaL_checkstring(LL, 2)));
    }, "GetMarkerReachedSignal");
    if (strcmp(key, "GetTimeOfKeyframe") == 0) return gl_pista_metodo(L, t, [](lua_State* LL) -> int {
        AnimationTrack* p = gl_pista_de_upvalue(LL);
        const char* nombre = luaL_checkstring(LL, 2);
        const float tt = p ? p->gl_tiempo_de_keyframe(String::utf8(nombre)) : -1.0f;
        if (tt < 0.0f) { luaL_error(LL, "Could not find a keyframe by the name of %s", nombre); return 0; }
        lua_pushnumber(LL, tt);
        return 1;
    }, "GetTimeOfKeyframe");
    return -1;
}

// ── AnimationTrack: escritura ────────────────────────────────────────
inline bool gl_animacion_newindex(lua_State* L, AnimationTrack* t, const char* key) {
    if (strcmp(key, "Looped") == 0)       { t->gl_poner_bucle(lua_toboolean(L, 3) != 0); return true; }
    if (strcmp(key, "Priority") == 0)     { t->prioridad = (int)gl_num_or(L, 3, 2); return true; }
    if (strcmp(key, "TimePosition") == 0) { t->gl_poner_tiempo((float)gl_num(L, 3)); return true; }
    // Speed es de solo lectura en Roblox; se acepta por los scripts viejos de aqui.
    if (strcmp(key, "Speed") == 0)        { t->gl_ajustar_velocidad((float)gl_num(L, 3)); return true; }
    return false;
}

// ── Humanoid, Animator y AnimationController ─────────────────────────
inline bool gl_es_dueno_de_animacion(Node* n) {
    if (gl_cast<Humanoid>(n)) return true;
    const String c = gl_anim_clase(n);
    return c == "Animator" || c == "AnimationController";
}

// LoadAnimation y compania. -1 si la clave no es suya.
inline int gl_animador_index(lua_State* L, Node* n, const char* key) {
    const uint64_t id = (uint64_t)n->get_instance_id();
    if (strcmp(key, "LoadAnimation") == 0) {
        lua_pushlightuserdata(L, (void*)(uintptr_t)id);
        lua_pushcclosure(L, [](lua_State* LL) -> int {
            Node* dueno = gl_nodo_de_upvalue(LL);
            if (!dueno) { lua_pushnil(LL); return 1; }
            Node* anim = _gl_has_metatable(LL, 2, "GodotObject") ? gow_node((GodotObjectWrapper*)lua_touserdata(LL, 2)) : nullptr;
            AnimationObject* ao = gl_cast<AnimationObject>(anim);
            String aid;
            String nombre_pista;
            String nombre_legado;
            if (ao) {
                aid = ao->get_animation_id();
                nombre_pista = gl_rbx_name(ao);
                nombre_legado = ao->get_anim_name();
            } else if (lua_isstring(LL, 2)) {
                // humanoid:LoadAnimation("walk"): la del AnimationPlayer del rig (API vieja de aqui).
                nombre_pista = nombre_legado = String::utf8(lua_tostring(LL, 2));
            } else {
                luaL_error(LL, "Unable to cast value to Object");
                return 0;
            }
            wrap_node(LL, gl_cargar_animacion(dueno, ao, aid, nombre_pista, nombre_legado));
            return 1;
        }, "LoadAnimation", 1);
        return 1;
    }
    if (strcmp(key, "GetPlayingAnimationTracks") == 0) {
        lua_pushlightuserdata(L, (void*)(uintptr_t)id);
        lua_pushcclosure(L, [](lua_State* LL) -> int {
            lua_newtable(LL);
            GLAnimador* a = gl_animador_de(gl_nodo_de_upvalue(LL), false);
            if (!a) return 1;
            int k = 0;
            for (const ObjectID& pid : a->pistas) {
                AnimationTrack* t = gl_cast<AnimationTrack>(ObjectDB::get_instance(pid));
                if (!t || !t->sonando) continue;
                wrap_node(LL, t);
                lua_rawseti(LL, -2, ++k);
            }
            return 1;
        }, "GetPlayingAnimationTracks", 1);
        return 1;
    }
    if (strcmp(key, "AnimationPlayed") == 0) {
        lua_newtable(L);
        lua_pushlightuserdata(L, (void*)(uintptr_t)id);
        lua_pushcclosure(L, [](lua_State* LL) -> int {
            GLAnimador* a = gl_animador_de(gl_nodo_de_upvalue(LL), true);
            int fn = -1;
            for (int i = 1; i <= lua_gettop(LL) && fn < 0; i++) if (lua_isfunction(LL, i)) fn = i;
            if (!a || fn < 0) { lua_pushnil(LL); return 1; }
            lua_pushvalue(LL, fn);
            const int ref = gl_ref(LL, -1);
            lua_pop(LL, 1);
            a->played_cbs.push_back({ gl_main_of(LL), ref, true });
            _gl_push_connection(LL, a, ref);
            return 1;
        }, "Connect", 1);
        lua_setfield(L, -2, "Connect");
        gl_signal_lowercase_aliases(L);
        return 1;
    }
    // Del editor de animaciones / la fisica de Roblox: aqui no hacen falta.
    if (strcmp(key, "StepAnimation") == 0 || strcmp(key, "ApplyJointVelocities") == 0) {
        lua_pushcfunction(L, [](lua_State*) -> int { return 0; }, "StepAnimation");
        return 1;
    }
    return -1;
}

#endif // GL_PUENTE_ANIMACION_H

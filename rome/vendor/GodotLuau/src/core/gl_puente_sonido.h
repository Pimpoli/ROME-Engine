#ifndef GL_PUENTE_SONIDO_H
#define GL_PUENTE_SONIDO_H
#include "gl_cast.h"

// ════════════════════════════════════════════════════════════════════
//  Sound, SoundGroup y SoundService:PlayLocalSound en el puente.
//  Se incluye desde luau_api.h, despues de sus ayudantes. Va aparte para no
//  seguir engordando luau_api.h.
//
//  Los metodos guardan el ObjectID del sonido, no el puntero: un script puede
//  quedarse `local tocar = s.Play` y el sonido destruirse antes de llamarla.
// ════════════════════════════════════════════════════════════════════

inline RobloxSound* gl_sonido_de_upvalue(lua_State* L) {
    const uint64_t id = (uint64_t)(uintptr_t)lua_touserdata(L, lua_upvalueindex(1));
    return gl_cast<RobloxSound>(ObjectDB::get_instance(ObjectID(id)));
}

// ── Lectura. Devuelve cuantos valores dejo en la pila, o -1 si no es suya. ──
inline int gl_sonido_index(lua_State* L, RobloxSound* s, const char* key) {
    if (strcmp(key, "SoundId") == 0)      { lua_pushstring(L, s->get_sound_id().utf8().get_data()); return 1; }
    if (strcmp(key, "Volume") == 0)       { lua_pushnumber(L, s->get_volume()); return 1; }
    if (strcmp(key, "Pitch") == 0 || strcmp(key, "PlaybackSpeed") == 0) { lua_pushnumber(L, s->get_pitch()); return 1; }
    if (strcmp(key, "Looped") == 0)       { lua_pushboolean(L, s->get_looped()); return 1; }
    if (strcmp(key, "Playing") == 0)      { lua_pushboolean(L, s->get_playing()); return 1; }
    if (strcmp(key, "IsPlaying") == 0)    { lua_pushboolean(L, s->is_playing()); return 1; }
    if (strcmp(key, "IsPaused") == 0)     { lua_pushboolean(L, s->is_paused()); return 1; }
    if (strcmp(key, "IsLoaded") == 0)     { lua_pushboolean(L, s->is_loaded()); return 1; }
    if (strcmp(key, "TimePosition") == 0) { lua_pushnumber(L, s->get_time_position()); return 1; }
    if (strcmp(key, "TimeLength") == 0)   { lua_pushnumber(L, s->get_time_length()); return 1; }
    if (strcmp(key, "PlayOnRemove") == 0) { lua_pushboolean(L, s->get_play_on_remove()); return 1; }
    if (strcmp(key, "RollOffMode") == 0)  { uis_push_enum(L, "RollOffMode", s->get_rolloff_mode()); return 1; }
    if (strcmp(key, "RollOffMinDistance") == 0 || strcmp(key, "EmitterSize") == 0) { lua_pushnumber(L, s->get_rolloff_min()); return 1; }
    if (strcmp(key, "RollOffMaxDistance") == 0) { lua_pushnumber(L, s->get_rolloff_max()); return 1; }
    if (strcmp(key, "PlaybackLoudness") == 0)   { lua_pushnumber(L, s->get_playback_loudness()); return 1; }
    if (strcmp(key, "SoundGroup") == 0) {
        if (RobloxSoundGroup* g = s->gl_get_sound_group_node()) wrap_node(L, g); else lua_pushnil(L);
        return 1;
    }

    // Metodos: Play / Stop / Pause / Resume
    lua_CFunction metodo = nullptr;
    if (strcmp(key, "Play") == 0)
        metodo = [](lua_State* LL) -> int { if (RobloxSound* x = gl_sonido_de_upvalue(LL)) x->play(); return 0; };
    else if (strcmp(key, "Stop") == 0)
        metodo = [](lua_State* LL) -> int { if (RobloxSound* x = gl_sonido_de_upvalue(LL)) x->stop(); return 0; };
    else if (strcmp(key, "Pause") == 0)
        metodo = [](lua_State* LL) -> int { if (RobloxSound* x = gl_sonido_de_upvalue(LL)) x->pause(); return 0; };
    else if (strcmp(key, "Resume") == 0)
        metodo = [](lua_State* LL) -> int { if (RobloxSound* x = gl_sonido_de_upvalue(LL)) x->resume(); return 0; };
    if (metodo) {
        lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)s->get_instance_id());
        lua_pushcclosure(L, metodo, key, 1);
        return 1;
    }

    // Señales del sonido. Ended guardaba la funcion y no la llamaba nunca
    // (y Connect devolvia nil): la musica que encadena canciones con
    // `sound.Ended:Wait()` se quedaba parada en la primera.
    int sig_sonido = -1;
    if      (strcmp(key, "Ended")   == 0) sig_sonido = 0;
    else if (strcmp(key, "Played")  == 0) sig_sonido = 1;
    else if (strcmp(key, "Stopped") == 0) sig_sonido = 2;
    else if (strcmp(key, "Paused")  == 0) sig_sonido = 3;
    else if (strcmp(key, "Resumed") == 0) sig_sonido = 4;
    else if (strcmp(key, "Loaded")  == 0) sig_sonido = 5;
    else if (strcmp(key, "DidLoop") == 0) sig_sonido = 6;
    if (sig_sonido < 0) return -1;
    lua_newtable(L);
    lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)s->get_instance_id());
    lua_pushinteger(L, sig_sonido);
    lua_pushcclosure(L, [](lua_State* LL) -> int {
        RobloxSound* x = gl_sonido_de_upvalue(LL);
        const int cual = (int)lua_tointeger(LL, lua_upvalueindex(2));
        if (!x || !lua_isfunction(LL, 2)) { lua_pushnil(LL); return 1; }
        lua_pushvalue(LL, 2);
        const int ref = gl_ref(LL, -1); lua_pop(LL, 1);
        lua_State* mL = gl_main_of(LL);
        switch (cual) {
            case 0: x->ended_cbs.push_back({ mL, ref, true });   break;
            case 1: x->played_cbs.push_back({ mL, ref, true });  break;
            case 2: x->stopped_cbs.push_back({ mL, ref, true }); break;
            case 3: x->paused_cbs.push_back({ mL, ref, true });  break;
            case 4: x->resumed_cbs.push_back({ mL, ref, true }); break;
            case 5: x->loaded_cbs.push_back({ mL, ref, true });  break;
            default: x->add_looped_cb(mL, ref);                  break;
        }
        _gl_push_connection(LL, x, ref);
        return 1;
    }, "Connect", 2);
    lua_setfield(L, -2, "Connect");
    gl_signal_lowercase_aliases(L);
    return 1;
}

// ── Escritura. Devuelve true si la propiedad era suya. ──
inline bool gl_sonido_newindex(lua_State* L, RobloxSound* s, const char* key) {
    if (strcmp(key, "SoundId") == 0)      { s->set_sound_id(String::utf8(gl_str(L, 3))); return true; }
    if (strcmp(key, "Volume") == 0)       { s->set_volume((float)gl_num(L, 3)); return true; }
    if (strcmp(key, "Pitch") == 0 || strcmp(key, "PlaybackSpeed") == 0) { s->set_pitch((float)gl_num(L, 3)); return true; }
    if (strcmp(key, "Looped") == 0)       { s->set_looped(lua_toboolean(L, 3) != 0); return true; }
    if (strcmp(key, "Playing") == 0)      { s->set_playing(lua_toboolean(L, 3) != 0); return true; }
    if (strcmp(key, "TimePosition") == 0) { s->set_time_position((float)gl_num(L, 3)); return true; }
    if (strcmp(key, "PlayOnRemove") == 0) { s->set_play_on_remove(lua_toboolean(L, 3) != 0); return true; }
    if (strcmp(key, "RollOffMode") == 0)  { s->set_rolloff_mode((int)gl_num_or(L, 3, 0.0)); return true; }
    if (strcmp(key, "RollOffMinDistance") == 0 || strcmp(key, "EmitterSize") == 0) { s->set_rolloff_min((float)gl_num(L, 3)); return true; }
    if (strcmp(key, "RollOffMaxDistance") == 0) { s->set_rolloff_max((float)gl_num(L, 3)); return true; }
    if (strcmp(key, "SoundGroup") == 0) {
        if (_gl_has_metatable(L, 3, "GodotObject")) {
            GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 3);
            s->set_sound_group(Variant(w ? gow_node(w) : nullptr));
        } else if (lua_isstring(L, 3)) {
            s->set_sound_group(String::utf8(lua_tostring(L, 3)));   // nombre de bus (API vieja)
        } else {
            s->set_sound_group(Variant());
        }
        return true;
    }
    return false;
}

// SoundService:PlayLocalSound(sound)
inline int gl_soundservice_index(lua_State* L, const char* key) {
    if (strcmp(key, "PlayLocalSound") != 0) return -1;
    lua_pushcfunction(L, [](lua_State* LL) -> int {
        const int i = lua_gettop(LL) >= 2 ? 2 : 1;
        if (!_gl_has_metatable(LL, i, "GodotObject")) return 0;
        GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(LL, i);
        if (RobloxSound* x = gl_cast<RobloxSound>(w ? gow_node(w) : nullptr)) x->gl_tocar_local();
        return 0;
    }, "PlayLocalSound");
    return 1;
}

#endif // GL_PUENTE_SONIDO_H

#ifndef GL_PUENTE_TEXTURAS_H
#define GL_PUENTE_TEXTURAS_H

// El streaming de texturas (gl_streaming_texturas.h) visto desde Luau: lo usa
// ContentProvider:PreloadAsync del stdlib para pedir las imagenes YA y esperar
// a que lleguen, como en Roblox. Se incluye desde luau_api_metodos.h y se
// registra al crear cada VM (luau_script_contexto.h).
//   __gl_tex_precargar(id) -> 0 nada que esperar, 1 cargando, 2 lista
//   __gl_tex_estado(id)    -> lo mismo, sin pedirla
//   __gl_tex_pendientes()  -> texturas en cola o cargando

#include "gl_streaming_texturas.h"

// Ruta de la imagen de un id de Roblox (o de una ruta res:// / user://) y si es
// un recurso importado. Lo mismo que resuelve gl_gui_load_image.
inline bool gl_tex_resolver(const String& id, String& ruta, bool& importada) {
    if (id.is_empty()) return false;
    if (gl_is_roblox_asset(id)) {
        ruta = gl_local_asset_path(id);
        importada = true;
        if (ruta.is_empty()) { ruta = gl_local_asset_file(id); importada = false; }
        if (ruta.is_empty()) { ruta = gl_cache_asset_path(id); importada = false; }
        if (ruta.is_empty()) return false;
        // gl_local_asset_path tambien da sonidos y mallas: solo imagenes.
        static const char* kImgs[] = { ".png", ".jpg", ".jpeg", ".webp", ".svg" };
        for (int i = 0; i < 5; i++)
            if (ruta.ends_with(kImgs[i])) return true;
        return false;
    }
    if (!id.begins_with("res://") && !id.begins_with("user://")) return false;
    ruta = id;
    ResourceLoader* rl = ResourceLoader::get_singleton();
    importada = rl && rl->exists(ruta);
    return importada || FileAccess::file_exists(ruta);
}

// 0 nada que esperar (no es imagen, no esta o va por el motor), 1 cargando, 2 lista.
inline int gl_tex_estado_de(const String& id, bool pedir) {
    if (!gl_tex_streaming_activo()) return 0;
    String ruta;
    bool importada = false;
    if (!gl_tex_resolver(id, ruta, importada) || (importada && gl_tex_motor_streaming())) return 0;
    if (gl_texturas_crudas().has(ruta)) return 2;
    GLTexCarga* c = nullptr;
    if (pedir) c = gl_tex_pedir(ruta, importada, nullptr);
    else if (GLTexCarga** hay = gl_tex_streaming().cargas.getptr(ruta)) c = *hay;
    if (!c) return 0;
    return c->estado == 2 ? 2 : (c->estado == 3 ? 0 : 1);
}

inline void gl_tex_registrar(lua_State* L) {
    lua_pushcfunction(L, [](lua_State* pL) -> int {
        lua_pushinteger(pL, gl_tex_estado_de(String::utf8(luaL_optstring(pL, 1, "")), true));
        return 1;
    }, "__gl_tex_precargar");
    lua_setglobal(L, "__gl_tex_precargar");
    lua_pushcfunction(L, [](lua_State* pL) -> int {
        lua_pushinteger(pL, gl_tex_estado_de(String::utf8(luaL_optstring(pL, 1, "")), false));
        return 1;
    }, "__gl_tex_estado");
    lua_setglobal(L, "__gl_tex_estado");
    lua_pushcfunction(L, [](lua_State* pL) -> int {
        lua_pushinteger(pL, gl_tex_pendientes());
        return 1;
    }, "__gl_tex_pendientes");
    lua_setglobal(L, "__gl_tex_pendientes");
}

#endif // GL_PUENTE_TEXTURAS_H

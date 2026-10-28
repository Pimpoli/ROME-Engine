#ifndef GL_INVOCAR_H
#define GL_INVOCAR_H

// Invocaciones LOCALES que pueden ceder: BindableFunction:Invoke, y
// InvokeServer/InvokeClient cuando el manejador vive en esta misma maquina
// (un jugador, o el host invocando a su propio cliente).
//
// Antes el manejador corria con lua_pcall: un OnInvoke con task.wait,
// WaitForChild o una llamada a DataStore moria con "attempt to yield across
// metamethod/C-call boundary", y un error dentro del manejador se tragaba en
// silencio (el invocador recibia nil).
//
// Ahora el manejador corre como TAREA de su script, envuelto en
// __gl_inv_local (luau_script.h). Si acaba sin ceder, Invoke devuelve en el
// acto; si cede, el que invoca cede tambien y se reanuda cuando el manejador
// termina, por el mismo camino que una respuesta de red (gl_net_responses, paso
// 5 de ScriptNodeBase::_process). El error del manejador se relanza en el
// invocador, como Roblox.

#include "gl_runtime.h"
#include "gl_net_bridge.h"
#include "roblox_remote.h"   // _lua_cross_push, _gl_push_node

// Suelta los valores guardados de una respuesta local (si nadie los recoge).
inline void gl_inv_soltar(GLNetResponse& r) {
    if (r.local_L && r.local_ref >= 0 && gl_state_alive(r.local_L)) gl_unref(r.local_L, r.local_ref);
    r.local_L = nullptr;
    r.local_ref = -1;
    r.local_n = 0;
}

// __gl_inv_fin(callId, ok, ...): lo llama el envoltorio al acabar el manejador.
// Los valores se quedan en la VM del manejador (una tabla en el registro) y se
// copian al invocador cuando se reanuda; asi una tabla o una instancia no pasa
// por la codificacion de red. Si fallo, se guarda el mensaje como texto.
inline int gl_inv_fin(lua_State* L) {
    const int64_t cid = (int64_t)luaL_checknumber(L, 1);
    const bool ok = lua_toboolean(L, 2) != 0;
    const int top = lua_gettop(L);
    GLNetResponse& r = gl_net_responses()[cid];
    gl_inv_soltar(r);
    r.rets = Array();
    if (ok) {
        lua_createtable(L, top - 2, 0);
        for (int i = 3; i <= top; i++) {
            lua_pushvalue(L, i);
            lua_rawseti(L, -2, i - 2);
        }
        r.local_ref = gl_ref(L, -1);
        lua_pop(L, 1);
        r.local_L = gl_main_of(L);
        r.local_n = top - 2;
    } else {
        const char* msg = top >= 3 ? luaL_tolstring(L, 3, nullptr) : "invoke failed";
        r.rets.push_back(String::utf8(msg ? msg : "invoke failed"));
    }
    r.ok = ok;
    r.ready = true;
    r.age = 0.0;
    return 0;
}

// Empuja en `dst` los valores de una respuesta local ya lista y los suelta.
// Devuelve cuantos. En la misma VM se mueven tal cual (misma tabla, mismas
// funciones); entre VMs se copian.
inline int gl_inv_empujar(lua_State* dst, GLNetResponse& r) {
    int n = 0;
    lua_State* src = r.local_L;
    if (src && r.local_ref >= 0 && gl_state_alive(src)) {
        const bool misma = src == gl_main_of(dst);
        lua_checkstack(dst, r.local_n + 4);
        lua_rawgeti(src, LUA_REGISTRYINDEX, r.local_ref);
        const int t = lua_gettop(src);
        for (int i = 1; i <= r.local_n; i++) {
            lua_rawgeti(src, t, i);
            if (misma) lua_xmove(src, dst, 1);
            else { _lua_cross_push(src, -1, dst); lua_pop(src, 1); }
            n++;
        }
        lua_pop(src, 1);
    }
    gl_inv_soltar(r);
    return n;
}

// Corre el manejador (hL, href) con los argumentos [base, base+nargs) de pL.
// con_player: OnServerInvoke recibe el Player de primer argumento.
// Devuelve lo que devuelva la funcion C que la llama (resultados o cesion).
inline int gl_invocar_local(lua_State* pL, int base, int nargs, lua_State* hL, int href,
                            Node* player, bool con_player, const char* que) {
    if (!hL || href < 0 || !gl_state_alive(hL)) { lua_pushnil(pL); return 1; }
    const int64_t cid = gl_net_new_call_id();
    lua_State* th = lua_newthread(hL);
    lua_getglobal(th, "__gl_inv_local");
    if (!lua_isfunction(th, -1)) { lua_pop(hL, 1); lua_pushnil(pL); return 1; }
    lua_pushnumber(th, (double)cid);
    lua_rawgeti(th, LUA_REGISTRYINDEX, href);
    int n = 2;
    if (con_player) { _gl_push_node(th, player); n++; }
    const bool misma = gl_main_of(pL) == hL;
    lua_checkstack(th, nargs + 4);
    for (int i = base; i < base + nargs; i++) {
        if (misma) { lua_pushvalue(pL, i); lua_xmove(pL, th, 1); }
        else _lua_cross_push(pL, i, th);
        n++;
    }
    gl_resume_handler(th, n);
    const bool cedio = lua_status(th) == LUA_YIELD;
    lua_pop(hL, 1);   // el hilo: si cedio, lo sujeta su script como tarea

    std::unordered_map<int64_t, GLNetResponse>& resp = gl_net_responses();
    auto it = resp.find(cid);
    if (it != resp.end() && it->second.ready) {
        GLNetResponse r = it->second;
        resp.erase(it);
        if (!r.ok) {
            const String msg = r.rets.size() > 0 ? String(r.rets[0]) : String("invoke failed");
            luaL_error(pL, "%s", msg.utf8().get_data());
            return 0;
        }
        return gl_inv_empujar(pL, r);
    }
    // No arranco (tope de reentrada) o murio sin responder: no llegara nada.
    if (!cedio) { lua_pushnil(pL); return 1; }

    // El manejador cedio: el invocador espera su respuesta.
    Node* sn = gl_nodo_script_de(pL);
    lua_State* main_L = gl_main_of(pL);
    if (!main_L) main_L = pL;
    if (!sn || !gl_add_invoke_wait_hook() || !lua_isyieldable(pL)) {
        luaL_error(pL, "%s: the handler yielded, and this thread cannot wait for it", que);
        return 0;
    }
    lua_pushthread(pL);
    const int ref = gl_ref(pL, -1);
    lua_pop(pL, 1);
    gl_add_invoke_wait_hook()(sn, pL, main_L, ref, cid, 0.0);
    return gl_yield_externo(pL, "__WAIT_INVOKE__");
}

#endif // GL_INVOCAR_H

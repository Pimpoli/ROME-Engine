#ifndef GL_CONTEXTO_H
#define GL_CONTEXTO_H

// ════════════════════════════════════════════════════════════════════
//  Una VM de Luau por CONTEXTO, como Roblox (F8 de la v1.17)
//  ----------------------------------------------------------------
//  Antes cada script tenia su propia VM (luaL_newstate). En Roblox todos
//  los Script del servidor comparten una, y todos los LocalScript del
//  cliente otra: por eso `_G`, `shared` y lo que devuelve `require` son
//  los MISMOS para todos los scripts de un contexto. Con una VM por
//  script, un modulo con estado (el clasico DataManager) corria una vez
//  por cada script que lo pedia y cada uno veia su propia copia.
//
//  Ahora hay un GLContexto por (juego, servidor/cliente). Cada script es
//  un HILO de ese contexto con su propio entorno (luaL_sandboxthread: sus
//  globales son suyas y lo que no tiene lo lee de los globales comunes).
//
//  Lo que antes daba la VM propia y ahora hay que llevar a mano:
//   - De quien es un hilo: lua_setthreaddata(hilo, GLHilo*). Los hilos
//     nuevos lo heredan del padre (callback userthread), asi que los de
//     task.spawn, los de las señales, coroutine.create... son del script
//     que los creo. gl_main_of() devuelve el hilo base del script: es el
//     "main_L" que el puente usaba como identidad (gl_state_alive, colas).
//   - Que se libera al morir un script: antes lua_close() lo soltaba todo.
//     Ahora cada ref del registro apunta quien la creo (gl_ref/gl_unref) y
//     al morir el script se sueltan las suyas.
//   - Que un puntero de hilo muerto no se reutilice: el hilo base de un
//     script muerto queda anclado (cementerio) hasta que se cierra el
//     contexto. Si no, otro script podria nacer en la misma direccion y
//     gl_state_alive() daria por vivo un callback viejo.
// ════════════════════════════════════════════════════════════════════

#include "lua.h"
#include "lualib.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct GLContexto;

// Datos de cada hilo de un script (lua_setthreaddata).
struct GLHilo {
    uint64_t nodo_id = 0;            // ObjectID del ScriptNodeBase dueño (se valida antes de usarlo)
    lua_State* principal = nullptr;  // hilo base del script: su "main_L" de siempre
    GLContexto* ctx = nullptr;
    bool vivo = true;
    bool sistema = false;            // el dueño de las tareas del propio stdlib
    std::unordered_set<int> refs;    // refs del registro creadas por sus hilos
};

struct GLContexto {
    uint64_t id = 0;                 // unico en el proceso (no se repite al cerrar y abrir)
    lua_State* L = nullptr;          // estado global: su hilo principal es el del sistema
    uint64_t juego_id = 0;           // nodo Game del que es
    bool cliente = false;
    int vivos = 0;                   // scripts vivos (sin el de sistema)
    bool sistema_vivo = false;
    GLHilo* sistema = nullptr;
    std::vector<std::unique_ptr<GLHilo>> hilos;    // de todo script que corrio; se liberan al cerrar
    std::unordered_map<int, GLHilo*> dueno_ref;   // ref -> quien la creo
};

inline std::vector<GLContexto*>& gl_contextos() {
    static std::vector<GLContexto*> v;
    return v;
}

inline GLHilo* gl_hilo_de(lua_State* L) {
    return L ? static_cast<GLHilo*>(lua_getthreaddata(L)) : nullptr;
}

inline GLContexto* gl_contexto_por_id(uint64_t id) {
    if (id == 0) return nullptr;
    for (GLContexto* c : gl_contextos()) if (c->id == id) return c;
    return nullptr;
}

// El contexto de cualquier hilo (tenga o no dueño).
inline GLContexto* gl_contexto_de(lua_State* L) {
    if (!L) return nullptr;
    if (GLHilo* h = gl_hilo_de(L)) return h->ctx;
    lua_State* m = lua_mainthread(L);
    for (GLContexto* c : gl_contextos()) if (c->L == m) return c;
    return nullptr;
}

inline GLContexto* gl_contexto_buscar(uint64_t juego_id, bool cliente) {
    for (GLContexto* c : gl_contextos())
        if (c->juego_id == juego_id && c->cliente == cliente) return c;
    return nullptr;
}

// ── Refs con dueño ───────────────────────────────────────────────────
// Todo el puente usa estas en vez de lua_ref/lua_unref. Una ref creada en
// un hilo de un script es de ese script: se suelta sola cuando muere. Al
// soltarla a mano solo cuenta si la suelta su dueño; si no, se deja (la
// soltara su dueño al morir). Asi una ref ya liberada por la muerte de su
// script y reutilizada por otro nunca se suelta dos veces.
inline int gl_ref(lua_State* L, int idx) {
    const int r = lua_ref(L, idx);
    if (r > 0) {
        if (GLHilo* h = gl_hilo_de(L)) {
            if (h->ctx && h->vivo) {
                h->refs.insert(r);
                h->ctx->dueno_ref[r] = h;
            }
        }
    }
    return r;
}

inline void gl_unref(lua_State* L, int r) {
    if (!L || r <= 0) return;
    GLContexto* ctx = gl_contexto_de(L);
    GLHilo* h = gl_hilo_de(L);
    if (!ctx) { lua_unref(L, r); return; }
    std::unordered_map<int, GLHilo*>::iterator it = ctx->dueno_ref.find(r);
    if (it != ctx->dueno_ref.end()) {
        if (it->second != h) return;          // es de otro (o ya se reutilizo): la suelta su dueño
        h->refs.erase(r);
        ctx->dueno_ref.erase(it);
        lua_unref(ctx->L, r);
        return;
    }
    // Sin dueño apuntado: o es del propio contexto (creada sin script) o ya
    // la solto la muerte de su script. Solo el contexto suelta las suyas.
    if (!h || h->sistema) lua_unref(ctx->L, r);
}

// Los hilos nuevos son del mismo script que su padre.
inline void gl_hilo_heredar(lua_State* padre, lua_State* L) {
    if (padre) lua_setthreaddata(L, lua_getthreaddata(padre));
}

#endif // GL_CONTEXTO_H

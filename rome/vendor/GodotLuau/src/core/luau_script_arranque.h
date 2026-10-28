#ifndef LUAU_SCRIPT_ARRANQUE_H
#define LUAU_SCRIPT_ARRANQUE_H
#include "gl_cast.h"

// Metodos de ScriptNodeBase: el arranque de la VM de cada script
// (iniciar_corrutina: globales, stdlib, puente), require y RemoteFunction,
// y el rescate del codigo Luau.
// Va aparte de luau_script.h (pasaba de 2.000 lineas) y se incluye desde
// alli, justo despues de la clase.

// RESCATE por script_id (1.17): un nodo guardado sin el ExtResource
// enlazado (escena editada a mano, merge, migracion) todavia sabe cual es su
// archivo. Se carga el texto de GodotLuau/<Clase>s/<id>.lua y el script corre
// igual. Lo usan el arranque de Script/LocalScript Y el require() de modulos.
inline void ScriptNodeBase::gl_rescue_codigo_luau() {
    if (codigo_luau.is_valid()) return;
    String sid = get("script_id");
    if (!sid.is_empty()) {
        String fp = String("res://GodotLuau/") + get_class() + "s/" + sid + ".lua";
        Ref<FileAccess> rf = FileAccess::open(fp, FileAccess::READ);
        if (rf.is_valid()) {
            Ref<LuauScript> ls; ls.instantiate();
            ls->_set_source_code(rf->get_as_text());
            codigo_luau = ls;
            return;
        }
    }
    // Sin archivo ni script_id: si es un script DE FABRICA (PlayerModule,
    // Menu, Health...), su codigo embebido en la extension siempre esta.
    // Cubre estructuras creadas fuera del editor (import headless, updater
    // incremental) donde nadie materializo el .lua todavia.
    if (gl_is_managed_template(get_name())) {
        Ref<LuauScript> ls; ls.instantiate();
        ls->_set_source_code(gl_builtin_template(get_name(), get_class()));
        codigo_luau = ls;
    }
}

// ════════════════════════════════════════════════════════════════════
//  COLA DE ARRANQUE: los scripts no arrancan todos en el mismo frame
//
//  Al montar un place entran todos a la vez y cada uno pedia su arranque
//  diferido: todos corrian en el mismo frame, antes del primer dibujado. En
//  Build a Mountain era un frame de 2,4 s con la ventana congelada y sin
//  pantalla de carga. Ahora van por turnos, como en Roblox:
//    0) los LocalScript de ReplicatedFirst (la pantalla de carga): en Roblox
//       son lo primero que corre en el cliente, antes de que llegue el juego;
//    1) los Script del servidor (en Roblox ya estaba en marcha cuando el
//       jugador entra);
//    2) el resto de scripts del cliente (PlayerScripts, PlayerGui, personaje).
//  Cada turno empieza en un frame nuevo (asi se dibuja la pantalla de carga
//  antes de que arranque el servidor), y dentro de un turno, si los arranques
//  de un frame pasan de GL_ARRANQUE_US, lo que queda sigue en el siguiente. El
//  orden dentro de un turno es el de siempre (el del arbol).
// ════════════════════════════════════════════════════════════════════
#define GL_ARRANQUE_US 8000

struct GLColaArranque {
    LocalVector<uint64_t> cola[3];
    uint32_t cabeza[3] = { 0, 0, 0 };
    bool pedido_ahora = false;     // drenado diferido pedido en este frame
    bool pedido_frame = false;     // drenado pedido para el frame siguiente
    uint64_t frame = UINT64_MAX;   // frame del ultimo drenado
    int turno = -1;                // turno atendido en ese frame
    uint64_t t0 = 0;               // cuando empezo a arrancar en ese frame
    uint32_t hechos = 0;           // cuantos arranco en ese frame
};
inline GLColaArranque& gl_cola_arranque() { static GLColaArranque c; return c; }

inline void gl_arranque_drenar_frame();

inline void gl_arranque_drenar() {
    GLColaArranque& c = gl_cola_arranque();
    const uint64_t frame = Engine::get_singleton()->get_process_frames();
    if (c.frame != frame) {
        c.frame = frame;
        c.turno = -1;
        c.hechos = 0;
        c.t0 = Time::get_singleton()->get_ticks_usec();
    }
    for (int t = 0; t < 3; t++) {
        if (c.cabeza[t] >= c.cola[t].size()) continue;
        if (c.turno >= 0 && c.turno != t) break;   // un turno por frame
        c.turno = t;
        while (c.cabeza[t] < c.cola[t].size()) {
            // Siempre arranca uno por frame, para avanzar aunque uno solo se
            // coma el presupuesto (el Server de un juego grande).
            if (c.hechos > 0 && Time::get_singleton()->get_ticks_usec() - c.t0 > GL_ARRANQUE_US) break;
            const uint64_t id = c.cola[t][c.cabeza[t]++];
            if (ScriptNodeBase* s = gl_cast<ScriptNodeBase>(ObjectDB::get_instance(ObjectID(id)))) {
                // GL_ARRANQUE_TRAZA=1: los arranques de mas de 15 ms (con lo
                // que hagan sus require), para saber quien se come el frame.
                static const bool traza = OS::get_singleton()->get_environment("GL_ARRANQUE_TRAZA") == "1";
                const uint64_t ts = traza ? Time::get_singleton()->get_ticks_usec() : 0;
                const String ruta = traza ? String(s->get_path()) : String();
                s->iniciar_corrutina();
                if (traza) {
                    const uint64_t us = Time::get_singleton()->get_ticks_usec() - ts;
                    if (us > 15000)
                        UtilityFunctions::print("[ARRANQUE-SCRIPT] ", (int64_t)(us / 1000), " ms  turno ", t, "  ", ruta);
                }
                c.hechos++;
            }
        }
        if (c.cabeza[t] < c.cola[t].size()) break;
        c.cola[t].clear();
        c.cabeza[t] = 0;
    }
    bool quedan = false;
    for (int t = 0; t < 3; t++) quedan = quedan || c.cabeza[t] < c.cola[t].size();
    if (!quedan || c.pedido_frame) return;
    // Lo que queda, al empezar el frame siguiente (un call_deferred correria en
    // este mismo vaciado de la cola de Godot, antes de dibujar).
    if (SceneTree* st = gl_cast<SceneTree>(Engine::get_singleton()->get_main_loop())) {
        c.pedido_frame = true;
        st->connect("process_frame", callable_mp_static(&gl_arranque_drenar_frame), Object::CONNECT_ONE_SHOT);
    }
}

inline void gl_arranque_drenar_ahora() {
    gl_cola_arranque().pedido_ahora = false;
    gl_arranque_drenar();
}

inline void gl_arranque_drenar_frame() {
    gl_cola_arranque().pedido_frame = false;
    gl_arranque_drenar();
}

// El turno de un script (ver arriba).
inline int gl_arranque_turno(ScriptNodeBase* s) {
    const int ctx = s->has_meta("RunContext") ? (int)(int64_t)s->get_meta("RunContext") : 0;
    const bool cliente = s->get_class() == "LocalScript" || ctx == 2;
    if (!cliente) return 1;
    static const StringName k_rf("ReplicatedFirst");
    for (Node* p = s->get_parent(); p; p = p->get_parent())
        if (p->get_name() == k_rf) return 0;
    return 2;
}

// Lo que antes era call_deferred("iniciar_corrutina"): a la cola de su turno.
inline void gl_arranque_pedir(ScriptNodeBase* s) {
    GLColaArranque& c = gl_cola_arranque();
    c.cola[gl_arranque_turno(s)].push_back((uint64_t)s->get_instance_id());
    if (c.pedido_ahora || c.pedido_frame) return;
    c.pedido_ahora = true;
    callable_mp_static(&gl_arranque_drenar_ahora).call_deferred();
}

inline void ScriptNodeBase::iniciar_corrutina() {
    if (Engine::get_singleton()->is_editor_hint()) return;
    // Un script FUERA del arbol no se ejecuta (en Roblox, Parent = nil = no
    // corre). Pasa de verdad: esto se llama en diferido desde ENTER_TREE y
    // el juego puede haber sacado o destruido el nodo en ese mismo frame.
    // Sin esta guarda, mas abajo se llama a get_tree() sobre un nodo suelto
    // y Godot suelta 'Parameter "data.tree" is null'.
    if (!is_inside_tree()) { _gl_mark(0, "fuera del arbol al arrancar"); return; }
    // El place se esta montando por trozos: esperar a tenerlo entero (ver
    // gl_montando_mundo en gl_stream.h). Se vuelve a intentar cada frame.
    if (gl_montando_mundo()) { call_deferred("iniciar_corrutina"); return; }
    if (get_class() == "ModuleScript") {
        // Un modulo no arranca solo: corre cuando alguien lo require. Se
        // marca aqui para que la auditoria no lo cuente como "no arranco",
        // y __gl_req_load lo pasa a "cargado" en cuanto se pide de verdad.
        if (_gl_run_state == 0) _gl_mark(5, "modulo: corre cuando alguien lo require");
        return;
    }
    // Plantillas (1.15): lo que vive dentro de un Starter* NO se ejecuta, solo
    // su clon. Sin esto corrían los dos: original + clon.
    if (gl_in_template_container(this)) {
        GL_DEBUG_PRINT("[GodotLuau] Script '", get_name(), "' no ejecutado: es una PLANTILLA (dentro de ",
                       get_parent() ? String(get_parent()->get_name()) : String("?"), "). Corre su clon.");
        _gl_mark(5, "plantilla (Starter*): corre su clon");
        return;
    }
    // ServerStorage / ReplicatedStorage guardan, no ejecutan (como Roblox).
    // Un RunContext explicito manda por encima, igual que en Roblox moderno.
    if (gl_in_storage_container(this)
        && (!has_meta("RunContext") || (int)(int64_t)get_meta("RunContext") == 0)) {
        GL_DEBUG_PRINT("[GodotLuau] Script '", get_name(), "' no ejecutado: esta GUARDADO (dentro de ",
                       get_parent() ? String(get_parent()->get_name()) : String("?"),
                       "). En Roblox los almacenes no ejecutan.");
        _gl_mark(5, "guardado en un almacen: en Roblox tampoco corre");
        return;
    }
    // Modelo server-authoritative (1.14.5): un ServerScript solo corre cuando
    // esta ventana es CONFIRMADAMENTE el servidor (gl_net_role()==1). En una
    // sesión de red (cliente O servidor cuyo rol aún se está fijando) se
    // APLAZA y lo ejecuta start_server → gl_run_deferred_server_scripts() con
    // role=1 ya puesto (así el mundo que crea SÍ se replica; y un cliente que
    // nunca es servidor no lo corre — recibe el mundo por replicación). En
    // single-player gl_startup_defer_server()==false → corre de inmediato. No
    // marca script_started para poder re-ejecutarlo al confirmarse el rol.
    if (get_class() == "ServerScript" && gl_net_role() != 1 && gl_startup_defer_server()) {
        uint64_t oid = (uint64_t)get_instance_id();
        std::vector<uint64_t>& dl = gl_deferred_server_scripts();
        bool dup = false;
        for (uint64_t x : dl) if (x == oid) { dup = true; break; }
        if (!dup) dl.push_back(oid);
        GL_DEBUG_PRINT("[GodotLuau] ServerScript '", get_name(), "' aplazado (ventana cliente; el mundo llega por replicacion).");
        _gl_mark(0, "aplazado hasta que se confirme el rol de servidor");
        return;
    }
    if (!script_enabled) {
        // Enabled=false (como Roblox): no ejecutar. Aviso en Modo Debug para
        // que "mi script no corre y no dice nada" se diagnostique solo.
        GL_DEBUG_PRINT("[GodotLuau] Script '", get_name(), "' NO ejecutado: Enabled = false.");
        _gl_mark(5, "Enabled = false");
        return;
    }
    if (script_started) return;    // ya corrio (evita doble arranque al re-habilitar)
    script_started = true;

    if (codigo_luau.is_null()) gl_rescue_codigo_luau();
    if (codigo_luau.is_null()) {
        UtilityFunctions::push_error("[GodotLuau] ", gl_tr3("Node '", "El nodo '", "O nó '"), get_name(), gl_tr3(
            "' has no Luau script assigned.",
            "' no tiene un archivo Luau asignado.",
            "' não tem um script Luau atribuído."));
        _gl_mark(3, "sin archivo Luau asignado");
        return;
    }

    // ── Contexto (F8): una VM por (juego, servidor/cliente) ───────────────
    // RunContext de Roblox moderno: un Script con RunContext=Client corre como
    // si fuera un LocalScript, este donde este (ReplicatedStorage, Workspace...).
    // El importador lo deja como meta; sin esto esos scripts corrian como
    // servidor y Players.LocalPlayer les salia nil.
    // Enum.RunContext: Legacy=0, Server=1, Client=2, Plugin=3.
    const int _run_ctx = has_meta("RunContext") ? (int)(int64_t)get_meta("RunContext") : 0;
    const bool _is_client = (get_class() == "LocalScript") || _run_ctx == 2;

    Node* game_node = nullptr;
    Node* ws_node = nullptr;
    // ── Buscar game y workspace ───────────────────────────────────────────
    {
        SceneTree* _st2 = get_tree();
        Node* root = _st2 ? (Node*)_st2->get_root() : nullptr;
        if (!root) { _gl_mark(3, "sin arbol al arrancar"); return; }
        // Buscar nodo "game": RobloxTemplate, RobloxGame3D, RobloxGame2D,
        // cualquier nodo llamado "Game", o la raiz de un place importado
        // (el importador la marca con meta __rbx_class="DataModel"; sin esta
        // rama `game` caia en el Window raiz y game.PlaceId & cia. morian
        // con "PlaceId is not a valid member of Window" — el DataService de
        // un place real revienta al cargar y arrastra leaderstats, niveles
        // de base, teleports... todo lo que dependa de datos).
        auto _es_game = [](Node* nd) -> bool {
            if (!nd) return false;
            if (nd->is_class("RobloxTemplate") || nd->is_class("RobloxGame3D") ||
                nd->is_class("RobloxGame2D")   || nd->get_name() == StringName("Game")) return true;
            return nd->has_meta("__rbx_class") &&
                   String(nd->get_meta("__rbx_class")) == "DataModel";
        };
        for (int _i = 0; _i < root->get_child_count() && !game_node; _i++) {
            Node* ch = root->get_child(_i);
            if (_es_game(ch)) {
                game_node = ch;
            } else if (ch) {
                for (int _j = 0; _j < ch->get_child_count() && !game_node; _j++) {
                    Node* gc = ch->get_child(_j);
                    if (_es_game(gc)) game_node = gc;
                }
            }
        }
        if (!game_node) game_node = root;
        ws_node = game_node->get_node_or_null("Workspace");
        if (!ws_node) ws_node = (Node*)get_viewport();
    }

    GLContexto* _ctx = gl_contexto_buscar((uint64_t)game_node->get_instance_id(), _is_client);
    if (!_ctx) {
        _ctx = gl_contexto_nuevo((uint64_t)game_node->get_instance_id(), _is_client);
        // Dueño de las tareas del propio stdlib (bucles de sondeo, el paso de
        // los tweens, Debris...): un nodo interno que vive lo que el juego. Si
        // fueran del primer script que las pide, al morir ese script (el de un
        // personaje que reaparece) se pararian para todos.
        ScriptNodeBase* _sis = gl_cast<ScriptNodeBase>(ClassDB::instantiate("ModuleScript"));
        if (_sis) {
            _sis->set_name(_is_client ? "GLSistemaCliente" : "GLSistemaServidor");
            _sis->set_meta("_gl_interno", true);
            _sis->gl_hacer_sistema(_ctx);
            _ctx->sistema->nodo_id = (uint64_t)_sis->get_instance_id();
            game_node->add_child(_sis, false, Node::INTERNAL_MODE_BACK);
        }
        _gl_montar_contexto(_ctx->L, _is_client, game_node, ws_node);
        // Globales comunes "seguros": los imports se resuelven al cargar y las
        // funciones de la libreria van por el camino rapido (como Roblox).
        lua_setsafeenv(_ctx->L, LUA_GLOBALSINDEX, true);
    }
    _gl_ctx_id = _ctx->id;
    L_main = gl_contexto_hilo_script(_ctx, (uint64_t)get_instance_id());
    relays.clear();              // refs de una ejecucion anterior no valen en esta
    main_start_args = 0;
    main_resched = false;
    // `script` es de cada script: va en SU entorno, no en los globales comunes.
    wrap_node(L_main, this);
    lua_setglobal(L_main, "script");

    // ── Compilar y ejecutar el script ─────────────────────────────────────
    L_thread = lua_newthread(L_main);
    String source = codigo_luau->_get_source_code();

    // CON cache, no sin ella. Un place clona el mismo modelo 42 veces por
    // oleada y cada copia trae los MISMOS scripts: se recompilaba el mismo
    // texto 42 veces en un frame. La cache es por texto fuente, asi que dos
    // scripts identicos comparten bytecode y la copia 2..42 sale gratis.
    // (La referencia es estable: unordered_map no invalida al insertar.)
    const std::string& bytecode = gl_compile_cached(source.utf8().get_data());

    // Chunkname = ruta completa estilo Roblox ("ServerScriptService.Server"):
    // asi los errores salen como alla ("ServerScriptService.Server:5: ...").
    // El prefijo '=' evita que Luau lo envuelva en [string "..."].
    String chunkname = String("=") + gl_instance_fullname(this);
    if (luau_load(L_thread, chunkname.utf8().get_data(), bytecode.data(), bytecode.size(), 0) == 0) {
        gl_nativo(L_thread);
        _gl_mark(1, "corriendo");
        resume();
    } else {
        // Error de sintaxis: linea roja identica a Roblox, sin stack.
        _gl_mark(3, String("error de sintaxis: ") + String::utf8(lua_tostring(L_thread, -1)));
        gl_report_error_line(String::utf8(lua_tostring(L_thread, -1)));
    }
}

inline void ScriptNodeBase::setup_require_system(lua_State* L) {
    lua_pushcfunction(L, [](lua_State* L) -> int {
        // Mensajes EXACTOS de Roblox en cada caso de require().
        GodotObjectWrapper* wrapper = (GodotObjectWrapper*)lua_touserdata(L, 1);
        if (!wrapper || !gow_node(wrapper)) {
            luaL_error(L, "Attempted to call require with invalid argument(s).");
            return 0;
        }

        Node* mod_node = gow_node(wrapper);
        if (mod_node->get_class() != "ModuleScript") {
            luaL_error(L, "Attempted to call require with invalid argument(s).");
            return 0;
        }

        String cache_id = "CACHED_MOD_" + String::num(mod_node->get_instance_id());
        lua_getfield(L, LUA_REGISTRYINDEX, cache_id.utf8().get_data());
        if (!lua_isnil(L, -1)) { lua_pushboolean(L, 1); lua_insert(L, -2); return 2; }   // (true, valorCacheado)
        lua_pop(L, 1);

        ScriptNodeBase* mod_ptr = gl_cast<ScriptNodeBase>(mod_node);
        if (!mod_ptr) { luaL_error(L, "Attempted to call require with invalid argument(s)."); return 0; }

        Ref<LuauScript> res = mod_ptr->get_codigo_luau();
        if (res.is_null()) {
            // Mismo rescate por script_id que al arrancar un Script.
            mod_ptr->gl_rescue_codigo_luau();
            res = mod_ptr->get_codigo_luau();
        }
        if (res.is_null()) {
            // Modulo sin codigo = modulo que no devuelve nada (como en Roblox)
            luaL_error(L, "Module code did not return exactly one value");
            return 0;
        }

        // Cacheado por texto: el mismo modulo en los dos contextos (servidor y
        // cliente) se compila una vez.
        const std::string& bc = gl_compile_cached(res->_get_source_code().utf8().get_data());
        // Auditoria: deja constancia de que ALGUIEN pidio este modulo.
        if (mod_ptr) mod_ptr->_gl_mark(1, "requerido al menos una vez");
        String mod_chunkname = String("=") + gl_instance_fullname(mod_node);

        // El modulo tiene SU entorno, como en Roblox: su `script` y sus
        // globales son suyos; lo que no tiene lo lee de los globales comunes.
        // (Antes escribia sus globales en el entorno del script que lo pedia.)
        // Se carga en un hilo cuyo entorno ES ese: Luau resuelve los imports
        // con el entorno del hilo que carga, no con el del argumento.
        lua_State* T = lua_newthread(L);                  // [.. T]
        lua_newtable(T);                                   // entorno del modulo
        wrap_node(T, mod_node);
        lua_setfield(T, -2, "script");
        lua_newtable(T);                                   // su metatabla
        lua_getfield(T, LUA_REGISTRYINDEX, "GL_GLOBALES");
        lua_setfield(T, -2, "__index");
        lua_setreadonly(T, -1, true);
        lua_setmetatable(T, -2);
        lua_setsafeenv(T, -1, true);
        lua_replace(T, LUA_GLOBALSINDEX);
        if (luau_load(T, mod_chunkname.utf8().get_data(), bc.data(), bc.size(), 0) != 0) {
            // Como Roblox: se imprime el error de sintaxis del modulo (rojo) y
            // el require lanza su error generico en la linea del caller.
            gl_report_error_line(String::utf8(lua_tostring(T, -1)));
            lua_pop(L, 1);
            luaL_error(L, "Requested module experienced an error while loading");
            return 0;
        }
        gl_nativo(T);
        lua_xmove(T, L, 1);                               // [.. T moduleFn]
        lua_remove(L, -2);                                // [.. moduleFn]

        // La EJECUCIÓN del cuerpo del módulo la hace el wrapper Lua `require`
        // con pcall (yieldable en Luau) — aquí solo devolvemos la función
        // cargada. Correrla desde C con lua_pcall rompía cualquier yield
        // interno (WaitForChild/task.wait) con "yield across C-call boundary".
        // stack: [target, moduleFn]
        lua_pushboolean(L, 0);                            // isCached = false
        lua_insert(L, -2);                                // [target, false, moduleFn]
        lua_pushstring(L, cache_id.utf8().get_data());    // cacheKey
        wrap_node(L, mod_node);                           // modScript (para el 'script' del módulo)
        return 4;                                         // (false, moduleFn, cacheKey, modScript)
    }, "__gl_req_load");
    lua_setglobal(L, "__gl_req_load");

    // Guardar el valor devuelto por un módulo en la caché de la VM.
    lua_pushcfunction(L, [](lua_State* L) -> int {
        const char* key = luaL_checkstring(L, 1);
        lua_pushvalue(L, 2);
        lua_setfield(L, LUA_REGISTRYINDEX, key);
        return 0;
    }, "__gl_req_store");
    lua_setglobal(L, "__gl_req_store");

    // Reportar en rojo (como Roblox) el error de un módulo, con su ubicación.
    lua_pushcfunction(L, [](lua_State* L) -> int {
        gl_report_error_line(String::utf8(luaL_optstring(L, 1, "")));
        return 0;
    }, "__gl_req_err");
    lua_setglobal(L, "__gl_req_err");

    // `require` en LUA: corre el cuerpo del módulo con pcall (yieldable), así
    // WaitForChild/task.wait DENTRO de un módulo ceden como corrutina normal.
    // El `script` del módulo va en su ENTORNO, no en una global (1.14.15).
    // Antes se ponía la global `script`, se corría el cuerpo y se restauraba:
    // si el módulo CEDÍA (WaitForChild/task.wait dentro), otra corrutina la
    // pisaba y el módulo despertaba con el `script` de OTRO.
    // Y `_loading` arregla algo peor: un módulo que cede podía ser pedido a la
    // vez por otra corrutina y, como la caché se escribe al TERMINAR, su cuerpo
    // corría DOS veces → dos estados distintos del mismo módulo. Ahora el
    // segundo espera al primero, y pedirse a sí mismo es error, como Roblox.
    static const char* GL_REQUIRE_LUA =
        "local _load, _store, _err = __gl_req_load, __gl_req_store, __gl_req_err\n"
        "__gl_req_load, __gl_req_store, __gl_req_err = nil, nil, nil\n"
        "local _loading = {}\n"
        "function require(target)\n"
        "  local cached, fn, key, mod = _load(target)\n"
        "  if cached then return fn end\n"
        "  local owner = _loading[key]\n"
        "  if owner ~= nil then\n"
        "    if owner == (coroutine.running() or 'main') then\n"
        "      error('Requested module was required recursively', 0)\n"
        "    end\n"
        "    while _loading[key] ~= nil do task.wait() end\n"
        "    local c2, f2 = _load(target)\n"
        "    if c2 then return f2 end\n"
        "    error('Requested module experienced an error while loading', 0)\n"
        "  end\n"
        "  _loading[key] = coroutine.running() or 'main'\n"
        "  local ok, result = pcall(fn)\n"
        "  _loading[key] = nil\n"
        "  if not ok then _err(tostring(result)) error('Requested module experienced an error while loading', 0) end\n"
        "  if result == nil then error('Module code did not return exactly one value', 0) end\n"
        "  _store(key, result)\n"
        "  return result\n"
        "end\n";
    const std::string& _rbc = gl_compile_cached(GL_REQUIRE_LUA);
    if (luau_load(L, "=__gl_require", _rbc.data(), _rbc.size(), 0) == 0) {
        gl_nativo(L);
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) { UtilityFunctions::print("[GodotLuau require setup] ", lua_tostring(L, -1)); lua_pop(L, 1); }
    } else { UtilityFunctions::print("[GodotLuau require syntax] ", lua_tostring(L, -1)); lua_pop(L, 1); }
}

// RemoteFunction por red (1.14.6): corre el handler (OnServerInvoke/
// OnClientInvoke) en la máquina remota con soporte de yield y devuelve el
// resultado al invocador. El invocador cede su hilo (add_waiting_invoke) y
// _process lo reanuda con la respuesta (gl_net_responses).
inline void ScriptNodeBase::setup_remotefunction_system(lua_State* L) {
    // C: tras correr el handler, empaqueta sus retornos y pide al
    // NetworkService que los mande al invocador. (callId, replyPeer, ok, ...rets)
    lua_pushcfunction(L, [](lua_State* L) -> int {
        int64_t call_id = (int64_t)luaL_checknumber(L, 1);
        int     reply_peer = (int)luaL_checknumber(L, 2);
        bool    ok = lua_toboolean(L, 3) != 0;
        Array rets;
        int top = lua_gettop(L);
        for (int i = 4; i <= top; i++) {
            gl_net_encode_err() = String();
            Variant rv = gl_net_encode(L, i);
            if (!gl_net_encode_err().is_empty()) {
                // El handler devolvió algo no serializable: en vez de mandar nil
                // en silencio, el invocador recibe el error.
                String what = gl_net_encode_err();
                gl_net_encode_err() = String();
                ok = false;
                rets = Array();
                rets.push_back(String("RemoteFunction handler returned a value that cannot be sent over the network (" + what + ")"));
                break;
            }
            rets.push_back(rv);
        }
        if (Node* ns = gl_cast<Node>(ObjectDB::get_instance(gl_net_service_id())))
            ns->call("net_rf_respond", reply_peer, (int)call_id, ok, rets);
        return 0;
    }, "__gl_rf_respond");
    lua_setglobal(L, "__gl_rf_respond");
    // Invocaciones locales (gl_invocar.h): guarda lo que devolvio el manejador.
    lua_pushcfunction(L, gl_inv_fin, "__gl_inv_fin");
    lua_setglobal(L, "__gl_inv_fin");

    // Lua: corre el handler en un task.spawn (así puede ceder: WaitForChild,
    // task.wait, DataStore...) y al terminar responde. isServer decide si el
    // handler recibe el Player (OnServerInvoke(player,...) vs OnClientInvoke(...)).
    static const char* GL_RF_LUA =
        "function __gl_rf_serve(handler, player, callId, replyPeer, isServer, ...)\n"
        "  local args = table.pack(...)\n"
        "  task.spawn(function()\n"
        "    local packed\n"
        "    if isServer then packed = table.pack(pcall(handler, player, table.unpack(args, 1, args.n)))\n"
        "    else packed = table.pack(pcall(handler, table.unpack(args, 1, args.n))) end\n"
        "    if not packed[1] then warn('RemoteFunction handler error: ' .. tostring(packed[2])) end\n"
        "    __gl_rf_respond(callId, replyPeer, table.unpack(packed, 1, packed.n))\n"
        "  end)\n"
        "end\n"
        // Local: ya corre como tarea (gl_invocar_local), sin task.spawn.
        "function __gl_inv_local(callId, handler, ...)\n"
        "  __gl_inv_fin(callId, pcall(handler, ...))\n"
        "end\n";
    const std::string& bc = gl_compile_cached(GL_RF_LUA);
    if (luau_load(L, "=__gl_rf", bc.data(), bc.size(), 0) == 0) {
        gl_nativo(L);
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) { UtilityFunctions::print("[GodotLuau rf setup] ", lua_tostring(L, -1)); lua_pop(L, 1); }
    } else { UtilityFunctions::print("[GodotLuau rf syntax] ", lua_tostring(L, -1)); lua_pop(L, 1); }
}

#endif // LUAU_SCRIPT_ARRANQUE_H

#ifndef LUAU_SCRIPT_CONTEXTO_H
#define LUAU_SCRIPT_CONTEXTO_H
#include "gl_cast.h"

// Metodos de ScriptNodeBase: montar la VM de un CONTEXTO (F8). Todos los
// scripts de un juego que corren en el mismo lado (servidor o cliente)
// comparten esta VM, como en Roblox; cada uno es un hilo con su entorno
// (ver gl_contexto.h). Aqui va todo lo que es comun: metatablas, globales,
// el stdlib y el puente. Lo de cada script (su `script`, su hilo) lo pone
// iniciar_corrutina.
//
// OJO: el parametro se llama L_main a proposito (es el codigo que antes
// montaba la VM de cada script); aqui es el hilo principal del contexto. Nada
// de este archivo puede usar `this`: lo monta el primer script que llega, pero
// es de todos.
// ── Dueños de las tareas ─────────────────────────────────────────────────
// Las señales hechas en Luau (las del stdlib) apuntan QUIEN conecto cada
// manejador y lo corren como tarea de ese script; si ese script ya murio la
// conexion cae, como en Roblox (destruir un script desconecta lo suyo). Con
// una VM por script esto venia solo: cada script tenia su copia del servicio.
// Y los bucles de fondo del stdlib (sondeos, el paso de los tweens, Debris)
// corren como tareas del SISTEMA del contexto, no del primer script que los
// pidio: si no, al morir ese script se pararian para todos.
//   __gl_dueno()                    -> ficha del script que corre (o nil)
//   __gl_dueno_vivo(ficha)          -> bool
//   __gl_spawn_dueno(ficha, f, ...) -> corre f como tarea de ese script; nil si ya murio
//   __gl_sistema(f, ...)            -> corre f como tarea del sistema del contexto

// Corre la funcion de la posicion `idx_f` (con lo que haya detras) como tarea
// del script `h`. Deja el hilo en la pila y devuelve 1, o 0 si `h` ya murio.
inline int gl_spawn_como(lua_State* L, GLHilo* h, int idx_f) {
    ScriptNodeBase* sn = nullptr;
    if (h && h->vivo && h->nodo_id)
        sn = gl_cast<ScriptNodeBase>(ObjectDB::get_instance(ObjectID(h->nodo_id)));
    if (!sn) return 0;
    luaL_checktype(L, idx_f, LUA_TFUNCTION);
    const int top = lua_gettop(L);
    const int nargs = top - idx_f;
    lua_State* NL = lua_newthread(h->principal);   // hereda su entorno y su dueño
    lua_xmove(h->principal, L, 1);                 // el hilo queda anclado en la pila de L
    for (int i = idx_f; i <= top; i++) lua_pushvalue(L, i);
    lua_xmove(L, NL, nargs + 1);
    sn->gl_start_task(NL, nargs);
    return 1;
}

inline void gl_registrar_duenos(lua_State* L) {
    lua_pushcfunction(L, [](lua_State* L) -> int {
        GLHilo* h = gl_hilo_de(L);
        if (!h) { lua_pushnil(L); return 1; }
        lua_pushlightuserdata(L, h);
        return 1;
    }, "__gl_dueno");
    lua_setglobal(L, "__gl_dueno");

    lua_pushcfunction(L, [](lua_State* L) -> int {
        GLHilo* h = static_cast<GLHilo*>(lua_tolightuserdata(L, 1));
        lua_pushboolean(L, (h && h->vivo && gl_nodo_script_de(h->principal)) ? 1 : 0);
        return 1;
    }, "__gl_dueno_vivo");
    lua_setglobal(L, "__gl_dueno_vivo");

    lua_pushcfunction(L, [](lua_State* L) -> int {
        return gl_spawn_como(L, static_cast<GLHilo*>(lua_tolightuserdata(L, 1)), 2);
    }, "__gl_spawn_dueno");
    lua_setglobal(L, "__gl_spawn_dueno");

    lua_pushcfunction(L, [](lua_State* L) -> int {
        GLContexto* c = gl_contexto_de(L);
        return gl_spawn_como(L, c ? c->sistema : nullptr, 1);
    }, "__gl_sistema");
    lua_setglobal(L, "__gl_sistema");
}

// Propiedades, metodos y eventos que en Roblox tiene TODA instancia: con estas
// claves gana siempre la propiedad aunque haya un hijo con ese nombre, asi que
// el __index no busca el hijo. Esa busqueda (un cruce a Godot con su NodePath
// en cada lectura) era lo mas caro de leer IsA, Name o Parent.
inline bool gl_clave_de_instancia(const char* k) {
    switch (k[0]) {
        case 'N': return strcmp(k, "Name") == 0;
        case 'P': return strcmp(k, "Parent") == 0;
        case 'H': return strcmp(k, "HasTag") == 0;
        case 'S': return strcmp(k, "SetAttribute") == 0;
        case 'R': return strcmp(k, "RemoveTag") == 0;
        case 'I': return strcmp(k, "IsA") == 0 || strcmp(k, "IsDescendantOf") == 0 || strcmp(k, "IsAncestorOf") == 0;
        case 'C': return strcmp(k, "ClassName") == 0 || strcmp(k, "Clone") == 0 || strcmp(k, "Changed") == 0 ||
                         strcmp(k, "ChildAdded") == 0 || strcmp(k, "ChildRemoved") == 0 ||
                         strcmp(k, "ClearAllChildren") == 0;
        case 'D': return strcmp(k, "Destroy") == 0 || strcmp(k, "Destroying") == 0 ||
                         strcmp(k, "DescendantAdded") == 0 || strcmp(k, "DescendantRemoving") == 0;
        case 'A': return strcmp(k, "AncestryChanged") == 0 || strcmp(k, "AttributeChanged") == 0 ||
                         strcmp(k, "AddTag") == 0 || strcmp(k, "Archivable") == 0;
        case 'G': return strcmp(k, "GetAttribute") == 0 || strcmp(k, "GetAttributes") == 0 ||
                         strcmp(k, "GetDescendants") == 0 || strcmp(k, "GetFullName") == 0 ||
                         strcmp(k, "GetTags") == 0 || strcmp(k, "GetPropertyChangedSignal") == 0 ||
                         strcmp(k, "GetAttributeChangedSignal") == 0;
        case 'F': return strcmp(k, "FindFirstChildOfClass") == 0 || strcmp(k, "FindFirstChildWhichIsA") == 0 ||
                         strcmp(k, "FindFirstAncestor") == 0 || strcmp(k, "FindFirstAncestorOfClass") == 0 ||
                         strcmp(k, "FindFirstAncestorWhichIsA") == 0 || strcmp(k, "FindFirstDescendant") == 0;
        default: return false;
    }
}

inline void ScriptNodeBase::_gl_montar_contexto(lua_State* L_main, bool es_cliente, Node* game_node, Node* ws_node) {
    luaL_openlibs(L_main);

    // ── Contexto cliente/servidor ─────────────────────────────────────────
    lua_pushboolean(L_main, es_cliente ? 1 : 0);
    lua_setglobal(L_main, "__IS_CLIENT");
    lua_pushboolean(L_main, es_cliente ? 0 : 1);
    lua_setglobal(L_main, "__IS_SERVER");

    // ── Metatable: GodotObject ────────────────────────────────────────────
    luaL_newmetatable(L_main, "GodotObject");

    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        GodotObjectWrapper* ptr = (GodotObjectWrapper*)lua_touserdata(L, 1);
        const char* key = luaL_checkstring(L, 2);

        if (ptr && gow_node(ptr)) {
            Node* node = gow_node(ptr);

            if (strcmp(key, "FindFirstChild") == 0) {
                lua_pushcfunction(L, [](lua_State* pL) -> int {
                    GodotObjectWrapper* pptr = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                    if (!pptr || !gow_node(pptr)) { lua_pushnil(pL); return 1; }
                    const char* cname = luaL_checkstring(pL, 2);
                    bool recursive = lua_isboolean(pL, 3) && lua_toboolean(pL, 3);
                    if (!recursive) {
                        // El StringName y el nodo se sacan FUERA del bucle:
                        // construir un StringName toma el mutex global de
                        // internado, y estaba dentro. Sobre un contenedor de
                        // 5.000 hijos eran 5.000 internados + 10.000 consultas
                        // a ObjectDB para buscar UN hijo.
                        const StringName buscado(cname);
                        Node* padre = gow_node(pptr);
                        const int n_hijos = padre->get_child_count();
                        for (int i = 0; i < n_hijos; i++) {
                            Node* ch = padre->get_child(i);
                            // gl_rbx_llamado y no get_name(): Godot renombra a los hermanos con nombre
                            // repetido (@RBXModel@N) y guarda el real en la meta __rbx_name. Comparando
                            // el de Godot esos nodos eran INVISIBLES por nombre desde Lua (221 grupos de
                            // hermanos repetidos en el place de prueba).
                            if (ch && gl_rbx_llamado(ch, buscado)) { wrap_node(pL, ch); return 1; }
                        }
                    } else {
                        // BFS propio: find_child() de Godot compara el nombre de
                        // Godot y se salta los renombrados (ver arriba).
                        const StringName buscado(cname);
                        std::vector<Node*> cola;
                        Node* raiz = gow_node(pptr);
                        for (int i = 0; i < raiz->get_child_count(); i++) cola.push_back(raiz->get_child(i));
                        for (size_t qi = 0; qi < cola.size(); qi++) {
                            Node* ch = cola[qi];
                            if (!ch) continue;
                            if (gl_rbx_llamado(ch, buscado)) { wrap_node(pL, ch); return 1; }
                            for (int i = 0; i < ch->get_child_count(); i++) cola.push_back(ch->get_child(i));
                        }
                    }
                    lua_pushnil(pL); return 1;
                }, "FindFirstChild");
                return 1;
            }
            if (strcmp(key, "WaitForChild") == 0) {
                lua_pushlightuserdata(L, (void*)node);
                lua_pushcclosure(L, [](lua_State* pL) -> int {
                    Node* parent = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
                    if (!parent) { lua_pushnil(pL); return 1; }
                    const char* cname = luaL_checkstring(pL, 2);
                    double timeout = lua_isnumber(pL, 3) ? lua_tonumber(pL, 3) : -1.0;
                    // Try immediate find (mismo motivo que en FindFirstChild:
                    // el StringName fuera del bucle)
                    const StringName buscado(cname);
                    const int n_hijos = parent->get_child_count();
                    for (int i = 0; i < n_hijos; i++) {
                        Node* ch = parent->get_child(i);
                        // gl_rbx_llamado y no get_name(): Godot renombra a los hermanos con nombre
                        // repetido (@RBXModel@N) y guarda el real en la meta __rbx_name. Comparando
                        // el de Godot esos nodos eran INVISIBLES por nombre desde Lua (221 grupos de
                        // hermanos repetidos en el place de prueba).
                        if (ch && gl_rbx_llamado(ch, buscado)) { wrap_node(pL, ch); return 1; }
                    }
                    // Not found — register async wait
                    ScriptNodeBase* sn = gl_script_de(pL);
                    if (!sn) { lua_pushnil(pL); return 1; }
                    lua_State* main_L = gl_main_of(pL);
                    if (!main_L) main_L = pL;
                    lua_pushthread(pL);
                    int ref = gl_ref(pL, -1);
                    lua_pop(pL, 1);
                    sn->add_waiting_child(pL, main_L, ref, parent, String(cname), timeout);
                    return gl_yield_externo(pL, "__WAIT_CHILD__");
                }, "WaitForChild", 1);
                return 1;
            }

            if (strcmp(key, "GetChildren") == 0) {
                lua_pushcfunction(L, [](lua_State* pL) -> int {
                    GodotObjectWrapper* pptr = (GodotObjectWrapper*)lua_touserdata(pL, 1);
                    lua_newtable(pL);
                    if (pptr && gow_node(pptr)) {
                        TypedArray<Node> children = gow_node(pptr)->get_children();
                        for (int i = 0; i < children.size(); i++) {
                            wrap_node(pL, gl_cast<Node>(children[i]));
                            lua_rawseti(pL, -2, i + 1);
                        }
                    }
                    return 1;
                }, "GetChildren");
                return 1;
            }

            // workspace.CurrentCamera es la camara ACTIVA (la del personaje);
            // el hijo "CurrentCamera" del Workspace es la plantilla del
            // editor y no dibuja. Lo resuelve el puente.
            // Despues, un hijo con ese nombre; no si es una clave que tiene toda
            // instancia (gl_clave_de_instancia) ni si el nodo no tiene hijos
            // visibles. El nombre, en UTF-8 (con Latin-1 no se encontraba
            // `workspace.Montaña`).
            if (strcmp(key, "CurrentCamera") == 0 || gl_clave_de_instancia(key) || node->get_child_count() == 0)
                return godot_object_index(L);
            Node* child = node->get_node_or_null(gl_ruta_de_nombre(String::utf8(key)));
            if (child) { wrap_node(L, child); return 1; }
        }

        return godot_object_index(L);
    }, "__index");
    lua_setfield(L_main, -2, "__index");

    lua_pushcfunction(L_main, godot_object_newindex, "__newindex");
    lua_setfield(L_main, -2, "__newindex");

    // ── __eq (1.14.17) ────────────────────────────────────────────────────
    // Cada acceso a una propiedad crea un envoltorio NUEVO, asi que sin esto
    // Lua comparaba los userdata y dos referencias al MISMO nodo daban
    // siempre false: `hit.Parent == player.Character`, `p.Character == x`…
    // El idioma mas comun de Roblox no entraba NUNCA, y en silencio.
    // Se comparan los ObjectID, no los punteros: asi dos referencias al mismo
    // nodo son iguales aunque sean envoltorios distintos, y sigue funcionando
    // con nodos ya destruidos (mismo id = misma instancia).
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        GodotObjectWrapper* a = (GodotObjectWrapper*)lua_touserdata(L, 1);
        GodotObjectWrapper* b = (GodotObjectWrapper*)lua_touserdata(L, 2);
        lua_pushboolean(L, (a && b && a->obj_id == b->obj_id) ? 1 : 0);
        return 1;
    }, "__eq");
    lua_setfield(L_main, -2, "__eq");

    // __tostring: en Roblox imprimir una instancia da su Nombre; antes salia
    // "userdata: 0x…", que ademas hacia irreconocibles los prints de depuracion.
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
        Node* n = w ? gow_node(w) : nullptr;
        lua_pushstring(L, n ? String(n->get_name()).utf8().get_data() : "NULL");
        return 1;
    }, "__tostring");
    lua_setfield(L_main, -2, "__tostring");

    lua_pop(L_main, 1);

    // Replicación (1.14.5): instala el aplicador de propiedades del cliente
    // (reentra al __newindex real). Idempotente; el NetworkService lo llama
    // por puntero al recibir _rep_new/_rep_prop.
    gl_apply_prop_hook() = gl_rep_apply_prop;
    gl_rep_tag_hook() = gl_rep_on_tag;   // AddTag/RemoveTag del servidor (gl_replica.h)
    // Hook para que InvokeServer/Client (en luau_api.h, sin ver ScriptNodeBase)
    // registre el hilo que cede esperando la respuesta por red.
    gl_add_invoke_wait_hook() = [](Node* n, lua_State* th, lua_State* mL, int ref, int64_t cid, double to) {
        if (ScriptNodeBase* sn = gl_cast<ScriptNodeBase>(n))
            sn->add_waiting_invoke(th, mL, ref, cid, to);
    };

    // ── Math metatables ───────────────────────────────────────────────────
    //// ── Metatables matemáticas ────────────────────────────────────────────
    luaL_newmetatable(L_main, "Vector3");
    lua_pushcfunction(L_main, godot_vector3_index, "__index");   lua_setfield(L_main, -2, "__index");
    lua_pushcfunction(L_main, godot_vector3_add,   "__add");     lua_setfield(L_main, -2, "__add");
    lua_pushcfunction(L_main, godot_vector3_sub,   "__sub");     lua_setfield(L_main, -2, "__sub");
    lua_pushcfunction(L_main, godot_vector3_mul,   "__mul");     lua_setfield(L_main, -2, "__mul");
    lua_pushcfunction(L_main, godot_vector3_div,   "__div");     lua_setfield(L_main, -2, "__div");
    lua_pushcfunction(L_main, godot_vector3_unm,   "__unm");     lua_setfield(L_main, -2, "__unm");
    lua_pushcfunction(L_main, godot_vector3_eq,    "__eq");      lua_setfield(L_main, -2, "__eq");
    lua_pushcfunction(L_main, godot_vector3_tostring, "__tostring"); lua_setfield(L_main, -2, "__tostring");
    lua_pop(L_main, 1);

    luaL_newmetatable(L_main, "Color3");
    lua_pushcfunction(L_main, godot_color3_index, "__index"); lua_setfield(L_main, -2, "__index");
    lua_pushcfunction(L_main, godot_color3_eq,    "__eq");    lua_setfield(L_main, -2, "__eq");
    lua_pushcfunction(L_main, godot_color3_tostring, "__tostring"); lua_setfield(L_main, -2, "__tostring");
    lua_pop(L_main, 1);

    // game y workspace del contexto (el nodo lo busca iniciar_corrutina)
    wrap_node(L_main, game_node); lua_setglobal(L_main, "game");
    wrap_node(L_main, ws_node);   lua_setglobal(L_main, "workspace");


    // ── Instance ──────────────────────────────────────────────────────────
    lua_newtable(L_main);
    lua_pushcfunction(L_main, godot_instance_new, "new");
    lua_setfield(L_main, -2, "new");
    lua_setglobal(L_main, "Instance");

    // ── Vector3 ───────────────────────────────────────────────────────────
    lua_newtable(L_main);
    lua_pushcfunction(L_main, godot_vector3_new,  "new");    lua_setfield(L_main, -2, "new");
    {
        LuauVector3* z = (LuauVector3*)lua_newuserdata(L_main, sizeof(LuauVector3));
        z->x = 0; z->y = 0; z->z = 0;
        luaL_getmetatable(L_main, "Vector3"); lua_setmetatable(L_main, -2);
        lua_setfield(L_main, -2, "zero");

        LuauVector3* o = (LuauVector3*)lua_newuserdata(L_main, sizeof(LuauVector3));
        o->x = 1; o->y = 1; o->z = 1;
        luaL_getmetatable(L_main, "Vector3"); lua_setmetatable(L_main, -2);
        lua_setfield(L_main, -2, "one");
    }
    lua_setglobal(L_main, "Vector3");

    // ── Color3 ────────────────────────────────────────────────────────────
    lua_newtable(L_main);
    lua_pushcfunction(L_main, godot_color3_new,      "new");     lua_setfield(L_main, -2, "new");
    lua_pushcfunction(L_main, godot_color3_from_rgb, "fromRGB"); lua_setfield(L_main, -2, "fromRGB");
    lua_setglobal(L_main, "Color3");

    // ── task ─────────────────────────────────────────────────────────────
    lua_newtable(L_main);

    lua_pushcfunction(L_main, _gl_task_wait, "wait");
    lua_setfield(L_main, -2, "wait");

    // task.spawn(f o hilo, ...) — lo corre YA; devuelve el hilo (como Roblox).
    // Con un hilo es la forma de reanudar uno que cedio esperando algo (es
    // como los :Wait() hechos en Luau devuelven el control).
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        ScriptNodeBase* self = gl_script_de(L);
        if (!self) return 0;
        if (lua_isthread(L, 1)) {
            lua_State* co = lua_tothread(L, 1);
            if (co == L || lua_costatus(L, co) != LUA_COSUS) {
                luaL_error(L, "cannot resume %s coroutine", co == L ? "non-suspended" : "dead");
                return 0;
            }
            const int n = _gl_args_a_hilo(L, co, 2);
            if (!self->gl_hilo_gestionado(co)) self->gl_adoptar_hilo(co, -1.0);
            self->_antes_de_reanudar(co);
            self->_after_resume(co, lua_resume(co, L, n));
            lua_settop(L, 1);
            return 1;
        }
        luaL_checktype(L, 1, LUA_TFUNCTION);

        int base = lua_gettop(L);                 // [func(1), args(2..base)]
        lua_State* NL = lua_newthread(L);          // NL en la cima
        int nl_idx = lua_gettop(L);
        lua_pushvalue(L, 1); lua_xmove(L, NL, 1);  // función → hilo
        int n_args = base - 1;
        for (int i = 2; i <= base; i++) lua_pushvalue(L, i);
        if (n_args > 0) lua_xmove(L, NL, n_args);
        self->gl_start_task(NL, n_args);
        lua_settop(L, nl_idx);                     // devolver el hilo
        return 1;
    }, "spawn");
    lua_setfield(L_main, -2, "spawn");

    // task.delay(n, f o hilo, ...) — lo corre tras n segundos; devuelve el hilo
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        ScriptNodeBase* self = gl_script_de(L);
        if (!self) return 0;
        double delay_time = luaL_checknumber(L, 1);
        if (lua_isthread(L, 2)) return self->_gl_reprogramar_hilo(L, lua_tothread(L, 2), 3, delay_time);
        luaL_checktype(L, 2, LUA_TFUNCTION);

        int base = lua_gettop(L);                 // [delay(1), func(2), args(3..base)]
        lua_State* NL = lua_newthread(L);
        int nl_idx = lua_gettop(L);
        lua_pushvalue(L, 2); lua_xmove(L, NL, 1);  // función → hilo
        int n_args = base - 2;
        for (int i = 3; i <= base; i++) lua_pushvalue(L, i);
        if (n_args > 0) lua_xmove(L, NL, n_args);

        int thread_ref = gl_ref(L, nl_idx);
        SpawnedThread st; st.L = NL; st.ref = thread_ref; st.timer = delay_time;
        st.start_args = n_args; st.started = false;
        self->spawned_threads.push_back(st);
        lua_settop(L, nl_idx);
        return 1;
    }, "delay");
    lua_setfield(L_main, -2, "delay");

    // task.defer(f o hilo, ...) — lo encola para el próximo frame; devuelve el hilo
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        ScriptNodeBase* self = gl_script_de(L);
        if (!self) return 0;
        if (lua_isthread(L, 1)) return self->_gl_reprogramar_hilo(L, lua_tothread(L, 1), 2, 0.0);
        luaL_checktype(L, 1, LUA_TFUNCTION);

        int base = lua_gettop(L);                 // [func(1), args(2..base)]
        lua_State* NL = lua_newthread(L);
        int nl_idx = lua_gettop(L);
        lua_pushvalue(L, 1); lua_xmove(L, NL, 1);
        int n_args = base - 1;
        for (int i = 2; i <= base; i++) lua_pushvalue(L, i);
        if (n_args > 0) lua_xmove(L, NL, n_args);

        int thread_ref = gl_ref(L, nl_idx);
        SpawnedThread st; st.L = NL; st.ref = thread_ref; st.timer = 0.0;  // 0 → próximo _process
        st.start_args = n_args; st.started = false;
        self->spawned_threads.push_back(st);
        lua_settop(L, nl_idx);
        return 1;
    }, "defer");
    lua_setfield(L_main, -2, "defer");

    // task.cancel(hilo) — no vuelve a correr. Se CIERRA, no basta con
    // olvidarlo: si estaba esperando un hijo o una señal, esa espera lo
    // habria despertado igual (resume_external_thread salta los cerrados).
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        ScriptNodeBase* self = gl_script_de(L);
        if (!self || !lua_isthread(L, 1)) return 0;
        lua_State* target = lua_tothread(L, 1);
        if (target == self->L_thread) {
            self->is_waiting = false;
            self->is_external_wait = false;
            self->script_finished = true;
            self->_gl_mark(2, "cancelado con task.cancel");
        } else {
            self->gl_quitar_tarea(target);
        }
        if (target != L && lua_costatus(L, target) == LUA_COSUS) lua_resetthread(target);
        return 0;
    }, "cancel");
    lua_setfield(L_main, -2, "cancel");

    lua_setglobal(L_main, "task");

    // ── print / warn / error ──────────────────────────────────────────────
    // Visible por defecto; se desactiva con godot_luau/script_output = false
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        if (!(bool)ProjectSettings::get_singleton()->get_setting("godot_luau/script_output", true))
            return 0;
        int n = lua_gettop(L);
        String msg;
        for (int i = 1; i <= n; i++) {
            msg += String(luaL_tolstring(L, i, nullptr));
            if (i < n) msg += "\t";
        }
        UtilityFunctions::print("[Luau] ", msg);
        return 0;
    }, "print");
    lua_setglobal(L_main, "print");

    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        if (!(bool)ProjectSettings::get_singleton()->get_setting("godot_luau/script_output", true))
            return 0;
        int n = lua_gettop(L);
        String msg;
        for (int i = 1; i <= n; i++) {
            msg += String(luaL_tolstring(L, i, nullptr));
            if (i < n) msg += "\t";
        }
        UtilityFunctions::push_warning(msg);   // canal de advertencia real (antes era print)
        return 0;
    }, "warn");
    lua_setglobal(L_main, "warn");

    // ── dprint / dwarn: salida de los scripts internos del sistema ────────
    // Solo imprimen si godot_luau/debug_mode esta activo (Modo Debug).
    // Los templates del sistema (PlayerModule, Health, Chat...) los usan
    // para no ensuciar la consola del usuario.
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        if (!(bool)ProjectSettings::get_singleton()->get_setting("godot_luau/debug_mode", false))
            return 0;
        int n = lua_gettop(L);
        String msg;
        for (int i = 1; i <= n; i++) {
            msg += String(luaL_tolstring(L, i, nullptr));
            if (i < n) msg += "\t";
        }
        UtilityFunctions::print("[Luau Debug] ", msg);
        return 0;
    }, "dprint");
    lua_setglobal(L_main, "dprint");

    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        if (!(bool)ProjectSettings::get_singleton()->get_setting("godot_luau/debug_mode", false))
            return 0;
        int n = lua_gettop(L);
        String msg;
        for (int i = 1; i <= n; i++) {
            msg += String(luaL_tolstring(L, i, nullptr));
            if (i < n) msg += "\t";
        }
        UtilityFunctions::print("[Luau Debug WARN] ", msg);
        return 0;
    }, "dwarn");
    lua_setglobal(L_main, "dwarn");

    // ── Puente de los grupos de colision ──────────────────────────────
    // El PhysicsService de Luau llevaba la contabilidad en una tabla y NADA
    // llegaba al motor: dos grupos "no colisionables" seguian chocando. Esta
    // funcion lleva las operaciones a la matriz de verdad (gl_colgroups.h),
    // que se traduce a capas y mascaras de Godot.
    //   __gl_colgroup("reg",  nombre)
    //   __gl_colgroup("del",  nombre)
    //   __gl_colgroup("set",  a, b, booleano)
    //   __gl_colgroup("get",  a, b)        -> bool
    //   __gl_colgroup("id",   nombre)      -> numero
    //   __gl_colgroup("name", id)          -> string
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        const char* op = luaL_checkstring(L, 1);
        if (strcmp(op, "reg") == 0) {
            gl_colgrupo_indice(String(luaL_checkstring(L, 2)));
            return 0;
        }
        if (strcmp(op, "del") == 0) {
            gl_colgrupo_quitar(String(luaL_checkstring(L, 2)));
            return 0;
        }
        if (strcmp(op, "set") == 0) {
            gl_colgrupo_colisionable(String(luaL_checkstring(L, 2)),
                                     String(luaL_checkstring(L, 3)),
                                     lua_toboolean(L, 4) != 0);
            return 0;
        }
        if (strcmp(op, "get") == 0) {
            lua_pushboolean(L, gl_colgrupos_colisionan(String(luaL_checkstring(L, 2)),
                                                       String(luaL_checkstring(L, 3))) ? 1 : 0);
            return 1;
        }
        if (strcmp(op, "id") == 0) {
            lua_pushnumber(L, (double)gl_colgrupo_indice(String(luaL_checkstring(L, 2))));
            return 1;
        }
        if (strcmp(op, "existe") == 0) {
            lua_pushboolean(L, gl_colgrupo_existe(String(luaL_checkstring(L, 2))) ? 1 : 0);
            return 1;
        }
        if (strcmp(op, "name") == 0) {
            const String n = gl_colgrupo_nombre((int)luaL_checknumber(L, 2));
            lua_pushstring(L, n.utf8().get_data());
            return 1;
        }
        return 0;
    }, "__gl_colgroup");
    lua_setglobal(L_main, "__gl_colgroup");

    // KeyframeSequenceProvider (F7): __gl_kfs("registrar", kfs) -> "rbxtemp://N",
    // el id que entiende LoadAnimation.
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        const char* op = luaL_checkstring(L, 1);
        if (strcmp(op, "registrar") == 0) {
            Node* k = _gl_has_metatable(L, 2, "GodotObject") ? gow_node((GodotObjectWrapper*)lua_touserdata(L, 2)) : nullptr;
            if (!k || gl_anim_clase(k) != "KeyframeSequence") {
                luaL_error(L, "Unable to cast value to KeyframeSequence");
                return 0;
            }
            lua_pushstring(L, gl_kfs_registrar(k).utf8().get_data());
            return 1;
        }
        return 0;
    }, "__gl_kfs");
    lua_setglobal(L_main, "__gl_kfs");

    // Roblox guarda los Vector2 y la Scale de UDim en float32: __gl_f32(x)
    // redondea un numero a float32 para que lo escrito y lo leido de vuelta sean
    // iguales con ==. Sin esto, frame.Position == lo_que_escribi daba false y el
    // modulo Pantalla de Build a Mountain rehacia toda la UI en cada frame.
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        lua_pushnumber(L, (double)(float)luaL_checknumber(L, 1));
        return 1;
    }, "__gl_f32");
    lua_setglobal(L_main, "__gl_f32");

    // error(message, level) — como en Lua/Roblox: level 0 omite la posición
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        int level = (int)luaL_optinteger(L, 2, 1);
        lua_settop(L, 1);
        if (lua_isstring(L, 1) && level > 0) {
            lua_Debug ar;
            if (lua_getinfo(L, level, "sl", &ar) && ar.currentline > 0) {
                lua_pushfstring(L, "%s:%d: %s", ar.short_src, ar.currentline, lua_tostring(L, 1));
                lua_replace(L, 1);
            }
        }
        lua_error(L);
        return 0;
    }, "error");
    lua_setglobal(L_main, "error");

    // ── wait (alias global de task.wait) ─────────────────────────────────
    lua_pushcfunction(L_main, _gl_task_wait, "wait");
    lua_setglobal(L_main, "wait");

    // ── __GL_SET_CONTROLS — congelar/soltar los controles del jugador ─────
    // Lo usa el PlayerModule de compatibilidad (Players.LocalPlayer.
    // PlayerScripts.PlayerModule): GetControls():Disable() / :Enable(), que
    // es como Roblox congela al jugador para volar, cinematicas, etc.
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        gl_set_controls_disabled(lua_toboolean(L, 1) != 0);
        return 0;
    }, "__GL_SET_CONTROLS");
    lua_setglobal(L_main, "__GL_SET_CONTROLS");

    // ── __GL_PROMPT_EVENTS(desde) -> (lista, ultimo) ─────────────────────
    // ProximityPromptService es una tabla de Luau y hay una VM POR SCRIPT,
    // asi que C++ no puede dispararle sus señales. En vez de eso apunta los
    // eventos en una cola y cada VM interesada se lleva los nuevos.
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        const uint64_t desde = (uint64_t)luaL_optnumber(L, 1, 0);
        uint64_t ultimo = desde;
        lua_newtable(L);
        int n = 0;
        const std::vector<GLPromptEvent>& log = gl_prompt_log();
        for (size_t i = 0; i < log.size(); i++) {
            const GLPromptEvent& e = log[i];
            if (e.seq <= desde) continue;
            ultimo = MAX(ultimo, e.seq);
            Node* prompt = gl_cast<Node>(ObjectDB::get_instance(e.prompt));
            if (!prompt) continue;      // se destruyo entre medias
            lua_newtable(L);
            wrap_node(L, prompt);
            lua_setfield(L, -2, "prompt");
            if (Node* j = gl_cast<Node>(ObjectDB::get_instance(e.jugador))) {
                wrap_node(L, j);
                lua_setfield(L, -2, "jugador");
            }
            lua_pushnumber(L, (double)e.tipo);
            lua_setfield(L, -2, "tipo");
            lua_rawseti(L, -2, ++n);
        }
        lua_pushnumber(L, (double)ultimo);
        return 2;
    }, "__GL_PROMPT_EVENTS");
    lua_setglobal(L_main, "__GL_PROMPT_EVENTS");

    // ── tick — tiempo Unix actual (segundos) ──────────────────────────────
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        lua_pushnumber(L, (double)Time::get_singleton()->get_unix_time_from_system());
        return 1;
    }, "tick");
    lua_setglobal(L_main, "tick");

    // ── time — game execution time (seconds) ─────────────────────────────
    //// ── time — tiempo de ejecución del juego (segundos) ───────────────────
    // En Roblox time() ES workspace.DistributedGameTime: el reloj del SERVIDOR,
    // sincronizado (1.14.8), no el uptime local de esta ventana.
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        lua_pushnumber(L, gl_distributed_game_time());
        return 1;
    }, "time");
    lua_setglobal(L_main, "time");

    // ── typeof — tipo extendido (reconoce Vector3, Color3, Instance) ──────
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        int t = lua_type(L, 1);
        if (t == LUA_TUSERDATA && lua_getmetatable(L, 1)) {
            luaL_getmetatable(L, "Vector3");
            if (lua_rawequal(L, -1, -2)) { lua_pop(L, 2); lua_pushstring(L, "Vector3"); return 1; }
            lua_pop(L, 1);
            luaL_getmetatable(L, "Color3");
            if (lua_rawequal(L, -1, -2)) { lua_pop(L, 2); lua_pushstring(L, "Color3"); return 1; }
            lua_pop(L, 1);
            luaL_getmetatable(L, "GodotObject");
            if (lua_rawequal(L, -1, -2)) { lua_pop(L, 2); lua_pushstring(L, "Instance"); return 1; }
            lua_pop(L, 1);
            // Los demas userdata del puente dicen su tipo en __type, como en
            // el typeof de Luau (EditableImage y EditableMesh: "Object").
            lua_rawgetfield(L, -1, "__type");
            if (lua_isstring(L, -1)) return 1;
            lua_pop(L, 2);
            lua_pushstring(L, "userdata"); return 1;
        }
        // Tipos basados en tabla (Vector2, UDim2, CFrame, TweenInfo...) llevan
        // un campo __type en su metatable (lo pone la stdlib) → como Roblox.
        if (t == LUA_TTABLE && lua_getmetatable(L, 1)) {
            lua_getfield(L, -1, "__type");
            if (lua_isstring(L, -1)) { const char* tn = lua_tostring(L, -1); lua_pushstring(L, tn); return 1; }
            lua_pop(L, 2);
        }
        lua_pushstring(L, lua_typename(L, t));
        return 1;
    }, "typeof");
    lua_setglobal(L_main, "typeof");

    // ── pcall mejorado (ya viene de luaL_openlibs pero lo sobreescribimos) ─
    // El pcall nativo de Luau ya funciona correctamente, solo lo dejamos.

    // ── tostring / tonumber ───────────────────────────────────────────────
    // luaL_tolstring respeta __tostring y SIEMPRE deja un string en la cima
    // (el lua_tostring anterior devolvía la tabla/nil tal cual para no-strings).
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        luaL_tolstring(L, 1, nullptr);
        return 1;
    }, "tostring");
    lua_setglobal(L_main, "tostring");

    // tonumber(v, [base]) — soporta base (2..36) y hex 0x; rechaza basura final.
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        if (!lua_isnoneornil(L, 2)) {
            int base = (int)luaL_checkinteger(L, 2);
            const char* s = luaL_checkstring(L, 1);
            if (base < 2 || base > 36) { lua_pushnil(L); return 1; }
            char* endp = nullptr;
            long val = strtol(s, &endp, base);
            // saltar espacios finales
            while (endp && (*endp == ' ' || *endp == '\t' || *endp == '\n')) endp++;
            if (endp && *endp == '\0' && endp != s) { lua_pushnumber(L, (double)val); return 1; }
            lua_pushnil(L); return 1;
        }
        if (lua_type(L, 1) == LUA_TNUMBER) { lua_pushvalue(L, 1); return 1; }
        if (lua_type(L, 1) == LUA_TSTRING) {
            const char* s = luaL_checkstring(L, 1);
            char* endp = nullptr;
            double n = strtod(s, &endp);
            while (endp && (*endp == ' ' || *endp == '\t' || *endp == '\n')) endp++;
            if (endp && *endp == '\0' && endp != s) { lua_pushnumber(L, n); return 1; }
        }
        lua_pushnil(L); return 1;
    }, "tonumber");
    lua_setglobal(L_main, "tonumber");

    // ── UserInputService ─────────────────────────────────────────────────
    lua_newtable(L_main);

    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        int n = lua_gettop(L);
        int arg = (n >= 2) ? 2 : 1;
        Input* inp = Input::get_singleton();
        Key code = KEY_UNKNOWN;

        // Accepts Enum.KeyCode (number) OR legacy string ("LeftShift", "W", etc.)
        //// Acepta Enum.KeyCode (número) O string legacy ("LeftShift", "W", etc.)
        if (lua_type(L, arg) == LUA_TNUMBER) {
            code = roblox_keycode_to_godot((int)lua_tonumber(L, arg));
        } else {
            const char* key_name = luaL_checkstring(L, arg);
            if      (strcmp(key_name, "W") == 0 || strcmp(key_name, "Up") == 0)       code = KEY_W;
            else if (strcmp(key_name, "A") == 0 || strcmp(key_name, "Left") == 0)     code = KEY_A;
            else if (strcmp(key_name, "S") == 0 || strcmp(key_name, "Down") == 0)     code = KEY_S;
            else if (strcmp(key_name, "D") == 0 || strcmp(key_name, "Right") == 0)    code = KEY_D;
            else if (strcmp(key_name, "Space") == 0)     code = KEY_SPACE;
            else if (strcmp(key_name, "E") == 0)         code = KEY_E;
            else if (strcmp(key_name, "Q") == 0)         code = KEY_Q;
            else if (strcmp(key_name, "F") == 0)         code = KEY_F;
            else if (strcmp(key_name, "G") == 0)         code = KEY_G;
            else if (strcmp(key_name, "H") == 0)         code = KEY_H;
            else if (strcmp(key_name, "I") == 0)         code = KEY_I;
            else if (strcmp(key_name, "J") == 0)         code = KEY_J;
            else if (strcmp(key_name, "K") == 0)         code = KEY_K;
            else if (strcmp(key_name, "L") == 0)         code = KEY_L;
            else if (strcmp(key_name, "M") == 0)         code = KEY_M;
            else if (strcmp(key_name, "N") == 0)         code = KEY_N;
            else if (strcmp(key_name, "O") == 0)         code = KEY_O;
            else if (strcmp(key_name, "P") == 0)         code = KEY_P;
            else if (strcmp(key_name, "R") == 0)         code = KEY_R;
            else if (strcmp(key_name, "T") == 0)         code = KEY_T;
            else if (strcmp(key_name, "U") == 0)         code = KEY_U;
            else if (strcmp(key_name, "V") == 0)         code = KEY_V;
            else if (strcmp(key_name, "X") == 0)         code = KEY_X;
            else if (strcmp(key_name, "Y") == 0)         code = KEY_Y;
            else if (strcmp(key_name, "Z") == 0)         code = KEY_Z;
            else if (strcmp(key_name, "C") == 0)         code = KEY_C;
            else if (strcmp(key_name, "B") == 0)         code = KEY_B;
            else if (strcmp(key_name, "0") == 0)         code = KEY_0;
            else if (strcmp(key_name, "1") == 0)         code = KEY_1;
            else if (strcmp(key_name, "2") == 0)         code = KEY_2;
            else if (strcmp(key_name, "3") == 0)         code = KEY_3;
            else if (strcmp(key_name, "4") == 0)         code = KEY_4;
            else if (strcmp(key_name, "5") == 0)         code = KEY_5;
            else if (strcmp(key_name, "6") == 0)         code = KEY_6;
            else if (strcmp(key_name, "7") == 0)         code = KEY_7;
            else if (strcmp(key_name, "8") == 0)         code = KEY_8;
            else if (strcmp(key_name, "9") == 0)         code = KEY_9;
            else if (strcmp(key_name, "LeftShift")   == 0 || strcmp(key_name, "RightShift")   == 0) code = KEY_SHIFT;
            else if (strcmp(key_name, "LeftControl") == 0 || strcmp(key_name, "RightControl") == 0) code = KEY_CTRL;
            else if (strcmp(key_name, "LeftAlt")     == 0 || strcmp(key_name, "RightAlt")     == 0) code = KEY_ALT;
            else if (strcmp(key_name, "Return")      == 0 || strcmp(key_name, "Enter")        == 0) code = KEY_ENTER;
            else if (strcmp(key_name, "Escape")      == 0) code = KEY_ESCAPE;
            else if (strcmp(key_name, "Tab")         == 0) code = KEY_TAB;
            else if (strcmp(key_name, "Backspace")   == 0) code = KEY_BACKSPACE;
            else if (strcmp(key_name, "Delete")      == 0) code = KEY_DELETE;
            else if (strcmp(key_name, "Insert")      == 0) code = KEY_INSERT;
            else if (strcmp(key_name, "Home")        == 0) code = KEY_HOME;
            else if (strcmp(key_name, "End")         == 0) code = KEY_END;
            else if (strcmp(key_name, "PageUp")      == 0) code = KEY_PAGEUP;
            else if (strcmp(key_name, "PageDown")    == 0) code = KEY_PAGEDOWN;
            else if (strcmp(key_name, "Up")          == 0) code = KEY_UP;
            else if (strcmp(key_name, "Down")        == 0) code = KEY_DOWN;
            else if (strcmp(key_name, "Left")        == 0) code = KEY_LEFT;
            else if (strcmp(key_name, "Right")       == 0) code = KEY_RIGHT;
            else if (strcmp(key_name, "F1")  == 0) code = KEY_F1;
            else if (strcmp(key_name, "F2")  == 0) code = KEY_F2;
            else if (strcmp(key_name, "F3")  == 0) code = KEY_F3;
            else if (strcmp(key_name, "F4")  == 0) code = KEY_F4;
            else if (strcmp(key_name, "F5")  == 0) code = KEY_F5;
            else if (strcmp(key_name, "F6")  == 0) code = KEY_F6;
            else if (strcmp(key_name, "F7")  == 0) code = KEY_F7;
            else if (strcmp(key_name, "F8")  == 0) code = KEY_F8;
            else if (strcmp(key_name, "F9")  == 0) code = KEY_F9;
            else if (strcmp(key_name, "F10") == 0) code = KEY_F10;
            else if (strcmp(key_name, "F11") == 0) code = KEY_F11;
            else if (strcmp(key_name, "F12") == 0) code = KEY_F12;
        }

        lua_pushboolean(L, inp ? inp->is_key_pressed(code) : false);
        return 1;
    }, "IsKeyDown");
    lua_setfield(L_main, -2, "IsKeyDown");

    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        int n = lua_gettop(L);
        int btn = (n >= 2) ? (int)luaL_checknumber(L, 2) : (int)luaL_checknumber(L, 1);
        Input* inp = Input::get_singleton();
        MouseButton mb = (btn == 1) ? MOUSE_BUTTON_LEFT :
                         (btn == 2) ? MOUSE_BUTTON_RIGHT :
                                      MOUSE_BUTTON_MIDDLE;
        lua_pushboolean(L, inp ? inp->is_mouse_button_pressed(mb) : false);
        return 1;
    }, "IsMouseButtonPressed");
    lua_setfield(L_main, -2, "IsMouseButtonPressed");

    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        Input* inp = Input::get_singleton();
        Vector2 delta = inp ? inp->get_last_mouse_velocity() * 0.001f : Vector2();
        lua_newtable(L);
        lua_pushnumber(L, delta.x); lua_setfield(L, -2, "X");
        lua_pushnumber(L, delta.y); lua_setfield(L, -2, "Y");
        return 1;
    }, "GetMouseDelta");
    lua_setfield(L_main, -2, "GetMouseDelta");

    // Un ServerScript no puede leer entrada de hardware (igual que Roblox):
    // sustituimos la tabla por un proxy que lanza un error claro al usarse.
    if (!es_cliente) {
        lua_pop(L_main, 1);  // descartar la tabla real de UserInputService
        lua_newtable(L_main);
        lua_newtable(L_main);  // metatable
        lua_pushcfunction(L_main, [](lua_State* L) -> int {
            luaL_error(L, "UserInputService no esta disponible en un ServerScript: "
                          "la entrada (teclado/raton/tactil) solo existe en el cliente (LocalScript).");
            return 0;
        }, "__index");
        lua_setfield(L_main, -2, "__index");
        lua_setmetatable(L_main, -2);
    }
    lua_setglobal(L_main, "UserInputService");

    // ── require ───────────────────────────────────────────────────────────
    setup_require_system(L_main);
    setup_remotefunction_system(L_main);

    // ── _FileAccess — I/O de archivos para DataStoreService ───────────────
    // Solo user:// (DataStore, MemoryStore y MessagingService no usan otra
    // cosa). Es un global que ve cualquier script: antes aceptaba cualquier
    // ruta y un script podia leer, escribir o borrar archivos del disco.
    lua_newtable(L_main);
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        const String path = gl_ruta_datos(L, 1);
        Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
        if (f.is_null()) { lua_pushnil(L); return 1; }
        String s = f->get_as_text();
        lua_pushstring(L, s.utf8().get_data());
        return 1;
    }, "read");     lua_setfield(L_main, -2, "read");
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        const String path = gl_ruta_datos(L, 1);
        const char* data = luaL_checkstring(L, 2);
        Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
        if (!f.is_null()) f->store_string(String::utf8(data));
        return 0;
    }, "write");    lua_setfield(L_main, -2, "write");
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        lua_pushboolean(L, FileAccess::file_exists(gl_ruta_datos(L, 1)) ? 1 : 0);
        return 1;
    }, "exists");   lua_setfield(L_main, -2, "exists");
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        DirAccess::remove_absolute(gl_ruta_datos(L, 1));
        return 0;
    }, "delete");   lua_setfield(L_main, -2, "delete");
    // Listar archivos de un directorio: lo usa el MessagingService entre
    // instancias (1.14.16) para ver los buzones de los demás servidores.
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        Ref<DirAccess> d = DirAccess::open(gl_ruta_datos(L, 1));
        lua_newtable(L);
        if (d.is_null()) return 1;
        PackedStringArray files = d->get_files();
        for (int i = 0; i < files.size(); i++) {
            lua_pushstring(L, files[i].utf8().get_data());
            lua_rawseti(L, -2, i + 1);
        }
        return 1;
    }, "list");     lua_setfield(L_main, -2, "list");
    lua_setglobal(L_main, "_FileAccess");

    // ── __gl_mps — MarketplaceService compartido (gl_tienda.h) ──────────
    gl_tienda_registrar(L_main);
    // ── __gl_nav — PathfindingService (gl_navegacion.h) ─────────────────
    gl_nav_registrar(L_main);
    // ── EditableImage/EditableMesh y AssetService (gl_puente_editables.h) ─
    gl_editables_registrar(L_main);
    // ── __gl_tex_* — streaming de texturas (gl_puente_texturas.h) ────────
    gl_tex_registrar(L_main);

    // ── _GL_NET_ROLE — 0 single-player, 1 servidor/host, 2 cliente ───────
    // Lo usa el DataStoreService del stdlib para no dejar que un cliente
    // toque los datos (en Roblox eso es error). Aquí importa el doble: TODAS
    // las ventanas del proyecto comparten `user://`, así que un cliente
    // escribiendo pisaría literalmente los archivos del servidor.
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        lua_pushnumber(L, (double)gl_net_role());
        return 1;
    }, "_GL_NET_ROLE");
    lua_setglobal(L_main, "_GL_NET_ROLE");

    // ── _GL_VM_ID — id único de esta VM dentro del proceso (1.14.16) ─────
    // Cada script tiene su propia VM. El MessagingService le da a cada una su
    // propio buzón: si dos scripts del MISMO servidor compartieran buzón,
    // cada uno saltaría el suyo al leer y NINGUNO vería los mensajes del otro.
    // El stdlib lo pide una sola vez al cargar.
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        static int64_t ctr = 0;
        lua_pushnumber(L, (double)(++ctr));
        return 1;
    }, "_GL_VM_ID");
    lua_setglobal(L_main, "_GL_VM_ID");

    // ── _GL_NET — puente al NetworkService para el stdlib (1.14.17) ──────
    // Lo usa TeleportService. Se resuelve por ObjectID y se llama por NOMBRE,
    // asi que el stdlib no depende de roblox_network.h.
    lua_newtable(L_main);
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        Object* ns = ObjectDB::get_instance(gl_net_service_id());
        if (!ns) { lua_pushnil(L); return 1; }
        const char* m = luaL_checkstring(L, 1);
        // (metodo, arg1, arg2, arg3) -> Variant; solo lo que necesita el stdlib
        Variant r;
        if (lua_gettop(L) >= 4)      r = ns->call(StringName(m), (int64_t)luaL_checknumber(L, 2),
                                                 (int64_t)luaL_checknumber(L, 3), String(luaL_checkstring(L, 4)));
        else if (lua_gettop(L) >= 2) r = ns->call(StringName(m), (int64_t)luaL_checknumber(L, 2));
        else                          r = ns->call(StringName(m));
        if (r.get_type() == Variant::STRING)      lua_pushstring(L, String(r).utf8().get_data());
        else if (r.get_type() == Variant::BOOL)   lua_pushboolean(L, (bool)r ? 1 : 0);
        else if (r.get_type() == Variant::INT)    lua_pushnumber(L, (double)(int64_t)r);
        else if (r.get_type() == Variant::PACKED_INT32_ARRAY) {
            PackedInt32Array a = r;
            lua_newtable(L);
            for (int i = 0; i < a.size(); i++) { lua_pushnumber(L, a[i]); lua_rawseti(L, -2, i + 1); }
        }
        else lua_pushnil(L);
        return 1;
    }, "call");
    lua_setfield(L_main, -2, "call");
    // Teleport necesita pasar el NODO del jugador, no un numero.
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        Object* ns = ObjectDB::get_instance(gl_net_service_id());
        GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
        Node* plr = w ? gow_node(w) : nullptr;
        if (!ns || !plr) { lua_pushnumber(L, 0); return 1; }
        int port = (int)luaL_optnumber(L, 2, 0);
        String data = lua_isstring(L, 3) ? String(lua_tostring(L, 3)) : String();
        Variant r = ns->call("net_teleport_player", (Object*)plr, port, data);
        lua_pushnumber(L, (double)(int64_t)r);
        return 1;
    }, "teleport");
    lua_setfield(L_main, -2, "teleport");
    lua_setglobal(L_main, "_GL_NET");

    // ── _JSON — JSON serialization ────────────────────────────────────────
    //// ── _JSON — serialización JSON ────────────────────────────────────────
    lua_newtable(L_main);
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        Variant v = _gdluau_lua_to_variant(L, 1);
        String result = JSON::stringify(v, "", false, true);
        lua_pushstring(L, result.utf8().get_data());
        return 1;
    }, "encode");   lua_setfield(L_main, -2, "encode");
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        const char* s = luaL_checkstring(L, 1);
        Variant v = JSON::parse_string(String(s));
        _gdluau_variant_to_lua(L, v);
        return 1;
    }, "decode");   lua_setfield(L_main, -2, "decode");
    lua_setglobal(L_main, "_JSON");

    // ── _HTTP — cliente HTTP real (ASINCRONO, vía HTTPClient) ─────────────
    // Las tres ceden la corrutina que las llama y la reanudan con el
    // resultado; el juego no se para. Ver gl_http_call.
    lua_newtable(L_main);
    // _HTTP.request(method, url, body?, headers?) → {Success,StatusCode,StatusMessage,Body}
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        String method = gl_arg_present(L, 1) ? String(luaL_checkstring(L, 1)) : String("GET");
        const char* url = luaL_checkstring(L, 2);
        String body = gl_arg_present(L, 3) ? String(luaL_checkstring(L, 3)) : String();
        PackedStringArray headers = gl_lua_headers_to_array(L, 4);
        return gl_http_call(L, gl_http_method_from_string(method), String(url), headers, body, 0);
    }, "request");  lua_setfield(L_main, -2, "request");
    // _HTTP.get(url, headers?) → body
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        const char* url = luaL_checkstring(L, 1);
        PackedStringArray headers = gl_lua_headers_to_array(L, 2);
        return gl_http_call(L, HTTPClient::METHOD_GET, String(url), headers, String(), 1);
    }, "get");     lua_setfield(L_main, -2, "get");
    // _HTTP.post(url, body?, contentType?, headers?) → body
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        const char* url = luaL_checkstring(L, 1);
        String body  = gl_arg_present(L, 2) ? String(luaL_checkstring(L, 2)) : String();
        String ctype = gl_arg_present(L, 3) ? String(luaL_checkstring(L, 3)) : String("application/json");
        PackedStringArray headers = gl_lua_headers_to_array(L, 4);
        headers.push_back(String("Content-Type: ") + ctype);
        return gl_http_call(L, HTTPClient::METHOD_POST, String(url), headers, body, 1);
    }, "post");    lua_setfield(L_main, -2, "post");
    lua_setglobal(L_main, "_HTTP");

    // ── _InstanceNew — crear instancias en runtime ────────────────────────
    lua_pushcfunction(L_main, [](lua_State* L) -> int {
        const char* class_name = luaL_checkstring(L, 1);
        // Una clase abstracta (HandleAdornment) no se crea: nil, y la capa Luau
        // da el error de Roblox. Preguntando antes, Godot no ensucia la consola
        // con su "cannot be instantiated".
        const StringName cn(class_name);
        if (!ClassDB::class_exists(cn) || !ClassDB::can_instantiate(cn)) { lua_pushnil(L); return 1; }
        Object* obj = ClassDB::instantiate(cn);
        Node* node  = gl_cast<Node>(obj);
        if (!node) { if (obj) memdelete(obj); lua_pushnil(L); return 1; }
        wrap_node(L, node);
        gl_track_orphan(node);   // sin padre = lo libera el cierre (1.15)
        return 1;
    }, "_InstanceNew");
    lua_setglobal(L_main, "_InstanceNew");

    gl_registrar_duenos(L_main);

    // ── Ejecutar stdlib (servicios Lua, class(), Enum, TweenInfo…) ────────
    // El bytecode sale del cache: se compila con el PRIMER script del juego
    // y las demas VMs lo reutilizan tal cual.
    {
        const std::string& sbc = gl_compile_cached(LUAU_STDLIB_CODE);
        if (luau_load(L_main, "__GodotLuauStdlib", sbc.data(), sbc.size(), 0) == 0) {
            gl_nativo(L_main);
            if (lua_pcall(L_main, 0, 0, 0) != LUA_OK) {
                UtilityFunctions::print("[GodotLuau Stdlib Error] ", lua_tostring(L_main, -1));
                lua_pop(L_main, 1);
            }
        } else {
            UtilityFunctions::print("[GodotLuau Stdlib Syntax] ", lua_tostring(L_main, -1));
            lua_pop(L_main, 1);
        }
        // Once/Wait genericos de las señales: de _G al registro, donde los
        // busca gl_signal_lowercase_aliases y ningun script los pisa.
        for (const char* k : { "__gl_sig_once", "__gl_sig_wait" }) {
            lua_getglobal(L_main, k);
            lua_setfield(L_main, LUA_REGISTRYINDEX, k);
            lua_pushnil(L_main);
            lua_setglobal(L_main, k);
        }
    }


    // ── _G y shared: tablas COMUNES del contexto (como Roblox) ─────────────
    // El stdlib se carga con _G = la tabla de globales (registra ahi sus
    // __SVC_*). Desde aqui _G es una tabla aparte y vacia, igual que en
    // Roblox: `_G.x = 1` en un script se ve en todos los del contexto, pero
    // no crea una global `x` en ninguno. `shared` la crea el stdlib.
    lua_pushvalue(L_main, LUA_GLOBALSINDEX);
    lua_setfield(L_main, LUA_REGISTRYINDEX, "GL_GLOBALES");
    lua_newtable(L_main);
    lua_setglobal(L_main, "_G");
}

#endif // LUAU_SCRIPT_CONTEXTO_H

#ifndef LUAU_SCRIPT_CICLO_H
#define LUAU_SCRIPT_CICLO_H
#include "gl_cast.h"

// Metodos de ScriptNodeBase: el ciclo de vida del script. Planificador de
// tareas (esperas, reanudar hilos, HTTP), ids de script, _notification,
// las senales de instancia por script (relays) y _process.
// Va aparte de luau_script.h (pasaba de 2.000 lineas) y se incluye desde
// alli, justo despues de la clase.

// Suspender el hilo `th` hasta que algo externo lo despierte. Es lo que
// hacen WaitForChild, InvokeServer y las peticiones HTTP: el hilo principal
// se marca con is_external_wait y una corrutina spawneada con timer = -1,
// que es como _process sabe que NO debe reanudarla por tiempo.
inline void ScriptNodeBase::_suspend_externally(lua_State* th) {
    if (th == L_thread) {
        is_external_wait = true;
    } else {
        for (auto& st : spawned_threads)
            if (st.L == th) { st.timer = -1.0; break; }
    }
}

// ── Tareas del planificador ───────────────────────────────────────
// ¿Lo gestiona este planificador? El hilo principal o uno de spawned_threads.
// Una corrutina de coroutine.create/wrap no: la reanuda quien la creo.
inline bool ScriptNodeBase::gl_hilo_gestionado(lua_State* th) const {
    if (th == L_thread) return true;
    for (const SpawnedThread& st : spawned_threads)
        if (st.L == th) return true;
    return false;
}

// Apuntar `th` en spawned_threads con su propia referencia, que lo mantiene
// vivo. timer = segundos hasta reanudarlo; -1 = suspendido hasta que algo de
// fuera lo reanude.
inline void ScriptNodeBase::gl_adoptar_hilo(lua_State* th, double timer) {
    lua_pushthread(th);
    const int ref = gl_ref(th, -1);
    lua_pop(th, 1);
    SpawnedThread st; st.L = th; st.ref = ref; st.timer = timer; st.started = true;
    spawned_threads.push_back(st);
}

// Arranca `th` (la funcion y sus nargs argumentos ya en su pila) como tarea.
// Se apunta ANTES del primer resume: si cede dentro, task.wait y compañia ya
// lo encuentran en spawned_threads, y _after_resume decide cuando vuelve.
inline void ScriptNodeBase::gl_start_task(lua_State* th, int nargs) {
    gl_adoptar_hilo(th, -1.0);
    _after_resume(th, lua_resume(th, nullptr, nargs));
}

// Quitar la tarea `th` sin reanudarla. false si no estaba apuntada.
inline bool ScriptNodeBase::gl_quitar_tarea(lua_State* th) {
    for (int i = (int)spawned_threads.size() - 1; i >= 0; --i) {
        if (spawned_threads[i].L != th) continue;
        if (L_main) gl_unref(L_main, spawned_threads[i].ref);
        spawned_threads.erase(spawned_threads.begin() + i);
        return true;
    }
    return false;
}

// Antes de reanudar `th`: queda suspendido (-1) salvo que, mientras corre,
// algo lo reprograme (task.wait, task.defer(coroutine.running())...).
inline void ScriptNodeBase::_antes_de_reanudar(lua_State* th) {
    if (th == L_thread) { main_resched = false; main_waited = 0.0; return; }
    for (SpawnedThread& st : spawned_threads)
        if (st.L == th) { st.timer = -1.0; st.waited = 0.0; break; }
}

// task.wait(n) y wait(n): ceden n segundos. Si cede una corrutina del
// usuario (coroutine.wrap/create) que el planificador no conoce, se adopta
// y cede SIN valores: Roblox la reanuda igual pasado el tiempo, y a quien
// la reanudo no le llega el numero. Antes se quedaba suspendida para siempre.
inline int ScriptNodeBase::_gl_task_wait(lua_State* L) {
    ScriptNodeBase* self = gl_script_de(L);
    const double sec = luaL_optnumber(L, 1, 0.0);
    if (self && lua_isyieldable(L) && !self->gl_hilo_gestionado(L)) {
        self->gl_adoptar_hilo(L, sec);
        return lua_yield(L, 0);
    }
    lua_pushnumber(L, sec);
    return lua_yield(L, 1);   // Luau: hay que DEVOLVER lua_yield para que ceda de verdad
}

// Pasa a la pila de `co` los argumentos de L desde la posicion `desde`, para
// su proximo resume. Al propio hilo que llama no se le pueden poner (esta
// corriendo): ahi solo se reprograma, sin argumentos.
inline int ScriptNodeBase::_gl_args_a_hilo(lua_State* L, lua_State* co, int desde) {
    const int top = lua_gettop(L);
    const int n = top - desde + 1;
    if (co == L || n <= 0) return 0;
    lua_checkstack(co, n);
    for (int i = desde; i <= top; i++) lua_pushvalue(L, i);
    lua_xmove(L, co, n);
    return n;
}

// task.defer/delay con un HILO: lo reanuda pasado `t` segundos (0 = el
// proximo frame) con los argumentos que vengan detras. Devuelve el hilo.
inline int ScriptNodeBase::_gl_reprogramar_hilo(lua_State* L, lua_State* co, int desde, double t) {
    if (co != L && lua_costatus(L, co) != LUA_COSUS) {
        luaL_error(L, "cannot resume dead coroutine");
        return 0;
    }
    const int n = _gl_args_a_hilo(L, co, desde);
    if (co == L_thread) {
        main_start_args = n;
        wait_timer = t;
        is_waiting = true;
        is_external_wait = false;
        main_resched = true;
        main_waited = 0.0;
    } else {
        gl_quitar_tarea(co);
        gl_adoptar_hilo(co, t);
        spawned_threads.back().started = false;   // 1er resume: con sus argumentos
        spawned_threads.back().start_args = n;
    }
    lua_pushvalue(L, desde - 1);
    return 1;
}

// Registrar un hilo que cede esperando la respuesta de un InvokeServer/Client
// por red (mismo mecanismo de suspensión que WaitForChild).
inline void ScriptNodeBase::add_waiting_invoke(lua_State* th, lua_State* mL, int ref,
    int64_t call_id, double timeout) {
    waiting_invokes.push_back({th, mL, ref, call_id, timeout, 0.0, false});
    _suspend_externally(th);
}

// Registrar un hilo que cede esperando una peticion HTTP ya lanzada.
inline void ScriptNodeBase::add_waiting_http(lua_State* th, lua_State* mL, int ref, int ret_mode,
    const GLHttpPending& req) {
    WaitingHttp w;
    w.thread = th; w.main_L = mL; w.ref = ref; w.ret_mode = ret_mode; w.req = req;
    waiting_https.push_back(w);
    _suspend_externally(th);
}

// Deja en la pila de `L` lo que devuelve _HTTP.*: la tabla completa para
// request(), o solo el body para get()/post().
inline int ScriptNodeBase::gl_http_push_result(lua_State* L, const GLHttpResult& r, int ret_mode) {
    if (ret_mode == 1) {
        lua_pushstring(L, r.body.utf8().get_data());
        return 1;
    }
    lua_newtable(L);
    lua_pushboolean(L, r.ok ? 1 : 0);                  lua_setfield(L, -2, "Success");
    lua_pushnumber(L, (double)r.status);               lua_setfield(L, -2, "StatusCode");
    lua_pushstring(L, r.status_msg.utf8().get_data()); lua_setfield(L, -2, "StatusMessage");
    lua_pushstring(L, r.body.utf8().get_data());       lua_setfield(L, -2, "Body");
    return 1;
}

// Punto de entrada de las tres funciones de _HTTP.
//
// Si el hilo puede ceder (todo el codigo de usuario corre en corrutinas), se
// lanza la peticion, se registra y se CEDE: _process la avanza y reanuda el
// hilo con el resultado. El juego sigue corriendo.
//
// Si NO puede ceder — dentro de un metametodo, en un callback llamado desde
// C, o sin nodo de script en el registro — se cae al camino bloqueante. Es
// el comportamiento de siempre y devuelve exactamente lo mismo; solo que
// ahi si se para el frame.
inline int ScriptNodeBase::gl_http_call(lua_State* L, HTTPClient::Method method, const String& url,
    const PackedStringArray& headers, const String& body, int ret_mode) {
    Node* nd = gl_nodo_script_de(L);
    ScriptNodeBase* sn = gl_cast<ScriptNodeBase>(nd);

    if (sn && sn->is_inside_tree() && lua_isyieldable(L)) {
        lua_State* main_L = gl_main_of(L);
        if (!main_L) main_L = L;

        GLHttpPending req;
        gl_http_begin(req, method, url, headers, body);
        // Aunque gl_http_begin ya haya fallado (URL mala, socket que no
        // abre) se cede igual: el resultado se entrega por el mismo camino
        // en el frame siguiente, y asi GetAsync se comporta SIEMPRE igual
        // en vez de a veces ceder y a veces no.
        lua_pushthread(L);
        int ref = gl_ref(L, -1);
        lua_pop(L, 1);
        sn->add_waiting_http(L, main_L, ref, ret_mode, req);
        return gl_yield_externo(L, "__WAIT_HTTP__");
    }

    GLHttpResult r = gl_http_perform(method, url, headers, body);
    return gl_http_push_result(L, r, ret_mode);
}

// Called by WaitForChild closures in luau_api.h (or the local __index override)
inline void ScriptNodeBase::add_waiting_child(lua_State* th, lua_State* mL, int ref, Node* parent,
    const String& name, double timeout) {
    uint64_t pid = parent ? (uint64_t)parent->get_instance_id() : 0;
    // El stack se captura AQUI (aun no cedimos): es el del caller de WaitForChild
    waiting_children.push_back({th, mL, ref, pid, name, timeout, 0.0,
                                String::utf8(lua_debugtrace(th)), false});
    // Suspend the coroutine so _process won't auto-resume it via timer
    if (th == L_thread) {
        is_external_wait = true;
    } else {
        for (auto& st : spawned_threads)
            if (st.L == th) { st.timer = -1.0; return; }
    }
}

// Resume a coroutine externally (Signal:Wait / WaitForChild resolved).
// Updates spawned_threads timer or main-thread flags based on new yield.
inline void ScriptNodeBase::resume_external_thread(lua_State* th, int nargs) {
    // Mismo motivo que en _process: solo se reanuda si sigue SUSPENDIDA.
    // Reanudar una corrutina ya terminada (o muerta por error) revienta
    // dentro de luau_execute. Aqui llegan las que esperaban un hijo
    // (WaitForChild) o una señal, y esas pueden haber muerto entretanto.
    if (!th || !L_main || !gl_state_alive(L_main)) return;
    if (lua_costatus(L_main, th) != LUA_COSUS) return;
    _antes_de_reanudar(th);
    _after_resume(th, lua_resume(th, nullptr, nargs));
}

// Reanudar un hilo LANZANDO un error dentro de él. Roblox re-lanza en el
// invocador el error del handler remoto de un RemoteFunction; antes aquí se
// devolvía nil y el mensaje se perdía.
inline void ScriptNodeBase::resume_external_thread_error(lua_State* th, const String& msg) {
    if (!th || !L_main || !gl_state_alive(L_main)) return;
    if (lua_costatus(L_main, th) != LUA_COSUS) return;
    _antes_de_reanudar(th);
    lua_pushstring(th, msg.utf8().get_data());
    _after_resume(th, lua_resumeerror(th, nullptr));
}

// Despues de reanudar `th`, decide cuando vuelve a correr. Un numero en la
// cima es task.wait(n); otra cosa es una marca de espera externa
// (WaitForChild, :Wait(), HTTP...) y lo reanuda quien la apunto; sin valores
// (coroutine.yield()) se queda suspendido, o con lo que le pusiera quien lo
// reprogramo mientras corria.
inline void ScriptNodeBase::_after_resume(lua_State* th, int status) {
    if (th == L_thread) { _main_tras_resume(status); return; }
    for (int i = (int)spawned_threads.size()-1; i >= 0; --i) {
        if (spawned_threads[i].L != th) continue;
        if (status == LUA_YIELD) {
            if (lua_gettop(th) > 0) {
                spawned_threads[i].timer = lua_isnumber(th, -1) ? lua_tonumber(th, -1) : -1.0;
                lua_pop(th, 1);
            }
        } else {
            if (status != LUA_OK) gl_report_script_error(th);
            if (L_main) gl_unref(L_main, spawned_threads[i].ref);
            spawned_threads.erase(spawned_threads.begin() + i);
        }
        return;
    }
    // No es una tarea apuntada (una corrutina del usuario reanudada desde
    // fuera): solo queda decir si murio.
    if (status != LUA_OK && status != LUA_YIELD) gl_report_script_error(th);
}

// Lo mismo para el hilo principal del script, que no vive en spawned_threads.
inline void ScriptNodeBase::_main_tras_resume(int status) {
    lua_State* th = L_thread;
    if (status == LUA_YIELD) {
        if (lua_gettop(th) > 0 && lua_isnumber(th, -1)) {
            wait_timer = lua_tonumber(th, -1);
            lua_pop(th, 1);
            is_external_wait = false;
        } else {
            if (lua_gettop(th) > 0) lua_pop(th, 1);   // marca de espera externa
            if (!main_resched) { is_external_wait = true; wait_timer = 1e9; }
        }
        is_waiting = true;
        if (_gl_run_state != 3) _gl_mark(4, is_external_wait
            ? String("esperando algo de fuera (WaitForChild / senal / HTTP)")
            : String("esperando ") + String::num(wait_timer, 2) + String(" s"));
    } else {
        is_waiting       = false;
        is_external_wait = false;
        script_finished  = true;
        if (status != LUA_OK) {
            const char* msj = lua_isstring(th, -1) ? lua_tostring(th, -1) : "error";
            _gl_mark(3, String(msj));
            gl_report_script_error(th);
        } else {
            _gl_mark(2, "terminado");
        }
    }
}

// ¿Otro ScriptNodeBase VIVO (distinto de este) ya usa este script_id?
// Es la señal inequívoca de un DUPLICADO en el editor: Godot copió el
// script_id exportado y compartió la MISMA Ref<LuauScript> (mismo .lua),
// por eso "abren el mismo script". Un proyecto recién abierto no dispara
// esto: cada nodo trae su id único persistido.
inline ScriptNodeBase* ScriptNodeBase::_gl_find_id_twin() {
    if (!get_tree() || !get_tree()->get_root()) return nullptr;
    ScriptNodeBase* found = nullptr;
    std::function<void(Node*)> rec = [&](Node* n) {
        if (found || !n) return;
        if (n != (Node*)this) {
            ScriptNodeBase* s = gl_cast<ScriptNodeBase>(n);
            if (s && !s->script_id.is_empty() && s->script_id == script_id) { found = s; return; }
        }
        for (int i = 0; i < n->get_child_count(); i++) rec(n->get_child(i));
    };
    rec(get_tree()->get_root());
    return found;
}

inline void gl_arranque_pedir(ScriptNodeBase* s);   // luau_script_arranque.h

inline void ScriptNodeBase::set_enabled(bool b) {
    script_enabled = b;
    if (script_enabled && !script_started && is_inside_tree()
        && !Engine::get_singleton()->is_editor_hint()) {
        gl_arranque_pedir(this);
    }
}

// ── Donde vive el contador de IDs ────────────────────────────────────
// TODO lo de la extension vive en res://GodotLuau/, asi que el contador
// tambien: en el proyecto del usuario solo tiene que aparecer esa carpeta.
// Si la carpeta no esta (proyecto a medio instalar), se cae a la raiz.
inline const String& ScriptNodeBase::_ids_path() {
    static String cached;
    if (cached.is_empty()) {
        cached = DirAccess::dir_exists_absolute("res://GodotLuau")
                     ? String("res://GodotLuau/.luau_ids.cfg")
                     : String(GL_IDS_PATH_LEGACY);
    }
    return cached;
}

// Contador secuencial por tipo en res://GodotLuau/.luau_ids.cfg
// (NO se usa project.godot: guardarlo hace que el editor recargue los plugins)
// Cacheado en memoria y con los handles SIEMPRE cerrados antes de reabrir:
// en Windows, leer y escribir el mismo archivo con el handle abierto
// produce "archivo en uso, bloqueado" cuando entran varios scripts a la vez.
inline int64_t ScriptNodeBase::_next_script_counter(const String& cls_lower) {
    static Dictionary counters;
    static bool loaded = false;
    const String path = _ids_path();

    if (!loaded) {
        loaded = true;
        // MIGRACION: hasta la 1.17 esto vivia en la raiz. Si el archivo nuevo
        // no existe todavia se leen los contadores del viejo. Sin esto, un
        // proyecto que ya tenia scripts vuelve a empezar en 1 y los ids
        // CHOCAN con los que ya estaban en la escena.
        String origen = path;
        if (!FileAccess::file_exists(origen) && FileAccess::file_exists(GL_IDS_PATH_LEGACY))
            origen = GL_IDS_PATH_LEGACY;
        Ref<FileAccess> f = FileAccess::open(origen, FileAccess::READ);
        if (f.is_valid()) {
            while (!f->eof_reached()) {
                String line = f->get_line().strip_edges();
                int eq = line.find("=");
                if (eq > 0) counters[line.substr(0, eq)] = line.substr(eq + 1).to_int();
            }
            f->close();  // cerrar ANTES de cualquier escritura posterior
        }
    }
    // Migración: contadores antiguos guardados en project.godot
    if (!counters.has(cls_lower)) {
        String legacy_key = "godot_luau/script_id_counter_" + cls_lower;
        if (ProjectSettings::get_singleton()->has_setting(legacy_key))
            counters[cls_lower] = (int64_t)ProjectSettings::get_singleton()->get_setting(legacy_key, 0);
    }
    int64_t next = (int64_t)counters.get(cls_lower, 0) + 1;
    counters[cls_lower] = next;

    Ref<FileAccess> w = FileAccess::open(path, FileAccess::WRITE);
    if (w.is_valid()) {
        Array keys = counters.keys();
        for (int i = 0; i < keys.size(); i++)
            w->store_line(String(keys[i]) + "=" + String::num_int64((int64_t)counters[keys[i]]));
        w->close();
    }
    return next;
}

inline void ScriptNodeBase::_notification(int p_what) {
    if (Engine::get_singleton()->is_editor_hint()) {
        if (p_what == NOTIFICATION_ENTER_TREE) {
            // ── Detección de DUPLICADO (Ctrl+D / copiar-pegar) ─────────
            // Si otro nodo vivo ya usa este script_id, este es una copia:
            // se le da identidad y archivo PROPIOS copiando el código actual
            // del original, para que dejen de "abrir el mismo script" y cada
            // uno ejecute sus funciones de forma independiente.
            if (!script_id.is_empty() && _gl_find_id_twin()) {
                if (codigo_luau.is_valid()) {
                    _gl_dup_source = codigo_luau->_get_source_code();
                    if (_gl_dup_source.is_empty() && !codigo_luau->get_path().is_empty()) {
                        Ref<FileAccess> rf = FileAccess::open(codigo_luau->get_path(), FileAccess::READ);
                        if (rf.is_valid()) { _gl_dup_source = rf->get_as_text(); rf->close(); }
                    }
                }
                _gl_is_dup = true;
                script_id = String();               // fuerza id nuevo abajo
                codigo_luau = Ref<LuauScript>();     // suelta el recurso compartido
            }

            // ── Asignar ID persistente la primera vez ──────────────────
            if (script_id.is_empty()) {
                String cls_id = get_class();
                script_id = cls_id + "_ID_" + String::num_int64(_next_script_counter(cls_id.to_lower()));
            }

            bool necesita_archivo = codigo_luau.is_null() ||
                (!codigo_luau->get_path().is_empty() &&
                 !FileAccess::file_exists(codigo_luau->get_path()));

            // ── Restauración (Ctrl+Z): papelera o memoria, sin crear recursos
            //    duplicados (evita "Another resource is loaded from path") ──
            if (necesita_archivo && codigo_luau.is_valid() && !codigo_luau->get_path().is_empty()) {
                String path  = codigo_luau->get_path();
                // Papelera en GodotLuau/ desde la 1.17; la de la raiz por si el
                // plugin aun no la ha migrado.
                String trash = "res://GodotLuau/.luau_trash/" + script_id + ".lua";
                if (!FileAccess::file_exists(trash)) trash = "res://.luau_trash/" + script_id + ".lua";
                Ref<DirAccess> root_dir = DirAccess::open("res://");
                if (root_dir.is_valid() && !root_dir->dir_exists(path.get_base_dir()))
                    root_dir->make_dir_recursive(path.get_base_dir());
                if (FileAccess::file_exists(trash)) {
                    // El archivo está en la papelera (nodo borrado + Ctrl+Z) → moverlo de vuelta
                    if (DirAccess::rename_absolute(
                            ProjectSettings::get_singleton()->globalize_path(trash),
                            ProjectSettings::get_singleton()->globalize_path(path)) == OK) {
                        GL_DEBUG_PRINT("[GodotLuau] Script restaurado de la papelera: ", path);
                        necesita_archivo = false;
                    }
                } else if (!codigo_luau->_get_source_code().is_empty()) {
                    // El recurso aún conserva el código en memoria → reescribirlo a disco
                    Ref<FileAccess> wf = FileAccess::open(path, FileAccess::WRITE);
                    if (wf.is_valid()) {
                        wf->store_string(codigo_luau->_get_source_code());
                        GL_DEBUG_PRINT("[GodotLuau] Script reescrito desde memoria: ", path);
                        necesita_archivo = false;
                    }
                }
            }

            if (necesita_archivo) {
                String cls = get_class();
                // Los scripts van DENTRO de GodotLuau/ (orden pedido por el
                // usuario): res://GodotLuau/LocalScripts, /ServerScripts, etc.
                String folder = "res://GodotLuau/" + cls + "s";
                Ref<DirAccess> dir = DirAccess::open("res://");
                if (dir.is_valid() && !dir->dir_exists(folder)) {
                    dir->make_dir_recursive(folder);
                }

                // El archivo se nombra por el ID → estable entre sesiones
                String file_path = folder + "/" + script_id + ".lua";

                Ref<LuauScript> new_script;
                new_script.instantiate();

                // ── Template selection based on node name ─────────────────
                // Fuente ÚNICA de la verdad: gl_builtin_template() (arriba).
                // La comparte el actualizador incremental (roblox_datamodel.h).
                //// ── Selección de template según nombre del nodo ────────────
                String node_name = get_name();
                String template_code = gl_builtin_template(node_name, cls);

                // Un duplicado hereda el código EXACTO del original (no un
                // template): así la copia conserva lo que el usuario escribió.
                bool is_dup = (_gl_is_dup && !_gl_dup_source.is_empty());
                if (is_dup)
                    template_code = _gl_dup_source;

                // Sustituir el ID único del script en la plantilla (para poder
                // localizarlo al buscar por su ID). Los templates de sistema
                // (Health/Animate…) no tienen el placeholder → quedan intactos.
                template_code = template_code.replace("%SCRIPT_ID%", script_id);
                new_script->_set_source_code(template_code);
                // take_over_path reclama la ruta aunque haya un recurso viejo cacheado
                // (evita "Another resource is loaded from path")
                new_script->take_over_path(file_path);
                ResourceSaver::get_singleton()->save(new_script, file_path);
                set_codigo_luau(new_script);
                // Registrar el hash "base" de fábrica (1.15): permite al
                // actualizador saber luego si el jugador tocó el archivo. Un
                // duplicado NO es plantilla de fábrica → no se registra base.
                if (!is_dup)
                    gl_tpl_base_set(script_id, template_code.md5_text());
                GL_DEBUG_PRINT("[GodotLuau] Script creado: ", file_path);
                if (_gl_is_dup)
                    GL_DEBUG_PRINT("[GodotLuau] Duplicado con identidad propia: ", script_id);
                _gl_is_dup = false;          // consumido: no repetir en re-entradas
                _gl_dup_source = String();
            }
        }
        // NOTA: el borrado/restauración del archivo al borrar el nodo (con soporte
        // de Ctrl+Z vía papelera res://GodotLuau/.luau_trash/) lo gestiona el addon
        // GodotLuauUpdater, que puede distinguir un borrado real de un cierre de escena.
    } else {
        if (p_what == NOTIFICATION_EXIT_TREE) {
            // ── REPARENTAR NO MATA EL SCRIPT ─────────────────────────────
            // Antes aqui se hacia lua_close() en el acto. Pero en Godot
            // mover un nodo (remove_child + add_child) dispara EXIT_TREE en
            // el nodo Y en todos sus descendientes — y eso es exactamente lo
            // que hace equipar una Tool (Backpack -> Character) o cualquier
            // `x.Parent = otroSitio`. La VM se cerraba PARA SIEMPRE y el
            // script quedaba muerto sin un solo error en consola. En Roblox
            // mover un Script NO lo detiene.
            //
            // Ahora el cierre se difiere: si al final del frame el nodo ha
            // vuelto al arbol (un reparent), se cancela y no se toca nada.
            _gl_cierre_pendiente = true;
            call_deferred(StringName("_gl_cerrar_vm_si_procede"));
            return;
        }
        if (p_what == GL_NOTIF_CERRAR_VM) {
            // OJO: al SALIR del arbol get_tree() ya puede ser nulo, y
            // ->get_root() sobre nulo tira el proceso. Pasaba de verdad: en
            // cuanto un script del juego destruia nodos con scripts dentro,
            // GodotLuau se caia aqui (segfault tras "Parameter data.tree is null").
            // is_inside_tree() ANTES de get_tree(): en el cierre diferido el
            // nodo ya esta fuera del arbol y get_tree() a secas imprime
            // "Parameter data.tree is null" en cada script que se destruye.
            SceneTree* _st = is_inside_tree() ? get_tree() : nullptr;
            if (L_main && _st && _st->get_root()) {
                Node* root = _st->get_root();
                // Clean RunService callbacks
                for (int _ri = 0; _ri < root->get_child_count(); _ri++) {
                    Node* game = root->get_child(_ri);
                    if (!game) continue;
                    Node* rs_node = game->get_node_or_null("RunService");
                    if (rs_node) {
                        RunService* rs = gl_cast<RunService>(rs_node);
                        if (rs) rs->remove_by_state(L_main);
                    }
                }
                // Clean Remote* callbacks — traverse entire tree
                std::function<void(Node*)> cleanup_remotes = [&](Node* nd) {
                    if (!nd) return;
                    if (RemoteEventNode*    re = gl_cast<RemoteEventNode>(nd))    re->cleanup_state(L_main);
                    if (RemoteFunctionNode* rf = gl_cast<RemoteFunctionNode>(nd)) rf->cleanup_state(L_main);
                    if (BindableEventNode*  be = gl_cast<BindableEventNode>(nd))  be->cleanup_state(L_main);
                    for (int _ci = 0; _ci < nd->get_child_count(); _ci++)
                        cleanup_remotes(nd->get_child(_ci));
                };
                cleanup_remotes(root);
            }

            // Peticiones HTTP a medias: se sueltan los sockets (las refs las
            // suelta el contexto con las demas del script).
            _gl_soltar_vm();
        }
    }
}

// Lo que antes era lua_close() de la VM del script (F8): el contexto sigue
// vivo para los demas. Las refs que crearon los hilos de este script (tareas,
// esperas, relays, manejadores de señales...) las suelta de golpe
// gl_contexto_soltar_hilo, porque son suyas; su hilo base queda anclado en el
// cementerio del contexto para que nadie reutilice su direccion.
inline void ScriptNodeBase::_gl_soltar_vm() {
    if (L_main) {
        if (gl_contexto_por_id(_gl_ctx_id)) {
            gl_run_close_cbs_for(L_main);  // BindToClose: guardar datos antes de soltar
            std::vector<LuauPendingResume>& pend = get_pending_resumes();
            for (int i = (int)pend.size() - 1; i >= 0; --i)
                if (pend[i].main_L == L_main) pend.erase(pend.begin() + i);
            // Su hilo principal suele quedarse cedido para siempre (un `while
            // true do task.wait() end`): con todo lo que tenga en la pila. Antes
            // lo liberaba lua_close; ahora se vacia y se suelta del hilo base,
            // que es lo unico que lo ancla.
            if (!_gl_es_sistema) {
                const int st = L_thread ? lua_costatus(L_main, L_thread) : LUA_COFIN;
                if (st == LUA_COSUS) lua_resetthread(L_thread);
                // Corriendo (o reanudando a otro): se queda anclado.
                if (st == LUA_COSUS || st == LUA_COFIN || st == LUA_COERR) lua_settop(L_main, 0);
            }
            gl_contexto_soltar_hilo(_gl_ctx_id, L_main);
        }
        L_main = nullptr;
        L_thread = nullptr;
    }
    spawned_threads.clear();
    waiting_children.clear();
    waiting_invokes.clear();
    waiting_https.clear();
    relays.clear();
    script_finished = true;
    is_waiting = false;
}

// Lo llama el deferred del EXIT_TREE. Si el nodo volvio al arbol (un
// reparent, p.ej. equipar una Tool), NO se cierra nada: el script sigue
// corriendo como en Roblox.
inline void ScriptNodeBase::_gl_cerrar_vm_si_procede() {
    if (!_gl_cierre_pendiente) return;
    _gl_cierre_pendiente = false;
    if (is_inside_tree()) return;              // volvio: era un reparent
    _notification(GL_NOTIF_CERRAR_VM);         // ahora si: teardown real
}

inline void ScriptNodeBase::resume() {
    if (!L_thread || !L_main || !gl_state_alive(L_main)) return;
    // Igual que arriba: si el hilo principal del script ya no esta
    // suspendido, reanudarlo casca dentro de la VM.
    if (lua_costatus(L_main, L_thread) != LUA_COSUS) {
        script_finished = true;
        is_waiting = false;
        return;
    }
    // wait() de Roblox devuelve (esperado, tiempoDeJuego). Si lo reprogramo
    // task.defer/delay, en cambio, recibe los argumentos que le dieron.
    int _nres = 0;
    if (main_start_args > 0) {
        _nres = main_start_args;
        main_start_args = 0;
        main_waited = 0.0;
    } else if (main_waited > 0.0) {
        lua_pushnumber(L_thread, main_waited);
        lua_pushnumber(L_thread, _gl_game_time());
        _nres = 2;
        main_waited = 0.0;
    }
    main_resched = false;
    _main_tras_resume(lua_resume(L_thread, nullptr, _nres));
}

inline void ScriptNodeBase::_gl_rel_conectar(Object* o, const char* sig, const Callable& c) {
    if (o && o->has_signal(sig) && !o->is_connected(sig, c)) o->connect(sig, c);
}

// Devuelve el id de la conexion (0 si el objeto ya no existe).
inline int ScriptNodeBase::_gl_relay_connect(int64_t target_id, int kind, int ref) {
    Node* tgt = gl_cast<Node>(ObjectDB::get_instance((uint64_t)target_id));
    if (!tgt) return 0;
    const int id = ++relay_siguiente;
    relays.push_back({ (uint64_t)target_id, (uint8_t)kind, ref, id });
    switch (kind) {
        case GL_REL_HIJO_ENTRA:
            _gl_rel_conectar(tgt, "child_entered_tree", Callable(this, "_gl_rel_hijo").bind(target_id, kind));
            break;
        case GL_REL_HIJO_SALE:
            _gl_rel_conectar(tgt, "child_exiting_tree", Callable(this, "_gl_rel_hijo").bind(target_id, kind));
            break;
        case GL_REL_DESC_ENTRA:
        case GL_REL_DESC_SALE: {
            SceneTree* st = tgt->is_inside_tree() ? tgt->get_tree() : (is_inside_tree() ? get_tree() : nullptr);
            _gl_rel_conectar(st, kind == GL_REL_DESC_ENTRA ? "node_added" : "node_removed",
                             Callable(this, "_gl_rel_desc").bind(kind));
        } break;
        case GL_REL_DESTRUYE:
            _gl_rel_conectar(tgt, "tree_exiting", Callable(this, "_gl_rel_destruye").bind(target_id));
            break;
        case GL_REL_ANCESTROS:
            _gl_rel_conectar(tgt, "tree_entered", Callable(this, "_gl_rel_ancestros").bind(target_id, 0));
            _gl_rel_conectar(tgt, "tree_exiting", Callable(this, "_gl_rel_ancestros").bind(target_id, 1));
            break;
    }
    return id;
}

inline void ScriptNodeBase::_gl_relay_disconnect(int id) {
    for (int i = (int)relays.size() - 1; i >= 0; --i) {
        if (relays[i].id != id) continue;
        if (L_main && gl_state_alive(L_main)) gl_unref(L_main, relays[i].ref);
        relays.erase(relays.begin() + i);
        return;
    }
}

// Corre el manejador `r` con hasta dos instancias como argumentos (por
// ObjectID: un manejador anterior pudo destruirlas). No corre si un
// manejador anterior lo desconecto, como en Roblox.
inline void ScriptNodeBase::_gl_rel_uno(const GLRelay& r, int nargs, uint64_t id1, uint64_t id2) {
    if (!L_main || !gl_state_alive(L_main)) return;
    bool sigue = false;
    for (const GLRelay& x : relays) if (x.id == r.id) { sigue = true; break; }
    if (!sigue) return;
    lua_State* th = lua_newthread(L_main);
    lua_rawgeti(L_main, LUA_REGISTRYINDEX, r.ref);
    if (lua_isfunction(L_main, -1)) {
        lua_xmove(L_main, th, 1);
        if (nargs >= 1) wrap_node(th, id1 ? gl_cast<Node>(ObjectDB::get_instance(id1)) : nullptr);
        if (nargs >= 2) wrap_node(th, id2 ? gl_cast<Node>(ObjectDB::get_instance(id2)) : nullptr);
        gl_resume_handler(th, nargs);
    } else {
        lua_pop(L_main, 1);
    }
    lua_pop(L_main, 1);
}

// Todos los manejadores de (objeto, señal). Se recorre una COPIA: un
// manejador puede conectar o desconectar y realojar `relays`.
inline void ScriptNodeBase::_gl_rel_disparar(uint64_t target_id, uint8_t kind, int nargs, Node* a1,
    Node* a2) {
    if (relays.empty()) return;
    std::vector<GLRelay> copia;
    for (const GLRelay& r : relays) if (r.target_id == target_id && r.kind == kind) copia.push_back(r);
    const uint64_t id1 = a1 ? (uint64_t)a1->get_instance_id() : 0;
    const uint64_t id2 = a2 ? (uint64_t)a2->get_instance_id() : 0;
    for (const GLRelay& r : copia) _gl_rel_uno(r, nargs, id1, id2);
}

inline bool ScriptNodeBase::_gl_va_a_morir(Node* n) {
    for (Node* a = n; a; a = a->get_parent())
        if (a->is_queued_for_deletion()) return true;
    return false;
}

inline void ScriptNodeBase::_gl_rel_hijo(Node* hijo, int64_t target_id, int kind) {
    if (hijo) _gl_rel_disparar((uint64_t)target_id, (uint8_t)kind, 1, hijo, nullptr);
}

// DescendantAdded/Removing: en Roblox saltan por CUALQUIER descendiente, a
// la profundidad que sea, no solo por los hijos directos. Godot no tiene esa
// señal por nodo, pero el SceneTree avisa de TODO nodo que entra o sale: se
// escucha ahi y se filtra subiendo por los padres del nodo hasta dar con el
// que escucha. Son unos pocos saltos de puntero, y solo se paga si alguien
// conecto la señal.
inline void ScriptNodeBase::_gl_rel_desc(Node* nodo, int kind) {
    gl_paso_stats().desc_llamadas++;
    if (!nodo || relays.empty()) return;
    std::vector<GLRelay> copia;
    for (const GLRelay& r : relays) if (r.kind == kind) copia.push_back(r);
    const uint64_t nodo_id = (uint64_t)nodo->get_instance_id();
    for (const GLRelay& r : copia) {
        Node* n = gl_cast<Node>(ObjectDB::get_instance(nodo_id));
        Node* raiz = gl_cast<Node>(ObjectDB::get_instance(r.target_id));
        if (!n || !raiz || raiz == n) continue;
        bool dentro = false;
        for (Node* a = n->get_parent(); a; a = a->get_parent())
            if (a == raiz) { dentro = true; break; }
        if (!dentro) continue;
        gl_paso_stats().desc_despachos++;
        _gl_rel_uno(r, 1, nodo_id, 0);
    }
}

// Destroying: tree_exiting tambien salta al cambiar de Parent, y en Roblox
// eso NO dispara Destroying; por eso se mira que el nodo (o un ancestro)
// vaya a liberarse. Sin argumentos.
inline void ScriptNodeBase::_gl_rel_destruye(int64_t target_id) {
    Node* tgt = gl_cast<Node>(ObjectDB::get_instance((uint64_t)target_id));
    if (tgt && _gl_va_a_morir(tgt)) _gl_rel_disparar((uint64_t)target_id, GL_REL_DESTRUYE, 0, nullptr, nullptr);
}

// AncestryChanged(instancia, padre): al entrar en el arbol con su padre
// nuevo (fase 0). Al salir (fase 1) puede ser un cambio de Parent, que
// vuelve a entrar enseguida, o quedarse fuera: se mira en diferido (fase 2)
// y solo si sigue fuera salta con nil. Si va a destruirse, salta ya. Como
// primer argumento va la propia instancia aunque se moviera un ancestro.
inline void ScriptNodeBase::_gl_rel_ancestros(int64_t target_id, int fase) {
    Node* tgt = gl_cast<Node>(ObjectDB::get_instance((uint64_t)target_id));
    if (!tgt) return;
    if (fase == 0) {
        _gl_rel_disparar((uint64_t)target_id, GL_REL_ANCESTROS, 2, tgt, tgt->get_parent());
    } else if (fase == 1) {
        if (_gl_va_a_morir(tgt)) _gl_rel_disparar((uint64_t)target_id, GL_REL_ANCESTROS, 2, tgt, nullptr);
        else call_deferred("_gl_rel_ancestros", target_id, 2);
    } else if (!tgt->is_inside_tree()) {
        _gl_rel_disparar((uint64_t)target_id, GL_REL_ANCESTROS, 2, tgt, nullptr);
    }
}

// Donde esta parado un hilo (el primer marco Luau de su pila): "Script:linea".
// Solo para la autopsia: dice que bucle de que modulo se lleva el frame.
static inline String gl_aut_donde(lua_State* th) {
    lua_Debug ar;
    for (int nivel = 0; nivel < 6; nivel++) {
        if (!lua_getinfo(th, nivel, "sl", &ar)) break;
        if (ar.currentline > 0)
            return String::utf8(ar.short_src) + ":" + itos(ar.currentline) + " (fn " + itos(ar.linedefined) + ")";
    }
    return "terminado";
}
// Lo mismo ANTES de reanudarlo: un hilo sin empezar (task.spawn/defer/delay)
// tiene su funcion en la pila debajo de sus `nargs` argumentos; al acabar ya no
// queda rastro de donde venia.
static inline String gl_aut_donde_antes(lua_State* th, bool empezado, int nargs) {
    if (!empezado) {
        lua_Debug ar;
        if (lua_getinfo(th, -(nargs + 1), "sl", &ar))
            return String::utf8(ar.short_src) + ":" + itos(ar.linedefined) + " (tarea)";
    }
    return gl_aut_donde(th);
}

inline void ScriptNodeBase::_process(double delta) {
    GLAutTiempo _aut_script(AUT_SCRIPT);
    const uint64_t _gl_t0 = Time::get_singleton()->get_ticks_usec();
    struct _GLCronometro {
        uint64_t t0; uint64_t* acc;
        ~_GLCronometro() { *acc += Time::get_singleton()->get_ticks_usec() - t0; }
    } _gl_crono{ _gl_t0, &_gl_cpu_us };
    // Con GL_AUTOPSIA=1, el tiempo de los hilos de ESTE script va a la misma
    // tabla que los callbacks ([AUT-CB]): sin esto solo se sabia que "SCRIPTS"
    // se llevaba 95 ms, no de quien eran.
    struct _GLAutHilos {
        ScriptNodeBase* s; uint64_t t0;
        ~_GLAutHilos() {
            if (!gl_aut_on()) return;
            const uint64_t us = Time::get_singleton()->get_ticks_usec() - t0;
            if (us >= 20) gl_aut_cb_anota(String("[script] ") + String(s->get_path()), us);
        }
    } _gl_aut_hilos{ this, _gl_t0 };
    // Si la VM de ESTE script ya se cerro (script destruido, recargado, o
    // sacado del arbol), reanudar sus corrutinas es un use-after-free:
    // lua_resume sobre un lua_State liberado casca DENTRO de la VM de Luau.
    // Era la causa del segfault intermitente del place real: aparecia solo
    // cuando algun script moria en ese frame (por error() o porque el juego
    // destruia nodos), y por eso salia 1 de cada 3 veces.
    // El resto del puente ya comprobaba gl_state_alive antes de invocar
    // refs; este camino era el que faltaba.
    if (!L_main || !gl_state_alive(L_main)) {
        spawned_threads.clear();
        waiting_https.clear();   // suelta los HTTPClient que quedaran en vuelo
        return;
    }

    // ── 1. Main thread timer ────────────────────────────────────────
    if (!script_finished && is_waiting && !is_external_wait) {
        wait_timer -= delta;
        main_waited += delta;
        if (wait_timer <= 0.0) {
            is_waiting = false;
            resume();
        }
    }

    // ── 2. Spawned threads ──────────────────────────────────────────
    for (int i = (int)spawned_threads.size() - 1; i >= 0; i--) {
        // La lista puede CRECER o ENCOGER mientras se recorre (ver abajo).
        if (i >= (int)spawned_threads.size()) continue;
        if (spawned_threads[i].timer < 0.0) continue;  // suspendida por condicion externa
        spawned_threads[i].timer  -= delta;
        spawned_threads[i].waited += delta;
        if (spawned_threads[i].timer > 0.0) continue;

        // ── Todo lo que se necesita se copia POR VALOR antes de reanudar ──
        //
        // ESTO ERA EL SEGFAULT INTERMITENTE del place real (1 de cada 3
        // partidas). lua_resume EJECUTA codigo Lua, y ese codigo puede
        // llamar a task.spawn / task.delay / task.defer, que hacen push_back
        // en ESTE MISMO vector y lo REALOCAN. La version anterior guardaba
        // una referencia (SpawnedThread& st) y despues de reanudar escribia
        // en ella: memoria ya liberada. Se corrompia st.L y al frame
        // siguiente lua_resume sobre ese puntero basura cascaba DENTRO de la
        // VM de Luau (por eso el backtrace acababa en luau_execute y no
        // señalaba a ningun sitio nuestro).
        lua_State* thL      = spawned_threads[i].L;
        const bool was_started = spawned_threads[i].started;
        const int  start_args  = spawned_threads[i].start_args;
        const double waited    = spawned_threads[i].waited;
        spawned_threads[i].waited  = 0.0;
        spawned_threads[i].started = true;

        // 1er resume de task.delay/defer: pasar sus args; despues, los
        // valores de retorno de wait() -> (esperado, tiempoDeJuego).
        int na = was_started ? 0 : start_args;
        if (was_started && waited > 0.0) {
            lua_pushnumber(thL, waited);
            lua_pushnumber(thL, _gl_game_time());
            na = 2;
        }
        // SOLO se puede reanudar una corrutina SUSPENDIDA. Reanudar una que
        // ya termino (o que murio por un error, o que esta corriendo) es
        // comportamiento indefinido: Luau no lo valida y el proceso se cae
        // DENTRO de luau_execute, sin apuntar a nada nuestro. Es lo que
        // pasaba de forma intermitente en el place real.
        const int co_st = lua_costatus(L_main, thL);
        if (co_st != LUA_COSUS) {
            if (L_main) gl_unref(L_main, spawned_threads[i].ref);
            spawned_threads.erase(spawned_threads.begin() + i);
            continue;
        }
        // Mientras corre queda "suspendida": si cede sin valores
        // (coroutine.yield()) no vuelve sola el frame siguiente.
        spawned_threads[i].timer = -1.0;
        const String _donde = gl_aut_on() ? gl_aut_donde_antes(thL, was_started, na) : String();
        const uint64_t _t_hilo = gl_aut_on() ? Time::get_singleton()->get_ticks_usec() : 0;
        const int status = lua_resume(thL, nullptr, na);
        if (_t_hilo) gl_aut_cb_anota(String("[hilo] ") + _donde, Time::get_singleton()->get_ticks_usec() - _t_hilo);
        // _after_resume la vuelve a buscar POR PUNTERO de hilo: el indice
        // puede haber cambiado si el Lua reanudado creo o cancelo corrutinas.
        _after_resume(thL, status);
    }

    // ── 3. Poll WaitForChild entries ────────────────────────────────
    for (int i = (int)waiting_children.size()-1; i >= 0; --i) {
        WaitingChild& wc = waiting_children[i];
        // Un hilo cancelado (task.cancel) o muerto ya no espera a nadie: se
        // suelta sin reanudarlo y sin el aviso de "Infinite yield".
        if (lua_costatus(L_main, wc.thread) != LUA_COSUS) {
            if (wc.main_L) gl_unref(wc.main_L, wc.ref);
            waiting_children.erase(waiting_children.begin() + i);
            continue;
        }
        wc.elapsed += delta;
        Node* found = nullptr;
        // Resolver el padre por ObjectID: nullptr si fue destruido (sin UAF).
        Node* parent = gl_cast<Node>(ObjectDB::get_instance(wc.parent_id));
        bool parent_gone = (wc.parent_id != 0 && parent == nullptr);
        if (parent && parent->is_inside_tree()) {
            for (int ci = 0; ci < parent->get_child_count(); ci++) {
                Node* ch = parent->get_child(ci);
                if (ch && ch->get_name() == StringName(wc.child_name)) { found = ch; break; }
            }
        }
        // Warning de Roblox: WaitForChild sin timeout que lleva 5s sin
        // resolverse — el clasico script colgado "en silencio".
        if (!found && !parent_gone && wc.timeout <= 0 && !wc.warned && wc.elapsed >= 5.0) {
            wc.warned = true;
            String pname = parent ? gl_instance_fullname(parent) : String(get_name());
            UtilityFunctions::print_rich(String("[color=") + GL_WARN_COLOR +
                "]Infinite yield possible on '" + pname + ":WaitForChild(\"" + wc.child_name + "\")'[/color]");
            UtilityFunctions::print_rich(gl_format_roblox_stack(wc.trace));
        }
        // Si el padre desaparece, no dejamos la corrutina colgada para siempre.
        bool timed_out = (wc.timeout > 0 && wc.elapsed >= wc.timeout) || parent_gone;
        if (found || timed_out) {
            if (found) { wrap_node(wc.thread, found); resume_external_thread(wc.thread, 1); }
            else       { lua_pushnil(wc.thread);       resume_external_thread(wc.thread, 1); }
            if (wc.main_L) gl_unref(wc.main_L, wc.ref);
            waiting_children.erase(waiting_children.begin() + i);
        }
    }

    // ── 4. Drain shared pending-resume queue (Signal:Wait etc.) ────
    auto& pending = get_pending_resumes();
    for (int i = (int)pending.size()-1; i >= 0; --i) {
        LuauPendingResume& pr = pending[i];
        // Only process entries that belong to this ScriptNode's main state
        if (pr.main_L != L_main) continue;
        if (pr.use_args) {   // RemoteEvent:Wait → devolver los args del evento
            Node* ctx = pr.ctx_id ? gl_cast<Node>(ObjectDB::get_instance(pr.ctx_id)) : nullptr;
            int n = gl_net_decode_args(pr.thread, pr.args, ctx);
            resume_external_thread(pr.thread, n);
        }
        else if (pr.node_arg) { wrap_node(pr.thread, pr.node_arg); resume_external_thread(pr.thread, 1); }
        else if (pr.dos_valores) {   // Stepped:Wait() -> (tiempo, delta)
            lua_pushnumber(pr.thread, pr.delta2);
            lua_pushnumber(pr.thread, pr.delta);
            resume_external_thread(pr.thread, 2);
        }
        else                  { lua_pushnumber(pr.thread, pr.delta); resume_external_thread(pr.thread, 1); }
        pending.erase(pending.begin() + i);
    }

    // ── 5. Respuestas de RemoteFunction por red (InvokeServer/Client) ──
    if (!waiting_invokes.empty()) {
        auto& resp = gl_net_responses();
        Node* nsctx = gl_net_service_id() ? gl_cast<Node>(ObjectDB::get_instance(gl_net_service_id())) : nullptr;
        for (int i = (int)waiting_invokes.size()-1; i >= 0; --i) {
            waiting_invokes[i].elapsed += delta;
            auto it = resp.find(waiting_invokes[i].call_id);
            bool ready = (it != resp.end() && it->second.ready);
            bool timed = (waiting_invokes[i].timeout > 0 && waiting_invokes[i].elapsed >= waiting_invokes[i].timeout);
            if (!ready && !timed) {
                // Roblox no tiene timeout: se espera indefinidamente y solo se
                // avisa. Si el receptor se cae, el NetworkService deposita el
                // fallo (no hace falta rendirse por tiempo).
                if (!waiting_invokes[i].warned && waiting_invokes[i].elapsed >= 10.0) {
                    waiting_invokes[i].warned = true;
                    UtilityFunctions::push_warning("Infinite yield possible on RemoteFunction invoke");
                }
                continue;
            }
            // Sacar el registro ANTES de reanudar: el hilo reanudado puede volver
            // a invocar y hacer push_back, invalidando la referencia y el índice.
            WaitingInvoke wi = waiting_invokes[i];
            waiting_invokes.erase(waiting_invokes.begin() + i);
            bool  ok = true;
            Array rets;
            GLNetResponse local;   // respuesta de un manejador de esta maquina (gl_invocar.h)
            if (ready) {
                ok = it->second.ok;
                rets = it->second.rets;
                if (it->second.local_L) local = it->second;
                resp.erase(it);
            }
            else       { ok = false; rets.push_back(String("RemoteFunction invoke timed out")); }
            if (wi.main_L) gl_unref(wi.main_L, wi.ref);
            if (ok && local.local_L) {
                int n = gl_inv_empujar(wi.thread, local);
                resume_external_thread(wi.thread, n);
            } else if (ok) {
                int n = gl_net_decode_args(wi.thread, rets, nsctx);
                resume_external_thread(wi.thread, n);
            } else {
                // El error del handler remoto se RE-LANZA aquí (como Roblox).
                String msg = rets.size() > 0 ? String(rets[0]) : String("RemoteFunction invocation failed");
                resume_external_thread_error(wi.thread, msg);
            }
        }
    }

    // ── 6. Peticiones HTTP en vuelo (HttpService) ──────────────────────
    // Se avanza cada una un poco. Cuando una termina se saca de la lista
    // ANTES de reanudar: el hilo reanudado puede lanzar otra peticion y
    // hacer push_back, que realoca el vector.
    for (int i = (int)waiting_https.size()-1; i >= 0; --i) {
        if (i >= (int)waiting_https.size()) continue;
        waiting_https[i].waited += delta;
        if (!gl_http_step(waiting_https[i].req, delta)) {
            // Mismo aviso que en WaitForChild: si algo se queda colgado, que
            // se vea en la consola en vez de "no pasa nada y no se por que".
            if (!waiting_https[i].warned && waiting_https[i].waited >= 10.0) {
                waiting_https[i].warned = true;
                UtilityFunctions::push_warning(
                    "Infinite yield possible on HttpService request to " + waiting_https[i].req.spec.host);
            }
            continue;
        }
        WaitingHttp wh = waiting_https[i];
        waiting_https.erase(waiting_https.begin() + i);
        if (wh.main_L) gl_unref(wh.main_L, wh.ref);
        int n = gl_http_push_result(wh.thread, wh.req.res, wh.ret_mode);
        resume_external_thread(wh.thread, n);
    }
}

inline void ScriptNodeBase::_ready() {
    // A la cola de arranque (luau_script_arranque.h): por turnos y repartida
    // entre frames, no todos los scripts del place en el mismo.
    if (!Engine::get_singleton()->is_editor_hint()) gl_arranque_pedir(this);
}

#endif // LUAU_SCRIPT_CICLO_H

#ifndef GL_RUNTIME_H
#define GL_RUNTIME_H

// ════════════════════════════════════════════════════════════════════
//  Seguridad de vida para el puente Luau <-> Godot
//  ----------------------------------------------------------------
//  1) Registro de estados Luau vivos: cada ScriptNodeBase registra su
//     lua_State principal al crearlo y lo da de baja ANTES de cerrarlo.
//     Cualquier despacho de señal/callback comprueba gl_state_alive()
//     para no invocar refs sobre una VM ya cerrada (use-after-free al
//     destruir o recargar un script).
//
//  2) Referencias a nodos por ObjectID: el wrapper de Lua guarda el
//     ObjectID del nodo, NO un puntero crudo. gow_node() resuelve y
//     valida contra ObjectDB, devolviendo nullptr si el nodo fue
//     destruido (igual que Roblox: acceder a un objeto destruido es
//     seguro y no crashea).
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/templates/local_vector.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/templates/hash_map.hpp>

#include "lua.h"
#include "lualib.h"
#include "gl_contexto.h"   // una VM por contexto (servidor/cliente), F8
#ifdef GL_LUAU_CODEGEN
#include "luacodegen.h"
#endif

#include <unordered_set>
#include <vector>
#include <cstdint>

using namespace godot;

// ── Menú nativo vs. menú Lua (1.15) ──────────────────────────────────────────
//  El menú de Escape es un módulo Luau EDITABLE (Modules/Menu). El menú nativo
//  de C++ ya no existe: se borró en la 1.17 para que solo haya UNO, y para que
//  sea el que el usuario puede leer y cambiar. Esta bandera se queda porque
//  game:SetNativeMenuEnabled() sigue siendo API de Roblox y algún place la
//  llama; hoy solo guarda el valor.
inline bool& gl_native_menu_flag() { static bool v = true; return v; }
inline void  gl_set_native_menu_enabled(bool b) { gl_native_menu_flag() = b; }
inline bool  gl_native_menu_enabled() { return gl_native_menu_flag(); }

// ── Estado de input cross-device compartido ──────────────────────────────────
//  La UI tactil (joystick + boton de salto, creada por RobloxPlayer cuando hay
//  pantalla tactil) ESCRIBE aqui; el Humanoid LEE de aqui para mover el
//  personaje. Asi el movimiento funciona en PC, movil y consola con un solo
//  camino, igual que el moveVector del ControlModule de Roblox.
//  `inline` (enlace externo) = una sola instancia compartida entre TUs.
struct GLMobileInput {
    Vector2 move;            // joystick virtual: x = derecha, y = adelante (-1..1)
    bool    jump   = false;  // boton de salto en pantalla mantenido
    bool    active = false;  // hay controles tactiles en pantalla
};
inline GLMobileInput& gl_mobile() { static GLMobileInput s; return s; }

// ── Raton de UserInputService (MouseBehavior / MouseIconEnabled) ─────────────
//  Lo escribe el servicio y lo respeta la camara: al soltar el boton derecho
//  o salir de primera persona, la camara ponia el raton VISIBLE aunque un
//  script lo tuviera bloqueado (LockCenter del shift lock).
struct GLRaton {
    int  comportamiento = 0;   // Enum.MouseBehavior: 0 Default, 1 LockCenter, 2 LockCurrentPosition
    bool icono = true;         // MouseIconEnabled
};
inline GLRaton& gl_raton() { static GLRaton r; return r; }
// El modo del raton cuando nadie esta arrastrando la camara.
inline void gl_raton_aplicar() {
    godot::Input* in = godot::Input::get_singleton();
    if (!in) return;
    const GLRaton& r = gl_raton();
    if (r.comportamiento != 0) in->set_mouse_mode(godot::Input::MOUSE_MODE_CAPTURED);
    else if (!r.icono)         in->set_mouse_mode(godot::Input::MOUSE_MODE_HIDDEN);
    else                       in->set_mouse_mode(godot::Input::MOUSE_MODE_VISIBLE);
}

// ── Vista de Servidor (cámara libre de debug, como en Roblox Studio) ─────────
//  Cuando está activa, el Humanoid y el RobloxPlayer IGNORAN el input: WASD y
//  el mouse mueven la cámara libre, no al personaje. La activa el botón
//  "Server View" que crea el NetworkService en sesiones multijugador.
struct GLFreecamState {
    bool active = false;
};
inline GLFreecamState& gl_freecam() { static GLFreecamState s; return s; }

// ── Menú abierto (Esc) ───────────────────────────────────────────────────────
//  Cuando el menú del juego está abierto, el jugador NO se mueve ni la rueda del
//  ratón hace zoom: así la rueda desplaza el menú (como en Roblox). Lo activan
//  lo pone el menu de Luau con Workspace:SetMenuOpen; lo leen el personaje y el Humanoid.
inline bool& gl_menu_open_ref() { static bool v = false; return v; }
inline void  gl_set_menu_open(bool b) { gl_menu_open_ref() = b; }
inline bool  gl_menu_open() { return gl_menu_open_ref(); }

// ── Flush de física en curso ─────────────────────────────────────────────────
//  true mientras un handler de señal de FÍSICA (Touched/TouchEnded) ejecuta
//  código Lua. Godot no permite sacar un CollisionObject del árbol durante el
//  flush de física (collision_object_3d.cpp:119: un ERROR por nodo y
//  comportamiento indefinido — medido: 1.190 errores en una partida del place
//  de prueba, los "tirones" cronometrables). Destroy / ClearAllChildren /
//  .Parent lo consultan para diferir el remove_child al final del frame.
inline bool& gl_en_callback_fisica() { static bool v = false; return v; }

// ── Controles apagados por script (PlayerModule:GetControls():Disable()) ─────
//  Es EL idioma de Roblox para congelar al jugador: volar, cinemáticas,
//  tutoriales, minijuegos. Lo leen RobloxPlayer y Humanoid igual que el menú.
inline bool& gl_controls_disabled_ref() { static bool v = false; return v; }
inline void  gl_set_controls_disabled(bool b) { gl_controls_disabled_ref() = b; }
inline bool  gl_controls_disabled() { return gl_controls_disabled_ref(); }

// ── Archivos internos del proyecto ───────────────────────────────────────────
//  Desde la 1.17 viven en res://GodotLuau/ y no sueltos en la raiz. Los que
//  escribe el plugin cambian de sitio sin mas (van juntos con la DLL). Los que
//  pone el usuario a mano (.gl_host, gl_match.cfg) se buscan tambien en la
//  raiz, que es donde los dejaba la 1.16.
#define GL_MP_SESSION_PATH "res://GodotLuau/.gl_mp_session"
#define GL_VIEW_CMD_PATH   "res://GodotLuau/.gl_view_cmd"
inline godot::Ref<godot::FileAccess> gl_open_user_config(const char* nombre) {
    godot::Ref<godot::FileAccess> f = godot::FileAccess::open(godot::String("res://GodotLuau/") + nombre, godot::FileAccess::READ);
    if (f.is_null()) f = godot::FileAccess::open(godot::String("res://") + nombre, godot::FileAccess::READ);
    return f;
}

// ── Dispositivo emulado (PC / Mobile / Console / VR) ─────────────────────────
//  Elegido en la barra "Players" del editor. Llega por --gldevice (ventanas
//  cliente) o por res://GodotLuau/.gl_mp_session "count|device|stamp" (ventana nativa).
//  Lo leen RobloxPlayer (controles tactiles en Mobile) y NetworkService
//  (tamano de ventana, vista VR, etc.).
inline godot::String gl_emulated_device() {
    godot::PackedStringArray a = godot::OS::get_singleton()->get_cmdline_user_args();
    for (int i = 0; i < a.size(); i++)
        if (godot::String(a[i]) == "--gldevice" && i + 1 < a.size()) return a[i + 1];
    godot::Ref<godot::FileAccess> f = godot::FileAccess::open(GL_MP_SESSION_PATH, godot::FileAccess::READ);
    if (f.is_valid()) {
        godot::PackedStringArray parts = godot::String(f->get_as_text()).strip_edges().split("|");
        f->close();
        if (parts.size() >= 2) return parts[1];
    }
    return godot::String("PC");
}

// ── Wrapper de instancia: ObjectID en lugar de Node* crudo ───────────────────
// El ObjectID es lo que manda: si el nodo murio, ObjectDB ya no lo da. `motor` y
// `nodo` son el nodo ya resuelto la ultima vez (el objeto del motor y su enlace
// de godot-cpp): si el vivo de ese id es el mismo `motor`, el enlace es el de
// siempre y no hay que volver a buscarlo. Se resolvia en cada acceso del puente
// (varias veces por acceso), con dos llamadas al motor y sus cerrojos.
struct GodotObjectWrapper {
    uint64_t obj_id;   // ObjectID del nodo (0 = ninguno)
    void* motor;       // el objeto del motor cuando se apunto (solo para comparar)
    Node* nodo;        // su enlace; vale si el vivo de obj_id es `motor`
};

// Resuelve el nodo validando contra ObjectDB. nullptr si fue destruido.
inline Node* gow_node(const GodotObjectWrapper* w) {
    if (!w || w->obj_id == 0) return nullptr;
    GDExtensionObjectPtr eng = godot::internal::gdextension_interface_object_get_instance_from_id(w->obj_id);
    if (!eng) return nullptr;
    if (w->motor == eng) return w->nodo;
    return gl_cast<Node>(godot::internal::get_object_instance_binding(eng));
}

inline void gow_set(GodotObjectWrapper* w, Node* n) {
    if (!w) return;
    w->obj_id = n ? (uint64_t)n->get_instance_id() : 0;
    w->motor = n ? (void*)n->_owner : nullptr;
    w->nodo = n;
}

// Envuelve un nodo para Luau: UN wrapper por instancia y por VM, en una cache
// de valores debiles (ver wrap_node en luau_api.h, que llama aqui). Esta aqui
// para que lo usen tambien las clases que se compilan antes del puente: un
// wrapper nuevo en cada aviso rompe la identidad (`t == miPista`, tablas por
// instancia).
inline void gl_wrap_node(lua_State* L, godot::Node* node) {
    if (!node) { lua_pushnil(L); return; }
    const uint64_t id = (uint64_t)node->get_instance_id();
    lua_getfield(L, LUA_REGISTRYINDEX, "GODOTLUAU_WRAPS");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);                 // la cache
        lua_newtable(L);                 // su metatabla
        lua_pushstring(L, "v");
        lua_setfield(L, -2, "__mode");   // valores debiles
        lua_setmetatable(L, -2);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "GODOTLUAU_WRAPS");
    }
    lua_pushlightuserdata(L, (void*)(uintptr_t)id);
    lua_rawget(L, -2);
    if (lua_isuserdata(L, -1)) {
        lua_remove(L, -2);
        return;
    }
    lua_pop(L, 1);
    GodotObjectWrapper* wrap = (GodotObjectWrapper*)lua_newuserdata(L, sizeof(GodotObjectWrapper));
    gow_set(wrap, node);
    luaL_getmetatable(L, "GodotObject");
    lua_setmetatable(L, -2);
    lua_pushlightuserdata(L, (void*)(uintptr_t)id);
    lua_pushvalue(L, -2);
    lua_rawset(L, -4);                   // cache[id] = wrapper
    lua_remove(L, -2);
}

// ── Registro global de estados Luau vivos ────────────────────────────────────
inline std::unordered_set<lua_State*>& gl_alive_states() {
    static std::unordered_set<lua_State*> s;
    return s;
}
inline void gl_register_state(lua_State* L)   { if (L) gl_alive_states().insert(L); }
inline void gl_unregister_state(lua_State* L) { if (L) gl_alive_states().erase(L); }
inline bool gl_state_alive(lua_State* L)      { return L && gl_alive_states().count(L) != 0; }

// ── Memoria que tiene Luau ahora mismo, sumando TODAS las VM ─────────────
// Es la sonda del recolector de basura: si un frame largo coincide con una
// bajada de golpe de este numero, el tiron es una barrida del GC.
// (Se cuenta en KB, que es lo que devuelve LUA_GCCOUNT.) Una VM por
// contexto: los hilos de los scripts comparten su memoria.
inline int gl_lua_memoria_kb() {
    int total = 0;
    for (GLContexto* c : gl_contextos()) if (c->L) total += lua_gc(c->L, LUA_GCCOUNT, 0);
    return total;
}
inline int gl_lua_vms() { return (int)gl_contextos().size(); }

// El hilo base del script dueño de L: lo que el puente guarda como main_L
// (identidad del script para gl_state_alive y las colas de reanudar). Un hilo
// sin dueño (el del propio contexto) da su hilo principal.
// SOLO debe llamarse mientras L sigue vivo (p.ej. al conectar el callback).
inline lua_State* gl_main_of(lua_State* L) {
    if (!L) return nullptr;
    if (GLHilo* h = gl_hilo_de(L)) return h->principal;
    return lua_mainthread(L);
}

// El nodo del script dueño de L (el de sistema para los hilos del propio
// contexto). nullptr si el script ya murio o el nodo ya no existe: un hilo
// suelto de un script muerto no debe programar nada en nombre de nadie.
inline Node* gl_nodo_script_de(lua_State* L) {
    GLHilo* h = gl_hilo_de(L);
    if (!h || !h->vivo || h->nodo_id == 0) return nullptr;
    return gl_cast<Node>(ObjectDB::get_instance(ObjectID(h->nodo_id)));
}

// ── Ciclo de vida de los contextos ───────────────────────────────────────
inline uint64_t& gl_contexto_contador() { static uint64_t n = 0; return n; }

// VM nueva para (juego, servidor/cliente). Su hilo principal es el del
// sistema (lo pone luego quien crea el nodo de sistema).
inline GLContexto* gl_contexto_nuevo(uint64_t juego_id, bool cliente) {
    GLContexto* c = new GLContexto();
    c->id = ++gl_contexto_contador();
    c->juego_id = juego_id;
    c->cliente = cliente;
    c->L = luaL_newstate();
#ifdef GL_LUAU_CODEGEN
    // Nativo: el motor se pone ANTES de cargar nada (luau_codegen_compile lo pide).
    if (luau_codegen_supported()) luau_codegen_create(c->L);
#endif
    lua_callbacks(c->L)->userthread = gl_hilo_heredar;
    std::unique_ptr<GLHilo> s(new GLHilo());
    s->principal = c->L;
    s->ctx = c;
    s->sistema = true;
    c->sistema = s.get();
    c->sistema_vivo = true;
    lua_setthreaddata(c->L, s.get());
    c->hilos.push_back(std::move(s));
    gl_register_state(c->L);
    gl_contextos().push_back(c);
    return c;
}

// Hilo base de un script: entorno propio que lee de los globales comunes.
inline lua_State* gl_contexto_hilo_script(GLContexto* c, uint64_t nodo_id) {
    lua_State* T = lua_newthread(c->L);
    lua_ref(c->L, -1);              // ancla hasta cerrar el contexto (cementerio: ver arriba)
    lua_pop(c->L, 1);
    luaL_sandboxthread(T);
    std::unique_ptr<GLHilo> h(new GLHilo());
    h->nodo_id = nodo_id;
    h->principal = T;
    h->ctx = c;
    lua_setthreaddata(T, h.get());
    c->hilos.push_back(std::move(h));
    c->vivos++;
    gl_register_state(T);
    return T;
}

inline void gl_contexto_cerrar(GLContexto* c) {
    for (std::unique_ptr<GLHilo>& h : c->hilos) gl_unregister_state(h->principal);
    gl_unregister_state(c->L);
    lua_close(c->L);
    c->L = nullptr;
    std::vector<GLContexto*>& v = gl_contextos();
    for (size_t i = 0; i < v.size(); i++) if (v[i] == c) { v.erase(v.begin() + (long)i); break; }
    delete c;
}

// Se cierra cuando ya no queda ni un script ni el nodo de sistema (los dos
// mueren con su juego).
inline void gl_contexto_cerrar_si_procede(GLContexto* c) {
    if (c && c->vivos <= 0 && !c->sistema_vivo) gl_contexto_cerrar(c);
}

// Un script muere: deja de estar vivo y se sueltan las refs que crearon sus
// hilos. Su hilo base queda anclado (cementerio) hasta cerrar el contexto.
inline void gl_contexto_soltar_hilo(uint64_t ctx_id, lua_State* T) {
    GLContexto* c = gl_contexto_por_id(ctx_id);
    if (!c || !T) return;           // el contexto ya se cerro: no queda nada que tocar
    GLHilo* h = gl_hilo_de(T);
    if (h && h->vivo) {
        h->vivo = false;
        for (int r : h->refs) {
            c->dueno_ref.erase(r);
            lua_unref(c->L, r);
        }
        h->refs.clear();
        if (h->sistema) c->sistema_vivo = false;
        else c->vivos--;
    }
    if (!h || !h->sistema) gl_unregister_state(T);
    gl_contexto_cerrar_si_procede(c);
}

// Al descargar la extension: lo que quede abierto.
inline void gl_contextos_cerrar_todos() {
    while (!gl_contextos().empty()) gl_contexto_cerrar(gl_contextos().back());
}

// ¿Es este meta un atributo del JUEGO o fontaneria nuestra? Los atributos de
// Roblox los pone el usuario con SetAttribute; el resto de metas son cosas
// internas (marcas del puente, la clase original del .rbxl, propiedades de UI
// guardadas del importador...) y no deben salir en GetAttributes.
inline bool gl_meta_es_atributo(const String& k) {
    if (k.is_empty()) return false;
    if (k.begins_with("_") || k.begins_with("__")) return false;
    if (k.begins_with("GL") || k.begins_with("gl_") || k.begins_with("rbx")) return false;
    return true;
}

// ── Disparar una lista de manejadores Luau ──────────────────────────────────
// Definida en luau_script.h (ver gl_errors.h): corre el manejador como tarea
// del planificador de su script, asi que puede ceder con task.wait.
inline void gl_resume_handler(lua_State* th, int nargs);
// Definida en luau_api.h: avisa a GetPropertyChangedSignal(prop) y a Changed
// de un cambio que no viene de una escritura desde Luau (la red, el usuario
// tecleando en un TextBox).
inline void gl_fire_prop_changed(godot::Node* n, const godot::String& prop);

// TextBox -> UserInputService.TextBoxFocused / TextBoxFocusReleased. La UI se
// compila antes que el servicio, asi que el aviso va por este puntero (lo pone
// el puente cuando alguien se conecta a esas senales).
using GLAvisoFocoTexto = void (*)(godot::Node*, bool);
inline GLAvisoFocoTexto& gl_aviso_foco_texto() { static GLAvisoFocoTexto f = nullptr; return f; }
// Igual para los atributos (AttributeChanged, GetAttributeChangedSignal): la red
// los cambia en el cliente sin pasar por SetAttribute.
inline void gl_fire_attribute_changed(godot::Node* n, const godot::String& attr);

// Señales de instancia que se reparten desde el script dueño (ver
// ScriptNodeBase::relays en luau_script.h).
enum GLRelayKind : uint8_t {
    GL_REL_HIJO_ENTRA = 0,   // ChildAdded(hijo)
    GL_REL_HIJO_SALE,        // ChildRemoved(hijo)
    GL_REL_DESC_ENTRA,       // DescendantAdded(descendiente)
    GL_REL_DESC_SALE,        // DescendantRemoving(descendiente)
    GL_REL_DESTRUYE,         // Destroying()
    GL_REL_ANCESTROS,        // AncestryChanged(instancia, padre)
};

// Cada elemento de `cbs` tiene main_L, ref y active (todas las clases con
// señales usan esa forma). `args(th)` empuja los argumentos en la pila del hilo
// y devuelve cuantos. main_L y ref se COPIAN antes de correr Lua: un manejador
// que conecta otro a la misma señal hace push_back y realoja el vector, y una
// referencia a cbs[i] apuntaria a memoria liberada. Va de atras adelante, como
// los bucles a los que sustituye.
template <typename CB, typename Args>
inline void gl_fire_cbs(std::vector<CB>& cbs, Args args) {
    for (int i = (int)cbs.size() - 1; i >= 0; i--) {
        if (i >= (int)cbs.size()) continue;   // la lista encogio mientras corria Lua
        lua_State* mL = cbs[i].main_L;
        if (!cbs[i].active || !gl_state_alive(mL)) { cbs.erase(cbs.begin() + i); continue; }
        const int ref = cbs[i].ref;
        lua_State* th = lua_newthread(mL);
        lua_rawgeti(mL, LUA_REGISTRYINDEX, ref);
        if (lua_isfunction(mL, -1)) {
            lua_xmove(mL, th, 1);
            const int n = args(th);
            gl_resume_handler(th, n);
        } else {
            lua_pop(mL, 1);
        }
        lua_pop(mL, 1);
    }
}
// Sin argumentos.
template <typename CB>
inline void gl_fire_cbs(std::vector<CB>& cbs) {
    gl_fire_cbs(cbs, [](lua_State*) { return 0; });
}

// Devuelve CUALQUIER estado Luau vivo (para operaciones que solo necesitan una VM
// válida, como aplicar una propiedad replicada). nullptr si no hay ninguno.
inline lua_State* gl_any_state() {
    std::unordered_set<lua_State*>& s = gl_alive_states();
    return s.empty() ? nullptr : *s.begin();
}

// ── Replicación automática (1.14.5) ──────────────────────────────────────────
//  `inline` (NO `static inline`) = UNA sola instancia compartida entre TUs.
//  Rol de red GLOBAL espejo del NetworkService (0=single, 1=servidor/host,
//  2=cliente). Lo actualiza el NetworkService. Permite que el hook de escritura
//  de propiedades descarte en O(1) cuando NO somos el servidor de una sesión de
//  red → cero coste en single-player.
inline int& gl_net_role() { static int r = 0; return r; }
//  ObjectID del NetworkService vivo (para resolverlo sin recorrer el árbol).
inline uint64_t& gl_net_service_id() { static uint64_t id = 0; return id; }
//  UserId secuencial e INMUTABLE del jugador local, asignado por el servidor por
//  orden de entrada (host=1, luego 2,3,...). 0 = sin asignar → el Player local
//  usa 1 (single-player). El servidor lo pone en start_server; un cliente lo
//  recibe por RPC (_gl_peer_uid) al conectarse.
inline int64_t& gl_local_user_id() { static int64_t v = 0; return v; }

//  Prefijo de meta del shadow de replicación: valor Lua-encodeado por propiedad.
static const char* GL_REP_SHADOW = "_glrep_";

//  ── JobId (1.14.16) ──────────────────────────────────────────────────────
//  Identificador ÚNICO de esta instancia de servidor. Antes `game.JobId`
//  devolvía "" siempre. Se genera una vez, la primera vez que se pide.
//  Como Roblox: sin sesión de red (equivalente a Studio) sigue siendo "".
inline String& gl_job_id() { static String id; return id; }

//  ── TeleportData (1.14.17) ───────────────────────────────────────────────
//  Datos que viajan con el jugador al cambiar de mundo. Van en memoria porque
//  el PROCESO del cliente sobrevive a la reconexión: solo se suelta el servidor
//  viejo y se entra al nuevo, así que al llegar siguen aquí.
inline String& gl_teleport_data()   { static String d;  return d; }
inline bool&   gl_teleport_arrived(){ static bool b = false; return b; }
inline String gl_ensure_job_id() {
    String& id = gl_job_id();
    if (!id.is_empty()) return id;
    auto hex = [](uint64_t v, int len) {
        String s = String::num_uint64(v, 16);
        while (s.length() < len) s = String("0") + s;
        return s.substr(s.length() - len, len);
    };
    uint64_t t  = (uint64_t)Time::get_singleton()->get_unix_time_from_system();
    uint64_t u  = (uint64_t)Time::get_singleton()->get_ticks_usec();
    uint64_t r1 = (uint64_t)UtilityFunctions::randi();
    uint64_t r2 = (uint64_t)UtilityFunctions::randi();
    id = hex(t, 8) + "-" + hex(r1 & 0xFFFF, 4) + "-4" + hex((r1 >> 16) & 0xFFF, 3)
       + "-" + hex(0x8000 | (r2 & 0x3FFF), 4) + "-" + hex((u << 16) ^ r2, 12);
    return id;
}

//  ── netId DETERMINISTA para los nodos del PLACE (1.14.10) ────────────────
//  Los netId de runtime los asigna el SERVIDOR con un contador y se anuncian por
//  RPC; los nodos que vienen en el place nunca pasan por ahí, así que no tenían
//  id y nada suyo se podía sincronizar (ni física ni props). Como TODAS las
//  ventanas cargan el MISMO place, la ruta absoluta ya identifica al nodo igual
//  en todas: hasheándola (FNV-1a) cada máquina llega al MISMO id sin negociar.
//  El bit 62 marca "estático" y mantiene ese rango separado del contador de
//  runtime (que empieza en 1), así que los dos tipos de id nunca chocan.
static const uint64_t GL_STATIC_ID_BIT = 0x4000000000000000ULL;
// Bit 61 (con el 62): el objeto Player de un peer y sus contenedores (F5). El id
// lleva el peer dentro, asi que un cliente que aun no conoce a ese jugador lo
// crea al recibir algo suyo (leaderstats, atributos) en vez de perderlo.
static const uint64_t GL_PLAYER_ID_BIT = 0x2000000000000000ULL;
inline uint64_t gl_static_netid(const String& path) {
    CharString cs = path.utf8();
    const char* p = cs.get_data();
    uint64_t h = 1469598103934665603ULL;   // FNV-1a offset basis
    for (int i = 0; p && p[i]; i++) { h ^= (uint8_t)p[i]; h *= 1099511628211ULL; }
    return (h & 0x1FFFFFFFFFFFFFFFULL) | GL_STATIC_ID_BIT;
}
inline bool gl_is_static_netid(uint64_t id) { return (id & GL_STATIC_ID_BIT) != 0; }
// cont: 0 el Player, 1 PlayerGui, 2 Backpack, 3 StarterGear, 4 PlayerScripts.
inline uint64_t gl_player_netid(int peer, int cont) {
    return GL_STATIC_ID_BIT | GL_PLAYER_ID_BIT | ((uint64_t)(uint32_t)peer << 3) | (uint64_t)(cont & 7);
}
inline bool gl_is_player_netid(uint64_t id) {
    return (id & (GL_STATIC_ID_BIT | GL_PLAYER_ID_BIT)) == (GL_STATIC_ID_BIT | GL_PLAYER_ID_BIT);
}
// A quién va lo replicado de este nodo: 0 a todos los clientes, N solo al peer
// N (lo de una PlayerGui, ver gl_replica.h).
inline int gl_rep_destino(Node* n) {
    return (n && n->has_meta("_gl_netpeer")) ? (int)n->get_meta("_gl_netpeer") : 0;
}
inline const char* gl_player_contenedor(int cont) {
    static const char* nombres[] = { "", "PlayerGui", "Backpack", "StarterGear", "PlayerScripts" };
    return (cont >= 1 && cont <= 4) ? nombres[cont] : "";
}

//  ── Qué nodo se reporta en Touched (1.14.11) ─────────────────────────────
//  En Roblox el Touched entrega una PARTE del personaje, y por eso el idioma
//  universal es `hit.Parent:FindFirstChild("Humanoid")`. Aquí el colisionador
//  es el personaje ENTERO (lleva una sola cápsula), así que reportarlo tal cual
//  dejaba `hit.Parent` = Workspace y ese idioma no entraba NUNCA. Se reporta su
//  HumanoidRootPart (o Torso), cuyo Parent sí es el personaje.
inline Node* gl_touch_reported_node(Node* body) {
    if (!body) return nullptr;
    if (!body->get_node_or_null(NodePath("Humanoid"))) return body;   // no es un personaje
    if (Node* p = body->get_node_or_null(NodePath("HumanoidRootPart"))) return p;
    if (Node* t = body->get_node_or_null(NodePath("Torso"))) return t;
    return body;
}

//  Reloj sincronizado (1.14.8). El servidor es la referencia; el cliente estima
//  su desfase por round-trip (NetworkService._time_req/_time_res). En servidor y
//  single-player los offsets son 0 → devuelven el reloj local.
inline double& gl_server_unix_offset() { static double v = 0.0; return v; }   // Δ a sumar al unix local para = unix del server
inline double& gl_dgt_offset()         { static double v = 0.0; return v; }   // Δ para DistributedGameTime
//  GetServerTimeNow(): hora unix (epoch) del servidor, en segundos con decimales.
inline double gl_server_time_now() {
    return Time::get_singleton()->get_unix_time_from_system() + gl_server_unix_offset();
}
//  DistributedGameTime: segundos que lleva corriendo el servidor, sincronizado.
inline double gl_distributed_game_time() {
    return (double)Time::get_singleton()->get_ticks_usec() / 1000000.0 + gl_dgt_offset();
}
//  Hook para APLICAR una propiedad replicada en el cliente reentrando al
//  __newindex real. Lo define luau_api.h (que ve godot_object_newindex/wrap_node/
//  gl_net_decode) y lo instala el setup por-VM; el NetworkService (que no puede
//  ver luau_api.h por el orden de includes) lo llama por puntero.
typedef void (*GLApplyPropFn)(Node* target, const String& key, const Variant& enc);
inline GLApplyPropFn& gl_apply_prop_hook() { static GLApplyPropFn f = nullptr; return f; }
// Hook para registrar en el ScriptNodeBase un hilo que cede esperando la
// respuesta de un RemoteFunction por red. luau_api.h (que NO ve ScriptNodeBase,
// se incluye antes) lo llama; luau_script.h lo instala tras definir la clase.
typedef void (*GLAddInvokeWaitFn)(Node* sn, lua_State* th, lua_State* main_L, int ref, int64_t call_id, double timeout);
inline GLAddInvokeWaitFn& gl_add_invoke_wait_hook() { static GLAddInvokeWaitFn f = nullptr; return f; }
// MarketplaceService (gl_tienda.h, que va despues de Players y de la red):
// Players avisa al entrar un jugador, y la red entrega los avisos de compra.
typedef void (*GLJugadorEntraFn)(Node* jugador);
inline GLJugadorEntraFn& gl_jugador_entra_hook() { static GLJugadorEntraFn f = nullptr; return f; }
typedef void (*GLTiendaRedFn)(int peer, const String& tipo, int64_t id, int estado);
inline GLTiendaRedFn& gl_tienda_red_hook() { static GLTiendaRedFn f = nullptr; return f; }
// CollectionService (roblox_input.h) avisa de cada AddTag/RemoveTag para
// replicarlo (gl_replica.h lo instala).
typedef void (*GLRepTagFn)(Node* n, const String& tag, bool puesta);
inline GLRepTagFn& gl_rep_tag_hook() { static GLRepTagFn f = nullptr; return f; }

// ── Modelo server-authoritative (1.14.5) ─────────────────────────────────────
//  ¿Esta ventana arrancó como CLIENTE de una sesión de red? Se decide por los
//  args del launcher (fiable al arranque, antes de que corran los scripts). Un
//  cliente NO ejecuta ServerScripts: recibe el mundo del servidor por
//  replicación, igual que Roblox. La ventana nativa/servidor/single devuelve
//  false → ejecuta todo.
inline bool gl_startup_is_client() {
    PackedStringArray a = OS::get_singleton()->get_cmdline_user_args();
    for (int i = 0; i < a.size(); i++) {
        String s = a[i];
        if (s == String("--glconnect") || s == String("--glmatch")) return true;
        if (s == String("--glindex") && i + 1 < a.size() && String(a[i + 1]).to_int() >= 2) return true;
    }
    return false;
}
//  ¿Esta ventana debe APLAZAR sus ServerScripts hasta CONFIRMAR que es servidor
//  (gl_net_role()==1)? El rol de servidor se fija tarde (start_server, vía
//  _auto_init diferido), así que si un ServerScript corriera antes, el mundo que
//  crea no se replicaría. Devuelve true para: cliente seguro; servidor por args
//  (--glserver/--glinstance/--glindex 1); y la ventana NATIVA con sesión MP
//  FRESCA (será el host del test local). Single-player → false (corre todo ya).
//  El guard de frescura (age<45s) evita romper single-player con una sesión vieja.
inline bool gl_startup_defer_server() {
    if (gl_startup_is_client()) return true;
    PackedStringArray a = OS::get_singleton()->get_cmdline_user_args();
    for (int i = 0; i < a.size(); i++) {
        String s = a[i];
        if (s == String("--glserver") || s == String("--glinstance")) return true;
        if (s == String("--glindex") && i + 1 < a.size() && String(a[i + 1]).to_int() == 1) return true;
    }
    Ref<FileAccess> f = FileAccess::open(GL_MP_SESSION_PATH, FileAccess::READ);
    if (f.is_valid()) {
        PackedStringArray p = String(f->get_as_text()).strip_edges().split("|");
        f->close();
        if (p.size() >= 3) {
            int c = String(p[0]).to_int();
            double age = Time::get_singleton()->get_unix_time_from_system() - (double)String(p[2]).to_float();
            if (c >= 2 && age >= 0.0 && age < 45.0) return true;   // sesión MP fresca → esta nativa será el servidor
        }
    }
    return false;
}
//  ServerScripts APLAZADOS en una ventana cliente (guardan ObjectID). Si esa
//  ventana se promueve a host de respaldo (gl_net_role()→1), se ejecutan
//  entonces — así el fallback de host del test local sigue funcionando.
inline std::vector<uint64_t>& gl_deferred_server_scripts() { static std::vector<uint64_t> v; return v; }
inline void gl_run_deferred_server_scripts() {
    std::vector<uint64_t>& dl = gl_deferred_server_scripts();
    std::vector<uint64_t> copy = dl;   // copiar: iniciar_corrutina puede tocar la lista
    dl.clear();
    for (uint64_t oid : copy) {
        Object* o = ObjectDB::get_instance(oid);
        if (Node* n = gl_cast<Node>(o)) n->call("iniciar_corrutina");
    }
}

// ── Contenedores PLANTILLA (1.15) ────────────────────────────────────────────
//  En Roblox los Starter* no son contenedores de ejecución: son MOLDES. Lo que
//  hay dentro no corre nunca; corre el CLON que se copia al jugador o al
//  personaje al spawnear. Aquí el original vive en el árbol, así que corría el
//  original Y el clon: todo por duplicado (el PlayerModule del usuario llevaba
//  ejecutándose dos veces desde siempre). Se mira por CLASE, no por nombre: un
//  Folder llamado "StarterGui" dentro del Workspace es un Folder normal y sus
//  scripts deben correr.
inline bool gl_is_template_container(const Node* n) {
    if (!n) return false;
    return n->is_class("StarterPlayer") || n->is_class("StarterPlayerScripts") ||
           n->is_class("StarterCharacterScripts") || n->is_class("StarterGui") ||
           n->is_class("StarterPack");
}
//  Un script es plantilla si CUALQUIER ancestro es un contenedor plantilla (no
//  vale mirar solo al padre: un Folder dentro de StarterGui con un LocalScript
//  dentro sigue siendo plantilla).
inline bool gl_in_template_container(const Node* n) {
    for (const Node* a = n ? n->get_parent() : nullptr; a; a = a->get_parent())
        if (gl_is_template_container(a)) return true;
    return false;
}

// ── Contenedores de ALMACEN (1.17) ───────────────────────────────────────────
//  ServerStorage y ReplicatedStorage guardan cosas; NO ejecutan. En Roblox un
//  Script (RunContext Legacy) solo corre bajo Workspace o ServerScriptService, y
//  un LocalScript solo bajo el personaje, PlayerGui, PlayerScripts, Backpack o
//  ReplicatedFirst. Aqui corrian igualmente, asi que las herramientas guardadas
//  en ServerStorage (las Magic Carpet del place de prueba) se ejecutaban solas
//  y sus LocalScripts se quedaban colgados en WaitForChild("ServerControl")
//  esperando algo que solo existe cuando la Tool esta de verdad en la mano.
//  Un RunContext explicito (Server=1 / Client=2) SI corre desde cualquier sitio,
//  como en Roblox moderno.
inline bool gl_is_storage_container(const Node* n) {
    if (!n) return false;
    return n->is_class("ServerStorage") || n->is_class("ReplicatedStorage");
}
inline bool gl_in_storage_container(const Node* n) {
    for (const Node* a = n ? n->get_parent() : nullptr; a; a = a->get_parent())
        if (gl_is_storage_container(a)) return true;
    return false;
}

// ── Transform "global" seguro ────────────────────────────────────────────────
//  Fuera del arbol, get_global_transform()/get_global_position() de Godot
//  imprimen un ERROR por consola y devuelven la identidad. En Roblox una
//  instancia sin padre conserva su CFrame, y clonar → colocar → asignar Parent
//  es el idioma mas comun que existe. Con esto el comportamiento es el de
//  Roblox y la consola se queda limpia.
inline godot::Transform3D gl_xf(const godot::Node3D* n) {
    if (!n) return godot::Transform3D();
    return n->is_inside_tree() ? n->get_global_transform() : n->get_transform();
}
inline godot::Vector3 gl_pos(const godot::Node3D* n) { return gl_xf(n).origin; }

// ── Mover el HumanoidRootPart mueve el PERSONAJE (1.15) ──────────────────────
//  En Roblox el HRP es la raíz del ensamblaje: las demás partes van soldadas a
//  él, así que "hrp.CFrame = x" teletransporta al personaje entero. Aquí el HRP
//  es un hijo más del personaje, así que movía solo esa pieza y el jugador se
//  quedaba donde estaba, sin decir nada. Es un idioma muy común (teletransportes,
//  spawns), así que se redirige al personaje conservando el offset del HRP.
//  Devuelve la raíz del personaje si n es su HumanoidRootPart; si no, nullptr.
// ── Generacion de anclajes ───────────────────────────────────────────
// Sube cada vez que algo puede cambiar la respuesta a "¿estas dos piezas estan
// quietas para siempre?": que una pieza cambie de Anchored, o que entre o salga
// del arbol. Los constraints la usan para DORMIRSE: un weld entre dos piezas
// ancladas no tiene nada que hacer nunca mas, pero comprobarlo costaba 6
// Object::cast_to (RTTI + 3 cruces de frontera cada uno) y 4 llamadas de
// propiedad POR WELD Y POR PASO DE FISICA. Con 2.077 welds a 60 Hz eran ~1,2
// millones de cruces de frontera por segundo para decidir que no hay nada que
// hacer — y ese era el grueso de los 6,7 ms del paso de fisica, que es lo que
// hacia que el frame alternara entre 10 y 16,7 ms (un vaiven del 40% de FPS
// estando quieto). Con esto, un constraint dormido cuesta dos comparaciones.
inline uint32_t& gl_gen_anclajes() { static uint32_t g = 1; return g; }
inline void gl_anclajes_cambiaron() { gl_gen_anclajes()++; }

// NodePath cacheado por nombre. Construir un NodePath parsea la cadena (busca
// '/', ':', subnombres) y reserva memoria, y eso corria en CADA lectura de
// propiedad del puente: 62.750 lecturas por segundo en el place de prueba. Los
// nombres que se consultan son un puñado y se repiten sin parar.
// (Vive aqui fuera y no dentro del __index porque `HashMap<String, NodePath>`
// lleva una coma y ese codigo va dentro de la macro lua_pushcfunction.)
inline const NodePath& gl_ruta_de_nombre(const String& nombre) {
    static HashMap<String, NodePath> rutas;
    HashMap<String, NodePath>::Iterator it = rutas.find(nombre);
    if (!it) it = rutas.insert(nombre, NodePath(nombre));
    return it->value;
}

// El EnumItem de Roblox de ese valor (el mismo objeto que Enum.<nombre>.<X>).
// Sin el stdlib cargado, o con un valor que el enum no tiene, queda el numero.
inline void gl_push_enum_item(lua_State* L, const char* enum_nombre, int valor) {
    lua_getglobal(L, "__gl_enum_item");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, valor); return; }
    lua_pushstring(L, enum_nombre);
    lua_pushnumber(L, valor);
    lua_call(L, 2, 1);
}

// ── Identidad de una conexion: (VM, ref), NUNCA solo el ref ─────────────────
//  lua_ref reparte enteros pequenos POR VM, y cada script es una VM: el ref 4
//  de un script y el 4 de otro son conexiones DISTINTAS. Buscando solo por
//  numero, un Disconnect mataba el callback de otro script — medido en el
//  place: la pantalla de carga, al terminar, desconectaba su Heartbeat y de
//  paso el de los NPCs del camino (se congelaban ~1 s despues de nacer, sin
//  ningun error). `vm` es el lua_State del que conecto, pasado como entero.
//  vm == 0 significa "cualquiera" (llamadas antiguas sin VM).
#define GL_MISMA_VM(cb) (vm == 0 || (int64_t)(intptr_t)(cb).main_L == vm)
#define GL_MISMA_VM_L(cb) (vm == 0 || (int64_t)(intptr_t)(cb).L == vm)

// Masa del personaje del jugador (AssemblyMass de un R6 de Roblox: torso,
// cabeza, cuatro extremidades y la HRP con densidad 0,7). Divide los impulsos.
inline constexpr float GL_MASA_PERSONAJE = 12.6f;

// La instancia a la que apunta una propiedad de referencia (Attachment0,
// Attachment1...) de un nodo con puente generico. La que escribe un script va en
// la meta "_glo_<Prop>" (ObjectID) y la del importador en la meta "<Prop>"
// (NodePath relativo). Nulo si no hay o ya no existe. Las claves llegan ya
// hechas (static en quien llama): esto corre en el tick de los constraints.
inline Node* gl_ref_de(const Node* n, const StringName& k_script, const StringName& k_importada) {
    if (!n) return nullptr;
    if (n->has_meta(k_script))
        return gl_cast<Node>(ObjectDB::get_instance(ObjectID((uint64_t)(int64_t)n->get_meta(k_script))));
    if (n->has_meta(k_importada)) {
        const Variant v = n->get_meta(k_importada);
        if (v.get_type() == Variant::NODE_PATH && n->is_inside_tree())
            return n->get_node_or_null((NodePath)v);
    }
    return nullptr;
}

inline Node3D* gl_character_root_of_hrp(Node* n) {
    if (!n || n->get_name() != StringName("HumanoidRootPart")) return nullptr;
    Node* p = n->get_parent();
    if (!p || !p->get_node_or_null(NodePath("Humanoid"))) return nullptr;
    return gl_cast<Node3D>(p);
}
// Transform "de mundo" de un nodo SIN depender del arbol de escena.
// get_global_transform() de Godot exige estar dentro del arbol; un personaje
// recien clonado todavia no lo esta, y ahi es justo donde Roblox lo coloca.
// Se acumula a mano subiendo por los padres (la profundidad es de unos pocos).
inline Transform3D gl_transform_acumulado(Node* n) {
    if (!n) return Transform3D();
    const Transform3D padre = gl_transform_acumulado(n->get_parent());
    if (Node3D* n3 = gl_cast<Node3D>(n)) return padre * n3->get_transform();
    return padre;
}
inline Transform3D gl_mundo_de(Node3D* n) {
    if (!n) return Transform3D();
    return n->is_inside_tree() ? n->get_global_transform() : gl_transform_acumulado(n);
}

inline void gl_set_part_global_transform(Node3D* n3d, const Transform3D& t) {
    if (!n3d) return;
    Node3D* root = gl_character_root_of_hrp(n3d);
    Node3D* obj = n3d;
    Transform3D dest = t;
    if (root) {
        // Queremos que el HRP acabe en t. Si L es su transform relativo a la
        // raíz (raiz_mundo * L = hrp_mundo), entonces raiz_mundo = t * L⁻¹.
        //
        // ESTO VALE TAMBIEN FUERA DEL ARBOL. Antes se exigia is_inside_tree() en
        // los dos y, si no, se caia a mover SOLO el HRP — con lo que el idioma
        // mas normal de Roblox (clonar el personaje, colocarlo por su
        // HumanoidRootPart y DESPUES asignar Parent) dejaba el cuerpo atras.
        // Medido: 229 studs en la sonda y 11.570 en los NPC del camino del
        // place, donde ademas el collider y el Humanoid se quedaban a 11 mil
        // studs de lo que se ve, asi que no respondian ni clics ni golpes.
        const Transform3D L = gl_mundo_de(root).affine_inverse() * gl_mundo_de(n3d);
        dest = t * L.affine_inverse();
        obj = root;
    }
    // Un CFrame de Roblox NO lleva escala: poner .CFrame reorienta y mueve, pero
    // jamás redimensiona. Antes se metía la base del CFrame tal cual, así que
    // cualquier CFrame mal formado (o simplemente el 1.0 de uno normal) pisaba
    // la escala real del personaje y el collider dejaba de coincidir con lo que
    // se ve. Se conserva la escala propia y se usa solo la rotación.
    const Transform3D actual = gl_mundo_de(obj);
    const Vector3 esc = actual.basis.get_scale();
    Basis b = dest.basis.orthonormalized();
    b = b.scaled(esc);
    const Transform3D nuevo(b, dest.origin);
    // Reasignar la MISMA CFrame cada frame es un idioma normalisimo en Roblox
    // (cintas, seguimientos, "mantener aqui"). Aqui no era gratis: mueve el
    // cuerpo en el broadphase, lo despierta y propaga TRANSFORM_CHANGED por todo
    // el subarbol. El place de prueba hace 55.900 escrituras de Rotation cada
    // 10 s. Comparar antes cuesta 12 floats.
    if (nuevo.is_equal_approx(actual)) return;
    if (obj->is_inside_tree()) {
        obj->set_global_transform(nuevo);
        return;
    }
    // Sin arbol no existe set_global_transform: se pasa a LOCAL quitando lo que
    // acumulan los padres. Antes se escribia el transform de MUNDO tal cual como
    // local, asi que el nodo acababa desplazado por el transform de su padre.
    obj->set_transform(gl_transform_acumulado(obj->get_parent()).affine_inverse() * nuevo);
}
inline void gl_set_part_global_position(Node3D* n3d, const Vector3& p) {
    if (!n3d) return;
    Node3D* root = gl_character_root_of_hrp(n3d);
    if (root && root->is_inside_tree() && n3d->is_inside_tree()) {
        root->set_global_position(root->get_global_position() + (p - n3d->get_global_position()));
        return;
    }
    // Mismo motivo que en gl_set_part_global_transform: escribir la posicion que
    // ya tiene no es gratis (broadphase + despertar + notificaciones).
    if (n3d->is_inside_tree()) {
        if (!n3d->get_global_position().is_equal_approx(p)) n3d->set_global_position(p);
    } else {
        if (!n3d->get_position().is_equal_approx(p)) n3d->set_position(p);
    }
}

// ── Visibilidad por ancestro (1.15) ──────────────────────────────────────────
//  En Roblox SOLO se renderiza lo que cuelga de Workspace. Una Part dentro de
//  ReplicatedStorage / ServerStorage / Lighting / Players / MaterialService…
//  existe como dato pero NO se ve. Aquí Godot renderiza cualquier VisualInstance
//  que esté en el árbol, así que una Part metida en uno de esos servicios salía a
//  la vista. Se mira por NOMBRE (los contenedores virtuales son Node normal) y
//  por clase, subiendo por TODOS los ancestros: un Folder dentro de Players con
//  una Part dentro debe seguir sin verse.
inline bool gl_is_nonrender_container(const Node* n) {
    if (!n) return false;
    StringName nm = n->get_name();
    static const char* NAMES[] = {
        "ReplicatedStorage", "ServerStorage", "ServerScriptService",
        "ReplicatedFirst", "Lighting", "MaterialService", "SoundService",
        "Players", "Teams", "StarterGui", "StarterPack", "StarterPlayer",
        "StarterPlayerScripts", "StarterCharacterScripts", "TextChatService",
        "Chat", "LocalizationService", nullptr
    };
    // MISMO FALLO QUE HABIA EN gl_clone.h: `StringName(NAMES[i])` CONSTRUYE una
    // StringName desde texto —hash, mutex y recuento— y aqui se hacian las 17 EN
    // CADA LLAMADA. Y esta funcion se llama por CADA NIVEL DE ANCESTRO de CADA
    // pieza que entra al arbol: una pieza a seis niveles pagaba mas de cien
    // construcciones. Medido: la visibilidad por ancestro era 2,6 us de los 15,1
    // que cuesta meter una pieza en el arbol. Ahora se construyen una sola vez.
    static LocalVector<StringName> nombres;
    if (nombres.is_empty())
        for (int i = 0; NAMES[i]; i++) nombres.push_back(StringName(NAMES[i]));
    for (uint32_t i = 0; i < nombres.size(); i++) if (nm == nombres[i]) return true;
    // La clase de un nodo no cambia: se mira una vez por nodo. Eran ocho
    // is_class por ANTEPASADO de cada pieza que entra al arbol, cada uno con un
    // String nuevo (memoria del monton) y una llamada al motor: 57 ms del
    // montaje de Build a Mountain, con miles de piezas bajo los mismos modelos.
    static HashMap<uint64_t, bool> por_nodo;
    const uint64_t id = (uint64_t)n->get_instance_id();
    if (const bool* hay = por_nodo.getptr(id)) return *hay;
    const bool es = n->is_class("Players") || n->is_class("Teams") || n->is_class("Lighting") ||
                    n->is_class("StarterPlayer") || n->is_class("StarterPlayerScripts") ||
                    n->is_class("StarterCharacterScripts") || n->is_class("StarterGui") ||
                    n->is_class("StarterPack");
    if (por_nodo.size() > 200000) por_nodo.clear();   // tope: ids de nodos ya liberados
    por_nodo.insert(id, es);
    return es;
}
inline bool gl_is_world_root(const Node* n) {
    if (!n) return false;
    // El nombre PRIMERO: comparar dos StringName es comparar punteros, mientras
    // que cada is_class() cruza la frontera de la GDExtension. En un place real
    // el Workspace se llama "Workspace", asi que casi siempre se acierta aqui y
    // no se llega a los dos is_class.
    static const StringName SN_WORKSPACE("Workspace");
    if (n->get_name() == SN_WORKSPACE) return true;
    // La clase no cambia: una vez por nodo (cada is_class crea un String; esto
    // se pregunta por cada antepasado de cada pieza que entra al arbol).
    static HashMap<uint64_t, bool> por_nodo;
    const uint64_t id = (uint64_t)n->get_instance_id();
    if (const bool* hay = por_nodo.getptr(id)) return *hay;
    const bool es = n->is_class("RobloxWorkspace") || n->is_class("RobloxWorkspace2D");
    if (por_nodo.size() > 200000) por_nodo.clear();   // tope: ids de nodos ya liberados
    por_nodo.insert(id, es);
    return es;
}
//  ¿este visual debe ocultarse por dónde está? Sube por los ancestros: si llega a
//  Workspace, se ve; si antes topa con un servicio de datos, se oculta.
inline bool gl_visual_hidden_by_ancestor(const Node* n) {
    for (const Node* a = n ? n->get_parent() : nullptr; a; a = a->get_parent()) {
        if (gl_is_world_root(a)) return false;
        if (gl_is_nonrender_container(a)) return true;
    }
    return false;   // fuera de todo contenedor conocido: no ocultar por las dudas
}

// ── Instancias HUERFANAS de Instance.new (1.15) ──────────────────────────────
//  Instance.new("Part") crea el nodo SIN padre: en Roblox lo sujeta el script y
//  el recolector lo tira cuando se pierde la referencia. Aqui, si el script nunca
//  lo cuelga del arbol, no lo libera nadie -> "ObjectDB instances were leaked at
//  exit" en cada ejecucion. Se apuntan al crearlos y al cerrar se liberan los que
//  sigan sin padre (los que si se colgaron ya son del arbol, que los destruye el).
inline std::vector<uint64_t>& gl_orphan_instances() { static std::vector<uint64_t> v; return v; }
inline void gl_track_orphan(Node* n) {
    if (n) gl_orphan_instances().push_back((uint64_t)n->get_instance_id());
}
inline void gl_free_orphan_instances() {
    std::vector<uint64_t>& v = gl_orphan_instances();
    for (uint64_t id : v) {
        Node* n = gl_cast<Node>(ObjectDB::get_instance(id));
        // Sin padre y no encolado ya para borrar: si estuviera encolado, borrarlo
        // aqui seria un doble free.
        if (n && !n->get_parent() && !n->is_queued_for_deletion()) memdelete(n);
    }
    v.clear();
}

// ── BindToClose: callbacks a ejecutar al cerrar el juego (guardar datos, etc.) ─
//  game:BindToClose(fn) registra fn aquí junto a su VM (main_L). Al cerrar el
//  juego corren TODOS cuando la raiz sale del arbol: todos los nodos ya han
//  salido y todavia no se ha borrado ninguno, asi que el juego entero sigue
//  vivo, como en Roblox al parar el servidor. Antes corrian en el destructor de
//  cada script, con servicios hermanos ya borrados: el DataService de los
//  juegos del usuario moria en RunService:IsStudio() al guardar la sesion.
//  Si un script se destruye en mitad de la partida, corren los suyos al cerrar
//  su VM. Best-effort: si el callback cede, no se reanuda.
struct GLCloseCb { lua_State* main_L; int ref; };
inline std::vector<GLCloseCb>& gl_close_cbs() { static std::vector<GLCloseCb> v; return v; }
inline void gl_correr_close_cb(const GLCloseCb& c) {
    if (!gl_state_alive(c.main_L)) return;
    lua_State* th = lua_newthread(c.main_L);
    lua_rawgeti(c.main_L, LUA_REGISTRYINDEX, c.ref);
    if (lua_isfunction(c.main_L, -1)) {
        lua_xmove(c.main_L, th, 1);
        lua_resume(th, nullptr, 0);   // síncrono; si cede, no se reanuda
    } else lua_pop(c.main_L, 1);
    lua_pop(c.main_L, 1);             // quita el hilo
    gl_unref(c.main_L, c.ref);
}
inline void gl_run_all_close_cbs() {
    std::vector<GLCloseCb> v;
    v.swap(gl_close_cbs());
    for (const GLCloseCb& c : v) gl_correr_close_cb(c);
}
inline void gl_vigilar_cierre_juego() {
    static uint64_t vigilada = 0;
    SceneTree* st = gl_cast<SceneTree>(Engine::get_singleton()->get_main_loop());
    Window* raiz = st ? st->get_root() : nullptr;
    if (!raiz || (uint64_t)raiz->get_instance_id() == vigilada) return;
    vigilada = (uint64_t)raiz->get_instance_id();
    raiz->connect("tree_exiting", callable_mp_static(&gl_run_all_close_cbs));
}
inline void gl_add_close_cb(lua_State* main_L, int ref) {
    if (!main_L) return;
    gl_close_cbs().push_back({ main_L, ref });
    if (!Engine::get_singleton()->is_editor_hint()) gl_vigilar_cierre_juego();
}
// Ejecuta y elimina los callbacks cuyo main_L coincide. Llamar ANTES de cerrar L.
inline void gl_run_close_cbs_for(lua_State* main_L) {
    if (!main_L) return;
    std::vector<GLCloseCb>& v = gl_close_cbs();
    for (int i = 0; i < (int)v.size(); ) {
        if (v[i].main_L == main_L) {
            const GLCloseCb c = v[i];
            v.erase(v.begin() + i);
            gl_correr_close_cb(c);
        } else ++i;
    }
}

#endif // GL_RUNTIME_H

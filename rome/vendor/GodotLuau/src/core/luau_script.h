#ifndef LUAU_SCRIPT_NODE_H
#define LUAU_SCRIPT_NODE_H

// ════════════════════════════════════════════════════════════════════
//  Script templates by node name
//  When ScriptNodeBase creates a .lua file in the editor, it uses the
//  node name to choose the correct template code.
//  e.g. Health → regen code, ControlModule → movement module, etc.
////
//  Templates de scripts por nombre de nodo
//  Cuando ScriptNodeBase crea un archivo .lua en el editor, usa el
//  nombre del nodo para elegir el código plantilla correcto.
//  Así Health → código de regen, ControlModule → módulo de movimiento, etc.
// ════════════════════════════════════════════════════════════════════

// ── De donde salen las plantillas (1.16.12) ─────────────────────────
//  Ya NO son literales dentro de este header: cada plantilla es un .luau
//  de verdad en GodotLuau/DefaultScripts/, colgado en la MISMA ruta que
//  tendra en el arbol de Roblox (StarterPlayer/StarterCharacterScripts/
//  Health.luau, ServerScriptService/GameManager.luau...). Asi se editan
//  con autocompletado y se encuentran de un vistazo, y un colaborador
//  puede bajarse solo el .luau que quiere mejorar.
//
//  luau_templates_gen.h es la copia EMBEBIDA de esos .luau, escrita por
//  tools/generar_plantillas_luau.py, para que la DLL siga funcionando sin
//  la carpeta (juego exportado, instalacion parcial). En ejecucion
//  gl_luau_template() (mas abajo) prefiere el archivo de disco si existe:
//  editas el .luau, arrancas y ves el cambio SIN recompilar.
//
//  Si tocas un .luau:  python tools/generar_plantillas_luau.py
#include "../ui/gl_cola_ui.h"   // arranque de VM con presupuesto por frame
#include "gl_cast.h"
#include "luau_templates_gen.h"   // LUAU_TEMPLATE_* + LUAU_TEMPLATE_*_PATH (generado)

// Default LocalScript / ServerScript template — mínimo estilo Roblox Studio.
// %SCRIPT_ID% se sustituye por el ID único del script al crearlo.
static const char* LUAU_TEMPLATE_LOCAL_SCRIPT =
    "-->GodotLuau Developed by PimpoliDev\n"
    "print(\"Hello World\" .. \" the script \" .. \"%SCRIPT_ID%\" .. \" he was executed\")\n";

static const char* LUAU_TEMPLATE_SERVER_SCRIPT =
    "-->GodotLuau Developed by PimpoliDev\n"
    "print(\"Hello World\" .. \" the script \" .. \"%SCRIPT_ID%\" .. \" he was executed\")\n";

// Default ModuleScript template — mínimo + boilerplate de módulo.
static const char* LUAU_TEMPLATE_MODULE_OOP =
    "-->GodotLuau Developed by PimpoliDev\n"
    "print(\"Hello World\" .. \" the script \" .. \"%SCRIPT_ID%\" .. \" he was executed\")\n"
    "\n"
    "local module = {}\n"
    "\n"
    "return module\n";

// ══════════════════════════════════════════════════════════════════════
//  Menu modular (1.15) — StarterPlayerScripts/Modules/Menu + submódulos.
//  Menu (principal) contiene: MenuUi (todas las UIs), Settings (Config),
//  Players (Personas). TODO editable por el usuario. El PlayerModule hace
//  require(Menu) y Menu.Init(player). El menú se abre con Escape y se ve
//  como el de Roblox.
// ══════════════════════════════════════════════════════════════════════
#include "menu_templates.h"   // LUAU_TEMPLATE_MENU / _UI / _SETTINGS / _PLAYERS

#include "luau_api.h"
#include "roblox_part.h"
#include "humanoid.h"
#include "luau_stdlib.h"

#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

// Sitio donde vivia el contador de ids de script antes de la 1.17 (la raiz del
// proyecto). Se sigue leyendo para migrar; ya no se escribe. El plugin del
// editor borra el archivo viejo una vez esta el nuevo (ver _migrate_old_layout).
#define GL_IDS_PATH_LEGACY "res://.luau_ids.cfg"
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/tls_options.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "lua.h"
#include "lualib.h"
#ifdef GL_LUAU_CODEGEN
#include "luacodegen.h"   // compilador NATIVO de Luau (lo que usa Roblox)
#endif
#include "Luau/Compiler.h"

#include <godot_cpp/classes/os.hpp>

#include "gl_debug.h"   // GL_DEBUG_PRINT + gl_tr3

#include <vector>
#include <string>
#include <map>
#include <unordered_map>

using namespace godot;

// ══════════════════════════════════════════════════════════════════════
//  Compilacion de Luau: opciones + cache de bytecode (1.16.13)
//
//  El modelo de GodotLuau es una VM por script, y CADA VM cargaba antes su
//  propia copia recien compilada del stdlib (~2.700 lineas), del wrapper de
//  require y del de RemoteFunction. En un place con 200 scripts eso son 200
//  compilaciones del MISMO texto en el arranque, a cambio de nada: el
//  bytecode que sale es identico byte a byte.
//
//  Aqui se compila una sola vez y se reparte. La clave del cache es el TEXTO
//  fuente, no la ruta: editar un script en caliente da una entrada nueva, asi
//  que nunca se sirve bytecode viejo.
//
//  Ademas se sube optimizationLevel a 2 (el que usa Roblox en produccion).
//  debugLevel se queda en 1: es lo que mantiene numeros de linea y nombres de
//  funcion, y sin eso los errores estilo Roblox ("Script:5: ...") y los
//  traceback se quedarian en blanco.
// ══════════════════════════════════════════════════════════════════════

// Compila a codigo maquina la funcion que hay en lo alto de la pila (y sus
// funciones internas). Si la plataforma no lo soporta no hace nada y todo
// sigue funcionando interpretado.
inline void gl_nativo(lua_State* L) {
#ifdef GL_LUAU_CODEGEN
    static int avisado = 0;
    if (!avisado) {
        avisado = 1;
        UtilityFunctions::print(luau_codegen_supported()
            ? "[GodotLuau] Luau NATIVO activo (codegen)"
            : "[GodotLuau] Luau interpretado (esta plataforma no soporta codegen)");
    }
    if (luau_codegen_supported()) luau_codegen_compile(L, -1);
#else
    (void)L;
#endif
}
inline const Luau::CompileOptions& gl_compile_options() {
    // Los globales de un contexto son "seguros" (safeenv, F8): el compilador
    // llama a las funciones de la libreria de Luau por el camino rapido sin
    // mirar el global. Estas tres las sustituye GodotLuau (typeof conoce los
    // tipos hechos con tablas: Vector2, UDim2, CFrame, EnumItem...), asi que no
    // pueden ir por ahi: con el camino rapido typeof(UDim2.new()) daba "table".
    static const char* const sustituidas[] = { "typeof", "tostring", "tonumber", nullptr };
    static Luau::CompileOptions o = [] {
        Luau::CompileOptions c;
        c.optimizationLevel = 2;   // inlining + unrolling (Roblox usa 2)
        c.debugLevel        = 1;   // lineas y nombres de funcion (traceback)
        c.disabledBuiltins  = sustituidas;
        return c;
    }();
    return o;
}

// Compila SIN cache. Para codigo que solo se compila una vez de todos modos.
inline std::string gl_compile(const std::string& src) {
    return Luau::compile(src, gl_compile_options());
}

// Compila CON cache global por texto fuente. Lo usan el stdlib, los wrappers
// internos y los ModuleScript (el mismo modulo se requiere desde muchas VMs).
// La referencia devuelta es estable: unordered_map no invalida referencias al
// insertar.
inline const std::string& gl_compile_cached(const std::string& src) {
    static std::unordered_map<std::string, std::string> cache;
    std::unordered_map<std::string, std::string>::iterator it = cache.find(src);
    if (it != cache.end()) return it->second;
    return cache.emplace(src, Luau::compile(src, gl_compile_options())).first->second;
}

// Compilacion SOLO para validar sintaxis: la llama el editor en cada pulsacion
// y el bytecode se tira, asi que no se optimiza ni se guarda informacion de
// depuracion. Tampoco se cachea (el texto cambia con cada tecla).
inline std::string gl_compile_syntax_only(const std::string& src) {
    Luau::CompileOptions o;
    o.optimizationLevel = 0;
    o.debugLevel        = 0;
    return Luau::compile(src, o);
}

// ══════════════════════════════════════════════════════════════════════
//  Carga en caliente de plantillas (1.16.12)
//  Cada plantilla de fábrica es un archivo .luau real en
//  res://GodotLuau/DefaultScripts/<ruta espejo del árbol de Roblox>.
//  gl_luau_template() intenta leer ese archivo y, si no está (juego
//  exportado, instalación sin la carpeta), devuelve el literal embebido de
//  luau_templates_gen.h. Así se edita un .luau, se arranca y se ve el
//  cambio SIN recompilar la extensión.
// ══════════════════════════════════════════════════════════════════════
// Donde se buscan las plantillas .luau EN DISCO, por orden. Hay dos porque hay
// dos disposiciones de proyecto: la del repo y la de prueba meten todo bajo
// GodotLuau/, y la que genera tools/nuevo_proyecto.py es plana (bin/, icons/…
// en la raiz). Antes solo se miraba la primera, asi que en un proyecto nuevo la
// carpeta no existia nunca y siempre ganaba la copia empotrada en la DLL: el
// menu funcionaba, pero no habia NADA que abrir ni editar.
static const char* GL_TPL_DIRS[] = {
    "res://GodotLuau/DefaultScripts/",
    "res://DefaultScripts/",
};

inline const char* gl_luau_template(const char* clave, const char* embebido) {
    // Caché por clave (el archivo se lee UNA vez). Guardamos std::string y no
    // String/CharString de Godot a propósito: el const char* que devolvemos
    // tiene que seguir vivo tras el return, y un contenedor estático de tipos
    // de Godot se destruiría al descargar la DLL (destrucción tardía).
    // Valor vacío = no hay archivo en disco → se usa el literal embebido.
    static std::map<std::string, std::string> cache;
    std::map<std::string, std::string>::iterator it = cache.find(clave);
    if (it == cache.end()) {
        std::string leido;
        String path;
        for (size_t d = 0; d < sizeof(GL_TPL_DIRS) / sizeof(GL_TPL_DIRS[0]); d++) {
            const String p = String(GL_TPL_DIRS[d]) + String(clave);
            if (FileAccess::file_exists(p)) { path = p; break; }
        }
        if (!path.is_empty()) {
            String txt = FileAccess::get_file_as_string(path);
            if (!txt.is_empty()) {
                // Igualar el archivo al literal embebido: éste empieza con un
                // salto de línea (el que abre su raw string) y usa LF, mientras
                // el archivo no trae ese salto y puede venir en CRLF (git).
                txt = String("\n") + txt.replace("\r\n", "\n");
                leido = std::string(txt.utf8().get_data());
                GL_DEBUG_PRINT("[GodotLuau] Plantilla leida de disco: ", path);
            }
        }
        it = cache.insert(std::pair<std::string, std::string>(std::string(clave), leido)).first;
    }
    return it->second.empty() ? embebido : it->second.c_str();
}

// ══════════════════════════════════════════════════════════════════════
//  Plantillas de fábrica: fuente ÚNICA de la verdad (1.15)
//  gl_builtin_template() decide qué código "de fábrica" corresponde a un
//  nodo de script según su NOMBRE y clase. La usan tanto el creador de
//  archivos (ENTER_TREE, más abajo) como el actualizador incremental
//  (roblox_datamodel.h), para no duplicar el mapeo en dos sitios.
// ══════════════════════════════════════════════════════════════════════
// Atajo: cada plantilla se pide por su .luau (con el literal embebido como
// reserva), así el disco manda siempre que el archivo esté.
// String::utf8 SIEMPRE: String(const char*) parsea Latin-1 y convertía los
// acentos del template ("máximos" → "mÃ¡ximos"); peor aún, el creador de
// archivos reescribía los .luau en disco con ese texto, corrompiéndolos un
// nivel más en cada arranque.
#define GL_TPL(NOMBRE) String::utf8(gl_luau_template(NOMBRE##_PATH, NOMBRE))

inline String gl_builtin_template(const String& node_name, const String& cls) {
    if (node_name == "Health")         return GL_TPL(LUAU_TEMPLATE_HEALTH);
    if (node_name == "Animate")        return GL_TPL(LUAU_TEMPLATE_ANIMATE);
    if (node_name == "PlayerModule")   return GL_TPL(LUAU_TEMPLATE_PLAYER_MODULE);
    if (node_name == "ControlModule")  return GL_TPL(LUAU_TEMPLATE_CONTROL_MODULE);
    if (node_name == "PCModule")       return GL_TPL(LUAU_TEMPLATE_PC_MODULE);
    if (node_name == "MobileModule")   return GL_TPL(LUAU_TEMPLATE_MOBILE_MODULE);
    if (node_name == "ConsoleModule")  return GL_TPL(LUAU_TEMPLATE_CONSOLE_MODULE);
    if (node_name == "CameraModule")   return GL_TPL(LUAU_TEMPLATE_CAMERA_MODULE);
    if (node_name == "ChatModule")     return GL_TPL(LUAU_TEMPLATE_CHAT_MODULE);
    if (node_name == "GameManager")    return GL_TPL(LUAU_TEMPLATE_GAME_MANAGER);
    // Menu modular (1.15): Menu principal + submódulos editables. Sus .luau
    // están en DefaultScripts/.../Modules/Menu (ver menu_templates.h).
    if (node_name == "Menu")           return GL_TPL(LUAU_TEMPLATE_MENU);
    if (node_name == "MenuUi")         return GL_TPL(LUAU_TEMPLATE_MENU_UI);
    if (node_name == "Settings")       return GL_TPL(LUAU_TEMPLATE_MENU_SETTINGS);
    if (node_name == "Players")        return GL_TPL(LUAU_TEMPLATE_MENU_PLAYERS);
    if (node_name == "Gallery")        return GL_TPL(LUAU_TEMPLATE_MENU_GALLERY);
    if (node_name == "Help")           return GL_TPL(LUAU_TEMPLATE_MENU_HELP);
    if (node_name == "English")        return GL_TPL(LUAU_TEMPLATE_LANG_ENGLISH);
    if (node_name == "Spanish")        return GL_TPL(LUAU_TEMPLATE_LANG_SPANISH);
    // Legado (WIP 1.15): módulo único de ajustes, reemplazado por Menu/*.
    if (node_name == "SettingsModule") return GL_TPL(LUAU_TEMPLATE_SETTINGS_MODULE);
    if (cls == "LocalScript")  return String::utf8(LUAU_TEMPLATE_LOCAL_SCRIPT);
    if (cls == "ServerScript") return String::utf8(LUAU_TEMPLATE_SERVER_SCRIPT);
    if (cls == "ModuleScript") return String::utf8(LUAU_TEMPLATE_MODULE_OOP);
    return String::utf8(LUAU_TEMPLATE_LOCAL_SCRIPT);
}
#undef GL_TPL   // atajo local: no se escapa al resto de la extensión

// ¿El nombre corresponde a un script "de fábrica" con plantilla propia? Un
// LocalScript/ModuleScript del usuario con nombre arbitrario NO lo es (cae en el
// template genérico) y el actualizador NO lo toca.
inline bool gl_is_managed_template(const String& node_name) {
    static const char* names[] = {
        "Health", "Animate", "PlayerModule", "ControlModule", "PCModule",
        "MobileModule", "ConsoleModule", "CameraModule", "ChatModule",
        "GameManager", "Menu", "MenuUi", "Settings", "Players", "Gallery", "Help", "English", "Spanish"
    };
    for (const char* n : names) if (node_name == String(n)) return true;
    return false;
}

// ── Sidecar de "hash base" por script (1.15) ──────────────────────────────
//  Guarda, por script_id, el md5 del ÚLTIMO template de fábrica que ESCRIBIMOS
//  en su archivo. Permite al actualizador distinguir:
//    · archivo == base            → el jugador NO lo tocó  → actualizar en sitio
//    · archivo != base            → el jugador lo tocó     → ofrecer en ScriptUpdate
//  Formato: líneas "script_id\tmd5" en res://GodotLuau/.gl_tpl_base.cfg
static const char* GL_TPL_BASE_PATH = "res://GodotLuau/.gl_tpl_base.cfg";

inline Dictionary gl_tpl_base_load() {
    Dictionary d;
    if (!FileAccess::file_exists(GL_TPL_BASE_PATH)) return d;
    Ref<FileAccess> f = FileAccess::open(GL_TPL_BASE_PATH, FileAccess::READ);
    if (f.is_null()) return d;
    while (!f->eof_reached()) {
        String line = f->get_line();
        int tab = line.find("\t");
        if (tab > 0) d[line.substr(0, tab)] = line.substr(tab + 1);
    }
    return d;
}
inline void gl_tpl_base_save(const Dictionary& d) {
    Ref<DirAccess> dir = DirAccess::open("res://");
    if (dir.is_valid() && !dir->dir_exists("res://GodotLuau")) dir->make_dir_recursive("res://GodotLuau");
    Ref<FileAccess> f = FileAccess::open(GL_TPL_BASE_PATH, FileAccess::WRITE);
    if (f.is_null()) return;
    Array keys = d.keys();
    for (int i = 0; i < keys.size(); i++) {
        String k = keys[i];
        f->store_line(k + "\t" + String(d[k]));
    }
}
inline String gl_tpl_base_get(const String& id) {
    Dictionary d = gl_tpl_base_load();
    return d.has(id) ? String(d[id]) : String();
}
inline void gl_tpl_base_set(const String& id, const String& md5) {
    Dictionary d = gl_tpl_base_load();
    d[id] = md5;
    gl_tpl_base_save(d);
}

// ── Helpers for _JSON: Variant ↔ Lua conversion ───────────────────────────
//// ── Helpers para _JSON: conversión Variant ↔ Lua ──────────────────────────
inline void _gdluau_variant_to_lua(lua_State* L, const Variant& v) {
    switch (v.get_type()) {
        case Variant::NIL:    lua_pushnil(L); return;
        case Variant::BOOL:   lua_pushboolean(L, (bool)v); return;
        case Variant::INT:    lua_pushnumber(L, (double)(int64_t)v); return;
        case Variant::FLOAT:  lua_pushnumber(L, (double)v); return;
        case Variant::STRING: { String s = v; lua_pushstring(L, s.utf8().get_data()); return; }
        case Variant::ARRAY: {
            Array arr = v; lua_newtable(L);
            for (int i = 0; i < arr.size(); i++) {
                _gdluau_variant_to_lua(L, arr[i]);
                lua_rawseti(L, -2, i + 1);
            }
            return;
        }
        case Variant::DICTIONARY: {
            Dictionary dict = v; lua_newtable(L);
            Array keys = dict.keys();
            for (int i = 0; i < keys.size(); i++) {
                String k = String(keys[i]);
                lua_pushstring(L, k.utf8().get_data());
                _gdluau_variant_to_lua(L, dict[keys[i]]);
                lua_rawset(L, -3);
            }
            return;
        }
        default: lua_pushnil(L); return;
    }
}

inline Variant _gdluau_lua_to_variant(lua_State* L, int idx, int depth = 0) {
    if (depth > 20) return Variant();
    if (idx < 0) idx = lua_gettop(L) + idx + 1;
    int t = lua_type(L, idx);
    if (t == LUA_TNIL)     return Variant();
    if (t == LUA_TBOOLEAN) return Variant((bool)lua_toboolean(L, idx));
    if (t == LUA_TNUMBER)  {
        double n = lua_tonumber(L, idx);
        return (n == (int64_t)n) ? Variant((int64_t)n) : Variant(n);
    }
    if (t == LUA_TSTRING)  return Variant(String(lua_tostring(L, idx)));
    if (t == LUA_TTABLE) {
        // ── ARRAY solo si es una SECUENCIA PERFECTA ──────────────────────
        // Antes bastaba con lua_objlen>0 para emitir un Array, y eso TIRABA en
        // silencio todas las claves de texto de una tabla mixta:
        // JSONEncode({1, 2, foo = 3}) daba [1,2] y `foo` desaparecia sin un
        // solo error — datos guardados corrompidos sin que nada avisara.
        const int seq_len = (int)lua_objlen(L, idx);
        bool solo_secuencia = (seq_len > 0);
        if (solo_secuencia) {
            lua_pushnil(L);
            while (lua_next(L, idx) != 0) {
                bool ok = false;
                if (lua_type(L, -2) == LUA_TNUMBER) {
                    const double k = lua_tonumber(L, -2);
                    ok = (k == (double)(int64_t)k) && k >= 1.0 && k <= (double)seq_len;
                }
                lua_pop(L, 1);                 // el valor
                if (!ok) { lua_pop(L, 1); solo_secuencia = false; break; }  // y la clave
            }
        }
        if (solo_secuencia) {
            Array arr;
            for (int i = 1; i <= seq_len; i++) {
                lua_rawgeti(L, idx, i);
                arr.push_back(_gdluau_lua_to_variant(L, -1, depth + 1));
                lua_pop(L, 1);
            }
            return arr;
        }
        Dictionary dict;
        lua_pushnil(L);
        while (lua_next(L, idx) != 0) {
            String key;
            if (lua_type(L, -2) == LUA_TSTRING) key = String(lua_tostring(L, -2));
            else if (lua_type(L, -2) == LUA_TNUMBER) key = String::num(lua_tonumber(L, -2));
            dict[key] = _gdluau_lua_to_variant(L, -1, depth + 1);
            lua_pop(L, 1);
        }
        return dict;
    }
    return Variant();
}

// ── Helpers para _HTTP: cliente HTTP real sobre HTTPClient ─────────────────
//// HttpService:GetAsync/PostAsync/RequestAsync son peticiones de verdad.
////
//// ASINCRONO (1.16.13). Antes esto bloqueaba el hilo principal hasta 10 s: el
//// juego ENTERO se congelaba mientras viajaba la respuesta (en Roblox solo
//// cede la corrutina que llamo). Ahora la peticion es una maquina de estados
//// que _process avanza un poco cada frame, y la corrutina que llamo CEDE igual
//// que con WaitForChild o RemoteFunction:InvokeServer. El juego sigue a 60 fps,
//// y dos scripts distintos pueden tener peticiones en vuelo a la vez.
////
//// El camino bloqueante sigue existiendo (gl_http_perform) para los contextos
//// donde no se puede ceder — dentro de un metametodo, o sin nodo de script — y
//// esta escrito sobre la MISMA maquina de estados, asi que los dos caminos dan
//// exactamente el mismo resultado.

// Tope de inactividad de una peticion. Como Roblox (30 s). El contador se
// reinicia cada vez que llegan datos: una descarga lenta pero que avanza no se
// corta a mitad.
static const double GL_HTTP_TIMEOUT_SEC = 30.0;

// Tope APARTE para la fase de conexion, mas corto.
//
// El HTTPClient de Godot se queda en STATUS_CONNECTING PARA SIEMPRE cuando el
// puerto esta cerrado o filtrado: nunca pasa a STATUS_CANT_CONNECT (comprobado
// contra 127.0.0.1 con el puerto cerrado — 8 s y seguia en CONNECTING). Sin
// este tope, una URL con el host mal escrito se comia los 30 s enteros. Un
// TCP que no ha abierto en 10 s no va a abrir.
static const double GL_HTTP_CONNECT_TIMEOUT_SEC = 10.0;

struct GLHttpResult {
    int    status = 0;      // código HTTP (200, 404…); 0 = fallo de red/conexión
    String status_msg;      // "OK", "Timeout al conectar"…
    String body;            // cuerpo de la respuesta (o texto de error si status==0)
    bool   ok    = false;   // true si status ∈ [200, 299]
};

// ¿Está presente el argumento idx (no none ni nil)? Evita depender de luaL_opt*.
inline bool gl_arg_present(lua_State* L, int idx) {
    int t = lua_type(L, idx);
    return t != LUA_TNONE && t != LUA_TNIL;
}

inline HTTPClient::Method gl_http_method_from_string(const String& m) {
    String u = m.to_upper();
    if (u == "GET")     return HTTPClient::METHOD_GET;
    if (u == "POST")    return HTTPClient::METHOD_POST;
    if (u == "PUT")     return HTTPClient::METHOD_PUT;
    if (u == "DELETE")  return HTTPClient::METHOD_DELETE;
    if (u == "PATCH")   return HTTPClient::METHOD_PATCH;
    if (u == "HEAD")    return HTTPClient::METHOD_HEAD;
    if (u == "OPTIONS") return HTTPClient::METHOD_OPTIONS;
    return HTTPClient::METHOD_GET;
}

// Convierte una tabla Luau {["Nombre"]="Valor", …} en el formato "Nombre: Valor"
// que espera HTTPClient::request. Ignora claves/valores no representables.
inline PackedStringArray gl_lua_headers_to_array(lua_State* L, int idx) {
    PackedStringArray out;
    if (idx < 0) idx = lua_gettop(L) + idx + 1;
    if (!lua_istable(L, idx)) return out;
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            String key = String(lua_tostring(L, -2));
            String val;
            int vt = lua_type(L, -1);
            if (vt == LUA_TSTRING)      val = String(lua_tostring(L, -1));
            else if (vt == LUA_TNUMBER) val = String::num(lua_tonumber(L, -1));
            else { lua_pop(L, 1); continue; }
            out.push_back(key + ": " + val);
        }
        lua_pop(L, 1);  // deja la clave para el próximo lua_next
    }
    return out;
}

// Peticion ya PARSEADA: lo que hace falta para lanzarla sin volver a mirar la URL.
struct GLHttpSpec {
    HTTPClient::Method method = HTTPClient::METHOD_GET;
    String            host;
    int               port = 80;
    bool              tls  = false;
    String            path;
    PackedStringArray headers;
    String            body;
    bool              valid = false;
    String            error;      // motivo si valid == false
};

// Parsea scheme://host[:port]/path?query. No admite IPv6 entre corchetes.
inline GLHttpSpec gl_http_parse(HTTPClient::Method method, const String& url,
                                const PackedStringArray& headers, const String& body) {
    GLHttpSpec sp;
    sp.method  = method;
    sp.headers = headers;
    sp.body    = body;

    String u = url.strip_edges();
    if (u.begins_with("https://"))     { sp.tls = true;  u = u.substr(8); }
    else if (u.begins_with("http://")) { sp.tls = false; u = u.substr(7); }
    else { sp.error = "URL sin esquema http(s)://"; return sp; }

    int slash = u.find("/");
    String authority = (slash == -1) ? u : u.substr(0, slash);
    sp.path = (slash == -1) ? String("/") : u.substr(slash);
    if (sp.path.is_empty()) sp.path = "/";

    sp.host = authority;
    sp.port = sp.tls ? 443 : 80;
    int colon = authority.rfind(":");
    if (colon != -1) {
        String p = authority.substr(colon + 1);
        if (p.is_valid_int()) { sp.host = authority.substr(0, colon); sp.port = (int)p.to_int(); }
    }
    if (sp.host.is_empty()) { sp.error = "URL sin host"; return sp; }

    sp.valid = true;
    return sp;
}

// ── Peticion EN CURSO ────────────────────────────────────────────────────────
//  gl_http_step la avanza un poco y vuelve enseguida: nunca duerme ni espera.
//  El que la lanzo decide como esperarla (cediendo la corrutina, o girando).
enum GLHttpPhase { GLHTTP_CONNECTING = 0, GLHTTP_REQUESTING, GLHTTP_READING, GLHTTP_DONE };

struct GLHttpPending {
    Ref<HTTPClient> client;
    GLHttpSpec      spec;
    PackedByteArray data;
    int             phase = GLHTTP_CONNECTING;
    double          idle  = 0.0;                      // s sin progreso (se reinicia al recibir datos)
    double          timeout = GL_HTTP_TIMEOUT_SEC;
    GLHttpResult    res;
};

// Cierra la peticion con un fallo de red. status queda en 0, como antes: el
// mensaje viaja tambien en el body para que GetAsync/PostAsync lo devuelvan.
inline void gl_http_fail(GLHttpPending& p, const String& msg) {
    p.res.status     = 0;
    p.res.ok         = false;
    p.res.status_msg = msg;
    p.res.body       = msg;
    if (p.client.is_valid()) p.client->close();
    p.client = Ref<HTTPClient>();
    p.phase  = GLHTTP_DONE;
}

// Cierra la peticion bien: monta el body y el status_msg definitivos.
inline void gl_http_succeed(GLHttpPending& p) {
    p.res.body = p.data.size() > 0
               ? String::utf8((const char*)p.data.ptr(), (int)p.data.size())
               : String();
    if (p.res.status_msg.is_empty())
        p.res.status_msg = p.res.ok ? "OK" : ("HTTP " + String::num_int64(p.res.status));
    if (p.client.is_valid()) p.client->close();
    p.client = Ref<HTTPClient>();
    p.phase  = GLHTTP_DONE;
}

// Abre la conexion. Al volver, p.phase puede ser ya GLHTTP_DONE si la URL era
// invalida o el socket no abrio; el resultado se entrega por el camino normal.
inline void gl_http_begin(GLHttpPending& p, HTTPClient::Method method, const String& url,
                          const PackedStringArray& headers, const String& body,
                          double timeout_sec = GL_HTTP_TIMEOUT_SEC) {
    p.timeout = timeout_sec;
    p.spec    = gl_http_parse(method, url, headers, body);
    if (!p.spec.valid) { gl_http_fail(p, p.spec.error); return; }

    p.client.instantiate();
    Ref<TLSOptions> tls = p.spec.tls ? TLSOptions::client() : Ref<TLSOptions>();
    if (p.client->connect_to_host(p.spec.host, p.spec.port, tls) != OK)
        gl_http_fail(p, "connect_to_host falló");
}

// Avanza la peticion. `delta` son los segundos transcurridos desde la ultima
// llamada (para el timeout). Devuelve true cuando ya hay resultado en p.res.
inline bool gl_http_step(GLHttpPending& p, double delta) {
    if (p.phase == GLHTTP_DONE) return true;
    if (p.client.is_null()) { gl_http_fail(p, "Cliente HTTP no válido"); return true; }
    p.idle += delta;

    // Varias vueltas por llamada: cada poll() avanza un paso pequeño, y esperar
    // un frame entero por paso volveria lenta hasta una peticion a localhost.
    // El tope evita que una respuesta enorme se coma el frame de golpe.
    for (int spin = 0; spin < 64 && p.phase != GLHTTP_DONE; spin++) {
        p.client->poll();
        HTTPClient::Status st = p.client->get_status();

        if (p.phase == GLHTTP_CONNECTING) {
            if (st == HTTPClient::STATUS_RESOLVING || st == HTTPClient::STATUS_CONNECTING) break;
            if (st != HTTPClient::STATUS_CONNECTED) {
                gl_http_fail(p, "No se pudo conectar (status " + String::num_int64((int)st) + ")");
                break;
            }
            if (p.client->request(p.spec.method, p.spec.path, p.spec.headers, p.spec.body) != OK) {
                gl_http_fail(p, "request() falló");
                break;
            }
            p.phase = GLHTTP_REQUESTING;
            p.idle  = 0.0;
            continue;
        }

        if (p.phase == GLHTTP_REQUESTING) {
            if (st == HTTPClient::STATUS_REQUESTING) break;
            if (st != HTTPClient::STATUS_BODY && st != HTTPClient::STATUS_CONNECTED) {
                gl_http_fail(p, "Error de conexión en la petición (status " + String::num_int64((int)st) + ")");
                break;
            }
            p.res.status = p.client->get_response_code();
            p.res.ok     = (p.res.status >= 200 && p.res.status < 300);
            p.phase      = GLHTTP_READING;
            p.idle       = 0.0;
            // 204, HEAD, respuesta sin cuerpo: ya esta todo.
            if (st != HTTPClient::STATUS_BODY) { gl_http_succeed(p); break; }
            continue;
        }

        // GLHTTP_READING
        if (st != HTTPClient::STATUS_BODY) { gl_http_succeed(p); break; }
        PackedByteArray chunk = p.client->read_response_body_chunk();
        if (chunk.size() == 0) break;          // aun no ha llegado: se sigue luego
        p.data.append_array(chunk);
        p.idle = 0.0;                          // hay progreso: el timeout se reinicia
    }

    if (p.phase != GLHTTP_DONE) {
        // Conectar tiene su propio tope, mas corto (ver GL_HTTP_CONNECT_TIMEOUT_SEC).
        double tope = (p.phase == GLHTTP_CONNECTING)
                    ? (p.timeout < GL_HTTP_CONNECT_TIMEOUT_SEC ? p.timeout : GL_HTTP_CONNECT_TIMEOUT_SEC)
                    : p.timeout;
        if (p.idle >= tope) {
            gl_http_fail(p, p.phase == GLHTTP_CONNECTING ? String("Timeout al conectar")
                          : p.phase == GLHTTP_REQUESTING ? String("Timeout esperando respuesta")
                                                         : String("Timeout leyendo la respuesta"));
        }
    }
    return p.phase == GLHTTP_DONE;
}

// Peticion BLOQUEANTE. Solo para los contextos donde no se puede ceder (dentro
// de un metametodo, o sin nodo de script detras). El codigo de usuario normal
// pasa por el camino asincrono de gl_http_call.
inline GLHttpResult gl_http_perform(HTTPClient::Method method, const String& url,
                                    const PackedStringArray& headers, const String& body,
                                    double timeout_sec = GL_HTTP_TIMEOUT_SEC) {
    GLHttpPending p;
    gl_http_begin(p, method, url, headers, body, timeout_sec);
    OS* os = OS::get_singleton();
    while (!gl_http_step(p, 0.001)) os->delay_msec(1);
    return p.res;
}

class LuauScript : public ScriptExtension {
    GDCLASS(LuauScript, ScriptExtension);

protected:
    // El codigo se declara como propiedad ALMACENABLE propia: sin esto, un
    // LuauScript embebido en la escena (import de .rbxl guardado como
    // SubResource) se serializaba VACIO y todos los scripts del place quedaban
    // mudos al recargar. La fuente de Script (source_code de Godot) no se
    // guarda en .tscn, asi que hace falta esta copia con nombre propio.
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("gl_set_fuente", "s"), &LuauScript::gl_set_fuente);
        ClassDB::bind_method(D_METHOD("gl_get_fuente"), &LuauScript::gl_get_fuente);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "gl_fuente", PROPERTY_HINT_MULTILINE_TEXT),
                     "gl_set_fuente", "gl_get_fuente");
    }
    String source_code;

public:
    void gl_set_fuente(const String &s) { source_code = s; }
    String gl_get_fuente() const { return source_code; }

protected:

public:
    String _get_source_code() const override { return source_code; }
    void _set_source_code(const String &p_code) override { source_code = p_code; }
    bool _is_tool() const override { return false; }
    bool _is_valid() const override { return true; }
    bool _has_source_code() const override { return true; }
    bool _can_instantiate() const override { return true; }
    bool _has_static_method(const StringName &p_method) const override { return false; }
    void _update_exports() override {}
    Ref<Script> _get_base_script() const override { return Ref<Script>(); }
    TypedArray<Dictionary> _get_documentation() const override { return TypedArray<Dictionary>(); }
    StringName _get_instance_base_type() const override { return StringName("Object"); }
    ScriptLanguage *_get_language() const override;
    void *_instance_create(Object *p_for_object) const override { return nullptr; }
    Error _reload(bool p_keep_state) override { return OK; }
};

// ════════════════════════════════════════════════════════════════════
//  ScriptNodeBase — Base class for LocalScript, ServerScript, ModuleScript
////  ScriptNodeBase — Base de LocalScript, ServerScript, ModuleScript
// ════════════════════════════════════════════════════════════════════
class ScriptNodeBase : public Node {
    GDCLASS(ScriptNodeBase, Node);

    // Notificacion PROPIA (fuera del rango de Godot) con la que el cierre
    // diferido reutiliza el mismo teardown que hacia EXIT_TREE.
    static const int GL_NOTIF_CERRAR_VM = 90210;
    bool _gl_cierre_pendiente = false;

public:
    // Luau thread spawned via task.spawn()
    // timer == -1.0  →  suspended by external condition (WaitForChild, Signal:Wait)
    //// Hilo de Luau spawneado via task.spawn()
    //// timer == -1.0  →  suspendido por condición externa (WaitForChild, Signal:Wait)
    struct SpawnedThread {
        lua_State* L;
        int        ref;
        double     timer;
        int        start_args = 0;     // args en la pila del hilo a pasar en el 1er resume (task.delay/defer)
        bool       started    = true;  // false hasta el 1er resume (delay/defer aún no arrancados)
        double     waited     = 0.0;   // tiempo REAL esperado (lo devuelve wait())
    };

    // Reloj de juego: es el segundo valor que devuelve wait() en Roblox
    // (workspace.DistributedGameTime).
    static double _gl_game_time() {
        return (double)Time::get_singleton()->get_ticks_msec() / 1000.0;
    }

    double main_waited = 0.0;   // tiempo esperado del hilo principal (lo devuelve wait())
    // task.defer/delay sobre el hilo principal: argumentos ya puestos en su pila
    // para el proximo resume, y si mientras corria alguien lo reprogramo (si no,
    // ceder sin valores lo deja suspendido hasta que algo de fuera lo reanude).
    int  main_start_args = 0;
    bool main_resched    = false;

    // Coroutine waiting for a child to appear in the tree
    //// Coroutina esperando a que aparezca un hijo en el árbol
    struct WaitingChild {
        lua_State* thread;
        lua_State* main_L;
        int        ref;
        uint64_t   parent_id;   // ObjectID del padre (no Node* crudo: el padre puede morir mientras se espera)
        String     child_name;
        double     timeout;   // -1 = sin timeout
        double     elapsed;
        String     trace;          // stack en el momento de la llamada (para el warning)
        bool       warned = false; // "Infinite yield possible" ya impreso
    };

    // Coroutina esperando la respuesta de un RemoteFunction:InvokeServer/Client por
    // red. El call_id la casa con la respuesta que deposita el NetworkService en
    // gl_net_responses(); _process la recoge, decodifica y reanuda el hilo.
    struct WaitingInvoke {
        lua_State* thread;
        lua_State* main_L;
        int        ref;
        int64_t    call_id;
        double     timeout;   // s antes de rendirse; <=0 = esperar siempre (como Roblox)
        double     elapsed;
        bool       warned = false;   // "Infinite yield possible" ya avisado
    };

    // Corrutina cedida esperando una peticion HTTP (1.16.13). Mismo mecanismo
    // que WaitingInvoke, pero aqui el trabajo lo lleva un HTTPClient propio que
    // _process avanza cada frame: el juego NO se para mientras viaja la
    // respuesta, y varias corrutinas pueden tener peticiones en vuelo a la vez.
    struct WaitingHttp {
        lua_State*    thread;
        lua_State*    main_L;
        int           ref;
        int           ret_mode;        // 0 = tabla {Success,StatusCode,…}; 1 = solo el body
        double        waited = 0.0;
        bool          warned = false;
        GLHttpPending req;
    };

private:
    Ref<LuauScript> codigo_luau;
    // L_main: el hilo BASE de este script dentro de la VM de su contexto (F8;
    // antes era una VM propia). Sigue siendo su identidad para el puente.
    lua_State* L_main   = nullptr;
    lua_State* L_thread = nullptr;
    uint64_t   _gl_ctx_id = 0;        // GLContexto al que pertenece (0 = ninguno)
    bool       _gl_es_sistema = false; // nodo interno dueño de las tareas del stdlib

    double wait_timer      = 0.0;
    bool   is_waiting      = false;
    bool   is_external_wait = false;  // true = no auto-resume via timer
    bool   script_finished = false;

public:
    std::vector<WaitingChild> waiting_children;
    std::vector<SpawnedThread> spawned_threads;
    std::vector<WaitingInvoke> waiting_invokes;
    std::vector<WaitingHttp>   waiting_https;

    void _suspend_externally(lua_State* th);
    bool gl_hilo_gestionado(lua_State* th) const;
    void gl_adoptar_hilo(lua_State* th, double timer);
    void gl_start_task(lua_State* th, int nargs);
    bool gl_quitar_tarea(lua_State* th);
    void _antes_de_reanudar(lua_State* th);
    static int _gl_task_wait(lua_State* L);
    static int _gl_args_a_hilo(lua_State* L, lua_State* co, int desde);
    int _gl_reprogramar_hilo(lua_State* L, lua_State* co, int desde, double t);
    void add_waiting_invoke(lua_State* th, lua_State* mL, int ref, int64_t call_id, double timeout);
    void add_waiting_http(lua_State* th, lua_State* mL, int ref, int ret_mode, const GLHttpPending& req);
    static int gl_http_push_result(lua_State* L, const GLHttpResult& r, int ret_mode);
    static int gl_http_call(lua_State* L, HTTPClient::Method method, const String& url,
                            const PackedStringArray& headers, const String& body, int ret_mode);
    void add_waiting_child(lua_State* th, lua_State* mL, int ref,
                           Node* parent, const String& name, double timeout);
    void resume_external_thread(lua_State* th, int nargs);
    void resume_external_thread_error(lua_State* th, const String& msg);
    void _after_resume(lua_State* th, int status);
    void _main_tras_resume(int status);

    // ID persistente e inmutable del script (ej. "ServerScript_ID_12").
    // Vincula el nodo con su archivo .lua para poder borrarlo/restaurarlo junto al nodo.
    String script_id;

    // Enabled (como Roblox): true por defecto; con false el script NO se
    // ejecuta. Si en runtime vuelve a true y no habia corrido, corre entonces.
    bool script_enabled = true;
    bool script_started = false;

    // ── Diagnóstico de ejecución (1.17) ──────────────────────────────────
    //  "0 errores en el log" NO significa "todo el juego corre": un script que
    //  nunca arranca, o que se queda colgado en un WaitForChild, no dice nada.
    //  Esto deja el estado a la vista para poder AUDITAR un place entero.
    //    0 sin arrancar · 1 corriendo · 2 terminado · 3 murió por error
    //    4 esperando · 5 no debe correr (plantilla / almacén / Enabled=false)
    int    _gl_run_state = 0;
    String _gl_run_note;
    // Microsegundos de CPU consumidos por este script (acumulado) y del ultimo
    // segundo. Sin esto no hay forma de saber QUE script se come el frame: en el
    // place de prueba los scripts se llevaban el 80% (8.9 FPS en vivo contra 44
    // con los scripts parados).
    uint64_t _gl_cpu_us = 0;
    void _gl_mark(int st, const String& nota) { _gl_run_state = st; _gl_run_note = nota; }

    // Duplicado (Ctrl+D / copiar-pegar) detectado en ENTER_TREE: código heredado
    // del original que se escribe en el archivo NUEVO en vez de un template.
    String _gl_dup_source;
    bool   _gl_is_dup = false;

    ScriptNodeBase* _gl_find_id_twin();

public:
    // Estado de ejecución, para auditar un place entero desde fuera.
    int    gl_run_state() const { return _gl_run_state; }
    String gl_run_note()  const { return _gl_run_note; }
    // CPU acumulada en microsegundos (para auditar el rendimiento).
    int64_t gl_cpu_us() const { return (int64_t)_gl_cpu_us; }
    void    gl_cpu_reset()    { _gl_cpu_us = 0; }

    void set_enabled(bool b);
    bool get_enabled() const { return script_enabled; }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_codigo_luau", "s"), &ScriptNodeBase::set_codigo_luau);
        ClassDB::bind_method(D_METHOD("get_codigo_luau"), &ScriptNodeBase::get_codigo_luau);
        ClassDB::bind_method(D_METHOD("iniciar_corrutina"), &ScriptNodeBase::iniciar_corrutina);
        ClassDB::bind_method(D_METHOD("_gl_cerrar_vm_si_procede"), &ScriptNodeBase::_gl_cerrar_vm_si_procede);
        ClassDB::bind_method(D_METHOD("set_script_id", "id"), &ScriptNodeBase::set_script_id);
        ClassDB::bind_method(D_METHOD("get_script_id"), &ScriptNodeBase::get_script_id);
        ClassDB::bind_method(D_METHOD("set_enabled", "b"), &ScriptNodeBase::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"), &ScriptNodeBase::get_enabled);
        // Señales de instancia que se reparten desde aqui (ver `relays`).
        ClassDB::bind_method(D_METHOD("_gl_relay_connect", "target_id", "kind", "ref"), &ScriptNodeBase::_gl_relay_connect);
        ClassDB::bind_method(D_METHOD("_gl_relay_disconnect", "id"), &ScriptNodeBase::_gl_relay_disconnect);
        ClassDB::bind_method(D_METHOD("_gl_rel_hijo", "hijo", "target_id", "kind"), &ScriptNodeBase::_gl_rel_hijo);
        ClassDB::bind_method(D_METHOD("_gl_rel_desc", "nodo", "kind"), &ScriptNodeBase::_gl_rel_desc);
        ClassDB::bind_method(D_METHOD("_gl_rel_destruye", "target_id"), &ScriptNodeBase::_gl_rel_destruye);
        ClassDB::bind_method(D_METHOD("_gl_rel_ancestros", "target_id", "fase"), &ScriptNodeBase::_gl_rel_ancestros);
        ClassDB::bind_method(D_METHOD("gl_run_state"), &ScriptNodeBase::gl_run_state);
        ClassDB::bind_method(D_METHOD("gl_run_note"),  &ScriptNodeBase::gl_run_note);
        ClassDB::bind_method(D_METHOD("gl_cpu_us"),    &ScriptNodeBase::gl_cpu_us);
        ClassDB::bind_method(D_METHOD("gl_cpu_reset"), &ScriptNodeBase::gl_cpu_reset);

        ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "codigo_luau", PROPERTY_HINT_RESOURCE_TYPE, "LuauScript"),
                     "set_codigo_luau", "get_codigo_luau");
        // NOTA: la propiedad "Enabled" NO se añade aquí a propósito. En Roblox
        // Studio solo Script/LocalScript la tienen; un ModuleScript no. Por eso
        // cada subclase ejecutable la añade en su propio _bind_methods (ver
        // ServerScript/LocalScript abajo) y ModuleScript la omite. Los métodos
        // set_enabled/get_enabled siguen ligados aquí para uso interno.
        // Visible en el inspector pero NO editable por el usuario
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "script_id", PROPERTY_HINT_NONE, "",
                     PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY),
                     "set_script_id", "get_script_id");
    }

    static const String& _ids_path();
    static int64_t _next_script_counter(const String& cls_lower);
    void _notification(int p_what);

public:
    void _gl_cerrar_vm_si_procede();

    ScriptNodeBase() {}
    ~ScriptNodeBase() {
        if (L_main) _gl_soltar_vm();   // por si no pasó por EXIT_TREE
    }

    // Suelta el hilo de este script de su contexto (lo que antes era lua_close).
    void _gl_soltar_vm();
    // Monta los globales, el stdlib y el puente de un contexto nuevo.
    void _gl_montar_contexto(lua_State* L_main, bool es_cliente, Node* game_node, Node* ws_node);
    // Este nodo pasa a ser el dueño de las tareas del sistema del contexto `c`.
    void gl_hacer_sistema(GLContexto* c) {
        L_main = c->L;
        _gl_ctx_id = c->id;
        _gl_es_sistema = true;
        script_started = true;
        script_finished = true;
        _gl_mark(5, "sistema del contexto (tareas del stdlib)");
    }

    void set_codigo_luau(const Ref<LuauScript> &s) { codigo_luau = s; }
    void gl_rescue_codigo_luau();
    Ref<LuauScript> get_codigo_luau() const { return codigo_luau; }

    void set_script_id(const String &p_id) { script_id = p_id; }
    String get_script_id() const { return script_id; }

    void iniciar_corrutina();
    void setup_require_system(lua_State* L);
    void setup_remotefunction_system(lua_State* L);
    void resume();

    // ── Señales de instancia que Godot no tiene por nodo ──────────────────
    // ChildAdded/ChildRemoved, DescendantAdded/Removing, Destroying y
    // AncestryChanged (GLRelayKind en gl_runtime.h). Godot solo deja conectar
    // UNA vez el mismo metodo a la misma señal de un objeto: compara el Callable
    // SIN sus argumentos atados (Object::connect usa get_base_comparator). Con
    // un Callable por conexion, el segundo Connect de un script sobre la misma
    // instancia no se conectaba nunca, y con DescendantAdded ni siquiera sobre
    // otra carpeta, porque esa escucha al SceneTree. Ahora se conecta UNA vez
    // por objeto y señal, y los manejadores de este script viven aqui.
    struct GLRelay { uint64_t target_id; uint8_t kind; int ref; int id; };
    std::vector<GLRelay> relays;
    int relay_siguiente = 0;

    static void _gl_rel_conectar(Object* o, const char* sig, const Callable& c);
    int _gl_relay_connect(int64_t target_id, int kind, int ref);
    void _gl_relay_disconnect(int id);
    void _gl_rel_uno(const GLRelay& r, int nargs, uint64_t id1, uint64_t id2);
    void _gl_rel_disparar(uint64_t target_id, uint8_t kind, int nargs, Node* a1, Node* a2);
    static bool _gl_va_a_morir(Node* n);
    void _gl_rel_hijo(Node* hijo, int64_t target_id, int kind);
    void _gl_rel_desc(Node* nodo, int kind);
    void _gl_rel_destruye(int64_t target_id);
    void _gl_rel_ancestros(int64_t target_id, int fase);
    void _process(double delta) override;
    void _ready() override;
};

// El script dueño de un hilo (definida al final; la usan los cuerpos de abajo).
inline ScriptNodeBase* gl_script_de(lua_State* L);

// Los cuerpos de los metodos van por zonas:
#include "luau_script_ciclo.h"
#include "luau_script_arranque.h"
#include "luau_script_contexto.h"

// ════════════════════════════════════════════════════════════════════
//  Concrete script types
////  Tipos concretos de script
// ════════════════════════════════════════════════════════════════════
class ServerScript : public ScriptNodeBase {
    GDCLASS(ServerScript, ScriptNodeBase);
protected:
    static void _bind_methods() {
        // Como en Roblox Studio: Enabled true por defecto; desmarcado no corre.
        // Solo los scripts ejecutables la exponen (los métodos viven en la base).
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Enabled"), "set_enabled", "get_enabled");
    }
};

class LocalScript : public ScriptNodeBase {
    GDCLASS(LocalScript, ScriptNodeBase);
protected:
    static void _bind_methods() {
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Enabled"), "set_enabled", "get_enabled");
    }
};

// ModuleScript NO expone "Enabled" (igual que Roblox Studio): un módulo no se
// ejecuta por sí mismo, solo se hace require(). Hereda source/script_id de la base.
class ModuleScript : public ScriptNodeBase {
    GDCLASS(ModuleScript, ScriptNodeBase);
protected:
    static void _bind_methods() {}
};

// ════════════════════════════════════════════════════════════════════
//  Planificador para el resto del puente (declaradas en gl_errors.h)
// ════════════════════════════════════════════════════════════════════

// El script dueño del hilo `L` (por sus datos de hilo; ver gl_contexto.h). Un
// hilo del propio contexto da su nodo de sistema; uno de un script ya muerto,
// nullptr.
inline ScriptNodeBase* gl_script_de(lua_State* L) {
    Node* nd = gl_nodo_script_de(L);
    return nd ? gl_cast<ScriptNodeBase>(nd) : nullptr;
}

inline int gl_yield_externo(lua_State* L, const char* centinela) {
    ScriptNodeBase* sn = gl_script_de(L);
    if (sn && lua_isyieldable(L) && !sn->gl_hilo_gestionado(L)) {
        sn->gl_adoptar_hilo(L, -1.0);
        return lua_yield(L, 0);
    }
    lua_pushstring(L, centinela);
    return lua_yield(L, 1);
}

// Profundidad de manejadores anidados: un manejador que dispara su propia
// señal se llamaria a si mismo sin fin y tumbaria el proceso con la pila de C.
// Roblox corta igual ("Maximum event re-entrancy depth exceeded", a 80); aqui
// algo antes, porque cada nivel es un lua_resume entero en la pila de C.
inline int& gl_profundidad_manejadores() { static int d = 0; return d; }

inline void gl_resume_handler(lua_State* th, int nargs) {
    int& prof = gl_profundidad_manejadores();
    if (prof >= 60) {
        UtilityFunctions::push_error("[GodotLuau] Maximum event re-entrancy depth exceeded");
        return;
    }
    ++prof;
    ScriptNodeBase* sn = gl_script_de(th);
    if (sn) sn->gl_start_task(th, nargs);
    else    gl_check_resume(th, lua_resume(th, nullptr, nargs));
    --prof;
}

#endif // LUAU_SCRIPT_NODE_H
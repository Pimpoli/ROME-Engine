#ifndef GL_ERRORS_H
#define GL_ERRORS_H

// ════════════════════════════════════════════════════════════════════
//  Reporte de errores de scripts estilo Roblox (usado por TODOS los
//  puntos que resumen corrutinas: scripts, señales, remotes, tweens...)
//
//  La linea roja del error es identica a la de Roblox porque el
//  chunkname de cada script es su ruta completa:
//      ServerScriptService.Server:5: attempt to index nil with 'Name'
//  y debajo va el stack en azul, formato Roblox exacto:
//      Stack Begin
//      Script 'ServerScriptService.Server', Line 5 - function foo
//      Stack End
//  (Luau NO desenrolla el stack de una corrutina muerta por error,
//  asi que lua_debugtrace justo despues del resume da el stack real.)
// ════════════════════════════════════════════════════════════════════

#include "lua.h"
#include "lualib.h"
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <vector>
#include <string>

inline constexpr const char* GL_ERR_COLOR   = "#ff5449";   // rojo error Roblox
inline constexpr const char* GL_STACK_COLOR = "#5db2ff";   // azul info Roblox
inline constexpr const char* GL_WARN_COLOR  = "#ff9e2c";   // naranja warning Roblox

// Ruta completa estilo Roblox ("ServerScriptService.Server.Modulo").
// Se usa en GetFullName, en los mensajes de error y como chunkname de scripts.
inline godot::String gl_instance_fullname(godot::Node* n) {
    std::vector<std::string> parts;
    godot::Node* cur = n;
    while (cur && cur->get_parent()) {
        parts.push_back(godot::String(cur->get_name()).utf8().get_data());
        cur = cur->get_parent();
    }
    // Excluir el nodo raiz de la escena (el "game"): Roblox no lo incluye
    if (parts.size() > 1) parts.pop_back();
    std::string result;
    for (int i = (int)parts.size() - 1; i >= 0; i--) {
        if (!result.empty()) result += ".";
        result += parts[i];
    }
    return godot::String(result.c_str());
}

// Convierte un traceback de lua_debugtrace ("chunk:12 function foo\n...")
// al bloque Roblox "Stack Begin / Script '...', Line N - function foo / Stack End".
inline godot::String gl_format_roblox_stack(const godot::String& trace) {
    godot::String out = godot::String("[color=") + GL_STACK_COLOR + "]Stack Begin[/color]";
    godot::PackedStringArray lines = trace.split("\n", false);
    for (int i = 0; i < lines.size(); i++) {
        godot::String ln = lines[i];
        if (ln.begins_with("[C]")) continue;   // Roblox no muestra frames C
        godot::String fn;
        int fpos = ln.find(" function ");
        if (fpos >= 0) { fn = ln.substr(fpos + 10); ln = ln.substr(0, fpos); }
        int c = ln.rfind(":");
        godot::String chunk = ln, lnum;
        if (c >= 0) { chunk = ln.substr(0, c); lnum = ln.substr(c + 1); }
        godot::String row = godot::String("Script '") + chunk + "'";
        if (!lnum.is_empty()) row += godot::String(", Line ") + lnum;
        if (!fn.is_empty())   row += godot::String(" - function ") + fn;
        out += godot::String("\n[color=") + GL_STACK_COLOR + "]" + row + "[/color]";
    }
    out += godot::String("\n[color=") + GL_STACK_COLOR + "]Stack End[/color]";
    return out;
}

// Imprime el error que esta en el tope de la pila de `th` + el stack dado.
inline void gl_report_script_error_with_trace(lua_State* th, const godot::String& trace) {
    // Freno de REENTRADA. Si al informar de un error se produce otro (formatear
    // la traza toca la pila de Lua, y print_rich puede acabar en otro script),
    // se entra otra vez aqui y la pila de C se desborda: crash. Pasaba con los
    // scripts que llaman a error() a proposito.
    static bool reporting = false;
    if (reporting) return;
    reporting = true;
    static bool* _flag = &reporting;
    struct Guard { ~Guard() { *_flag = false; } } _guard;
    const char* raw = lua_tostring(th, -1);
    godot::String msg = raw ? godot::String::utf8(raw) : godot::String("Unknown error");
    godot::UtilityFunctions::print_rich(godot::String("[color=") + GL_ERR_COLOR + "]" + msg + "[/color]");
    godot::UtilityFunctions::print_rich(gl_format_roblox_stack(trace));
}

// Version directa: usable tras lua_resume fallido (el stack sigue vivo).
inline void gl_report_script_error(lua_State* th) {
    gl_report_script_error_with_trace(th, godot::String::utf8(lua_debugtrace(th)));
}

// Linea roja suelta (errores de sintaxis / require): sin stack.
inline void gl_report_error_line(const godot::String& msg) {
    godot::UtilityFunctions::print_rich(godot::String("[color=") + GL_ERR_COLOR + "]" + msg + "[/color]");
}

// Para lua_pcall (que SI desenrolla el stack): message handler que captura el
// traceback EN el momento del error; leerlo despues con gl_pcall_trace().
// (static local, NO global: un String global se construiria al cargar la DLL,
// antes de que godot-cpp este inicializado, y revienta la carga con error 1114)
inline godot::String& gl_pcall_trace() {
    static godot::String s;
    return s;
}
inline int gl_trace_handler(lua_State* L) {
    gl_pcall_trace() = godot::String::utf8(lua_debugtrace(L));
    return 1;   // devuelve el mismo objeto de error
}

// Chequeo estandar tras lua_resume de un callback de usuario.
inline void gl_check_resume(lua_State* th, int status) {
    if (status != LUA_OK && status != LUA_YIELD) gl_report_script_error(th);
}

// ── Planificador (definidas en luau_script.h, donde vive ScriptNodeBase) ──
// Se declaran aqui porque los headers que despachan señales se incluyen antes.
//
// Corre un manejador de señal como tarea del planificador de su script: la
// funcion y sus `nargs` argumentos ya estan en la pila de `th`, un hilo recien
// creado con lua_newthread. Si el manejador cede (task.wait, WaitForChild,
// :Wait()...), el script lo reanuda despues; con gl_check_resume a secas el
// hilo quedaba suspendido para siempre y el resto del manejador se perdia.
inline void gl_resume_handler(lua_State* th, int nargs);
// Cede `L` esperando algo de fuera (un hijo, una señal, una respuesta) que la
// reanudara con resume_external_thread. Si `L` es una corrutina del usuario
// (coroutine.create/wrap) que el planificador no conoce, la adopta y cede sin
// valores, para que la marca interna no le llegue al que la reanudo.
inline int gl_yield_externo(lua_State* L, const char* centinela);

#endif // GL_ERRORS_H

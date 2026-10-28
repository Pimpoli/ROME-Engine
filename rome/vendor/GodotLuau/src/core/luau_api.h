#ifndef LUAU_API_H
#define LUAU_API_H

#include "gl_errors.h"
#include "gl_cast.h"
#include "gl_stackguard.h"
#include "gl_enum_props.h"   // que propiedades son enum en Roblox (F8)
#include <godot_cpp/classes/tween.hpp>       // TweenSize/TweenPosition de GuiObject
#include <godot_cpp/classes/property_tweener.hpp>  // lo devuelve tween_property()
#include <godot_cpp/classes/scene_tree.hpp>
#include "roblox_extra.h"   // nodos puente (VideoFrame, constraints, efectos de sonido...)
#include "folder.h"
#include "humanoid.h"
#include "humanoid2d.h"
#include "roblox_player.h"
#include "roblox_player2d.h"
#include "roblox_part.h"
#include "roblox_terrain.h"
#include "roblox_services.h"
#include "roblox_network.h"
#include "roblox_remote.h"
#include "roblox_workspace.h"
#include "roblox_bodymovers.h"
#include "roblox_constraints.h"
#include "roblox_gui.h"
#include "roblox_tween.h"
#include "roblox_sound.h"
#include "roblox_interactive.h"
#include "roblox_billboard.h"
#include "roblox_input.h"
#include "roblox_animation.h"
#include "gl_viewport_frame.h"   // ViewportFrame, Camera y WorldModel

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/spot_light3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"

#include "gl_runtime.h"   // GodotObjectWrapper (ObjectID), gow_node/gow_set, registro de estados vivos
#include "gl_clone.h"   // Clone() rapido + marca de hijos internos
#include "gl_colgroups.h" // matriz real de grupos de colision
#include "gl_invocar.h"   // Invoke/InvokeServer locales que pueden ceder
#include "gl_tienda.h"    // MarketplaceService compartido entre scripts

#include <cmath>
#include <cstring>
#include <functional>

using namespace godot;

// Roblox mantiene por compatibilidad los nombres en MINUSCULA de las señales
// (:connect, :wait, :once). Muchos juegos antiguos los usan y sin ellos el script
// muere con "attempt to call missing method 'connect'". Esto copia el metodo ya
// puesto en la tabla de señal a su alias en minuscula.
// Callbacks registrados con game:BindToClose (se ejecutan al cerrar el juego).
inline std::vector<int>& gl_bind_to_close_refs() { static std::vector<int> v; return v; }

inline void gl_signal_lowercase_aliases(lua_State* L) {
    // La tabla de la señal esta en el tope de la pila.
    // Once y Wait genericos (escritos en Luau en luau_stdlib.h, sobre Connect)
    // para las señales que solo traian Connect: un :Once() o un :Wait() sobre
    // Changed, ChildAdded, Touched... era "attempt to call a nil value". Las que
    // tienen los suyos los ponen despues y mandan esos.
    static const char* kGenericos[][2] = { {"Once","__gl_sig_once"}, {"Wait","__gl_sig_wait"} };
    for (int i = 0; i < 2; i++) {
        lua_getfield(L, -1, kGenericos[i][0]);
        const bool falta = lua_isnil(L, -1);
        lua_pop(L, 1);
        if (!falta) continue;
        lua_getfield(L, LUA_REGISTRYINDEX, kGenericos[i][1]);
        lua_setfield(L, -2, kGenericos[i][0]);
    }
    static const char* kPairs[][2] = { {"Connect","connect"}, {"Wait","wait"}, {"Once","once"} };
    for (int i = 0; i < 3; i++) {
        lua_getfield(L, -1, kPairs[i][0]);
        if (!lua_isnil(L, -1)) lua_setfield(L, -2, kPairs[i][1]);
        else lua_pop(L, 1);
    }
}


// ════════════════════════════════════════════════════════════════════
//  Data structures for Luau types
////
//  Estructuras de datos para los tipos Luau
//  (GodotObjectWrapper vive en gl_runtime.h: guarda ObjectID, no Node* crudo)
// ════════════════════════════════════════════════════════════════════

struct LuauVector3 {
    float x, y, z;
    float magnitude() const { return std::sqrt(x*x + y*y + z*z); }
    LuauVector3 unit() const {
        float m = magnitude();
        if (m < 0.0001f) return {0.0f, 0.0f, 0.0f};
        return {x/m, y/m, z/m};
    }
    LuauVector3 lerp(const LuauVector3& to, float alpha) const {
        return {x + (to.x - x)*alpha, y + (to.y - y)*alpha, z + (to.z - z)*alpha};
    }
    float dot(const LuauVector3& b) const { return x*b.x + y*b.y + z*b.z; }
    LuauVector3 cross(const LuauVector3& b) const {
        return {y*b.z - z*b.y, z*b.x - x*b.z, x*b.y - y*b.x};
    }
};

struct LuauColor3 { float r, g, b; };
struct LuauCFrame { Transform3D transform; };

// ════════════════════════════════════════════════════════════════════
//  Internal helpers
////
//  Helpers internos
// ════════════════════════════════════════════════════════════════════

// Envuelve un nodo para Luau. Hay UN wrapper por instancia y por VM, guardado
// en una cache con valores debiles (el recolector puede soltar los que ya nadie
// use).
//
// Antes se reservaba un userdata NUEVO en cada acceso. Dos consecuencias:
//   · Rendimiento: un bucle que recorre los descendientes de decenas de modelos
//     por frame hacia miles de reservas por frame. Medido en el place de
//     prueba, ese patron era el script mas caro con diferencia.
//   · IDENTIDAD: como cada acceso daba un userdata distinto, usar una instancia
//     como CLAVE de tabla (`cache[objeto] = datos`, idioma comun en Roblox)
//     metia una entrada nueva cada vez y nunca encontraba la anterior. Con la
//     cache, la misma instancia es literalmente el mismo valor, como en Roblox.
inline void wrap_node(lua_State* L, Node* node) { gl_wrap_node(L, node); }

// Transform de un nodo tal y como lo espera un script de Roblox. Fuera del
// arbol, get_global_transform() de Godot suelta un ERROR por consola y devuelve
// la identidad; en Roblox una instancia sin padre conserva su CFrame, asi que
// se usa el transform local. (Es normalisimo: Instance.new(...) y configurarlo
// antes de asignarle Parent.)
inline Transform3D gl_node_xf(Node3D* n) {
    if (!n) return Transform3D();
    return n->is_inside_tree() ? n->get_global_transform() : n->get_transform();
}

// Un CFrame es una tabla con X/Y/Z + m00..m22 y la metatabla global `CFrame`
// (el almacenamiento canónico del stdlib; los accesores Position/LookVector se
// calculan al leerlos, ver luau_stdlib.h).
//
// RENDIMIENTO: esto se construía llamando a `CFrame.new` con lua_pcall. Es un
// camino CALIENTE — cualquier bucle que recorra objetos llama a GetPivot() o
// lee .CFrame en cada vuelta — y cada pcall monta un marco de llamada de Luau
// entero. Medido en el place de prueba: un solo script (TodoElCliente, que
// recorre los brainrots cada frame) se comía 45 ms de los 112 ms del frame.
// Aquí la tabla se arma en C++ y solo se le engancha la metatabla: mismo
// resultado, sin entrar al intérprete.
inline void _gl_push_cframe_raw(lua_State* L, const Vector3& p, const Basis* b) {
    lua_createtable(L, 0, 12);
    lua_pushnumber(L, p.x); lua_setfield(L, -2, "X");
    lua_pushnumber(L, p.y); lua_setfield(L, -2, "Y");
    lua_pushnumber(L, p.z); lua_setfield(L, -2, "Z");
    static const char* nm[9] = {"m00","m01","m02","m10","m11","m12","m20","m21","m22"};
    if (b) {
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) {
                lua_pushnumber(L, b->rows[r][c]);
                lua_setfield(L, -2, nm[r * 3 + c]);
            }
    } else {
        // identidad
        for (int i = 0; i < 9; i++) {
            lua_pushnumber(L, (i % 4 == 0) ? 1.0 : 0.0);
            lua_setfield(L, -2, nm[i]);
        }
    }
    lua_getglobal(L, "CFrame");
    if (lua_istable(L, -1)) lua_setmetatable(L, -2);
    else lua_pop(L, 1);
}
inline void _gl_push_cframe_pos(lua_State* L, const Vector3& p) {
    _gl_push_cframe_raw(L, p, nullptr);
}
inline void _gl_push_cframe_full(lua_State* L, const Transform3D& t) {
    _gl_push_cframe_raw(L, t.origin, &t.basis);
}
inline Transform3D _gl_read_cframe_tbl(lua_State* L, int idx) {
    Transform3D t;
    auto f = [&](const char* k) -> float {
        lua_getfield(L, idx, k); float v = (float)lua_tonumber(L, -1); lua_pop(L, 1); return v;
    };
    t.origin = Vector3(f("X"), f("Y"), f("Z"));
    t.basis.rows[0] = Vector3(f("m00"), f("m01"), f("m02"));
    t.basis.rows[1] = Vector3(f("m10"), f("m11"), f("m12"));
    t.basis.rows[2] = Vector3(f("m20"), f("m21"), f("m22"));
    return t;
}

inline LuauVector3* push_vector3(lua_State* L, float x, float y, float z) {
    LuauVector3* v = (LuauVector3*)lua_newuserdata(L, sizeof(LuauVector3));
    v->x = x; v->y = y; v->z = z;
    luaL_getmetatable(L, "Vector3");
    lua_setmetatable(L, -2);
    return v;
}

inline LuauColor3* push_color3(lua_State* L, float r, float g, float b) {
    LuauColor3* c = (LuauColor3*)lua_newuserdata(L, sizeof(LuauColor3));
    c->r = r; c->g = g; c->b = b;
    luaL_getmetatable(L, "Color3");
    lua_setmetatable(L, -2);
    return c;
}

// ── BrickColor (1.15) ────────────────────────────────────────────────────────
//  La paleta vive en el stdlib de Luau, así que el puente no la duplica: llama
//  al BrickColor.new de allí. En C++ un TeamColor se guarda como el NOMBRE del
//  BrickColor, que es su identidad en Roblox.
inline void push_brickcolor(lua_State* L, const String& name) {
    lua_getglobal(L, "BrickColor");
    if (!lua_istable(L, -1)) {   // sin stdlib cargado: al menos no romper
        lua_pop(L, 1);
        lua_pushstring(L, name.utf8().get_data());
        return;
    }
    lua_getfield(L, -1, "new");
    lua_remove(L, -2);
    lua_pushstring(L, name.utf8().get_data());
    lua_call(L, 1, 1);
}
// Registra un callback de hover en cualquier GuiObject que lo soporte.
// Un solo sitio para que MouseEnter/MouseLeave funcionen igual en Frame,
// TextLabel, ImageLabel, ScrollingFrame y los dos botones.
inline bool gl_gui_add_hover_cb(Node* n, bool entra, lua_State* mL, int ref) {
    #define GL_HOVER_TRY(T) \
        if (T* c = gl_cast<T>(n)) { \
            if (entra) c->add_enter_cb(mL, ref); else c->add_leave_cb(mL, ref); \
            return true; \
        }
    GL_HOVER_TRY(RobloxTextButton)
    GL_HOVER_TRY(RobloxImageButton)
    GL_HOVER_TRY(RobloxScrollingFrame)
    GL_HOVER_TRY(RobloxTextLabel)
    GL_HOVER_TRY(RobloxImageLabel)
    GL_HOVER_TRY(RobloxFrame)
    #undef GL_HOVER_TRY
    return false;
}

//  Vector2 real (usa el global de Luau) para AbsolutePosition/AbsoluteSize etc.
inline void push_vector2(lua_State* L, float x, float y) {
    lua_getglobal(L, "Vector2");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "new");
        lua_remove(L, -2);
        lua_pushnumber(L, x);
        lua_pushnumber(L, y);
        lua_call(L, 2, 1);
        return;
    }
    lua_pop(L, 1);            // sin stdlib: tabla plana {X,Y}
    lua_newtable(L);
    lua_pushnumber(L, x); lua_setfield(L, -2, "X");
    lua_pushnumber(L, y); lua_setfield(L, -2, "Y");
}

// UDim2 de verdad (el UDim2.new del stdlib). Una tabla pelada no tenia
// metatabla: `frame.Size == objetivo` daba false siempre, y + / :Lerp /
// typeof fallaban sobre lo que devolvia la propia GUI.
inline void gl_push_udim2(lua_State* L, float xs, float xo, float ys, float yo) {
    lua_getglobal(L, "UDim2");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "new");
        lua_remove(L, -2);
        if (lua_isfunction(L, -1)) {
            lua_pushnumber(L, xs); lua_pushnumber(L, xo);
            lua_pushnumber(L, ys); lua_pushnumber(L, yo);
            lua_call(L, 4, 1);
            return;
        }
    }
    lua_pop(L, 1);            // sin stdlib: tabla plana {X={Scale,Offset},Y=...}
    lua_newtable(L);
    auto eje = [&](float esc, float off, const char* campo) {
        lua_newtable(L);
        lua_pushnumber(L, esc); lua_setfield(L, -2, "Scale");
        lua_pushnumber(L, off); lua_setfield(L, -2, "Offset");
        lua_setfield(L, -2, campo);
    };
    eje(xs, xo, "X");
    eje(ys, yo, "Y");
}

//  Acepta lo mismo que Roblox: un BrickColor, un nombre o un número.
inline String read_brickcolor_name(lua_State* L, int idx) {
    if (idx < 0) idx = lua_gettop(L) + 1 + idx;
    if (lua_istable(L, idx)) {
        lua_getfield(L, idx, "Name");
        String s = lua_isstring(L, -1) ? String(lua_tostring(L, -1)) : String();
        lua_pop(L, 1);
        return s;
    }
    if (lua_isstring(L, idx)) return String(lua_tostring(L, idx));
    if (lua_isnumber(L, idx)) {   // número → nombre, vía la paleta de Luau
        lua_getglobal(L, "BrickColor");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "new");
            lua_remove(L, -2);
            lua_pushvalue(L, idx);
            lua_call(L, 1, 1);
            lua_getfield(L, -1, "Name");
            String s = lua_isstring(L, -1) ? String(lua_tostring(L, -1)) : String();
            lua_pop(L, 2);
            return s;
        }
        lua_pop(L, 1);
    }
    return String();
}

// ── Comprobación de tipo segura: solo devuelve el puntero si el userdata
//    tiene EXACTAMENTE la metatable esperada. Evita reinterpretar un tipo
//    por otro (p.ej. pasar un Color3 donde se espera un Vector3, o un nodo
//    donde se espera un Vector3 → lectura fuera de límites). ────────────────
// Enum.PartType de Roblox (Ball=0, Block=1) <-> Shape del C++ (Block=0,
// Ball=1). El cambio es su propia inversa, asi que sirve en los dos sentidos.
// Sin esto `parte.Shape = Enum.PartType.Ball` hacia un cubo.
inline int gl_shape_roblox(int s) {
    if (s == 0) return 1;
    if (s == 1) return 0;
    return s;
}

inline bool _gl_has_metatable(lua_State* L, int idx, const char* mt) {
    if (lua_type(L, idx) != LUA_TUSERDATA) return false;
    if (!lua_getmetatable(L, idx)) return false;
    luaL_getmetatable(L, mt);
    bool ok = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return ok;
}
inline LuauVector3* check_vector3(lua_State* L, int idx) {
    return _gl_has_metatable(L, idx, "Vector3") ? (LuauVector3*)lua_touserdata(L, idx) : nullptr;
}
inline LuauColor3* check_color3(lua_State* L, int idx) {
    return _gl_has_metatable(L, idx, "Color3") ? (LuauColor3*)lua_touserdata(L, idx) : nullptr;
}

// ════════════════════════════════════════════════════════════════════
//  Puente genérico de propiedades (clases nuevas con meta "_gl_bridge")
//  Permite leer/escribir cualquier propiedad registrada en el ClassDB con
//  nombre estilo Roblox (ADD_PROPERTY) sin cablearla a mano en el index/
//  newindex. Solo se activa en nodos que ponen set_meta("_gl_bridge", true)
//  en su constructor, así que NO afecta a las clases existentes.
// ════════════════════════════════════════════════════════════════════
// El listado de propiedades depende SOLO de la clase (ninguna clase nuestra
// implementa _get_property_list, o sea que no hay propiedades por instancia).
// Preguntarlo cada vez era carisimo: get_property_list() fabrica un Dictionary
// por propiedad y cruza el limite de la GDExtension otras tantas veces, y una
// Part tiene ~180 entre las suyas, RigidBody3D, CollisionObject3D, Node3D y
// Node. Medido en el place de prueba: una escritura de Anchored costaba 162 us
// (y congelar el cuerpo de verdad son 0,5 us: TODO el tiempo era esta consulta).
// Tambien lo pagaban las escrituras del puente generico: Enabled 145 us,
// CornerRadius 52, Thickness 50, ApplyStrokeMode 46...
// Se calcula una vez por clase y se recuerda.
inline bool gl_has_property(Object* o, const String& pname) {
    if (!o) return false;
    static HashMap<StringName, HashMap<StringName, bool>> cache;
    const StringName clase = o->get_class();
    HashMap<StringName, HashMap<StringName, bool>>::Iterator it = cache.find(clase);
    if (!it) {
        HashMap<StringName, bool> props;
        TypedArray<Dictionary> pl = o->get_property_list();
        for (int i = 0; i < pl.size(); i++) {
            Dictionary d = pl[i];
            const String nombre = d.get("name", String());
            if (!nombre.is_empty()) props.insert(StringName(nombre), true);
        }
        cache.insert(clase, props);
        it = cache.find(clase);
        if (!it) return false;
    }
    return it->value.has(StringName(pname));
}
// ── Numero que TAMBIEN acepta un EnumItem ────────────────────────────────────
//  En Roblox, Enum.Material.Neon es un OBJETO con .Name, .Value y .EnumType,
//  no un numero. Aqui eran numeros pelados (todo el puente los lee asi) y
//  cualquier script que hiciera `parte.Material.Name` moria.
//
//  Al convertirlos en EnumItem hay que aceptar las dos formas en el puente:
//  el numero de siempre y el objeto nuevo (del que se saca .Value). Esta
//  funcion sustituye a luaL_checknumber en TODO el puente; para un numero se
//  comporta exactamente igual, asi que no cambia nada de lo que ya funcionaba.
//
//  Lua no puede hacer que una tabla sea == a un numero (__eq solo salta entre
//  dos tablas), asi que la conversion tiene que ocurrir en el limite C++.
inline double gl_num(lua_State* L, int idx) {
    if (lua_type(L, idx) == LUA_TTABLE) {
        lua_getfield(L, idx, "Value");
        if (lua_isnumber(L, -1)) { double v = lua_tonumber(L, -1); lua_pop(L, 1); return v; }
        lua_pop(L, 1);
    }
    return luaL_checknumber(L, idx);
}
// Igual que gl_num pero para CADENAS. Hace falta por el mismo motivo: hay
// propiedades que se escriben con texto o con enum indistintamente
// (prompt.KeyboardKeyCode = "E" o = Enum.KeyCode.E). Cuando el enum era un
// numero, Lua lo convertia a cadena solo; siendo un objeto ya no, asi que se
// coge su .Name — que es justo el texto que esperaban esos setters.
inline const char* gl_str(lua_State* L, int idx) {
    if (lua_type(L, idx) == LUA_TTABLE) {
        lua_getfield(L, idx, "Name");
        if (lua_isstring(L, -1)) {
            const char* s = lua_tostring(L, -1);
            lua_pop(L, 1);
            return s;   // vive en la tabla del EnumItem, no en la pila temporal
        }
        lua_pop(L, 1);
    }
    return luaL_checkstring(L, idx);
}

// Ruta para _FileAccess: solo user://, sin subir de carpeta ("../") ni barras
// invertidas. Otra cosa es un error de Luau y el script no toca el disco. (Dos
// puntos dentro de un nombre, como un topic "a..b" del MessagingService, valen.)
inline String gl_ruta_datos(lua_State* L, int idx) {
    const String p = String::utf8(luaL_checkstring(L, idx));
    if (!p.begins_with("user://") || p.find("../") >= 0 || p.find("/..") >= 0 || p.find("\\") >= 0)
        luaL_error(L, "_FileAccess only allows user:// paths");
    return p;
}

// Version tolerante: como gl_num pero sin reventar si no es numero.
inline double gl_num_or(lua_State* L, int idx, double def) {
    if (lua_isnumber(L, idx)) return lua_tonumber(L, idx);
    if (lua_type(L, idx) == LUA_TTABLE) {
        lua_getfield(L, idx, "Value");
        if (lua_isnumber(L, -1)) { double v = lua_tonumber(L, -1); lua_pop(L, 1); return v; }
        lua_pop(L, 1);
    }
    return def;
}

inline bool gl_luau_to_variant(lua_State* L, int idx, Variant& out) {
    switch (lua_type(L, idx)) {
        case LUA_TNUMBER:  out = (double)lua_tonumber(L, idx); return true;
        case LUA_TBOOLEAN: out = (bool)(lua_toboolean(L, idx) != 0); return true;
        case LUA_TSTRING:  out = String(lua_tostring(L, idx)); return true;
        case LUA_TTABLE: {
            // Un EnumItem escrito en una propiedad generica va como su numero.
            lua_getfield(L, idx, "Value");
            if (lua_isnumber(L, -1)) { out = (double)lua_tonumber(L, -1); lua_pop(L, 1); return true; }
            lua_pop(L, 1);
            // Un CFrame (tabla con m00..m22) va como Transform3D: AlignOrientation.CFrame,
            // Weld.C0... Antes no se sabia guardar y la escritura se perdia.
            if (idx < 0) idx = lua_gettop(L) + 1 + idx;
            // NumberSequence / ColorSequence: Array de Dictionary {Time,
            // Value | Color, Envelope}, lo mismo que entrega el importador.
            // Antes se perdian: emitter.Color = ColorSequence.new(...) no hacia nada.
            lua_getfield(L, idx, "Keypoints");
            if (lua_istable(L, -1)) {
                Array kps;
                const int nk = (int)lua_objlen(L, -1);
                for (int i = 1; i <= nk; i++) {
                    lua_rawgeti(L, -1, i);
                    if (lua_istable(L, -1)) {
                        Dictionary kp;
                        lua_getfield(L, -1, "Time");
                        kp["Time"] = (double)lua_tonumber(L, -1);
                        lua_pop(L, 1);
                        lua_getfield(L, -1, "Value");
                        if (lua_isnumber(L, -1)) kp["Value"] = (double)lua_tonumber(L, -1);
                        else if (LuauColor3* c = check_color3(L, -1)) kp["Color"] = Color(c->r, c->g, c->b);
                        lua_pop(L, 1);
                        lua_getfield(L, -1, "Envelope");
                        kp["Envelope"] = (double)lua_tonumber(L, -1);
                        lua_pop(L, 1);
                        kps.push_back(kp);
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
                out = kps;
                return true;
            }
            lua_pop(L, 1);
            // NumberRange: Vector2(Min, Max), como el importador.
            lua_getfield(L, idx, "Min");
            lua_getfield(L, idx, "Max");
            if (lua_isnumber(L, -2) && lua_isnumber(L, -1)) {
                out = Vector2((float)lua_tonumber(L, -2), (float)lua_tonumber(L, -1));
                lua_pop(L, 2);
                return true;
            }
            // Rect (Min y Max son Vector2): Rect2, como el importador. Es el
            // SliceCenter de las imagenes; antes la escritura se perdia.
            if (lua_istable(L, -2) && lua_istable(L, -1)) {
                lua_getfield(L, -2, "X"); lua_getfield(L, -3, "Y");
                lua_getfield(L, -3, "X"); lua_getfield(L, -4, "Y");
                const bool es_rect = lua_isnumber(L, -4) && lua_isnumber(L, -3) && lua_isnumber(L, -2) && lua_isnumber(L, -1);
                if (es_rect) {
                    const float x0 = (float)lua_tonumber(L, -4), y0 = (float)lua_tonumber(L, -3);
                    const float x1 = (float)lua_tonumber(L, -2), y1 = (float)lua_tonumber(L, -1);
                    out = Rect2(x0, y0, x1 - x0, y1 - y0);
                }
                lua_pop(L, 6);
                if (es_rect) return true;
            } else {
                lua_pop(L, 2);
            }
            lua_getfield(L, idx, "m00");
            const bool es_cframe = lua_isnumber(L, -1);
            lua_pop(L, 1);
            if (es_cframe) { out = _gl_read_cframe_tbl(L, idx); return true; }
            // UDim2 (X e Y son UDim): Vector4(xScale, xOffset, yScale, yOffset),
            // como la lectura. Antes `marco.CanvasSize = UDim2...` se perdia.
            lua_getfield(L, idx, "X");
            lua_getfield(L, idx, "Y");
            if (lua_istable(L, -2) && lua_istable(L, -1)) {
                lua_getfield(L, -2, "Scale"); lua_getfield(L, -3, "Offset");
                lua_getfield(L, -3, "Scale"); lua_getfield(L, -4, "Offset");
                const bool es_udim2 = lua_isnumber(L, -4) && lua_isnumber(L, -3) && lua_isnumber(L, -2) && lua_isnumber(L, -1);
                if (es_udim2) out = Vector4((float)lua_tonumber(L, -4), (float)lua_tonumber(L, -3),
                                            (float)lua_tonumber(L, -2), (float)lua_tonumber(L, -1));
                lua_pop(L, 6);
                if (es_udim2) return true;
            } else {
                lua_pop(L, 2);
            }
            // Un Vector2 (tabla con X e Y numericos y sin Z): PlaneVelocity, AnchorPoint...
            lua_getfield(L, idx, "X");
            lua_getfield(L, idx, "Y");
            lua_getfield(L, idx, "Z");
            const bool es_v2 = lua_isnumber(L, -3) && lua_isnumber(L, -2) && lua_isnil(L, -1);
            if (es_v2) out = Vector2((float)lua_tonumber(L, -3), (float)lua_tonumber(L, -2));
            lua_pop(L, 3);
            return es_v2;
        }
        case LUA_TUSERDATA:
            if (LuauVector3* v = check_vector3(L, idx)) { out = Vector3(v->x, v->y, v->z); return true; }
            if (LuauColor3*  c = check_color3(L, idx))  { out = Color(c->r, c->g, c->b);   return true; }
            return false;
        default: return false;
    }
}
inline bool gl_variant_to_luau(lua_State* L, const Variant& v) {
    switch (v.get_type()) {
        case Variant::BOOL:    lua_pushboolean(L, (bool)v);              return true;
        case Variant::INT:     lua_pushnumber(L, (double)(int64_t)v);    return true;
        case Variant::FLOAT:   lua_pushnumber(L, (double)v);             return true;
        case Variant::STRING:  { String s = v; lua_pushstring(L, s.utf8().get_data()); return true; }
        case Variant::VECTOR3: { Vector3 q = v; push_vector3(L, q.x, q.y, q.z); return true; }
        // VECTOR2 faltaba: cualquier propiedad Vector2 que llegara por el camino
        // generico se leia como nil. La mas visible, AnchorPoint — en TODA clase
        // de UI, no solo en una: `frame.AnchorPoint.X` mataba el script.
        case Variant::VECTOR2: { Vector2 q = v; push_vector2(L, q.x, q.y);      return true; }
        case Variant::VECTOR4: {   // UDim2 (xScale, xOffset, yScale, yOffset): asi van todos
            const Vector4 u = v;
            lua_getglobal(L, "UDim2");
            if (!lua_istable(L, -1)) { lua_pop(L, 1); return false; }
            lua_getfield(L, -1, "new");
            lua_remove(L, -2);
            lua_pushnumber(L, u.x); lua_pushnumber(L, u.y);
            lua_pushnumber(L, u.z); lua_pushnumber(L, u.w);
            lua_call(L, 4, 1);
            return true;
        }
        case Variant::COLOR:   { Color q = v; push_color3(L, q.r, q.g, q.b);    return true; }
        case Variant::TRANSFORM3D: { _gl_push_cframe_full(L, (Transform3D)v); return true; }
        case Variant::RECT2: {   // Rect de Roblox (SliceCenter...)
            const Rect2 r = v;
            lua_getglobal(L, "Rect");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "new");
                lua_remove(L, -2);
                lua_pushnumber(L, r.position.x); lua_pushnumber(L, r.position.y);
                lua_pushnumber(L, r.position.x + r.size.x); lua_pushnumber(L, r.position.y + r.size.y);
                lua_call(L, 4, 1);
                return true;
            }
            lua_pop(L, 1);
            return false;
        }
        default: return false;
    }
}

// Señal "inerte": objeto con Connect/Once/Wait que NO hace nada y NO filtra
// referencias. Se usa para señales aún no soportadas (antes conectaban a
// métodos inexistentes y acumulaban refs sin liberarse nunca).
inline void _gl_push_inert_signal(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, [](lua_State* pL) -> int {
        // Devuelve una conexión con Disconnect()/Connected = false
        lua_newtable(pL);
        lua_pushcfunction(pL, [](lua_State*) -> int { return 0; }, "Disconnect");
        lua_setfield(pL, -2, "Disconnect");
        lua_getfield(pL, -1, "Disconnect");   // alias legacy :disconnect()
        lua_setfield(pL, -2, "disconnect");
        lua_pushboolean(pL, 0); lua_setfield(pL, -2, "Connected");
        return 1;
    }, "Connect");
    lua_setfield(L, -2, "Connect");
    gl_signal_lowercase_aliases(L);
    lua_pushcfunction(L, [](lua_State* pL) -> int { return 0; }, "Once");
    lua_setfield(L, -2, "Once");
    lua_pushcfunction(L, [](lua_State* pL) -> int { lua_pushnil(pL); return 1; }, "Wait");
    lua_setfield(L, -2, "Wait");
}

// `Connected` de una conexion: lo lee de su campo _gl_conn (que Disconnect pone
// a false) a traves de __index, asi que solo salta si la clave no esta en la
// tabla. La conexion esta en la cima de la pila.
inline void gl_conn_connected_mt(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, [](lua_State* mL) -> int {
        const char* k = lua_tostring(mL, 2);
        if (k && strcmp(k, "Connected") == 0) { lua_getfield(mL, 1, "_gl_conn"); return 1; }
        lua_pushnil(mL);
        return 1;
    }, "__index");
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
}

// Las conexiones hechas a mano (ChildAdded, Destroying, AncestryChanged...)
// traen su propio Disconnect: se envuelve para que ademas apague Connected.
inline void gl_conn_add_connected(lua_State* L) {
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "_gl_conn");
    lua_getfield(L, -1, "Disconnect");
    lua_pushcclosure(L, [](lua_State* dL) -> int {
        lua_pushvalue(dL, lua_upvalueindex(1));
        lua_pushvalue(dL, 1);
        lua_call(dL, 1, 0);
        if (lua_istable(dL, 1)) { lua_pushboolean(dL, 0); lua_setfield(dL, 1, "_gl_conn"); }
        return 0;
    }, "Disconnect", 1);
    lua_setfield(L, -2, "Disconnect");
    lua_getfield(L, -1, "Disconnect");   // alias legacy :disconnect()
    lua_setfield(L, -2, "disconnect");
    gl_conn_connected_mt(L);
}

// Devuelve un objeto conexión estilo Roblox: { Disconnect(), Connected }.
// Disconnect resuelve el nodo por ObjectID (seguro tras destruir) y llama al
// método _gl_disconnect(ref) de la clase, que desactiva ese callback.
inline void _gl_push_connection(lua_State* L, Node* node, int ref) {
    lua_newtable(L);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "_gl_conn");   // estado real (mutable) de la conexión
    lua_pushlightuserdata(L, (void*)(uintptr_t)(node ? (uint64_t)node->get_instance_id() : 0));
    lua_pushinteger(L, ref);
    // La VM que conecto. Sin esto, Disconnect apaga "el primer callback con ese
    // numero" y puede matar el de OTRO script (ver GL_MISMA_VM en gl_runtime.h).
    lua_State* conn_L = gl_main_of(L);
    if (!conn_L) conn_L = L;
    lua_pushlightuserdata(L, (void*)conn_L);
    lua_pushcclosure(L, [](lua_State* dL) -> int {
        uint64_t id = (uint64_t)(uintptr_t)lua_touserdata(dL, lua_upvalueindex(1));
        int r = (int)lua_tointeger(dL, lua_upvalueindex(2));
        const int64_t vm = (int64_t)(intptr_t)lua_touserdata(dL, lua_upvalueindex(3));
        Node* n = gl_cast<Node>(ObjectDB::get_instance(id));
        if (n && n->has_method("_gl_disconnect")) n->call("_gl_disconnect", r, vm);
        if (lua_istable(dL, 1)) { lua_pushboolean(dL, 0); lua_setfield(dL, 1, "_gl_conn"); }
        return 0;
    }, "Disconnect", 3);
    lua_setfield(L, -2, "Disconnect");
    lua_getfield(L, -1, "Disconnect");   // alias legacy :disconnect()
    lua_setfield(L, -2, "disconnect");
    // Alias en minuscula: Roblox lo mantiene por compatibilidad y los scripts
    // de animacion que traen los places (Animate.lua) usan connection:disconnect().
    lua_getfield(L, -1, "Disconnect");
    lua_setfield(L, -2, "disconnect");
    // Connected DINÁMICO (1.14.15): antes era un booleano fijo en true, así que
    // seguía diciendo true después de Disconnect().
    gl_conn_connected_mt(L);
}

// ── ClassName / IsA estilo Roblox ────────────────────────────────────────────
// Devuelve el nombre de clase Roblox a partir de la clase Godot interna.
inline const char* gl_roblox_classname(Node* n) {
    if (!n) return "Instance";
    // Los nodos importados de un .rbxl traen la clase Roblox EXACTA en meta
    // (RBXModel→"Model", RBXMeshPart→"MeshPart", RobloxImageLabel→"ImageButton"
    // si eso era en el place...). Es la verdad 1:1: sin esto, ClassName devolvía
    // el nombre interno y cosas como `t:IsA("Model")` fallaban EN SILENCIO
    // (el TycoonSystem de un place saltaba sus 7 parcelas sin decir nada).
    // MEMOIZADO. Esta funcion la llama gl_isa en su segunda linea, y IsA es de
    // lo mas leido del puente (142.737 llamadas cada 10 s medidas con GL_PERFIL).
    // Antes reservaba memoria en CADA llamada: utf8() del meta, o get_class()
    // (que cruza la frontera de la GDExtension y construye un String) + otro
    // utf8(). Todo para devolver un dato que es CONSTANTE por clase.
    //
    // De paso mata un peligro real: los `static CharString` de antes se pisaban
    // en la siguiente llamada, asi que el `const char*` devuelto solo valia
    // hasta que alguien volviera a llamar. Ahora cada cadena vive en su mapa y
    // el puntero es estable (godot HashMap aloja cada elemento aparte, asi que
    // rehashear no mueve los valores).
    static const StringName k_rbx_class("__rbx_class");
    if (n->has_meta(k_rbx_class)) {
        static HashMap<String, CharString> internados;
        const String v = n->get_meta(k_rbx_class);
        HashMap<String, CharString>::Iterator it = internados.find(v);
        if (!it) it = internados.insert(v, v.utf8());
        return it->value.get_data();
    }
    // BindableEvent y BindableFunction comparten clase de Godot; los distingue
    // su marca. Va antes del cache por clase, que no podria separarlos.
    if (BindableEventNode* _bfn = gl_cast<BindableEventNode>(n))
        if (_bfn->es_funcion) return "BindableFunction";
    static HashMap<StringName, CharString> por_clase;
    const StringName clase = n->get_class();
    if (HashMap<StringName, CharString>::Iterator hay = por_clase.find(clase))
        return hay->value.get_data();
    String c = String(clase);
    struct M { const char* g; const char* r; };
    static const M map[] = {
        {"RobloxPart","Part"},{"Folder","Folder"},{"Humanoid","Humanoid"},{"Humanoid2D","Humanoid"},
        {"RobloxPlayer","Player"},{"RobloxPlayer2D","Player"},
        {"OmniLight3D","PointLight"},{"SpotLight3D","SpotLight"},{"DirectionalLight3D","DirectionalLight"},
        {"RemoteEventNode","RemoteEvent"},{"RemoteFunctionNode","RemoteFunction"},{"BindableEventNode","BindableEvent"},
        {"RobloxSound","Sound"},{"RobloxSoundGroup","SoundGroup"},{"RobloxTween","Tween"},{"TweenService","TweenService"},
        {"ScreenGui","ScreenGui"},{"RobloxFrame","Frame"},{"RobloxTextLabel","TextLabel"},
        {"RobloxTextButton","TextButton"},{"RobloxTextBox","TextBox"},{"RobloxImageLabel","ImageLabel"},
        {"RobloxScrollingFrame","ScrollingFrame"},{"ServerScript","Script"},{"LocalScript","LocalScript"},
        {"ModuleScript","ModuleScript"},{"RobloxTool","Tool"},{"ClickDetector","ClickDetector"},
        {"ProximityPrompt","ProximityPrompt"},{"SpawnLocation","SpawnLocation"},{"BillboardGui","BillboardGui"},
        {"SurfaceGui","SurfaceGui"},{"Motor6D","Motor6D"},{"AnimationObject","Animation"},{"AnimationTrack","AnimationTrack"},
        {"RobloxWorkspace","Workspace"},{"RobloxWorkspace2D","Workspace"},{"RobloxChat","TextChatService"},
        {"RobloxTerrain","Terrain"},{"RobloxValue","NumberValue"},{"PlayerObject","Player"},
        {"BodyVelocity","BodyVelocity"},{"BodyPosition","BodyPosition"},{"BodyForce","BodyForce"},
        {"BodyAngularVelocity","BodyAngularVelocity"},{"BodyGyro","BodyGyro"},
        {"WeldConstraint","WeldConstraint"},{"HingeConstraint","HingeConstraint"},
        {"BallSocketConstraint","BallSocketConstraint"},{"RodConstraint","RodConstraint"},{"SpringConstraint","SpringConstraint"},
        // Las del importador creadas por script (las importadas llevan __rbx_class).
        {"RBXDecal","Decal"},{"RBXTexture","Texture"},{"RBXMesh","SpecialMesh"},{"RBXModel","Model"},
        {"RBXMeshPart","MeshPart"},
        // Las de Lighting y CanvasGroup (con Instance.new salian con el nombre de Godot).
        {"LightingSkyNode","Sky"},{"AtmosphereNode","Atmosphere"},{"SunRaysNode","SunRaysEffect"},
        {"RobloxCanvasGroup","CanvasGroup"},
        {nullptr,nullptr}
    };
    // Camino FRIO: solo la primera vez que se ve cada clase.
    const char* r = nullptr;
    for (int i = 0; map[i].g; i++) if (c == map[i].g) { r = map[i].r; break; }
    // Servicios / nodos no mapeados conservan su nombre (Players, Lighting,
    // ReplicatedStorage...).
    HashMap<StringName, CharString>::Iterator nuevo =
        por_clase.insert(clase, r ? String(r).utf8() : c.utf8());
    return nuevo->value.get_data();
}

#include "gl_herencia_gen.h"

// Padre de una clase de Roblox segun su volcado oficial (nullptr si hereda de
// Instance o si no es una clase de Roblox). Busqueda binaria: la tabla va
// ordenada por strcmp.
inline const char* gl_rbx_padre(const char* clase) {
    int lo = 0, hi = GL_RBX_HERENCIA_N - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const int c = strcmp(GL_RBX_HERENCIA[mid][0], clase);
        if (c == 0) return GL_RBX_HERENCIA[mid][1];
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

// IsA estilo Roblox: nombre exacto + jerarquía pragmática + compat con clase Godot.
inline bool gl_isa(Node* n, const char* q) {
    if (!n || !q) return false;
    if (strcmp(q, "Instance") == 0) return true;          // todo es Instance
    const char* rc = gl_roblox_classname(n);
    if (strcmp(rc, q) == 0) return true;
    // BasePart: TODAS las clases de part de Roblox (no solo "Part": un place
    // real está lleno de MeshPart/UnionOperation/Seat/WedgePart...).
    bool basepart = strcmp(rc,"Part")==0||strcmp(rc,"MeshPart")==0||strcmp(rc,"WedgePart")==0||
                    strcmp(rc,"CornerWedgePart")==0||strcmp(rc,"TrussPart")==0||strcmp(rc,"UnionOperation")==0||
                    strcmp(rc,"NegateOperation")==0||strcmp(rc,"IntersectOperation")==0||
                    strcmp(rc,"SpawnLocation")==0||strcmp(rc,"Seat")==0||strcmp(rc,"VehicleSeat")==0||
                    strcmp(rc,"Terrain")==0;
    if (basepart && (strcmp(q,"BasePart")==0 || strcmp(q,"PVInstance")==0)) return true;
    if ((strcmp(rc,"Model")==0 || strcmp(rc,"Actor")==0) &&
        (strcmp(q,"PVInstance")==0 || (strcmp(rc,"Actor")==0 && strcmp(q,"Model")==0))) return true;
    bool gui = (strcmp(rc,"Frame")==0||strcmp(rc,"TextLabel")==0||strcmp(rc,"TextButton")==0||
                strcmp(rc,"TextBox")==0||strcmp(rc,"ImageLabel")==0||strcmp(rc,"ScrollingFrame")==0||
                strcmp(rc,"ImageButton")==0||strcmp(rc,"ViewportFrame")==0||strcmp(rc,"CanvasGroup")==0);
    if (gui && (strcmp(q,"GuiObject")==0 || strcmp(q,"GuiBase2d")==0)) return true;
    if ((strcmp(rc,"TextButton")==0||strcmp(rc,"ImageButton")==0) && strcmp(q,"GuiButton")==0) return true;
    // StringValue/NumberValue/IntValue/BoolValue/ObjectValue... → ValueBase
    { size_t l = strlen(rc); if (l > 5 && strcmp(rc + l - 5, "Value") == 0 && strcmp(q,"ValueBase")==0) return true; }
    if ((strcmp(rc,"Script")==0||strcmp(rc,"LocalScript")==0||strcmp(rc,"ModuleScript")==0)
        && strcmp(q,"LuaSourceContainer")==0) return true;
    // El resto de la cadena de Roblox: ScreenGui es LayerCollector y GuiBase2d,
    // UIListLayout es UILayout, PointLight es Light... TextFx (Motor2D) sube por
    // los padres hasta un LayerCollector para saber si un texto se ve; sin esto
    // no lo encontraba nunca y no pintaba ninguna letra.
    for (const char* p = gl_rbx_padre(rc); p; p = gl_rbx_padre(p))
        if (strcmp(p, q) == 0) return true;
    // Compat: aceptar también el nombre de clase Godot (IsA("RobloxPart"), IsA("Node")...)
    if (n->is_class(q)) return true;
    return false;
}

#include "luau_api_metodos.h"             // metodos de Instance, zonas y rayos

// Lectura de propiedades: una funcion por zona, llamadas en orden desde
// godot_object_index_real.
#include "luau_api_indice_instancia.h"
#include "luau_api_indice_personaje.h"
#include "luau_api_indice_mundo.h"
#include "luau_api_indice_gui.h"
#include "luau_api_indice_juego.h"

// El numero en la cima pasa a su EnumItem si en Roblox esa propiedad de esa
// clase es un enum (gl_enum_props.h). Un valor que el enum no tiene se queda
// como numero (__gl_enum_item lo devuelve tal cual).
inline void gl_enum_si_toca(lua_State* L, Node* n, const char* key) {
    const GLEnumPropIndice* ei = gl_enum_indice(key);
    if (!ei) return;
    const char* en = gl_enum_para_clase(ei, gl_roblox_classname(n));
    if (!en) return;
    lua_getglobal(L, "__gl_enum_item");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushstring(L, en);
    lua_pushvalue(L, -3);      // el numero
    lua_call(L, 2, 1);
    lua_replace(L, -2);        // en su sitio
}

inline int godot_object_index_real(lua_State* L) {
    // Corta la recursion por el puente ANTES de quedarse sin pila (si no, el
    // proceso se cae con un segfault sin mensaje).
    GL_STACK_GUARD(L);
    GodotObjectWrapper* wrapper = (GodotObjectWrapper*)lua_touserdata(L, 1);
    const char* key = gl_str(L, 2);
    if (!wrapper || !gow_node(wrapper)) {
        if (gl_index_instancia_muerta(L, key)) return 1;
        lua_pushnil(L); return 1;
    }
    Node* n = gow_node(wrapper);

    // ══════════════════════════════════════════════════════════════════
    //  CAMINO RAPIDO — rendimiento
    //
    //  Esta funcion es una cadena de CIENTOS de strcmp, y toda lectura de
    //  propiedad la recorre desde el principio. `Name` estaba en el puesto ~700,
    //  o sea que leer instancia.Name comparaba la clave contra 700 literales.
    //
    //  Eso se nota de verdad: en el place de prueba, un solo script recorria
    //  cada frame los descendientes de cada brainrot leyendo su .Name, y se
    //  comia 45 ms de los 112 ms del frame. Aqui se atienden primero las cuatro
    //  claves que domina cualquier bucle. Se comprueba la primera letra antes
    //  del strcmp para que las demas claves no paguen ni eso.
    //
    //  OJO con el orden: solo entran claves cuyo significado es el MISMO para
    //  todas las clases. `Position` NO puede ir aqui tal cual (en un GuiObject
    //  es un UDim2), por eso se exige que el nodo sea Node3D — y un Control
    //  nunca lo es.
    // ══════════════════════════════════════════════════════════════════
    switch (key[0]) {
        case 'N':
            if (strcmp(key, "Name") == 0 && !gl_cast<RobloxPlayer>(n)) {
                lua_pushstring(L, gl_rbx_name(n).utf8().get_data());
                return 1;
            }
            break;
        case 'P':
            if (strcmp(key, "Parent") == 0) { wrap_node(L, n->get_parent()); return 1; }
            if (strcmp(key, "Position") == 0) {
                if (Node3D* n3f = gl_cast<Node3D>(n)) {
                    // La de un Attachment es LOCAL (respecto a su pieza), como en
                    // Roblox; la del mundo es WorldPosition.
                    const Vector3 p = gl_cast<Attachment>(n3f) ? n3f->get_position() : gl_node_xf(n3f).origin;
                    push_vector3(L, p.x, p.y, p.z);
                    return 1;
                }
            }
            break;
        case 'C':
            if (strcmp(key, "ClassName") == 0 && !gl_cast<RobloxPlayer>(n)
                && !gl_cast<RobloxMouse>(n)) {
                lua_pushstring(L, gl_roblox_classname(n));
                return 1;
            }
            break;
        case 'I':
            // ── IsA: el 41% de TODO el trafico del puente ────────────────
            // Perfilado del place de prueba, 10 s reales:
            //     lecturas del puente ... 383.476
            //     de ellas IsA .......... 157.449   (~15.700 por segundo)
            // IsA estaba en el puesto 65 de una cadena de 611 strcmp, o sea
            // ~10,2 MILLONES de strcmp cada 10 s solo para resolver IsA.
            //
            // Hay dos IsA especializados mas abajo (Player y ValueBase) y por eso
            // se excluyen aqui: para todo lo demas, la cadena acabaria llamando a
            // este mismo method_isa, asi que el comportamiento es identico.
            if (strcmp(key, "IsA") == 0
                && !gl_cast<PlayerObject>(n)
                && !gl_cast<RobloxValue>(n)) {
                lua_pushcfunction(L, method_isa, "IsA");
                return 1;
            }
            break;
        case 'H':
            // HasTag: 193.000 lecturas en 10 s de la carga de Build a Mountain. El
            // del servicio (CollectionService:HasTag(inst, tag)) va por su lado.
            if (strcmp(key, "HasTag") == 0 && !gl_cast<CollectionService>(n)) {
                lua_pushcfunction(L, method_hastag, "HasTag");
                return 1;
            }
            break;
        case 'G':
            // GetAttribute: 62.651 llamadas en 10 s, estaba en el puesto 104.
            // Tiene una sola implementacion, asi que subirla no cambia nada.
            if (strcmp(key, "GetAttribute") == 0) {
                lua_pushcfunction(L, method_getattribute, "GetAttribute");
                return 1;
            }
            // GetPivot: 35.254 en 10 s y estaba en el puesto 544 DE 611, o sea
            // ~19 millones de strcmp cada 10 s. Solo existe para RBXModel, de ahi
            // la comprobacion de clase (mas barata que recorrer media cadena).
            if (strcmp(key, "GetPivot") == 0 && n->is_class("RBXModel")) {
                lua_pushcfunction(L, method_getpivot_model, "GetPivot");
                return 1;
            }
            break;
        default: break;
    }


    // El resto va por zonas (luau_api_indice_*.h), en el mismo orden de
    // siempre: los casos especiales primero y el respaldo generico al final.
    // Cada zona devuelve -1 si la clave no es suya.
    int r;
    if ((r = gl_index_instancia(L, n, key)) < 0 &&
        (r = gl_index_personaje(L, n, key)) < 0 &&
        (r = gl_index_mundo(L, n, key)) < 0 &&
        (r = gl_index_gui(L, n, key)) < 0)
        r = gl_index_juego(L, n, key);
    // Lo que Roblox da como EnumItem y la zona leyo como numero (F8).
    if (r == 1 && lua_type(L, -1) == LUA_TNUMBER) gl_enum_si_toca(L, n, key);
    return r;
}

#include "luau_api_escritura.h"           // __newindex y avisos de escritura

// ════════════════════════════════════════════════════════════════════
//  Instance.new
// ════════════════════════════════════════════════════════════════════
inline int godot_instance_new(lua_State* L) {
    GLAutTiempo _aut_nuevo(AUT_NUEVO);
    const char* cls = gl_str(L, 1);
    Node* nn = nullptr;

    // Primary Roblox class names → Godot nodes
    // "Part" crea un Part (nombre sin "Roblox", 1.15); las demás variantes que aún
    // no tienen clase propia caen a Part también (IS-A RobloxPart, mismo comportamiento).
    if      (strcmp(cls, "Part")              == 0 ||
             strcmp(cls, "BasePart")          == 0 ||
             strcmp(cls, "MeshPart")          == 0 ||
             strcmp(cls, "WedgePart")         == 0 ||
             strcmp(cls, "CornerWedgePart")   == 0 ||
             strcmp(cls, "TrussPart")         == 0 ||
             strcmp(cls, "Seat")              == 0 ||
             strcmp(cls, "VehicleSeat")       == 0 ||
             strcmp(cls, "UnionOperation")    == 0) {
        Part* pz = memnew(Part);
        // Como en Roblox, una pieza creada por script nace SIN anclar. El true
        // por defecto del C++ es el de las piezas de escena, que se guardan sin
        // la propiedad: cambiarlo alli dejaria caer todos los mapas.
        pz->set_anchored(false);
        // Una cuña no es un cubo (antes todas las variantes salian Block).
        if (strcmp(cls, "WedgePart") == 0)       pz->set_roblox_shape(3);
        if (strcmp(cls, "CornerWedgePart") == 0) pz->set_roblox_shape(4);
        if (strcmp(cls, "TrussPart") == 0)       pz->set_roblox_shape(5);
        // ClassName/IsA de la variante (MeshPart, WedgePart...), como el importador.
        if (strcmp(cls, "Part") != 0) pz->set_meta("__rbx_class", String(cls));
        nn = pz;
    }
    else if (strcmp(cls, "Folder")            == 0 ||
             strcmp(cls, "Configuration")     == 0) nn = memnew(Folder);
    // Clases de solo datos (KeyframeSequence y lo de dentro, AnimationController):
    // instancia generica con su clase de Roblox, como las que trae el importador.
    else if (strcmp(cls, "KeyframeSequence") == 0 || strcmp(cls, "Keyframe") == 0 ||
             strcmp(cls, "Pose") == 0 || strcmp(cls, "KeyframeMarker") == 0 ||
             strcmp(cls, "AnimationController") == 0) {
        nn = gl_cast<Node>(ClassDB::instantiate(StringName("RBXInstance")));
        if (nn) {
            nn->set_meta("__rbx_class", String(cls));
            nn->set_meta("_gl_bridge", true);
        }
    }
    // Animator: contenedor que reenvia LoadAnimation al Humanoid. Con su clase,
    // FindFirstChildOfClass("Animator") lo encuentra y no se crea otro cada vez.
    else if (strcmp(cls, "Animator")          == 0) {
        nn = memnew(Folder);
        nn->set_meta("__rbx_class", String("Animator"));
    }
    else if (strcmp(cls, "StringValue")       == 0 ||
             strcmp(cls, "IntValue")          == 0 ||
             strcmp(cls, "NumberValue")       == 0 ||
             strcmp(cls, "BoolValue")         == 0 ||
             strcmp(cls, "ObjectValue")       == 0 ||
             strcmp(cls, "Vector3Value")      == 0 ||
             strcmp(cls, "Color3Value")       == 0 ||
             strcmp(cls, "CFrameValue")       == 0) {
        // Cada tipo es SU clase (IntValue, StringValue...), no un RobloxValue
        // generico: asi el arbol dice la verdad y IsA funciona por herencia.
        RobloxValue* rv = nullptr;
        if      (strcmp(cls,"StringValue")  == 0) rv = memnew(StringValue);
        else if (strcmp(cls,"IntValue")     == 0) rv = memnew(IntValue);
        else if (strcmp(cls,"NumberValue")  == 0) rv = memnew(NumberValue);
        else if (strcmp(cls,"BoolValue")    == 0) rv = memnew(BoolValue);
        else if (strcmp(cls,"ObjectValue")  == 0) rv = memnew(ObjectValue);
        else if (strcmp(cls,"Vector3Value") == 0) rv = memnew(Vector3Value);
        else if (strcmp(cls,"Color3Value")  == 0) rv = memnew(Color3Value);
        else                                      rv = memnew(CFrameValue);
        // Valor por defecto tipado (como Roblox): número 0, texto "", bool false.
        if (strcmp(cls,"NumberValue")==0 || strcmp(cls,"IntValue")==0) rv->set_value((double)0);
        else if (strcmp(cls,"StringValue")==0) rv->set_value(String(""));
        else if (strcmp(cls,"BoolValue")==0)   rv->set_value(false);
        nn = rv;
    }
    // Model: la clase RBXModel de verdad (la que trae GetPivot/PivotTo/
    // GetBoundingBox/PrimaryPart). Con un Node3D pelado, un Model creado por
    // codigo no tenia ninguno de esos metodos, aunque el mismo Model importado
    // del .rbxl si los tuviera.
    else if (strcmp(cls, "Model")             == 0) {
        nn = gl_cast<Node>(ClassDB::instantiate(StringName("RBXModel")));
        if (!nn) nn = memnew(Node3D);
    }
    // Camera y WorldModel de verdad (gl_viewport_frame.h): el stdlib los daba
    // como Folder y un ViewportFrame no tenia camara que mirar.
    else if (strcmp(cls, "Camera") == 0)            nn = memnew(Camera);
    else if (strcmp(cls, "WorldModel") == 0)        nn = memnew(WorldModel);
    else if (strcmp(cls, "Humanoid")          == 0) nn = memnew(Humanoid);
    else if (strcmp(cls, "Humanoid2D")        == 0) nn = memnew(Humanoid2D);
    // PointLight / SpotLight / SurfaceLight tienen clase propia (roblox_luces.h)
    // y salen por ClassDB: antes eran un OmniLight3D pelado sin sus propiedades.
    else if (strcmp(cls, "DirectionalLight")  == 0) nn = memnew(DirectionalLight3D);
    else if (strcmp(cls, "RemoteEvent")       == 0) nn = memnew(RemoteEventNode);
    else if (strcmp(cls, "RemoteFunction")    == 0) nn = memnew(RemoteFunctionNode);
    else if (strcmp(cls, "BindableEvent")     == 0 ||
             strcmp(cls, "BindableFunction")  == 0) {
        BindableEventNode* be = memnew(BindableEventNode);
        // Marcar cual de las dos es: la funcion tiene UN manejador que devuelve.
        be->es_funcion = (strcmp(cls, "BindableFunction") == 0);
        nn = be;
    }

    if (nn) {
        nn->set_name(cls);
        wrap_node(L, nn);
        gl_track_orphan(nn);   // si nunca se le pone padre, se libera al cerrar

        if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
            GodotObjectWrapper* p_wrap = (GodotObjectWrapper*)lua_touserdata(L, 2);
            if (p_wrap && gow_node(p_wrap) && gow_node(p_wrap) != nn) {
                gl_add_child_rbx(gow_node(p_wrap), nn);
            }
        }
        return 1;
    }

    // Clase no mapeada aquí: devolvemos nil SIN avisar. La capa Lua de stdlib
    // intenta luego _InstanceNew con el mapeo completo y, si tampoco existe,
    // emite un único warning. (Antes esto ensuciaba la consola con clases válidas.)
    lua_pushnil(L);
    return 1;
}

// ════════════════════════════════════════════════════════════════════
//  Vector3
// ════════════════════════════════════════════════════════════════════

inline int godot_vector3_new(lua_State* L) {
    float x = (float)luaL_optnumber(L, 1, 0.0);
    float y = (float)luaL_optnumber(L, 2, 0.0);
    float z = (float)luaL_optnumber(L, 3, 0.0);
    push_vector3(L, x, y, z);
    return 1;
}

// Metodos de Vector3 que faltaban (estan en Roblox): se llaman con dos puntos,
// asi que el propio vector es el argumento 1. Min/Max aceptan varios vectores.
static int gl_v3_minmax(lua_State* L, bool es_min) {
    LuauVector3* a = check_vector3(L, 1);
    if (!a) { lua_pushnil(L); return 1; }
    float x = a->x, y = a->y, z = a->z;
    for (int i = 2; i <= lua_gettop(L); i++) {
        LuauVector3* b = check_vector3(L, i);
        if (!b) { luaL_error(L, "invalid argument #%d (Vector3 expected)", i - 1); return 0; }
        x = es_min ? MIN(x, b->x) : MAX(x, b->x);
        y = es_min ? MIN(y, b->y) : MAX(y, b->y);
        z = es_min ? MIN(z, b->z) : MAX(z, b->z);
    }
    push_vector3(L, x, y, z);
    return 1;
}
static int gl_v3_min(lua_State* L) { return gl_v3_minmax(L, true); }
static int gl_v3_max(lua_State* L) { return gl_v3_minmax(L, false); }
static int gl_v3_abs(lua_State* L) {
    LuauVector3* a = check_vector3(L, 1);
    if (!a) { lua_pushnil(L); return 1; }
    push_vector3(L, Math::abs(a->x), Math::abs(a->y), Math::abs(a->z));
    return 1;
}
static int gl_v3_ceil(lua_State* L) {
    LuauVector3* a = check_vector3(L, 1);
    if (!a) { lua_pushnil(L); return 1; }
    push_vector3(L, Math::ceil(a->x), Math::ceil(a->y), Math::ceil(a->z));
    return 1;
}
static int gl_v3_floor(lua_State* L) {
    LuauVector3* a = check_vector3(L, 1);
    if (!a) { lua_pushnil(L); return 1; }
    push_vector3(L, Math::floor(a->x), Math::floor(a->y), Math::floor(a->z));
    return 1;
}
static int gl_v3_sign(lua_State* L) {
    LuauVector3* a = check_vector3(L, 1);
    if (!a) { lua_pushnil(L); return 1; }
    push_vector3(L, (float)SIGN(a->x), (float)SIGN(a->y), (float)SIGN(a->z));
    return 1;
}
// Angle(otro [, eje]): sin eje, el angulo sin signo; con eje, con el signo del
// giro alrededor de ese eje (como Roblox).
static int gl_v3_angle(lua_State* L) {
    LuauVector3* a = check_vector3(L, 1);
    LuauVector3* b = check_vector3(L, 2);
    if (!a || !b) { lua_pushnumber(L, 0); return 1; }
    const Vector3 va(a->x, a->y, a->z), vb(b->x, b->y, b->z);
    float ang = va.angle_to(vb);
    if (LuauVector3* eje = check_vector3(L, 3)) {
        if (Vector3(eje->x, eje->y, eje->z).dot(va.cross(vb)) < 0.0f) ang = -ang;
    }
    lua_pushnumber(L, ang);
    return 1;
}
static int gl_v3_fuzzyeq(lua_State* L) {
    LuauVector3* a = check_vector3(L, 1);
    LuauVector3* b = check_vector3(L, 2);
    const double eps = luaL_optnumber(L, 3, 1e-5);
    if (!a || !b) { lua_pushboolean(L, 0); return 1; }
    const Vector3 d(a->x - b->x, a->y - b->y, a->z - b->z);
    lua_pushboolean(L, d.length() <= eps ? 1 : 0);
    return 1;
}

inline int godot_vector3_index(lua_State* L) {
    LuauVector3* vec = check_vector3(L, 1);
    const char* key  = gl_str(L, 2);
    if (!vec) { lua_pushnil(L); return 1; }

    if (strcmp(key, "X") == 0 || strcmp(key, "x") == 0) { lua_pushnumber(L, vec->x); return 1; }
    if (strcmp(key, "Y") == 0 || strcmp(key, "y") == 0) { lua_pushnumber(L, vec->y); return 1; }
    if (strcmp(key, "Z") == 0 || strcmp(key, "z") == 0) { lua_pushnumber(L, vec->z); return 1; }

    if (strcmp(key, "Magnitude") == 0) { lua_pushnumber(L, vec->magnitude()); return 1; }

    if (strcmp(key, "Unit") == 0) {
        LuauVector3 u = vec->unit();
        push_vector3(L, u.x, u.y, u.z);
        return 1;
    }

    if (strcmp(key, "Lerp") == 0) {
        lua_pushlightuserdata(L, vec);
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            LuauVector3* self = (LuauVector3*)lua_touserdata(pL, lua_upvalueindex(1));
            // Las DOS formas: v:Lerp(otro, t) — como lo escribe todo el mundo —
            // y Vector3.Lerp(otro, t). Estaba escrito solo para la segunda, asi
            // que con dos puntos el destino caia en el hueco del alpha y saltaba
            // "invalid argument #2 to 'Lerp' (number expected, got userdata)".
            LuauVector3* goal = check_vector3(pL, 2);
            int i_alpha = 3;
            if (!goal) { goal = check_vector3(pL, 1); i_alpha = 2; }
            float alpha = (float)gl_num(pL, i_alpha);
            if (!self || !goal) { lua_pushnil(pL); return 1; }
            LuauVector3 r = self->lerp(*goal, alpha);
            push_vector3(pL, r.x, r.y, r.z);
            return 1;
        }, "Lerp", 1);
        return 1;
    }

    if (strcmp(key, "Dot") == 0) {
        lua_pushlightuserdata(L, vec);
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            LuauVector3* self = (LuauVector3*)lua_touserdata(pL, lua_upvalueindex(1));
            // Igual que en Lerp: con v:Metodo(otro) el otro vector va en el 2.
            // Antes se leia el 1 (que es el propio self), asi que v:Dot(otro)
            // devolvia v:Dot(v) sin avisar de nada.
            LuauVector3* other = check_vector3(pL, 2);
            if (!other) other = check_vector3(pL, 1);
            if (!self || !other) { lua_pushnumber(pL, 0); return 1; }
            lua_pushnumber(pL, self->dot(*other));
            return 1;
        }, "Dot", 1);
        return 1;
    }

    if (strcmp(key, "Cross") == 0) {
        lua_pushlightuserdata(L, vec);
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            LuauVector3* self = (LuauVector3*)lua_touserdata(pL, lua_upvalueindex(1));
            // Igual que en Lerp: con v:Metodo(otro) el otro vector va en el 2.
            // Antes se leia el 1 (que es el propio self), asi que v:Dot(otro)
            // devolvia v:Dot(v) sin avisar de nada.
            LuauVector3* other = check_vector3(pL, 2);
            if (!other) other = check_vector3(pL, 1);
            if (!self || !other) { lua_pushnil(pL); return 1; }
            LuauVector3 c = self->cross(*other);
            push_vector3(pL, c.x, c.y, c.z);
            return 1;
        }, "Cross", 1);
        return 1;
    }

    if (strcmp(key, "Min") == 0)     { lua_pushcfunction(L, gl_v3_min, "Min"); return 1; }
    if (strcmp(key, "Max") == 0)     { lua_pushcfunction(L, gl_v3_max, "Max"); return 1; }
    if (strcmp(key, "Abs") == 0)     { lua_pushcfunction(L, gl_v3_abs, "Abs"); return 1; }
    if (strcmp(key, "Ceil") == 0)    { lua_pushcfunction(L, gl_v3_ceil, "Ceil"); return 1; }
    if (strcmp(key, "Floor") == 0)   { lua_pushcfunction(L, gl_v3_floor, "Floor"); return 1; }
    if (strcmp(key, "Sign") == 0)    { lua_pushcfunction(L, gl_v3_sign, "Sign"); return 1; }
    if (strcmp(key, "Angle") == 0)   { lua_pushcfunction(L, gl_v3_angle, "Angle"); return 1; }
    if (strcmp(key, "FuzzyEq") == 0) { lua_pushcfunction(L, gl_v3_fuzzyeq, "FuzzyEq"); return 1; }

    lua_pushnil(L); return 1;
}

inline int godot_vector3_add(lua_State* L) {
    LuauVector3* a = check_vector3(L, 1);
    LuauVector3* b = check_vector3(L, 2);
    if (a && b) { push_vector3(L, a->x+b->x, a->y+b->y, a->z+b->z); return 1; }
    lua_pushnil(L); return 1;
}

inline int godot_vector3_sub(lua_State* L) {
    LuauVector3* a = check_vector3(L, 1);
    LuauVector3* b = check_vector3(L, 2);
    if (a && b) { push_vector3(L, a->x-b->x, a->y-b->y, a->z-b->z); return 1; }
    lua_pushnil(L); return 1;
}

inline int godot_vector3_mul(lua_State* L) {
    if (lua_isnumber(L, 1)) {
        float s = (float)lua_tonumber(L, 1);
        LuauVector3* v = check_vector3(L, 2);
        if (v) { push_vector3(L, v->x*s, v->y*s, v->z*s); return 1; }
    } else {
        LuauVector3* v = check_vector3(L, 1);
        if (lua_isnumber(L, 2)) {
            float s = (float)lua_tonumber(L, 2);
            if (v) { push_vector3(L, v->x*s, v->y*s, v->z*s); return 1; }
        } else {
            LuauVector3* b = check_vector3(L, 2);
            if (v && b) { push_vector3(L, v->x*b->x, v->y*b->y, v->z*b->z); return 1; }
        }
    }
    lua_pushnil(L); return 1;
}

inline int godot_vector3_unm(lua_State* L) {
    LuauVector3* v = check_vector3(L, 1);
    if (v) { push_vector3(L, -v->x, -v->y, -v->z); return 1; }
    lua_pushnil(L); return 1;
}

// Vector3 / number  ó  Vector3 / Vector3 (componente a componente), como Roblox
inline int godot_vector3_div(lua_State* L) {
    LuauVector3* a = check_vector3(L, 1);
    if (a && lua_isnumber(L, 2)) {
        float s = (float)lua_tonumber(L, 2);
        push_vector3(L, a->x/s, a->y/s, a->z/s); return 1;
    }
    LuauVector3* b = check_vector3(L, 2);
    if (a && b) { push_vector3(L, a->x/b->x, a->y/b->y, a->z/b->z); return 1; }
    lua_pushnil(L); return 1;
}

// Vector3 == Vector3 por valor (sin esto, == comparaba identidad de userdata)
inline int godot_vector3_eq(lua_State* L) {
    LuauVector3* a = check_vector3(L, 1);
    LuauVector3* b = check_vector3(L, 2);
    lua_pushboolean(L, (a && b && a->x == b->x && a->y == b->y && a->z == b->z) ? 1 : 0);
    return 1;
}

inline int godot_vector3_tostring(lua_State* L) {
    LuauVector3* v = check_vector3(L, 1);
    if (v) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", v->x, v->y, v->z);
        lua_pushstring(L, buf);
        return 1;
    }
    lua_pushstring(L, "Vector3(nil)"); return 1;
}

// ════════════════════════════════════════════════════════════════════
//  Color3
// ════════════════════════════════════════════════════════════════════

inline int godot_color3_new(lua_State* L) {
    float r = (float)luaL_optnumber(L, 1, 0.0);
    float g = (float)luaL_optnumber(L, 2, 0.0);
    float b = (float)luaL_optnumber(L, 3, 0.0);
    push_color3(L, r, g, b);
    return 1;
}

inline int godot_color3_from_rgb(lua_State* L) {
    float r = (float)luaL_optnumber(L, 1, 0.0) / 255.0f;
    float g = (float)luaL_optnumber(L, 2, 0.0) / 255.0f;
    float b = (float)luaL_optnumber(L, 3, 0.0) / 255.0f;
    push_color3(L, r, g, b);
    return 1;
}

// Color3 == Color3 por valor. Sin esto se comparaban los userdata y dos
// colores iguales daban false (`if parte.Color == Color3.new(1, 0, 0)`).
inline int godot_color3_eq(lua_State* L) {
    LuauColor3* a = check_color3(L, 1);
    LuauColor3* b = check_color3(L, 2);
    lua_pushboolean(L, (a && b && a->r == b->r && a->g == b->g && a->b == b->b) ? 1 : 0);
    return 1;
}

// Como Roblox: "1, 0.5, 0" (antes salia "userdata: 0x...").
inline int godot_color3_tostring(lua_State* L) {
    LuauColor3* c = check_color3(L, 1);
    if (!c) { lua_pushstring(L, "Color3(nil)"); return 1; }
    char buf[96];
    snprintf(buf, sizeof(buf), "%.7g, %.7g, %.7g", c->r, c->g, c->b);
    lua_pushstring(L, buf);
    return 1;
}

inline int godot_color3_index(lua_State* L) {
    LuauColor3* col = check_color3(L, 1);
    const char* key = gl_str(L, 2);
    if (!col) { lua_pushnil(L); return 1; }
    if (strcmp(key, "R") == 0 || strcmp(key, "r") == 0) { lua_pushnumber(L, col->r); return 1; }
    if (strcmp(key, "G") == 0 || strcmp(key, "g") == 0) { lua_pushnumber(L, col->g); return 1; }
    if (strcmp(key, "B") == 0 || strcmp(key, "b") == 0) { lua_pushnumber(L, col->b); return 1; }
    // Un Color3 de Roblox tambien tiene METODOS. Aqui solo respondia R/G/B, asi
    // que `c:Lerp(otro, t)` —el pan de cada dia de cualquier animacion de UI—
    // devolvia nil y el script moria al intentar llamarlo.
    if (strcmp(key, "Lerp") == 0) {
        lua_pushlightuserdata(L, col);
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            LuauColor3* self = (LuauColor3*)lua_touserdata(pL, lua_upvalueindex(1));
            // Las dos formas: c:Lerp(otro, t) y Color3.Lerp(c, otro, t).
            LuauColor3* otro = check_color3(pL, 2);
            int i_alpha = 3;
            if (!otro) { otro = check_color3(pL, 1); i_alpha = 2; }
            const float a = (float)gl_num(pL, i_alpha);
            if (!self || !otro) { lua_pushnil(pL); return 1; }
            push_color3(pL, self->r + (otro->r - self->r) * a,
                            self->g + (otro->g - self->g) * a,
                            self->b + (otro->b - self->b) * a);
            return 1;
        }, "Lerp", 1);
        return 1;
    }
    if (strcmp(key, "ToHSV") == 0) {
        lua_pushlightuserdata(L, col);
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            LuauColor3* c = (LuauColor3*)lua_touserdata(pL, lua_upvalueindex(1));
            if (!c) { lua_pushnumber(pL, 0); lua_pushnumber(pL, 0); lua_pushnumber(pL, 0); return 3; }
            const float mx = MAX(c->r, MAX(c->g, c->b));
            const float mn = MIN(c->r, MIN(c->g, c->b));
            const float d  = mx - mn;
            float h = 0.0f;
            if (d > 0.00001f) {
                if      (mx == c->r) h = Math::fmod(((c->g - c->b) / d) + 6.0f, 6.0f);
                else if (mx == c->g) h = ((c->b - c->r) / d) + 2.0f;
                else                 h = ((c->r - c->g) / d) + 4.0f;
                h /= 6.0f;
            }
            lua_pushnumber(pL, h);
            lua_pushnumber(pL, mx <= 0.00001f ? 0.0f : d / mx);
            lua_pushnumber(pL, mx);
            return 3;
        }, "ToHSV", 1);
        return 1;
    }
    if (strcmp(key, "ToHex") == 0) {
        lua_pushlightuserdata(L, col);
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            LuauColor3* c = (LuauColor3*)lua_touserdata(pL, lua_upvalueindex(1));
            char buf[8] = "000000";
            if (c) {
                auto b255 = [](float v) { return (int)CLAMP(Math::round(v * 255.0f), 0.0f, 255.0f); };
                snprintf(buf, sizeof(buf), "%02X%02X%02X", b255(c->r), b255(c->g), b255(c->b));
            }
            lua_pushstring(pL, buf);
            return 1;
        }, "ToHex", 1);
        return 1;
    }
    lua_pushnil(L); return 1;
}

#endif // LUAU_API_H
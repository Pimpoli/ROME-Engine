#ifndef ROBLOX_INPUT_H
#define ROBLOX_INPUT_H

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_screen_touch.hpp>
#include <godot_cpp/classes/input_event_screen_drag.hpp>
#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_event_pan_gesture.hpp>
#include <godot_cpp/classes/input_event_magnify_gesture.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/control.hpp>   // gui_get_hovered_control (TouchTapInWorld)
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/collision_object3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <vector>
#include <cstring>
#include <algorithm>
#include <map>

#include "lua.h"
#include "lualib.h"
#include "gl_errors.h"
#include "gl_runtime.h"
#include "gl_autopsia.h"
#include "roblox_part.h"   // gl_raycast_roblox: los rayos respetan CanQuery

using namespace godot;

// ════════════════════════════════════════════════════════════════════
//  RobloxMouse — el objeto que devuelve Player:GetMouse() (1.14.10 Finish)
//  ------------------------------------------------------------------
//  No existía NADA de esto: GetMouse estaba anunciado en el autocompletado
//  y en la base de tipos, pero no en el runtime, así que llamarlo reventaba.
//
//  Hit/Target se calculan BAJO DEMANDA con un raycast desde la cámara por el
//  ratón (no hay coste por frame si nadie los lee). Como en Roblox, el rayo
//  ignora el personaje del propio jugador y el TargetFilter.
//  Los eventos (Button1Down/Up, Button2Down/Up, Move, rueda) salen de _input.
// ════════════════════════════════════════════════════════════════════
class RobloxMouse : public Node {
    GDCLASS(RobloxMouse, Node);

public:
    struct LuaCB { lua_State* main_L; int ref; bool active = true; bool once = false; };
    std::vector<LuaCB> b1down, b1up, b2down, b2up, move_cbs, wheel_f, wheel_b;
    uint64_t target_filter_id = 0;   // Mouse.TargetFilter
    uint64_t character_id = 0;       // el personaje propio: el rayo lo ignora

    void add_cb(std::vector<LuaCB>& v, lua_State* mL, int ref, bool once) { v.push_back({mL, ref, true, once}); }
    // Selección por índice: un array con comas dentro de lua_pushcclosure (que es
    // una MACRO) el preprocesador lo parte en argumentos. Por eso va aquí.
    std::vector<LuaCB>* cb_list(int idx) {
        switch (idx) {
            case 0: return &b1down;
            case 1: return &b1up;
            case 2: return &b2down;
            case 3: return &b2up;
            case 4: return &move_cbs;
            case 5: return &wheel_f;
            case 6: return &wheel_b;
        }
        return nullptr;
    }
    void _gl_disconnect(int ref, int64_t vm) {
        std::vector<LuaCB>* all[] = { &b1down, &b1up, &b2down, &b2up, &move_cbs, &wheel_f, &wheel_b };
        for (auto* v : all) for (auto& c : *v) if (c.ref == ref) c.active = false;
    }

    Camera3D* cam() { return is_inside_tree() ? get_viewport()->get_camera_3d() : nullptr; }
    Vector2 pos2d()  { return is_inside_tree() ? get_viewport()->get_mouse_position() : Vector2(); }
    Vector2 view_size() {
        if (!is_inside_tree()) return Vector2();
        return get_viewport()->get_visible_rect().size;
    }

    // Junta los RID de todos los CollisionObject3D de un subárbol (para excluir).
    void _collect_rids(Node* n, TypedArray<RID>& out, int depth = 0) {
        if (!n || depth > 32) return;
        if (CollisionObject3D* co = gl_cast<CollisionObject3D>(n)) out.push_back(co->get_rid());
        for (int i = 0; i < n->get_child_count(); i++) _collect_rids(n->get_child(i), out, depth + 1);
    }

    // Raycast desde la cámara por el ratón. Dict vacío si no hay impacto.
    Dictionary cast() {
        Dictionary empty;
        Camera3D* c = cam();
        if (!c || !c->is_inside_tree()) return empty;
        Ref<World3D> w = c->get_world_3d();
        if (w.is_null()) return empty;
        PhysicsDirectSpaceState3D* ss = w->get_direct_space_state();
        if (!ss) return empty;
        Vector2 mp = pos2d();
        Vector3 from = c->project_ray_origin(mp);
        Vector3 dir  = c->project_ray_normal(mp);
        Ref<PhysicsRayQueryParameters3D> q;
        q.instantiate();
        q->set_from(from);
        q->set_to(from + dir * 5000.0f);   // Roblox usa ~1000 studs; de sobra
        TypedArray<RID> ex;
        if (Node* ch = character_id ? gl_cast<Node>(ObjectDB::get_instance(character_id)) : nullptr)
            _collect_rids(ch, ex);
        if (Node* tf = target_filter_id ? gl_cast<Node>(ObjectDB::get_instance(target_filter_id)) : nullptr)
            _collect_rids(tf, ex);
        if (ex.size() > 0) q->set_exclude(ex);
        return gl_raycast_roblox(ss, q);   // CanQuery de Roblox
    }
    Vector3 unit_ray_origin() { Camera3D* c = cam(); return c ? c->project_ray_origin(pos2d()) : Vector3(); }
    Vector3 unit_ray_dir()    { Camera3D* c = cam(); return c ? c->project_ray_normal(pos2d()) : Vector3(0,0,-1); }

    // Lo llama Players::load_local_character tras respawnear: si no, un
    // `local mouse = plr:GetMouse()` guardado una vez seguiría filtrando por el
    // personaje MUERTO y el rayo empezaría a chocar contra el nuevo.
    void _gl_set_character(Object* ch) {
        Node* n = gl_cast<Node>(ch);
        character_id = n ? (uint64_t)n->get_instance_id() : 0;
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"), &RobloxMouse::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("_gl_set_character", "ch"), &RobloxMouse::_gl_set_character);
    }
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_READY && !Engine::get_singleton()->is_editor_hint())
            set_process_input(true);
    }

public:
    void _input(const Ref<InputEvent>& event) override {
        if (event.is_null()) return;
        Ref<InputEventMouseButton> mb = event;
        if (mb.is_valid()) {
            int b = (int)mb->get_button_index();
            bool down = mb->is_pressed();
            if      (b == MOUSE_BUTTON_LEFT)        _fire(down ? b1down : b1up);
            else if (b == MOUSE_BUTTON_RIGHT)       _fire(down ? b2down : b2up);
            else if (b == MOUSE_BUTTON_WHEEL_UP   && down) _fire(wheel_f);
            else if (b == MOUSE_BUTTON_WHEEL_DOWN && down) _fire(wheel_b);
            return;
        }
        Ref<InputEventMouseMotion> mm = event;
        if (mm.is_valid()) _fire(move_cbs);
    }

    void _fire(std::vector<LuaCB>& list) {
        if (list.empty()) return;
        std::vector<LuaCB> snap = list;   // copia defensiva: un callback puede conectar/desconectar
        for (size_t i = 0; i < snap.size(); i++) {
            LuaCB& cb = snap[i];
            if (!cb.active || !gl_state_alive(cb.main_L)) continue;
            if (cb.once) for (auto& o : list) if (o.ref == cb.ref) o.active = false;   // :Once antes de invocar
            lua_State* th = lua_newthread(cb.main_L);
            lua_rawgeti(cb.main_L, LUA_REGISTRYINDEX, cb.ref);
            if (lua_isfunction(cb.main_L, -1)) {
                lua_xmove(cb.main_L, th, 1);
                gl_resume_handler(th, 0);
            } else lua_pop(cb.main_L, 1);
            lua_pop(cb.main_L, 1);
        }
        for (int i = (int)list.size()-1; i >= 0; --i) if (!list[i].active) list.erase(list.begin()+i);
    }
};

// ────────────────────────────────────────────────────────────────────
//  Helpers: Roblox Enum.KeyCode ↔ Godot Key conversion
//  Roblox uses ASCII values for letters/digits (A=65, Space=32) and
//  its own values for special keys (LeftShift=304, etc.)
//  Godot uses the same ASCII for letters but 0x01000000+ for specials.
////
//  Helpers: conversión Roblox Enum.KeyCode ↔ Godot Key
//  Roblox usa valores ASCII para letras/dígitos (A=65, Space=32) y
//  valores propios para teclas especiales (LeftShift=304, etc.)
//  Godot usa ASCII igual para letras, pero 0x01000000+ para especiales.
// ────────────────────────────────────────────────────────────────────
// ── Botones de mando: Enum.KeyCode de Roblox <-> JoyButton de Godot ─────────
//  Roblox numera los botones del mando dentro de Enum.KeyCode a partir de 1000
//  (ButtonX=1000 … Thumbstick2=1017). Godot usa su propio indice (A=0, B=1…).
//  Sin esta traduccion, un `if input.KeyCode == Enum.KeyCode.ButtonA` no entraba
//  nunca, porque llegaba 0 y se comparaba contra 1002.
//  Los gatillos L2/R2 en Godot son EJES, no botones: no salen por aqui.
inline int gl_joybutton_to_roblox_keycode(int gb) {
    switch (gb) {
        case 0:  return 1002;  // A            -> ButtonA
        case 1:  return 1003;  // B            -> ButtonB
        case 2:  return 1000;  // X            -> ButtonX
        case 3:  return 1001;  // Y            -> ButtonY
        case 4:  return 1011;  // Back         -> ButtonSelect
        case 6:  return 1010;  // Start        -> ButtonStart
        case 7:  return 1009;  // StickIzq     -> ButtonL3
        case 8:  return 1008;  // StickDer     -> ButtonR3
        case 9:  return 1005;  // HombroIzq    -> ButtonL1
        case 10: return 1004;  // HombroDer    -> ButtonR1
        case 11: return 1014;  // DPad arriba  -> DPadUp
        case 12: return 1015;  // DPad abajo   -> DPadDown
        case 13: return 1012;  // DPad izq     -> DPadLeft
        case 14: return 1013;  // DPad der     -> DPadRight
        default: return gb;    // sin equivalente: se deja el indice crudo
    }
}
inline JoyButton gl_roblox_keycode_to_joybutton(int rc) {
    switch (rc) {
        case 1002: return (JoyButton)0;   case 1003: return (JoyButton)1;
        case 1000: return (JoyButton)2;   case 1001: return (JoyButton)3;
        case 1011: return (JoyButton)4;   case 1010: return (JoyButton)6;
        case 1009: return (JoyButton)7;   case 1008: return (JoyButton)8;
        case 1005: return (JoyButton)9;   case 1004: return (JoyButton)10;
        case 1014: return (JoyButton)11;  case 1015: return (JoyButton)12;
        case 1012: return (JoyButton)13;  case 1013: return (JoyButton)14;
        default:   return (JoyButton)-1;  // L2/R2 y demas: no son botones
    }
}

inline Key roblox_keycode_to_godot(int rc) {
    if (rc >= 32 && rc <= 127) return (Key)rc;  // ASCII: letters, digits, punctuation / letras, dígitos, puntuación
    switch (rc) {
        case 8:   return KEY_BACKSPACE;
        case 9:   return KEY_TAB;
        case 13:  return KEY_ENTER;
        case 27:  return KEY_ESCAPE;
        case 127: return KEY_DELETE;
        case 273: return KEY_UP;
        case 274: return KEY_DOWN;
        case 275: return KEY_RIGHT;
        case 276: return KEY_LEFT;
        case 277: return KEY_INSERT;
        case 278: return KEY_HOME;
        case 279: return KEY_END;
        case 280: return KEY_PAGEUP;
        case 281: return KEY_PAGEDOWN;
        case 282: return KEY_F1;   case 283: return KEY_F2;
        case 284: return KEY_F3;   case 285: return KEY_F4;
        case 286: return KEY_F5;   case 287: return KEY_F6;
        case 288: return KEY_F7;   case 289: return KEY_F8;
        case 290: return KEY_F9;   case 291: return KEY_F10;
        case 292: return KEY_F11;  case 293: return KEY_F12;
        case 303: case 304: return KEY_SHIFT;
        case 305: case 306: return KEY_CTRL;
        case 307: case 308: return KEY_ALT;
        case 301: return KEY_CAPSLOCK;
        default:  return KEY_UNKNOWN;
    }
}

inline int godot_key_to_roblox(Key gk) {
    int k = (int)gk;
    if (k >= 32 && k <= 127) return k;  // Direct ASCII / ASCII directo
    switch (gk) {
        case KEY_BACKSPACE:  return 8;
        case KEY_TAB:        return 9;
        case KEY_ENTER:      return 13;
        case KEY_ESCAPE:     return 27;
        case KEY_DELETE:     return 127;
        case KEY_UP:         return 273;
        case KEY_DOWN:       return 274;
        case KEY_RIGHT:      return 275;
        case KEY_LEFT:       return 276;
        case KEY_INSERT:     return 277;
        case KEY_HOME:       return 278;
        case KEY_END:        return 279;
        case KEY_PAGEUP:     return 280;
        case KEY_PAGEDOWN:   return 281;
        case KEY_F1:  return 282; case KEY_F2:  return 283;
        case KEY_F3:  return 284; case KEY_F4:  return 285;
        case KEY_F5:  return 286; case KEY_F6:  return 287;
        case KEY_F7:  return 288; case KEY_F8:  return 289;
        case KEY_F9:  return 290; case KEY_F10: return 291;
        case KEY_F11: return 292; case KEY_F12: return 293;
        case KEY_SHIFT:    return 304;
        case KEY_CTRL:     return 306;
        case KEY_ALT:      return 308;
        case KEY_CAPSLOCK: return 301;
        default:           return 0;
    }
}

// ────────────────────────────────────────────────────────────────────
//  UserInputService — exact equivalent to Roblox UserInputService
//  Usage from Luau:
//    local UIS = game:GetService("UserInputService")
//    UIS.InputBegan:Connect(function(input, gameProcessed) ... end)
//    UIS.InputEnded:Connect(function(input, gameProcessed) ... end)
//    UIS.InputChanged:Connect(function(input, gameProcessed) ... end)
//    UIS:IsKeyDown(Enum.KeyCode.W)
//    UIS:GetMouseLocation() → Vector2
////
//  UserInputService — equivalente exacto a Roblox UserInputService
//  Uso desde Luau:
//    local UIS = game:GetService("UserInputService")
//    UIS.InputBegan:Connect(function(input, gameProcessed) ... end)
//    UIS.InputEnded:Connect(function(input, gameProcessed) ... end)
//    UIS.InputChanged:Connect(function(input, gameProcessed) ... end)
//    UIS:IsKeyDown(Enum.KeyCode.W)
//    UIS:GetMouseLocation() → Vector2
// ────────────────────────────────────────────────────────────────────
// Crea un Vector3 de Luau (userdata con metatable "Vector3") sin depender de
// push_vector3 de luau_api.h (que se define DESPUES de incluir este header). El
// metatable solo lee los 3 primeros floats, asi que escribirlos basta.
inline void uis_push_vec3(lua_State* L, float x, float y, float z) {
    float* v = (float*)lua_newuserdata(L, sizeof(float) * 3);
    v[0] = x; v[1] = y; v[2] = z;
    luaL_getmetatable(L, "Vector3");
    lua_setmetatable(L, -2);
}
// Vector2 de verdad, igual que push_vector2 de luau_api.h (que aun no existe
// cuando se compila este header). Los gestos tactiles de Roblox entregan
// Vector2, no Vector3: TouchTap recibe {Vector2}, TouchPan un totalTranslation…
// Empuja el EnumItem de verdad (Enum.KeyCode.Escape y no el numero 27). El __eq
// de Lua solo salta entre dos tablas, asi que si aqui se entrega un numero,
// `input.KeyCode == Enum.KeyCode.X` es false SIEMPRE y ningun juego puede leer
// el teclado por comparacion — ni nuestro propio menu de Escape.
inline void uis_push_enum(lua_State* L, const char* enum_name, int v) {
    lua_getglobal(L, "__gl_enum_item");
    if (lua_isfunction(L, -1)) {
        lua_pushstring(L, enum_name);
        lua_pushinteger(L, v);
        if (lua_pcall(L, 2, 1, 0) == 0) return;   // deja el EnumItem en la pila
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
    }
    lua_pushinteger(L, v);   // sin stdlib: el numero de siempre
}

inline void uis_push_vec2(lua_State* L, float x, float y) {
    lua_getglobal(L, "Vector2");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "new");
        lua_remove(L, -2);
        lua_pushnumber(L, x);
        lua_pushnumber(L, y);
        lua_call(L, 2, 1);
        return;
    }
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushnumber(L, x); lua_setfield(L, -2, "X");
    lua_pushnumber(L, y); lua_setfield(L, -2, "Y");
}

class UserInputService : public Node {
    GDCLASS(UserInputService, Node);

public:
    struct LuaCallback { lua_State* main_L; int ref; bool active = true; };

    std::vector<LuaCallback> input_began_cbs;
    std::vector<LuaCallback> input_ended_cbs;
    std::vector<LuaCallback> input_changed_cbs;

    // ── Señales tactiles: lista PROPIA cada una ────────────────────────────
    // Antes TouchStarted/TouchTap/TouchLongPress devolvian la MISMA lista que
    // InputBegan (y TouchEnded la de InputEnded, y TouchMoved/Pinch/Rotate/Pan/
    // Swipe la de InputChanged). O sea que conectarse a TouchTap metia el
    // callback en input_began_cbs y CUALQUIER clic de raton lo disparaba: el
    // juego creia que estaba en un movil. Ahora cada una tiene su lista y solo
    // la alimentan los eventos tactiles REALES (InputEventScreenTouch/Drag).
    std::vector<LuaCallback> touch_started_cbs;
    std::vector<LuaCallback> touch_ended_cbs;
    std::vector<LuaCallback> touch_moved_cbs;
    std::vector<LuaCallback> touch_tap_cbs;
    std::vector<LuaCallback> touch_long_press_cbs;
    std::vector<LuaCallback> touch_pan_cbs;
    std::vector<LuaCallback> touch_pinch_cbs;
    std::vector<LuaCallback> touch_rotate_cbs;
    std::vector<LuaCallback> touch_swipe_cbs;
    std::vector<LuaCallback> touch_tap_world_cbs;
    // F6: la ventana gana/pierde el foco, la peticion de salto y el cambio de
    // tipo de entrada (teclado -> mando -> tactil).
    std::vector<LuaCallback> window_focused_cbs;
    std::vector<LuaCallback> window_focus_released_cbs;
    std::vector<LuaCallback> jump_request_cbs;
    std::vector<LuaCallback> last_input_type_changed_cbs;
    std::vector<LuaCallback> gamepad_connected_cbs;
    std::vector<LuaCallback> gamepad_disconnected_cbs;
    std::vector<LuaCallback> textbox_focused_cbs;
    std::vector<LuaCallback> textbox_focus_released_cbs;
    std::vector<LuaCallback> pointer_action_cbs;

private:
    Vector2 raton_previo;        // donde estaba el cursor al pasar a LockCurrentPosition
    bool    raton_previo_ok = false;
    Vector2 mouse_delta_accum;   // acumulado durante el frame en curso
    Vector2 mouse_delta_frame;   // doble-buffer: delta del frame anterior (lo que devuelve GetMouseDelta)
    int     last_input_type = 10; // Enum.UserInputType del ultimo input (Keyboard por defecto)

    // ── Estado de los dedos en pantalla (para los gestos) ──────────────────
    // Roblox no expone eventos crudos por dedo: expone GESTOS ya reconocidos.
    // Aqui se guarda cada dedo activo para poder deducir tap, long press, pan,
    // pinch, rotate y swipe igual que lo hace Roblox.
    struct Dedo {
        int     index = 0;
        Vector2 inicio;      // donde toco
        Vector2 pos;         // donde esta ahora
        double  t_inicio = 0.0;
        bool    movido = false;      // paso del umbral de tap
        bool    largo_avisado = false;
        bool    gesto_avisado = false;  // ya se conto como pan/pinch/rotate
    };
    std::vector<Dedo> dedos;
    // Referencia del gesto de 2 dedos, para escala y rotacion acumuladas.
    float  pinch_dist0 = 0.0f;
    float  rotate_ang0 = 0.0f;
    float  pinch_escala_ult = 1.0f;   // ultimo valor, para poder cerrar en End
    float  rotate_ang_ult   = 0.0f;
    float  pinch_vel_ult    = 0.0f;
    float  rotate_vel_ult   = 0.0f;
    double gesto2_t_prev    = 0.0;
    bool   gesto2_activo = false;
    Vector2 pan_total;
    Vector2 pan_centro_ult;

    static constexpr float TAP_PX    = 10.0f;   // Roblox tolera un pelin de arrastre
    static constexpr double TAP_SEG  = 0.30;    // mas de esto ya no es un tap
    static constexpr double LARGO_SEG = 0.50;   // umbral de TouchLongPress

    Dedo* _dedo(int idx) {
        for (auto& d : dedos) if (d.index == idx) return &d;
        return nullptr;
    }
    static double _ahora() {
        return (double)Time::get_singleton()->get_ticks_msec() / 1000.0;
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"),    &UserInputService::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("is_key_down", "keycode"),   &UserInputService::is_key_down);
        ClassDB::bind_method(D_METHOD("get_mouse_location"),       &UserInputService::get_mouse_location_v);
        ClassDB::bind_method(D_METHOD("get_mouse_delta"),          &UserInputService::get_mouse_delta_v);
        ClassDB::bind_method(D_METHOD("set_mouse_behavior", "b"),  &UserInputService::set_mouse_behavior);
        ClassDB::bind_method(D_METHOD("get_mouse_behavior"),       &UserInputService::get_mouse_behavior);
        ClassDB::bind_method(D_METHOD("set_mouse_icon_enabled", "v"), &UserInputService::set_mouse_icon_enabled);
        ClassDB::bind_method(D_METHOD("get_mouse_icon_enabled"),   &UserInputService::get_mouse_icon_enabled);
        ClassDB::bind_method(D_METHOD("is_gamepad_button_down","device","button"), &UserInputService::is_gamepad_button_down);
        ClassDB::bind_method(D_METHOD("get_last_input_type"),      &UserInputService::get_last_input_type);
        ClassDB::bind_method(D_METHOD("get_touch_enabled"),        &UserInputService::get_touch_enabled);
        ClassDB::bind_method(D_METHOD("get_gamepad_connected"),    &UserInputService::get_gamepad_connected);
        ClassDB::bind_method(D_METHOD("get_mouse_enabled"),        &UserInputService::get_mouse_enabled);
        ClassDB::bind_method(D_METHOD("get_keyboard_enabled"),     &UserInputService::get_keyboard_enabled);

        ADD_SIGNAL(MethodInfo("InputBegan",   PropertyInfo(Variant::OBJECT, "input"), PropertyInfo(Variant::BOOL, "gameProcessed")));
        ADD_SIGNAL(MethodInfo("InputEnded",   PropertyInfo(Variant::OBJECT, "input"), PropertyInfo(Variant::BOOL, "gameProcessed")));
        ADD_SIGNAL(MethodInfo("InputChanged", PropertyInfo(Variant::OBJECT, "input"), PropertyInfo(Variant::BOOL, "gameProcessed")));
    }

public:
    void add_input_began_cb(lua_State* L, int ref)   { input_began_cbs.push_back({L,ref,true}); }
    void add_input_ended_cb(lua_State* L, int ref)   { input_ended_cbs.push_back({L,ref,true}); }
    void add_input_changed_cb(lua_State* L, int ref) { input_changed_cbs.push_back({L,ref,true}); }

    // TODAS las listas del servicio, para que apagar una VM o un Disconnect no
    // se deje ninguna atras (las 10 tactiles nuevas incluidas).
    std::vector<std::vector<LuaCallback>*> _todas_las_listas() {
        return {
            &input_began_cbs, &input_ended_cbs, &input_changed_cbs,
            &touch_started_cbs, &touch_ended_cbs, &touch_moved_cbs,
            &touch_tap_cbs, &touch_long_press_cbs, &touch_pan_cbs,
            &touch_pinch_cbs, &touch_rotate_cbs, &touch_swipe_cbs,
            &touch_tap_world_cbs,
            &window_focused_cbs, &window_focus_released_cbs,
            &jump_request_cbs, &last_input_type_changed_cbs,
            &gamepad_connected_cbs, &gamepad_disconnected_cbs,
            &textbox_focused_cbs, &textbox_focus_released_cbs, &pointer_action_cbs,
        };
    }
    void remove_callbacks_for_state(lua_State* L) {
        for (auto* lista : _todas_las_listas())
            for (auto& cb : *lista) if (cb.main_L==L) cb.active=false;
    }
    void _gl_disconnect(int ref, int64_t vm) {
        for (auto* lista : _todas_las_listas())
            for (auto& cb : *lista) if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
    }

    // Antes casteaba (Key)keycode directo → las teclas especiales (Shift, flechas,
    // etc.) nunca se detectaban. Ahora convierte el Enum.KeyCode de Roblox al Key
    // de Godot. Acepta tambien un Key de Godot crudo como respaldo.
    bool is_key_down(int keycode) const {
        if (!is_inside_tree()) return false;
        Key gk = roblox_keycode_to_godot(keycode);
        if (gk == KEY_UNKNOWN) gk = (Key)keycode;
        return Input::get_singleton()->is_key_pressed(gk);
    }
    // Acepta el Enum.KeyCode de Roblox (Enum.KeyCode.ButtonA = 1002) y tambien
    // el indice crudo de Godot, para no romper el codigo que ya pasaba el indice.
    bool is_gamepad_button_down(int device, int button) const {
        JoyButton gb = (button >= 1000) ? gl_roblox_keycode_to_joybutton(button) : (JoyButton)button;
        if (gb == (JoyButton)-1) return false;
        return Input::get_singleton()->is_joy_button_pressed(device, gb);
    }
    int  get_last_input_type() const  { return last_input_type; }

    // ── Que perifericos hay, segun el dispositivo EMULADO ──────────────────
    // Igual que el emulador de dispositivos de Roblox Studio: en Movil no hay
    // raton ni teclado, en Consola manda el gamepad. MouseEnabled y
    // KeyboardEnabled estaban clavados a true, asi que el chequeo tipico
    // `if TouchEnabled and not KeyboardEnabled then <modo movil>` NUNCA entraba
    // y la UI tactil de los juegos no aparecia ni emulando Movil.
    // gl_emulated_device() abre un archivo: se lee UNA vez, no en cada lectura
    // de propiedad.
    static const String& _dispositivo() {
        static String d = gl_emulated_device();
        return d;
    }
    bool get_mouse_enabled() const {
        const String& d = _dispositivo();
        return !(d == "Mobile" || d == "Console" || d == "VR");
    }
    bool get_keyboard_enabled() const { return get_mouse_enabled(); }
    bool get_touch_enabled() const {
        const String& d = _dispositivo();
        if (d == "Mobile") return true;
        if (d == "Console" || d == "VR") return false;
        return DisplayServer::get_singleton()->is_touchscreen_available();
    }
    bool get_gamepad_connected() const{
        if (_dispositivo() == "Console") return true;
        return Input::get_singleton()->get_connected_joypads().size() > 0;
    }

    // Coordenadas del VIEWPORT, no de la pantalla. Antes devolvia la posicion
    // global del escritorio (DisplayServer), asi que en ventana no-maximizada el
    // valor no casaba con AbsolutePosition de las GUIs y los scripts que comparan
    // "¿solte el raton encima del boton?" fallaban siempre. Roblox lo define
    // relativo a la esquina superior izquierda del viewport (sin aplicar el
    // GuiInset): https://create.roblox.com/docs/reference/engine/classes/UserInputService#GetMouseLocation
    Vector2 get_mouse_location_v() const {
        if (is_inside_tree()) return get_viewport()->get_mouse_position();
        return DisplayServer::get_singleton()->mouse_get_position();
    }
    // Delta real del mouse en el frame (antes siempre devolvia (0,0)).
    Vector2 get_mouse_delta_v() const { return mouse_delta_frame; }

    // Enum.MouseBehavior. Godot no sabe dejar el cursor quieto donde esta:
    // LockCurrentPosition lo captura igual que LockCenter y al volver a
    // Default lo devuelve a donde estaba. Escribirlo cada frame (lo normal en
    // un shift lock) no hace nada si el valor no cambia.
    void set_mouse_behavior(int b) {
        GLRaton& r = gl_raton();
        if (b < 0 || b > 2) b = 0;
        if (b == r.comportamiento) return;
        if (r.comportamiento == 0 && b == 2 && is_inside_tree()) {
            raton_previo = get_viewport()->get_mouse_position();
            raton_previo_ok = true;
        }
        const bool devolver = (r.comportamiento == 2 && b == 0 && raton_previo_ok);
        r.comportamiento = b;
        gl_raton_aplicar();
        if (devolver && is_inside_tree()) get_viewport()->warp_mouse(raton_previo);
        if (b == 0) raton_previo_ok = false;
    }
    int get_mouse_behavior() const { return gl_raton().comportamiento; }
    void set_mouse_icon_enabled(bool v) {
        gl_raton().icono = v;
        if (gl_raton().comportamiento == 0) gl_raton_aplicar();
    }
    bool get_mouse_icon_enabled() const { return gl_raton().icono; }

    bool is_mouse_button_down(int btn) const {
        return Input::get_singleton()->is_mouse_button_pressed((MouseButton)btn);
    }

    void _ready() override {
        if (!Engine::get_singleton()->is_editor_hint()) {
            set_process_input(true);
            set_process(true);   // para el doble-buffer del delta de mouse
        }
    }

    // Doble-buffer del delta de mouse: lo acumulado en _input este frame pasa a
    // ser el valor que GetMouseDelta devuelve durante el frame siguiente, asi
    // todos los lectores ven el mismo valor consistente.
    void _process(double) override {
        mouse_delta_frame = mouse_delta_accum;
        mouse_delta_accum = Vector2();
        // TouchLongPress no tiene evento equivalente en Godot: es "el dedo sigue
        // ahi y no se ha movido", asi que se comprueba por reloj cada frame.
        if (!dedos.empty()) _revisar_long_press();
    }

    // JumpRequest (espacio o boton A, como el salto del Humanoid) y
    // LastInputTypeChanged alrededor del reparto de InputBegan/Changed/Ended.
    void _input(const Ref<InputEvent>& event) override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        const int tipo_antes = last_input_type;
        const int pref_antes = gl_preferido();
        _input_real(event);
        if (!pointer_action_cbs.empty()) _gl_pointer_action(event);
        if (!jump_request_cbs.empty()) {
            const InputEventKey* k = gl_cast<const InputEventKey>(*event);
            const InputEventJoypadButton* j = gl_cast<const InputEventJoypadButton>(*event);
            const bool salto = (k && k->is_pressed() && !k->is_echo() && k->get_keycode() == KEY_SPACE)
                            || (j && j->is_pressed() && j->get_button_index() == JOY_BUTTON_A);
            if (salto && !gl_menu_open()) _fire_args(jump_request_cbs, 0, [](lua_State*) {});
        }
        if (last_input_type != tipo_antes && !last_input_type_changed_cbs.empty()) {
            const int tipo = last_input_type;
            _fire_args(last_input_type_changed_cbs, 1, [tipo](lua_State* th) { uis_push_enum(th, "UserInputType", tipo); });
        }
        if (gl_preferido() != pref_antes) gl_fire_prop_changed(this, "PreferredInput");
    }
    // Enum.PreferredInput (0 KeyboardAndMouse, 1 Gamepad, 2 Touch): lo ultimo
    // que se uso; en un movil sin teclado, tactil desde el principio.
    int gl_preferido() const {
        const int t = last_input_type;
        if (t == 8 || t == 9 || (t >= 15 && t <= 20)) return 1;
        if (t == 7) return 2;
        if (t == 10 && get_touch_enabled() && !get_keyboard_enabled()) return 2;
        return 0;
    }
    // PointerAction(rueda, desplazar, pellizco, procesado): la rueda del raton
    // y los gestos del trackpad, como los recibe la camara de Roblox.
    void _gl_pointer_action(const Ref<InputEvent>& event) {
        float rueda = 0.0f, pellizco = 0.0f;
        Vector2 desplazar;
        if (const InputEventMouseButton* mb = gl_cast<const InputEventMouseButton>(*event)) {
            if (!mb->is_pressed()) return;
            if (mb->get_button_index() == MOUSE_BUTTON_WHEEL_UP) rueda = 1.0f;
            else if (mb->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN) rueda = -1.0f;
            else return;
        } else if (const InputEventPanGesture* pg = gl_cast<const InputEventPanGesture>(*event)) {
            desplazar = pg->get_delta();
        } else if (const InputEventMagnifyGesture* mg = gl_cast<const InputEventMagnifyGesture>(*event)) {
            pellizco = (float)mg->get_factor() - 1.0f;
        } else return;
        const bool procesado = gl_menu_open();
        _fire_args(pointer_action_cbs, 4, [=](lua_State* th) {
            lua_pushnumber(th, rueda);
            uis_push_vec2(th, desplazar.x, desplazar.y);
            lua_pushnumber(th, pellizco);
            lua_pushboolean(th, procesado ? 1 : 0);
        });
    }
    // El servicio vivo (para el aviso de foco de los TextBox).
    static ObjectID& gl_uis_id() { static ObjectID id; return id; }
    void _gl_mando_cambio(int device, bool conectado) {
        const int tipo = device <= 0 ? 8 : (device == 1 ? 9 : 15 + MIN(device - 2, 5));
        _fire_args(conectado ? gamepad_connected_cbs : gamepad_disconnected_cbs, 1,
                   [tipo](lua_State* th) { uis_push_enum(th, "UserInputType", tipo); });
    }
    void _notification(int what) {
        if (what == NOTIFICATION_READY && !Engine::get_singleton()->is_editor_hint()) {
            gl_uis_id() = get_instance_id();
            Input::get_singleton()->connect("joy_connection_changed",
                                            callable_mp(this, &UserInputService::_gl_mando_cambio));
        } else if (what == NOTIFICATION_APPLICATION_FOCUS_IN)
            _fire_args(window_focused_cbs, 0, [](lua_State*) {});
        else if (what == NOTIFICATION_APPLICATION_FOCUS_OUT)
            _fire_args(window_focus_released_cbs, 0, [](lua_State*) {});
    }

    // Recibe TODOS los eventos (teclado, mouse, rueda, tactil, gamepad) y dispara
    // InputBegan / InputChanged / InputEnded a Luau en tiempo real, con un
    // InputObject que trae UserInputType, KeyCode, UserInputState, Position y Delta.
    void _input_real(const Ref<InputEvent>& event) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        Vector2 mloc = get_mouse_location_v();

        // ── Teclado ────────────────────────────────────────────────
        const InputEventKey* key_ev = gl_cast<const InputEventKey>(*event);
        if (key_ev) {
            if (key_ev->is_echo()) return;
            int rc = godot_key_to_roblox(key_ev->get_keycode());
            if (rc == 0) return;
            last_input_type = 10;
            if (key_ev->is_pressed()) _fire(input_began_cbs, 10, rc, 0, Vector3(), Vector3());
            else                      _fire(input_ended_cbs, 10, rc, 2, Vector3(), Vector3());
            return;
        }

        // ── Boton de mouse + rueda ─────────────────────────────────
        const InputEventMouseButton* mb = gl_cast<const InputEventMouseButton>(*event);
        if (mb && !mb->is_double_click()) {
            int btn = (int)mb->get_button_index();   // 1=Izq 2=Der 3=Medio 4=ruedaArriba 5=ruedaAbajo
            if (btn == MOUSE_BUTTON_WHEEL_UP || btn == MOUSE_BUTTON_WHEEL_DOWN) {
                if (mb->is_pressed()) {
                    float dir = (btn == MOUSE_BUTTON_WHEEL_UP) ? 1.0f : -1.0f;
                    last_input_type = 4;  // MouseWheel
                    _fire(input_changed_cbs, 4, 0, 1, Vector3(mloc.x, mloc.y, dir), Vector3(0, 0, dir));
                }
                return;
            }
            last_input_type = btn;  // MouseButton1..3
            if (mb->is_pressed()) _fire(input_began_cbs, btn, 0, 0, Vector3(mloc.x, mloc.y, 0), Vector3());
            else                  _fire(input_ended_cbs, btn, 0, 2, Vector3(mloc.x, mloc.y, 0), Vector3());
            return;
        }

        // ── Movimiento de mouse → InputChanged (MouseMovement=5) ───
        const InputEventMouseMotion* mm = gl_cast<const InputEventMouseMotion>(*event);
        if (mm) {
            Vector2 rel = mm->get_relative();
            mouse_delta_accum += rel;
            last_input_type = 5;
            _fire(input_changed_cbs, 5, 0, 1, Vector3(mloc.x, mloc.y, 0), Vector3(rel.x, rel.y, 0));
            return;
        }

        // ── Tactil ─────────────────────────────────────────────────
        // Ademas de InputBegan/Ended/Changed (que en Roblox tambien reciben los
        // toques), aqui se reconocen los GESTOS y se disparan sus señales
        // propias. Un clic de raton NO pasa por aqui, que era el bug.
        const InputEventScreenTouch* st = gl_cast<const InputEventScreenTouch>(*event);
        if (st) {
            const Vector2 p = st->get_position();
            const int idx = st->get_index();
            last_input_type = 7;  // Touch
            if (st->is_pressed()) {
                Dedo d; d.index = idx; d.inicio = p; d.pos = p; d.t_inicio = _ahora();
                dedos.push_back(d);
                _fire(input_began_cbs, 7, idx, 0, Vector3(p.x, p.y, 0), Vector3());
                _fire_touch_obj(touch_started_cbs, p, Vector2(), idx, 0);
                _revisar_gesto2(0);
            } else {
                Dedo* d = _dedo(idx);
                const bool fue_tap = d && !d->movido && (_ahora() - d->t_inicio) <= TAP_SEG;
                _fire(input_ended_cbs, 7, idx, 2, Vector3(p.x, p.y, 0), Vector3());
                _fire_touch_obj(touch_ended_cbs, p, Vector2(), idx, 2);
                if (d) {
                    // Swipe: recorrido largo y rapido en una direccion dominante.
                    const Vector2 rec = p - d->inicio;
                    if (rec.length() >= 50.0f && (_ahora() - d->t_inicio) <= 0.60) {
                        const float ax = (rec.x < 0) ? -rec.x : rec.x;
                        const float ay = (rec.y < 0) ? -rec.y : rec.y;
                        int dir;   // Enum.SwipeDirection: Right0 Left1 Up2 Down3
                        if (ax >= ay) dir = (rec.x > 0) ? 0 : 1;
                        else          dir = (rec.y < 0) ? 2 : 3;
                        _fire_swipe(dir, (int)dedos.size());
                    }
                }
                if (gesto2_activo) { _fire_gestos2(2); gesto2_activo = false; }
                if (fue_tap) {
                    _fire_touch_pos(touch_tap_cbs, p, -1);
                    // TouchTapInWorld solo salta cuando el toque NO cayo sobre
                    // una GUI, igual que en Roblox.
                    if (!touch_tap_world_cbs.empty()) {
                        Control* encima = nullptr;
                        if (is_inside_tree() && get_viewport())
                            encima = get_viewport()->gui_get_hovered_control();
                        if (!encima) _fire_tap_world(p, false);
                    }
                }
                for (int i = (int)dedos.size() - 1; i >= 0; --i)
                    if (dedos[i].index == idx) dedos.erase(dedos.begin() + i);
                if (dedos.size() < 2) { gesto2_activo = false; pan_total = Vector2(); }
            }
            return;
        }
        const InputEventScreenDrag* sd = gl_cast<const InputEventScreenDrag>(*event);
        if (sd) {
            const Vector2 p = sd->get_position(), rel = sd->get_relative();
            const int idx = sd->get_index();
            last_input_type = 7;
            Dedo* d = _dedo(idx);
            if (d) {
                d->pos = p;
                if (d->inicio.distance_to(p) > TAP_PX) d->movido = true;
            }
            _fire(input_changed_cbs, 7, idx, 1, Vector3(p.x, p.y, 0), Vector3(rel.x, rel.y, 0));
            _fire_touch_obj(touch_moved_cbs, p, rel, idx, 1);
            if (dedos.size() == 1) {
                // Un dedo arrastrando = TouchPan.
                pan_total += rel;
                _fire_pan(p, pan_total, rel, d && d->gesto_avisado ? 1 : 0);
                if (d) d->gesto_avisado = true;
            } else if (dedos.size() >= 2) {
                _revisar_gesto2(gesto2_activo ? 1 : 0);
            }
            return;
        }

        // ── Gamepad: botones y sticks/gatillos ─────────────────────
        const InputEventJoypadButton* jb = gl_cast<const InputEventJoypadButton>(*event);
        if (jb) {
            last_input_type = 8;  // Gamepad1
            // Se reporta el Enum.KeyCode de ROBLOX, no el indice de Godot: el
            // codigo de usuario compara contra Enum.KeyCode.ButtonA y antes esa
            // comparacion no entraba NUNCA (llegaba 0 y se esperaba 1002).
            int kc = gl_joybutton_to_roblox_keycode((int)jb->get_button_index());
            if (jb->is_pressed()) _fire(input_began_cbs, 8, kc, 0, Vector3(), Vector3());
            else                  _fire(input_ended_cbs, 8, kc, 2, Vector3(), Vector3());
            return;
        }
        const InputEventJoypadMotion* jm = gl_cast<const InputEventJoypadMotion>(*event);
        if (jm) {
            float v = jm->get_axis_value();
            last_input_type = 8;
            _fire(input_changed_cbs, 8, (int)jm->get_axis(), 1, Vector3(v, 0, 0), Vector3(v, 0, 0));
            return;
        }
    }

    // Compatibilidad: helpers antiguos que disparaban began/ended por tipo+codigo.
    void fire_input_began(int t, int kc, bool gp) { _fire(input_began_cbs, t, kc, 0, Vector3(), Vector3(), gp); }
    void fire_input_ended(int t, int kc, bool gp) { _fire(input_ended_cbs, t, kc, 2, Vector3(), Vector3(), gp); }

private:
    // state: 0=Begin 1=Change 2=End (Enum.UserInputState)
    void _fire(std::vector<LuaCallback>& list, int type, int kc, int state,
               Vector3 pos, Vector3 delta, bool game_proc = false) {
        if (list.empty()) return;
        // Copia defensiva (mismo patron que RobloxMouse::_fire): el handler
        // puede hacer Connect/Disconnect sobre ESTA lista y realojar el
        // vector — iterar la lista viva era use-after-free.
        std::vector<LuaCallback> snap = list;
        for (size_t i = 0; i < snap.size(); i++) {
            auto& cb = snap[i];
            if (!cb.active || !gl_state_alive(cb.main_L)) continue;
            lua_State* th = lua_newthread(cb.main_L);
            lua_rawgeti(cb.main_L, LUA_REGISTRYINDEX, cb.ref);
            if (lua_isfunction(cb.main_L, -1)) {
                lua_xmove(cb.main_L, th, 1);
                // InputObject (tabla con campos estilo Roblox). Los tres campos
                // de enum van como EnumItems DE VERDAD — ver uis_push_enum.
                lua_newtable(th);
                uis_push_enum(th, "UserInputType",  type);  lua_setfield(th, -2, "UserInputType");
                uis_push_enum(th, "KeyCode",        kc);    lua_setfield(th, -2, "KeyCode");
                uis_push_enum(th, "UserInputState", state); lua_setfield(th, -2, "UserInputState");
                uis_push_vec3(th, pos.x,   pos.y,   pos.z);   lua_setfield(th, -2, "Position");
                uis_push_vec3(th, delta.x, delta.y, delta.z); lua_setfield(th, -2, "Delta");
                lua_pushboolean(th, game_proc);  // 2do argumento: gameProcessed
                gl_resume_handler(th, 2);
            } else lua_pop(cb.main_L, 1);
            lua_pop(cb.main_L, 1);
        }
        for (int i=(int)list.size()-1; i>=0; --i)
            if (!list[i].active || !gl_state_alive(list[i].main_L)) list.erase(list.begin()+i);
    }

    // ── Disparo generico: la lambda empuja los argumentos que toquen ───────
    // Cada gesto de Roblox tiene su propia firma (TouchTap recibe 2 args,
    // TouchPinch 5...), asi que el numero de argumentos viaja como parametro.
    template <typename F>
    void _fire_args(std::vector<LuaCallback>& list, int nargs, F empujar) {
        if (list.empty()) return;
        std::vector<LuaCallback> snap = list;   // copia defensiva (ver _fire)
        for (size_t i = 0; i < snap.size(); i++) {
            auto& cb = snap[i];
            if (!cb.active || !gl_state_alive(cb.main_L)) continue;
            lua_State* th = lua_newthread(cb.main_L);
            lua_rawgeti(cb.main_L, LUA_REGISTRYINDEX, cb.ref);
            if (lua_isfunction(cb.main_L, -1)) {
                lua_xmove(cb.main_L, th, 1);
                empujar(th);
                gl_resume_handler(th, nargs);
            } else lua_pop(cb.main_L, 1);
            lua_pop(cb.main_L, 1);
        }
        for (int i=(int)list.size()-1; i>=0; --i)
            if (!list[i].active || !gl_state_alive(list[i].main_L)) list.erase(list.begin()+i);
    }

    // {Vector2} con las posiciones de los dedos implicados, como Roblox.
    void _push_posiciones(lua_State* th) {
        lua_newtable(th);
        int n = 0;
        for (auto& d : dedos) { uis_push_vec2(th, d.pos.x, d.pos.y); lua_rawseti(th, -2, ++n); }
    }
    void _push_una_pos(lua_State* th, Vector2 p) {
        lua_newtable(th);
        uis_push_vec2(th, p.x, p.y);
        lua_rawseti(th, -2, 1);
    }

    // TouchStarted / TouchEnded / TouchMoved → (InputObject, gameProcessed)
    void _fire_touch_obj(std::vector<LuaCallback>& list, Vector2 p, Vector2 rel, int idx, int state) {
        if (list.empty()) return;
        _fire_args(list, 2, [&](lua_State* th) {
            lua_newtable(th);
            uis_push_enum(th, "UserInputType", 7);      lua_setfield(th, -2, "UserInputType");   // Touch
            // En Roblox el InputObject de un toque trae KeyCode = Unknown; los
            // dedos se distinguen por la identidad del objeto, no por un campo.
            uis_push_enum(th, "KeyCode", 0);            lua_setfield(th, -2, "KeyCode");
            uis_push_enum(th, "UserInputState", state); lua_setfield(th, -2, "UserInputState");
            uis_push_vec3(th, p.x,   p.y,   0); lua_setfield(th, -2, "Position");
            uis_push_vec3(th, rel.x, rel.y, 0); lua_setfield(th, -2, "Delta");
            lua_pushinteger(th, idx);    lua_setfield(th, -2, "TouchId");  // extra util
            lua_pushboolean(th, 0);
        });
    }
    // TouchTap → ({Vector2}, gameProcessed)
    void _fire_touch_pos(std::vector<LuaCallback>& list, Vector2 p, int) {
        if (list.empty()) return;
        _fire_args(list, 2, [&](lua_State* th) {
            _push_una_pos(th, p);
            lua_pushboolean(th, 0);
        });
    }
    // TouchLongPress → ({Vector2}, state, gameProcessed)
    void _fire_long_press(Vector2 p, int state) {
        if (touch_long_press_cbs.empty()) return;
        _fire_args(touch_long_press_cbs, 3, [&](lua_State* th) {
            _push_una_pos(th, p);
            lua_pushinteger(th, state);
            lua_pushboolean(th, 0);
        });
    }
    // TouchPan → ({Vector2}, totalTranslation, velocity, state, gameProcessed)
    void _fire_pan(Vector2 p, Vector2 total, Vector2 vel, int state) {
        if (touch_pan_cbs.empty()) return;
        _fire_args(touch_pan_cbs, 5, [&](lua_State* th) {
            _push_una_pos(th, p);
            uis_push_vec2(th, total.x, total.y);
            uis_push_vec2(th, vel.x,   vel.y);
            lua_pushinteger(th, state);
            lua_pushboolean(th, 0);
        });
    }
    // TouchPinch → ({Vector2}, scale, velocity, state, gameProcessed)
    void _fire_pinch(float escala, float vel, int state) {
        if (touch_pinch_cbs.empty()) return;
        _fire_args(touch_pinch_cbs, 5, [&](lua_State* th) {
            _push_posiciones(th);
            lua_pushnumber(th, escala);
            lua_pushnumber(th, vel);
            lua_pushinteger(th, state);
            lua_pushboolean(th, 0);
        });
    }
    // TouchRotate → ({Vector2}, rotation, velocity, state, gameProcessed)
    void _fire_rotate(float ang, float vel, int state) {
        if (touch_rotate_cbs.empty()) return;
        _fire_args(touch_rotate_cbs, 5, [&](lua_State* th) {
            _push_posiciones(th);
            lua_pushnumber(th, ang);
            lua_pushnumber(th, vel);
            lua_pushinteger(th, state);
            lua_pushboolean(th, 0);
        });
    }
    // TouchTapInWorld → (position: Vector2, processedByUI: boolean)
    void _fire_tap_world(Vector2 p, bool por_ui) {
        if (touch_tap_world_cbs.empty()) return;
        _fire_args(touch_tap_world_cbs, 2, [&](lua_State* th) {
            uis_push_vec2(th, p.x, p.y);
            lua_pushboolean(th, por_ui ? 1 : 0);
        });
    }
    // TouchSwipe → (Enum.SwipeDirection, numberOfTouches, gameProcessed)
    void _fire_swipe(int dir, int n_dedos) {
        if (touch_swipe_cbs.empty()) return;
        _fire_args(touch_swipe_cbs, 3, [&](lua_State* th) {
            lua_pushinteger(th, dir);
            lua_pushinteger(th, n_dedos);
            lua_pushboolean(th, 0);
        });
    }

    // Pinch y rotate salen del MISMO par de dedos: distancia entre ellos para la
    // escala, angulo de la recta que los une para la rotacion.
    void _revisar_gesto2(int state_pedido) {
        if (dedos.size() < 2) return;
        const Vector2 a = dedos[0].pos, b = dedos[1].pos;
        const float dist = a.distance_to(b);
        const float ang  = (b - a).angle();
        const double ahora = _ahora();
        if (!gesto2_activo) {
            if (dist <= 0.001f) return;
            pinch_dist0 = dist; rotate_ang0 = ang;
            pinch_escala_ult = 1.0f; rotate_ang_ult = 0.0f;
            pinch_vel_ult = 0.0f;    rotate_vel_ult = 0.0f;
            gesto2_t_prev = ahora;
            gesto2_activo = true;
            _fire_pinch(1.0f, 0.0f, 0);
            _fire_rotate(0.0f, 0.0f, 0);
            return;
        }
        const float escala = (pinch_dist0 > 0.001f) ? (dist / pinch_dist0) : 1.0f;
        const float giro   = ang - rotate_ang0;
        const double dt    = ahora - gesto2_t_prev;
        if (dt > 0.0005) {
            pinch_vel_ult  = (float)((escala - pinch_escala_ult) / dt);
            rotate_vel_ult = (float)((giro   - rotate_ang_ult)   / dt);
            gesto2_t_prev  = ahora;
        }
        pinch_escala_ult = escala;
        rotate_ang_ult   = giro;
        _fire_pinch(escala, pinch_vel_ult, state_pedido == 0 ? 0 : 1);
        _fire_rotate(giro,  rotate_vel_ult, state_pedido == 0 ? 0 : 1);
    }
    // Cierre del gesto de 2 dedos cuando se levanta uno.
    void _fire_gestos2(int state) {
        _fire_pinch(pinch_escala_ult, 0.0f, state);
        _fire_rotate(rotate_ang_ult,  0.0f, state);
    }
    // El dedo lleva LARGO_SEG quieto encima: eso es un TouchLongPress.
    void _revisar_long_press() {
        if (touch_long_press_cbs.empty()) return;
        const double ahora = _ahora();
        for (auto& d : dedos) {
            if (d.largo_avisado || d.movido) continue;
            if (ahora - d.t_inicio < LARGO_SEG) continue;
            d.largo_avisado = true;
            _fire_long_press(d.pos, 0);   // Begin, como Roblox
        }
    }
};

// ────────────────────────────────────────────────────────────────────
//  CollectionService — Roblox-style tag/label system
//  Usage from Luau:
//    CollectionService:AddTag(inst, "Enemy")
//    CollectionService:GetTagged("Enemy") → array
//    CollectionService:HasTag(inst, "Enemy") → bool
////
//  CollectionService — sistema de tags/etiquetas tipo Roblox
//  Uso desde Luau:
//    CollectionService:AddTag(inst, "Enemy")
//    CollectionService:GetTagged("Enemy") → array
//    CollectionService:HasTag(inst, "Enemy") → bool
// ────────────────────────────────────────────────────────────────────
// ¿Tiene la instancia ese tag? Mira su meta __rbx_tags (pocos tags por pieza).
// Lo usan CollectionService:HasTag e Instance:HasTag (method_hastag).
inline bool gl_tiene_tag(const Node* n, const String& tag) {
    static const StringName k_tags("__rbx_tags");
    if (!n || !n->has_meta(k_tags)) return false;
    const PackedStringArray t = n->get_meta(k_tags);
    return t.has(tag);
}

class CollectionService : public Node {
    GDCLASS(CollectionService, Node);

    // Se guardan IDs de objeto, NO punteros crudos.
    //
    // El bug que arregla: al destruirse una instancia etiquetada, su Node* se
    // quedaba en la lista APUNTANDO A MEMORIA LIBERADA. La siguiente llamada a
    // GetTagged leia ese puntero (n->is_inside_tree()) y el proceso se caia. En
    // el place de prueba pasaba SIEMPRE en cuanto el sistema de dialogo pedia
    // CollectionService:GetTagged("NPCprompt"): "ERROR: Parameter data.tree is
    // null" y detras un segfault. Con el ID, un objeto ya liberado simplemente
    // no aparece (y se limpia de la lista), que es lo que hace Roblox.
    std::map<String, std::vector<uint64_t>> tagged;

    // ── Sincronizacion con las etiquetas del place importado (1.17) ──────
    // El importador NO llama a add_tag: deja las etiquetas de cada instancia en
    // el metadato `__rbx_tags` (rbx_importer.h:_apply_tags). Antes ese camino no
    // llegaba NUNCA a este registro, asi que GetTagged de un tag importado
    // devolvia vacio y modulos enteros (AnimUi, EasyVisuals) se quedaban mudos.
    // Ahora, en el primer acceso en runtime, se barre el subarbol del juego y se
    // registran esos metadatos; despues, `node_added` del SceneTree recoge lo que
    // entre al arbol (clones de StarterGui al respawnear incluidos: duplicate()
    // copia los metadatos, asi que el clon trae sus tags).
    bool synced = false;

    // El mundo de ESTE servicio: en el test multijugador local conviven varios
    // DataModel en el mismo SceneTree; sin acotar por ancestro, cada mundo veria
    // las etiquetas de los demas.
    Node* _game_root() {
        if (get_parent()) return get_parent();
        return is_inside_tree() ? (Node*)get_tree()->get_root() : nullptr;
    }

    void _register_meta_tags(Node* n) {
        if (!n || !n->has_meta("__rbx_tags")) return;
        PackedStringArray arr = n->get_meta("__rbx_tags");
        const uint64_t id = (uint64_t)n->get_instance_id();
        for (int i = 0; i < arr.size(); i++) {
            auto& list = tagged[arr[i]];
            bool dup = false;
            for (uint64_t v : list) if (v == id) { dup = true; break; }
            if (!dup) list.push_back(id);
        }
    }

    void _scan_subtree(Node* n, int depth = 0) {
        if (!n || depth > 64) return;
        _register_meta_tags(n);
        for (int i = 0; i < n->get_child_count(); i++)
            _scan_subtree(n->get_child(i), depth + 1);
    }

    void _ensure_synced() {
        if (synced || !is_inside_tree()) return;
        if (Engine::get_singleton()->is_editor_hint()) return;
        synced = true;
        _scan_subtree(_game_root());
        SceneTree* st = get_tree();
        if (st && !st->is_connected("node_added", Callable(this, "_gl_node_added")))
            st->connect("node_added", Callable(this, "_gl_node_added"));
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("add_tag", "inst", "tag"),    &CollectionService::add_tag);
        ClassDB::bind_method(D_METHOD("remove_tag", "inst", "tag"), &CollectionService::remove_tag);
        ClassDB::bind_method(D_METHOD("has_tag", "inst", "tag"),    &CollectionService::has_tag);
        ClassDB::bind_method(D_METHOD("get_tagged", "tag"),         &CollectionService::get_tagged);
        ClassDB::bind_method(D_METHOD("get_tags", "inst"),          &CollectionService::get_tags);
        ClassDB::bind_method(D_METHOD("_gl_node_added", "node"),    &CollectionService::_gl_node_added);
    }

    void _notification(int p_what) {
        // El barrido tambien es perezoso (_ensure_synced en cada metodo) porque
        // el orden de _ready entre hermanos no garantiza que este nodo este
        // listo antes que el primer script que pregunte por un tag.
        if (p_what == NOTIFICATION_READY) _ensure_synced();
    }

public:
    void _gl_node_added(Node* n) {
        GLAutTiempo _aut_tags(AUT_TAGS);
        if (!n || !n->has_meta("__rbx_tags")) return;
        Node* root = _game_root();
        if (root && (n == root || root->is_ancestor_of(n))) _register_meta_tags(n);
    }

    void add_tag(Node* inst, const String& tag) {
        if (!inst) return;
        _ensure_synced();
        // El metadato es la identidad persistente del tag: asi un Clone() del
        // nodo hereda sus etiquetas (duplicate() copia los metadatos), igual que
        // en Roblox las etiquetas viajan con la instancia clonada.
        PackedStringArray meta_tags = inst->has_meta("__rbx_tags")
            ? (PackedStringArray)inst->get_meta("__rbx_tags") : PackedStringArray();
        if (!meta_tags.has(tag)) {
            meta_tags.push_back(tag);
            inst->set_meta("__rbx_tags", meta_tags);
        }
        const uint64_t id = (uint64_t)inst->get_instance_id();
        auto& list = tagged[tag];
        for (uint64_t v : list) if (v == id) return;
        list.push_back(id);
        if (gl_rep_tag_hook()) gl_rep_tag_hook()(inst, tag, true);   // red (gl_replica.h)
    }

    void remove_tag(Node* inst, const String& tag) {
        if (!inst) return;
        _ensure_synced();
        if (inst->has_meta("__rbx_tags")) {
            PackedStringArray meta_tags = inst->get_meta("__rbx_tags");
            const int64_t idx = meta_tags.find(tag);
            if (idx >= 0) {
                meta_tags.remove_at(idx);
                inst->set_meta("__rbx_tags", meta_tags);
            }
        }
        auto it = tagged.find(tag);
        if (it == tagged.end()) return;
        const uint64_t id = (uint64_t)inst->get_instance_id();
        auto& list = it->second;
        const size_t antes = list.size();
        list.erase(std::remove(list.begin(), list.end(), id), list.end());
        if (list.size() != antes && gl_rep_tag_hook()) gl_rep_tag_hook()(inst, tag, false);   // red
    }

    // Los tags de una instancia van en su meta __rbx_tags, que es la fuente de
    // verdad (el registro se reconstruye de ella). Antes se buscaba el id entre
    // TODAS las instancias con ese tag, en lineal: con miles de piezas
    // etiquetadas, cada HasTag recorria miles.
    bool has_tag(Node* inst, const String& tag) {
        return gl_tiene_tag(inst, tag);
    }

    // Como en Roblox, solo devuelve instancias dentro del DataModel: "instances
    // can have the given tag assigned to them but not be a descendant of the
    // DataModel; this method will not return such instances."
    // https://create.roblox.com/docs/reference/engine/classes/CollectionService#GetTagged
    TypedArray<Node> get_tagged(const String& tag) {
        _ensure_synced();
        TypedArray<Node> result;
        auto it = tagged.find(tag);
        if (it == tagged.end()) return result;
        auto& list = it->second;
        // De paso se purgan los que ya no existen (asi la lista no crece sin fin
        // en un juego que crea y destruye NPC todo el rato).
        for (size_t i = 0; i < list.size();) {
            Node* n = gl_cast<Node>(ObjectDB::get_instance(ObjectID(list[i])));
            if (!n) { list.erase(list.begin() + i); continue; }
            if (n->is_inside_tree()) result.push_back(n);
            i++;
        }
        return result;
    }

    PackedStringArray get_tags(Node* inst) {
        PackedStringArray result;
        if (!inst) return result;
        _ensure_synced();
        const uint64_t id = (uint64_t)inst->get_instance_id();
        for (auto& [tag, list] : tagged) {
            for (uint64_t v : list) {
                if (v == id) { result.push_back(tag); break; }
            }
        }
        return result;
    }
};


// El nombre que ve el usuario, sin "Roblox" delante. Subclase fina: hereda todo
// y sigue siendo un RobloxMouse, asi que ningun cast ni is_class de por ahi se
// entera. Ver core/gl_nombres_limpios.h para el porque y para el resto.
class Mouse : public RobloxMouse {
    GDCLASS(Mouse, RobloxMouse);
protected:
    static void _bind_methods() {}
public:
    Mouse() {}
};

#endif // ROBLOX_INPUT_H

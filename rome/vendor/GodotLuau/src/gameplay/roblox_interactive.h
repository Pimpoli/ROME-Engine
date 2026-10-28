#ifndef ROBLOX_INTERACTIVE_H
#define ROBLOX_INTERACTIVE_H

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/area3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/sphere_shape3d.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/color.hpp>
#include <vector>
#include <cstring>

#include "lua.h"
#include "lualib.h"
#include "gl_errors.h"
#include "gl_runtime.h"
#include "gl_clone.h"   // Clone() rapido + marca de hijos internos
#include "roblox_input.h"   // godot_key_to_roblox: la tecla del prompt
#include "roblox_part.h"    // SpawnLocation ES un BasePart (ver abajo)
#include <godot_cpp/classes/input_event_key.hpp>

using namespace godot;

// ────────────────────────────────────────────────────────────────────
//  ClickDetector — exact equivalent to Roblox ClickDetector
//  Usage from Luau:
//    detector.MouseClick:Connect(function(player) end)
//    detector.MouseHoverEnter:Connect(function(player) end)
////
//  ClickDetector — equivalente exacto a Roblox ClickDetector
//  Uso desde Luau:
//    detector.MouseClick:Connect(function(player) end)
//    detector.MouseHoverEnter:Connect(function(player) end)
// ────────────────────────────────────────────────────────────────────
class ClickDetector : public Area3D {
    GDCLASS(ClickDetector, Area3D);

public:
    struct LuaCallback { lua_State* main_L; int ref; bool active = true; };

    std::vector<LuaCallback> mouse_click_cbs;
    std::vector<LuaCallback> hover_enter_cbs;
    std::vector<LuaCallback> hover_leave_cbs;

    float max_activation_distance = 32.0f;
    bool  cursor_icon_change       = true;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"), &ClickDetector::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("set_max_distance", "d"), &ClickDetector::set_max_distance);
        ClassDB::bind_method(D_METHOD("get_max_distance"),      &ClickDetector::get_max_distance);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "MaxActivationDistance", PROPERTY_HINT_RANGE, "0,1000,0.1"),
                     "set_max_distance", "get_max_distance");
        ADD_SIGNAL(MethodInfo("MouseClick",      PropertyInfo(Variant::OBJECT, "player")));
        ADD_SIGNAL(MethodInfo("MouseHoverEnter", PropertyInfo(Variant::OBJECT, "player")));
        ADD_SIGNAL(MethodInfo("MouseHoverLeave", PropertyInfo(Variant::OBJECT, "player")));
    }

public:
    void set_max_distance(float d) { max_activation_distance = d; }
    float get_max_distance() const { return max_activation_distance; }

    void add_click_cb(lua_State* L, int ref) {
        mouse_click_cbs.push_back({L, ref, true});
    }
    void add_hover_enter_cb(lua_State* L, int ref) {
        hover_enter_cbs.push_back({L, ref, true});
    }
    void add_hover_leave_cb(lua_State* L, int ref) {
        hover_leave_cbs.push_back({L, ref, true});
    }

    void remove_callbacks_for_state(lua_State* L) {
        for (auto& cb : mouse_click_cbs) if (cb.main_L == L) cb.active = false;
        for (auto& cb : hover_enter_cbs) if (cb.main_L == L) cb.active = false;
        for (auto& cb : hover_leave_cbs) if (cb.main_L == L) cb.active = false;
    }
    void _gl_disconnect(int ref, int64_t vm) {
        for (auto& cb : mouse_click_cbs) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
        for (auto& cb : hover_enter_cbs) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
        for (auto& cb : hover_leave_cbs) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
    }

    void fire_click(Node* player = nullptr) {
        emit_signal("MouseClick", player);
        _fire_list(mouse_click_cbs, player);
    }

    void fire_hover_enter(Node* player = nullptr) {
        emit_signal("MouseHoverEnter", player);
        _fire_list(hover_enter_cbs, player);
    }

    void fire_hover_leave(Node* player = nullptr) {
        emit_signal("MouseHoverLeave", player);
        _fire_list(hover_leave_cbs, player);
    }

private:
    void _fire_list(std::vector<LuaCallback>& list, Node* player) {
        if (list.empty()) return;
        // Copia defensiva (patron RobloxMouse::_fire): un handler puede hacer
        // Connect/Disconnect y realojar el vector — iterar el vivo era UAF.
        std::vector<LuaCallback> snap = list;
        for (size_t i = 0; i < snap.size(); i++) {
            auto& cb = snap[i];
            if (!cb.active || !gl_state_alive(cb.main_L)) continue;
            lua_State* th = lua_newthread(cb.main_L);
            lua_rawgeti(cb.main_L, LUA_REGISTRYINDEX, cb.ref);
            if (lua_isfunction(cb.main_L, -1)) {
                lua_xmove(cb.main_L, th, 1);
                if (player) {
                    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_newuserdata(th, sizeof(GodotObjectWrapper));
                    gow_set(w, player);
                    luaL_getmetatable(th, "GodotObject"); lua_setmetatable(th, -2);
                } else lua_pushnil(th);
                gl_resume_handler(th, 1);
            } else lua_pop(cb.main_L, 1);
            lua_pop(cb.main_L, 1);
        }
        for (int i = (int)list.size() - 1; i >= 0; --i)
            if (!list[i].active || !gl_state_alive(list[i].main_L)) list.erase(list.begin() + i);
    }
};

// ────────────────────────────────────────────────────────────────────
//  ProximityPrompt — equivalent to Roblox ProximityPrompt
//  Shows a keybind hint when nearby and fires Triggered
//  Usage from Luau:
//    prompt.ActionText = "Open Door"
//    prompt.KeyboardKeyCode = "E"
//    prompt.Triggered:Connect(function(player) end)
////
//  ProximityPrompt — equivalente a Roblox ProximityPrompt
//  Muestra un keybind al acercarse y dispara Triggered
//  Uso desde Luau:
//    prompt.ActionText = "Open Door"
//    prompt.KeyboardKeyCode = "E"
//    prompt.Triggered:Connect(function(player) end)
// ────────────────────────────────────────────────────────────────────
// ── Cola de eventos de prompts ──────────────────────────────────────────
// ProximityPromptService vive en Luau y hay UNA VM POR SCRIPT, asi que C++ no
// tiene a quien llamar cuando un cartel aparece. Se apuntan aqui y cada VM que
// tenga oyentes se lleva los suyos con __GL_PROMPT_EVENTS(desde).
// Sin esto, PromptShown/PromptHidden/PromptTriggered no disparaban NUNCA: en el
// place de prueba eso dejaba muda toda la interfaz de agarrar, vender y robar.
struct GLPromptEvent { uint64_t seq; uint64_t prompt; uint64_t jugador; int tipo; };  // 0=visible 1=oculto 2=activado
inline std::vector<GLPromptEvent>& gl_prompt_log() { static std::vector<GLPromptEvent> v; return v; }
inline uint64_t& gl_prompt_seq() { static uint64_t s = 0; return s; }
inline void gl_prompt_anota(Node* prompt, Node* jugador, int tipo) {
    std::vector<GLPromptEvent>& v = gl_prompt_log();
    GLPromptEvent e;
    e.seq     = ++gl_prompt_seq();
    e.prompt  = prompt  ? (uint64_t)prompt->get_instance_id()  : 0;
    e.jugador = jugador ? (uint64_t)jugador->get_instance_id() : 0;
    e.tipo    = tipo;
    v.push_back(e);
    // Tope: si nadie escucha, la cola no puede crecer sin fin.
    if (v.size() > 128) v.erase(v.begin(), v.begin() + (v.size() - 128));
}

class ProximityPrompt : public Node3D {
    GDCLASS(ProximityPrompt, Node3D);

public:
    struct LuaCallback { lua_State* main_L; int ref; bool active = true; };

    std::vector<LuaCallback> triggered_cbs;
    std::vector<LuaCallback> hold_began_cbs;
    std::vector<LuaCallback> hold_ended_cbs;
    std::vector<LuaCallback> shown_cbs;
    std::vector<LuaCallback> hidden_cbs;

private:
    String  action_text        = "Interact";
    String  object_text        = "";
    String  key_code           = "E";
    int     key_num            = 'E';   // Enum.KeyCode de Roblox: letras en ASCII mayuscula
    float   max_distance       = 10.0f;
    float   hold_duration      = 0.0f;
    bool    enabled            = true;
    bool    require_line_sight = false;
    float   ui_offset_y        = 0.0f;
    int     style              = 0;   // Default=0, Custom=1
    // Las 4 que faltaban. El ProximityPromptScript de Roblox (el que trae el
    // place en ReplicatedFirst) lee ClickablePrompt al crear cada prompt, asi
    // que sin ella el script moria en su linea 331 y NINGUN prompt aparecia.
    bool    clickable_prompt   = true;                 // Roblox: true
    int     gamepad_key_num    = 1000;                 // Enum.KeyCode.ButtonX
    int     exclusivity        = 1;                    // OneGlobally=0 OnePerButton=1 AlwaysShow=2
    bool    auto_localize      = true;

    Area3D*          trigger_area  = nullptr;
    Label3D*         label_node    = nullptr;
    bool             player_near   = false;
    int              cuerpos_dentro = 0;   // partes del personaje dentro del area
    float            hold_elapsed  = 0.0f;
    bool             holding       = false;

    // ── DONDE VA EL PROMPT ───────────────────────────────────────────────
    // Un ProximityPrompt es un Node3D y su sitio lo pone su PADRE. Pero los
    // Model importados son nodos identidad (la posicion vive en cada pieza),
    // asi que un prompt colgado de un Model se dibujaba —etiqueta y esfera— en
    // el ORIGEN DEL MAPA. Como Roblox: si el padre no tiene sitio propio, se
    // usa su PrimaryPart o la primera BasePart de dentro.
    Node3D* _soporte() const {
        Node* p = get_parent();
        if (!p) return nullptr;
        if (p->is_class("RobloxPart") || p->is_class("Attachment"))
            return gl_cast<Node3D>(p);
        // Model: PrimaryPart si lo declara.
        if (p->has_method("GetPrimaryPartCFrame") || p->is_class("RBXModel")) {
            const Variant pp = p->get(StringName("PrimaryPart"));
            if (Node3D* n = gl_cast<Node3D>(pp)) return n;
        }
        // Si no, la primera BasePart de dentro (recorrido en anchura).
        std::vector<Node*> cola;
        for (int i = 0; i < p->get_child_count(); i++) cola.push_back(p->get_child(i));
        for (size_t i = 0; i < cola.size(); i++) {
            Node* c = cola[i];
            if (!c) continue;
            if (c->is_class("RobloxPart")) return gl_cast<Node3D>(c);
            for (int k = 0; k < c->get_child_count(); k++) cola.push_back(c->get_child(k));
        }
        return gl_cast<Node3D>(p);
    }

    // Coloca este nodo sobre el soporte cuando el padre no aporta transform.
    void _colocar_sobre_soporte() {
        Node* p = get_parent();
        if (!p || p->is_class("RobloxPart") || p->is_class("Attachment")) return;
        Node3D* s = _soporte();
        if (!s || s == gl_cast<Node3D>(p) || !s->is_inside_tree() || !is_inside_tree()) return;
        set_as_top_level(true);
        set_global_position(s->get_global_position());
    }

    void _setup_area() {
        if (trigger_area) return;
        trigger_area = memnew(Area3D);
        trigger_area->set_name("__prompt_area__");
        CollisionShape3D* cs = memnew(CollisionShape3D);
        Ref<SphereShape3D> sp = Ref<SphereShape3D>(memnew(SphereShape3D));
        sp->set_radius(max_distance);
        cs->set_shape(sp);
        trigger_area->add_child(cs);
        // El juego mete al personaje en su propio CollisionGroup ("Players"),
        // que aqui es otra capa de Godot. Con la mascara por defecto (solo la 1)
        // el area no veia al jugador y NINGUN prompt llegaba a aparecer. La capa
        // de consultas no: ahi solo hay piezas que no chocan.
        trigger_area->set_collision_mask(~GL_CAPA_CONSULTA);
        trigger_area->set_meta(GL_META_INTERNO, true);   // lo recrea la clase: no se clona
        add_child(trigger_area, false, Node::INTERNAL_MODE_BACK);

        trigger_area->connect("body_entered", callable_mp(this, &ProximityPrompt::_on_body_entered));
        trigger_area->connect("body_exited",  callable_mp(this, &ProximityPrompt::_on_body_exited));
    }

    void _setup_label() {
        if (label_node) return;
        _colocar_sobre_soporte();
        label_node = memnew(Label3D);
        label_node->set_name("__prompt_label__");
        label_node->set_position(Vector3(0, 0.5f + ui_offset_y, 0));
        label_node->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
        label_node->set_visible(false);
        label_node->set_meta(GL_META_INTERNO, true);     // lo recrea la clase: no se clona
        _update_label_text();
        add_child(label_node, false, Node::INTERNAL_MODE_BACK);
    }

    void _update_label_text() {
        if (!label_node) return;
        String txt = "[" + key_code + "] " + action_text;
        if (!object_text.is_empty()) txt += "\n" + object_text;
        label_node->set_text(txt);
    }

    // En Roblox un prompt solo reacciona al personaje del JUGADOR. El area de
    // aqui avisa de cualquier cuerpo, y en un mapa con cientos de piezas y
    // brainrots sueltos eso disparaba PromptShown decenas de veces por segundo.
    // El cuerpo que reporta el area puede ser DOS cosas distintas:
    //  · el personaje entero — el jugador local es un CharacterBody3D con UNA
    //    sola forma y sus miembros como Node3D sueltos;
    //  · una pieza suya — los rigs importados sí traen partes con cuerpo propio.
    // Mirar solo el padre (el caso de la pieza) hacia que el jugador local no
    // contara nunca y ningun prompt llegara a aparecer.
    static Node* _personaje_de(Node* body) {
        for (int nivel = 0; nivel < 2 && body; nivel++, body = body->get_parent()) {
            for (int i = 0; i < body->get_child_count(); i++)
                if (body->get_child(i)->is_class("Humanoid")) return body;
        }
        return nullptr;
    }

    bool _es_personaje(Node* body) {
        Node* pers = _personaje_de(body);
        if (!pers) return false;                  // ni siquiera es un personaje
        Node* jug = _jugador_local();
        if (!jug) return true;                    // sin jugador todavia: no se filtra
        Node* suyo = gl_cast<Node>(jug->call("get_character"));
        return !suyo || suyo == pers;             // los NPC y brainrots quedan fuera
    }

    void _on_body_entered(Node* body) {
        if (!enabled || !_es_personaje(body)) return;
        cuerpos_dentro++;
        if (player_near) return;            // ya estaba: no se avisa dos veces
        player_near = true;
        // Style = Custom: el juego dibuja SU cartel, el nuestro estorba.
        if (label_node) label_node->set_visible(style == 0);
        // El teclado solo se escucha mientras se esta al lado: con cientos de
        // prompts en el mapa, uno por uno mirando cada tecla seria absurdo.
        set_process_unhandled_input(true);
        _fire_list(shown_cbs, nullptr);
        gl_prompt_anota(this, nullptr, 0);
    }

    void _on_body_exited(Node* body) {
        if (!_es_personaje(body)) return;
        if (cuerpos_dentro > 0) cuerpos_dentro--;
        if (cuerpos_dentro > 0 || !player_near) return;
        player_near = false;
        if (label_node) label_node->set_visible(false);
        holding = false;
        hold_elapsed = 0.0f;
        set_process_unhandled_input(false);
        set_process(false);
        _fire_list(hidden_cbs, nullptr);
        gl_prompt_anota(this, nullptr, 1);
    }

    // El Player local, que es el primer argumento de Triggered en Roblox.
    Node* _jugador_local() {
        if (!is_inside_tree()) return nullptr;
        for (Node* p = get_parent(); p; p = p->get_parent()) {
            Node* svc = p->get_node_or_null(NodePath("Players"));
            if (!svc) continue;
            for (int i = 0; i < svc->get_child_count(); i++)
                if (svc->get_child(i)->is_class("PlayerObject")) return svc->get_child(i);
        }
        return nullptr;
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"),  &ProximityPrompt::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("set_action_text", "t"),   &ProximityPrompt::set_action_text);
        ClassDB::bind_method(D_METHOD("get_action_text"),         &ProximityPrompt::get_action_text);
        ClassDB::bind_method(D_METHOD("set_max_distance", "d"),   &ProximityPrompt::set_max_distance);
        ClassDB::bind_method(D_METHOD("get_max_distance"),         &ProximityPrompt::get_max_distance);
        ClassDB::bind_method(D_METHOD("set_hold_duration", "d"),  &ProximityPrompt::set_hold_duration);
        ClassDB::bind_method(D_METHOD("get_hold_duration"),        &ProximityPrompt::get_hold_duration);
        ClassDB::bind_method(D_METHOD("set_key_code", "k"),       &ProximityPrompt::set_key_code);
        ClassDB::bind_method(D_METHOD("get_key_code"),             &ProximityPrompt::get_key_code);
        ClassDB::bind_method(D_METHOD("set_enabled", "v"),        &ProximityPrompt::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),              &ProximityPrompt::get_enabled);
        ClassDB::bind_method(D_METHOD("set_style", "v"),          &ProximityPrompt::set_style);
        ClassDB::bind_method(D_METHOD("get_style"),                &ProximityPrompt::get_style);
        ClassDB::bind_method(D_METHOD("trigger"),                  &ProximityPrompt::trigger);
        ClassDB::bind_method(D_METHOD("set_object_text","t"),      &ProximityPrompt::set_object_text);
        ClassDB::bind_method(D_METHOD("get_object_text"),          &ProximityPrompt::get_object_text);
        ClassDB::bind_method(D_METHOD("set_clickable_prompt","b"), &ProximityPrompt::set_clickable_prompt);
        ClassDB::bind_method(D_METHOD("get_clickable_prompt"),     &ProximityPrompt::get_clickable_prompt);
        ClassDB::bind_method(D_METHOD("set_requires_line_of_sight","b"), &ProximityPrompt::set_requires_line_of_sight);
        ClassDB::bind_method(D_METHOD("get_requires_line_of_sight"),     &ProximityPrompt::get_requires_line_of_sight);
        ClassDB::bind_method(D_METHOD("set_gamepad_key_num","v"),  &ProximityPrompt::set_gamepad_key_num);
        ClassDB::bind_method(D_METHOD("get_gamepad_key_num"),      &ProximityPrompt::get_gamepad_key_num);
        ClassDB::bind_method(D_METHOD("set_exclusivity","v"),      &ProximityPrompt::set_exclusivity);
        ClassDB::bind_method(D_METHOD("get_exclusivity"),          &ProximityPrompt::get_exclusivity);
        ClassDB::bind_method(D_METHOD("set_auto_localize","b"),    &ProximityPrompt::set_auto_localize);
        ClassDB::bind_method(D_METHOD("get_auto_localize"),        &ProximityPrompt::get_auto_localize);
        ClassDB::bind_method(D_METHOD("set_ui_offset_y","v"),      &ProximityPrompt::set_ui_offset_y);
        ClassDB::bind_method(D_METHOD("get_ui_offset_y"),          &ProximityPrompt::get_ui_offset_y);
        ADD_PROPERTY(PropertyInfo(Variant::INT, "Style"), "set_style", "get_style");
        // Las que el importador tiene que poder escribir desde el .rbxl.
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "ObjectText"), "set_object_text", "get_object_text");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ClickablePrompt"), "set_clickable_prompt", "get_clickable_prompt");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "RequiresLineOfSight"), "set_requires_line_of_sight", "get_requires_line_of_sight");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "GamepadKeyCode"), "set_gamepad_key_num", "get_gamepad_key_num");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "Exclusivity"), "set_exclusivity", "get_exclusivity");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "AutoLocalize"), "set_auto_localize", "get_auto_localize");

        ADD_PROPERTY(PropertyInfo(Variant::STRING, "ActionText"),  "set_action_text",  "get_action_text");
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "KeyboardKeyCode"), "set_key_code", "get_key_code");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "MaxActivationDistance", PROPERTY_HINT_RANGE, "0,100,0.1"), "set_max_distance", "get_max_distance");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "HoldDuration", PROPERTY_HINT_RANGE, "0,10,0.1"), "set_hold_duration", "get_hold_duration");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,   "Enabled"), "set_enabled", "get_enabled");

        ADD_SIGNAL(MethodInfo("Triggered",  PropertyInfo(Variant::OBJECT, "player")));
        ADD_SIGNAL(MethodInfo("HoldBegan",  PropertyInfo(Variant::OBJECT, "player")));
        ADD_SIGNAL(MethodInfo("HoldEnded",  PropertyInfo(Variant::OBJECT, "player")));
    }

public:
    void _ready() override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        _setup_area();
        _setup_label();
    }

    void set_action_text(const String& t)  { action_text = t; _update_label_text(); }
    String get_action_text() const         { return action_text; }
    // ObjectText ya se pintaba en la etiqueta pero no se podia leer ni escribir
    // desde Luau: el puente va por listas de nombres y no estaba en ninguna.
    void set_object_text(const String& t)  { object_text = t; _update_label_text(); }
    String get_object_text() const         { return object_text; }
    void set_clickable_prompt(bool b)      { clickable_prompt = b; }
    bool get_clickable_prompt() const      { return clickable_prompt; }
    void set_requires_line_of_sight(bool b){ require_line_sight = b; }
    bool get_requires_line_of_sight() const{ return require_line_sight; }
    void set_gamepad_key_num(int v)        { gamepad_key_num = v; }
    int  get_gamepad_key_num() const       { return gamepad_key_num; }
    void set_exclusivity(int v)            { exclusivity = v; }
    int  get_exclusivity() const           { return exclusivity; }
    void set_auto_localize(bool b)         { auto_localize = b; }
    bool get_auto_localize() const         { return auto_localize; }
    void set_ui_offset_y(float v) {
        ui_offset_y = v;
        if (label_node) label_node->set_position(Vector3(0, 0.5f + ui_offset_y, 0));
    }
    float get_ui_offset_y() const          { return ui_offset_y; }
    void set_max_distance(float d)         { max_distance = d; }
    float get_max_distance() const         { return max_distance; }
    void set_hold_duration(float d)        { hold_duration = d; }
    float get_hold_duration() const        { return hold_duration; }
    // La tecla llega de tres formas: texto ("E"), Enum.KeyCode desde un script
    // (un NUMERO: en Roblox las letras son ASCII EN MAYUSCULA, E = 69) y, en un
    // place importado, ese mismo numero convertido a texto por el sistema de
    // propiedades. Antes solo se aceptaba la primera: un prompt importado se
    // quedaba con key_num 0 y no respondia a ninguna tecla.
    void set_key_code(const String& k) {
        if (k.is_valid_int()) { set_key_code_num((int)k.to_int()); return; }
        key_code = k;
        key_num  = (k.length() == 1) ? (int)k.to_upper()[0] : 0;
        _update_label_text();
    }
    void set_key_code_num(int kc) {
        if (kc >= 'a' && kc <= 'z') kc -= 32;    // por si llega en minuscula
        key_num = kc;
        if (kc >= 33 && kc <= 126)  key_code = String::chr((char32_t)kc);
        else if (kc == 32)          key_code = "Space";
        else                        key_code = String::num_int64(kc);
        _update_label_text();
    }
    int get_key_code_num() const           { return key_num; }
    String get_key_code() const            { return key_code; }
    void set_enabled(bool v)               { enabled = v; if (label_node && !v) label_node->set_visible(false); }
    bool get_enabled() const               { return enabled; }

    void add_triggered_cb(lua_State* L, int ref)  { triggered_cbs.push_back({L, ref, true}); }
    void add_hold_began_cb(lua_State* L, int ref) { hold_began_cbs.push_back({L, ref, true}); }
    void add_hold_ended_cb(lua_State* L, int ref) { hold_ended_cbs.push_back({L, ref, true}); }

    void set_style(int v) { style = v; if (label_node && v != 0) label_node->set_visible(false); }
    int  get_style() const { return style; }

    void remove_callbacks_for_state(lua_State* L) {
        std::vector<LuaCallback>* todas[] = { &triggered_cbs, &hold_began_cbs, &hold_ended_cbs, &shown_cbs, &hidden_cbs };
        for (auto* v : todas) for (auto& cb : *v) if (cb.main_L == L) cb.active = false;
    }
    void _gl_disconnect(int ref, int64_t vm) {
        std::vector<LuaCallback>* todas[] = { &triggered_cbs, &hold_began_cbs, &hold_ended_cbs, &shown_cbs, &hidden_cbs };
        for (auto* v : todas) for (auto& cb : *v) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
    }

    void trigger(Node* player = nullptr) {
        if (!enabled) return;
        emit_signal("Triggered", player);
        _fire_list(triggered_cbs, player);
        gl_prompt_anota(this, player, 2);   // ProximityPromptService.PromptTriggered
    }

    // ── Pulsar la tecla ─────────────────────────────────────────────────
    // NADIE llamaba a trigger(): el prompt se dibujaba, aceptaba conexiones y
    // pulsar E o F no hacia absolutamente nada. En el place de prueba eso deja
    // sin funcionar agarrar, colocar, vender y robar brainrots — o sea, el juego.
    void _unhandled_input(const Ref<InputEvent>& p_event) override {
        if (!enabled || !player_near || Engine::get_singleton()->is_editor_hint()) return;
        Ref<InputEventKey> k = p_event;
        if (k.is_null() || k->is_echo()) return;
        if (godot_key_to_roblox(k->get_keycode()) != key_num) return;

        if (k->is_pressed()) {
            if (hold_duration > 0.01f) {
                if (holding) return;
                holding = true;
                hold_elapsed = 0.0f;
                set_process(true);
                _fire_list(hold_began_cbs, _jugador_local());
            } else {
                trigger(_jugador_local());
            }
        } else if (holding) {
            // Solto antes de tiempo: se cancela, como en Roblox.
            holding = false;
            hold_elapsed = 0.0f;
            set_process(false);
            _fire_list(hold_ended_cbs, _jugador_local());
        }
    }

    void _process(double delta) override {
        if (!holding) { set_process(false); return; }
        hold_elapsed += (float)delta;
        if (hold_elapsed < hold_duration) return;
        holding = false;
        hold_elapsed = 0.0f;
        set_process(false);
        trigger(_jugador_local());
    }

private:
    void _fire_list(std::vector<LuaCallback>& list, Node* player) {
        if (list.empty()) return;
        // Copia defensiva (patron RobloxMouse::_fire): un handler puede hacer
        // Connect/Disconnect y realojar el vector — iterar el vivo era UAF.
        std::vector<LuaCallback> snap = list;
        for (size_t i = 0; i < snap.size(); i++) {
            auto& cb = snap[i];
            if (!cb.active || !gl_state_alive(cb.main_L)) continue;
            lua_State* th = lua_newthread(cb.main_L);
            lua_rawgeti(cb.main_L, LUA_REGISTRYINDEX, cb.ref);
            if (lua_isfunction(cb.main_L, -1)) {
                lua_xmove(cb.main_L, th, 1);
                if (player) {
                    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_newuserdata(th, sizeof(GodotObjectWrapper));
                    gow_set(w, player);
                    luaL_getmetatable(th, "GodotObject"); lua_setmetatable(th, -2);
                } else lua_pushnil(th);
                gl_resume_handler(th, 1);
            } else lua_pop(cb.main_L, 1);
            lua_pop(cb.main_L, 1);
        }
        for (int i = (int)list.size() - 1; i >= 0; --i)
            if (!list[i].active || !gl_state_alive(list[i].main_L)) list.erase(list.begin() + i);
    }
};

// ────────────────────────────────────────────────────────────────────
//  SpawnLocation — equivalent to Roblox SpawnLocation
//  Marks where the character will appear when the scene loads
////
//  SpawnLocation — equivalente a Roblox SpawnLocation
//  Marca donde el personaje aparecerá al cargar la escena
//
//  En Roblox SpawnLocation ES un BasePart: se dibuja, colisiona, tiene Size,
//  Color, Decals... Aqui era un marcador Node3D pelado que solo fabricaba una
//  placa visual EN EL EDITOR: en un place importado el pad no existia (ni
//  malla ni colision ni tamaño — sus propiedades de part acababan en
//  metadata). Medido con el place de prueba: su spawn es una isla-lobby de
//  138x165 studs flotando BAJO el mapa, y como el pad era invisible el
//  jugador aparecia "en el vacio" de pie sobre nada. Heredando de RobloxPart
//  el importador le aplica size/CFrame/Color/Anchored como a cualquier pieza
//  y el pad se ve y se pisa, 1:1 con Roblox.
// ────────────────────────────────────────────────────────────────────
class SpawnLocation : public RobloxPart {
    GDCLASS(SpawnLocation, RobloxPart);

    bool    enabled          = true;
    float   duration         = 10.0f;
    bool    allow_team_change = false;
    // Nombre de BrickColor del equipo que puede usar este spawn (1.15). El campo
    // ya existía pero estaba MUERTO: sin getter, sin setter y sin ADD_PROPERTY,
    // así que no había forma de leerlo ni escribirlo — por eso los spawns
    // ignoraban los equipos por completo.
    String  team_color       = "Medium stone grey";
    bool    neutral          = true;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_enabled", "v"),            &SpawnLocation::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),                  &SpawnLocation::get_enabled);
        ClassDB::bind_method(D_METHOD("set_duration", "v"),           &SpawnLocation::set_duration);
        ClassDB::bind_method(D_METHOD("get_duration"),                 &SpawnLocation::get_duration);
        ClassDB::bind_method(D_METHOD("set_neutral", "v"),            &SpawnLocation::set_neutral);
        ClassDB::bind_method(D_METHOD("get_neutral"),                  &SpawnLocation::get_neutral);
        ClassDB::bind_method(D_METHOD("set_allow_team_change", "v"),  &SpawnLocation::set_allow_team_change);
        ClassDB::bind_method(D_METHOD("get_allow_team_change"),        &SpawnLocation::get_allow_team_change);
        ClassDB::bind_method(D_METHOD("set_team_color", "v"),         &SpawnLocation::set_team_color);
        ClassDB::bind_method(D_METHOD("get_team_color"),                &SpawnLocation::get_team_color);

        ADD_PROPERTY(PropertyInfo(Variant::STRING, "TeamColor"),       "set_team_color",        "get_team_color");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Enabled"),          "set_enabled",           "get_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Duration",
                                  PROPERTY_HINT_RANGE, "0,60,0.1"),    "set_duration",          "get_duration");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Neutral"),          "set_neutral",           "get_neutral");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "AllowTeamChangeOnTouch"), "set_allow_team_change", "get_allow_team_change");
    }

public:
    // Placa de fabrica como la que inserta Roblox Studio: 12x1x12, gris,
    // anclada. Un place importado la sobreescribe con lo que diga el archivo.
    // (La placa de editor de antes — StaticBody "Plate" + Visual + Collision —
    // sobra: ahora el part ES la placa. Una escena vieja que la traiga baked
    // solo duplica el visual en el mismo sitio, sin romper nada.)
    SpawnLocation() {
        set_anchored(true);
        set_size(Vector3(12, 1, 12));
        set_color(Color(0.64f, 0.64f, 0.64f));  // Medium stone grey
    }

    void set_enabled(bool v)           { enabled = v; }
    bool get_enabled() const           { return enabled; }
    void set_duration(float v)         { duration = v; }
    float get_duration() const         { return duration; }
    void set_neutral(bool v)           { neutral = v; }
    bool get_neutral() const           { return neutral; }
    void set_allow_team_change(bool v) { allow_team_change = v; }
    bool get_allow_team_change() const { return allow_team_change; }
    void   set_team_color(String v)    { team_color = v; }
    String get_team_color() const      { return team_color; }

    Vector3 get_spawn_position() const {
        // Sobre la CARA SUPERIOR del pad (como Roblox), no sobre su centro:
        // con la placa de 1 stud da el +3.0 de siempre; con un pad gordo no
        // te entierra dentro.
        const Vector3 base = is_inside_tree() ? get_global_position() : get_position();
        return base + Vector3(0, get_size().y * 0.5f + 2.5f, 0);
    }
};

#endif // ROBLOX_INTERACTIVE_H

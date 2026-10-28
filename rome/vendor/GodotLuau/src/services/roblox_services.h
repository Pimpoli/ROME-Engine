#ifndef ROBLOX_SERVICES_H
#define ROBLOX_SERVICES_H

#include "roblox_chat.h"
#include "gl_cast.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/procedural_sky_material.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/object.hpp>
#include "gl_debug.h"

#include "lua.h"
#include "lualib.h"
#include "gl_errors.h"
#include "gl_runtime.h"   // gl_state_alive, GodotObjectWrapper, gow_set
#include "../ui/gl_gui_core.h"   // gl_hover_tick (hover central de la GUI)
#include <vector>
#include <algorithm>
#include <cstring>

// ── Shared resume queue: RunService → ScriptNodeBase ─────────────────
// RunService queues pending coroutine resumes here; ScriptNodeBase drains
// them each frame so it can update spawned_threads correctly.
struct LuauPendingResume {
    lua_State* thread;   // coroutine to resume
    lua_State* main_L;   // owning main state
    double     delta;    // dt argument to pass
    Node*      node_arg; // optional node (WaitForChild result); nullptr if not used
    // Reanudar con args de red (RemoteEvent:Wait): si use_args, se decodifica
    // `args` con el nodo ctx (por ObjectID, sin puntero colgante entre frames)
    // y se pasan como N valores en vez de delta/node_arg.
    Array      args;
    uint64_t   ctx_id = 0;
    bool       use_args = false;
    // Stepped:Wait() devuelve DOS numeros en Roblox: (tiempo, delta). Con esto
    // se reanuda con `delta2` primero y `delta` despues.
    bool       dos_valores = false;
    double     delta2 = 0.0;
};
inline std::vector<LuauPendingResume>& get_pending_resumes() {
    static std::vector<LuauPendingResume> v;
    return v;
}

#include "gl_servicios.h"
#include "gl_chat_texto.h"   // TextChannel, TextSource, TextChatCommand... (F5)

using namespace godot;

// ════════════════════════════════════════════════════════════════════
//  RobloxValue — objetos de valor (NumberValue/IntValue/StringValue/
//  BoolValue/ObjectValue/Vector3Value/…). Es un NODO REAL (antes eran
//  tablas Lua que rompían Parent/FindFirstChild → leaderstats no funcionaba).
//  .Value se lee/escribe por el puente _gl_bridge; .Changed y
//  :GetPropertyChangedSignal("Value") disparan al cambiar (como Roblox).
// ════════════════════════════════════════════════════════════════════
class RobloxValue : public Node {
    GDCLASS(RobloxValue, Node);
    Variant _value;
public:
    String roblox_class = "NumberValue";   // clase Roblox original (para ClassName/IsA)
    struct LuaCallback { lua_State* main_L; int ref; bool active = true; };
    std::vector<LuaCallback> changed_cbs;

    void add_changed_cb(lua_State* L, int ref) { changed_cbs.push_back({ L, ref, true }); }
    void _gl_disconnect(int ref, int64_t vm) { for (auto& cb : changed_cbs) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false; }

    void set_value(const Variant& v) {
        bool changed = (_value != v);
        _value = v;
        if (changed) fire_changed();
    }
    Variant get_value() const { return _value; }

    void _push_value(lua_State* th) {
        switch (_value.get_type()) {
            case Variant::BOOL:   lua_pushboolean(th, (bool)_value); break;
            case Variant::INT:    lua_pushnumber(th, (double)(int64_t)_value); break;
            case Variant::FLOAT:  lua_pushnumber(th, (double)_value); break;
            case Variant::STRING: { String s = _value; lua_pushstring(th, s.utf8().get_data()); } break;
            case Variant::OBJECT: {
                Node* nd = gl_cast<Node>((Object*)(_value.operator Object*()));
                if (nd) {
                    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_newuserdata(th, sizeof(GodotObjectWrapper));
                    gow_set(w, nd);
                    luaL_getmetatable(th, "GodotObject");
                    lua_setmetatable(th, -2);
                } else lua_pushnil(th);
            } break;
            default: lua_pushnil(th);
        }
    }

    void fire_changed() {
        std::vector<LuaCallback> snap = changed_cbs;
        for (auto& cb : snap) {
            if (!cb.active || !gl_state_alive(cb.main_L)) continue;
            lua_State* th = lua_newthread(cb.main_L);
            lua_rawgeti(cb.main_L, LUA_REGISTRYINDEX, cb.ref);
            if (lua_isfunction(cb.main_L, -1)) {
                lua_xmove(cb.main_L, th, 1);
                _push_value(th);
                gl_resume_handler(th, 1);
            } else lua_pop(cb.main_L, 1);
            lua_pop(cb.main_L, 1);
        }
    }
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"), &RobloxValue::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("set_value", "v"), &RobloxValue::set_value);
        ClassDB::bind_method(D_METHOD("get_value"),      &RobloxValue::get_value);
        ADD_PROPERTY(PropertyInfo(Variant::NIL, "Value", PROPERTY_HINT_NONE, "",
                     PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT),
                     "set_value", "get_value");
    }
public:
    RobloxValue() { set_meta("_gl_bridge", true); }
};

// ── Los objetos Value, cada uno CON SU CLASE ─────────────────────────
// En Roblox no existe un "Value" generico: son IntValue, StringValue,
// NumberValue... clases distintas. Aqui eran todas la MISMA (RobloxValue con un
// campo `roblox_class` de texto), y eso se notaba en dos sitios:
//   · en el arbol salian 998 nodos llamados "RobloxValue"
//   · `nodo:IsA("IntValue")` dependia del campo de texto, no del tipo real
// Con una subclase por tipo el editor dice la verdad, IsA funciona por herencia
// como en Roblox, y no cuesta nada: siguen siendo el mismo RobloxValue por
// dentro, asi que todo el codigo que castea a RobloxValue sigue igual.
#define GL_VALUE_CLASE(Nombre)                          \
    class Nombre : public RobloxValue {                 \
        GDCLASS(Nombre, RobloxValue);                   \
    protected:                                          \
        static void _bind_methods() {}                  \
    public:                                             \
        Nombre() { roblox_class = #Nombre; }            \
    };

GL_VALUE_CLASE(NumberValue)
GL_VALUE_CLASE(IntValue)
GL_VALUE_CLASE(StringValue)
GL_VALUE_CLASE(BoolValue)
GL_VALUE_CLASE(ObjectValue)
GL_VALUE_CLASE(Vector3Value)
GL_VALUE_CLASE(CFrameValue)
GL_VALUE_CLASE(Color3Value)
GL_VALUE_CLASE(BrickColorValue)
GL_VALUE_CLASE(RayValue)
#undef GL_VALUE_CLASE

// ════════════════════════════════════════════════════════════════════
//  PlayerObject — el objeto "Player" de Roblox (Player ≠ Character).
//  Vive bajo el servicio Players (no en Workspace). Tiene UserId/DisplayName
//  y su .Character apunta al personaje (RobloxPlayer local) o al muñeco
//  (AnimatableBody3D) del peer remoto. Señales: CharacterAdded /
//  CharacterRemoving / CharacterAppearanceLoaded.
// ════════════════════════════════════════════════════════════════════
class PlayerObject : public Node {
    GDCLASS(PlayerObject, Node);
public:
    int64_t  user_id = 0;
    String   display_name;
    uint64_t character_id = 0;    // ObjectID del personaje/muñeco (sin puntero colgante)
    String   team_color = "Medium stone grey";   // nombre de BrickColor si no hay Team (1.15)
    int64_t  account_age = 0;
    bool     is_local = false;
    bool     added_fired = false; // PlayerAdded ya disparado (una sola vez por Player)
    int      camera_mode = 0;     // Enum.CameraMode (0 Classic, 1 LockFirstPerson)
    int      peer_id = 0;         // peer ENet (0 = local/single)
    uint64_t team_id = 0;         // ObjectID del Team (1.14.10); 0 = Neutral

    Node* get_team() const {
        return team_id ? gl_cast<Node>(ObjectDB::get_instance(team_id)) : nullptr;
    }

    struct LuaCallback { lua_State* main_L; int ref; bool active = true; };
    std::vector<LuaCallback> char_added_cbs;
    std::vector<LuaCallback> char_removing_cbs;
    std::vector<LuaCallback> char_appearance_cbs;
    struct LuaWait { lua_State* th; lua_State* main_L; };
    std::vector<LuaWait> char_added_waits;   // CharacterAdded:Wait()
    void add_char_added_wait(lua_State* th, lua_State* mL) { char_added_waits.push_back({ th, mL }); }

    void add_char_added_cb(lua_State* L, int ref)      { char_added_cbs.push_back({ L, ref, true }); }
    void add_char_removing_cb(lua_State* L, int ref)   { char_removing_cbs.push_back({ L, ref, true }); }
    void add_char_appearance_cb(lua_State* L, int ref) { char_appearance_cbs.push_back({ L, ref, true }); }
    void _gl_disconnect(int ref, int64_t vm) {
        for (auto& c : char_added_cbs)      if (c.ref == ref && GL_MISMA_VM(c)) c.active = false;
        for (auto& c : char_removing_cbs)   if (c.ref == ref && GL_MISMA_VM(c)) c.active = false;
        for (auto& c : char_appearance_cbs) if (c.ref == ref && GL_MISMA_VM(c)) c.active = false;
        for (auto& c : chatted_cbs)         if (c.ref == ref && GL_MISMA_VM(c)) c.active = false;
    }

    // Player.Chatted(mensaje, destinatario). Lo disparan el chat al enviar (el
    // jugador local) y el servidor al recibir el mensaje de un cliente. Era una
    // señal inerte: los comandos de admin por chat (";kill me") no llegaban.
    std::vector<LuaCallback> chatted_cbs;
    void add_chatted_cb(lua_State* L, int ref) { chatted_cbs.push_back({ L, ref, true }); }
    void gl_fire_chatted(const String& msg) {
        const CharString m = msg.utf8();
        gl_fire_cbs(chatted_cbs, [&](lua_State* th) {
            lua_pushstring(th, m.get_data());
            lua_pushnil(th);
            return 2;
        });
    }

    Node* get_character() const {
        return character_id ? gl_cast<Node>(ObjectDB::get_instance(character_id)) : nullptr;
    }
    void _fire_char(std::vector<LuaCallback>& list, Node* character) {
        std::vector<LuaCallback> snap = list;
        for (auto& cb : snap) {
            if (!cb.active || !gl_state_alive(cb.main_L)) continue;
            lua_State* th = lua_newthread(cb.main_L);
            lua_rawgeti(cb.main_L, LUA_REGISTRYINDEX, cb.ref);
            if (lua_isfunction(cb.main_L, -1)) {
                lua_xmove(cb.main_L, th, 1);
                if (character) {
                    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_newuserdata(th, sizeof(GodotObjectWrapper));
                    gow_set(w, character);
                    luaL_getmetatable(th, "GodotObject");
                    lua_setmetatable(th, -2);
                    gl_resume_handler(th, 1);
                } else {
                    gl_resume_handler(th, 0);
                }
            } else lua_pop(cb.main_L, 1);
            lua_pop(cb.main_L, 1);
        }
    }
    // Asigna el personaje SIN disparar señales (para poder disparar PlayerAdded
    // ANTES que CharacterAdded, como garantiza Roblox).
    void set_character_silent(Node* c) {
        character_id = c ? (uint64_t)c->get_instance_id() : 0;
        _no_archivable(c);
    }
    // El personaje de un jugador nace con Archivable = false, como en Roblox:
    // character:Clone() da nil hasta que el juego lo pone a true.
    static void _no_archivable(Node* c) {
        static const StringName k("_gl_no_archivable");
        if (c && !c->has_meta(k)) c->set_meta(k, true);
    }
    // Dispara CharacterAdded + CharacterAppearanceLoaded del personaje actual.
    void fire_character_added() {
        Node* c = get_character();
        if (!c) return;
        _fire_char(char_added_cbs, c);
        _fire_char(char_appearance_cbs, c);
        // CharacterAdded:Wait() → reanudar (diferido) con el personaje. Se pasa por
        // ObjectID (ruta use_args), NO por puntero crudo: entre encolar y drenar el
        // personaje podría liberarse (peer que se va) → use-after-free.
        if (!char_added_waits.empty()) {
            Array a;
            Dictionary d;
            d["__glnet"] = String("inst");
            d["p"] = c->is_inside_tree() ? String(c->get_path()) : String();
            a.push_back(d);
            for (auto& w : char_added_waits) {
                if (!gl_state_alive(w.main_L)) continue;
                LuauPendingResume pr;
                pr.thread = w.th; pr.main_L = w.main_L; pr.delta = 0; pr.node_arg = nullptr;
                pr.args = a; pr.ctx_id = (uint64_t)get_instance_id(); pr.use_args = true;
                get_pending_resumes().push_back(pr);
            }
            char_added_waits.clear();
        }
    }
    // Asigna el personaje y dispara CharacterAdded si cambió (orden no crítico).
    void set_character(Node* c) {
        uint64_t nid = c ? (uint64_t)c->get_instance_id() : 0;
        if (nid == character_id) return;
        character_id = nid;
        _no_archivable(c);
        if (c) fire_character_added();
    }
    void fire_character_removing() {
        Node* c = get_character();
        if (c) _fire_char(char_removing_cbs, c);
    }
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"), &PlayerObject::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("gl_fire_chatted","msg"), &PlayerObject::gl_fire_chatted);
        ClassDB::bind_method(D_METHOD("_gl_team_color_name"), &PlayerObject::_gl_team_color_name);
        ClassDB::bind_method(D_METHOD("_gl_is_local"),        &PlayerObject::_gl_is_local);
        // Lo consulta el ProximityPrompt para reaccionar SOLO al personaje del
        // jugador, no a cada NPC o brainrot que le pase por delante.
        ClassDB::bind_method(D_METHOD("get_character"),       &PlayerObject::get_character);
    }

public:
    // Color efectivo del jugador: el de su Team si lo tiene, si no el propio.
    // Lo usa el Workspace para elegir SpawnLocation (1.15).
    String _gl_team_color_name() const {
        Node* t = get_team();
        return t ? String(t->get("TeamColor")) : team_color;
    }
    bool _gl_is_local() const { return is_local; }
};

// ════════════════════════════════════════════════════════════════════
//  Service placement protection: in Roblox, services only exist under
//  the DataModel. These helpers enforce that in the editor: if someone
//  tries to add a service to an invalid parent, it is deleted and an
//  error is shown. At runtime there is no restriction (scene is saved).
////
//  Protección de servicios: en Roblox, los servicios solo existen bajo
//  el DataModel. Estos helpers lo garantizan en el editor: si alguien
//  intenta añadir un servicio a un nodo incorrecto, se elimina y muestra
//  un error. En runtime no hay restricción (la escena ya está guardada).
// ════════════════════════════════════════════════════════════════════

inline bool _is_valid_game_parent(Node* p) {
    if (!p) return false;
    return p->is_class("RobloxTemplate") ||
           p->is_class("RobloxGame3D")    ||
           p->is_class("RobloxGame2D");
}

inline bool _is_valid_starter_parent(Node* p) {
    if (!p) return false;
    return p->is_class("StarterPlayer");
}

// Returns false if the node was removed (invalid parent).
//// Retorna false si el nodo fue eliminado (padre inválido).
inline bool _enforce_game_parent(Node* self) {
    if (!Engine::get_singleton()->is_editor_hint()) return true;
    if (_is_valid_game_parent(self->get_parent())) return true;
    UtilityFunctions::push_error(
        String("[GodotLuau] '") + self->get_class() +
        "' is a Roblox service and can only exist as a direct child of "
        "RobloxGame3D, RobloxGame2D or RobloxTemplate. "
        "Use the RobloxGame3D/2D root node to auto-generate the structure.");
    self->queue_free();
    return false;
}

inline bool _enforce_starter_parent(Node* self) {
    if (!Engine::get_singleton()->is_editor_hint()) return true;
    if (_is_valid_starter_parent(self->get_parent())) return true;
    UtilityFunctions::push_error(
        String("[GodotLuau] '") + self->get_class() +
        "' can only be a direct child of StarterPlayer.");
    self->queue_free();
    return false;
}

// ════════════════════════════════════════════════════════════════════
//  STORAGE
//  Direct equivalents to Roblox services
////
//  ALMACENAMIENTO
//  Equivalentes directos a los servicios de Roblox
// ════════════════════════════════════════════════════════════════════

// Visible to client AND server — used for ModuleScripts and shared objects
//// Visible para cliente Y servidor — se usa para ModuleScripts y objetos compartidos
class ReplicatedStorage : public Node {
    GDCLASS(ReplicatedStorage, Node);
protected:
    static void _bind_methods() {}
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_game_parent(this);
    }
};

// Server-only — secure content not accessible to the client
//// Solo visible para el servidor — contenido seguro no accesible al cliente
class ServerStorage : public Node {
    GDCLASS(ServerStorage, Node);
protected:
    static void _bind_methods() {}
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_game_parent(this);
    }
};

// Replicated to client BEFORE the rest of the scene loads
// Ideal for loading screens and critical assets
//// Se replica al cliente ANTES de que cargue el resto de la escena
//// Ideal para pantallas de carga, assets críticos
class ReplicatedFirst : public Node {
    GDCLASS(ReplicatedFirst, Node);
protected:
    static void _bind_methods() {}
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_game_parent(this);
    }
};

// ════════════════════════════════════════════════════════════════════
//  SERVER SCRIPTS
////  SCRIPTS DEL SERVIDOR
// ════════════════════════════════════════════════════════════════════

// Scripts that run server-side only (secure game logic)
//// Scripts que solo se ejecutan en el servidor (lógica de juego segura)
class ServerScriptService : public Node {
    GDCLASS(ServerScriptService, Node);
protected:
    static void _bind_methods() {}
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_game_parent(this);
    }
};

// ════════════════════════════════════════════════════════════════════
//  STARTER PLAYER — Player scripts and character system
////  STARTER PLAYER — Sistema de scripts y personaje del jugador
// ════════════════════════════════════════════════════════════════════

// Scripts cloned onto each character when they spawn
//// Scripts que se clonan en cada personaje al spawnear
class StarterCharacterScripts : public Node {
    GDCLASS(StarterCharacterScripts, Node);
protected:
    static void _bind_methods() {}
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_starter_parent(this);
    }
};

// Scripts cloned onto the local player when they join
//// Scripts que se clonan en el jugador local al entrar
class StarterPlayerScripts : public Node {
    GDCLASS(StarterPlayerScripts, Node);
protected:
    static void _bind_methods() {}
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_starter_parent(this);
    }
};

// Main player container — auto-creates its children when added in the editor
//// Container principal del jugador — crea sus hijos automáticamente al añadirlo al editor
class StarterPlayer : public Node {
    GDCLASS(StarterPlayer, Node);
protected:
    static void _bind_methods() {}

    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE && Engine::get_singleton()->is_editor_hint()) {
            if (!_enforce_game_parent(this)) return;
            if (!is_inside_tree()) return;   // sin arbol, get_tree() es nulo
            Node* root = get_tree()->get_edited_scene_root();
            if (!root) return;

            // Auto-create children if they don't exist
            //// Auto-crear hijos si no existen
            auto make_child = [&](const String& name, Node* child) {
                if (!get_node_or_null(name)) {
                    child->set_name(name);
                    add_child(child);
                    child->set_owner(root);
                }
            };

            if (!get_node_or_null("StarterCharacterScripts")) make_child("StarterCharacterScripts", memnew(StarterCharacterScripts));
            if (!get_node_or_null("StarterPlayerScripts"))    make_child("StarterPlayerScripts",    memnew(StarterPlayerScripts));
        }
    }
};

// ════════════════════════════════════════════════════════════════════
//  STARTER GUI AND PACK
////  STARTER GUI Y PACK
// ════════════════════════════════════════════════════════════════════

// ScreenGuis here are cloned into each player's HUD
//// ScreenGuis aquí se clonan al HUD de cada jugador
// ── Contenedores por jugador (1.14.10) ───────────────────────────────
// En Roblox cada Player tiene los suyos y al entrar se CLONA dentro lo que haya
// en StarterGui / StarterPack / StarterPlayerScripts. (Backpack ya existía.)
class PlayerGui : public Node {
    GDCLASS(PlayerGui, Node);
protected:
    static void _bind_methods() {}
};
class PlayerScripts : public Node {
    GDCLASS(PlayerScripts, Node);
protected:
    static void _bind_methods() {}
};

class StarterGui : public Node {
    GDCLASS(StarterGui, Node);
protected:
    static void _bind_methods() {}
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_game_parent(this);
    }
};

// Tools / inventory items (backpack) for the player
//// Tools / items del inventario (mochila) del jugador
class StarterPack : public Node {
    GDCLASS(StarterPack, Node);
protected:
    static void _bind_methods() {}
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_game_parent(this);
    }
};

// ════════════════════════════════════════════════════════════════════
//  PLAYERS AND TEAMS
////  JUGADORES Y EQUIPOS
// ════════════════════════════════════════════════════════════════════

// Players service — accessed via game:GetService("Players")
//// Servicio de jugadores — acceso via game:GetService("Players")
class Players : public Node {
    GDCLASS(Players, Node);

public:
    struct LuaCallback { lua_State* main_L; int ref; bool active = true; bool once = false; };
    std::vector<LuaCallback> player_added_cbs;
    std::vector<LuaCallback> player_removing_cbs;
    // PlayerAdded:Wait() / PlayerRemoving:Wait() (1.14.10): el hilo cede y lo
    // reanuda _fire_player_event con el Player por la cola compartida.
    struct LuaWait { lua_State* th; lua_State* main_L; };
    std::vector<LuaWait> player_added_waits;
    std::vector<LuaWait> player_removing_waits;
    int max_players = 50;

    // PlayerAdded del jugador LOCAL: se dispara una sola vez, cuando el personaje
    // ya existe y tras un breve margen para que los scripts alcancen a :Connect.
    // (Los peers remotos los dispara el NetworkService al conectar/desconectar.)
    bool   _gl_local_added_fired = false;
    double _gl_ready_time = 0.0;

    // ── Auto-respawn (1.15) ──────────────────────────────────────────────
    // Hasta ahora el Humanoid emitía Died y ahí se acababa todo: nadie volvía a
    // levantar al jugador. Como Roblox: al morir se espera RespawnTime y se
    // reconstruye el personaje, salvo que CharacterAutoLoads esté en false.
    bool   character_auto_loads = true;
    double respawn_time = 5.0;
    double _death_timer = -1.0;   // <0 = nadie muerto esperando

protected:
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_game_parent(this);
        else if (p_what == NOTIFICATION_READY && !Engine::get_singleton()->is_editor_hint())
            set_process(true);
    }
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"), &Players::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("load_local_character"), &Players::load_local_character);
        ClassDB::bind_method(D_METHOD("get_local_player"),     &Players::get_local_player);
        ClassDB::bind_method(D_METHOD("get_players"),          &Players::get_players);
        ClassDB::bind_method(D_METHOD("get_max_players"),      &Players::get_max_players);
        ClassDB::bind_method(D_METHOD("set_max_players", "v"), &Players::set_max_players);
        ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "LocalPlayer",
                                  PROPERTY_HINT_NODE_TYPE, "Node"),
                     "", "get_local_player");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "MaxPlayers"), "set_max_players", "get_max_players");
        ClassDB::bind_method(D_METHOD("set_character_auto_loads", "v"), &Players::set_character_auto_loads);
        ClassDB::bind_method(D_METHOD("get_character_auto_loads"),      &Players::get_character_auto_loads);
        ClassDB::bind_method(D_METHOD("set_respawn_time", "v"),         &Players::set_respawn_time);
        ClassDB::bind_method(D_METHOD("get_respawn_time"),              &Players::get_respawn_time);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "CharacterAutoLoads"), "set_character_auto_loads", "get_character_auto_loads");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "RespawnTime"),        "set_respawn_time",         "get_respawn_time");
    }

public:
    void _process(double delta) override {
        _tick_player_added(delta);
        _tick_respawn(delta);
    }

    bool get_character_auto_loads() const { return character_auto_loads; }
    void set_character_auto_loads(bool v) { character_auto_loads = v; if (!v) _death_timer = -1.0; }
    double get_respawn_time() const { return respawn_time; }
    void set_respawn_time(double v)  { respawn_time = Math::max(v, 0.0); }

    int  get_max_players() const { return max_players; }
    void set_max_players(int v)  { max_players = v; }

private:
    // PlayerAdded del jugador local: una sola vez, cuando el personaje ya existe.
    void _tick_player_added(double delta) {
        if (_gl_local_added_fired) return;
        Node* character = _find_local_character();
        if (!character) { _gl_ready_time = 0.0; return; }  // esperar al personaje
        _gl_ready_time += delta;
        if (_gl_ready_time < 0.25) return;                 // margen para PlayerAdded:Connect
        _gl_local_added_fired = true;
        PlayerObject* po = ensure_local_player_obj();      // Player con .Character ya asignado (silencioso)
        if (po) {
            fire_player_added(po);        // PlayerAdded PRIMERO (como Roblox)
            po->fire_character_added();   // ...luego CharacterAdded (ya hay listeners)
        }
    }

    // Muerte → esperar RespawnTime → reconstruir el personaje (como Roblox).
    // Se vigila por sondeo en vez de conectarse a Died: el personaje se destruye
    // y se rehace en cada respawn, así que una conexión habría que rehacerla
    // cada vez (y se pierde si alguien mata al Humanoid sin pasar por Died).
    void _tick_respawn(double delta) {
        if (!character_auto_loads) return;
        if (_death_timer >= 0.0) {
            _death_timer += delta;
            if (_death_timer >= respawn_time) {
                _death_timer = -1.0;
                load_local_character();
            }
            return;
        }
        Node* ch  = _find_local_character();
        Node* hum = ch ? ch->get_node_or_null(NodePath("Humanoid")) : nullptr;
        if (!hum || !hum->has_method("get_health")) return;
        if ((double)hum->call("get_health") <= 0.0) _death_timer = 0.0;
    }

public:

    void add_player_added_cb(lua_State* L, int ref, bool once = false)    { player_added_cbs.push_back({L,ref,true,once}); }
    void add_player_removing_cb(lua_State* L, int ref, bool once = false) { player_removing_cbs.push_back({L,ref,true,once}); }
    // Crea PlayerGui/Backpack/PlayerScripts del jugador y clona dentro lo que
    // haya en StarterGui/StarterPack/StarterPlayerScripts (como Roblox al
    // entrar). Idempotente. Se instancia por NOMBRE de clase para no depender
    // del orden de includes (Backpack vive en otra cabecera).
    void ensure_player_containers(PlayerObject* po) {
        if (!po || !is_inside_tree()) return;
        Node* dm = get_parent();   // DataModel
        if (!dm) return;
        // Los Starter se clonan SOLO para el jugador local. El PlayerGui de un
        // jugador REMOTO existe (el servidor puede mirarlo) pero va vacío: si se
        // clonara para todos, cada máquina renderizaría N copias de la UI, una
        // por jugador conectado.
        bool local = po->is_local;
        _make_container(po, "PlayerGui", "PlayerGui", local ? dm->get_node_or_null("StarterGui") : nullptr);
        _make_container(po, "Backpack",  "Backpack",  local ? dm->get_node_or_null("StarterPack") : nullptr);
        // PlayerScripts: contenedor SIN clonar dentro. Hoy los StarterPlayerScripts
        // los clona el Workspace sobre el PERSONAJE; clonarlos aquí también los
        // haría correr DOS veces. Mover ese cargador aquí es lo correcto en
        // Roblox pero cambia script.Parent → se hará aparte, no de tapadillo.
        _make_container(po, "PlayerScripts", "PlayerScripts", nullptr);
        // StarterGear: FALTABA ENTERO. En Roblox es donde viven las Tools que
        // sobreviven al respawn, y los inventarios persistentes de los juegos se
        // apoyan en el (escuchan su ChildAdded/ChildRemoved para guardar). Sin
        // el, un `player:WaitForChild("StarterGear", 10)` devolvia nil a los 10
        // segundos y el sistema de guardado de objetos no se conectaba NUNCA.
        _make_container(po, "Folder", "StarterGear", nullptr);
        if (local) _ensure_player_module(po);
    }

    // Players.LocalPlayer.PlayerScripts.PlayerModule — el ModuleScript que
    // Roblox pone ahí y que medio ecosistema requiere para congelar al jugador
    // (volar, cinemáticas, tutoriales). Aquí no existía, así que el
    // WaitForChild("PlayerModule") se quedaba colgado PARA SIEMPRE y con él el
    // script que lo esperaba (en el place de prueba, el Fly del panel de admin).
    // Es un puente: GetControls():Disable() apaga el input de verdad.
    void _ensure_player_module(PlayerObject* po) {
        Node* ps = po ? po->get_node_or_null(NodePath("PlayerScripts")) : nullptr;
        if (!ps || ps->get_node_or_null(NodePath("PlayerModule"))) return;
        Node* mod = gl_cast<Node>(ClassDB::instantiate(StringName("ModuleScript")));
        if (!mod) return;
        mod->set_name("PlayerModule");
        // OJO: ClassDB::instantiate devuelve un Variant y LuauScript es un
        // Resource (con recuento). Si se saca el Object* a una variable suelta,
        // el Variant temporal muere al final de esa linea y se lleva el recurso
        // por delante. Se mantiene vivo en `v` mientras se toma la referencia.
        Variant v = ClassDB::instantiate(StringName("LuauScript"));
        Ref<Resource> src = gl_cast<Resource>(v);
        if (src.is_null()) { memdelete(mod); return; }
        src->call("gl_set_fuente", String::utf8(
R"LUAU(-- PlayerModule — puente de compatibilidad con Roblox (lo genera el motor).
-- Roblox lo coloca en Players.LocalPlayer.PlayerScripts y los juegos hacen
--   require(player.PlayerScripts.PlayerModule):GetControls():Disable()
-- para congelar al personaje. Aqui Disable/Enable apagan y encienden el input
-- de verdad (lo leen RobloxPlayer y Humanoid).
local PlayerModule = {}
PlayerModule.__index = PlayerModule

local Controls = {}
Controls.__index = Controls
function Controls:Enable()  __GL_SET_CONTROLS(false) end
function Controls:Disable() __GL_SET_CONTROLS(true)  end
-- La direccion que pide el teclado AHORA, en ejes de camara (X derecha,
-- Z adelante negativo), igual que Roblox. Devolvia siempre cero, asi que
-- cualquier cosa que se moviera con el (volar, control por script) se
-- quedaba clavada en el sitio.
function Controls:GetMoveVector()
    local ok, uis = pcall(function() return game:GetService("UserInputService") end)
    if not ok or not uis then return Vector3.new(0, 0, 0) end
    local x, z = 0, 0
    if uis:IsKeyDown(Enum.KeyCode.D) then x = x + 1 end
    if uis:IsKeyDown(Enum.KeyCode.A) then x = x - 1 end
    if uis:IsKeyDown(Enum.KeyCode.S) then z = z + 1 end
    if uis:IsKeyDown(Enum.KeyCode.W) then z = z - 1 end
    if x == 0 and z == 0 then return Vector3.new(0, 0, 0) end
    local m = math.sqrt(x * x + z * z)
    return Vector3.new(x / m, 0, z / m)
end

local CameraController = {}
CameraController.__index = CameraController
function CameraController:Enable(_) end
function CameraController:Disable() end

local controls = setmetatable({}, Controls)
local camera   = setmetatable({}, CameraController)

function PlayerModule:GetControls()               return controls end
function PlayerModule:GetCameraController()       return camera   end
function PlayerModule:GetClickToMoveController()  return controls end

return setmetatable({}, PlayerModule)
)LUAU"));
        mod->set("codigo_luau", src);
        ps->add_child(mod);
    }
    void _make_container(PlayerObject* po, const char* cls, const char* name, Node* starter) {
        if (po->get_node_or_null(NodePath(name))) return;   // ya existe
        Node* c = gl_cast<Node>(ClassDB::instantiate(StringName(cls)));
        if (!c) return;
        c->set_name(name);
        po->add_child(c);
        if (!starter) return;
        // Con el clon de Clone() (gl_clone.h), no con duplicate(): copia solo lo
        // de Roblox, se salta lo no Archivable y no copia los hijos que cada
        // clase se fabrica sola. duplicate() era 126 ms del primer frame de
        // Build a Mountain (su StarterGui entero a PlayerGui).
        for (int i = 0; i < starter->get_child_count(); i++) {
            Node* src = starter->get_child(i);
            if (!src) continue;
            static const StringName k_no_arch("_gl_no_archivable");
            if (src->has_meta(k_no_arch)) continue;
            if (Node* dup = gl_clonar(src)) c->add_child(dup);
        }
    }

    void add_player_added_wait(lua_State* th, lua_State* mL)    { player_added_waits.push_back({th, mL}); }
    void add_player_removing_wait(lua_State* th, lua_State* mL) { player_removing_waits.push_back({th, mL}); }
    void _gl_disconnect(int ref, int64_t vm) {
        for (auto& cb : player_added_cbs)    if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
        for (auto& cb : player_removing_cbs) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
    }

    void fire_player_added(Node* player) {
        _fire_player_event(player_added_cbs, player);
        _drain_waits(player_added_waits, player);
        if (gl_jugador_entra_hook()) gl_jugador_entra_hook()(player);   // recibos pendientes (gl_tienda.h)
    }
    void fire_player_removing(Node* player) { _fire_player_event(player_removing_cbs, player); _drain_waits(player_removing_waits, player); }
    void _drain_waits(std::vector<LuaWait>& waits, Node* player) {
        if (waits.empty()) return;
        std::vector<LuaWait> snap = waits;
        waits.clear();   // vaciar ANTES: el hilo reanudado puede volver a esperar
        for (auto& w : snap) {
            if (!gl_state_alive(w.main_L)) continue;
            LuauPendingResume pr;
            pr.thread = w.th; pr.main_L = w.main_L; pr.delta = 0.0; pr.node_arg = player;
            get_pending_resumes().push_back(pr);
        }
    }

    // El personaje local, recordado por ObjectID. Va como MIEMBRO y no en una
    // estatica suelta porque en el test multijugador local conviven varios
    // DataModel, cada uno con su Players y su personaje.
    mutable uint64_t _cache_personaje = 0;

    // Busca el PERSONAJE local (RobloxPlayer/2D) en Workspace.
    //
    // Lo llaman desde _process (respawn, salud, camara): antes recorria TODOS
    // los hijos del Workspace con DOS is_class por hijo en cada frame, y cada
    // is_class construye un String y cruza la frontera de la GDExtension. En un
    // Workspace con cientos de hijos eso es coste fijo por frame para encontrar
    // algo que no se mueve. Ahora se recuerda y solo se revalida (que siga vivo
    // y dentro del arbol), que es O(1).
    Node* _find_local_character() const {
        if (!is_inside_tree()) return nullptr;
        if (_cache_personaje) {
            Node* c = gl_cast<Node>(ObjectDB::get_instance(ObjectID(_cache_personaje)));
            if (c && c->is_inside_tree()) return c;
            _cache_personaje = 0;   // murio o lo sacaron: a buscar de nuevo
        }
        Node* parent = get_parent(); // DataModel
        if (!parent) return nullptr;
        Node* ws = parent->get_node_or_null("Workspace");
        if (!ws) return nullptr;
        for (int i = 0; i < ws->get_child_count(); i++) {
            Node* child = ws->get_child(i);
            if (child && (child->is_class("RobloxPlayer") || child->is_class("RobloxPlayer2D"))) {
                _cache_personaje = (uint64_t)child->get_instance_id();
                return child;
            }
        }
        return nullptr;
    }

    // Crea/devuelve el objeto Player LOCAL (Player ≠ Character), hijo de Players.
    // Su .Character apunta al personaje; se sincroniza si el personaje respawnea.
    PlayerObject* ensure_local_player_obj() {
        if (!is_inside_tree()) return nullptr;
        PlayerObject* po = nullptr;
        for (int i = 0; i < get_child_count(); i++) {
            PlayerObject* c = gl_cast<PlayerObject>(get_child(i));
            if (c && c->is_local) { po = c; break; }
        }
        Node* character = _find_local_character();
        if (!po) {
            po = memnew(PlayerObject);
            po->is_local = true;
            po->peer_id = 0;
            String pname = "Player";
            // UserId secuencial e inmutable: lo fija el servidor (host=1, 2,3,...) vía
            // gl_local_user_id(). 0 = sin sesión de red → jugador 1 (single-player).
            int64_t uid = gl_local_user_id() > 0 ? gl_local_user_id() : 1;
            if (character) {
                Variant dn = character->get("DisplayName");
                if (dn.get_type() == Variant::STRING && !String(dn).is_empty()) pname = dn;
            }
            po->set_name(pname);
            po->user_id = uid;
            po->display_name = pname;
            add_child(po);
        }
        ensure_player_containers(po);   // PlayerGui/Backpack/PlayerScripts (1.14.10)
        if (character) po->set_character_silent(character);  // sin disparar (CharacterAdded se dispara tras PlayerAdded)
        return po;
    }

    // Player:LoadCharacter() (1.14.10): rehace el personaje LOCAL disparando
    // CharacterRemoving y luego CharacterAdded, como Roblox. La construcción la
    // hace el Workspace (build_local_character), que es quien sabe del spawn,
    // el rig y los StarterCharacterScripts.
    void load_local_character() {
        if (!is_inside_tree()) return;
        PlayerObject* po = ensure_local_player_obj();
        if (Node* old = _find_local_character()) {
            if (po) po->fire_character_removing();
            if (old->get_parent()) old->get_parent()->remove_child(old);
            old->queue_free();
        }
        Node* dm = get_parent();
        Node* ws = dm ? dm->get_node_or_null("Workspace") : nullptr;
        if (ws && ws->has_method("build_local_character")) ws->call("build_local_character");
        Node* fresh = _find_local_character();
        if (po && fresh) {
            po->set_character_silent(fresh);
            // Re-apuntar el Mouse al personaje NUEVO (por método, para no depender
            // del orden de includes con RobloxMouse).
            if (Node* mo = po->get_node_or_null(NodePath("Mouse")))
                if (mo->has_method("_gl_set_character")) mo->call("_gl_set_character", fresh);
            // El personaje es NUEVO: hay que reaplicarle los ajustes que vivían en
            // el Player, o se pierden al morir (LockFirstPerson, 1.15).
            if (fresh->has_method("set_lock_first_person"))
                fresh->call("set_lock_first_person", po->camera_mode == 1);
            po->fire_character_added();
        }
    }

    // Players.LocalPlayer → el OBJETO Player local (no el personaje).
    Node* get_local_player() { return ensure_local_player_obj(); }

    TypedArray<Node> get_players() {
        // Con sesión de red, el roster completo lo tiene el NetworkService
        // (jugador local + los demás peers). Sin red, solo el jugador local.
        if (is_inside_tree()) {
            Node* ns = _network_service();
            if (ns) {
                Variant rv = ns->call("get_players_roster");
                if (rv.get_type() == Variant::ARRAY) {
                    TypedArray<Node> out;
                    Array a = rv;
                    for (int i = 0; i < a.size(); i++) {
                        Node* n = gl_cast<Node>(a[i]);
                        if (n) out.push_back(n);
                    }
                    return out;
                }
            }
        }
        TypedArray<Node> result;
        Node* lp = get_local_player();
        if (lp) result.push_back(lp);
        return result;
    }

private:
    // ── El NetworkService, APUNTADO ───────────────────────────────────
    // Players:GetPlayers() lo buscaba recorriendo el ARBOL ENTERO y llamando
    // is_class() en cada nodo. En el place de prueba son ~60.000 nodos y el
    // servicio esta casi al final: **33 MILISEGUNDOS por llamada** (medido con
    // un micro-banco dentro del juego; lo siguiente mas caro eran 9 us).
    //
    // Un juego normal lo llama dentro de un bucle por frame — el place de
    // prueba lo hace en un Heartbeat, una vez por NPC — asi que una sola
    // llamada ya se comia el frame entero. Era la causa de los 30 FPS.
    //
    // Se guarda el ObjectID (no el puntero): si el servicio muere, ObjectDB
    // devuelve null y se vuelve a buscar, sin quedarse con basura.
    Node* _network_service() { return gl_servicio(this, "NetworkService"); }

    static Node* _find_network_service(Node* n) {
        if (!n) return nullptr;
        if (n->is_class("NetworkService")) return n;
        for (int i = 0; i < n->get_child_count(); i++) {
            Node* r = _find_network_service(n->get_child(i));
            if (r) return r;
        }
        return nullptr;
    }
    void _fire_player_event(std::vector<LuaCallback>& list, Node* player) {
        // :Once → desconectar ANTES de invocar (como Roblox). Se apagan aqui,
        // antes de correr ningun Lua, porque el manejador puede realojar `list`.
        std::vector<LuaCallback> snap = list;
        for (auto& cb : list) if (cb.once) cb.active = false;
        for (int i = (int)snap.size()-1; i >= 0; --i) {
            if (!snap[i].active || !gl_state_alive(snap[i].main_L)) continue;
            lua_State* mL = snap[i].main_L;
            lua_State* th = lua_newthread(mL);
            lua_rawgeti(mL, LUA_REGISTRYINDEX, snap[i].ref);
            if (lua_isfunction(mL, -1)) {
                lua_xmove(mL, th, 1);
                if (player) {
                    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_newuserdata(th, sizeof(GodotObjectWrapper));
                    gow_set(w, player);
                    luaL_getmetatable(th, "GodotObject");
                    lua_setmetatable(th, -2);
                } else {
                    lua_pushnil(th);
                }
                gl_resume_handler(th, 1);
            } else lua_pop(mL, 1);
            lua_pop(mL, 1);
        }
        for (int i = (int)list.size()-1; i >= 0; --i)
            if (!list[i].active || !gl_state_alive(list[i].main_L)) list.erase(list.begin()+i);
    }
};

// Team — a team players can belong to (child of the Teams service)
//// Team (1.14.10) — antes NO existía esta clase: Player.Team devolvía nil
//// siempre y Teams era un nodo vacío. Un Team es un nodo hijo de Teams y los
//// Player apuntan a él por ObjectID. Se replica solo: "Teams" es contenedor
//// replicable, así que un Team creado por el servidor aparece en los clientes.
class Team : public Node {
    GDCLASS(Team, Node);
public:
    // TeamColor se guarda por NOMBRE de BrickColor (1.15). Antes era un int suelto
    // que no casaba con nada: ni con el String del SpawnLocation ni con el Color
    // del RobloxPlayer, y no había forma de sacar el color para pintar UI. El
    // nombre es la identidad del BrickColor en Roblox y es con lo que se comparan
    // los SpawnLocation.
    String team_color = "Medium stone grey";
    bool auto_assignable = true;

    String get_team_color() const      { return team_color; }
    void   set_team_color(String v)    { team_color = v; }
    bool get_auto_assignable() const { return auto_assignable; }
    void set_auto_assignable(bool v) { auto_assignable = v; }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("get_team_color"),          &Team::get_team_color);
        ClassDB::bind_method(D_METHOD("set_team_color", "v"),     &Team::set_team_color);
        ClassDB::bind_method(D_METHOD("get_auto_assignable"),     &Team::get_auto_assignable);
        ClassDB::bind_method(D_METHOD("set_auto_assignable", "v"), &Team::set_auto_assignable);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "TeamColor"),    "set_team_color",      "get_team_color");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "AutoAssignable"), "set_auto_assignable", "get_auto_assignable");
    }
};

// Teams management — players can belong to colored teams
//// Gestión de equipos — los jugadores pueden pertenecer a equipos con colores
class Teams : public Node {
    GDCLASS(Teams, Node);
public:
    TypedArray<Node> get_teams() {
        TypedArray<Node> out;
        for (int i = 0; i < get_child_count(); i++)
            if (gl_cast<Team>(get_child(i))) out.push_back(get_child(i));
        return out;
    }
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("get_teams"), &Teams::get_teams);
    }
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_game_parent(this);
    }
};

// Folder is defined in folder.h (already existed before)
//// Folder está definido en folder.h (ya existía antes)

#include "gl_lighting.h"   // Lighting (aparte por tamaño)

// ════════════════════════════════════════════════════════════════════
//  AUDIO, MATERIALS, NETWORK
////  AUDIO, MATERIALES, RED
// ════════════════════════════════════════════════════════════════════

// Controls global volume, reverb, and world sound effects
//// Controla el volumen global, reverb, y efectos de sonido del mundo
class SoundService : public Node {
    GDCLASS(SoundService, Node);

private:
    float ambient_reverb = 0.0f;
    float doppler_scale  = 1.0f;
    float distance_factor = 1.0f;

protected:
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_game_parent(this);
    }
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_ambient_reverb", "v"), &SoundService::set_ambient_reverb);
        ClassDB::bind_method(D_METHOD("get_ambient_reverb"),       &SoundService::get_ambient_reverb);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "AmbientReverb",
                                  PROPERTY_HINT_RANGE, "0,10,0.1"),
                     "set_ambient_reverb", "get_ambient_reverb");
    }

public:
    void set_ambient_reverb(float v) { ambient_reverb = v; }
    float get_ambient_reverb() const { return ambient_reverb; }
};

// Manages world surface materials
//// Gestiona los materiales de superficie del mundo
class MaterialService : public Node {
    GDCLASS(MaterialService, Node);
protected:
    static void _bind_methods() {}
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_game_parent(this);
    }
};

// Network management for multiplayer
//// Gestión de la red para multijugador
class NetworkClient : public Node {
    GDCLASS(NetworkClient, Node);
protected:
    static void _bind_methods() {}
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_game_parent(this);
    }
};

// ════════════════════════════════════════════════════════════════════
//  TEXT CHAT SERVICE — Chat funcional tipo Roblox
// ════════════════════════════════════════════════════════════════════

#include "roblox_textchat.h"   // TextChatService (aparte por tamaño)

// ════════════════════════════════════════════════════════════════════
//  RUN SERVICE — Motor de eventos por frame (equivalente 100% a Roblox)
//  Uso desde Luau:
//    local RS = game:GetService("RunService")
//    RS.Heartbeat:Connect(function(dt) ... end)
//    RS.RenderStepped:Connect(function(dt) ... end)
//    RS.Stepped:Connect(function(dt) ... end)
//    RS:IsRunning()  RS:IsClient()  RS:IsServer()
// ════════════════════════════════════════════════════════════════════
class RunService : public Node {
    GDCLASS(RunService, Node);

public:
    struct LuaCallback {
        lua_State* main_L;
        int        ref;
        bool       active = true;
        bool       once   = false;
    };

    // Coroutines waiting on the next signal fire (Signal:Wait())
    struct WaitCallback {
        lua_State* thread;
        lua_State* main_L;
    };

    std::vector<LuaCallback> heartbeat_cbs;
    std::vector<LuaCallback> render_stepped_cbs;
    std::vector<LuaCallback> stepped_cbs;

    std::vector<WaitCallback> heartbeat_wait_cbs;
    std::vector<WaitCallback> render_stepped_wait_cbs;
    std::vector<WaitCallback> stepped_wait_cbs;

    void add_heartbeat(lua_State* main_L, int ref, bool once = false)      { heartbeat_cbs.push_back({main_L, ref, true, once}); }
    void add_render_stepped(lua_State* main_L, int ref, bool once = false) { render_stepped_cbs.push_back({main_L, ref, true, once}); }
    void add_stepped(lua_State* main_L, int ref, bool once = false)        { stepped_cbs.push_back({main_L, ref, true, once}); }

    void add_heartbeat_wait(lua_State* th, lua_State* mL)      { heartbeat_wait_cbs.push_back({th, mL}); }
    void add_render_stepped_wait(lua_State* th, lua_State* mL) { render_stepped_wait_cbs.push_back({th, mL}); }
    void add_stepped_wait(lua_State* th, lua_State* mL)        { stepped_wait_cbs.push_back({th, mL}); }

    // ── BindToRenderStep ─────────────────────────────────────────────
    // Funciones con nombre que corren cada frame ANTES de RenderStepped, de
    // menor a mayor prioridad (Enum.RenderPriority: Input 100, Camera 200,
    // Character 300). No existia: la camara o el input propios de un juego
    // (BindToRenderStep("Camera", ...)) morian en esa linea.
    struct RenderBind { String nombre; int prioridad; lua_State* main_L; int ref; };
    std::vector<RenderBind> render_binds;

    void bind_render_step(const String& nombre, int prioridad, lua_State* mL, int ref) {
        // stable_sort al disparar mantiene el orden de llegada a igual prioridad.
        render_binds.push_back({ nombre, prioridad, mL, ref });
    }
    void unbind_render_step(const String& nombre) {
        for (size_t i = 0; i < render_binds.size();) {
            if (render_binds[i].nombre == nombre) {
                if (gl_state_alive(render_binds[i].main_L)) gl_unref(render_binds[i].main_L, render_binds[i].ref);
                render_binds.erase(render_binds.begin() + i);
            } else i++;
        }
    }
    void fire_render_binds(double delta) {
        if (render_binds.empty()) return;
        std::vector<RenderBind> orden = render_binds;   // un manejador puede (des)atar
        std::stable_sort(orden.begin(), orden.end(),
            [](const RenderBind& a, const RenderBind& b) { return a.prioridad < b.prioridad; });
        for (const RenderBind& b : orden) {
            // ¿Sigue atado? Otro manejador de este mismo frame pudo soltarlo.
            bool sigue = false;
            for (const RenderBind& r : render_binds)
                if (r.ref == b.ref && r.main_L == b.main_L) { sigue = true; break; }
            if (!sigue || !gl_state_alive(b.main_L)) continue;
            lua_State* th = lua_newthread(b.main_L);
            lua_rawgeti(b.main_L, LUA_REGISTRYINDEX, b.ref);
            if (lua_isfunction(b.main_L, -1)) {
                lua_xmove(b.main_L, th, 1);
                lua_pushnumber(th, delta);
                gl_resume_handler(th, 1);
            } else lua_pop(b.main_L, 1);
            lua_pop(b.main_L, 1);
        }
    }

    void remove_by_state(lua_State* L) {
        render_binds.erase(std::remove_if(render_binds.begin(), render_binds.end(),
            [L](const RenderBind& b){ return b.main_L == L; }), render_binds.end());
        for (auto& cb : heartbeat_cbs)      if (cb.main_L == L) cb.active = false;
        for (auto& cb : render_stepped_cbs) if (cb.main_L == L) cb.active = false;
        for (auto& cb : stepped_cbs)        if (cb.main_L == L) cb.active = false;
        heartbeat_wait_cbs.erase(std::remove_if(heartbeat_wait_cbs.begin(), heartbeat_wait_cbs.end(),
            [L](const WaitCallback& w){ return w.main_L == L; }), heartbeat_wait_cbs.end());
        render_stepped_wait_cbs.erase(std::remove_if(render_stepped_wait_cbs.begin(), render_stepped_wait_cbs.end(),
            [L](const WaitCallback& w){ return w.main_L == L; }), render_stepped_wait_cbs.end());
        stepped_wait_cbs.erase(std::remove_if(stepped_wait_cbs.begin(), stepped_wait_cbs.end(),
            [L](const WaitCallback& w){ return w.main_L == L; }), stepped_wait_cbs.end());
    }

    // OJO: hace falta el PAR (VM, ref), no solo el ref. Los refs de lua_ref son
    // enteros pequenos POR VM (cada script es una VM): el 4 de un script y el 4
    // de otro son connections distintas. Antes se desactivaba "el primero que
    // tenga ese numero", asi que cuando la pantalla de carga desconectaba su
    // Heartbeat al terminar, mataba el Heartbeat de OTRO script (medido en el
    // place: los NPC del camino andaban ~1 s y se congelaban para siempre, sin
    // ningun error).
    void disconnect_by_ref(const char* ev, int ref, lua_State* main_L) {
        auto deactivate = [ref, main_L](std::vector<LuaCallback>& list) {
            for (auto& cb : list)
                if (cb.ref == ref && cb.main_L == main_L) { cb.active = false; return; }
        };
        if      (strcmp(ev, "Heartbeat")     == 0) deactivate(heartbeat_cbs);
        else if (strcmp(ev, "RenderStepped") == 0) deactivate(render_stepped_cbs);
        else if (strcmp(ev, "Stepped")       == 0) deactivate(stepped_cbs);
    }

protected:
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_game_parent(this);
    }
    static void _bind_methods() {}

    // ── ORDEN DE DISPARO ──────────────────────────────────────────────
    // Roblox llama a los conectados EN EL ORDEN EN QUE SE CONECTARON. Esto
    // recorria la lista AL REVES (del ultimo al primero), asi que dos scripts
    // que se apuntan al mismo evento corrian en orden inverso al de Roblox — y
    // en un juego donde uno prepara el estado y el otro lo lee, eso es la
    // diferencia entre funcionar y no.
    //
    // De paso se deja de borrar por el medio del vector (cada erase mueve toda
    // la cola): se compacta in situ, como el tick de constraints.
    //
    // `arg2` solo lo usa Stepped, que en Roblox recibe DOS numeros.
    static void fire_list(std::vector<LuaCallback>& list, double delta,
                          bool dos_args = false, double arg2 = 0.0,
                          const char* evento = "?") {
        if (list.empty()) return;
        size_t escribe = 0;
        const size_t n = list.size();
        for (size_t i = 0; i < n; i++) {
            LuaCallback cb = list[i];             // copia: el callback puede tocar la lista
            if (!cb.active || !gl_state_alive(cb.main_L)) continue;
            if (cb.once) list[i].active = false;
            else         list[escribe++] = cb;
            lua_rawgeti(cb.main_L, LUA_REGISTRYINDEX, cb.ref);
            if (!lua_isfunction(cb.main_L, -1)) { lua_pop(cb.main_L, 1); continue; }
            // ¿QUIEN es este callback? Con la autopsia puesta se le pregunta a
            // Luau por el archivo y la linea donde se definio la funcion, para
            // poder decir "este Heartbeat de este script cuesta 12 ms" en vez
            // de solo "RunService cuesta 57 ms", que no arregla nada.
            // Nivel NEGATIVO = "la funcion que hay en ese hueco de la pila".
            String quien;
            uint64_t t_cb = 0;
            if (gl_aut_on()) {
                lua_Debug ar;
                if (lua_getinfo(cb.main_L, -1, "s", &ar))
                    quien = String(evento) + " " + String(ar.short_src) + ":"
                          + String::num_int64(ar.linedefined);
                else
                    quien = String(evento) + " (sin datos)";
                t_cb = Time::get_singleton()->get_ticks_usec();
            }
            // El hilo se crea DESPUES de saber que hay funcion: si el ref murio,
            // antes se fabricaba un hilo para nada y se lo comia el recolector.
            lua_State* thread = lua_newthread(cb.main_L);
            lua_insert(cb.main_L, -2);            // [thread][fn] -> [fn][thread]...
            lua_xmove(cb.main_L, thread, 1);      // mueve la funcion al hilo
            int nargs = 1;
            if (dos_args) { lua_pushnumber(thread, arg2); nargs = 2; }
            lua_pushnumber(thread, delta);
            gl_resume_handler(thread, nargs);
            if (t_cb) gl_aut_cb_anota(quien, Time::get_singleton()->get_ticks_usec() - t_cb);
            lua_pop(cb.main_L, 1);                // saca el hilo
        }
        // Los que se apuntaron DURANTE el disparo van al final, sin perderse.
        for (size_t i = n; i < list.size(); i++) list[escribe++] = list[i];
        list.resize(escribe);
    }

    // Queue Signal:Wait() coroutines into the shared pending-resume list
    static void fire_wait_list(std::vector<WaitCallback>& list, double delta,
                               bool dos_args = false, double arg2 = 0.0) {
        if (list.empty()) return;
        for (size_t i = 0; i < list.size(); i++) {
            const WaitCallback& wc = list[i];
            if (!gl_state_alive(wc.main_L)) continue;
            LuauPendingResume pr;
            pr.thread = wc.thread;
            pr.main_L = wc.main_L;
            pr.delta = delta;
            pr.node_arg = nullptr;
            pr.dos_valores = dos_args;
            pr.delta2 = arg2;
            get_pending_resumes().push_back(pr);
        }
        list.clear();
    }

    // Tiempo que lleva corriendo el juego. Es el PRIMER argumento de Stepped en
    // Roblox (el segundo es el delta), y sin el `Stepped:Connect(function(t, dt)`
    // recibia el delta en `t` y nil en `dt`.
    double tiempo_juego = 0.0;
    double   _aut_acc    = 0.0;   // solo para el volcado de la autopsia
    uint32_t _aut_frames = 0;

public:
    // ── ORDEN DEL FRAME EN ROBLOX ─────────────────────────────────────
    //   RenderStepped -> Stepped -> (fisica) -> Heartbeat
    // Godot llama _physics_process ANTES de pisar el solver y _process despues
    // del paso, asi que Stepped en la fisica y Heartbeat en el frame ya caen
    // donde toca. Lo que estaba al reves era RenderStepped, que se disparaba
    // DESPUES de Heartbeat; en Roblox es el primero de todos.
    void _process(double delta) override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        GL_AUT_SCOPE(AUT_P_RUN);
        // Hover central de la GUI: un solo barrido por frame sobre los
        // elementos que tienen MouseEnter/MouseLeave conectados (el resto ni
        // se mira). Va aqui porque RunService es el unico nodo que con
        // seguridad late cada frame en cualquier juego.
        Time* T = Time::get_singleton();
        GLPasoStats& ps = gl_paso_stats();
        // Puede haber MAS DE UN RunService en el arbol (el del place importado y
        // el de la plantilla). Cada uno tiene sus propias listas, asi que las
        // cifras se SUMAN dentro del mismo frame en vez de asignarse — con "="
        // el ultimo en correr pisaba al anterior y salia 0 aunque hubiera
        // callbacks disparando (medido).
        const uint64_t frame = Engine::get_singleton()->get_process_frames();
        if (ps.frame_proc != frame) {
            ps.frame_proc = frame;
            ps.hb_us = 0; ps.rs_us = 0; ps.hover_us = 0; ps.hb_n = 0; ps.rs_n = 0;
        }
        uint64_t t0 = T->get_ticks_usec();
        { GL_AUT_SCOPE(AUT_P_HOVER); gl_hover_tick(); }
        ps.hover_us += T->get_ticks_usec() - t0;

        t0 = T->get_ticks_usec();
        {
            GL_AUT_SCOPE(AUT_P_RENDER);
            fire_render_binds(delta);
            fire_list(render_stepped_cbs, delta, false, 0.0, "RenderStepped");
            fire_wait_list(render_stepped_wait_cbs, delta);
        }
        ps.rs_us += T->get_ticks_usec() - t0;

        t0 = T->get_ticks_usec();
        {
            GL_AUT_SCOPE(AUT_P_HEART);
            fire_list(heartbeat_cbs, delta, false, 0.0, "Heartbeat");
            fire_wait_list(heartbeat_wait_cbs, delta);
        }
        ps.hb_us += T->get_ticks_usec() - t0;

        // Cuanta memoria tiene Luau ahora mismo, sumando todas las VM. Va aqui
        // porque RunService es el unico nodo que late seguro cada frame. Si un
        // frame largo coincide con una BAJADA de este numero, ese frame se lo
        // comio el recolector de basura.
        if (gl_aut_on()) {
            gl_aut().lua_kb  = gl_lua_memoria_kb();
            gl_aut().lua_vms = gl_lua_vms();
        }

        for (const auto& c : render_stepped_cbs) if (c.active) ps.rs_n++;
        for (const auto& c : heartbeat_cbs)      if (c.active) ps.hb_n++;
        // Cada pocos segundos se sacan los callbacks mas caros.
        if (gl_aut_on()) {
            _aut_frames++;
            _aut_acc += delta;
            if (_aut_acc >= 4.0) { gl_aut_cb_volcar(_aut_frames); _aut_acc = 0.0; _aut_frames = 0; }
        }
        // Para la autopsia: el "xN" de RunService son los CALLBACKS del juego
        // que laten cada frame, no los nodos RunService (que son 2).
        if (gl_aut_on()) {
            uint32_t vivos = 0;
            for (const auto& c : render_stepped_cbs) if (c.active) vivos++;
            for (const auto& c : heartbeat_cbs)      if (c.active) vivos++;
            gl_aut().n[AUT_P_RUN] += vivos;
        }
    }

    void _physics_process(double delta) override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        tiempo_juego += delta;
        Time* T = Time::get_singleton();
        const uint64_t t0 = T->get_ticks_usec();
        fire_list(stepped_cbs, delta, true, tiempo_juego, "Stepped");
        fire_wait_list(stepped_wait_cbs, delta, true, tiempo_juego);
        GLPasoStats& ps = gl_paso_stats();
        const uint64_t frame = Engine::get_singleton()->get_physics_frames();
        if (ps.frame_phys != frame) { ps.frame_phys = frame; ps.st_us = 0; ps.st_n = 0; }
        ps.st_us += T->get_ticks_usec() - t0;
        for (const auto& c : stepped_cbs) if (c.active) ps.st_n++;
    }
};

#endif // ROBLOX_SERVICES_H

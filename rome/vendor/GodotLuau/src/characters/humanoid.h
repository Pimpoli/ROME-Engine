#ifndef HUMANOID_H
#define HUMANOID_H

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/kinematic_collision3d.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/sprite3d.hpp>
#include <godot_cpp/classes/sprite_base3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <vector>

#include "lua.h"
#include "lualib.h"
#include "gl_errors.h"
#include "gl_runtime.h"   // gl_state_alive
#include "gl_physlod.h"   // nivel de detalle de la fisica (paso adaptativo)
#include "gl_servicios.h" // gl_en_workspace: solo se dibuja lo del mundo
#include "roblox_part.h"  // NPC: el conjunto de su HumanoidRootPart; asientos
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>

using namespace godot;

// ════════════════════════════════════════════════════════════════════
//  Humanoid — Movement and health system for 3D characters
//
//  Humanoid is the character's "engine". It handles:
//  - Movement physics (WASD + camera)
//  - Health and damage
//  - Signals (Died, HealthChanged)
//  - Collision detection with RobloxParts (Touched event)
//
//  Properties editable from Luau:
//    humanoid.WalkSpeed  = 20    -- Normal speed
//    humanoid.JumpPower  = 20    -- Jump force
//    humanoid.Health     = 100   -- Current health
////
//  Humanoid — Sistema de movimiento y salud del personaje 3D
//
//  El Humanoid es el "motor" del personaje. Maneja:
//  - Física de movimiento (WASD + cámara)
//  - Salud y daño
//  - Señales (Died, HealthChanged)
//  - Detección de colisiones con RobloxParts (evento Touched)
//
//  Propiedades modificables desde Luau:
//    humanoid.WalkSpeed  = 20    -- Velocidad normal
//    humanoid.JumpPower  = 20    -- Fuerza del salto
//    humanoid.Health     = 100   -- Salud actual
// ════════════════════════════════════════════════════════════════════
class Humanoid : public Node {
    GDCLASS(Humanoid, Node);

public:
    // Callback for Luau signals (Died, HealthChanged)
    //// Callback para señales Luau (Died, HealthChanged)
    struct LuaCallback {
        lua_State* main_L;
        int ref;
        bool active = true;
    };

    std::vector<LuaCallback> died_cbs;
    std::vector<LuaCallback> health_changed_cbs;
    std::vector<LuaCallback> state_changed_cbs;
    // Senales de movimiento de Roblox. Se disparan desde _physics_process con el
    // estado real del cuerpo, asi que Animate.lua (el script de animacion que
    // trae cada NPC de un place importado) funciona tal cual.
    std::vector<LuaCallback> running_cbs;
    std::vector<LuaCallback> jumping_cbs;
    std::vector<LuaCallback> freefalling_cbs;
    std::vector<LuaCallback> climbing_cbs;
    std::vector<LuaCallback> swimming_cbs;
    std::vector<LuaCallback> gettingup_cbs;
    std::vector<LuaCallback> seated_cbs;
    std::vector<LuaCallback> moveto_cbs;
    // Senales que Roblox tiene y aqui no tienen disparador propio todavia: se
    // aceptan y guardan la conexion para que el script no muera (es lo que hace
    // que Animate.lua llegue hasta el final y los NPC se animen).
    std::vector<LuaCallback> other_move_cbs;

    // Estado de animación REAL de este personaje (1.14.17). Lo lee el
    // NetworkService para replicarlo tal cual, en vez de que cada máquina lo
    // deduzca del movimiento del muñeco (que hacía durar de más los saltos).
    float anim_speed01  = 0.0f;
    bool  anim_airborne = false;

    void add_died_callback(lua_State* main_L, int ref) {
        died_cbs.push_back({main_L, ref, true});
    }
    void add_health_changed_callback(lua_State* main_L, int ref) {
        health_changed_cbs.push_back({main_L, ref, true});
    }
    void add_state_changed_callback(lua_State* main_L, int ref) {
        state_changed_cbs.push_back({main_L, ref, true});
    }
    // add_move_signal_callback(which, ...) — 0=Running 1=Jumping 2=FreeFalling
    // 3=Climbing 4=Swimming 5=GettingUp 6=Seated 7=MoveToFinished
    std::vector<LuaCallback>* move_signal_list(int which) {
        switch (which) {
            case 0: return &running_cbs;
            case 1: return &jumping_cbs;
            case 2: return &freefalling_cbs;
            case 3: return &climbing_cbs;
            case 4: return &swimming_cbs;
            case 5: return &gettingup_cbs;
            case 6: return &seated_cbs;
            case 7: return &moveto_cbs;
            default: return &other_move_cbs;
        }
    }
    void add_move_signal_callback(int which, lua_State* main_L, int ref) {
        move_signal_list(which)->push_back({main_L, ref, true});
    }
    void remove_callbacks_for_state(lua_State* L) {
        for (auto& cb : died_cbs)           if (cb.main_L == L) cb.active = false;
        for (auto& cb : health_changed_cbs) if (cb.main_L == L) cb.active = false;
        for (auto& cb : state_changed_cbs)  if (cb.main_L == L) cb.active = false;
        for (int w = 0; w <= 13; w++)
            for (auto& cb : *move_signal_list(w)) if (cb.main_L == L) cb.active = false;
    }
    void _gl_disconnect(int ref, int64_t vm) {
        for (auto& cb : died_cbs)           if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
        for (auto& cb : health_changed_cbs) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
        for (auto& cb : state_changed_cbs)  if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
        for (int w = 0; w <= 13; w++)
            for (auto& cb : *move_signal_list(w)) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
    }

    static void fire_lua_died(std::vector<LuaCallback>& cbs) {
        gl_fire_cbs(cbs);
    }
    // Como Roblox: StateChanged(viejo, nuevo), los dos EnumItem. Antes iban dos
    // numeros y al reves (nuevo, viejo): `function(old, new)` los recibia
    // cambiados y `new == Enum.HumanoidStateType.Jumping` no daba nunca true.
    static void fire_lua_state_changed(std::vector<LuaCallback>& cbs, int new_state, int old_state) {
        gl_fire_cbs(cbs, [&](lua_State* th) {
            gl_push_enum_item(th, "HumanoidStateType", old_state);
            gl_push_enum_item(th, "HumanoidStateType", new_state);
            return 2;
        });
    }

    // Dispara una senal de movimiento con un solo argumento (velocidad para
    // Running, booleano para Jumping/Climbing/...).
    static void fire_lua_one(std::vector<LuaCallback>& cbs, const Variant& arg) {
        gl_fire_cbs(cbs, [&](lua_State* th) {
            if (arg.get_type() == Variant::BOOL) lua_pushboolean(th, (bool)arg);
            else                                 lua_pushnumber(th, (double)arg);
            return 1;
        });
    }

    static void fire_lua_health_changed(std::vector<LuaCallback>& cbs, float new_hp, float old_hp) {
        gl_fire_cbs(cbs, [&](lua_State* th) {
            lua_pushnumber(th, new_hp);
            lua_pushnumber(th, old_hp);
            return 2;
        });
    }

private:
    float  health                 = 100.0f;
    float  max_health             = 100.0f;
    float  walk_speed             = 16.0f;
    float  jump_power             = 50.0f;    // Roblox: 50
    float  jump_height            = 7.2f;     // Roblox: 7.2 (modo alternativo)
    float  hip_height             = 0.0f;
    // Humanoid.UseJumpPower de Roblox: true = manda JumpPower, false = JumpHeight.
    bool   use_jump_power         = true;
    // Humanoid.Jump = true por script (la forma canonica de saltar en Roblox:
    // la usan los Animate/ControlModule portados). Se consume al ejecutarse.
    bool   jump_requested         = false;
    // Roblox: 89 grados. Estaba clavado a 50 en el codigo, asi que habia rampas
    // que en Roblox se suben y aqui te frenaban.
    float  max_slope_angle        = 89.0f;
    // ── GRAVEDAD ──────────────────────────────────────────────────────
    // En Roblox el personaje cae con Workspace.Gravity (196.2 studs/s^2) como
    // cualquier otra pieza; el Humanoid NO tiene gravedad propia. Aqui la tenia,
    // y ademas valia 45: caiamos 4,36 veces mas lento que en Roblox, y bajar
    // Workspace.Gravity movia las cajas pero no al jugador.
    //
    // -1 = usar la del Workspace (lo normal, y lo que hace Roblox). Cualquier
    // otro valor es un override de esta instancia; se conserva porque la
    // propiedad ya estaba expuesta a Luau y puede haber scripts usandola.
    float  gravity                = -1.0f;
    bool   is_dead                = false;
    bool   auto_rotate            = true;
    // Suavidad del giro del cuerpo hacia la direccion de movimiento (AutoRotate).
    // Mayor = gira mas rapido; menor = mas suave. Independiente de los FPS.
    // v1.8.3: bajado de 9 a 5 (giro claramente mas suave, no brusco).
    float  turn_speed             = 11.0f;   // giro rapido hacia el movimiento, como Roblox
    bool   auto_jump_enabled      = true;
    float  name_display_distance  = 100.0f;
    float  health_display_distance= 100.0f;
    // Enum.HumanoidDisplayDistanceType: 0=Viewer, 1=Subject, 2=None.
    // None apaga el cartel del nombre; es lo que usan los juegos para que los
    // NPC no lleven su nombre flotando encima.
    int    display_distance_type  = 0;
    // Enum.HumanoidHealthDisplayType: 0=DisplayWhenDamaged, 1=AlwaysOn, 2=AlwaysOff
    int    health_display_type    = 0;
    // 0 = R6, 1 = R15
    int    rig_type               = 1;
    bool   evaluate_state_machine = true;
    bool   break_joints_on_death  = true;
    bool   requires_neck          = true;
    String display_name;

    // ── Display overhead (nombre + barra de vida), como Roblox ──────────
    Node3D*             _overhead    = nullptr;
    Label3D*            _name_label  = nullptr;
    Sprite3D*           _hp_sprite   = nullptr;

    // ── HumanoidStateType (mirror of Enum.HumanoidStateType in Luau) ─
    //// ── HumanoidStateType (espejo de Enum.HumanoidStateType en Luau) ─
    // 0=None 1=Running 2=Jumping 3=Landed 4=Swimming 5=Climbing 6=Seated
    // 7=Dead 8=Freefall 9=Flying 10=GettingUp 11=FallingDown 12=Ragdoll
    // 13=StrafingNoWalk 14=Frozen
    int     current_state = 1;
    int     forced_state  = -1;   // -1 = automatic / automático
    bool    was_on_floor  = true;
    float   landed_timer  = 0.0f;
    bool    body_configured = false;   // config del CharacterBody3D una sola vez
    Vector3 move_direction;
    Vector3 npc_move_dir;         // Non-zero = NPC/scripted movement, overrides player input

    // ── MoveTo / Move: lo que hace andar a un Humanoid por script ──────────
    bool     moveto_activo = false;
    Vector3  moveto_destino;         // en mundo, o relativo a la pieza si la hay
    uint64_t moveto_pieza  = 0;      // MoveTo(pos, pieza): sigue a la pieza
    double   moveto_tiempo = 0.0;    // Roblox lo da por fallido a los 8 s
    Vector3  walk_dir;               // Humanoid:Move(dir): se mantiene hasta otra orden
    int      _moveto_fin   = 0;      // 1 llego, 2 se rindio: se avisa en _process

    // ── Asiento (Seat / VehicleSeat) ──────────────────────────────────────
    uint64_t asiento_id     = 0;     // SeatPart
    uint64_t _asiento_aviso = 0;     // el asiento cuyo Occupant cambio
    int      _sentado_aviso = 0;     // 1 se sento, 2 se levanto
    int      _vehiculo_aviso = 0;    // bit 1 Throttle, bit 2 Steer
    double   sentar_espera  = 0.0;   // tras levantarse, un rato sin volver a sentarse
    bool     platform_stand = false;

    // ── Empujes de fuera (BodyVelocity/LinearVelocity colgados de la HRP) ──
    // Mandan sobre el andar en los ejes que piden mientras sigan ahi: caducan
    // solos al paso siguiente si nadie los renueva.
    Vector3  ext_vel;
    Vector3  ext_ejes;
    uint64_t ext_frame = 0;

    // NPC: el cuerpo que se mantiene de pie (ejes de giro X/Z bloqueados).
    uint64_t _npc_raiz = 0;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"), &Humanoid::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("set_health", "h"),       &Humanoid::set_health);
        ClassDB::bind_method(D_METHOD("get_health"),             &Humanoid::get_health);
        ClassDB::bind_method(D_METHOD("set_max_health", "h"),   &Humanoid::set_max_health);
        ClassDB::bind_method(D_METHOD("get_max_health"),         &Humanoid::get_max_health);
        ClassDB::bind_method(D_METHOD("set_walk_speed", "s"),   &Humanoid::set_walk_speed);
        ClassDB::bind_method(D_METHOD("get_walk_speed"),         &Humanoid::get_walk_speed);
        ClassDB::bind_method(D_METHOD("set_jump_power", "p"),   &Humanoid::set_jump_power);
        ClassDB::bind_method(D_METHOD("get_jump_power"),         &Humanoid::get_jump_power);
        ClassDB::bind_method(D_METHOD("set_gravity_val", "g"),  &Humanoid::set_gravity_val);
        ClassDB::bind_method(D_METHOD("get_gravity_val"),        &Humanoid::get_gravity_val);
        ClassDB::bind_method(D_METHOD("take_damage", "amount"), &Humanoid::take_damage);
        ClassDB::bind_method(D_METHOD("heal", "amount"),         &Humanoid::heal);

        ADD_GROUP("Salud", "");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Health",
                                  PROPERTY_HINT_RANGE, "0,1000,0.1"),
                     "set_health", "get_health");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "MaxHealth",
                                  PROPERTY_HINT_RANGE, "1,1000,1"),
                     "set_max_health", "get_max_health");

        ClassDB::bind_method(D_METHOD("set_jump_height","h"),  &Humanoid::set_jump_height);
        ClassDB::bind_method(D_METHOD("get_jump_height"),      &Humanoid::get_jump_height);
        ClassDB::bind_method(D_METHOD("set_hip_height","h"),   &Humanoid::set_hip_height);
        ClassDB::bind_method(D_METHOD("get_hip_height"),       &Humanoid::get_hip_height);

        ADD_GROUP("Movimiento", "");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "WalkSpeed",
                                  PROPERTY_HINT_RANGE, "0,100,0.1"),
                     "set_walk_speed", "get_walk_speed");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "JumpPower",
                                  PROPERTY_HINT_RANGE, "0,200,0.1"),
                     "set_jump_power", "get_jump_power");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "JumpHeight",
                                  PROPERTY_HINT_RANGE, "0,100,0.1"),
                     "set_jump_height", "get_jump_height");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "HipHeight",
                                  PROPERTY_HINT_RANGE, "0,10,0.01"),
                     "set_hip_height", "get_hip_height");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Gravity",
                                  PROPERTY_HINT_RANGE, "-1,1000,0.1"),
                     "set_gravity_val", "get_gravity_val");
        ClassDB::bind_method(D_METHOD("set_use_jump_power","b"), &Humanoid::set_use_jump_power);
        ClassDB::bind_method(D_METHOD("get_use_jump_power"),     &Humanoid::get_use_jump_power);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "UseJumpPower"),
                     "set_use_jump_power", "get_use_jump_power");
        ClassDB::bind_method(D_METHOD("set_max_slope_angle","a"), &Humanoid::set_max_slope_angle);
        ClassDB::bind_method(D_METHOD("get_max_slope_angle"),     &Humanoid::get_max_slope_angle);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "MaxSlopeAngle",
                                  PROPERTY_HINT_RANGE, "0,89,0.1"),
                     "set_max_slope_angle", "get_max_slope_angle");

        ADD_GROUP("Personaje", "");
        ClassDB::bind_method(D_METHOD("set_auto_rotate","b"),              &Humanoid::set_auto_rotate);
        ClassDB::bind_method(D_METHOD("get_auto_rotate"),                  &Humanoid::get_auto_rotate);
        ClassDB::bind_method(D_METHOD("set_auto_jump_enabled","b"),        &Humanoid::set_auto_jump_enabled);
        ClassDB::bind_method(D_METHOD("get_auto_jump_enabled"),            &Humanoid::get_auto_jump_enabled);
        ClassDB::bind_method(D_METHOD("set_display_name","n"),             &Humanoid::set_display_name);
        ClassDB::bind_method(D_METHOD("get_display_name"),                 &Humanoid::get_display_name);
        ClassDB::bind_method(D_METHOD("set_name_display_distance","d"),    &Humanoid::set_name_display_distance);
        ClassDB::bind_method(D_METHOD("get_name_display_distance"),        &Humanoid::get_name_display_distance);
        ClassDB::bind_method(D_METHOD("set_health_display_distance","d"),  &Humanoid::set_health_display_distance);
        ClassDB::bind_method(D_METHOD("get_health_display_distance"),      &Humanoid::get_health_display_distance);
        ClassDB::bind_method(D_METHOD("set_display_distance_type","t"),    &Humanoid::set_display_distance_type);
        ClassDB::bind_method(D_METHOD("get_display_distance_type"),        &Humanoid::get_display_distance_type);
        ClassDB::bind_method(D_METHOD("set_health_display_type","t"),      &Humanoid::set_health_display_type);
        ClassDB::bind_method(D_METHOD("get_health_display_type"),          &Humanoid::get_health_display_type);
        ClassDB::bind_method(D_METHOD("set_rig_type","t"),                 &Humanoid::set_rig_type);
        ClassDB::bind_method(D_METHOD("get_rig_type"),                     &Humanoid::get_rig_type);
        ClassDB::bind_method(D_METHOD("set_evaluate_state_machine","b"),   &Humanoid::set_evaluate_state_machine);
        ClassDB::bind_method(D_METHOD("get_evaluate_state_machine"),       &Humanoid::get_evaluate_state_machine);
        ClassDB::bind_method(D_METHOD("set_break_joints_on_death","b"),    &Humanoid::set_break_joints_on_death);
        ClassDB::bind_method(D_METHOD("get_break_joints_on_death"),        &Humanoid::get_break_joints_on_death);
        ClassDB::bind_method(D_METHOD("set_requires_neck","b"),            &Humanoid::set_requires_neck);
        ClassDB::bind_method(D_METHOD("get_requires_neck"),                &Humanoid::get_requires_neck);

        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "AutoRotate"),            "set_auto_rotate","get_auto_rotate");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "AutoJumpEnabled"),       "set_auto_jump_enabled","get_auto_jump_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::STRING,"DisplayName"),           "set_display_name","get_display_name");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "NameDisplayDistance",    PROPERTY_HINT_RANGE,"0,1000,1"), "set_name_display_distance","get_name_display_distance");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "HealthDisplayDistance",  PROPERTY_HINT_RANGE,"0,1000,1"), "set_health_display_distance","get_health_display_distance");
        ADD_PROPERTY(PropertyInfo(Variant::INT,   "DisplayDistanceType",    PROPERTY_HINT_ENUM, "Viewer,Subject,None"), "set_display_distance_type","get_display_distance_type");
        ADD_PROPERTY(PropertyInfo(Variant::INT,   "HealthDisplayType",      PROPERTY_HINT_ENUM, "DisplayWhenDamaged,AlwaysOn,AlwaysOff"), "set_health_display_type","get_health_display_type");
        ADD_PROPERTY(PropertyInfo(Variant::INT,   "RigType",                PROPERTY_HINT_ENUM,"R6:0,R15:1"),"set_rig_type","get_rig_type");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "EvaluateStateMachine"),  "set_evaluate_state_machine","get_evaluate_state_machine");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "BreakJointsOnDeath"),    "set_break_joints_on_death","get_break_joints_on_death");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "RequiresNeck"),          "set_requires_neck","get_requires_neck");

        ClassDB::bind_method(D_METHOD("get_state"),              &Humanoid::get_state);
        ClassDB::bind_method(D_METHOD("change_state","s"),       &Humanoid::change_state);
        ClassDB::bind_method(D_METHOD("get_move_direction"),     &Humanoid::get_move_direction);
        ClassDB::bind_method(D_METHOD("set_npc_move_dir","dir"), &Humanoid::set_npc_move_dir);
        ClassDB::bind_method(D_METHOD("get_npc_move_dir"),       &Humanoid::get_npc_move_dir);
        ClassDB::bind_method(D_METHOD("stop_npc_movement"),      &Humanoid::stop_npc_movement);
        ClassDB::bind_method(D_METHOD("gl_forzar_velocidad", "v", "ejes"), &Humanoid::gl_forzar_velocidad);
        ClassDB::bind_method(D_METHOD("gl_sentar", "asiento"),   &Humanoid::gl_sentar);
        ClassDB::bind_method(D_METHOD("gl_levantar", "saltar"),  &Humanoid::gl_levantar);

        // Roblox-style signals
        //// Señales tipo Roblox
        ADD_SIGNAL(MethodInfo("Died"));
        ADD_SIGNAL(MethodInfo("HealthChanged",
            PropertyInfo(Variant::FLOAT, "new_health"),
            PropertyInfo(Variant::FLOAT, "old_health")));
        ADD_SIGNAL(MethodInfo("StateChanged",
            PropertyInfo(Variant::INT, "new_state"),
            PropertyInfo(Variant::INT, "old_state")));
    }

public:
    // ── Health ─────────────────────────────────────────────────────
    //// ── Salud ──────────────────────────────────────────────────────
    // Humanoid:SetStateEnabled: estados apagados (numeracion del stdlib).
    // Jumping (2) apagado no deja saltar; Dead (7) apagado no deja morir.
    uint32_t estados_apagados = 0;
    void gl_set_state_enabled(int st, bool on) {
        if (st < 0 || st > 31) return;
        if (on) estados_apagados &= ~(1u << st); else estados_apagados |= (1u << st);
    }
    bool gl_get_state_enabled(int st) const {
        return st < 0 || st > 31 || !(estados_apagados & (1u << st));
    }

    void set_health(float h) {
        if (is_dead) return;
        float old = health;
        health = Math::clamp(h, 0.0f, max_health);

        if (old != health) {
            emit_signal("HealthChanged", health, old);
            fire_lua_health_changed(health_changed_cbs, health, old);
            _update_overhead();
        }

        if (health <= 0.0f && gl_get_state_enabled(7)) {
            is_dead = true;
            _set_state_internal(7); // Dead
            emit_signal("Died");
            fire_lua_died(died_cbs);
        }
    }
    float get_health() const { return health; }

    void set_max_health(float h) {
        max_health = Math::max(h, 1.0f);
        health = Math::min(health, max_health);
    }
    float get_max_health() const { return max_health; }

    // Equivalent to Roblox Humanoid:TakeDamage()
    //// Equivalente a Humanoid:TakeDamage() de Roblox
    void take_damage(float amount) {
        set_health(health - amount);
    }

    // Restore health
    //// Curar salud
    void heal(float amount) {
        if (is_dead) return;
        set_health(health + amount);
    }

    bool is_character_dead() const { return is_dead; }

    // ── Humanoid State ─────────────────────────────────────────────
    //// ── Estado del Humanoid ────────────────────────────────────────
    int     get_state() const          { return current_state; }
    Vector3 get_move_direction() const { return move_direction; }
    int floor_material = 36;   // Enum.Material (indice interno); 36 = Air
    int get_floor_material() const { return floor_material; }
    void    set_npc_move_dir(Vector3 dir) { npc_move_dir = dir; }
    Vector3 get_npc_move_dir() const      { return npc_move_dir; }
    void    stop_npc_movement()           { npc_move_dir = Vector3(); moveto_activo = false; walk_dir = Vector3(); }

    // ── MoveTo / Move ────────────────────────────────────────────────────
    // MoveTo(pos, pieza): anda hasta alli (o hasta ese punto de la pieza, que
    // se sigue aunque se mueva). Al llegar dispara MoveToFinished(true); si a
    // los 8 s no ha llegado, MoveToFinished(false), como Roblox.
    void gl_move_to(const Vector3& destino, Node3D* pieza) {
        moveto_activo = true;
        moveto_tiempo = 0.0;
        walk_dir = Vector3();
        npc_move_dir = Vector3();
        if (pieza && pieza->is_inside_tree()) {
            moveto_pieza = (uint64_t)pieza->get_instance_id();
            moveto_destino = pieza->get_global_transform().affine_inverse().xform(destino);
        } else {
            moveto_pieza = 0;
            moveto_destino = destino;
        }
    }
    // Humanoid:Move(dir): anda en esa direccion hasta que le digan otra cosa.
    void gl_move(const Vector3& dir) {
        walk_dir = Vector3(dir.x, 0.0f, dir.z);
        moveto_activo = false;
    }
    Node3D* gl_moveto_pieza() const {
        return moveto_pieza ? gl_cast<Node3D>(ObjectDB::get_instance(ObjectID(moveto_pieza))) : nullptr;
    }
    // WalkToPoint: con pieza, relativo a ella (como Roblox).
    Vector3 gl_walk_to_point() const { return moveto_destino; }

    // Direccion que pide un script (MoveTo, Move o la vieja WalkToPart), o cero.
    // Aqui se cierra tambien el MoveTo: al llegar o a los 8 s.
    Vector3 _gl_dir_guiada(const Vector3& pos, double delta) {
        if (moveto_activo) {
            Vector3 destino = moveto_destino;
            if (moveto_pieza) {
                Node3D* p = gl_moveto_pieza();
                if (!p || !p->is_inside_tree()) { _gl_terminar_moveto(false); return Vector3(); }
                destino = p->get_global_transform().xform(moveto_destino);
            }
            const Vector3 d(destino.x - pos.x, 0.0f, destino.z - pos.z);
            moveto_tiempo += delta;
            if (d.length() < 1.0f) { _gl_terminar_moveto(true); return Vector3(); }
            if (moveto_tiempo > 8.0) { _gl_terminar_moveto(false); return Vector3(); }
            return d.normalized();
        }
        if (walk_dir.length_squared() > 1e-4f) return walk_dir.normalized();
        if (npc_move_dir.length_squared() > 0.01f) return Vector3(npc_move_dir.x, 0.0f, npc_move_dir.z).normalized();
        return Vector3();
    }
    void _gl_terminar_moveto(bool llego) {
        moveto_activo = false;
        _moveto_fin = llego ? 1 : 2;   // MoveToFinished se dispara en _process
    }

    // ── Empujes de fuera ─────────────────────────────────────────────────
    // `ejes` lleva 1 en los ejes que manda el empuje (los de MaxForce > 0).
    void gl_forzar_velocidad(Vector3 v, Vector3 ejes) {
        ext_vel = v;
        ext_ejes = ejes;
        ext_frame = Engine::get_singleton()->get_physics_frames();
    }
    void _gl_aplicar_empuje(Vector3& v) const {
        if (ext_frame + 1 < Engine::get_singleton()->get_physics_frames()) return;
        if (ext_ejes.x > 0.5f) v.x = ext_vel.x;
        if (ext_ejes.y > 0.5f) v.y = ext_vel.y;
        if (ext_ejes.z > 0.5f) v.z = ext_vel.z;
    }

    // ── Asientos ─────────────────────────────────────────────────────────
    Node3D* gl_asiento() const {
        return asiento_id ? gl_cast<Node3D>(ObjectDB::get_instance(ObjectID(asiento_id))) : nullptr;
    }
    bool gl_sentar(Object* asiento) {
        Node3D* a = gl_cast<Node3D>(asiento);
        if (!a || is_dead || asiento_id || !gl_es_asiento(a)) return false;
        if (gl_asiento_deshabilitado(a) || gl_ocupante_de(a)) return false;
        asiento_id = (uint64_t)a->get_instance_id();
        a->set_meta("_gl_ocupante", (int64_t)(uint64_t)get_instance_id());
        moveto_activo = false;
        _set_state_internal(6);   // Seated
        _sentado_aviso = 1;
        _asiento_aviso = asiento_id;
        return true;
    }
    void gl_levantar(bool saltar) {
        if (!asiento_id) {
            if (current_state == 6) _set_state_internal(1);   // Sit = true sin asiento
            return;
        }
        Node3D* a = gl_asiento();
        if (a) {
            if (a->has_meta("_gl_ocupante")) a->remove_meta("_gl_ocupante");
            if (gl_es_asiento_vehiculo(a)) _gl_mando_vehiculo(a, 0, 0);   // suelta el mando
        }
        _asiento_aviso = asiento_id;
        asiento_id = 0;
        sentar_espera = 1.0;
        _sentado_aviso = 2;
        if (saltar) jump_requested = true;
        _set_state_internal(1);
    }
    // Throttle/Steer del VehicleSeat: -1, 0 o 1, como Roblox (los Float igual).
    void _gl_mando_vehiculo(Node3D* a, int acelera, int gira) {
        static const StringName k_t("_gl_throttle"), k_s("_gl_steer");
        const int ta = a->has_meta(k_t) ? (int)a->get_meta(k_t) : 0;
        const int sa = a->has_meta(k_s) ? (int)a->get_meta(k_s) : 0;
        if (ta != acelera) { a->set_meta(k_t, acelera); _vehiculo_aviso |= 1; _asiento_aviso = (uint64_t)a->get_instance_id(); }
        if (sa != gira)    { a->set_meta(k_s, gira);    _vehiculo_aviso |= 2; _asiento_aviso = (uint64_t)a->get_instance_id(); }
    }
    void set_platform_stand(bool b) { platform_stand = b; if (b) _set_state_internal(15); else if (current_state == 15) _set_state_internal(1); }
    bool get_platform_stand() const { return platform_stand; }

    void change_state(int s) {
        if (s < 0 || s > 14) return;
        // Physics states (Running, Jumping, Freefall, Landed) remain automatic
        // unless the user forces them explicitly
        //// Los estados de física (Running, Jumping, Freefall, Landed) siguen siendo automáticos
        //// excepto si el usuario los fuerza explícitamente
        forced_state = (s == 1 || s == 2 || s == 3 || s == 8) ? -1 : s;
        _set_state_internal(s);
    }

    // ── Movement ───────────────────────────────────────────────────
    //// ── Movimiento ─────────────────────────────────────────────────
    void  set_walk_speed(float s)   { walk_speed = Math::max(s, 0.0f); }
    float get_walk_speed() const    { return walk_speed; }
    void  set_jump_power(float p)   { jump_power = Math::max(p, 0.0f); }
    float get_jump_power() const    { return jump_power; }
    void  set_use_jump_power(bool b) { use_jump_power = b; }
    bool  get_use_jump_power() const  { return use_jump_power; }
    void  set_jump_request(bool b)   { jump_requested = b; }
    bool  get_jump_request() const   { return jump_requested; }
    void  set_max_slope_angle(float a) {
        max_slope_angle = Math::clamp(a, 0.0f, 89.0f);
        if (CharacterBody3D* b = gl_cast<CharacterBody3D>(get_parent()))
            b->set_floor_max_angle((float)Math::deg_to_rad((double)max_slope_angle));
    }
    float get_max_slope_angle() const { return max_slope_angle; }

    // La gravedad con la que cae ESTE personaje: la suya si tiene override,
    // y si no la del Workspace, como en Roblox.
    float gl_gravedad_efectiva() const {
        if (gravity >= 0.0f) return gravity;
        return gl_workspace_gravity();
    }

    void  set_jump_height(float h)  { jump_height = Math::max(h, 0.0f); }
    float get_jump_height() const   { return jump_height; }
    void  set_hip_height(float h)   { hip_height = Math::max(h, 0.0f); }
    float get_hip_height() const    { return hip_height; }
    void  set_gravity_val(float g)  { gravity = Math::max(g, 0.0f); }
    float get_gravity_val() const   { return gravity; }

    // ── Character ──────────────────────────────────────────────────
    //// ── Personaje ──────────────────────────────────────────────────
    void   set_auto_rotate(bool b)              { auto_rotate = b; }
    bool   get_auto_rotate() const              { return auto_rotate; }
    void   set_auto_jump_enabled(bool b)        { auto_jump_enabled = b; }
    bool   get_auto_jump_enabled() const        { return auto_jump_enabled; }
    void   set_display_name(String n)           { display_name = n; }
    String get_display_name() const             { return display_name; }
    void   set_name_display_distance(float d)   { name_display_distance = Math::max(d, 0.0f); }
    float  get_name_display_distance() const    { return name_display_distance; }
    void   set_health_display_distance(float d) { health_display_distance = Math::max(d, 0.0f); }
    float  get_health_display_distance() const  { return health_display_distance; }
    void   set_display_distance_type(int t)     { display_distance_type = Math::clamp(t, 0, 2); }
    int    get_display_distance_type() const    { return display_distance_type; }
    void   set_health_display_type(int t)       { health_display_type = Math::clamp(t, 0, 2); }
    int    get_health_display_type() const      { return health_display_type; }
    void   set_rig_type(int t)                  { rig_type = Math::clamp(t, 0, 1); }
    int    get_rig_type() const                 { return rig_type; }
    void   set_evaluate_state_machine(bool b)   { evaluate_state_machine = b; }
    bool   get_evaluate_state_machine() const   { return evaluate_state_machine; }
    void   set_break_joints_on_death(bool b)    { break_joints_on_death = b; }
    bool   get_break_joints_on_death() const    { return break_joints_on_death; }
    void   set_requires_neck(bool b)            { requires_neck = b; }
    bool   get_requires_neck() const            { return requires_neck; }

    void _set_state_internal(int new_state) {
        if (current_state == new_state) return;
        int old = current_state;
        current_state = new_state;
        emit_signal("StateChanged", new_state, old);
        fire_lua_state_changed(state_changed_cbs, new_state, old);
    }

    // ── _physics_process: all movement logic ───────────────────────
    //// ── _physics_process: toda la lógica de movimiento ─────────────
    // Crea el display sobre la cabeza (nombre + barra de vida), estilo Roblox:
    // el nombre es un Label3D en billboard; la barra es un quad con shader que se
    // llena desde la izquierda (verde→rojo) y se ve a través de paredes (always
    // on top). Se crea una sola vez, colgado del cuerpo del personaje.
    void _ensure_overhead() {
        // ── SOLO SI EL PERSONAJE ESTA EN EL MUNDO ────────────────────
        // Un personaje guardado en ReplicatedStorage/ServerStorage es una
        // PLANTILLA que el juego clona; en Roblox no se ve nada de el. Aqui su
        // Humanoid seguia creando el cartel de nombre y vida, y como esos
        // servicios no tienen transform el Label3D acababa dibujandose EN EL
        // ORIGEN del mapa. Medido en el place: seis nombres de NPC
        // ("Jandel", "KreekCraft", "rip_indra"...) flotando en el origen.
        // Ademas se ahorra un Label3D y un Sprite3D por cada plantilla.
        const bool en_mundo = gl_en_workspace(this);
        if (_overhead) {
            // Y al reves: si lo mueven fuera del mundo, se apaga; si lo clonan
            // dentro, se vuelve a encender. Esto corre cada tick, asi que se
            // entera del cambio de padre sin escuchar nada.
            if (_overhead->is_visible() != en_mundo) _overhead->set_visible(en_mundo);
            return;
        }
        if (!en_mundo) return;
        Node3D* body = gl_cast<Node3D>(get_parent());
        if (!body) return;

        _overhead = memnew(Node3D);
        _overhead->set_name("Overhead");
        body->add_child(_overhead);
        _overhead->set_position(Vector3(0, 3.0f, 0));   // sobre la cabeza (R6/R15)

        // Nombre
        _name_label = memnew(Label3D);
        _name_label->set_text(display_name.is_empty() ? String(body->get_name()) : display_name);
        _name_label->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
        _name_label->set_draw_flag(Label3D::FLAG_DISABLE_DEPTH_TEST, true);
        _name_label->set_font_size(48);
        _name_label->set_pixel_size(0.0065f);   // ~0.3 unidades de alto, escala con distancia
        _name_label->set_outline_size(8);
        _name_label->set_position(Vector3(0, 0.58f, 0));   // nombre arriba, con hueco sobre la barra
        _overhead->add_child(_name_label);

        // Barra de vida: Sprite3D con textura generada (billboard nativo, fiable).
        // La textura se rellena de izquierda a derecha según la vida (verde→rojo),
        // exactamente como una barra de Roblox.
        _hp_sprite = memnew(Sprite3D);
        _hp_sprite->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
        _hp_sprite->set_draw_flag(SpriteBase3D::FLAG_DISABLE_DEPTH_TEST, true);
        _hp_sprite->set_pixel_size(0.014f);   // 100px * 0.014 = ~1.4 unidades de ancho
        _hp_sprite->set_position(Vector3(0, 0.28f, 0));
        _overhead->add_child(_hp_sprite);

        _update_overhead();
    }

    Ref<ImageTexture> _make_hp_texture(float ratio) {
        const int W = 100, H = 14;
        Ref<Image> img = Image::create_empty(W, H, false, Image::FORMAT_RGBA8);
        if (img.is_null() || img->is_empty()) return Ref<ImageTexture>();
        Color fill = Color(0.9f, 0.15f, 0.15f).lerp(Color(0.15f, 0.9f, 0.2f), ratio);
        Color bg(0.07f, 0.07f, 0.07f, 1.0f);
        Color border(0.0f, 0.0f, 0.0f, 1.0f);
        int fillx = (int)(ratio * W);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                bool b = (x < 2 || x >= W - 2 || y < 2 || y >= H - 2);
                img->set_pixel(x, y, b ? border : (x < fillx ? fill : bg));
            }
        return ImageTexture::create_from_image(img);
    }

    // Oculta el nombre y la barra cuando la camara esta lejos, usando las
    // distancias de Roblox (NameDisplayDistance / HealthDisplayDistance). Antes
    // esas propiedades existian pero NO se aplicaban: el cartel se dibujaba
    // siempre, a cualquier distancia y para todos los personajes.
    void _update_overhead_distance() {
        if (!_overhead || !_overhead->is_inside_tree()) return;
        Viewport* vp = _overhead->get_viewport();
        Camera3D* cam = vp ? vp->get_camera_3d() : nullptr;
        if (!cam) return;
        const float d = cam->get_global_position().distance_to(_overhead->get_global_position());
        // DisplayDistanceType.None (2) apaga el cartel entero, pase lo que pase
        // con las distancias: es como los juegos quitan el nombre a los NPC.
        const bool sin_cartel = (display_distance_type == 2);
        if (_name_label) _name_label->set_visible(!sin_cartel && d <= name_display_distance);
        if (_hp_sprite) {
            const float ratio = max_health > 0.0f ? (health / max_health) : 0.0f;
            // HealthDisplayType: 0=solo con daño, 1=siempre, 2=nunca
            bool por_tipo = (health_display_type == 1) || (health_display_type == 0 && ratio < 0.999f);
            _hp_sprite->set_visible(!sin_cartel && por_tipo && d <= health_display_distance);
        }
    }

    // Ratio con el que se genero la textura de vida que hay puesta ahora mismo.
    // Sin esto, un personaje MUERTO regeneraba la barra en CADA paso de fisica
    // (ver _physics_process: `if (is_dead) { _update_overhead(); return; }`):
    // 1.400 set_pixel + crear una ImageTexture + subirla a la GPU, 30 veces por
    // segundo y por cada NPC muerto del mapa. La barra solo cambia cuando cambia
    // la vida, asi que se compara antes de rehacerla.
    float _hp_ratio_dibujado = -1.0f;

    void _update_overhead() {
        if (_hp_sprite) {
            float ratio = max_health > 0.0f ? Math::clamp(health / max_health, 0.0f, 1.0f) : 0.0f;
            // 1/256 es mas fino que un pixel de la barra: por debajo de eso la
            // textura saldria identica.
            if (Math::abs(ratio - _hp_ratio_dibujado) > 0.0039f) {
                _hp_ratio_dibujado = ratio;
                _hp_sprite->set_texture(_make_hp_texture(ratio));
            }
            // Como Roblox: HealthDisplayType decide (por defecto, solo con daño).
            bool por_tipo = (health_display_type == 1) || (health_display_type == 0 && ratio < 0.999f);
            _hp_sprite->set_visible(display_distance_type != 2 && por_tipo);
        }
        if (_name_label) {
            Node* body = get_parent();
            _name_label->set_text(display_name.is_empty() && body ? String(body->get_name()) : display_name);
        }
    }

    // ── Physics LOD del muñeco (ver gl_physlod.h) ─────────────────────
    // Un NPC a 400 studs no necesita 60 pasos por segundo, pero tampoco puede
    // andar mas despacio por ello. Se salta pasos y se ACUMULA el delta, asi
    // que el que se ejecuta recorre exactamente lo mismo: es un paso adaptativo
    // de verdad. El personaje local nunca entra (esta a ~15 studs de su camara).
    // El desfase por id es obligatorio: sin el, todos los NPC lejanos del mapa
    // trabajarian el MISMO frame y ese frame seria un bajon.
    uint8_t _lod_div = 1, _lod_fase = 0;
    double  _lod_acum = 0.0, _lod_timer = 0.0;

    bool _lod_saltar(double& delta) {
        GLPhysLodCfg& c = gl_physlod();
        if (!c.activo) { _lod_div = 1; return false; }
        _lod_timer -= delta;
        if (_lod_timer <= 0.0) {
            _lod_timer = 0.5;            // reclasificar 2 veces por segundo basta
            uint8_t div = 1;
            Node3D* body = gl_cast<Node3D>(get_parent());
            Viewport* vp = is_inside_tree() ? get_viewport() : nullptr;
            Camera3D* cam = vp ? vp->get_camera_3d() : nullptr;
            if (body && cam) {
                const float d2 = (float)body->get_global_position()
                                     .distance_squared_to(cam->get_global_transform().origin);
                // Solo un escalon (60 -> 30 Hz). Bajar mas empieza a notarse en
                // el paso del muñeco, y move_and_slide con un delta muy grande
                // puede atravesar suelos finos.
                if (d2 > c.cerca * c.cerca) div = gl_lod_divisor(c.hz_lejos) > 1 ? 2 : 1;
            }
            if (div != _lod_div) {
                _lod_div  = div;
                _lod_fase = div > 1 ? (uint8_t)((uint64_t)get_instance_id() % (uint64_t)div) : 0;
            }
        }
        if (_lod_div <= 1) { _lod_acum = 0.0; return false; }
        _lod_acum += delta;
        if (!gl_lod_toca(_lod_div, _lod_fase)) return true;
        delta = _lod_acum;               // el paso que si corre lleva lo acumulado
        _lod_acum = 0.0;
        return false;
    }

    // ── Sentado (jugador) ────────────────────────────────────────────────
    // El cuerpo se queda en el asiento y, si es un VehicleSeat, el input va al
    // mando (Throttle/Steer) en vez de mover al personaje. Devuelve false si se
    // acaba de levantar (salto, asiento borrado o deshabilitado).
    bool _gl_tick_sentado(CharacterBody3D* body, Input* input, bool fc, bool salta) {
        Node3D* a = gl_asiento();
        if (!a || !a->is_inside_tree() || gl_asiento_deshabilitado(a)) { gl_levantar(false); return false; }
        if (salta) { gl_levantar(true); return false; }
        const Transform3D t = a->get_global_transform();
        const Vector3 arriba = t.basis.get_column(1).normalized();
        const Vector3 delante = -t.basis.get_column(2).normalized();
        Vector3 tam(2.0f, 1.0f, 2.0f);
        if (RobloxPart* rp = gl_cast<RobloxPart>(a)) tam = rp->get_size();
        body->set_velocity(Vector3());
        // La HRP sentada queda 1,5 studs sobre el asiento, como en Roblox: en el
        // R6 la cadera esta 1 bajo el centro del torso y la pierna doblada mide
        // 1 de grueso; en el R15, 1,0 + medio muslo (0,49).
        body->set_global_position(t.origin + arriba * (tam.y * 0.5f + 1.5f));
        Vector3 rot = body->get_rotation();
        rot.y = Math::atan2(-delante.x, -delante.z);
        body->set_rotation(rot);
        if (gl_es_asiento_vehiculo(a)) {
            int acelera = 0;
            int gira = 0;
            if (!fc) {
                if (input->is_key_pressed(KEY_W) || input->is_key_pressed(KEY_UP))    acelera += 1;
                if (input->is_key_pressed(KEY_S) || input->is_key_pressed(KEY_DOWN))  acelera -= 1;
                if (input->is_key_pressed(KEY_D) || input->is_key_pressed(KEY_RIGHT)) gira += 1;
                if (input->is_key_pressed(KEY_A) || input->is_key_pressed(KEY_LEFT))  gira -= 1;
                const float lx = input->get_joy_axis(0, JOY_AXIS_LEFT_X);
                const float ly = input->get_joy_axis(0, JOY_AXIS_LEFT_Y);
                if (lx > 0.4f) gira = 1; else if (lx < -0.4f) gira = -1;
                if (ly < -0.4f) acelera = 1; else if (ly > 0.4f) acelera = -1;
            }
            _gl_mando_vehiculo(a, CLAMP(acelera, -1, 1), CLAMP(gira, -1, 1));
        }
        _drive_animation(body, Vector3(), 0.0);
        _set_state_internal(6);
        return true;
    }

    // ── NPC: Humanoid dentro de un Model cualquiera ──────────────────────
    // En Roblox el Humanoid mueve el conjunto de su HumanoidRootPart: lo lleva
    // hacia el MoveTo, lo mantiene de pie a HipHeight del suelo y lo gira hacia
    // donde va. Aqui solo existia el camino del jugador (un CharacterBody3D),
    // asi que un NPC de un place se quedaba clavado aunque su script le hiciera
    // MoveTo, y si no estaba anclado se caia de lado.
    void _gl_npc_tick(double delta) {
        static const StringName k_hrp("HumanoidRootPart");
        Node3D* modelo = gl_cast<Node3D>(get_parent());
        if (!modelo || !gl_en_workspace(this)) return;
        RobloxPart* hrp = gl_cast<RobloxPart>(modelo->get_node_or_null(NodePath(k_hrp)));
        if (!hrp || !hrp->is_inside_tree()) return;
        RobloxPart* raiz = gl_raiz_de(hrp);
        if (raiz->gl_lod_congelada() && (moveto_activo || walk_dir.length_squared() > 1e-4f))
            raiz->gl_lod_despertar();   // lejos y quieto, pero le mandan andar
        // Anclado (o congelado por otro: la red, un conjunto anclado): Roblox no
        // lo mueve. El MoveTo solo cuenta su tiempo para rendirse a los 8 s.
        if (raiz->is_freeze_enabled()) {
            if (moveto_activo) { moveto_tiempo += delta; if (moveto_tiempo > 8.0) _gl_terminar_moveto(false); }
            return;
        }
        if (platform_stand) { if (_npc_raiz) _gl_npc_soltar(); return; }

        const Vector3 pos = hrp->get_global_position();
        // Sentado: el conjunto va pegado al asiento (lo empuja la propia raiz).
        if (Node3D* a = gl_asiento()) {
            if (!a->is_inside_tree() || gl_asiento_deshabilitado(a)) { gl_levantar(false); return; }
            const Transform3D t = a->get_global_transform();
            Vector3 tam(2.0f, 1.0f, 2.0f);
            if (RobloxPart* rp = gl_cast<RobloxPart>(a)) tam = rp->get_size();
            const Vector3 sitio = t.origin + t.basis.get_column(1).normalized() * (tam.y * 0.5f + hrp->get_size().y * 0.5f + 0.5f);
            raiz->set_linear_velocity((sitio - pos) / (float)MAX(delta, 0.001));
            raiz->set_angular_velocity(Vector3());
            return;
        }

        // Altura de la HRP sobre el suelo: HipHeight + media HRP (R15). Con
        // HipHeight 0 (R6) cuentan las piernas: 2 studs.
        const float cadera = (hip_height > 0.01f ? hip_height : 2.0f) + hrp->get_size().y * 0.5f;
        Vector3 v = raiz->get_linear_velocity();
        bool en_suelo = false;
        if (v.y <= 2.0f) {   // subiendo deprisa (salto, empujon) no se pega al suelo
            PhysicsDirectSpaceState3D* space = hrp->get_world_3d()->get_direct_space_state();
            if (space) {
                Ref<PhysicsRayQueryParameters3D> q;
                q.instantiate();
                q->set_from(pos);
                q->set_to(pos - Vector3(0.0f, cadera + 1.5f, 0.0f));
                q->set_collision_mask(~GL_CAPA_CONSULTA);   // el suelo tiene que chocar
                TypedArray<RID> ex;
                ex.push_back(raiz->get_rid());
                q->set_exclude(ex);
                const Dictionary hit = space->intersect_ray(q);
                if (!hit.is_empty()) {
                    const float suelo_y = ((Vector3)hit.get("position", Vector3())).y;
                    if (pos.y - suelo_y <= cadera + 0.6f) {
                        en_suelo = true;
                        // Muelle hacia la altura de cadera, como el Humanoid de Roblox.
                        v.y = CLAMP(((suelo_y + cadera) - pos.y) * 12.0f, -30.0f, 30.0f);
                    }
                }
            }
        }
        if (jump_requested && en_suelo) {
            const float g = gl_gravedad_efectiva();
            v.y = use_jump_power ? jump_power
                                 : (float)Math::sqrt(2.0 * (double)g * (double)Math::max(jump_height, 0.0f));
            jump_requested = false;
            en_suelo = false;
        }

        const Vector3 dir = _gl_dir_guiada(pos, delta);
        Vector2 vh(v.x, v.z);
        vh = vh.move_toward(Vector2(dir.x, dir.z) * walk_speed, (en_suelo ? 110.0f : 45.0f) * (float)delta);
        v.x = vh.x;
        v.z = vh.y;
        _gl_aplicar_empuje(v);
        raiz->set_linear_velocity(v);

        // De pie: sin giro en X/Z; en Y, hacia donde anda (AutoRotate).
        if (_npc_raiz != (uint64_t)raiz->get_instance_id()) {
            if (_npc_raiz) _gl_npc_soltar();
            raiz->set_axis_lock(PhysicsServer3D::BODY_AXIS_ANGULAR_X, true);
            raiz->set_axis_lock(PhysicsServer3D::BODY_AXIS_ANGULAR_Z, true);
            _npc_raiz = (uint64_t)raiz->get_instance_id();
        }
        float giro = 0.0f;
        if (auto_rotate && dir.length_squared() > 0.01f) {
            const Vector3 fwd = -hrp->get_global_transform().basis.get_column(2);
            const float actual = Math::atan2(-fwd.x, -fwd.z);
            const float objetivo = Math::atan2(-dir.x, -dir.z);
            const float dif = Math::wrapf(objetivo - actual, -(float)Math_PI, (float)Math_PI);
            giro = CLAMP(dif * 10.0f, -12.0f, 12.0f);
        }
        raiz->set_angular_velocity(Vector3(0.0f, giro, 0.0f));

        // Estado y señales (Running/Jumping/FreeFalling para Animate.lua).
        move_direction = dir;
        const float spd = vh.length();
        anim_speed01 = Math::clamp(spd / Math::max(walk_speed, 0.1f), 0.0f, 1.0f);
        anim_airborne = !en_suelo;
        if (Math::abs(spd - _last_running_speed) > 0.25f) {
            _last_running_speed = spd;
            _pending_running = spd;
            _has_pending_running = true;
        }
        if (anim_airborne != _last_airborne) {
            _last_airborne = anim_airborne;
            _pending_airborne = anim_airborne;
            _pending_jumped = anim_airborne && v.y > 0.1f;
            _has_pending_air = true;
        }
        if (evaluate_state_machine && forced_state < 0) {
            if (en_suelo) _set_state_internal(1);
            else          _set_state_internal(v.y > 0.1f ? 2 : 8);
        }
    }
    // Suelta los ejes de giro del NPC (muerto, PlatformStand, otra raiz).
    void _gl_npc_soltar() {
        if (RigidBody3D* r = gl_cast<RigidBody3D>(ObjectDB::get_instance(ObjectID(_npc_raiz)))) {
            r->set_axis_lock(PhysicsServer3D::BODY_AXIS_ANGULAR_X, false);
            r->set_axis_lock(PhysicsServer3D::BODY_AXIS_ANGULAR_Z, false);
        }
        _npc_raiz = 0;
    }

    void _physics_process(double delta) override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (_lod_saltar(delta)) return;
        _ensure_overhead();
        _update_overhead_distance();
        if (sentar_espera > 0.0) sentar_espera -= delta;
        if (is_dead) {
            _update_overhead();
            if (_npc_raiz) _gl_npc_soltar();
            if (asiento_id) gl_levantar(false);
            return;
        }

        CharacterBody3D* body = gl_cast<CharacterBody3D>(get_parent());
        if (!body) { _gl_npc_tick(delta); return; }

        // Config del cuerpo una sola vez: pegado al suelo (no rebota en rampas/
        // escalones) y angulo de suelo amplio, como un personaje de Roblox.
        if (!body_configured) {
            body->set_up_direction(Vector3(0, 1, 0));
            body->set_floor_snap_length(0.5f);
            body->set_floor_stop_on_slope_enabled(true);
            body->set_floor_max_angle((float)Math::deg_to_rad((double)max_slope_angle));
            body_configured = true;
        }

        Input* input = Input::get_singleton();
        Vector3 velocity = body->get_velocity();

        // ── 0. Sentado: el cuerpo va pegado al asiento ──────────────
        if (asiento_id) {
            const bool fc = gl_freecam().active || gl_menu_open() || gl_controls_disabled();
            const bool salta = jump_requested || (!fc && (input->is_key_pressed(KEY_SPACE)
                || input->is_joy_button_pressed(0, JOY_BUTTON_A)
                || (gl_mobile().active && gl_mobile().jump)));
            if (_gl_tick_sentado(body, input, fc, salta)) return;
            // Se acaba de levantar: si fue saltando, sale con el salto.
            velocity = Vector3();
            if (salta) {
                const float g = gl_gravedad_efectiva();
                velocity.y = use_jump_power
                    ? jump_power
                    : (float)Math::sqrt(2.0 * (double)g * (double)Math::max(jump_height, 0.0f));
                jump_requested = false;
            }
        }

        // ── 1. Gravity ─────────────────────────────────────────────
        //// ── 1. Gravedad ────────────────────────────────────────────
        if (!body->is_on_floor()) {
            velocity.y -= gl_gravedad_efectiva() * (float)delta;
        }

        // ── 2. Jump (teclado + gamepad A + boton tactil) ───────────
        //// ── 2. Salto (teclado + gamepad A + botón táctil) ──────────
        // Vista de Servidor activa (o menú abierto): el input no mueve al
        // personaje (WASD/salto congelados, como en Roblox Studio / menú Esc).
        bool freecam = gl_freecam().active || gl_menu_open() || gl_controls_disabled();
        // jump_requested (Humanoid.Jump por script) NO pasa por la puerta del
        // freecam/menu: en Roblox un script puede hacer saltar al personaje
        // aunque el jugador no este tocando el input.
        bool jump_pressed = jump_requested || (!freecam && (input->is_key_pressed(KEY_SPACE)
            || input->is_joy_button_pressed(0, JOY_BUTTON_A)
            || (gl_mobile().active && gl_mobile().jump)));
        if (jump_pressed && body->is_on_floor() && gl_get_state_enabled(2)) {
            // Roblox: la altura de un salto es JumpPower^2 / (2*Gravity). Con
            // UseJumpPower=false manda JumpHeight y hay que despejar la
            // velocidad inicial: v0 = sqrt(2*g*altura).
            // Con los valores de Roblox (JumpPower 50, gravedad 196.2) salen
            // 6,371 studs de alto y 0,51 s de vuelo.
            // Antes JumpHeight estaba guardada pero no se usaba NUNCA: los
            // places que la configuran no hacian nada.
            const float g = gl_gravedad_efectiva();
            velocity.y = use_jump_power
                ? jump_power
                : (float)Math::sqrt(2.0 * (double)g * (double)Math::max(jump_height, 0.0f));
            jump_requested = false;   // la peticion por script se consume al saltar
        }

        // ── 3. Direction based on active camera ────────────────────
        //// ── 3. Dirección según la cámara activa ───────────────────
        Camera3D* cam = get_viewport()->get_camera_3d();
        if (cam) {
            Basis cam_basis = cam->get_global_transform().basis;
            // forward: camera -Z axis, projected onto the ground
            //// forward: eje -Z de la cámara, proyectado al suelo
            Vector3 forward = -cam_basis.get_column(2);
            Vector3 right   =  cam_basis.get_column(0);
            forward.y = 0.0f;
            right.y   = 0.0f;
            forward   = forward.normalized();
            right     = right.normalized();

            // Input de movimiento UNIFICADO cross-device: teclado, gamepad y
            // joystick tactil alimentan el mismo vector (como el moveVector del
            // ControlModule de Roblox). Asi el personaje se mueve en PC, movil
            // y consola sin cambiar nada.
            float input_forward = 0.0f;
            float input_right   = 0.0f;
            if (!freecam) {
                // Teclado (WASD + flechas)
                if (input->is_key_pressed(KEY_W) || input->is_key_pressed(KEY_UP))    input_forward += 1.0f;
                if (input->is_key_pressed(KEY_S) || input->is_key_pressed(KEY_DOWN))  input_forward -= 1.0f;
                if (input->is_key_pressed(KEY_D) || input->is_key_pressed(KEY_RIGHT)) input_right   += 1.0f;
                if (input->is_key_pressed(KEY_A) || input->is_key_pressed(KEY_LEFT))  input_right   -= 1.0f;
                // Gamepad: stick izquierdo (zona muerta para evitar deriva)
                {
                    Vector2 ls(input->get_joy_axis(0, JOY_AXIS_LEFT_X),
                               input->get_joy_axis(0, JOY_AXIS_LEFT_Y));
                    if (ls.length() > 0.2f) { input_right += ls.x; input_forward -= ls.y; }
                }
                // Tactil: joystick virtual en pantalla (lo escribe el RobloxPlayer)
                if (gl_mobile().active) {
                    Vector2 tv = gl_mobile().move;
                    if (tv.length() > 0.05f) { input_right += tv.x; input_forward += tv.y; }
                }
            }
            input_forward = Math::clamp(input_forward, -1.0f, 1.0f);
            input_right   = Math::clamp(input_right,   -1.0f, 1.0f);

            Vector3 move_dir = forward * input_forward + right * input_right;

            // Lo que pide un script (MoveTo / Move) manda sobre el input, y
            // con PlatformStand el Humanoid no anda.
            const Vector3 guiada = _gl_dir_guiada(body->get_global_position(), delta);
            if (guiada.length_squared() > 0.01f) move_dir = guiada;
            if (platform_stand) move_dir = Vector3();

            // Primera persona (1.15): el cuerpo mira SIEMPRE hacia donde apunta
            // la camara, tambien parado y andando hacia atras. Antes giraba hacia
            // la direccion del movimiento tambien aqui, asi que pulsar S te daba
            // la vuelta en vez de andar de espaldas.
            bool first_person = body->has_method("_gl_is_first_person")
                             && (bool)body->call("_gl_is_first_person");

            // Velocidad horizontal OBJETIVO (0 si no hay input).
            Vector3 target_h(0.0f, 0.0f, 0.0f);
            if (move_dir.length_squared() > 0.01f) {
                move_dir = move_dir.normalized();
                // Sin sprint: en Roblox correr con Shift NO existe, y ese x1.5
                // rompia el balanceo de cualquier place importado (velocidad
                // punta 24 en vez de 16). Un juego que lo quiera se lo programa
                // en Luau tocando Humanoid.WalkSpeed, que es como se hace alli.
                target_h = move_dir * walk_speed;

                // AutoRotate: girar el cuerpo hacia la direccion del movimiento,
                // como Roblox. Suavizado exponencial: independiente de los FPS y
                // SIN sobre-girar (el factor nunca pasa de 1). Antes usaba
                // 16*delta, que con FPS bajos podia pasar de 1 y "latigar".
                if (auto_rotate && !first_person) {
                    float target_angle = (float)Math::atan2((double)-move_dir.x, (double)-move_dir.z);
                    Vector3 cur_rot = body->get_rotation();
                    float t = 1.0f - Math::exp(-turn_speed * (float)delta);
                    cur_rot.y = (float)Math::lerp_angle(
                        (double)cur_rot.y, (double)target_angle, (double)t);
                    body->set_rotation(cur_rot);
                }
            }

            // En primera persona el giro es INSTANTANEO y va pegado a la camara
            // (Roblox lo bloquea ahi); suavizarlo se sentiria como un retraso.
            if (auto_rotate && first_person && body->has_method("_gl_camera_yaw")) {
                Vector3 cur_rot = body->get_rotation();
                cur_rot.y = (float)(double)body->call("_gl_camera_yaw");
                body->set_rotation(cur_rot);
            }

            // Aceleracion/desaceleracion hacia la velocidad objetivo. DELTA-correcto
            // (antes la frenada dependia de los FPS) y con menos control en el aire.
            //
            // ── EN VECTOR, NO EJE POR EJE ─────────────────────────────
            // Antes se hacia un move_toward a X y otro a Z, cada uno con el MISMO
            // ritmo. Eso NO conserva la direccion: yendo en diagonal, la
            // componente pequeña llega a cero antes que la grande, y desde ese
            // instante te mueves solo por el eje que queda. O sea que el personaje
            // GIRA mientras frena.
            //   Ejemplo: v = (3, 10) soltando el mando -> X se agota primero y el
            //   ultimo tramo se recorre en linea recta sobre Z, no en la diagonal.
            // Se veia como un tironcito de lado al soltar la W y una correccion
            // despues. Tratando (x,z) como UN vector, el frenado va en linea recta
            // y la direccion se mantiene hasta pararse.
            const float accel = body->is_on_floor() ? 110.0f : 45.0f;
            Vector2 v_h(velocity.x, velocity.z);
            const Vector2 t_h(target_h.x, target_h.z);
            v_h = v_h.move_toward(t_h, accel * (float)delta);
            velocity.x = v_h.x;
            velocity.z = v_h.y;
        }

        _gl_aplicar_empuje(velocity);   // BodyVelocity/LinearVelocity en la HRP
        body->set_velocity(velocity);
        body->move_and_slide();

        // ── FloorMaterial: el material de lo que hay bajo los pies ─────
        // Antes era un 256 fijo (ni siquiera un material valido). En el aire
        // es Air, y un suelo que no es una pieza (terreno) cuenta como Plastic.
        {
            static const StringName k_mat("get_roblox_material");
            int suelo = 36;   // Enum.Material.Air
            if (body->is_on_floor()) {
                suelo = 0;
                for (int i = 0; i < body->get_slide_collision_count(); i++) {
                    Ref<KinematicCollision3D> col = body->get_slide_collision(i);
                    if (col.is_null() || col->get_normal().y < 0.7f) continue;
                    Object* o = col->get_collider();
                    if (o && o->has_method(k_mat)) { suelo = (int)o->call(k_mat); break; }
                }
            }
            floor_material = suelo;
        }

        // ── 4. Detect collisions with RobloxParts (Touched event) ──
        //// ── 4. Detectar colisiones con RobloxParts (evento Touched) ─
        for (int i = 0; i < body->get_slide_collision_count(); i++) {
            Ref<KinematicCollision3D> col = body->get_slide_collision(i);
            if (col.is_valid()) {
                Node* collider = gl_cast<Node>(col->get_collider());
                // Tocar un Seat libre sienta al personaje, como en Roblox. Se
                // mira antes de Touched: su manejador puede destruir la pieza.
                if (collider && !asiento_id && sentar_espera <= 0.0 && gl_es_asiento(collider))
                    gl_sentar(collider);
                if (collider && collider->has_signal("Touched")) {
                    // Se reporta una PARTE del personaje, no el personaje: así
                    // `hit.Parent:FindFirstChild("Humanoid")` funciona (1.14.11).
                    collider->emit_signal("Touched", gl_touch_reported_node(body));
                }
            }
        }

        // ── 5. State machine (HumanoidStateType) ───────────────────
        //// ── 5. Máquina de estados (HumanoidStateType) ─────────────
        if (evaluate_state_machine) {
            bool on_floor = body->is_on_floor();
            Vector3 horiz = Vector3(velocity.x, 0.0f, velocity.z);
            move_direction = horiz.length_squared() > 0.01f ? horiz.normalized() : Vector3();

            if (forced_state >= 0) {
                _set_state_internal(forced_state);
            } else if (on_floor) {
                if (!was_on_floor && (current_state == 8 || current_state == 2)) {
                    // Just landed
                    //// Acaba de aterrizar
                    _set_state_internal(3); // Landed
                    landed_timer = 0.12f;
                } else if (landed_timer > 0.0f) {
                    landed_timer -= (float)delta;
                    if (landed_timer <= 0.0f) _set_state_internal(1); // Running
                } else {
                    _set_state_internal(1); // Running (también cuando está quieto en suelo)
                }
            } else {
                // En el aire
                if (velocity.y > 0.1f) _set_state_internal(2); // Jumping
                else                    _set_state_internal(8); // Freefall
            }
            was_on_floor = on_floor;
        }

        // Animacion de PRUEBA por codigo (placeholder): SOLO si el toggle de
        // Debug 'debug_test_animation' esta activo. Anima la malla visual, no
        // la fisica; sirve para ver el muñeco "vivo" sin animaciones reales aun.
        _drive_animation(body, velocity, delta);
    }

    // ── Estado de animacion del personaje ──
    // Al avatar R6/R15 lo anima su Animate (Running/Jumping/FreeFalling de aqui
    // abajo); _test_animate es la prueba visual que se enciende a mano.
    void _drive_animation(CharacterBody3D* body, const Vector3& velocity, double delta) {
        Vector3 horiz(velocity.x, 0.0f, velocity.z);
        double speed01 = Math::clamp((double)horiz.length() / (double)Math::max(walk_speed, 0.1f), 0.0, 1.0);
        // Estado REAL de animación (1.14.17): se guarda para que el NetworkService
        // lo replique tal cual. Antes cada máquina lo ADIVINABA del movimiento del
        // muñeco, y por eso el salto de otro jugador duraba más de la cuenta.
        anim_speed01  = (float)speed01;
        anim_airborne = !body->is_on_floor();
        // Senales de Roblox: Running con la velocidad real (studs/s) cada vez que
        // cambia de forma apreciable, y FreeFalling/Jumping al despegar y
        // aterrizar. Es lo que consume Animate.lua para elegir la animacion.
        //
        // OJO: aqui solo se APUNTA lo que hay que avisar. Disparar Lua dentro del
        // paso de fisica reventaba el motor: los manejadores crean y destruyen
        // nodos (AnimationPlayer, tracks) y Godot no admite tocar el arbol de
        // cuerpos a mitad del step. Se emite en _process, ya fuera del step.
        {
            const float spd = horiz.length();
            if (Math::abs(spd - _last_running_speed) > 0.25f) {
                _last_running_speed = spd;
                _pending_running = spd;
                _has_pending_running = true;
            }
            if (anim_airborne != _last_airborne) {
                _last_airborne = anim_airborne;
                _pending_airborne = anim_airborne;
                _pending_jumped = anim_airborne && velocity.y > 0.1f;
                _has_pending_air = true;
            }
        }
        _test_animate(body, velocity, delta);
    }

    // ── Animacion de prueba: como el avatar es una malla unica sin huesos,
    //    animamos su transform (rebote al estar quieto/caminar + giro hacia
    //    la direccion de movimiento). Se restaura sola al apagar el toggle.
    float _last_running_speed = -1.0f;
    bool  _last_airborne = false;
    float _pending_running = 0.0f;
    bool  _has_pending_running = false;
    bool  _has_pending_air = false;
    bool  _pending_airborne = false;
    bool  _pending_jumped = false;

public:
    // Emite las senales de movimiento pendientes FUERA del paso de fisica.
    void _gl_flush_move_signals() {
        if (_has_pending_running) {
            _has_pending_running = false;
            if (!running_cbs.empty()) fire_lua_one(running_cbs, (double)_pending_running);
        }
        if (_has_pending_air) {
            _has_pending_air = false;
            if (_pending_airborne) {
                if (_pending_jumped && !jumping_cbs.empty()) fire_lua_one(jumping_cbs, true);
                if (!freefalling_cbs.empty()) fire_lua_one(freefalling_cbs, true);
            } else {
                if (!freefalling_cbs.empty()) fire_lua_one(freefalling_cbs, false);
                if (!gettingup_cbs.empty())   fire_lua_one(gettingup_cbs, true);
            }
        }
    }

private:
    float anim_time     = 0.0f;
    float anim_base_y   = 0.0f;
    bool  anim_base_set = false;

    void _test_animate(CharacterBody3D* body, const Vector3& velocity, double delta) {
        Node3D* mesh = body ? gl_cast<Node3D>(body->get_node_or_null(NodePath("Character"))) : nullptr;
        if (!mesh) return;
        bool on = (bool)ProjectSettings::get_singleton()->get_setting("godot_luau/debug_test_animation", false);
        if (!on) {
            if (anim_base_set) {
                Vector3 p = mesh->get_position(); p.y = anim_base_y;
                mesh->set_position(p); mesh->set_rotation(Vector3());
                anim_base_set = false;
            }
            return;
        }
        if (!anim_base_set) { anim_base_y = mesh->get_position().y; anim_base_set = true; }
        anim_time += (float)delta;
        Vector3 horiz(velocity.x, 0.0f, velocity.z);
        bool moving = horiz.length() > 0.5f;
        float freq = moving ? 9.0f : 2.2f;
        float amp  = moving ? 0.12f : 0.04f;
        Vector3 p = mesh->get_position();
        p.y = anim_base_y + Math::abs(Math::sin(anim_time * freq)) * amp;
        mesh->set_position(p);
        // Mismo criterio que el AutoRotate del cuerpo: en Godot el "adelante"
        // es -Z, asi que el yaw hacia una direccion (dx,dz) es atan2(-dx,-dz).
        // Sin los signos esto giraba la malla 180 grados respecto al cuerpo.
        float yaw  = moving ? Math::atan2(-velocity.x, -velocity.z) : mesh->get_rotation().y;
        float sway = moving ? Math::sin(anim_time * freq) * 0.08f : 0.0f;
        mesh->set_rotation(Vector3(0.0f, yaw, sway));
    }

    bool _ocupante_cambio = false;

    // MoveToFinished, Seated y los cambios de Sit/SeatPart/Occupant/Throttle/
    // Steer se apuntan en el paso de fisica y se avisan aqui, ya fuera de el.
    void _gl_flush_avisos() {
        if (_moveto_fin) {
            const bool llego = (_moveto_fin == 1);
            _moveto_fin = 0;
            if (!moveto_cbs.empty()) fire_lua_one(moveto_cbs, llego);
        }
        if (_sentado_aviso) {
            const bool sentado = (_sentado_aviso == 1);
            _sentado_aviso = 0;
            _ocupante_cambio = true;
            Node3D* a = sentado ? gl_asiento() : nullptr;
            if (!seated_cbs.empty()) {
                gl_fire_cbs(seated_cbs, [&](lua_State* th) {
                    lua_pushboolean(th, sentado ? 1 : 0);
                    if (a) {
                        GodotObjectWrapper* w = (GodotObjectWrapper*)lua_newuserdata(th, sizeof(GodotObjectWrapper));
                        gow_set(w, a);
                        luaL_getmetatable(th, "GodotObject");
                        lua_setmetatable(th, -2);
                    } else {
                        lua_pushnil(th);
                    }
                    return 2;
                });
            }
            gl_fire_prop_changed(this, "Sit");
            gl_fire_prop_changed(this, "SeatPart");
        }
        if (_asiento_aviso) {
            Node* a = gl_cast<Node>(ObjectDB::get_instance(ObjectID(_asiento_aviso)));
            _asiento_aviso = 0;
            if (a) {
                if (_ocupante_cambio) gl_fire_prop_changed(a, "Occupant");
                if (_vehiculo_aviso & 1) { gl_fire_prop_changed(a, "Throttle"); gl_fire_prop_changed(a, "ThrottleFloat"); }
                if (_vehiculo_aviso & 2) { gl_fire_prop_changed(a, "Steer");    gl_fire_prop_changed(a, "SteerFloat"); }
            }
            _ocupante_cambio = false;
            _vehiculo_aviso = 0;
        }
    }

public:
    // Fuera del paso de fisica: aqui SI se puede llamar a Lua (los manejadores
    // de Animate.lua crean y destruyen nodos).
    void _process(double) override { _gl_flush_move_signals(); _gl_flush_avisos(); }
};

#endif // HUMANOID_H

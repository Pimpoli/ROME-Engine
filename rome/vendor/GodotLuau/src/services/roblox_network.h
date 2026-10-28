#ifndef ROBLOX_NETWORK_H
#define ROBLOX_NETWORK_H

// ════════════════════════════════════════════════════════════════════
//  NetworkService — multijugador local estilo Roblox Studio.
//
//  Fase 0: conectividad real (ENet/UDP) con la red nativa de Godot.
//    net:StartServer(port, maxPlayers)   → tu PC como servidor (host)
//    net:Connect(ip, port)               → unirse a un servidor (acepta IP o hostname)
//    net:Disconnect()                    → volver a un jugador
//    net:IsConnected() / net:GetConnectionState() / net:GetServerAddress()
//
//  Direct Connect por IP (juegos exportados, sin escribir Lua) vía args:
//    --glconnect <ip>:<port>   → arrancar uniéndose a ese servidor
//    --glserver  <port>        → arrancar hospedando en ese puerto
//    --glhost    <ip>          → host al que apuntan las ventanas cliente del
//                                test local (por defecto 127.0.0.1; o res://GodotLuau/.gl_host)
//
//  Fase 1 (ESTE archivo): RÉPLICA de jugadores. Cada instancia difunde la
//  posición de su propio personaje y ve a los demás como "muñecos" (puppets)
//  que se mueven en tiempo real y tienen colisión (te chocas con ellos).
//  Topología estrella con relay por el servidor: todos ven a todos.
//
//  Arranque automático: cuando el usuario pulsa el botón Play nativo con
//  "Players" >= 2 en la barra del editor, se lanzan N instancias:
//    · la instancia nativa recibe "--glindex 1" vía ProjectSettings
//      "editor/run/main_run_args" (el editor los añade DESPUÉS de la escena,
//      así llegan como user args) y se vuelve el HOST,
//    · las demás se lanzan con --glindex 2..N y se conectan como clientes.
//  Las ventanas se acomodan en mosaico con título Player1..N para que se vean
//  todas (antes se apilaban en la misma posición y parecían menos ventanas).
//
//  Vista de Servidor: en sesiones multijugador cada ventana tiene un botón
//  "Server View" (como Roblox Studio) que suelta una cámara libre: WASD +
//  click derecho vuelan la cámara SIN mover al personaje.
//
//  Señales (estilo Roblox): PlayerConnected(peerId), PlayerDisconnected(peerId),
//           Connected(), ConnectionFailed().
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/e_net_multiplayer_peer.hpp>
#include <godot_cpp/classes/ip.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/sub_viewport_container.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/animatable_body3d.hpp>
#include <godot_cpp/classes/animatable_body2d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/collision_shape2d.hpp>
#include <godot_cpp/classes/capsule_shape3d.hpp>
#include <godot_cpp/classes/capsule_shape2d.hpp>
#include <godot_cpp/classes/capsule_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <vector>
#include <map>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#include "lua.h"
#include "lualib.h"
#include "gl_errors.h"
#include "gl_runtime.h"   // gl_state_alive, gl_freecam
#include "roblox_services.h"   // RobloxValue (replicar subtipo IntValue/StringValue)
#include "gl_debug.h"     // gl_debug_on (prints internos solo en Modo Debug)
#include "gl_avatar.h"    // personaje R6 + animador (muñecos remotos)

#include "gl_servicios.h"

using namespace godot;

// Prints internos del multijugador: SOLO con el Modo Debug activo (como el
// resto de la extension). Sin tildes: la consola de Windows los rompe.
inline void gl_mp_log(const String& s) {
    if (gl_debug_on()) UtilityFunctions::print(String("[GodotLuau MP] ") + s);
}

// ── Sesion de prueba por res:// ────────────────────────────────────────────
// El plugin del editor escribe res://GodotLuau/.gl_mp_session = "count|device|stamp".
// res:// es EXACTAMENTE la misma carpeta para el editor y para el juego
// lanzado desde el editor (corre sin pack), asi que este canal no puede
// fallar por rutas. count>=2 => la ventana NATIVA es el SERVIDOR; count==1
// el archivo solo transporta el dispositivo emulado.
inline bool gl_mp_session_read_ex(int& out_count, String& out_device, double& out_age) {
    Ref<FileAccess> f = FileAccess::open(GL_MP_SESSION_PATH, FileAccess::READ);
    if (f.is_null()) return false;
    PackedStringArray parts = f->get_as_text().strip_edges().split("|");
    f->close();
    if (parts.size() < 3) return false;
    int    cnt   = String(parts[0]).to_int();
    double stamp = String(parts[2]).to_float();
    double now   = Time::get_singleton()->get_unix_time_from_system();
    if (cnt < 1) return false;
    out_age = now - stamp;
    if (out_age > 21600.0) return false;   // 6 h: sesion de otro dia no cuenta
    out_count  = cnt;
    out_device = parts[1];
    return true;
}
inline bool gl_mp_session_read(int& out_count, String& out_device) {
    double age;
    return gl_mp_session_read_ex(out_count, out_device, age);
}

// Host al que se conectan las ventanas cliente del test local. Por defecto
// 127.0.0.1 (misma máquina); se puede apuntar a OTRA PC con el arg
// "--glhost <ip>" o el archivo res://GodotLuau/.gl_host, para probar entre máquinas.
inline String gl_mp_host() {
    PackedStringArray a = OS::get_singleton()->get_cmdline_user_args();
    for (int i = 0; i < a.size(); i++)
        if (String(a[i]) == "--glhost" && i + 1 < a.size()) return a[i + 1];
    Ref<FileAccess> f = gl_open_user_config(".gl_host");
    if (f.is_valid()) {
        String h = f->get_as_text().strip_edges();
        f->close();
        if (!h.is_empty()) return h;
    }
    return "127.0.0.1";
}

// ¿Debe auto-crearse el NetworkService al arrancar el juego?
// Siempre que el juego corra desde el editor (toggle de vista, dispositivo,
// sesiones) o que lleve --glindex (ventanas cliente).
inline bool gl_mp_autostart_requested() {
    PackedStringArray a = OS::get_singleton()->get_cmdline_user_args();
    for (int i = 0; i < a.size(); i++) {
        const String arg = a[i];
        // Juegos EXPORTADOS que se unen/hospedan por IP o por coordinador.
        if (arg == "--glindex" || arg == "--glconnect" || arg == "--glserver" ||
            arg == "--glhost"  || arg == "--glmatch"   || arg == "--glinstance") return true;
    }
    int c; String d;
    if (gl_mp_session_read(c, d)) return true;
    return OS::get_singleton()->has_feature("editor");
}

// ── Cámara libre de la Vista de Servidor (como la cámara de Roblox Studio) ──
//  WASD mueve, click derecho + mouse rota, rueda cambia la velocidad,
//  E/Espacio sube, Q/Ctrl baja. NO toca al personaje.
class GLFreeCamera : public Camera3D {
    GDCLASS(GLFreeCamera, Camera3D);
    float speed = 16.0f;
    float sens  = 0.0025f;
protected:
    static void _bind_methods() {}
public:
    void _input(const Ref<InputEvent>& e) override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        Input* in = Input::get_singleton();
        Ref<InputEventMouseButton> mb = e;
        if (mb.is_valid()) {
            if (mb->get_button_index() == MOUSE_BUTTON_RIGHT)
                in->set_mouse_mode(mb->is_pressed() ? Input::MOUSE_MODE_CAPTURED
                                                    : Input::MOUSE_MODE_VISIBLE);
            if (mb->get_button_index() == MOUSE_BUTTON_WHEEL_UP)   speed = Math::min(speed * 1.15f, 200.0f);
            if (mb->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN) speed = Math::max(speed / 1.15f, 1.0f);
        }
        Ref<InputEventMouseMotion> mm = e;
        if (mm.is_valid() && in->get_mouse_mode() == Input::MOUSE_MODE_CAPTURED) {
            Vector3 r = get_rotation();
            r.y -= mm->get_relative().x * sens;
            r.x  = Math::clamp(r.x - mm->get_relative().y * sens, -1.5f, 1.5f);
            set_rotation(r);
        }
    }
    void _process(double dt) override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        Input* in = Input::get_singleton();
        Basis b = gl_xf(this).basis;
        Vector3 dir;
        if (in->is_key_pressed(KEY_W)) dir -= b.get_column(2);
        if (in->is_key_pressed(KEY_S)) dir += b.get_column(2);
        if (in->is_key_pressed(KEY_D)) dir += b.get_column(0);
        if (in->is_key_pressed(KEY_A)) dir -= b.get_column(0);
        if (in->is_key_pressed(KEY_E) || in->is_key_pressed(KEY_SPACE)) dir += Vector3(0, 1, 0);
        if (in->is_key_pressed(KEY_Q) || in->is_key_pressed(KEY_CTRL))  dir -= Vector3(0, 1, 0);
        if (dir.length_squared() > 0.0001f)
            set_global_position(get_global_position() + dir.normalized() * speed * (float)dt);
    }
    GLFreeCamera() {}
};

// ── Vista previa de VR: la pantalla se divide en dos "ojos" ─────────────────
//  Dos SubViewports que comparten el mundo 3D del juego, cada uno con una
//  camara desplazada ~3.2 cm (distancia interpupilar) que sigue a la camara
//  activa del juego. Es una PREVIEW visual (sin lentes reales).
class GLVRPreview : public Control {
    GDCLASS(GLVRPreview, Control);
    SubViewport* eye_l = nullptr;
    SubViewport* eye_r = nullptr;
    Camera3D*    cam_l = nullptr;
    Camera3D*    cam_r = nullptr;
protected:
    static void _bind_methods() {}
public:
    HBoxContainer* hb = nullptr;
    void _ready() override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        hb = memnew(HBoxContainer);
        hb->add_theme_constant_override("separation", 4);
        add_child(hb);
        for (int i = 0; i < 2; i++) {
            SubViewportContainer* svc = memnew(SubViewportContainer);
            svc->set_stretch(true);
            svc->set_h_size_flags(Control::SIZE_EXPAND_FILL);
            svc->set_v_size_flags(Control::SIZE_EXPAND_FILL);
            hb->add_child(svc);
            SubViewport* sv = memnew(SubViewport);
            // Tamano inicial REAL: sin esto el primer render sale con un buffer
            // diminuto y el glow del look Roblox spamea errores de mipmaps.
            sv->set_size(Vector2i(600, 560));
            sv->set_update_mode(SubViewport::UPDATE_ALWAYS);
            svc->add_child(sv);
            Camera3D* c = memnew(Camera3D);
            sv->add_child(c);
            c->set_current(true);
            if (i == 0) { eye_l = sv; cam_l = c; } else { eye_r = sv; cam_r = c; }
        }
        // Compartir el mundo 3D del juego (mismas luces, mismo escenario)
        Ref<World3D> w = get_viewport()->find_world_3d();
        if (w.is_valid()) { eye_l->set_world_3d(w); eye_r->set_world_3d(w); }
    }
    void _process(double) override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        Viewport* root = get_viewport();
        if (!root) return;
        // Tamano forzado por codigo (los anchors dentro de un CanvasLayer pueden
        // quedarse en 0 el primer frame y el render de los ojos explota)
        Vector2 vs = root->get_visible_rect().size;
        if (vs.x >= 2.0f && !get_size().is_equal_approx(vs)) {
            set_position(Vector2());
            set_size(vs);
            if (hb) { hb->set_position(Vector2()); hb->set_size(vs); }
        }
        Camera3D* main_cam = root->get_camera_3d();
        if (!main_cam || !cam_l || !cam_r) return;
        Transform3D t = gl_xf(main_cam);
        Vector3 right = t.basis.get_column(0).normalized() * 0.032f;   // ~IPD/2
        cam_l->set_global_transform(Transform3D(t.basis, t.origin - right));
        cam_r->set_global_transform(Transform3D(t.basis, t.origin + right));
        cam_l->set_fov(main_cam->get_fov());
        cam_r->set_fov(main_cam->get_fov());
    }
    GLVRPreview() {}
};

#include "roblox_remote.h"   // RemoteEventNode (entrega de eventos que llegan por red)
#include "roblox_part.h"     // RobloxPart (física en red / Network Ownership 1.14.7)
#include "roblox_animation.h" // gl_anim_apply (animaciones replicadas 1.14.12)

class NetworkService : public Node {
    GDCLASS(NetworkService, Node);

    struct NCB { lua_State* L; int ref; bool active = true; };
    struct Puppet {
        Node*   node = nullptr;
        Vector3 tpos;                 // objetivo 3D
        Vector2 tpos2d;               // objetivo 2D
        float   tyaw = 0.0f;
        bool    has_target = false;
        // Estado de animacion REPLICADO por su dueño (1.14.17), no deducido aqui.
        float   anim_sp = 0.0f;
        bool    anim_air = false;
        // Animacion de caminar (bob del visual, como el test-anim del local)
        float   walk_phase = 0.0f;
        float   vis_base_y = 0.0f;
    };

    Ref<ENetMultiplayerPeer> peer;
    int  mode = 0;   // 0 = SinglePlayer, 1 = Host/Server, 2 = Client
    bool signals_bound = false;
    std::vector<NCB> pc_cbs, pd_cbs, conn_cbs, fail_cbs;

    // Auto-arranque local
    String device = "PC";
    int    player_index = 0;
    bool   net_connected = false;
    int    retry_left = 0;
    String target_address = "";   // "ip:puerto" del servidor al que se conectó (para GetServerAddress)
    bool   is_dedicated = false;   // servidor dedicado (sin jugador local, estilo minecraft_server.jar)
    double retry_timer = 0.0;

    // ── Coordinador / matchmaker (mundos/instancias estilo Roblox) ──────
    bool   is_coordinator = false;   // esta instancia REPARTE jugadores a mundos
    bool   is_instance    = false;   // esta instancia ES un mundo lanzado por el coordinador
    int    coord_port     = 0;       // puerto del coordinador
    int    coord_max      = 8;       // máximo de jugadores por mundo (como Roblox)
    String host_secret    = "";      // llave del dueño (solo él puede hostear)
    double coord_tick     = 0.0;     // temporizador de mantenimiento del coordinador
    double inst_hb_timer  = 0.0;     // temporizador de latido de la instancia
    String match_ip       = "";      // cliente: IP del coordinador para auto-join
    int    match_port     = 0;
    String reconnect_ip   = "";      // cliente: reconectar a la instancia asignada
    int    reconnect_port = 0;
    double reconnect_timer = 0.0;
    int    reconnect_try   = 0;

    // Réplica de jugadores
    bool   is_2d = false;
    double bcast_timer = 0.0;
    std::map<int, Puppet> puppets;   // peerId -> muñeco visible
    std::map<int, uint64_t> peer_player_ids;   // peerId remoto -> ObjectID del objeto Player

    // ── UserId secuencial e inmutable (asignado por el servidor) ─────────
    // Cada juego numera a sus jugadores por orden de entrada: host=1, luego
    // 2,3,... El servidor es autoridad; replica cada asignación a todos con
    // _gl_peer_uid para que todas las ventanas vean el mismo UserId por peer.
    std::map<int, int64_t> peer_seq_uid;   // peerId -> UserId asignado
    int64_t next_seq_uid = 2;              // servidor: siguiente UserId (host toma el 1)

    // ── RemoteFunction en vuelo (1.14.9.1) ──────────────────────────────
    // call_id -> a quién le pedimos la respuesta. Sirve para tres cosas: validar
    // que la respuesta viene del peer al que invocamos (si no, cualquier cliente
    // podría falsificar la respuesta de otro), fallar al invocador en el acto si
    // ese peer se cae, y purgar por edad lo que quede huérfano.
    struct RFPending { int peer; double age; };
    std::map<int64_t, RFPending> rf_expect;
    double rf_gc_timer = 0.0;

    // Última salud que difundió cada peer (1.14.11). El servidor la compara con
    // la de su copia para saber si un ServerScript la cambió y avisar al dueño.
    std::map<int, float> peer_net_health;
    double health_auth_timer = 0.0;

    // Animación activa de cada peer (1.14.12). Sirve para dos cosas: aplicarla en
    // cuanto aparece su muñeco (la animación puede llegar ANTES que el primer
    // estado de posición) y para que el servidor se la cuente a quien entra tarde
    // —si no, una animación en bucle sería invisible para el recién llegado—.
    struct AnimState { String id; String name; float speed = 1.0f; bool looped = false; bool play = false; };
    std::map<int, AnimState> peer_anim;

    // ── Replicación automática (1.14.5) ─────────────────────────────────
    // netId (asignado por el servidor) -> ObjectID del nodo replicado. En el
    // servidor se llena al replicar; en el cliente al recibir _rep_new.
    std::map<uint64_t, uint64_t> net_instances;
    uint64_t next_net_id_ctr = 1;   // servidor: contador de netId
    bool static_ids_done = false;   // los nodos del place ya llevan netId determinista (1.14.10)

    // ── Física en red / Network Ownership (1.14.7) ───────────────────────
    // Cada parte no anclada tiene un dueño (meta "_gl_owner": 0=servidor,
    // N=peer). La máquina AUTORIDAD simula y transmite su transform+velocidad;
    // las demás la congelan (kinematic) e interpolan al estado recibido.
    double phys_timer = 0.0;
    double phys_auto_timer = 0.0;   // reasignación de dueño Auto (jugador más cercano)
    struct PhysTarget { Transform3D xform; Vector3 lvel; Vector3 avel; bool has = false; double age = 0.0; };
    std::map<uint64_t, PhysTarget> phys_targets;   // netId → estado remoto a interpolar

    // Ping (medido cliente→servidor→cliente, en milisegundos)
    double ping_ms = -1.0;
    double ping_timer = 0.0;
    // Ping de CADA cliente, visto por el servidor (1.14.10). La medida la hace
    // el cliente (solo él conoce la ida y vuelta) y la reporta; el servidor la
    // guarda para que Player:GetNetworkPing() funcione desde un ServerScript.
    std::map<int, double> peer_ping_ms;
    // Reloj sincronizado (1.14.8): el cliente sincroniza su offset con el servidor.
    double time_sync_timer = 0.0;
    bool   time_synced = false;

    // Vista de Servidor (cámara libre) — comandada desde el panel Game del
    // editor vía res://GodotLuau/.gl_view_cmd ("server|N" / "client|N")
    uint64_t     freecam_id = 0;    // ObjectID de la cámara libre
    uint64_t     prevcam_id = 0;    // ObjectID de la cámara del jugador
    int          view_last_n = -1;  // último comando aplicado
    double       view_poll = 0.0;
    bool         view_watch = false;
    bool         boot_server_view = false;   // la ventana del servidor arranca en cámara libre
    double       xform_poll = 0.0;           // vigilancia de sesión para transformarse en servidor

    static Node* _find_by_class(Node* n, const char* cls);
    static Node* _find_by_name(Node* n, const char* nm);
    int _my_peer_id();
    bool _valid(Node* n) const { return n && UtilityFunctions::is_instance_valid(n); }

    Node* _local_player();
    Node* _workspace();
    Players* _players_service();
    PlayerObject* _local_player_obj();
    PlayerObject* _peer_player(int peer);
    PlayerObject* _ensure_peer_player(int peer);
    void _gl_peer_uid(int peer, int uid);
    void _name_local_player(int display_num);
    void _offset_spawn(int idx);
    void _remove_local_player();
    void _apply_device();
    void _apply_window_layout(int idx, int cnt);
    void _poll_view_cmd(double dt);
    Ref<MultiplayerAPI> _mp();
    void _bind_mp_signals();
    void _setup_rpcs();
    static void _fire(std::vector<NCB>& cbs, bool has_id, int id);
    Node3D* _make_puppet3d(int idx);
    Node2D* _make_puppet2d(int idx);
    void _ensure_puppet_humanoid(Node* pup);
    void _update_puppet3d(int key, int idx, Vector3 pos, float yaw, float hp, float sp, bool air);
    void _apply_puppet_health(Node* pup, int key, float hp);
    void _health_authority_tick();
    void _set_char_health(float hp);
    void _update_puppet2d(int key, int idx, Vector2 pos, float rot);
    void _on_puppet_gone(int key);
    void _remove_puppet(int key);
    void _clear_puppets();
    void _lerp_puppets(double dt);
    double diag_timer = 0.0;   // diagnostico TX/RX (1 linea/seg, solo Modo Debug)
    bool _diag_tick();
    void _broadcast_local();

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_on_peer_connected", "id"),    &NetworkService::_on_peer_connected);
        ClassDB::bind_method(D_METHOD("_on_peer_disconnected", "id"), &NetworkService::_on_peer_disconnected);
        ClassDB::bind_method(D_METHOD("_on_connected_ok"),            &NetworkService::_on_connected_ok);
        ClassDB::bind_method(D_METHOD("_on_connect_failed"),          &NetworkService::_on_connect_failed);
        ClassDB::bind_method(D_METHOD("_on_server_disconnected"),     &NetworkService::_on_server_disconnected);
        ClassDB::bind_method(D_METHOD("_auto_init"),                  &NetworkService::_auto_init);
        // RPCs de réplica
        ClassDB::bind_method(D_METHOD("_recv_state", "who", "pos", "yaw", "idx", "hp", "sp", "air"), &NetworkService::_recv_state);
        ClassDB::bind_method(D_METHOD("_relay_state", "pos", "yaw", "idx", "hp", "sp", "air"),      &NetworkService::_relay_state);
        ClassDB::bind_method(D_METHOD("_set_char_health", "hp"),                    &NetworkService::_set_char_health);
        ClassDB::bind_method(D_METHOD("net_anim_state", "character", "anim_id", "anim_name", "speed", "looped", "play"), &NetworkService::net_anim_state);
        ClassDB::bind_method(D_METHOD("_relay_anim", "anim_id", "anim_name", "speed", "looped", "play"),                 &NetworkService::_relay_anim);
        ClassDB::bind_method(D_METHOD("_recv_anim", "who", "anim_id", "anim_name", "speed", "looped", "play"),           &NetworkService::_recv_anim);
        ClassDB::bind_method(D_METHOD("_recv_state2d", "who", "pos", "rot", "idx"), &NetworkService::_recv_state2d);
        ClassDB::bind_method(D_METHOD("_relay_state2d", "pos", "rot", "idx"),       &NetworkService::_relay_state2d);
        ClassDB::bind_method(D_METHOD("_recv_remove", "who"),                       &NetworkService::_recv_remove);
        ClassDB::bind_method(D_METHOD("_on_puppet_gone", "key"),                    &NetworkService::_on_puppet_gone);
        ClassDB::bind_method(D_METHOD("gl_send_chat", "name", "text"),              &NetworkService::gl_send_chat);
        ClassDB::bind_method(D_METHOD("_relay_chat", "name", "text"),               &NetworkService::_relay_chat);
        ClassDB::bind_method(D_METHOD("_recv_chat", "from", "name", "text"),        &NetworkService::_recv_chat);
        ClassDB::bind_method(D_METHOD("_relay_cmd", "text"),                        &NetworkService::_relay_cmd);
        ClassDB::bind_method(D_METHOD("gl_send_cmd", "text"),                       &NetworkService::gl_send_cmd);
        ClassDB::bind_method(D_METHOD("_ping_req", "t"),                            &NetworkService::_ping_req);
        ClassDB::bind_method(D_METHOD("_ping_res", "t"),                            &NetworkService::_ping_res);
        ClassDB::bind_method(D_METHOD("_ping_report", "ms"),                        &NetworkService::_ping_report);
        ClassDB::bind_method(D_METHOD("net_ping_for_player", "player"),             &NetworkService::net_ping_for_player);
        ClassDB::bind_method(D_METHOD("net_kick_player", "player", "msg"),          &NetworkService::net_kick_player);
        ClassDB::bind_method(D_METHOD("_kicked", "msg"),                            &NetworkService::_kicked);
        ClassDB::bind_method(D_METHOD("net_set_team", "player", "team"),            &NetworkService::net_set_team);
        ClassDB::bind_method(D_METHOD("_set_team", "peer", "team_netid"),           &NetworkService::_set_team);
        ClassDB::bind_method(D_METHOD("net_load_character", "player"),              &NetworkService::net_load_character);
        ClassDB::bind_method(D_METHOD("_load_character"),                           &NetworkService::_load_character);
        ClassDB::bind_method(D_METHOD("_gl_disconnect", "ref", "vm"),               &NetworkService::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("disconnect_net"),                            &NetworkService::disconnect_net);   // para poder diferirlo (1.14.10 Finish)
        ClassDB::bind_method(D_METHOD("net_teleport_player", "player", "dest_port", "data"), &NetworkService::net_teleport_player);
        ClassDB::bind_method(D_METHOD("_gl_teleport", "dest_port", "data"),         &NetworkService::_gl_teleport);
        ClassDB::bind_method(D_METHOD("net_teleport_data"),                         &NetworkService::net_teleport_data);
        ClassDB::bind_method(D_METHOD("net_teleport_arrived"),                      &NetworkService::net_teleport_arrived);
        ClassDB::bind_method(D_METHOD("net_world_ports"),                           &NetworkService::net_world_ports);
        ClassDB::bind_method(D_METHOD("_time_req", "c_unix", "c_dgt"),              &NetworkService::_time_req);
        ClassDB::bind_method(D_METHOD("_time_res", "c_unix", "c_dgt", "s_unix", "s_dgt"), &NetworkService::_time_res);
        ClassDB::bind_method(D_METHOD("get_ping_ms"),                               &NetworkService::get_ping_ms);
        ClassDB::bind_method(D_METHOD("get_player_count"),                          &NetworkService::get_player_count);
        ClassDB::bind_method(D_METHOD("get_player_index"),                          &NetworkService::get_player_index);
        ClassDB::bind_method(D_METHOD("is_connected_net"),                          &NetworkService::is_connected_net);
        ClassDB::bind_method(D_METHOD("get_server_address"),                        &NetworkService::get_server_address);
        ClassDB::bind_method(D_METHOD("get_connection_state"),                      &NetworkService::get_connection_state);
        ClassDB::bind_method(D_METHOD("is_dedicated_server"),                       &NetworkService::is_dedicated_server);
        ClassDB::bind_method(D_METHOD("get_local_ip"),                              &NetworkService::get_local_ip);
        ClassDB::bind_method(D_METHOD("get_server_list"),                           &NetworkService::get_server_list);
        ClassDB::bind_method(D_METHOD("add_server", "name", "ip", "port"),          &NetworkService::add_server);
        ClassDB::bind_method(D_METHOD("remove_server", "name"),                     &NetworkService::remove_server);
        ClassDB::bind_method(D_METHOD("join_server", "name"),                       &NetworkService::join_server);
        ClassDB::bind_method(D_METHOD("_gl_assign", "iport"),                       &NetworkService::_gl_assign);
        ClassDB::bind_method(D_METHOD("start_coordinator", "port", "max"),          &NetworkService::start_coordinator);
        ClassDB::bind_method(D_METHOD("join_matchmaking", "ip", "port"),            &NetworkService::join_matchmaking);
        ClassDB::bind_method(D_METHOD("get_host_key"),                              &NetworkService::get_host_key);
        ClassDB::bind_method(D_METHOD("read_or_make_host_key"),                     &NetworkService::read_or_make_host_key);
        // Fase 2: estado de red + RemoteEvent por red + roster de jugadores
        ClassDB::bind_method(D_METHOD("is_server"),                                 &NetworkService::is_server);
        ClassDB::bind_method(D_METHOD("is_client"),                                 &NetworkService::is_client);
        ClassDB::bind_method(D_METHOD("get_mode"),                                  &NetworkService::get_mode);
        ClassDB::bind_method(D_METHOD("get_peer_id"),                               &NetworkService::get_peer_id);
        ClassDB::bind_method(D_METHOD("net_send_event", "path", "args", "target", "reliable"), &NetworkService::net_send_event);
        ClassDB::bind_method(D_METHOD("player_node_for_peer", "peer"),              &NetworkService::player_node_for_peer);
        ClassDB::bind_method(D_METHOD("peer_for_player_node", "player"),            &NetworkService::peer_for_player_node);
        ClassDB::bind_method(D_METHOD("get_players_roster"),                        &NetworkService::get_players_roster);
        ClassDB::bind_method(D_METHOD("_re_deliver_server", "path", "args"),        &NetworkService::_re_deliver_server);
        ClassDB::bind_method(D_METHOD("_re_deliver_client", "path", "args"),        &NetworkService::_re_deliver_client);
        ClassDB::bind_method(D_METHOD("_ure_deliver_server", "path", "args"),       &NetworkService::_ure_deliver_server);
        ClassDB::bind_method(D_METHOD("_ure_deliver_client", "path", "args"),       &NetworkService::_ure_deliver_client);
        // Replicación automática (1.14.5)
        ClassDB::bind_method(D_METHOD("_rep_new", "id", "gclass", "name", "parent_ref", "props"), &NetworkService::_rep_new);
        ClassDB::bind_method(D_METHOD("_rep_prop", "id", "key", "enc"),             &NetworkService::_rep_prop);
        ClassDB::bind_method(D_METHOD("_rep_reparent", "id", "parent_ref"),         &NetworkService::_rep_reparent);
        ClassDB::bind_method(D_METHOD("_rep_destroy", "id"),                        &NetworkService::_rep_destroy);
        ClassDB::bind_method(D_METHOD("_rep_attr", "id", "key", "v"),               &NetworkService::_rep_attr);
        ClassDB::bind_method(D_METHOD("_rep_tag", "id", "tag", "puesta"),           &NetworkService::_rep_tag);
        ClassDB::bind_method(D_METHOD("_gl_peer_uid", "peer", "uid"),               &NetworkService::_gl_peer_uid);
        // RemoteFunction por red (1.14.6)
        ClassDB::bind_method(D_METHOD("net_invoke_server", "path", "call_id", "args"),          &NetworkService::net_invoke_server);
        ClassDB::bind_method(D_METHOD("net_invoke_client", "peer", "path", "call_id", "args"),  &NetworkService::net_invoke_client);
        ClassDB::bind_method(D_METHOD("net_rf_respond", "reply_peer", "call_id", "ok", "rets"), &NetworkService::net_rf_respond);
        ClassDB::bind_method(D_METHOD("_rf_invoke_server", "path", "call_id", "args"),          &NetworkService::_rf_invoke_server);
        ClassDB::bind_method(D_METHOD("_rf_invoke_client", "path", "call_id", "args"),          &NetworkService::_rf_invoke_client);
        ClassDB::bind_method(D_METHOD("_rf_return", "call_id", "ok", "rets"),                   &NetworkService::_rf_return);
        ClassDB::bind_method(D_METHOD("net_mps_enviar", "peer", "tipo", "id", "estado"),        &NetworkService::net_mps_enviar);
        ClassDB::bind_method(D_METHOD("_mps_red", "tipo", "id", "estado"),                      &NetworkService::_mps_red);
        // Física en red / Network Ownership (1.14.7)
        ClassDB::bind_method(D_METHOD("net_set_owner", "net_id", "owner_peer", "is_auto"),      &NetworkService::net_set_owner);
        ClassDB::bind_method(D_METHOD("net_is_owner", "net_id"),                                &NetworkService::net_is_owner);
        ClassDB::bind_method(D_METHOD("net_node_by_id", "net_id"),                              &NetworkService::net_node_by_id);
        ClassDB::bind_method(D_METHOD("_phys_state", "net_id", "t", "lv", "av"),                &NetworkService::_phys_state);
        ClassDB::bind_method(D_METHOD("_phys_owned", "net_id", "t", "lv", "av"),                &NetworkService::_phys_owned);
        ClassDB::bind_method(D_METHOD("_phys_owner", "net_id", "owner_peer", "is_auto"),        &NetworkService::_phys_owner);
    }

    void _notification(int p_what) {
        if (p_what == NOTIFICATION_EXIT_TREE) _clear_puppets();
    }

public:
    void _on_peer_connected(int id);
    void _remove_peer_player(int id);
    void _on_peer_disconnected(int id);
    void _on_connected_ok();
    void _on_connect_failed()          { mode = 0; if (retry_left > 0) return; _fire(fail_cbs, false, 0); }
    void _on_server_disconnected();
    void net_send_event(const String& path, const Array& args, int target, bool reliable);
    void _deliver_event(const String& path, const Array& args, bool to_server);
    void _re_deliver_server(String path, Array args)  { _deliver_event(path, args, true); }
    void _re_deliver_client(String path, Array args)  { _deliver_event(path, args, false); }
    void _ure_deliver_server(String path, Array args) { _deliver_event(path, args, true); }
    void _ure_deliver_client(String path, Array args) { _deliver_event(path, args, false); }

    void _rf_fail_local(int64_t call_id, const String& msg);
    void net_invoke_server(String path, int call_id, Array args);
    void net_invoke_client(int peer, String path, int call_id, Array args);
    void _rf_send_return(int reply_peer, int call_id, bool ok, const Array& rets);
    void net_rf_respond(int reply_peer, int call_id, bool ok, Array rets);
    void _rf_send_error(int reply_peer, int call_id, const String& msg);
    void _rf_run_handler(lua_State* L, int handler_ref, Node* player, int call_id, int reply_peer, const Array& args, bool is_server);
    void _rf_invoke_server(String path, int call_id, Array args);
    void _rf_invoke_client(String path, int call_id, Array args);
    void _rf_return(int call_id, bool ok, Array rets);
    void net_mps_enviar(int peer, String tipo, int64_t id, int estado);
    void _mps_red(String tipo, int64_t id, int estado);
    void _rf_fail_pending(int peer, const String& msg);

    // ── Replicación automática de instancias/propiedades (1.14.5) ─────────
    // El SERVIDOR es autoridad: cuando un script del servidor crea una instancia
    // bajo un contenedor replicable (Workspace/ReplicatedStorage) o cambia una
    // propiedad de una ya replicada, difunde el DELTA a los clientes. Cada
    // instancia lleva un netId estable (meta "_gl_netid"). El mundo INICIAL
    // (puesto en el editor) NO se replica: cada ventana ya cargó el mismo place;
    // esto es una capa de deltas de runtime. Los envíos los dispara luau_api.h
    // (godot_object_newindex/method_destroy) llamando estos métodos por tipo.
    int64_t rep_next_id() { return (int64_t)(next_net_id_ctr++); }

    void _stamp_subtree(Node* n, int depth, int& count);
    void _mark_place_subtree(Node* n, int depth = 0);
    void _stamp_static_ids();
    void rep_map(int64_t id, Object* node);
    Node* _rep_node(uint64_t id);
    Node* _rep_node_red(uint64_t id);
    void _sellar(Node* n, uint64_t id);
    void _sellar_jugador(PlayerObject* po, int peer);
    Node* _rep_resolve_parent(const String& ref);
    bool _phys_authority(Node* part);
    bool net_is_owner(int64_t netId);
    // Resolver una instancia replicada por su netId (1.14.9: Instances por id en
    // los payloads de RemoteEvent/Function en vez de por ruta).
    Object* net_node_by_id(int64_t netId) { return _rep_node_red((uint64_t)netId); }
    void net_set_owner(int64_t netId, int owner_peer, bool is_auto);
    static void _red_soltar(RobloxPart* part);
    static void _red_congelar(RobloxPart* part);
    void _apply_owner(uint64_t netId, int owner_peer, bool is_auto);
    void _phys_owner(int64_t netId, int owner_peer, bool is_auto);
    void _phys_apply_remote(RobloxPart* part, uint64_t netId, const Transform3D& t, const Vector3& lv, const Vector3& av);
    void _phys_state(int64_t netId, Transform3D t, Vector3 lv, Vector3 av);
    void _phys_owned(int64_t netId, Transform3D t, Vector3 lv, Vector3 av);
    // Tick ~20Hz: la autoridad transmite sus partes no ancladas despiertas; los
    // demás las congelan (kinematic) para no simular contra el stream.
    // Posición del personaje de cada peer (culling + auto-ownership).
    struct PeerPos { int peer; Vector3 pos; };
    std::vector<PeerPos> _peer_positions();
    void _phys_send_culled(uint64_t netId, const Transform3D& t, const Vector3& lv, const Vector3& av,
                           const std::vector<PeerPos>& plrs, int skip_peer);
    void _phys_tick();
    void _phys_lerp(double dt);
    void _phys_auto_tick();
    static bool _rep_prop_type_ok(const Variant& v);
    void _rep_real_props(Node* n, Dictionary& out);
    // Solo lo que CAMBIÓ por script (para nodos del place, que el cliente ya tiene).
    Dictionary rep_build_shadow_props(Node* n) { return _rep_props(n, false); }
    // Estado completo (para instancias NUEVAS que el cliente tiene que crear).
    Dictionary rep_build_props(Node* n) { return _rep_props(n, true); }
    Dictionary _rep_props(Node* n, bool include_real);
    bool _rep_a_uno(int destino);
    void rep_send_new(int64_t id, const String& gclass, const String& name, const String& parent_ref, const Dictionary& props, int destino = 0);
    void rep_send_prop(int64_t id, const String& key, const Variant& enc, int destino = 0);
    void rep_send_reparent(int64_t id, const String& parent_ref, int destino = 0);
    void rep_send_attr(int64_t id, const String& key, const Variant& v, int destino = 0);
    void rep_send_tag(int64_t id, const String& tag, bool puesta, int destino = 0);
    int rep_destino_de(Node* n);
    void _rep_collect_forget(Node* n);
    void rep_unreplicate(Node* n);
    void _rep_sync_peer(int id);
    void _rep_new(int64_t id, String gclass, String name, String parent_ref, Dictionary props);
    void _rep_prop(int64_t id, String key, Variant enc);
    void _rep_attr(int64_t id, String key, Variant v);
    void _rep_tag(int64_t id, String tag, bool puesta);
    void _rep_marcas_nuevas(Node* n, Dictionary& props);
    void _rep_attr_aqui(Node* n, const String& k, const Variant& v);
    void _rep_tag_aqui(Node* n, const String& tag, bool puesta);
    void _rep_tags_aqui(Node* n, const PackedStringArray& quiero);
    void _rep_reparent(int64_t id, String parent_ref);
    void _rep_destroy(int64_t id);
    Node* player_node_for_peer(int peer_id);
    int peer_for_player_node(Node* player);
    TypedArray<Node> get_players_roster();

    // ── RPCs de réplica ──────────────────────────────────────────────────
    //
    // AUTORIDAD (1.16.13). Todos los RPC estan en RPC_MODE_ANY_PEER porque un
    // cliente tiene que poder hablarle al servidor. Pero ENet reenvia entre
    // peers (set_server_relay viene activado de fabrica), asi que un cliente
    // modificado tambien podia llamar los _recv_* DIRECTAMENTE en otro cliente
    // y hacerse pasar por el servidor: mover el muñeco de un tercero, borrar a
    // un jugador de la vista de todos o escribir en el chat con el nombre de
    // quien quisiera.
    //
    // Los _relay_* solo los acepta el SERVIDOR (van dirigidos a el) y los
    // _recv_* solo se aceptan si vienen del peer 1. Es la misma regla que ya
    // protegia _rep_new/_rep_prop/_rep_destroy y OnClientEvent.

    void _relay_state(Vector3 pos, float yaw, int idx, float hp, float sp, bool air);
    void _relay_state2d(Vector2 pos, float rot, int idx);
    void _recv_state(int who, Vector3 pos, float yaw, int idx, float hp, float sp, bool air);
    void _recv_state2d(int who, Vector2 pos, float rot, int idx);
    void _recv_remove(int who);
    void _show_chat(int peer, const String& name, const String& text);
    void gl_send_cmd(const String& text);
    void _relay_cmd(String text);
    void gl_send_chat(const String& name, const String& text);
    String _authoritative_peer_name(int peer);
    void _relay_chat(String name_cliente, String text);
    void _recv_chat(int from, String name, String text);
    void _ping_req(double t);
    void _ping_res(double t);
    void _ping_report(double ms);
    double net_ping_for_player(Object* player_node);
    void net_anim_state(Object* character, String anim_id, String anim_name, float speed, bool looped, bool play);
    void _relay_anim(String anim_id, String anim_name, float speed, bool looped, bool play);
    void _recv_anim(int who, String anim_id, String anim_name, float speed, bool looped, bool play);
    void _apply_anim_puppet(int who, const String& anim_id, const String& anim_name, float speed, bool looped, bool play);
    int net_teleport_player(Object* player_node, int dest_port, String data_json);
    void _gl_teleport(int dest_port, String data_json);
    // Lo consulta TeleportService:GetLocalPlayerTeleportData() en el destino.
    String net_teleport_data() { return gl_teleport_data(); }
    bool net_teleport_arrived();
    PackedInt32Array net_world_ports();
    void _strip_server_only();
    void net_load_character(Object* player_node);
    void _load_character();
    void _do_load_character();
    void net_set_team(Object* player_node, Object* team_node);
    void _apply_team(int peer, int64_t team_netid);
    void _set_team(int peer, int64_t team_netid);
    void net_kick_player(Object* player_node, String msg);
    void _kick_self(const String& msg);
    void _kicked(String msg);
    void _time_req(double c_unix, double c_dgt);
    void _time_res(double c_unix, double c_dgt, double s_unix, double s_dgt);
    double get_ping_ms();
    bool _set_view(bool server);

    void add_pc_cb(lua_State* L, int ref)   { pc_cbs.push_back({L, ref}); }
    void add_pd_cb(lua_State* L, int ref)   { pd_cbs.push_back({L, ref}); }
    void add_conn_cb(lua_State* L, int ref) { conn_cbs.push_back({L, ref}); }
    void add_fail_cb(lua_State* L, int ref) { fail_cbs.push_back({L, ref}); }
    void _gl_disconnect(int ref, int64_t vm);
    void _ready() override;
    void _process(double dt) override;
    void _auto_init();
    int    get_player_index() const { return player_index; }
    String get_device() const { return device; }

    bool start_server(int port, int max_players);
    bool connect_server(const String& ip, int port);
    void disconnect_net();
    bool is_server();
    bool is_client();
    int get_mode() const { return mode; }
    int get_peer_id() { return _my_peer_id(); }
    int get_player_count();
    bool is_dedicated_server() const { return is_dedicated; }

    String get_local_ip();

    // ── Lista de servidores guardados (estilo lista de Minecraft) ────────
    // Persistida en user://gl_servers.json: [{"name","ip","port"}].
    static String _servers_path() { return "user://gl_servers.json"; }
    Array get_server_list();
    void _save_server_list(const Array& list);
    void add_server(const String& name, const String& ip, int port);
    void remove_server(const String& name);
    bool join_server(const String& name);

    // ════════════════════════════════════════════════════════════════
    //  Coordinador / matchmaker (mundos independientes estilo Roblox)
    //  El dueño corre el COORDINADOR; al entrar un jugador, lo manda a un
    //  MUNDO (instancia = proceso aparte en otro puerto) con espacio, o
    //  arranca uno nuevo si todos están llenos (respetando el máximo).
    // ════════════════════════════════════════════════════════════════
    static String _host_key_path() { return "user://host.key"; }
    String _gen_key();
    String read_or_make_host_key();
    String get_host_key();

    struct InstInfo { int port; int players; };
    std::vector<InstInfo> _coord_read_instances();
    int _spawn_instance(int iport);
    int _coord_pick_or_spawn();
    bool start_coordinator(int port, int max_per_world);
    bool join_matchmaking(const String& ip, int port);
    void _gl_assign(int iport);
    void _inst_write_heartbeat();

    // ¿Hay una sesión de red activa? El host siempre; el cliente al conectar.
    bool is_connected_net() { return mode == 1 || (mode == 2 && net_connected); }
    // "ip:puerto" del servidor al que se conectó el cliente ("" si es host/single).
    String get_server_address() const { return target_address; }
    String get_connection_state();

    NetworkService() {}
};

// Los cuerpos de los metodos van por zonas:
#include "roblox_network_jugadores.h"
#include "roblox_network_servicios.h"
#include "roblox_network_replica.h"
#include "roblox_network_sesion.h"

#endif // ROBLOX_NETWORK_H

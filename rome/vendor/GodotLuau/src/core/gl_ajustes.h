#ifndef GL_AJUSTES_H
#define GL_AJUSTES_H

// ════════════════════════════════════════════════════════════════════════
//  LOS AJUSTES DEL JUGADOR, SIN MENU DE POR MEDIO
//
//  Calidad, tope de FPS, V-Sync, volumen, sensibilidad, pantalla completa,
//  bloqueo de camara... y su guardado en `user://gl_settings_playerN.cfg`.
//
//  Por que existe este archivo: todo esto vivia DENTRO de la clase C++ del
//  menu de Escape (GLSettingsMenu). O sea que los ajustes solo se cargaban y
//  se aplicaban porque alguien creaba ese menu — un nodo de interfaz — al
//  arrancar. Un ajuste guardado no es cosa de la interfaz: es del juego. Con
//  el menu ahora escrito en Luau (Modules/Menu, editable por el usuario), si
//  esto se hubiera quedado ahi dentro, quitar el menu viejo se habria llevado
//  por delante el guardado entero.
//
//  Y arregla de paso un agujero que ya estaba: el menu de Luau cambia los
//  ajustes por `Workspace:SetGraphicsQuality`, `SetMaxFPS`, `SetVSync`... y
//  NADIE los guardaba, porque el unico que escribia el .cfg era el menu de
//  C++, que ya no se abre. O sea que el jugador cambiaba la calidad y al
//  volver a entrar estaba igual que antes. Ahora cada setter avisa aqui.
// ════════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "gl_graphics.h"

using namespace godot;

// Los topes de FPS que se ofrecen, en orden. 0 = ilimitado.
static const int GL_FPS_OPCIONES[] = { 30, 60, 120, 144, 165, 240, 300, 360, 480, 0 };
static const int GL_FPS_N = (int)(sizeof(GL_FPS_OPCIONES) / sizeof(GL_FPS_OPCIONES[0]));

struct GLAjustes {
    int    quality     = 8;      // 1..10, como Roblox
    int    gfx_mode    = 0;      // 0 Automatic, 1 Manual, 2 Custom
    int    fps_idx     = 5;      // 240
    bool   vsync       = false;  // apagado a proposito (ver gl_apply_vsync)
    double volume      = 100.0;  // 0..100
    double sensitivity = 1.0;    // 0.2..3.0
    bool   fullscreen  = false;
    bool   cam_lock    = false;
    bool   invert_cam  = false;
    bool   shift_lock  = false;
    bool   show_fps    = false;
    bool   show_ping   = false;
    double bg_trans    = 0.96;   // opacidad del panel del menu
    int    jugador     = 0;      // indice de ventana en el multijugador local
};

inline GLAjustes& gl_ajustes() { static GLAjustes a; return a; }

inline String gl_ajustes_ruta() {
    return String("user://gl_settings_player")
         + String::num_int64(gl_ajustes().jugador) + ".cfg";
}

// Busca un nodo por clase bajo la raiz (para el personaje: sensibilidad y
// bloqueo de camara son suyos).
inline Node* gl_ajustes_buscar(Node* n, const char* cls) {
    if (!n) return nullptr;
    if (n->is_class(cls)) return n;
    for (int i = 0; i < n->get_child_count(); i++)
        if (Node* r = gl_ajustes_buscar(n->get_child(i), cls)) return r;
    return nullptr;
}

// Deja el mundo como dicen los ajustes. `any` es cualquier nodo del arbol.
inline void gl_ajustes_aplicar(Node* any) {
    GLAjustes& a = gl_ajustes();
    // El tope de FPS lo manda SOLO el jugador (0 = ilimitado).
    Engine::get_singleton()->set_max_fps(GL_FPS_OPCIONES[CLAMP(a.fps_idx, 0, GL_FPS_N - 1)]);
    // El V-Sync se aplica siempre, tambien al arrancar: el ajuste de proyecto
    // de Godot solo se lee al iniciar y viene ENCENDIDO, asi que sin esto los
    // FPS seguirian atados a la tasa de refresco del monitor.
    gl_apply_vsync(a.vsync, any);
    gl_quality_mode() = a.gfx_mode;
    // En Automatic se reaplica el nivel EFECTIVO y no el guardado: si no, tocar
    // el volumen daria un salto de calidad hacia atras.
    gl_apply_graphics_quality(a.gfx_mode == GL_QUALITY_AUTOMATIC ? gl_graphics_level() : a.quality, any);

    if (AudioServer* as = AudioServer::get_singleton()) {
        as->set_bus_mute(0, a.volume <= 0.5);
        as->set_bus_volume_db(0, (float)UtilityFunctions::linear_to_db(Math::max(a.volume, 0.5) / 100.0));
    }
    if (any && any->is_inside_tree()) {
        Node* raiz = (Node*)any->get_tree()->get_root();
        // El personaje se llama Character desde la 1.17; se prueban los dos
        // nombres para no romper una escena guardada con el viejo.
        Node* p = gl_ajustes_buscar(raiz, "Character");
        if (!p) p = gl_ajustes_buscar(raiz, "RobloxPlayer");
        if (p) {
            p->call("set_mouse_sensitivity", 0.0022 * a.sensitivity);
            if (p->has_method("set_lock_first_person")) p->call("set_lock_first_person", a.cam_lock);
        }
        if (a.fullscreen) {
            if (Window* w = any->get_window())
                w->set_mode(Window::MODE_EXCLUSIVE_FULLSCREEN);
        }
    }
}

inline void gl_ajustes_guardar() {
    const GLAjustes& a = gl_ajustes();
    Ref<ConfigFile> cf; cf.instantiate();
    cf->set_value("settings", "quality",     a.quality);
    cf->set_value("settings", "gfx_mode",    a.gfx_mode);
    cf->set_value("settings", "max_fps",     GL_FPS_OPCIONES[CLAMP(a.fps_idx, 0, GL_FPS_N - 1)]);
    cf->set_value("settings", "vsync",       a.vsync);
    cf->set_value("settings", "volume",      a.volume);
    cf->set_value("settings", "sensitivity", a.sensitivity);
    cf->set_value("settings", "fullscreen",  a.fullscreen);
    cf->set_value("settings", "cam_lock",    a.cam_lock);
    cf->set_value("settings", "invert_cam",  a.invert_cam);
    cf->set_value("settings", "shift_lock",  a.shift_lock);
    cf->set_value("settings", "show_fps",    a.show_fps);
    cf->set_value("settings", "show_ping",   a.show_ping);
    cf->set_value("settings", "bg_trans",    a.bg_trans);
    const GLCustomSettings& c = gl_custom();
    cf->set_value("settings", "c_render_scale",  c.render_scale);
    cf->set_value("settings", "c_upscaler",      c.upscaler);
    cf->set_value("settings", "c_sharpness",     c.sharpness);
    cf->set_value("settings", "c_shadow",        c.shadow_quality);
    cf->set_value("settings", "c_view_distance", c.view_distance);
    cf->set_value("settings", "c_view_studs",    c.view_studs);
    cf->set_value("settings", "c_ssao",          c.ssao);
    cf->set_value("settings", "c_glow",          c.glow);
    cf->set_value("settings", "c_fog",           c.fog);
    cf->set_value("settings", "c_occlusion",     c.occlusion);
    cf->set_value("settings", "c_batching",      c.static_batching);
    for (int lvl = 1; lvl <= 10; lvl++)
        cf->set_value("settings", "view_studs_" + String::num_int64(lvl), gl_view_studs(lvl));
    cf->save(gl_ajustes_ruta());
}

inline int gl_fps_indice_de(int fps) {
    for (int i = 0; i < GL_FPS_N; i++) if (GL_FPS_OPCIONES[i] == fps) return i;
    return 5;
}

inline void gl_ajustes_cargar() {
    GLAjustes& a = gl_ajustes();
    Ref<ConfigFile> cf; cf.instantiate();
    if (cf->load(gl_ajustes_ruta()) != OK) return;   // primera vez: valores por defecto
    a.quality     = CLAMP((int)(int64_t)cf->get_value("settings", "quality", 8), 1, 10);
    a.gfx_mode    = CLAMP((int)(int64_t)cf->get_value("settings", "gfx_mode", 0), 0, 2);
    const int fps = (int)(int64_t)cf->get_value("settings", "max_fps", -1);
    a.fps_idx     = fps < 0 ? 5 : gl_fps_indice_de(fps);
    a.vsync       = (bool)cf->get_value("settings", "vsync", false);
    a.volume      = CLAMP((double)cf->get_value("settings", "volume", 100.0), 0.0, 100.0);
    a.sensitivity = CLAMP((double)cf->get_value("settings", "sensitivity", 1.0), 0.2, 3.0);
    a.fullscreen  = (bool)cf->get_value("settings", "fullscreen", false);
    a.cam_lock    = (bool)cf->get_value("settings", "cam_lock", false);
    a.invert_cam  = (bool)cf->get_value("settings", "invert_cam", false);
    a.shift_lock  = (bool)cf->get_value("settings", "shift_lock", false);
    a.show_fps    = (bool)cf->get_value("settings", "show_fps", false);
    a.show_ping   = (bool)cf->get_value("settings", "show_ping", false);
    a.bg_trans    = CLAMP((double)cf->get_value("settings", "bg_trans", 0.96), 0.0, 1.0);
    // `d` es una struct recien construida (= los valores por defecto): NO puede
    // ser una referencia a gl_custom(), que es justo lo que se esta escribiendo.
    const GLCustomSettings d{};
    gl_custom_set("RenderScale",    (float)(double)cf->get_value("settings", "c_render_scale",  d.render_scale));
    gl_custom_set("Upscaler",       (float)(double)cf->get_value("settings", "c_upscaler",      (double)d.upscaler));
    gl_custom_set("Sharpness",      (float)(double)cf->get_value("settings", "c_sharpness",     d.sharpness));
    gl_custom_set("ShadowQuality",  (float)(double)cf->get_value("settings", "c_shadow",        (double)d.shadow_quality));
    gl_custom_set("ViewDistance",   (float)(double)cf->get_value("settings", "c_view_distance", d.view_distance));
    gl_custom_set("ViewDistanceStuds", (float)(double)cf->get_value("settings", "c_view_studs", d.view_studs));
    gl_custom_set("SSAO",           (bool)cf->get_value("settings", "c_ssao",      d.ssao)      ? 1.0f : 0.0f);
    gl_custom_set("Glow",           (bool)cf->get_value("settings", "c_glow",      d.glow)      ? 1.0f : 0.0f);
    gl_custom_set("Fog",            (bool)cf->get_value("settings", "c_fog",       d.fog)       ? 1.0f : 0.0f);
    gl_custom_set("Occlusion",      (bool)cf->get_value("settings", "c_occlusion", d.occlusion) ? 1.0f : 0.0f);
    gl_custom_set("StaticBatching", (bool)cf->get_value("settings", "c_batching",  d.static_batching) ? 1.0f : 0.0f);
    for (int lvl = 1; lvl <= 10; lvl++)
        gl_view_studs_set(lvl, (float)(double)cf->get_value(
            "settings", "view_studs_" + String::num_int64(lvl), gl_view_studs(lvl)));
}

// ── Guardado DIFERIDO ────────────────────────────────────────────────
// El menu de Luau mueve una barra y eso son decenas de escrituras seguidas.
// Escribir el .cfg en cada una seria absurdo, asi que se marca y lo escribe el
// tick del Workspace un poco despues.
inline bool& gl_ajustes_sucios() { static bool s = false; return s; }
inline void  gl_ajustes_cambiaron() { gl_ajustes_sucios() = true; }
inline void gl_ajustes_tick(double delta) {
    if (!gl_ajustes_sucios()) return;
    static double t = 0.0;
    t += delta;
    if (t < 0.75) return;
    t = 0.0;
    gl_ajustes_sucios() = false;
    gl_ajustes_guardar();
}

#endif // GL_AJUSTES_H

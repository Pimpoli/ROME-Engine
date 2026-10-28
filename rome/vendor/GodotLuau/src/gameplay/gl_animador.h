#ifndef GL_ANIMADOR_H
#define GL_ANIMADOR_H

// ════════════════════════════════════════════════════════════════════
//  Animacion por capas (F7): el Animator de Roblox.
//
//  · Un clip es una KeyframeSequence leida una vez (GLClipAnim): por cada
//    articulacion (el nombre de la Pose = el de la Part1 de su Motor6D) sus
//    claves con tiempo, CFrame y suavizado, y las marcas para las senales
//    (keyframes con nombre y KeyframeMarker).
//  · Un GLAnimador por rig: hijo interno del Humanoid o del
//    AnimationController (el Animator de dentro comparte el mismo). Cada frame
//    avanza sus pistas, mezcla por articulacion y escribe Motor6D.Transform.
//  · La mezcla de Roblox: gana la prioridad mas alta que mueva esa
//    articulacion; las de la misma prioridad se reparten por peso; si su peso
//    total no llega a 1 (un fundido a medias) el resto lo pone la siguiente
//    prioridad, y lo que quede, la pose de reposo. Core es la mas baja aunque
//    valga 1000.
//  · Una Pose con Weight 0 no es una clave: el editor de animaciones las deja
//    solo para colgar de ellas las de debajo (la de HumanoidRootPart).
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/animation_player.hpp>
#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/animation_library.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <memory>
#include <vector>
#include <algorithm>

#include "lua.h"
#include "lualib.h"
#include "gl_runtime.h"
#include "gl_asset.h"
#include "roblox_motor6d.h"

using namespace godot;

// ── Lectura de las instancias de animacion ───────────────────────────
// Las importadas llevan su clase y su nombre de Roblox en metas, y sus
// propiedades como meta con su nombre; las creadas por script, con `_glp_`.
inline String gl_anim_clase(const Node* n) {
    static const StringName k("__rbx_class");
    return n->has_meta(k) ? String(n->get_meta(k)) : String(n->get_class());
}
inline String gl_anim_nombre(const Node* n) {
    static const StringName k("__rbx_name");
    return n->has_meta(k) ? String(n->get_meta(k)) : String(n->get_name());
}
inline Variant gl_anim_prop(const Node* n, const char* k) {
    const StringName a(k);
    if (n->has_meta(a)) return n->get_meta(a);
    const StringName b(String("_glp_") + k);
    if (n->has_meta(b)) return n->get_meta(b);
    return n->get(a);
}

// ── El clip ──────────────────────────────────────────────────────────
struct GLClaveAnim { float t; Vector3 p; Quaternion q; int estilo; int direccion; };
struct GLPistaAnim { StringName hueso; std::vector<GLClaveAnim> claves; };
struct GLMarcaAnim { float t; String nombre; String valor; bool keyframe; };
struct GLClipAnim {
    float duracion = 0.0f;
    bool  bucle = false;
    int   prioridad = 2;   // Action, la de una KeyframeSequence nueva
    std::vector<GLPistaAnim> pistas;
    std::vector<GLMarcaAnim> marcas;   // por tiempo
};
using GLClipRef = std::shared_ptr<GLClipAnim>;

inline void _gl_anim_poses(Node* padre, float t, HashMap<StringName, int>& indice, GLClipAnim& c) {
    for (int i = 0; i < padre->get_child_count(); i++) {
        Node* p = padre->get_child(i);
        const String clase = gl_anim_clase(p);
        if (clase == "KeyframeMarker") {
            c.marcas.push_back({ t, gl_anim_nombre(p), String(gl_anim_prop(p, "Value")), false });
            continue;
        }
        if (clase != "Pose") continue;
        const Variant w = gl_anim_prop(p, "Weight");
        const float peso = w.get_type() == Variant::NIL ? 1.0f : (float)(double)w;
        if (peso > 0.0f) {
            const Variant v = gl_anim_prop(p, "CFrame");
            const Transform3D x = v.get_type() == Variant::TRANSFORM3D ? (Transform3D)v : Transform3D();
            const StringName hueso(gl_anim_nombre(p));
            const int* idx = indice.getptr(hueso);
            int i_pista;
            if (idx) i_pista = *idx;
            else {
                i_pista = (int)c.pistas.size();
                indice.insert(hueso, i_pista);
                c.pistas.push_back({ hueso, {} });
            }
            c.pistas[i_pista].claves.push_back({ t, x.origin, x.basis.get_rotation_quaternion(),
                                                 (int)(int64_t)gl_anim_prop(p, "EasingStyle"),
                                                 (int)(int64_t)gl_anim_prop(p, "EasingDirection") });
        }
        _gl_anim_poses(p, t, indice, c);
    }
}

inline GLClipRef gl_clip_leer(Node* kfs) {
    GLClipRef c = std::make_shared<GLClipAnim>();
    c->bucle = gl_anim_prop(kfs, "Loop").booleanize();
    const Variant pr = gl_anim_prop(kfs, "Priority");
    if (pr.get_type() == Variant::INT || pr.get_type() == Variant::FLOAT) c->prioridad = (int)(int64_t)pr;
    HashMap<StringName, int> indice;
    for (int i = 0; i < kfs->get_child_count(); i++) {
        Node* k = kfs->get_child(i);
        if (gl_anim_clase(k) != "Keyframe") continue;
        const float t = (float)(double)gl_anim_prop(k, "Time");
        c->duracion = MAX(c->duracion, t);
        const String nombre = gl_anim_nombre(k);
        if (nombre != "Keyframe") c->marcas.push_back({ t, nombre, String(), true });
        _gl_anim_poses(k, t, indice, *c);
    }
    for (GLPistaAnim& p : c->pistas)
        std::stable_sort(p.claves.begin(), p.claves.end(),
                         [](const GLClaveAnim& a, const GLClaveAnim& b) { return a.t < b.t; });
    std::stable_sort(c->marcas.begin(), c->marcas.end(),
                     [](const GLMarcaAnim& a, const GLMarcaAnim& b) { return a.t < b.t; });
    return c;
}

// Un clip por KeyframeSequence (por su ObjectID). Se vacia al descargar la
// extension (ver register_types.cpp).
inline HashMap<uint64_t, GLClipRef>& gl_clips_cache() { static HashMap<uint64_t, GLClipRef> m; return m; }
inline GLClipRef gl_clip_de_kfs(Node* kfs) {
    if (!kfs) return nullptr;
    const uint64_t id = (uint64_t)kfs->get_instance_id();
    if (GLClipRef* c = gl_clips_cache().getptr(id)) return *c;
    GLClipRef c = gl_clip_leer(kfs);
    gl_clips_cache().insert(id, c);
    return c;
}

// ── KeyframeSequenceProvider:RegisterKeyframeSequence → "rbxtemp://N" ──
inline HashMap<int64_t, ObjectID>& gl_kfs_registro() { static HashMap<int64_t, ObjectID> m; return m; }
inline String gl_kfs_registrar(Node* kfs) {
    static int64_t siguiente = 0;
    const int64_t n = ++siguiente;
    gl_kfs_registro().insert(n, ObjectID(kfs->get_instance_id()));
    return String("rbxtemp://") + String::num_int64(n);
}
inline void gl_clips_limpiar() { gl_clips_cache().clear(); gl_kfs_registro().clear(); }

// El numero de un id de Roblox: rbxassetid://123, http://www.roblox.com/asset/?id=123, "123".
inline int64_t gl_anim_numero(const String& id) {
    int fin = id.length();
    while (fin > 0 && !(id[fin - 1] >= '0' && id[fin - 1] <= '9')) fin--;
    int ini = fin;
    while (ini > 0 && id[ini - 1] >= '0' && id[ini - 1] <= '9') ini--;
    return fin > ini ? id.substr(ini, fin - ini).to_int() : 0;
}
// La raiz del juego (el DataModel) desde cualquier nodo de dentro.
inline Node* gl_anim_raiz_juego(Node* n) {
    static const StringName k("__rbx_class");
    Node* ultimo = nullptr;
    for (Node* a = n; a; a = a->get_parent()) {
        if ((a->has_meta(k) && String(a->get_meta(k)) == "DataModel") ||
            a->is_class("RobloxGame3D") || a->is_class("RobloxGame2D") || a->is_class("RobloxTemplate"))
            return a;
        if (a->get_parent() && !gl_cast<Window>(a->get_parent())) ultimo = a->get_parent();
    }
    return ultimo;
}
// La KeyframeSequence de un AnimationId: las registradas por script y las que
// trae el .gdlu en GDLU_Animaciones (exportadas desde Studio). Las demas viven
// en la nube de Roblox y aqui no hay forma de bajarlas.
inline Node* gl_kfs_de_id(const String& id, Node* ctx) {
    if (id.begins_with("rbxtemp://")) {
        const ObjectID* o = gl_kfs_registro().getptr(id.substr(10).to_int());
        return o ? gl_cast<Node>(ObjectDB::get_instance(*o)) : nullptr;
    }
    const int64_t n = gl_anim_numero(id);
    if (n <= 0 || !ctx) return nullptr;
    Node* juego = gl_anim_raiz_juego(ctx);
    Node* carpeta = juego ? juego->get_node_or_null(NodePath("GDLU_Animaciones")) : nullptr;
    Node* k = carpeta ? carpeta->get_node_or_null(NodePath(String::num_int64(n))) : nullptr;
    return (k && gl_anim_clase(k) == "KeyframeSequence") ? k : nullptr;
}

#include "gl_clips_integrados.h"   // las animaciones de los avatares R6/R15 (F8)

// ── Suavizados de las Pose ───────────────────────────────────────────
// PoseEasingStyle: Linear 0, Constant 1, Elastic 2, Cubic 3, Bounce 4,
// CubicV2 5. En los estilos viejos In y Out van al reves que en TweenService
// (CubicV2 lo arreglo); el suavizado de una clave va hasta la siguiente.
inline float gl_anim_rebote(float a) {
    const float n = 7.5625f, d = 2.75f;
    if (a < 1.0f / d) return n * a * a;
    if (a < 2.0f / d) { a -= 1.5f / d;   return n * a * a + 0.75f; }
    if (a < 2.5f / d) { a -= 2.25f / d;  return n * a * a + 0.9375f; }
    a -= 2.625f / d;
    return n * a * a + 0.984375f;
}
inline float gl_anim_suavizar(int estilo, int direccion, float a) {
    if (estilo == 1) return 0.0f;
    if (estilo != 2 && estilo != 3 && estilo != 4 && estilo != 5) return a;
    int dir = direccion;
    if (estilo != 5 && dir != 2) dir = 1 - dir;
    auto entra = [estilo](float x) -> float {
        if (estilo == 4) return 1.0f - gl_anim_rebote(1.0f - x);
        if (estilo == 2) {
            if (x <= 0.0f) return 0.0f;
            if (x >= 1.0f) return 1.0f;
            return -Math::pow(2.0f, 10.0f * x - 10.0f) * Math::sin((x * 10.0f - 10.75f) * (2.0f * (float)Math_PI / 3.0f));
        }
        return x * x * x;
    };
    if (dir == 0) return entra(a);
    if (dir == 1) return 1.0f - entra(1.0f - a);
    return a < 0.5f ? entra(a * 2.0f) * 0.5f : 1.0f - entra((1.0f - a) * 2.0f) * 0.5f;
}
inline void gl_anim_muestra(const GLPistaAnim& p, float t, Vector3& pos, Quaternion& rot) {
    const std::vector<GLClaveAnim>& c = p.claves;
    const size_t n = c.size();
    if (n == 1 || t <= c[0].t) { pos = c[0].p; rot = c[0].q; return; }
    if (t >= c[n - 1].t) { pos = c[n - 1].p; rot = c[n - 1].q; return; }
    size_t lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        if (c[mid].t <= t) lo = mid; else hi = mid;
    }
    const float d = c[hi].t - c[lo].t;
    float a = d > 1e-6f ? (t - c[lo].t) / d : 1.0f;
    a = gl_anim_suavizar(c[lo].estilo, c[lo].direccion, a);
    pos = c[lo].p.lerp(c[hi].p, a);
    rot = c[lo].q.slerp(c[hi].q, a);
}

// Lo que dura una animacion de la que no hay datos (un id de la nube que no
// vino en el .gdlu). Con 0 acababa en el frame siguiente y los bucles de los
// juegos ("ataca, espera su Stopped, aplasta") corrian a cada frame: el place
// de prueba disparaba oleadas de 42 clones por frame. Sin los datos no se
// sabe lo que dura la de verdad; 1 s es lo tipico de una accion.
static constexpr float GL_ANIM_SIN_DATOS = 1.0f;

// Orden de mezcla: Core 0 (la mas baja), Idle 1, Movement 2, Action 3 ... Action4 6.
inline int gl_anim_rango(int prioridad) {
    if (prioridad >= 1000) return 0;
    return CLAMP(prioridad, 0, 5) + 1;
}

class GLAnimador;

// ════════════════════════════════════════════════════════════════════
//  AnimationTrack — lo que devuelve LoadAnimation.
// ════════════════════════════════════════════════════════════════════
class AnimationTrack : public Node {
    GDCLASS(AnimationTrack, Node);

public:
    struct LuaCallback { lua_State* main_L; int ref; bool active = true; };
    struct LuaCallbackMarca { lua_State* main_L; int ref; bool active = true; String nombre; };

    std::vector<LuaCallback> ended_cbs;
    std::vector<LuaCallback> stopped_cbs;
    std::vector<LuaCallback> did_loop_cbs;
    std::vector<LuaCallback> keyframe_cbs;
    std::vector<LuaCallbackMarca> marca_cbs;

    GLClipRef clip;
    ObjectID  animador_id;
    ObjectID  animacion_id;     // la Animation que se paso a LoadAnimation
    String    anim_id;          // su AnimationId (para replicar)
    float     tiempo = 0.0f;
    float     velocidad = 1.0f;
    float     peso = 0.0f;      // WeightCurrent
    float     peso_objetivo = 1.0f;
    float     ritmo = 0.0f;     // cuanto peso cambia por segundo en el fundido
    float     fundido = 0.1f;   // el del ultimo Play: con el se va al acabar solo
    int       prioridad = 2;
    bool      bucle = false;
    bool      sonando = false;  // IsPlaying
    bool      activa = false;   // aun pesa en la mezcla (sonando o desvaneciendose)
    bool      primera = false;  // la primera vuelta tras Play cuenta las marcas del instante 0
    // Modo de antes: una Animation de Godot en un AnimationPlayer (.anim/.res locales).
    ObjectID  legado_ap;
    String    legado_nombre;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect", "ref", "vm"), &AnimationTrack::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("_gl_legado_fin", "nombre"), &AnimationTrack::_gl_legado_fin);
        ADD_SIGNAL(MethodInfo("Ended"));
        ADD_SIGNAL(MethodInfo("Stopped"));
    }

public:
    AnimationPlayer* gl_legado() const {
        return legado_ap.is_valid() ? gl_cast<AnimationPlayer>(ObjectDB::get_instance(legado_ap)) : nullptr;
    }
    bool gl_es_legado() const { return legado_ap.is_valid(); }
    GLAnimador* gl_animador() const;

    float gl_duracion() const {
        if (clip) return clip->duracion;
        AnimationPlayer* ap = gl_legado();
        if (ap && ap->has_animation(StringName(legado_nombre))) {
            Ref<Animation> a = ap->get_animation(StringName(legado_nombre));
            if (a.is_valid()) return (float)a->get_length();
        }
        return ap ? 0.0f : GL_ANIM_SIN_DATOS;
    }
    float gl_tiempo() const {
        if (!gl_es_legado()) return tiempo;
        AnimationPlayer* ap = gl_legado();
        return (ap && sonando) ? (float)ap->get_current_animation_position() : 0.0f;
    }
    void gl_poner_tiempo(float t) {
        tiempo = Math::clamp(t, 0.0f, gl_duracion());
        if (AnimationPlayer* ap = gl_legado()) if (sonando) ap->seek(tiempo, true);
    }

    void _hacia(float objetivo, float fade) {
        peso_objetivo = objetivo;
        if (fade <= 1e-4f) { peso = objetivo; ritmo = 0.0f; }
        else ritmo = Math::abs(objetivo - peso) / fade;
    }

    void gl_play(float fade, float w, float speed);
    void gl_stop(float fade);
    void gl_ajustar_peso(float w, float fade) { _hacia(MAX(w, 0.0f), fade); }
    void gl_ajustar_velocidad(float s) {
        velocidad = s;
        if (AnimationPlayer* ap = gl_legado()) ap->set_speed_scale(MAX(s, 0.0f));
    }
    void gl_poner_bucle(bool v) {
        bucle = v;
        AnimationPlayer* ap = gl_legado();
        if (ap && ap->has_animation(StringName(legado_nombre))) {
            Ref<Animation> a = ap->get_animation(StringName(legado_nombre));
            if (a.is_valid()) a->set_loop_mode(v ? Animation::LOOP_LINEAR : Animation::LOOP_NONE);
        }
    }
    float gl_tiempo_de_keyframe(const String& nombre) const {
        if (clip) for (const GLMarcaAnim& m : clip->marcas) if (m.keyframe && m.nombre == nombre) return m.t;
        return -1.0f;
    }

    // Avanza tiempo y fundido y dispara las senales. true si necesita mas frames.
    bool gl_avanzar(double dt);

    void remove_callbacks_for_state(lua_State* L) {
        for (auto& cb : ended_cbs)    if (cb.main_L == L) cb.active = false;
        for (auto& cb : stopped_cbs)  if (cb.main_L == L) cb.active = false;
        for (auto& cb : did_loop_cbs) if (cb.main_L == L) cb.active = false;
        for (auto& cb : keyframe_cbs) if (cb.main_L == L) cb.active = false;
        for (auto& cb : marca_cbs)    if (cb.main_L == L) cb.active = false;
    }
    void _gl_disconnect(int ref, int64_t vm) {
        for (auto& cb : ended_cbs)    if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
        for (auto& cb : stopped_cbs)  if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
        for (auto& cb : did_loop_cbs) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
        for (auto& cb : keyframe_cbs) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
        for (auto& cb : marca_cbs)    if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
    }

    template <typename T, typename F>
    void _disparar(std::vector<T>& lista, F empujar, int nargs) {
        if (lista.empty()) return;
        std::vector<T> copia = lista;   // un manejador puede conectar o desconectar
        for (T& cb : copia) {
            if (!cb.active || !gl_state_alive(cb.main_L)) continue;
            if (!empujar_filtro(cb)) continue;
            lua_State* th = lua_newthread(cb.main_L);
            lua_rawgeti(cb.main_L, LUA_REGISTRYINDEX, cb.ref);
            if (lua_isfunction(cb.main_L, -1)) {
                lua_xmove(cb.main_L, th, 1);
                empujar(th);
                gl_resume_handler(th, nargs);
            } else lua_pop(cb.main_L, 1);
            lua_pop(cb.main_L, 1);
        }
        for (int i = (int)lista.size() - 1; i >= 0; --i)
            if (!lista[i].active || !gl_state_alive(lista[i].main_L)) lista.erase(lista.begin() + i);
    }
    static bool empujar_filtro(const LuaCallback&) { return true; }
    bool empujar_filtro(const LuaCallbackMarca& cb) const { return cb.nombre == _marca_actual; }
    String _marca_actual;

    void _disparar_simple(std::vector<LuaCallback>& lista) { _disparar(lista, [](lua_State*) {}, 0); }
    void _marca(const GLMarcaAnim& m) {
        if (m.keyframe) {
            const String n = m.nombre;
            _disparar(keyframe_cbs, [n](lua_State* th) { lua_pushstring(th, n.utf8().get_data()); }, 1);
        } else {
            _marca_actual = m.nombre;
            const String v = m.valor;
            _disparar(marca_cbs, [v](lua_State* th) { lua_pushstring(th, v.utf8().get_data()); }, 1);
        }
    }

    void _net_notify(bool play_flag);
    void _gl_legado_fin(StringName nombre) {
        if (nombre != StringName(legado_nombre) || !sonando) return;
        sonando = false;
        activa = false;
        emit_signal("Stopped");
        emit_signal("Ended");
        _disparar_simple(stopped_cbs);
        _disparar_simple(ended_cbs);
    }
};

// ════════════════════════════════════════════════════════════════════
//  GLAnimador — el Animator de un rig.
// ════════════════════════════════════════════════════════════════════
class GLAnimador : public Node {
    GDCLASS(GLAnimador, Node);

public:
    struct LuaCallback { lua_State* main_L; int ref; bool active = true; };
    std::vector<LuaCallback> played_cbs;   // AnimationPlayed(track)

    LocalVector<ObjectID> pistas;          // todas las cargadas
    // Capa Core procedural (el animador R6 propio): articulacion -> Transform.
    HashMap<StringName, Transform3D> core;

private:
    struct Acum { Quaternion q = Quaternion(0, 0, 0, 0); Vector3 p; float w = 0.0f; };
    struct Junta { Acum r[7]; };
    HashMap<StringName, LocalVector<ObjectID>> juntas;   // nombre de la Part1 -> sus Motor6D
    float refresco = 0.0f;
    bool  juntas_al_dia = false;
    HashMap<StringName, Junta> mezcla;
    HashMap<StringName, bool> tocadas;     // las escritas el frame anterior

    static void _sumar(Acum& a, Quaternion q, const Vector3& p, float w) {
        if (a.w > 0.0f && a.q.dot(q) < 0.0f) q = -q;
        a.q = a.q + q * w;
        a.p += p * w;
        a.w += w;
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect", "ref", "vm"), &GLAnimador::_gl_disconnect);
    }

public:
    void _gl_disconnect(int ref, int64_t vm) {
        for (auto& cb : played_cbs) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
    }
    // El rig es el padre de su dueño (Humanoid o AnimationController), mirado
    // cada vez: el dueño puede cambiar de sitio despues de LoadAnimation.
    Node* gl_rig() const {
        Node* dueno = get_parent();
        return dueno ? dueno->get_parent() : nullptr;
    }

    void gl_juntas_rehacer() {
        juntas.clear();
        juntas_al_dia = true;
        refresco = 0.0f;
        Node* rig = gl_rig();
        if (!rig) return;
        std::vector<Node*> pila{ rig };
        while (!pila.empty()) {
            Node* n = pila.back();
            pila.pop_back();
            for (int i = 0; i < n->get_child_count(); i++) pila.push_back(n->get_child(i));
            if (Motor6D* m = gl_cast<Motor6D>(n))
                if (Node* p1 = m->get_part1()) {
                    const StringName nombre(gl_anim_nombre(p1));
                    LocalVector<ObjectID>* l = juntas.getptr(nombre);
                    if (!l) { juntas.insert(nombre, LocalVector<ObjectID>()); l = juntas.getptr(nombre); }
                    l->push_back(ObjectID(m->get_instance_id()));
                }
        }
    }
    bool gl_tiene_junta(const StringName& nombre) {
        if (!juntas_al_dia) gl_juntas_rehacer();
        return juntas.has(nombre);
    }

    void gl_pista_empieza(AnimationTrack* t);   // AnimationPlayed y a trabajar
    void gl_despertar() { juntas_al_dia = false; set_process(true); }

    // Las 256 de Roblox: pasado el tope se sueltan las mas viejas que ya no suenan.
    void gl_anadir(AnimationTrack* t) {
        pistas.push_back(ObjectID(t->get_instance_id()));
        if (pistas.size() <= 256) return;
        for (uint32_t i = 0; i < pistas.size() && pistas.size() > 256; ) {
            AnimationTrack* v = gl_cast<AnimationTrack>(ObjectDB::get_instance(pistas[i]));
            if (!v) { pistas.remove_at(i); continue; }
            if (!v->activa && v != t) { pistas.remove_at(i); v->queue_free(); continue; }
            i++;
        }
    }

    void _process(double dt) override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        // 1) Avanzar. Sobre una copia: un manejador puede cargar o parar otras.
        const LocalVector<ObjectID> ids = pistas;
        bool alguna = false;
        for (const ObjectID& id : ids) {
            AnimationTrack* t = gl_cast<AnimationTrack>(ObjectDB::get_instance(id));
            if (t && t->activa && t->gl_avanzar(dt)) alguna = true;
        }
        // 2) Mezclar por articulacion y prioridad.
        if (!juntas_al_dia || (refresco += (float)dt) > 1.0f) gl_juntas_rehacer();
        mezcla.clear();
        for (const ObjectID& id : pistas) {
            AnimationTrack* t = gl_cast<AnimationTrack>(ObjectDB::get_instance(id));
            if (!t || !t->activa || !t->clip || t->peso <= 0.0f) continue;
            const int rango = gl_anim_rango(t->prioridad);
            for (const GLPistaAnim& p : t->clip->pistas) {
                if (p.claves.empty() || !juntas.has(p.hueso)) continue;
                Vector3 pos;
                Quaternion rot;
                gl_anim_muestra(p, t->tiempo, pos, rot);
                Junta* j = mezcla.getptr(p.hueso);
                if (!j) { mezcla.insert(p.hueso, Junta()); j = mezcla.getptr(p.hueso); }
                _sumar(j->r[rango], rot, pos, t->peso);
            }
        }
        for (const KeyValue<StringName, Transform3D>& kv : core) {
            if (!juntas.has(kv.key)) continue;
            Junta* j = mezcla.getptr(kv.key);
            if (!j) { mezcla.insert(kv.key, Junta()); j = mezcla.getptr(kv.key); }
            _sumar(j->r[0], kv.value.basis.get_rotation_quaternion(), kv.value.origin, 1.0f);
        }
        // 3) Escribir Motor6D.Transform; las que se quedan sin nadie, a reposo.
        HashMap<StringName, bool> nuevas;
        for (const KeyValue<StringName, Junta>& kv : mezcla) {
            float queda = 1.0f;
            Acum fin;
            for (int r = 6; r >= 0 && queda > 1e-4f; r--) {
                const Acum& a = kv.value.r[r];
                if (a.w <= 0.0f) continue;
                const float toma = MIN(a.w, 1.0f) * queda;
                _sumar(fin, a.q.normalized(), a.p / a.w, toma);
                queda -= toma;
            }
            if (queda > 1e-4f) _sumar(fin, Quaternion(), Vector3(), queda);
            const Transform3D xf(Basis(fin.q.normalized()), fin.w > 0.0f ? fin.p / fin.w : Vector3());
            if (const LocalVector<ObjectID>* l = juntas.getptr(kv.key))
                for (const ObjectID& mid : *l)
                    if (Motor6D* m = gl_cast<Motor6D>(ObjectDB::get_instance(mid))) m->set_transform(xf);
            nuevas.insert(kv.key, true);
        }
        for (const KeyValue<StringName, bool>& kv : tocadas) {
            if (nuevas.has(kv.key)) continue;
            if (const LocalVector<ObjectID>* l = juntas.getptr(kv.key))
                for (const ObjectID& mid : *l)
                    if (Motor6D* m = gl_cast<Motor6D>(ObjectDB::get_instance(mid))) m->set_transform(Transform3D());
        }
        tocadas = nuevas;
        if (!alguna && core.is_empty() && tocadas.is_empty()) set_process(false);
    }

    GLAnimador() { set_meta("_gl_interno", true); }
};

inline GLAnimador* AnimationTrack::gl_animador() const {
    return animador_id.is_valid() ? gl_cast<GLAnimador>(ObjectDB::get_instance(animador_id)) : nullptr;
}

inline void AnimationTrack::gl_play(float fade, float w, float speed) {
    fundido = MAX(fade, 0.0f);
    velocidad = speed;
    if (AnimationPlayer* ap = gl_legado()) {
        if (!ap->has_animation(StringName(legado_nombre))) return;
        gl_poner_bucle(bucle);
        ap->set_speed_scale(MAX(speed, 0.0f));
        ap->play(StringName(legado_nombre));
        sonando = true;
        activa = true;
        peso = peso_objetivo = MAX(w, 0.0f);
        if (GLAnimador* a = gl_animador()) a->gl_pista_empieza(this);
        _net_notify(true);
        return;
    }
    // Play sobre una que ya suena la vuelve a empezar, como en Roblox.
    tiempo = (speed < 0.0f && clip) ? clip->duracion : 0.0f;
    primera = true;
    _hacia(MAX(w, 0.0f), fundido);
    sonando = true;
    activa = true;
    if (GLAnimador* a = gl_animador()) a->gl_pista_empieza(this);
    _net_notify(true);
}

inline void AnimationTrack::gl_stop(float fade) {
    if (AnimationPlayer* ap = gl_legado()) {
        if (!sonando) return;
        ap->stop();
        sonando = false;
        activa = false;
        emit_signal("Stopped");
        _disparar_simple(stopped_cbs);
        _net_notify(false);
        emit_signal("Ended");
        _disparar_simple(ended_cbs);
        return;
    }
    if (!sonando && !activa) return;
    const bool estaba = sonando;
    sonando = false;
    _hacia(0.0f, MAX(fade, 0.0f));
    if (estaba) {
        emit_signal("Stopped");
        _disparar_simple(stopped_cbs);
        _net_notify(false);
    }
    if (peso <= 0.0f && activa) {
        activa = false;
        emit_signal("Ended");
        _disparar_simple(ended_cbs);
    }
}

inline bool AnimationTrack::gl_avanzar(double dt) {
    if (!activa) return false;
    if (gl_es_legado()) return sonando;
    // Fundido del peso.
    if (peso != peso_objetivo) {
        const float paso = ritmo * (float)dt;
        if (ritmo <= 0.0f) peso = peso_objetivo;
        else if (peso < peso_objetivo) peso = MIN(peso + paso, peso_objetivo);
        else peso = MAX(peso - paso, peso_objetivo);
    }
    if (!sonando) {
        if (peso <= 0.0f) {
            activa = false;
            emit_signal("Ended");
            _disparar_simple(ended_cbs);
            return false;
        }
        // Mientras se desvanece sigue moviendose.
        if (clip && clip->duracion > 0.0f) {
            float t = tiempo + (float)dt * velocidad;
            if (bucle) {
                while (t >= clip->duracion) t -= clip->duracion;
                while (t < 0.0f) t += clip->duracion;
            }
            tiempo = Math::clamp(t, 0.0f, clip->duracion);
        }
        return true;
    }
    // Sin datos (la animacion no esta disponible aqui): no mueve nada y dura
    // GL_ANIM_SIN_DATOS. La que no se repite acaba al cumplirlos, para que
    // quien espera su Stopped siga a un ritmo como el de Roblox. La que se
    // repite sigue "sonando" para IsPlaying pero no pide frames: no hay nada
    // que mover (en un place importado son casi todas las de los NPC).
    if (!clip) {
        if (bucle) return activa && peso != peso_objetivo;
        tiempo += (float)dt * MAX(velocidad, 0.0f);
        if (tiempo >= GL_ANIM_SIN_DATOS || velocidad <= 0.0f) {
            tiempo = GL_ANIM_SIN_DATOS;
            gl_stop(fundido);
        }
        return activa;
    }
    const float dur = clip->duracion;
    const float t0 = tiempo;
    float t1 = tiempo + (float)dt * velocidad;
    const bool incluir_t0 = primera;
    primera = false;
    // Marcas en (desde, hasta], o [desde, hasta] la primera vez.
    auto marcas = [&](float desde, float hasta, bool incl) {
        for (const GLMarcaAnim& m : clip->marcas) {
            if (!sonando) return;
            if ((incl ? m.t >= desde : m.t > desde) && m.t <= hasta) _marca(m);
        }
    };
    // Una sola pose: se queda puesta si es un bucle; si no, acaba ya.
    if (dur <= 0.0f) {
        if (incluir_t0) marcas(0.0f, 0.0f, true);
        if (!bucle && sonando) gl_stop(fundido);
        return activa;
    }
    if (velocidad >= 0.0f) {
        if (t1 >= dur) {
            marcas(t0, dur, incluir_t0);
            if (!sonando) return activa;
            if (bucle) {
                while (t1 >= dur) t1 -= dur;
                tiempo = t1;
                _disparar_simple(did_loop_cbs);
                marcas(0.0f, t1, true);
            } else {
                tiempo = dur;
                gl_stop(fundido);
            }
            return activa;
        }
        marcas(t0, t1, incluir_t0);
    } else {
        if (t1 <= 0.0f) {
            for (int i = (int)clip->marcas.size() - 1; i >= 0 && sonando; i--)
                if (clip->marcas[i].t < t0 || (incluir_t0 && clip->marcas[i].t <= t0)) _marca(clip->marcas[i]);
            if (!sonando) return activa;
            if (bucle) {
                while (t1 <= 0.0f) t1 += dur;
                tiempo = t1;
                _disparar_simple(did_loop_cbs);
            } else {
                tiempo = 0.0f;
                gl_stop(fundido);
            }
            return activa;
        }
        for (int i = (int)clip->marcas.size() - 1; i >= 0 && sonando; i--) {
            const float t = clip->marcas[i].t;
            if ((incluir_t0 ? t <= t0 : t < t0) && t >= t1) _marca(clip->marcas[i]);
        }
    }
    if (sonando) tiempo = t1;
    return activa;
}

inline void AnimationTrack::_net_notify(bool play_flag) {
    if (!gl_net_service_id()) return;   // sin sesion de red: nada que replicar
    Object* ns = ObjectDB::get_instance(gl_net_service_id());
    GLAnimador* a = gl_animador();
    Node* rig = a ? a->gl_rig() : nullptr;
    if (!ns || !rig || anim_id.is_empty()) return;
    ns->call("net_anim_state", (Object*)rig, anim_id, String(get_name()), velocidad, bucle, play_flag);
}

inline void GLAnimador::gl_pista_empieza(AnimationTrack* t) {
    juntas_al_dia = false;
    set_process(true);
    if (played_cbs.empty()) return;
    std::vector<LuaCallback> copia = played_cbs;
    for (LuaCallback& cb : copia) {
        if (!cb.active || !gl_state_alive(cb.main_L)) continue;
        lua_State* th = lua_newthread(cb.main_L);
        lua_rawgeti(cb.main_L, LUA_REGISTRYINDEX, cb.ref);
        if (lua_isfunction(cb.main_L, -1)) {
            lua_xmove(cb.main_L, th, 1);
            gl_wrap_node(th, t);
            gl_resume_handler(th, 1);
        } else lua_pop(cb.main_L, 1);
        lua_pop(cb.main_L, 1);
    }
}

// ── El animador de un rig ────────────────────────────────────────────
// Uno por Humanoid o AnimationController (el Animator que lleven dentro usa el
// mismo: si no, Humanoid:LoadAnimation y Animator:LoadAnimation mezclarian por
// separado y se pisarian). El rig es el padre de ese dueño.
inline GLAnimador* gl_animador_de(Node* n, bool crear) {
    if (!n) return nullptr;
    Node* dueno = n;
    if (gl_anim_clase(n) == "Animator" && n->get_parent()) dueno = n->get_parent();
    for (int i = 0; i < dueno->get_child_count(true); i++)
        if (GLAnimador* a = gl_cast<GLAnimador>(dueno->get_child(i, true))) return a;
    if (!crear) return nullptr;
    GLAnimador* a = memnew(GLAnimador);
    a->set_name("GLAnimador");
    dueno->add_child(a, false, Node::INTERNAL_MODE_BACK);
    a->set_process(false);
    return a;
}

// El Animator que tiene todo Humanoid de Roblox (contenedor con su clase; el
// animador del rig cuelga del Humanoid y lo comparten).
inline void gl_asegurar_animator(Node* hum) {
    if (!hum) return;
    for (int i = 0; i < hum->get_child_count(); i++)
        if (gl_anim_clase(hum->get_child(i)) == "Animator") return;
    Node* a = gl_cast<Node>(ClassDB::instantiate(StringName("Folder")));
    if (!a) return;
    a->set_name("Animator");
    a->set_meta("__rbx_class", String("Animator"));
    hum->add_child(a);
}

// Una Animation de Godot (.anim/.res local) para el modo de antes. nullptr si no hay.
inline AnimationPlayer* gl_anim_legado_preparar(Node* rig, const String& aid, const String& nombre) {
    if (!rig || aid.is_empty()) return nullptr;
    String ruta = aid;
    if (gl_is_roblox_asset(aid)) ruta = gl_local_asset_path(aid);
    ResourceLoader* rl = ResourceLoader::get_singleton();
    if (ruta.is_empty() || !rl || !rl->exists(ruta, "Animation")) return nullptr;
    AnimationPlayer* ap = nullptr;
    for (int i = 0; i < rig->get_child_count() && !ap; i++) ap = gl_cast<AnimationPlayer>(rig->get_child(i));
    if (!ap) {
        ap = memnew(AnimationPlayer);
        ap->set_name("AnimationPlayer");
        rig->add_child(ap);
    }
    if (!ap->has_animation(StringName(nombre))) {
        Ref<Animation> a = rl->load(ruta, "Animation");
        if (a.is_null()) return nullptr;
        Ref<AnimationLibrary> lib;
        if (ap->has_animation_library(StringName(""))) lib = ap->get_animation_library(StringName(""));
        if (lib.is_null()) { lib.instantiate(); ap->add_animation_library(StringName(""), lib); }
        lib->add_animation(StringName(nombre), a);
    }
    return ap;
}

// La del AnimationPlayer que ya tenga el rig con ese nombre (humanoid:LoadAnimation("walk")).
inline AnimationPlayer* gl_anim_legado_por_nombre(Node* rig, const String& nombre) {
    if (!rig || nombre.is_empty()) return nullptr;
    for (int i = 0; i < rig->get_child_count(); i++)
        if (AnimationPlayer* ap = gl_cast<AnimationPlayer>(rig->get_child(i)))
            if (ap->has_animation(StringName(nombre))) return ap;
    return nullptr;
}

// LoadAnimation: una pista nueva en el animador del rig. Primero una
// KeyframeSequence (registrada o del .gdlu); si no, una Animation de Godot
// (el modo de antes); si no, una pista sin datos que no mueve nada.
inline AnimationTrack* gl_cargar_animacion(Node* dueno, Node* animacion, const String& aid,
                                           const String& nombre, const String& nombre_legado) {
    GLAnimador* a = gl_animador_de(dueno, true);
    if (!a) return nullptr;
    AnimationTrack* t = memnew(AnimationTrack);
    t->set_name(nombre.is_empty() ? String("Animation") : nombre);
    t->animador_id = ObjectID(a->get_instance_id());
    if (animacion) t->animacion_id = ObjectID(animacion->get_instance_id());
    t->anim_id = aid;
    if (Node* kfs = gl_kfs_de_id(aid, dueno)) t->clip = gl_clip_de_kfs(kfs);
    // Las animaciones por defecto de Roblox: las de los avatares de aqui.
    if (!t->clip) t->clip = gl_clip_integrado_de_id(aid);
    if (!t->clip) {
        AnimationPlayer* ap = gl_anim_legado_preparar(a->gl_rig(), aid, nombre_legado);
        if (!ap) ap = gl_anim_legado_por_nombre(a->gl_rig(), nombre_legado);
        if (ap) {
            t->legado_ap = ObjectID(ap->get_instance_id());
            t->legado_nombre = nombre_legado;
            ap->connect("animation_finished", Callable(t, "_gl_legado_fin"));
        } else {
            // Ultimo recurso: la del mismo estado del avatar, si la pide un Animate.
            t->clip = gl_clip_integrado_de_ranura(animacion, dueno);
        }
    }
    if (t->clip) {
        t->prioridad = t->clip->prioridad;
        t->bucle = t->clip->bucle;
    }
    a->add_child(t, false, Node::INTERNAL_MODE_BACK);
    a->gl_anadir(t);
    return t;
}

#endif // GL_ANIMADOR_H

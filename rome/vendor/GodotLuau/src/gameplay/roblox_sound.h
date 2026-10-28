#ifndef ROBLOX_SOUND_H
#define ROBLOX_SOUND_H

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/audio_stream_mp3.hpp>
#include <godot_cpp/classes/audio_stream_ogg_vorbis.hpp>
#include <godot_cpp/classes/audio_stream_wav.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include "gl_asset.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <vector>
#include <cstring>
#include <cmath>

#include "lua.h"
#include "lualib.h"
#include "gl_runtime.h"   // gl_fire_cbs: las señales del sonido

using namespace godot;

inline float gl_db(float v) { return 20.0f * log10f(Math::clamp(v, 1e-6f, 10.0f)); }

// ── Buses con nombre propio ──────────────────────────────────────────
// Cada SoundGroup y cada Sound con SoundEffects tiene su bus. Se crean al
// entrar y se quitan al salir; los reproductores los buscan por NOMBRE, asi que
// quitar uno no descoloca a los demas.
inline int gl_bus_crear(const String& nombre, const String& envia_a) {
    AudioServer* as = AudioServer::get_singleton();
    if (!as || nombre.is_empty()) return -1;
    int idx = as->get_bus_index(nombre);
    if (idx < 0) {
        idx = as->get_bus_count();
        as->add_bus();
        as->set_bus_name(idx, nombre);
    }
    if (idx > 0) as->set_bus_send(idx, envia_a.is_empty() ? StringName("Master") : StringName(envia_a));
    return idx;
}
inline void gl_bus_quitar(const String& nombre) {
    AudioServer* as = AudioServer::get_singleton();
    if (!as || nombre.is_empty() || nombre == "Master") return;
    const int idx = as->get_bus_index(nombre);
    if (idx > 0) as->remove_bus(idx);
}

// ── Sonidos sueltos: PlayOnRemove y SoundService:PlayLocalSound ─────
// Un reproductor de un solo uso colgado de la raiz, que se borra al acabar.
// Se encolan y los lanza el Workspace en su frame: un Sound que sale del arbol
// no puede saber si es para siempre (Destroy, Parent = nil) o si vuelve a
// entrar en otro sitio, y al cerrar el juego tampoco hay a quien colgarselo.
struct GLSonidoSuelto {
    Ref<AudioStream> stream;
    float    vol_db = 0.0f, pitch = 1.0f;
    String   bus;
    bool     es3d = false;
    Vector3  pos;
    float    unit_size = 10.0f, max_dist = 10000.0f;
    int      atenuacion = 0;
    uint64_t vigilar = 0;   // PlayOnRemove: solo suena si este Sound sigue fuera
};
inline LocalVector<GLSonidoSuelto>& gl_sonidos_sueltos() {
    static LocalVector<GLSonidoSuelto> v;
    return v;
}
inline void gl_sonidos_sueltos_drenar(Node* alguien_en_arbol) {
    LocalVector<GLSonidoSuelto>& v = gl_sonidos_sueltos();
    if (v.is_empty() || !alguien_en_arbol || !alguien_en_arbol->is_inside_tree()) return;
    Node* raiz = alguien_en_arbol->get_tree()->get_root();
    for (uint32_t i = 0; i < v.size(); i++) {
        const GLSonidoSuelto& s = v[i];
        if (s.vigilar) {
            Node* sonido = gl_cast<Node>(ObjectDB::get_instance(ObjectID(s.vigilar)));
            if (sonido && sonido->is_inside_tree()) continue;   // solo cambio de sitio
        }
        if (s.stream.is_null()) continue;
        if (s.es3d) {
            AudioStreamPlayer3D* p = memnew(AudioStreamPlayer3D);
            p->set_stream(s.stream);
            p->set_volume_db(s.vol_db);
            p->set_pitch_scale(s.pitch);
            p->set_bus(s.bus);
            p->set_unit_size(s.unit_size);
            p->set_max_distance(s.max_dist);
            p->set_attenuation_model((AudioStreamPlayer3D::AttenuationModel)s.atenuacion);
            p->set_position(s.pos);
            raiz->add_child(p);
            p->connect("finished", Callable(p, "queue_free"));
            p->play();
        } else {
            AudioStreamPlayer* p = memnew(AudioStreamPlayer);
            p->set_stream(s.stream);
            p->set_volume_db(s.vol_db);
            p->set_pitch_scale(s.pitch);
            p->set_bus(s.bus);
            raiz->add_child(p);
            p->connect("finished", Callable(p, "queue_free"));
            p->play();
        }
    }
    v.clear();
}

// ────────────────────────────────────────────────────────────────────
//  SoundGroup — equivalente a Roblox SoundGroup: un bus de audio propio
//  (antes, sin GroupName, su Volume movia el bus Master: el de TODO el juego).
//  Un SoundGroup dentro de otro manda su bus al del padre.
// ────────────────────────────────────────────────────────────────────
class RobloxSoundGroup : public Node {
    GDCLASS(RobloxSoundGroup, Node);

    String bus_name;          // GroupName puesto a mano (escenas viejas); vacio = uno propio
    float  volume = 0.5f;     // Roblox: 0,5 por defecto

    String _bus_auto() const { return String("GLSG_") + String::num_uint64((uint64_t)get_instance_id()); }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_group_name", "n"), &RobloxSoundGroup::set_group_name);
        ClassDB::bind_method(D_METHOD("get_group_name"),      &RobloxSoundGroup::get_group_name);
        ClassDB::bind_method(D_METHOD("set_volume", "v"),     &RobloxSoundGroup::set_volume);
        ClassDB::bind_method(D_METHOD("get_volume"),          &RobloxSoundGroup::get_volume);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "GroupName"), "set_group_name", "get_group_name");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "Volume", PROPERTY_HINT_RANGE, "0,10,0.01"), "set_volume", "get_volume");
    }
    void _notification(int p_what) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        // ENTER va de padre a hijos: el bus existe antes de que los efectos
        // hijos se cuelguen de el. EXIT va al reves: los efectos ya se fueron.
        if (p_what == NOTIFICATION_ENTER_TREE) _aplicar();
        else if (p_what == NOTIFICATION_EXIT_TREE && bus_name.is_empty()) gl_bus_quitar(_bus_auto());
    }

public:
    // Sin la meta, el puente generico no escribia ni leia sus propiedades:
    // `grupo.Volume = 0.2` no hacia nada.
    RobloxSoundGroup() { set_meta("_gl_bridge", true); }
    String gl_bus() const { return bus_name.is_empty() ? _bus_auto() : bus_name; }
    String gl_bus_padre() const {
        RobloxSoundGroup* p = gl_cast<RobloxSoundGroup>(get_parent());
        return p ? p->gl_bus() : String("Master");
    }
    void _aplicar() {
        if (!is_inside_tree() || Engine::get_singleton()->is_editor_hint()) return;
        AudioServer* as = AudioServer::get_singleton();
        const String bus = gl_bus();
        const int idx = (bus == "Master") ? 0 : gl_bus_crear(bus, gl_bus_padre());
        if (as && idx >= 0) as->set_bus_volume_db(idx, gl_db(volume));
    }
    void set_group_name(const String& n) { bus_name = n; _aplicar(); }
    String get_group_name() const { return bus_name; }
    void set_volume(float v) { volume = Math::clamp(v, 0.0f, 10.0f); _aplicar(); }
    float get_volume() const { return volume; }
};

// ────────────────────────────────────────────────────────────────────
//  Sound — equivalente a Roblox Sound.
//  Como en Roblox, es 3D si cuelga de una pieza o de un Attachment (suena desde
//  ahi y se atenua con la distancia) y 2D en cualquier otro sitio. Antes solo
//  era 3D si un script tocaba RollOffMode, y aun asi sonaba desde el origen del
//  mundo: el reproductor colgaba de este Node, que no tiene posicion.
//    local snd = Instance.new("Sound")
//    snd.SoundId = "rbxassetid://..."
//    snd.Parent = workspace.Pieza
//    snd:Play()
// ────────────────────────────────────────────────────────────────────
class RobloxSound : public Node {
    GDCLASS(RobloxSound, Node);

    String  sound_id       = "";
    float   volume         = 0.5f;
    float   pitch          = 1.0f;
    bool    looped         = false;
    bool    rolloff_3d     = false;    // fuerza 3D (propiedad vieja del editor)
    float   rolloff_max    = 10000.0f; // valores por defecto de Roblox
    float   rolloff_min    = 10.0f;
    int     rolloff_mode   = 0;        // Enum.RollOffMode
    bool    playing_state  = false;
    bool    sonar_al_entrar = false;   // Playing = true antes de estar en el arbol
    bool    play_on_remove = false;
    float   pos_pedida     = -1.0f;    // TimePosition escrito sin sonar: Play empieza ahi
    String  sound_group    = "";       // bus por nombre (escenas viejas)
    NodePath grupo_ruta;               // Sound.SoundGroup: la instancia, por ruta
    ObjectID grupo_id;                 // ...y la instancia misma (vale fuera del arbol)
    String  bus_propio;                // si lleva SoundEffects hijos
    Ref<AudioStream> stream_actual;

    AudioStreamPlayer*   player2d = nullptr;
    AudioStreamPlayer3D* player3d = nullptr;

    double ultima_pos = 0.0;   // para DidLoop: la posicion vuelve atras al dar la vuelta
    int    vueltas    = 0;

public:
    // Señales de Roblox: todas reciben el SoundId (DidLoop, ademas, las vueltas).
    struct LuaCallback { lua_State* main_L; int ref; bool active = true; };
    std::vector<LuaCallback> ended_cbs, played_cbs, stopped_cbs, paused_cbs,
                             resumed_cbs, loaded_cbs, looped_cbs;
    void _gl_disconnect(int ref, int64_t vm) {
        for (auto* l : { &ended_cbs, &played_cbs, &stopped_cbs, &paused_cbs,
                         &resumed_cbs, &loaded_cbs, &looped_cbs })
            for (auto& cb : *l) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;
    }
    void add_looped_cb(lua_State* mL, int ref) {
        looped_cbs.push_back({ mL, ref, true });
        _actualizar_proceso();   // DidLoop se detecta mirando la posicion cada frame
    }

private:
    void _fire_sonido(std::vector<LuaCallback>& cbs) {
        if (cbs.empty()) return;
        const CharString id = sound_id.utf8();
        gl_fire_cbs(cbs, [&](lua_State* th) { lua_pushstring(th, id.get_data()); return 1; });
    }
    // El reproductor acabo solo (no salta con Looped: el sonido sigue).
    void _gl_terminado() {
        playing_state = false;
        _actualizar_proceso();
        _fire_sonido(ended_cbs);
    }

    // El proceso solo late con algo que vigilar: DidLoop conectado, o un 3D
    // sonando que tiene que seguir a su pieza. Con un override de _process,
    // Godot pondria a procesar TODOS los sonidos del juego en cada frame.
    void _actualizar_proceso() {
        if (Engine::get_singleton()->is_editor_hint()) return;
        set_process(!looped_cbs.empty() || (player3d && playing_state));
    }

    // Un Sound con Playing = true en el place suena al cargar, pero solo donde
    // Roblox lo deja sonar: no en ReplicatedStorage ni en las plantillas.
    bool _en_sitio_que_suena() const {
        for (const Node* p = get_parent(); p; p = p->get_parent())
            if (p->is_class("RobloxWorkspace") || p->is_class("SoundService") || p->is_class("PlayerGui"))
                return true;
        return false;
    }

    bool _quiere_3d() const {
        if (rolloff_3d) return true;
        const Node* p = get_parent();
        return p && (p->is_class("RobloxPart") || p->is_class("Attachment"));
    }
    void _seguir_padre() {
        if (!player3d) return;
        if (Node3D* p = gl_cast<Node3D>(get_parent()))
            if (p->is_inside_tree()) player3d->set_position(p->get_global_position());
    }

    RobloxSoundGroup* _grupo() const {
        if (grupo_id.is_valid())
            if (RobloxSoundGroup* g = gl_cast<RobloxSoundGroup>(ObjectDB::get_instance(grupo_id))) return g;
        if (grupo_ruta.is_empty() || !is_inside_tree()) return nullptr;
        return gl_cast<RobloxSoundGroup>(get_node_or_null(grupo_ruta));
    }
    // get_path_to necesita un antepasado comun; sin el da error y ruta vacia.
    bool _misma_raiz(const Node* g) const {
        const Node* a = this;
        while (a->get_parent()) a = a->get_parent();
        while (g->get_parent()) g = g->get_parent();
        return a == g;
    }
    // La ruta es lo que se guarda en la escena: se rehace con la instancia.
    void _rehacer_ruta_grupo() {
        if (!grupo_id.is_valid()) return;
        Node* g = gl_cast<Node>(ObjectDB::get_instance(grupo_id));
        if (g && _misma_raiz(g)) grupo_ruta = get_path_to(g);
    }
    // Por donde sale: su bus de efectos, su SoundGroup, el bus viejo o Master.
    String _bus_sin_propio() const {
        if (RobloxSoundGroup* g = _grupo()) return g->gl_bus();
        if (!sound_group.is_empty()) return sound_group;
        return String("Master");
    }
    String _bus_salida() const { return bus_propio.is_empty() ? _bus_sin_propio() : bus_propio; }
    void _aplicar_bus() {
        const StringName b(_bus_salida());
        if (player2d) player2d->set_bus(b);
        if (player3d) player3d->set_bus(b);
    }

    // El stream del SoundId, o nulo. El audio de la nube de Roblox no se puede
    // descargar: se deja constancia en el nodo y se busca antes una copia LOCAL
    // en GodotLuau/assets/rbx/<id>.ogg (ver gl_local_asset_path).
    Ref<AudioStream> _cargar() {
        if (sound_id.is_empty()) return Ref<AudioStream>();
        String src_path = sound_id;
        if (gl_is_roblox_asset(sound_id)) {
            src_path = gl_local_asset_path(sound_id);
            if (src_path.is_empty()) {
                set_meta("__rbx_missing_asset", sound_id);
                return Ref<AudioStream>();
            }
            remove_meta("__rbx_missing_asset");
        }
        // exists() antes de load() evita el error rojo del cargador cuando la
        // ruta simplemente no esta: preferimos un aviso propio y legible.
        ResourceLoader* rl = ResourceLoader::get_singleton();
        if (!rl || !rl->exists(src_path, "AudioStream")) {
            UtilityFunctions::print("[Sound] No se encontro el audio: ", sound_id);
            return Ref<AudioStream>();
        }
        Ref<AudioStream> stream = rl->load(src_path, "AudioStream");
        if (!stream.is_valid()) UtilityFunctions::print("[Sound] No se pudo cargar: ", sound_id);
        return stream;
    }

    // Monta el reproductor que toca (2D o 3D) con el stream cargado. Si cambia de
    // tipo (el Sound se movio de una pieza a la GUI o al reves), el nuevo sigue
    // sonando por donde iba el viejo.
    void _montar() {
        if (stream_actual.is_null()) return;
        const bool es3d = _quiere_3d();
        const bool sonaba = is_playing();
        const float por = sonaba ? get_time_position() : 0.0f;
        if (es3d && player2d) { player2d->queue_free(); player2d = nullptr; }
        if (!es3d && player3d) { player3d->queue_free(); player3d = nullptr; }
        if (es3d) {
            if (!player3d) {
                player3d = memnew(AudioStreamPlayer3D);
                player3d->set_name("__player3d__");
                add_child(player3d, false, Node::INTERNAL_MODE_BACK);
                player3d->connect("finished", callable_mp(this, &RobloxSound::_gl_terminado));
            }
            player3d->set_stream(stream_actual);
            player3d->set_volume_db(gl_db(volume));
            player3d->set_pitch_scale(pitch);
            _apply_spatial();
            _seguir_padre();
        } else {
            if (!player2d) {
                player2d = memnew(AudioStreamPlayer);
                player2d->set_name("__player2d__");
                add_child(player2d, false, Node::INTERNAL_MODE_BACK);
                player2d->connect("finished", callable_mp(this, &RobloxSound::_gl_terminado));
            }
            player2d->set_stream(stream_actual);
            player2d->set_volume_db(gl_db(volume));
            player2d->set_pitch_scale(pitch);
        }
        _aplicar_bus();
        _apply_loop();
        if (sonaba) {
            if (player2d) player2d->play(por);
            if (player3d) player3d->play(por);
        }
        _actualizar_proceso();
    }

    void _load_stream() {
        stream_actual = _cargar();
        if (stream_actual.is_null()) return;
        _montar();
        _fire_sonido(loaded_cbs);
    }

    // Aplica Looped al stream cargado (Ogg/MP3/WAV) — antes no hacía nada
    void _apply_loop() {
        Ref<AudioStream> st = stream_actual;
        if (st.is_null()) return;
        Ref<AudioStreamOggVorbis> ogg = st;
        if (ogg.is_valid()) { ogg->set_loop(looped); return; }
        Ref<AudioStreamMP3> mp3 = st;
        if (mp3.is_valid()) { mp3->set_loop(looped); return; }
        Ref<AudioStreamWAV> wav = st;
        if (wav.is_valid()) { wav->set_loop_mode(looped ? AudioStreamWAV::LOOP_FORWARD : AudioStreamWAV::LOOP_DISABLED); }
    }

    GLSonidoSuelto _copia_suelta() const {
        GLSonidoSuelto s;
        s.stream = stream_actual;
        s.vol_db = gl_db(volume);
        s.pitch = pitch;
        s.bus = _bus_salida();
        s.es3d = player3d != nullptr;
        if (const Node3D* p = gl_cast<Node3D>(get_parent()))
            if (p->is_inside_tree()) s.pos = p->get_global_position();
        s.unit_size = Math::max(rolloff_min, 0.1f);
        s.max_dist = rolloff_max;
        s.atenuacion = _atten_model();
        return s;
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_sound_id", "id"),    &RobloxSound::set_sound_id);
        ClassDB::bind_method(D_METHOD("get_sound_id"),          &RobloxSound::get_sound_id);
        ClassDB::bind_method(D_METHOD("set_volume", "v"),       &RobloxSound::set_volume);
        ClassDB::bind_method(D_METHOD("get_volume"),            &RobloxSound::get_volume);
        ClassDB::bind_method(D_METHOD("set_pitch", "v"),        &RobloxSound::set_pitch);
        ClassDB::bind_method(D_METHOD("get_pitch"),             &RobloxSound::get_pitch);
        ClassDB::bind_method(D_METHOD("set_looped", "v"),       &RobloxSound::set_looped);
        ClassDB::bind_method(D_METHOD("get_looped"),            &RobloxSound::get_looped);
        ClassDB::bind_method(D_METHOD("set_rolloff_3d", "v"),   &RobloxSound::set_rolloff_3d);
        ClassDB::bind_method(D_METHOD("get_rolloff_3d"),        &RobloxSound::get_rolloff_3d);
        ClassDB::bind_method(D_METHOD("set_sound_group", "v"),  &RobloxSound::set_sound_group);
        ClassDB::bind_method(D_METHOD("get_sound_group"),       &RobloxSound::get_sound_group);
        ClassDB::bind_method(D_METHOD("set_playing", "v"),      &RobloxSound::set_playing);
        ClassDB::bind_method(D_METHOD("get_playing"),           &RobloxSound::get_playing);
        ClassDB::bind_method(D_METHOD("set_play_on_remove", "v"), &RobloxSound::set_play_on_remove);
        ClassDB::bind_method(D_METHOD("get_play_on_remove"),      &RobloxSound::get_play_on_remove);
        ClassDB::bind_method(D_METHOD("set_rolloff_mode", "v"), &RobloxSound::set_rolloff_mode);
        ClassDB::bind_method(D_METHOD("get_rolloff_mode"),      &RobloxSound::get_rolloff_mode);
        ClassDB::bind_method(D_METHOD("set_rolloff_min", "v"),  &RobloxSound::set_rolloff_min);
        ClassDB::bind_method(D_METHOD("get_rolloff_min"),       &RobloxSound::get_rolloff_min);
        ClassDB::bind_method(D_METHOD("set_rolloff_max", "v"),  &RobloxSound::set_rolloff_max);
        ClassDB::bind_method(D_METHOD("get_rolloff_max"),       &RobloxSound::get_rolloff_max);
        ClassDB::bind_method(D_METHOD("play"),  &RobloxSound::play);
        ClassDB::bind_method(D_METHOD("stop"),  &RobloxSound::stop);
        ClassDB::bind_method(D_METHOD("pause"), &RobloxSound::pause);
        ClassDB::bind_method(D_METHOD("resume"), &RobloxSound::resume);
        ClassDB::bind_method(D_METHOD("is_playing"), &RobloxSound::is_playing);
        ClassDB::bind_method(D_METHOD("is_paused"),  &RobloxSound::is_paused);
        ClassDB::bind_method(D_METHOD("is_loaded"),  &RobloxSound::is_loaded);
        ClassDB::bind_method(D_METHOD("get_time_position"),      &RobloxSound::get_time_position);
        ClassDB::bind_method(D_METHOD("set_time_position", "t"), &RobloxSound::set_time_position);
        ClassDB::bind_method(D_METHOD("_gl_disconnect", "ref", "vm"), &RobloxSound::_gl_disconnect);

        ADD_PROPERTY(PropertyInfo(Variant::STRING, "SoundId"), "set_sound_id", "get_sound_id");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Volume", PROPERTY_HINT_RANGE, "0,10,0.01"), "set_volume", "get_volume");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Pitch", PROPERTY_HINT_RANGE, "0.01,4,0.01"), "set_pitch", "get_pitch");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Looped"), "set_looped", "get_looped");
        // Enum.RollOffMode de Roblox (Inverse = 0). Antes era un bool que forzaba
        // el 3D, asi que el valor importado de un place lo encendia o apagaba.
        ADD_PROPERTY(PropertyInfo(Variant::INT, "RollOffMode", PROPERTY_HINT_ENUM,
                                  "Inverse,Linear,LinearSquare,InverseTapered"),
                     "set_rolloff_mode", "get_rolloff_mode");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "RollOffMinDistance"), "set_rolloff_min", "get_rolloff_min");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "RollOffMaxDistance"), "set_rolloff_max", "get_rolloff_max");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "PlayOnRemove"), "set_play_on_remove", "get_play_on_remove");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Playing"), "set_playing", "get_playing");
        // Acepta la instancia (lo que entrega el importador y el puente), su
        // ruta, o el nombre de un bus (escenas viejas).
        ADD_PROPERTY(PropertyInfo(Variant::NIL, "SoundGroup", PROPERTY_HINT_NONE, "",
                                  PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT),
                     "set_sound_group", "get_sound_group");
    }

    void _notification(int p_what) {
        if (p_what == NOTIFICATION_PROCESS) {
            _seguir_padre();
            _gl_vigilar_vuelta();
        } else if (p_what == NOTIFICATION_ENTER_TREE) {
            if (Engine::get_singleton()->is_editor_hint()) return;
            _rehacer_ruta_grupo();
            if (!bus_propio.is_empty()) gl_bus_crear(bus_propio, _bus_sin_propio());
            // Movido de una pieza a la GUI o al reves: cambia de 2D a 3D.
            if (stream_actual.is_valid() && _quiere_3d() != (player3d != nullptr)) _montar();
            else _aplicar_bus();
            _seguir_padre();
            // Playing = true escrito fuera del arbol en un Sound que ya estuvo
            // (el primer ingreso lo atiende _ready).
            if (sonar_al_entrar && is_node_ready()) {
                sonar_al_entrar = false;
                if (_en_sitio_que_suena()) call_deferred("play");
            }
        } else if (p_what == NOTIFICATION_EXIT_TREE) {
            if (Engine::get_singleton()->is_editor_hint()) return;
            if (play_on_remove && stream_actual.is_valid()) {
                GLSonidoSuelto s = _copia_suelta();
                s.vigilar = (uint64_t)get_instance_id();
                gl_sonidos_sueltos().push_back(s);
            }
            if (!bus_propio.is_empty()) gl_bus_quitar(bus_propio);
        }
    }

public:
    RobloxSound() { set_meta("_gl_bridge", true); }
    void set_sound_id(const String& v) { sound_id = v; if (is_inside_tree()) _load_stream(); }
    String get_sound_id() const { return sound_id; }

    void set_volume(float v) {
        volume = Math::clamp(v, 0.0f, 10.0f);
        if (player2d) player2d->set_volume_db(gl_db(volume));
        if (player3d) player3d->set_volume_db(gl_db(volume));
    }
    float get_volume() const { return volume; }

    void set_pitch(float v) {
        pitch = Math::clamp(v, 0.01f, 10.0f);
        if (player2d) player2d->set_pitch_scale(pitch);
        if (player3d) player3d->set_pitch_scale(pitch);
    }
    float get_pitch() const { return pitch; }

    void set_looped(bool v) { looped = v; _apply_loop(); }
    bool get_looped() const { return looped; }

    void set_rolloff_3d(bool v) { rolloff_3d = v; if (is_inside_tree()) _montar(); }
    bool get_rolloff_3d() const { return rolloff_3d; }

    // Sound.SoundGroup: instancia, ruta o nombre de bus.
    void set_sound_group(const Variant& v) {
        grupo_ruta = NodePath();
        grupo_id = ObjectID();
        sound_group = String();
        if (v.get_type() == Variant::OBJECT) {
            // Lo normal en Roblox: Instance.new("Sound"), SoundGroup y luego
            // Parent. Fuera del arbol no hay ruta todavia.
            if (Node* g = gl_cast<Node>(v.operator Object*())) {
                grupo_id = g->get_instance_id();
                _rehacer_ruta_grupo();
            }
        } else if (v.get_type() == Variant::NODE_PATH) {
            grupo_ruta = v;
        } else if (v.get_type() == Variant::STRING || v.get_type() == Variant::STRING_NAME) {
            const String s = v;
            if (s.find("/") >= 0) grupo_ruta = NodePath(s);
            else sound_group = s;
        }
        if (!bus_propio.is_empty() && is_inside_tree()) gl_bus_crear(bus_propio, _bus_sin_propio());
        _aplicar_bus();
    }
    Variant get_sound_group() const {
        if (!grupo_ruta.is_empty()) return grupo_ruta;
        return sound_group;
    }
    RobloxSoundGroup* gl_get_sound_group_node() const { return _grupo(); }

    void set_play_on_remove(bool v) { play_on_remove = v; }
    bool get_play_on_remove() const { return play_on_remove; }

    // Un SoundEffect colgado de este Sound pide su bus: el sonido pasa a salir
    // por uno propio (que manda a su grupo o a Master) y el efecto solo le toca
    // a el. Antes iba a Master y cambiaba el audio de todo el juego.
    String gl_bus_para_efecto() {
        if (bus_propio.is_empty()) {
            bus_propio = String("GLSN_") + String::num_uint64((uint64_t)get_instance_id());
            if (is_inside_tree()) gl_bus_crear(bus_propio, _bus_sin_propio());
            _aplicar_bus();
        }
        return bus_propio;
    }

    void _ready() override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (!sound_id.is_empty()) _load_stream();
        if (sonar_al_entrar) {
            sonar_al_entrar = false;
            if (_en_sitio_que_suena()) play();
        }
    }

    void play() {
        if (sound_id.is_empty()) return;
        if (stream_actual.is_null()) _load_stream();
        // Como Roblox: desde el ultimo TimePosition que puso un script, o 0.
        const float desde = pos_pedida >= 0.0f ? pos_pedida : 0.0f;
        pos_pedida = -1.0f;
        // El SoundGroup puede haber entrado al arbol despues que el Sound (vive
        // en SoundService): el bus se resuelve otra vez al sonar.
        _aplicar_bus();
        _seguir_padre();
        if (player2d) { player2d->stop(); player2d->play(desde); }
        if (player3d) { player3d->stop(); player3d->play(desde); }
        playing_state = true;
        ultima_pos = desde; vueltas = 0;
        _actualizar_proceso();
        _fire_sonido(played_cbs);
    }

    void stop() {
        if (player2d) player2d->stop();
        if (player3d) player3d->stop();
        pos_pedida = -1.0f;
        const bool sonaba = playing_state;
        playing_state = false;
        _actualizar_proceso();
        if (sonaba) _fire_sonido(stopped_cbs);
    }

    void pause() {
        // Idempotente como en Roblox: dos Pause() seguidos NO reanudan (era un
        // toggle de antes de existir resume(), y pausar desde dos sitios
        // volvia a arrancar el sonido).
        if (player2d) player2d->set_stream_paused(true);
        if (player3d) player3d->set_stream_paused(true);
        const bool sonaba = playing_state;
        playing_state = false;
        _actualizar_proceso();
        if (sonaba) _fire_sonido(paused_cbs);
    }

    // Resume real (antes solo existía pause() con toggle)
    void resume() {
        if (player2d) player2d->set_stream_paused(false);
        if (player3d) player3d->set_stream_paused(false);
        const bool estaba = playing_state;
        playing_state = true;
        _actualizar_proceso();
        if (!estaba) _fire_sonido(resumed_cbs);
    }

    // Sound.Playing: true sigue donde iba (o empieza desde TimePosition) y false
    // pausa sin perder la posicion, como en Roblox.
    void set_playing(bool b) {
        if (!is_inside_tree() || !is_node_ready()) { sonar_al_entrar = b; return; }   // al cargar el place
        if (b) {
            if (is_paused()) resume();
            else if (!is_playing()) play();
        } else if (playing_state) {
            pause();
        }
    }
    bool get_playing() const { return playing_state || sonar_al_entrar; }

    bool is_playing() const {
        if (player2d) return player2d->is_playing();
        if (player3d) return player3d->is_playing();
        return false;
    }

    float get_time_position() const {
        if (is_playing() || is_paused()) {
            if (player2d) return (float)player2d->get_playback_position();
            if (player3d) return (float)player3d->get_playback_position();
        }
        return pos_pedida >= 0.0f ? pos_pedida : 0.0f;
    }
    // Sonando salta ahi; parado, Play empezara ahi.
    void set_time_position(float t) {
        t = Math::max(t, 0.0f);
        if (is_playing()) {
            if (player2d) player2d->seek(t);
            if (player3d) player3d->seek(t);
        } else {
            pos_pedida = t;
        }
    }
    float get_time_length() const { return stream_actual.is_valid() ? (float)stream_actual->get_length() : 0.0f; }

    // SoundService:PlayLocalSound: suena entero, en 2D, aunque el Sound no este
    // en el mundo (es lo normal: suelen vivir en SoundService o en la GUI).
    void gl_tocar_local() {
        if (stream_actual.is_null()) stream_actual = _cargar();
        if (stream_actual.is_null()) return;
        GLSonidoSuelto s = _copia_suelta();
        s.es3d = false;
        gl_sonidos_sueltos().push_back(s);
    }

    void _gl_vigilar_vuelta() {
        if (looped_cbs.empty() || !looped || !playing_state) return;
        const double pos = get_time_position();
        if (pos + 0.05 < ultima_pos) {
            vueltas++;
            const CharString id = sound_id.utf8();
            const int n = vueltas;
            gl_fire_cbs(looped_cbs, [&](lua_State* th) {
                lua_pushstring(th, id.get_data());
                lua_pushnumber(th, n);
                return 2;
            });
        }
        ultima_pos = pos;
    }
    bool is_paused() const {
        if (player2d) return player2d->get_stream_paused();
        if (player3d) return player3d->get_stream_paused();
        return false;
    }
    bool is_loaded() const { return stream_actual.is_valid(); }

    // ── Audio 3D espacial (rolloff) ──────────────────────────────────
    int _atten_model() const {
        switch (rolloff_mode) {
            case 2:  return 1;  // LinearSquare → InverseSquare
            case 3:  return 2;  // InverseTapered → Logarithmic
            default: return 0;  // Linear/Inverse → InverseDistance
        }
    }
    void _apply_spatial() {
        if (!player3d) return;
        player3d->set_max_distance(rolloff_max);
        player3d->set_unit_size(Math::max(rolloff_min, 0.1f));
        player3d->set_attenuation_model((AudioStreamPlayer3D::AttenuationModel)_atten_model());
    }
    // RollOffMode ya no fuerza el 3D: lo decide el padre, como en Roblox.
    void  set_rolloff_mode(int m) { rolloff_mode = m; _apply_spatial(); }
    int   get_rolloff_mode() const { return rolloff_mode; }
    void  set_rolloff_min(float v) { rolloff_min = Math::max(v, 0.0f); _apply_spatial(); }
    float get_rolloff_min() const { return rolloff_min; }
    void  set_rolloff_max(float v) { rolloff_max = Math::max(v, 0.0f); _apply_spatial(); }
    float get_rolloff_max() const { return rolloff_max; }
    // EmitterSize es el nombre viejo de RollOffMinDistance.
    void  set_emitter_size(float v) { set_rolloff_min(v); }
    float get_emitter_size() const { return rolloff_min; }
    float get_playback_loudness() const {
        AudioServer* as = AudioServer::get_singleton();
        int bi = as->get_bus_index(_bus_salida());
        if (bi < 0) return 0.0f;
        float db = as->get_bus_peak_volume_left_db(bi, 0);
        float lin = Math::pow(10.0f, db / 20.0f);
        return Math::clamp(lin * 1000.0f, 0.0f, 1000.0f);
    }
};

#endif // ROBLOX_SOUND_H

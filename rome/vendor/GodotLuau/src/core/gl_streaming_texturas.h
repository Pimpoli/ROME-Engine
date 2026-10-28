#ifndef GL_STREAMING_TEXTURAS_H
#define GL_STREAMING_TEXTURAS_H

// ════════════════════════════════════════════════════════════════════
//  Streaming de texturas de los assets del juego (Godot 4.7)
//
//  Godot 4.8 trae streaming de texturas por niveles de mipmap (el singleton
//  TextureStreaming): cada textura sube a la GPU solo el detalle que hace falta
//  para lo que se ve. 4.7 no lo tiene, y GodotLuau cargaba todas las imagenes
//  del juego al crear sus nodos, en el hilo principal (leer el PNG,
//  descomprimirlo y subirlo), se vieran o no. MEDIDO al darle a Play con Build
//  a Mountain: ~220 ms del arranque se iban en eso, con casi toda la UI oculta.
//
//  Aqui, para las imagenes de assets/rbx y del cache de descargas:
//   - el PNG suelto se lee y se descomprime en el WorkerThreadPool, y el
//     importado va por la carga en hilos de Godot (load_threaded_request);
//   - la subida a la GPU va en el hilo principal, con un presupuesto por frame;
//   - al llegar se le vuelve a aplicar a quien la pidio (su metodo
//     _gl_textura_lista), como hace el descargador de assets;
//   - UI: solo se pide lo que se ve (un ImageLabel oculto la pide al dibujarse);
//   - 3D: la textura empieza a 1/4 y su resolucion sigue a la distancia del
//     usuario mas cercano (entera, 1/2, 1/4 u 1/8, nunca por debajo de 64 px),
//     como los niveles de mipmap del streaming de 4.8. Es UNA ImageTexture por
//     archivo: al cambiar de nivel se sustituye su contenido y todos los
//     materiales que la usan lo ven solos;
//   - ContentProvider:PreloadAsync la pide ya y espera a que llegue.
//  Con Godot 4.8+ y rendering/textures/streaming/enabled, lo importado lo carga
//  el motor, que ya sube solo los mipmaps que se ven (gl_tex_motor_streaming).
//  GL_TEX_SIN_STREAMING=1 vuelve a la carga de antes (para medir A/B).
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/canvas_item.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include "gl_asset.h"

using namespace godot;

// Subir texturas a la GPU por frame, como mucho (siempre al menos una). Si el
// frame ya es largo (el arranque), se sube mas: un 10 % de lo que duro el
// anterior, hasta 30 ms. MEDIDO en Build a Mountain: con 3 ms fijos y frames de
// 300 ms, las 50 imagenes de la pantalla de carga tardaban 3 s en llegar.
#define GL_TEX_PRESUPUESTO_US 3000
#define GL_TEX_PRESUPUESTO_MAX_US 30000
// Lecturas a la vez. El WorkerThreadPool las reparte entre los nucleos: se
// lanzan en cuanto se piden, sin esperar al frame siguiente.
#define GL_TEX_EN_VUELO 64
// 3D: nivel con el que empieza una textura, el mas bajo, el lado minimo y cada
// cuanto se mira la distancia. Bajar de nivel espera 3 s (al alejarse un poco y
// volver no se recarga nada).
#define GL_TEX_NIVEL_INICIAL 2
#define GL_TEX_NIVEL_MAX 3
#define GL_TEX_LADO_MIN 64
#define GL_TEX_LOD_CADA 0.5
#define GL_TEX_BAJAR_TRAS_US 3000000

struct GLTexCarga {
    String ruta;                 // archivo crudo o recurso importado
    bool importada = false;      // ResourceLoader (importada) o PNG suelto
    bool es3d = false;           // con mipmaps y nivel de detalle por distancia
    int estado = 0;              // 0 en cola, 1 cargando, 2 lista, 3 fallo
    int64_t tarea = -1;          // WorkerThreadPool (PNG suelto)
    Ref<Image> imagen;           // lo que deja el hilo
    LocalVector<uint64_t> esperan;   // a quien se le vuelve a aplicar al llegar
    // ── 3D ──
    Ref<ImageTexture> tex3d;     // la misma siempre: cambiar de nivel = set_image
    int nivel = -1;              // nivel subido (0 entera, k = 1/2^k)
    int nivel_hilo = GL_TEX_NIVEL_INICIAL;   // el que esta leyendo el hilo
    int lado = 0;                // lado mayor de la imagen entera (tras la 1.a lectura)
    int lado_hilo = 0;           // lo apunta el hilo; el principal lo copia al acabar
    LocalVector<uint64_t> usuarios;
    LocalVector<float> radios;
    uint64_t quiere_bajar_desde = 0;
};

struct GLTexStreaming {
    HashMap<String, GLTexCarga*> cargas;   // clave: ruta (UI) o ruta + "#3d"
    LocalVector<GLTexCarga*> cola;         // pedidas, sin empezar (en orden)
    LocalVector<GLTexCarga*> en_vuelo;     // cargando
    LocalVector<GLTexCarga*> de3d;         // las 3D (para el nivel de detalle)
    bool bombeo_pedido = false;
    bool lod_armado = false;
    uint64_t ultimo_bombeo = 0;            // cuando fue el ultimo bombeo (us)...
    uint64_t ultimo_frame = 0;             // ...y en que frame: cuanto duro el frame
};
inline GLTexStreaming& gl_tex_streaming() { static GLTexStreaming s; return s; }

// Encendido en el juego (no en el editor, donde la imagen tiene que salir ya).
inline bool gl_tex_streaming_activo() {
    static int v = -1;
    if (v < 0) {
        Engine* e = Engine::get_singleton();
        OS* os = OS::get_singleton();
        v = (e && !e->is_editor_hint() && os && os->get_environment("GL_TEX_SIN_STREAMING") != "1") ? 1 : 0;
    }
    return v == 1;
}

// Godot 4.8+ con su streaming encendido: lo importado lo carga el motor.
inline bool gl_tex_motor_streaming() {
    static int v = -1;
    if (v < 0) {
        Engine* e = Engine::get_singleton();
        ProjectSettings* ps = ProjectSettings::get_singleton();
        static const String k_ajuste = "rendering/textures/streaming/enabled";
        v = (e && e->has_singleton("TextureStreaming") && ps && ps->has_setting(k_ajuste) &&
             (bool)ps->get_setting(k_ajuste)) ? 1 : 0;
    }
    return v == 1;
}

// Quien la pide la ve: un CanvasItem visible en el arbol. Un nodo 3D, con estar
// en el arbol basta.
inline bool gl_tex_se_ve(Object* dueno) {
    if (CanvasItem* ci = Object::cast_to<CanvasItem>(dueno)) return ci->is_inside_tree() && ci->is_visible_in_tree();
    if (Node* n = Object::cast_to<Node>(dueno)) return n->is_inside_tree();
    return false;
}

// Sabe recibir la textura cuando llega.
inline bool gl_tex_sabe_esperar(Object* dueno) {
    static const StringName k_lista("_gl_textura_lista");
    return dueno && dueno->has_method(k_lista);
}

// GL_TEX_TRAZA=1: cuando se pide y cuando llega cada textura (ms del motor).
inline bool gl_tex_traza() {
    static const bool t = OS::get_singleton() && OS::get_singleton()->get_environment("GL_TEX_TRAZA") == "1";
    return t;
}

inline void gl_tex_bombear();
inline void gl_tex_lod();

inline void gl_tex_pedir_bombeo() {
    GLTexStreaming& s = gl_tex_streaming();
    if (s.bombeo_pedido) return;
    if (SceneTree* st = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop())) {
        s.bombeo_pedido = true;
        st->connect("process_frame", callable_mp_static(&gl_tex_bombear), Object::CONNECT_ONE_SHOT);
    }
}

// El repaso del nivel de detalle, cada GL_TEX_LOD_CADA s mientras haya texturas 3D.
inline void gl_tex_armar_lod() {
    GLTexStreaming& s = gl_tex_streaming();
    if (s.lod_armado) return;
    if (SceneTree* st = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop())) {
        Ref<SceneTreeTimer> t = st->create_timer(GL_TEX_LOD_CADA, true, false, true);
        if (t.is_null()) return;
        s.lod_armado = true;
        t->connect("timeout", callable_mp_static(&gl_tex_lod));
    }
}

// En un hilo del WorkerThreadPool: leer y descomprimir el PNG suelto. En 3D,
// ademas, bajarlo al nivel pedido y hacerle los mipmaps.
inline void gl_tex_hilo(int64_t p) {
    GLTexCarga* c = (GLTexCarga*)(uintptr_t)p;
    Ref<Image> img = Image::load_from_file(c->ruta);
    if (img.is_valid() && img->is_empty()) img.unref();
    if (img.is_valid() && c->es3d) {
        const int lado = MAX(img->get_width(), img->get_height());
        c->lado_hilo = lado;
        int k = c->nivel_hilo;
        while (k > 0 && (lado >> k) < GL_TEX_LADO_MIN) k--;
        if (k > 0) img->resize(MAX(img->get_width() >> k, 1), MAX(img->get_height() >> k, 1), Image::INTERPOLATE_LANCZOS);
        c->nivel_hilo = k;
        img->generate_mipmaps();
    }
    c->imagen = img;
}

// Empieza las pedidas que quepan: el PNG suelto a un hilo del WorkerThreadPool,
// el importado a la carga en hilos de Godot.
inline void gl_tex_lanzar() {
    GLTexStreaming& s = gl_tex_streaming();
    ResourceLoader* rl = ResourceLoader::get_singleton();
    WorkerThreadPool* wp = WorkerThreadPool::get_singleton();
    uint32_t i = 0;
    while (s.en_vuelo.size() < GL_TEX_EN_VUELO && i < s.cola.size()) {
        GLTexCarga* c = s.cola[i++];
        bool ok = false;
        if (c->importada)
            ok = rl->load_threaded_request(c->ruta, "Texture2D") == OK;
        else {
            c->tarea = wp->add_task(callable_mp_static(&gl_tex_hilo).bind((int64_t)(uintptr_t)c), false, "GLTexturas");
            ok = c->tarea >= 0;
        }
        if (!ok) { c->estado = 3; continue; }
        c->estado = 1;
        s.en_vuelo.push_back(c);
    }
    // Lo lanzado (o fallido) sale de la cola de una vez, conservando el orden.
    if (i > 0) {
        LocalVector<GLTexCarga*> resto;
        for (uint32_t k = i; k < s.cola.size(); k++) resto.push_back(s.cola[k]);
        s.cola = resto;
    }
}

// Tipos de carga: UI (sin mipmaps), 3D (mipmaps y nivel de detalle por
// distancia) o solo mipmaps (siempre entera: las caras del cielo).
enum GLTexTipo { GL_TEX_UI = 0, GL_TEX_3D = 1, GL_TEX_MIPMAPS = 2 };

// La busca o la crea (en la cola) por su clave.
inline GLTexCarga* gl_tex_entrada(const String& ruta, bool importada, int tipo, Object* dueno) {
    GLTexStreaming& s = gl_tex_streaming();
    const String clave = tipo == GL_TEX_3D ? ruta + String("#3d") : (tipo == GL_TEX_MIPMAPS ? ruta + String("#mm") : ruta);
    if (GLTexCarga** hay = s.cargas.getptr(clave)) return *hay;
    GLTexCarga* c = memnew(GLTexCarga);
    c->ruta = ruta;
    c->importada = importada;
    c->es3d = tipo != GL_TEX_UI;
    c->nivel_hilo = tipo == GL_TEX_3D ? GL_TEX_NIVEL_INICIAL : 0;
    s.cargas.insert(clave, c);
    s.cola.push_back(c);
    if (tipo == GL_TEX_3D) s.de3d.push_back(c);
    if (gl_tex_traza())
        UtilityFunctions::print("[TEX] pedida ", (int64_t)Time::get_singleton()->get_ticks_msec(), " ",
                                ruta.get_file(), tipo == GL_TEX_3D ? " (3d)" : (tipo == GL_TEX_MIPMAPS ? " (mipmaps)" : ""),
                                dueno ? "" : " (precarga)");
    gl_tex_lanzar();
    gl_tex_pedir_bombeo();
    return c;
}

inline void gl_tex_esperar(GLTexCarga* c, Object* dueno) {
    if (!dueno || c->estado == 3) return;
    const uint64_t id = (uint64_t)dueno->get_instance_id();
    for (uint32_t i = 0; i < c->esperan.size(); i++)
        if (c->esperan[i] == id) return;
    c->esperan.push_back(id);
    gl_tex_pedir_bombeo();
}

// UI: la pide (si no estaba) y apunta a quien la espera. Devuelve su carga.
inline GLTexCarga* gl_tex_pedir(const String& ruta, bool importada, Object* dueno) {
    GLTexCarga* c = gl_tex_entrada(ruta, importada, GL_TEX_UI, dueno);
    if (c->estado < 2) gl_tex_esperar(c, dueno);
    return c;
}

// La textura, si ya llego; si no, nula (y pedida).
inline Ref<Texture2D> gl_tex_obtener(const String& ruta, bool importada, Object* dueno) {
    if (Ref<Texture2D>* hay = gl_texturas_crudas().getptr(ruta)) return *hay;
    gl_tex_pedir(ruta, importada, dueno);
    return Ref<Texture2D>();
}

// 3D (PNG suelto): la textura compartida si ya hay alguna version subida (nula
// si no, y pedida). `usuario` cuenta para el nivel de detalle con su radio.
inline Ref<Texture2D> gl_tex_obtener_3d(const String& ruta, Node3D* usuario, float radio) {
    GLTexCarga* c = gl_tex_entrada(ruta, false, GL_TEX_3D, usuario);
    if (usuario) {
        const uint64_t id = (uint64_t)usuario->get_instance_id();
        bool ya = false;
        for (uint32_t i = 0; i < c->usuarios.size() && !ya; i++)
            if (c->usuarios[i] == id) { c->radios[i] = radio; ya = true; }
        if (!ya) { c->usuarios.push_back(id); c->radios.push_back(radio); }
        gl_tex_armar_lod();
    }
    if (c->tex3d.is_valid()) return c->tex3d;
    gl_tex_esperar(c, usuario);
    return Ref<Texture2D>();
}

// PNG suelto con mipmaps y SIEMPRE entero (sin nivel de detalle): las caras del
// cielo. Nula hasta que llega; entonces `dueno` recibe _gl_textura_lista.
inline Ref<Texture2D> gl_tex_obtener_mipmaps(const String& ruta, Object* dueno) {
    GLTexCarga* c = gl_tex_entrada(ruta, false, GL_TEX_MIPMAPS, dueno);
    if (c->tex3d.is_valid()) return c->tex3d;
    gl_tex_esperar(c, dueno);
    return Ref<Texture2D>();
}

// El nivel (0 entera .. GL_TEX_NIVEL_MAX) que pide la distancia: el mas bajo que
// aun da un texel por pixel al usuario mas cercano. -1 si nadie la usa.
inline int gl_tex_nivel_deseado(GLTexCarga* c, const Vector3& ojo, float px_por_unidad) {
    float px = 0.0f;
    for (uint32_t i = 0; i < c->usuarios.size();) {
        Node3D* n = Object::cast_to<Node3D>(ObjectDB::get_instance(ObjectID(c->usuarios[i])));
        if (!n) {
            c->usuarios.remove_at_unordered(i);
            c->radios.remove_at_unordered(i);
            continue;
        }
        if (n->is_inside_tree() && n->is_visible_in_tree()) {
            // El tamaño de AHORA si es una pieza (al cargar la escena la textura
            // llega antes que el Size); si no, el radio con el que se apunto.
            static const StringName k_size("Size");
            const Variant tam = n->get(k_size);
            const float r = tam.get_type() == Variant::VECTOR3 ? ((Vector3)tam).length() * 0.5f : c->radios[i];
            const float d = MAX(n->get_global_position().distance_to(ojo) - r, 0.5f);
            px = MAX(px, 2.0f * r / d * px_por_unidad);
        }
        i++;
    }
    if (c->usuarios.is_empty() || c->lado <= 0) return -1;
    const float hace_falta = MAX(px, (float)GL_TEX_LADO_MIN);
    int k = 0;
    while (k < GL_TEX_NIVEL_MAX && (float)(c->lado >> (k + 1)) >= hace_falta) k++;
    return k;
}

// Volver a leer el archivo a otro nivel (la textura actual sigue mientras tanto).
inline void gl_tex_recargar(GLTexCarga* c, int nivel) {
    if (c->estado < 2 || c->nivel == nivel) return;
    c->nivel_hilo = nivel;
    c->estado = 0;
    c->quiere_bajar_desde = 0;
    gl_tex_streaming().cola.push_back(c);
    gl_tex_lanzar();
    gl_tex_pedir_bombeo();
}

inline void gl_tex_evaluar(GLTexCarga* c, const Vector3& ojo, float px_por_unidad, uint64_t ahora) {
    if (c->estado != 2 || c->tex3d.is_null()) return;
    const int k = gl_tex_nivel_deseado(c, ojo, px_por_unidad);
    if (gl_tex_traza() && k != c->nivel)
        UtilityFunctions::print("[TEX] nivel ", c->ruta.get_file(), ": quiere ", k, ", tiene ", c->nivel,
                                ", usuarios ", (int64_t)c->usuarios.size(), ", lado ", c->lado);
    if (k < 0 || k == c->nivel) { c->quiere_bajar_desde = 0; return; }
    if (k < c->nivel) { gl_tex_recargar(c, k); return; }   // mas detalle: ya
    if (!c->quiere_bajar_desde) c->quiere_bajar_desde = ahora;
    else if (ahora - c->quiere_bajar_desde > GL_TEX_BAJAR_TRAS_US) gl_tex_recargar(c, k);
}

// La camara del juego y cuantos pixeles ocupa una unidad a distancia 1.
inline bool gl_tex_camara(Vector3& ojo, float& px_por_unidad) {
    SceneTree* st = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
    Window* raiz = st ? st->get_root() : nullptr;
    Camera3D* cam = raiz ? raiz->get_camera_3d() : nullptr;
    if (!cam) return false;
    ojo = cam->get_global_position();
    const float alto = raiz->get_visible_rect().size.y;
    px_por_unidad = alto / (2.0f * Math::tan(Math::deg_to_rad(cam->get_fov()) * 0.5f));
    return true;
}

inline void gl_tex_lod() {
    GLTexStreaming& s = gl_tex_streaming();
    s.lod_armado = false;
    Vector3 ojo;
    float px_por_unidad = 0.0f;
    const bool hay_camara = gl_tex_camara(ojo, px_por_unidad);
    if (gl_tex_traza())
        UtilityFunctions::print("[TEX] repaso de nivel: ", (int64_t)s.de3d.size(), " texturas 3d, camara ", hay_camara);
    if (hay_camara) {
        const uint64_t ahora = Time::get_singleton()->get_ticks_usec();
        for (uint32_t i = 0; i < s.de3d.size(); i++) gl_tex_evaluar(s.de3d[i], ojo, px_por_unidad, ahora);
    }
    for (uint32_t i = 0; i < s.de3d.size(); i++)
        if (!s.de3d[i]->usuarios.is_empty()) { gl_tex_armar_lod(); break; }
}

// Una vez por frame mientras quede algo: recoger lo que termino, subirlo con
// presupuesto, avisar a quien esperaba y lanzar lo siguiente.
inline void gl_tex_bombear() {
    GLTexStreaming& s = gl_tex_streaming();
    s.bombeo_pedido = false;
    ResourceLoader* rl = ResourceLoader::get_singleton();
    WorkerThreadPool* wp = WorkerThreadPool::get_singleton();
    const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
    // Presupuesto: 3 ms, o el 10 % del frame anterior si fue largo (hasta 30).
    const uint64_t frame = Engine::get_singleton()->get_process_frames();
    uint64_t presupuesto = GL_TEX_PRESUPUESTO_US;
    if (s.ultimo_frame + 1 == frame && t0 > s.ultimo_bombeo)
        presupuesto = CLAMP((t0 - s.ultimo_bombeo) / 10, (uint64_t)GL_TEX_PRESUPUESTO_US, (uint64_t)GL_TEX_PRESUPUESTO_MAX_US);
    s.ultimo_bombeo = t0;
    s.ultimo_frame = frame;
    int subidas = 0;
    static const StringName k_lista("_gl_textura_lista");
    for (uint32_t i = 0; i < s.en_vuelo.size();) {
        GLTexCarga* c = s.en_vuelo[i];
        Ref<Texture2D> tex;
        bool primera = true;
        if (c->importada) {
            const ResourceLoader::ThreadLoadStatus st = rl->load_threaded_get_status(c->ruta);
            if (st == ResourceLoader::THREAD_LOAD_IN_PROGRESS) { i++; continue; }
            if (st == ResourceLoader::THREAD_LOAD_LOADED) tex = rl->load_threaded_get(c->ruta);
        } else {
            if (!wp->is_task_completed(c->tarea)) { i++; continue; }
            // Subir es lo caro del hilo principal: gastado el presupuesto, al
            // frame siguiente.
            if (subidas > 0 && Time::get_singleton()->get_ticks_usec() - t0 > presupuesto) break;
            wp->wait_for_task_completion(c->tarea);
            c->tarea = -1;
            if (c->imagen.is_valid()) {
                if (c->es3d) {
                    c->lado = c->lado_hilo;
                    c->nivel = c->nivel_hilo;
                    primera = c->tex3d.is_null();
                    if (primera) c->tex3d = ImageTexture::create_from_image(c->imagen);
                    else c->tex3d->set_image(c->imagen);
                    tex = c->tex3d;
                } else {
                    tex = ImageTexture::create_from_image(c->imagen);
                }
            }
            c->imagen.unref();
            subidas++;
        }
        s.en_vuelo.remove_at_unordered(i);
        // Una 3D que ya tenia version sigue valiendo aunque la recarga falle.
        c->estado = (tex.is_valid() || c->tex3d.is_valid()) ? 2 : 3;
        if (gl_tex_traza())
            UtilityFunctions::print("[TEX] lista ", (int64_t)Time::get_singleton()->get_ticks_msec(), " ",
                                    c->ruta.get_file(), c->es3d ? String(" (3d nivel ") + itos(c->nivel) + ")" : String(),
                                    tex.is_valid() ? "" : " FALLO");
        if (tex.is_valid() && !c->es3d) gl_texturas_guardar(c->ruta, tex);
        if (!primera) continue;   // cambio de nivel: los materiales ya la tienen
        LocalVector<uint64_t> esperan;
        esperan.resize(c->esperan.size());
        for (uint32_t k = 0; k < c->esperan.size(); k++) esperan[k] = c->esperan[k];
        c->esperan.clear();
        for (uint32_t k = 0; k < esperan.size(); k++)
            if (Object* o = ObjectDB::get_instance(ObjectID(esperan[k]))) o->call(k_lista);
        // Recien llegada a 1/4: si alguien la tiene cerca, se sube ya.
        if (c->es3d && c->estado == 2) {
            Vector3 ojo;
            float ppu = 0.0f;
            if (gl_tex_camara(ojo, ppu)) gl_tex_evaluar(c, ojo, ppu, Time::get_singleton()->get_ticks_usec());
        }
    }
    gl_tex_lanzar();
    if (!s.cola.is_empty() || !s.en_vuelo.is_empty()) gl_tex_pedir_bombeo();
}

// Lo que espera en la cola (para ContentProvider.RequestQueueSize).
inline int gl_tex_pendientes() {
    const GLTexStreaming& s = gl_tex_streaming();
    return (int)(s.cola.size() + s.en_vuelo.size());
}

// Al descargar la extension: esperar a los hilos y soltarlo todo.
inline void gl_tex_streaming_liberar() {
    GLTexStreaming& s = gl_tex_streaming();
    WorkerThreadPool* wp = WorkerThreadPool::get_singleton();
    for (const KeyValue<String, GLTexCarga*>& kv : s.cargas) {
        if (kv.value->tarea >= 0 && wp) wp->wait_for_task_completion(kv.value->tarea);
        memdelete(kv.value);
    }
    s.cargas.clear();
    s.cola.clear();
    s.en_vuelo.clear();
    s.de3d.clear();
}

#endif // GL_STREAMING_TEXTURAS_H

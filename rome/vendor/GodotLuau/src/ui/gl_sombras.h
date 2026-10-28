#ifndef GL_SOMBRAS_H
#define GL_SOMBRAS_H

// ════════════════════════════════════════════════════════════════════
//  Sombras del sol propias: mapa GUARDADO y pintadas POR PANTALLA.
//
//  MEDIDO (UHD 770, Build a Mountain a 1457x711, calidad 7): la sombra del sol
//  de Godot costaba 2,3 ms de GPU por frame (borrar el mapa de 4096: 0,69;
//  dibujarlo: 0,78; muestrearlo al pintar las piezas: ~0,85). Roblox guarda su
//  mapa de sombras y no lo rehace cada frame. Asi:
//   - El sol de Godot va SIN sombra.
//   - Mapa guardado en BALDOSAS fijas en el mundo (plano del sol), en un atlas
//     de 5x5 que se recorre en mosaico: solo se dibuja la baldosa que falta o
//     que cambio, UNA por frame (0,73 ms cada una), y de paso se repasan todas
//     poco a poco (una cada 4 frames) por si un script movio algo anclado.
//     Dibujar el mapa entero de golpe eran 9,4 ms: un tiron.
//   - Mapa de cada frame, pequeño y alrededor de la camara, solo con lo que se
//     mueve (capa GL_CAPA_DINAMICA): ~0,3 ms.
//   - GLSombraPantalla pinta la sombra tras la pasada opaca, a media
//     resolucion la mascara (~0,75 ms las dos pasadas).
//   - Quitar solo la luz del SOL de un pixel pide saber cuanta luz ambiente y
//     cuanto sol le llegan: lo mide una SONDA (8 cuadros blancos escondidos bajo
//     el mundo que dibuja el propio Godot con su ambiente y su sol).
//  Total 121 -> 143 FPS en calidad 7. Va de la calidad 6 a la 10 (las que usaban
//  el mapa de 4096); en la 4 y la 5 el mapa de 2048 de Godot sale mas barato.
//
//  Las camaras del sol van EN el plano de cada baldosa con el plano cercano
//  negativo: asi estan cerca de lo que dibujan y el LOD por distancia
//  (visibility_range de las piezas pequeñas) no las saca del mapa.
//
//  Limites conocidos: lo transparente no proyecta (no escribe profundidad); el
//  neon en sombra se oscurece algo (la cuenta escala todo el pixel); lo que
//  se mueve lejos de la camara (fuera del mapa de cada frame) no tiene sombra.
// ════════════════════════════════════════════════════════════════════

#include "gl_sombras_efectos.h"
#include "gl_sonda.h"
#include "../core/gl_capas.h"
#include "../core/gl_graphics.h"
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/window.hpp>

static const int GL_SOMBRA_N = 5;              // baldosas por lado del atlas
static const int GL_SOMBRA_HUECOS = GL_SOMBRA_N * GL_SOMBRA_N;
static const float GL_SOMBRA_ALZA = 1500.0f;   // media caja del sol (studs)

// ¿Toca usarlas? Forward+ fuera del editor y sin GL_SOMBRAS_GODOT=1 (para
// medir A/B): con la luz propia, siempre que haya sombras; sin ella, en las
// calidades 6-10 y no en Custom.
inline bool gl_sombras_propias_quiere() {
    if (!gl_postfx_disponible()) return false;
    static int godot = -1;
    if (godot < 0) godot = OS::get_singleton()->get_environment("GL_SOMBRAS_GODOT") == "1" ? 1 : 0;
    if (godot) return false;
    // Con la luz propia las piezas no reciben la sombra de Godot: van las
    // propias siempre que haya sombras (en las calidades, desde la 4, que es
    // donde Roblox empieza a tenerlas).
    if (gl_luz_propia_activa()) return gl_calidad_sombras();
    if (gl_quality_mode() == GL_QUALITY_CUSTOM) return false;
    return gl_graphics_level() >= 6;
}

class GLSombras : public Node {
    GDCLASS(GLSombras, Node);

    struct Atlas {
        RID tex;
        Basis b;               // ejes del sol (b.z = hacia el sol)
        Vector3 dir;
        Vector2i hueco[GL_SOMBRA_HUECOS];
        bool ok[GL_SOMBRA_HUECOS] = {};
        bool usado = false;
    };

    // Ajustes (los de calidad 6-10 son iguales; el alcance de la sombra de
    // Godot en esas calidades era 150-205 studs y aqui son 160-200).
    float T = 80.0f;            // lado de la baldosa (studs): 5 x 80 = 400
    int tile_px = 512;          // 6,4 texeles por stud
    int din_px = 512;
    float din_mitad = 32.0f;    // mapa de cada frame: 64 studs de lado
    float suave_env = -1.0f;    // GL_SOMBRAS_SUAVE: degradado base fijo (studs), para medir
    int rr_cada = 15;           // repaso lento: una baldosa cada tantos frames (GL_SOMBRAS_RR)
    int din_cada = 0;           // mapa de lo que se mueve: cada tantos frames (0 = segun los FPS; GL_SOMBRAS_DIN_CADA)
    bool din_hecho = false;
    float din_lx = 0.0f, din_ly = 0.0f;
    Basis din_bd;
    float sesgo = 1.0f;         // texeles
    float normal_off = 1.5f;    // texeles
    float opac = 0.975f;        // la de la sombra de Godot (gl_graphics.h)
    int modo = 0;               // GL_SOMBRAS_MODO=1: la mascara en pantalla

    SubViewport* vp_tile = nullptr;
    Camera3D* cam_tile = nullptr;
    Ref<GLSombraCopia> copia_tile;
    SubViewport* vp_din = nullptr;
    Camera3D* cam_din = nullptr;
    Ref<GLSombraCopia> copia_din;
    Ref<GLSombraPantalla> pantalla;
    uint64_t sol_id = 0, we_id = 0, luz_id = 0;

    Atlas at[2];
    int frente = 0;
    bool rehaciendo = false;     // el otro atlas se esta dibujando con un sol nuevo
    bool sucio[GL_SOMBRA_HUECOS] = {};
    int pendiente = -1, pendiente_atlas = 0;
    uint32_t copias_al_pedir = 0;
    uint64_t frame_pedido = 0;
    int rr = 0;
    uint64_t frame_rr = 0;
    bool atlas_creados = false;

    bool montado = false, listo = false;
    Ref<Texture2DRD> masc_tex;   // la mascara de la sombra para los shaders de la luz propia
    uint32_t masc_gen_visto = 0;
    Vector4 sombra_puesta = Vector4(-1, -1, -1, -1);

    static uint32_t _n_frame() { return (uint32_t)Engine::get_singleton()->get_process_frames(); }

    void _crear_mapa(int px, float mitad, uint32_t mascara, SubViewport*& vp, Camera3D*& cam, Ref<GLSombraCopia>& cp) {
        vp = memnew(SubViewport);
        vp->set_size(Vector2i(px, px));
        vp->set_update_mode(SubViewport::UPDATE_DISABLED);
        vp->set_debug_draw(Viewport::DEBUG_DRAW_UNSHADED);
        vp->set_msaa_3d(Viewport::MSAA_DISABLED);
        vp->set_screen_space_aa(Viewport::SCREEN_SPACE_AA_DISABLED);
        vp->set_positional_shadow_atlas_size(0);
        vp->set_use_occlusion_culling(false);
        vp->set_disable_input(true);
        add_child(vp, false, Node::INTERNAL_MODE_BACK);
        cam = memnew(Camera3D);
        cam->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
        cam->set_keep_aspect_mode(Camera3D::KEEP_HEIGHT);
        cam->set_size(mitad * 2.0f);
        cam->set_near(-GL_SOMBRA_ALZA);
        cam->set_far(GL_SOMBRA_ALZA);
        cam->set_cull_mask(mascara);
        Ref<Environment> env;
        env.instantiate();
        env->set_background(Environment::BG_COLOR);
        env->set_bg_color(Color(0, 0, 0));
        cam->set_environment(env);
        cp.instantiate();
        cp->cerca = -GL_SOMBRA_ALZA;
        cp->lejos = GL_SOMBRA_ALZA;
        Ref<Compositor> comp;
        comp.instantiate();
        TypedArray<CompositorEffect> efectos;
        efectos.push_back(cp);
        comp->set_compositor_effects(efectos);
        cam->set_compositor(comp);
        vp->add_child(cam);
        cam->set_current(true);
    }

    void _crear_atlas() {
        RenderingDevice* rd = RenderingServer::get_singleton()->get_rendering_device();
        if (!rd) return;
        const Vector2i t(GL_SOMBRA_N * tile_px, GL_SOMBRA_N * tile_px);
        for (int i = 0; i < 2; i++)
            if (!at[i].tex.is_valid()) at[i].tex = gl_sombra_textura(rd, t, RenderingDevice::DATA_FORMAT_R32_SFLOAT);
        atlas_creados = at[0].tex.is_valid() && at[1].tex.is_valid();
    }

    DirectionalLight3D* _sol() {
        if (DirectionalLight3D* s = Object::cast_to<DirectionalLight3D>(ObjectDB::get_instance(ObjectID(sol_id)))) return s;
        Node* root = get_tree() ? (Node*)get_tree()->get_root() : nullptr;
        DirectionalLight3D* s = root ? Object::cast_to<DirectionalLight3D>(gl_buscar_clase_cache(root, "DirectionalLight3D")) : nullptr;
        sol_id = s ? (uint64_t)s->get_instance_id() : 0;
        return s;
    }

    WorldEnvironment* _we() {
        if (WorldEnvironment* w = Object::cast_to<WorldEnvironment>(ObjectDB::get_instance(ObjectID(we_id)))) return w;
        Node* root = get_tree() ? (Node*)get_tree()->get_root() : nullptr;
        WorldEnvironment* w = root ? Object::cast_to<WorldEnvironment>(gl_buscar_clase_cache(root, "WorldEnvironment")) : nullptr;
        we_id = w ? (uint64_t)w->get_instance_id() : 0;
        return w;
    }

    // ¿El juego quiere sombras? GlobalShadows del Lighting (si lo hay).
    // Lighting.ShadowSoftness de Roblox (0..1, 0,2 de serie).
    float _suavidad() {
        Node* l = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(luz_id)));
        if (!l) return 0.2f;
        static const StringName k_ss("ShadowSoftness");
        const Variant v = l->get(k_ss);
        return v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT ? CLAMP((float)v, 0.0f, 1.0f) : 0.2f;
    }

    bool _juego_quiere() {
        Node* l = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(luz_id)));
        if (!l) {
            Node* root = get_tree() ? (Node*)get_tree()->get_root() : nullptr;
            l = root ? gl_buscar_clase_cache(root, "Lighting") : nullptr;
            luz_id = l ? (uint64_t)l->get_instance_id() : 0;
        }
        static const StringName k_gs("GlobalShadows");
        return (l ? (bool)l->get(k_gs) : true) && gl_calidad_sombras();
    }

    bool _montar() {
        DirectionalLight3D* sol = _sol();
        WorldEnvironment* we = _we();
        if (!sol || !we || we->get_environment().is_null()) return false;
        // Solo las piezas quietas que proyectan (gl_capas.h): ni carteles, ni
        // particulas, ni nada que Roblox no ponga en su sombra.
        _crear_mapa(tile_px, T * 0.5f, GL_CAPA_MUNDO, vp_tile, cam_tile, copia_tile);
        _crear_mapa(din_px, din_mitad, GL_CAPA_DINAMICA, vp_din, cam_din, copia_din);
        pantalla.instantiate();
        pantalla->din_id = (uint64_t)copia_din->get_instance_id();
        // Con la luz propia la sombra la ponen los shaders de las piezas
        // (GL_SOMBRA_PANTALLA=1: la de pantalla de siempre, para medir A/B).
        pantalla->gl_en_shader(gl_luz_propia_activa() &&
                               OS::get_singleton()->get_environment("GL_SOMBRA_PANTALLA") != "1");
        Ref<Compositor> comp = we->get_compositor();
        if (comp.is_null()) {
            comp.instantiate();
            we->set_compositor(comp);
        }
        TypedArray<CompositorEffect> efectos = comp->get_compositor_effects();
        efectos.push_front(pantalla);
        comp->set_compositor_effects(efectos);
        RenderingServer::get_singleton()->call_on_render_thread(callable_mp(this, &GLSombras::_crear_atlas));
        gl_sombra_empezar_compilacion();
        montado = true;
        return true;
    }

    // La mascara a los shaders de la luz propia (uniforms globales gl_sombra y
    // gl_sombra_masc de core/gl_luz_propia.h).
    void _sombra_a_shaders(bool on) {
        if (!pantalla.is_valid() || !pantalla->en_shader) return;
        RenderingServer* rs = RenderingServer::get_singleton();
        static const StringName k_masc("gl_sombra_masc"), k_sombra("gl_sombra");
        const uint32_t g = pantalla->masc_gen.load();
        if (on && g != masc_gen_visto && pantalla->masc.is_valid()) {
            masc_gen_visto = g;
            if (masc_tex.is_null()) masc_tex.instantiate();
            masc_tex->set_texture_rd_rid(pantalla->masc);
            rs->global_shader_parameter_set(k_masc, masc_tex);
        } else if (!on && masc_tex.is_valid()) {
            // Que ningun material apunte a la mascara cuando se libere.
            rs->global_shader_parameter_set(k_masc, gl_luz_textura_blanca());
            masc_tex.unref();
            masc_gen_visto = 0;
        }
        const bool ok = on && masc_tex.is_valid();
        const Vector4 v(ok ? 1.0f : 0.0f, opac, (float)pantalla->masc_w.load(), (float)pantalla->masc_h.load());
        if (v != sombra_puesta) {
            sombra_puesta = v;
            rs->global_shader_parameter_set(k_sombra, v);
        }
    }

    void _apagar() {
        _sombra_a_shaders(false);
        // Al volver se redibuja todo: lo que cambie mientras tanto no avisa a
        // nadie (y una baldosa vieja es una sombra de algo que ya no esta).
        for (int i = 0; i < 2; i++)
            for (int k = 0; k < GL_SOMBRA_HUECOS; k++) at[i].ok[k] = false;
        for (int k = 0; k < GL_SOMBRA_HUECOS; k++) sucio[k] = false;
        rehaciendo = false;
        gl_sombras_toques().clear();
        gl_sombras_todo_sucio() = false;
        if (listo) {
            listo = false;
            gl_sombras_propias_listas() = false;
            if (pantalla.is_valid()) pantalla->set_enabled(false);
            if (DirectionalLight3D* sol = _sol()) sol->set_shadow(_juego_quiere());
        }
        if (vp_din) vp_din->set_update_mode(SubViewport::UPDATE_DISABLED);
        pendiente = -1;
    }

    Vector2i _ventana(const Atlas& a, const Vector3& ojo) const {
        const Vector2i ci((int)Math::floor(a.b.get_column(0).dot(ojo) / T), (int)Math::floor(a.b.get_column(1).dot(ojo) / T));
        return ci - Vector2i(GL_SOMBRA_N / 2, GL_SOMBRA_N / 2);
    }

    static int _hueco(const Vector2i& ij) {
        return Math::posmod(ij.y, GL_SOMBRA_N) * GL_SOMBRA_N + Math::posmod(ij.x, GL_SOMBRA_N);
    }

    // La baldosa que la ventana `w` pone en el hueco k.
    static Vector2i _esperada(int k, const Vector2i& w) {
        const int si = k % GL_SOMBRA_N, sj = k / GL_SOMBRA_N;
        return Vector2i(w.x + Math::posmod(si - w.x, GL_SOMBRA_N), w.y + Math::posmod(sj - w.y, GL_SOMBRA_N));
    }

    void _dibujar_baldosa(int ai, const Vector2i& ij, const Vector3& ojo) {
        Atlas& a = at[ai];
        const int k = _hueco(ij);
        const Vector3 bx = a.b.get_column(0), by = a.b.get_column(1), bz = a.b.get_column(2);
        const Vector3 o = bx * ((ij.x + 0.5f) * T) + by * ((ij.y + 0.5f) * T) + bz * bz.dot(ojo);
        cam_tile->set_global_transform(Transform3D(a.b, o));
        copia_tile->destino = a.tex;
        copia_tile->origen = Vector2i(Math::posmod(ij.x, GL_SOMBRA_N) * tile_px, Math::posmod(ij.y, GL_SOMBRA_N) * tile_px);
        copia_tile->desfase = o.dot(-a.dir);
        vp_tile->set_update_mode(SubViewport::UPDATE_ONCE);
        if (a.hueco[k] != ij) a.ok[k] = false;   // otra baldosa: no vale hasta que se dibuje
        a.hueco[k] = ij;
        copias_al_pedir = copia_tile->copias.load();
        frame_pedido = _n_frame();
        pendiente = k;
        pendiente_atlas = ai;
    }

    // Lo que falta del atlas `ai` en su ventana, lo mas cerca de la camara.
    bool _falta(int ai, const Vector3& ojo, Vector2i& sale) const {
        const Atlas& a = at[ai];
        const Vector2i w = _ventana(a, ojo);
        const Vector2i c = w + Vector2i(GL_SOMBRA_N / 2, GL_SOMBRA_N / 2);
        int mejor = INT32_MAX;
        for (int j = 0; j < GL_SOMBRA_N; j++)
            for (int i = 0; i < GL_SOMBRA_N; i++) {
                const Vector2i ij = w + Vector2i(i, j);
                const int k = _hueco(ij);
                if (a.ok[k] && a.hueco[k] == ij) continue;
                const int d = (ij.x - c.x) * (ij.x - c.x) + (ij.y - c.y) * (ij.y - c.y);
                if (d < mejor) { mejor = d; sale = ij; }
            }
        return mejor != INT32_MAX;
    }

    // Las ancladas que movia un script y ya llevan 20 frames quietas vuelven al
    // mapa guardado (RobloxPart::gl_sombra_parada).
    void _repasar_moviles() {
        HashSet<uint64_t>& m = gl_sombras_moviles();
        if (m.is_empty()) return;
        const uint64_t f = Engine::get_singleton()->get_process_frames();
        LocalVector<uint64_t> fuera;
        for (const uint64_t id : m) {
            RobloxPart* p = Object::cast_to<RobloxPart>(ObjectDB::get_instance(ObjectID(id)));
            if (!p || p->gl_sombra_parada(f, 20)) fuera.push_back(id);
        }
        for (const uint64_t id : fuera) m.erase(id);
    }

    void _recoger_toques(const Vector3& ojo) {
        if (gl_sombras_todo_sucio()) {
            gl_sombras_todo_sucio() = false;
            gl_sombras_toques().clear();
            for (int k = 0; k < GL_SOMBRA_HUECOS; k++) sucio[k] = true;
            return;
        }
        LocalVector<GLSombraToque>& v = gl_sombras_toques();
        if (v.is_empty()) return;
        Atlas& a = at[frente];
        const Vector2i w = _ventana(a, ojo);
        const Vector3 bx = a.b.get_column(0), by = a.b.get_column(1);
        for (const GLSombraToque& t : v) {
            const float x = bx.dot(t.centro), y = by.dot(t.centro);
            const int i0 = (int)Math::floor((x - t.radio) / T), i1 = (int)Math::floor((x + t.radio) / T);
            const int j0 = (int)Math::floor((y - t.radio) / T), j1 = (int)Math::floor((y + t.radio) / T);
            for (int j = MAX(j0, w.y); j <= MIN(j1, w.y + GL_SOMBRA_N - 1); j++)
                for (int i = MAX(i0, w.x); i <= MIN(i1, w.x + GL_SOMBRA_N - 1); i++) {
                    const int k = _hueco(Vector2i(i, j));
                    if (a.hueco[k] == Vector2i(i, j)) sucio[k] = true;
                }
        }
        v.clear();
    }

    void _programar(const Vector3& ojo) {
        if (pendiente >= 0) {
            if (copia_tile->copias.load() > copias_al_pedir) {
                at[pendiente_atlas].ok[pendiente] = true;
                if (pendiente_atlas == frente) sucio[pendiente] = false;
                pendiente = -1;
            } else if (_n_frame() - frame_pedido > 8) {
                pendiente = -1;   // no llego a dibujarse: se vuelve a pedir
            } else {
                return;
            }
        }
        Vector2i ij;
        if (rehaciendo) {
            const int atras = 1 - frente;
            if (_falta(atras, ojo, ij)) { _dibujar_baldosa(atras, ij, ojo); return; }
            frente = atras;            // el atlas nuevo esta completo: pasa delante
            rehaciendo = false;
            for (int k = 0; k < GL_SOMBRA_HUECOS; k++) sucio[k] = false;
        }
        if (_falta(frente, ojo, ij)) { _dibujar_baldosa(frente, ij, ojo); return; }
        // Lo que cambio, lo mas cerca primero.
        const Vector2i w = _ventana(at[frente], ojo);
        const Vector2i c = w + Vector2i(GL_SOMBRA_N / 2, GL_SOMBRA_N / 2);
        int mejor = INT32_MAX, mk = -1;
        for (int k = 0; k < GL_SOMBRA_HUECOS; k++) {
            if (!sucio[k]) continue;
            const Vector2i h = at[frente].hueco[k];
            const int d = (h.x - c.x) * (h.x - c.x) + (h.y - c.y) * (h.y - c.y);
            if (d < mejor) { mejor = d; mk = k; }
        }
        if (mk >= 0) { _dibujar_baldosa(frente, at[frente].hueco[mk], ojo); return; }
        // Repaso lento, por si algo cambio sin avisar (lo normal avisa: aparecer,
        // irse, moverse, cambiar de tamaño o de visibilidad). Cada baldosa cuesta
        // ~0,7 ms: una cada 4 frames eran 0,18 ms de media.
        if (_n_frame() - frame_rr >= (uint32_t)rr_cada) {
            frame_rr = _n_frame();
            rr = (rr + 1) % GL_SOMBRA_HUECOS;
            _dibujar_baldosa(frente, _esperada(rr, w), ojo);
        }
    }

    void _v4(int i, float x, float y, float z, float w) {
        float* d = pantalla->datos + i * 4;
        d[0] = x; d[1] = y; d[2] = z; d[3] = w;
    }
    void _v4(int i, const Vector3& v, float w) { _v4(i, v.x, v.y, v.z, w); }

    void _rellenar(Camera3D* cam, const Vector3& dir, const Basis& bd, const Vector2& esquina) {
        const Transform3D t = cam->get_global_transform();
        _v4(0, t.basis.get_column(0), 0);
        _v4(1, t.basis.get_column(1), 0);
        _v4(2, t.basis.get_column(2), 0);
        _v4(3, t.origin, 0);
        const Vector2 tam = get_viewport()->get_visible_rect().size;
        const float aspecto = tam.x / MAX(tam.y, 1.0f);
        const float ta = Math::tan(Math::deg_to_rad(cam->get_fov()) * 0.5f);
        const bool alto = cam->get_keep_aspect_mode() == Camera3D::KEEP_HEIGHT;
        _v4(4, alto ? ta * aspecto : ta, alto ? ta : ta / aspecto, cam->get_near(), cam->get_far());
        _v4(5, 0, 0, (float)modo, opac);
        // Penumbra (sombra_mapa del shader): base + distancia al bloqueador x
        // tan(tamaño del sol), con ShadowSoftness del Lighting de Roblox (0,2
        // de serie): 0,2 studs pegado al objeto y ~1,2 a 40 studs de el.
        const float blando = _suavidad();
        _v4(6, dir, suave_env > 0.0f ? suave_env : 0.1f + 0.5f * blando);
        _v4(7, 0, 0, 0, 1);
        const Atlas& a = at[frente];
        _v4(8, a.b.get_column(0), T);
        _v4(9, a.b.get_column(1), (float)GL_SOMBRA_N);
        _v4(10, (float)tile_px, (float)(GL_SOMBRA_N * tile_px), sesgo, normal_off);
        const Vector2i w = _ventana(a, t.origin);
        _v4(11, (float)w.x, (float)w.y, Math::tan(Math::deg_to_rad(0.6f + 6.0f * blando)), 1.6f);
        for (int k = 0; k < 28; k++) {
            float v = 0.0f;
            if (k < GL_SOMBRA_HUECOS) v = (a.ok[k] && a.hueco[k] == _esperada(k, w)) ? 1.0f : 0.0f;
            pantalla->datos[12 * 4 + k] = v;
        }
        _v4(19, bd.get_column(0), esquina.x);
        _v4(20, bd.get_column(1), esquina.y);
        _v4(21, din_mitad * 2.0f, (float)din_px, 0, 0);
        // La niebla de Godot, para no oscurecer la parte que es niebla.
        float niebla[8] = {0, 0, 0, 0, 1, 0, 0, 0};
        if (WorldEnvironment* we = _we()) {
            Ref<Environment> env = we->get_environment();
            if (env.is_valid() && env->is_fog_enabled()) {
                const bool prof = env->get_fog_mode() == Environment::FOG_MODE_DEPTH;
                niebla[0] = prof ? 2.0f : 1.0f;
                niebla[1] = env->get_fog_density();
                niebla[2] = env->get_fog_depth_begin();
                niebla[3] = env->get_fog_depth_end();
                niebla[4] = env->get_fog_depth_curve();
                niebla[5] = env->get_fog_height();
                niebla[6] = env->get_fog_height_density();
            }
        }
        _v4(22, niebla[0], niebla[1], niebla[2], niebla[3]);
        _v4(23, niebla[4], niebla[5], niebla[6], 0);
        pantalla->atlas = a.tex;
    }

    void _frame() {
        if (!gl_sombras_propias_quiere() || !_juego_quiere()) { _apagar(); return; }
        if (!montado && !_montar()) return;
        DirectionalLight3D* sol = _sol();
        Camera3D* cam = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
        if (!sol || !cam || cam->get_projection() != Camera3D::PROJECTION_PERSPECTIVE || !atlas_creados) {
            _apagar();
            return;
        }
        const Vector3 dir = sol->get_global_transform().basis.get_column(2).normalized();   // hacia el sol
        const Vector3 arriba = Math::abs(dir.y) < 0.999f ? Vector3(0, 1, 0) : Vector3(0, 0, 1);
        const Basis bd = Basis::looking_at(-dir, arriba);
        // La sonda (gl_sonda.h) mide el ambiente y el sol de Godot.
        GLSonda* sonda = gl_sonda();
        const bool sonda_lista = sonda && sonda->lista();
        if (sonda_lista) pantalla->sonda = sonda->textura_a();
        if (!at[frente].usado) {
            at[frente].b = bd;
            at[frente].dir = dir;
            at[frente].usado = true;
        } else if (!rehaciendo && dir.dot(at[frente].dir) < 0.99996f) {
            // Sol nuevo (medio grado): el otro atlas se dibuja con el y, al
            // terminar, pasa delante. Mientras, el de delante sigue valiendo.
            Atlas& b = at[1 - frente];
            b.b = bd;
            b.dir = dir;
            b.usado = true;
            for (int k = 0; k < GL_SOMBRA_HUECOS; k++) b.ok[k] = false;
            rehaciendo = true;
        }
        const Vector3 ojo = cam->get_global_position();
        _repasar_moviles();
        _recoger_toques(ojo);
        _programar(ojo);
        // Mapa de lo que se mueve: un poco por delante de la camara, pegado a sus
        // texeles. Por encima de 120 FPS, uno de cada dos frames (sigue yendo a
        // mas de 60 por segundo; MEDIDO: -0,12 ms en calidad 7); entre medias se
        // usa el ultimo con SU sitio y sus ejes. GL_SOMBRAS_DIN_CADA lo fija.
        const int cada = din_cada > 0 ? din_cada : (Engine::get_singleton()->get_frames_per_second() > 120.0 ? 2 : 1);
        if (cada <= 1 || !din_hecho || _n_frame() % (uint32_t)cada == 0) {
            Vector3 delante = -cam->get_global_transform().basis.get_column(2);
            delante.y = 0.0f;
            if (delante.length() > 0.01f) delante.normalize();
            const Vector3 centro = ojo + delante * (din_mitad * 0.4f);
            const float texel = din_mitad * 2.0f / (float)din_px;
            din_lx = Math::snapped(bd.get_column(0).dot(centro), texel);
            din_ly = Math::snapped(bd.get_column(1).dot(centro), texel);
            din_bd = bd;
            const Vector3 o = bd.get_column(0) * din_lx + bd.get_column(1) * din_ly + dir * dir.dot(centro);
            cam_din->set_global_transform(Transform3D(bd, o));
            copia_din->desfase = o.dot(-dir);
            if (cada <= 1) {
                if (vp_din->get_update_mode() != SubViewport::UPDATE_ALWAYS) vp_din->set_update_mode(SubViewport::UPDATE_ALWAYS);
            } else {
                vp_din->set_update_mode(SubViewport::UPDATE_ONCE);
            }
            din_hecho = true;
        }
        _rellenar(cam, dir, din_bd, Vector2(din_lx - din_mitad, din_ly - din_mitad));
        // Listo cuando la sonda midio y esta la baldosa de la camara: entonces se
        // apaga la sombra de Godot (hasta ahi pintaba ella, sin hueco).
        const Atlas& a = at[frente];
        const Vector2i w = _ventana(a, ojo);
        const Vector2i cij = w + Vector2i(GL_SOMBRA_N / 2, GL_SOMBRA_N / 2);
        const int ck = _hueco(cij);
        if (!listo && sonda_lista && pantalla->sonda.is_valid() && a.ok[ck] && a.hueco[ck] == cij) {
            listo = true;
            gl_sombras_propias_listas() = true;
            pantalla->set_enabled(true);
            sol->set_shadow(false);
        } else if (listo && sol->has_shadow()) {
            sol->set_shadow(false);   // alguien la volvio a encender (Lighting, calidad)
        }
        _sombra_a_shaders(listo);
    }

protected:
    static void _bind_methods() {}

public:
    GLSombras() {
        OS* os = OS::get_singleton();
        if (os->get_environment("GL_SOMBRAS_MODO") != "") modo = os->get_environment("GL_SOMBRAS_MODO").to_int();
        if (os->get_environment("GL_SOMBRAS_SUAVE") != "") suave_env = os->get_environment("GL_SOMBRAS_SUAVE").to_float();
        if (os->get_environment("GL_SOMBRAS_RR") != "") rr_cada = MAX(1, (int)os->get_environment("GL_SOMBRAS_RR").to_int());
        if (os->get_environment("GL_SOMBRAS_DIN_CADA") != "") din_cada = MAX(1, (int)os->get_environment("GL_SOMBRAS_DIN_CADA").to_int());
        for (int i = 0; i < 2; i++)
            for (int k = 0; k < GL_SOMBRA_HUECOS; k++) at[i].hueco[k] = Vector2i(INT32_MIN / 2, INT32_MIN / 2);
    }

    ~GLSombras() {
        gl_sombras_propias_listas() = false;
        RenderingServer* rs = RenderingServer::get_singleton();
        RenderingDevice* rd = rs ? rs->get_rendering_device() : nullptr;
        if (!rd) return;
        for (int i = 0; i < 2; i++)
            if (at[i].tex.is_valid()) rd->free_rid(at[i].tex);
    }

    void _notification(int p_what) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (p_what == NOTIFICATION_READY) {
            // Tambien con el arbol en pausa: la camara puede moverse igual.
            set_process_mode(PROCESS_MODE_ALWAYS);
            set_process(true);
        }
        else if (p_what == NOTIFICATION_PROCESS) _frame();
        else if (p_what == NOTIFICATION_EXIT_TREE) _apagar();
    }
};

#endif // GL_SOMBRAS_H

#ifndef GL_SONDA_H
#define GL_SONDA_H

// ════════════════════════════════════════════════════════════════════
//  La SONDA: mide la luz que pone Godot y se la pasa a lo nuestro.
//   - Las sombras propias (gl_sombras.h) leen su textura en la GPU: quitar solo
//     la luz del sol de un pixel pide saber cuanto ambiente y cuanto sol le
//     llegan.
//   - La luz propia de las piezas (core/gl_luz_propia.h) la lee en la CPU con
//     una lectura asincrona (sin parar la GPU) y la pone en uniforms globales.
//     Cada frame pone ademas la direccion del sol y las 8 luces puntuales o
//     focos que mas cuentan para la camara.
//  Son cuadros escondidos bajo el mundo que dibuja el propio Godot:
//   fila A (8): pieza blanca mate con la normal en +x -x +y -y +z -z y en la
//     direccion del sol, sin sol (6) y con sol (7);
//   fila B (6): lo mismo con el ambiente entero de cielo, que es la luz con la
//     que Godot pinta el reflejo del cielo (su nivel mas borroso).
//  Se vuelve a medir al cambiar la luz (gl_luz_version), al moverse el sol o
//  cada 2 s.
// ════════════════════════════════════════════════════════════════════

#include <cstring>
#include <mutex>
#include "gl_postfx.h"   // gl_postfx_disponible, includes de RD
#include "../core/gl_capas.h"
#include "../core/gl_luz_propia.h"
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/spot_light3d.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/world_environment.hpp>

static const float GL_SONDA_BAJO = -100000.0f;

static const char* GL_SONDA_SHADER = R"(shader_type spatial;
render_mode cull_disabled, diffuse_lambert, specular_disabled, fog_disabled;
uniform vec3 dir_n = vec3(0.0, 1.0, 0.0);
void fragment() {
	ALBEDO = vec3(1.0);
	ROUGHNESS = 1.0;
	METALLIC = 0.0;
	SPECULAR = 0.0;
	NORMAL = normalize((VIEW_MATRIX * vec4(dir_n, 0.0)).xyz);
}
)";

inline uint64_t& gl_sonda_id() {
    static uint64_t id = 0;
    return id;
}

// Medio flotante (IEEE 754 binario 16) a float.
static inline float gl_medio(uint16_t h) {
    const uint32_t s = (uint32_t)(h & 0x8000u) << 16;
    const uint32_t e = (h >> 10) & 0x1Fu;
    const uint32_t m = h & 0x3FFu;
    float v;
    if (e == 0) v = (float)m * (1.0f / 16777216.0f);              // subnormal: m * 2^-24
    else if (e == 31) v = 65504.0f;                               // inf/NaN: el maximo
    else {
        uint32_t b = ((e + 112u) << 23) | (m << 13);
        memcpy(&v, &b, 4);
    }
    return s ? -v : v;
}

class GLSonda : public Node {
    GDCLASS(GLSonda, Node);

    SubViewport* vp[2] = {nullptr, nullptr};
    Camera3D* cam[2] = {nullptr, nullptr};
    Ref<ShaderMaterial> mat_sol[2];
    uint64_t sol_id = 0, we_id = 0;
    bool montada = false;

    uint32_t luz_vista = 0;
    Vector3 dir_vista;
    uint64_t t_medida = 0;
    uint32_t frame_pedido = 0;
    bool pedida = false;
    bool lista_gpu = false;

    // Lectura en la CPU (con la luz propia). El aviso llega en el hilo de render.
    std::mutex cerrojo;
    PackedByteArray leido[2];
    bool nuevo[2] = {false, false};
    bool con_datos = false;

    // Lo ultimo puesto en los globales de las luces.
    Vector4 ult[4][GL_LUZ_MAX];
    int ult_n = -1;
    Vector3 ult_dir;
    Vector3 ult_sol;
    Vector4 sol_puesto = Vector4(-1, -1, -1, -1);
    bool estimado = false;

    static uint32_t _n_frame() { return (uint32_t)Engine::get_singleton()->get_process_frames(); }

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

    // Una fila: viewport de n cuadros de 16x16 con su camara ortogonal. Las dos
    // filas estan en el mundo del juego, lejos de todo y separadas entre si.
    void _crear_fila(int f, int n, DirectionalLight3D* sol) {
        const float y = GL_SONDA_BAJO - 10.0f * (float)f;
        SubViewport* v = memnew(SubViewport);
        v->set_size(Vector2i(n * 16, 16));
        v->set_use_hdr_2d(true);
        v->set_update_mode(SubViewport::UPDATE_DISABLED);
        v->set_msaa_3d(Viewport::MSAA_DISABLED);
        v->set_positional_shadow_atlas_size(0);
        v->set_disable_input(true);
        add_child(v, false, Node::INTERNAL_MODE_BACK);
        Camera3D* c = memnew(Camera3D);
        c->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
        c->set_keep_aspect_mode(Camera3D::KEEP_HEIGHT);
        c->set_size(1.0f);
        c->set_near(0.1f);
        c->set_far(20.0f);
        // Tambien las capas del sol: una camara solo usa las luces cuyas capas ve.
        c->set_cull_mask(GL_CAPA_SONDA | GL_CAPA_SONDA_SOL | sol->get_layer_mask());
        c->set_compositor(Ref<Compositor>(memnew(Compositor)));
        v->add_child(c);
        c->set_position(Vector3(0, y, 5));
        c->set_current(true);
        Ref<Shader> sh;
        sh.instantiate();
        sh->set_code(String(GL_SONDA_SHADER));
        Ref<QuadMesh> q;
        q.instantiate();
        q->set_size(Vector2(1, 1));
        static const Vector3 ejes[6] = {Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0),
                                        Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1)};
        for (int i = 0; i < n; i++) {
            MeshInstance3D* m = memnew(MeshInstance3D);
            m->set_mesh(q);
            Ref<ShaderMaterial> mt;
            mt.instantiate();
            mt->set_shader(sh);
            if (i < 6) mt->set_shader_parameter("dir_n", ejes[i]);
            else mat_sol[i - 6] = mt;
            m->set_material_override(mt);
            // Solo el cuadro 7 de la fila A recibe el sol.
            m->set_layer_mask(f == 0 && i == 7 ? GL_CAPA_SONDA_SOL : GL_CAPA_SONDA);
            m->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
            v->add_child(m);
            m->set_position(Vector3(-0.5f * (float)n + 0.5f + (float)i, y, 0));
        }
        vp[f] = v;
        cam[f] = c;
    }

    bool _montar() {
        DirectionalLight3D* sol = _sol();
        WorldEnvironment* we = _we();
        if (!sol || !we || we->get_environment().is_null()) return false;
        _crear_fila(0, 8, sol);
        _crear_fila(1, 6, sol);
        montada = true;
        return true;
    }

    // El ambiente del juego tal cual, sin lo que se pone despues de la luz.
    static Ref<Environment> _entorno_sonda(const Ref<Environment>& env, bool solo_cielo) {
        Ref<Environment> p = env->duplicate();
        if (p.is_null()) return p;
        p->set_tonemapper(Environment::TONE_MAPPER_LINEAR);
        p->set_tonemap_exposure(1.0f);
        p->set_glow_enabled(false);
        p->set_fog_enabled(false);
        p->set_volumetric_fog_enabled(false);
        p->set_adjustment_enabled(false);
        p->set_ssao_enabled(false);
        p->set_ssil_enabled(false);
        p->set_ssr_enabled(false);
        p->set_sdfgi_enabled(false);
        if (solo_cielo) {
            // Mismo cielo y misma energia que el reflejo de Godot, sin el color.
            p->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
            p->set_ambient_light_sky_contribution(1.0f);
        }
        return p;
    }

    void _medir(const Vector3& dir, uint64_t ahora) {
        if (pedida) {
            if (_n_frame() - frame_pedido < 2) return;
            pedida = false;
            lista_gpu = true;
            if (gl_luz_propia_activa())
                RenderingServer::get_singleton()->call_on_render_thread(callable_mp(this, &GLSonda::_pedir_lectura));
            return;
        }
        const bool cambio = luz_vista != gl_luz_version() || dir.dot(dir_vista) < 0.99996f;
        if (t_medida != 0 && !cambio && ahora - t_medida < 2000) return;
        WorldEnvironment* we = _we();
        Ref<Environment> env = we ? we->get_environment() : Ref<Environment>();
        if (env.is_null()) return;
        Ref<Environment> pa = _entorno_sonda(env, false);
        Ref<Environment> pb = _entorno_sonda(env, true);
        if (pa.is_null() || pb.is_null()) return;
        cam[0]->set_environment(pa);
        cam[1]->set_environment(pb);
        mat_sol[0]->set_shader_parameter("dir_n", dir);
        mat_sol[1]->set_shader_parameter("dir_n", dir);
        vp[0]->set_update_mode(SubViewport::UPDATE_ONCE);
        vp[1]->set_update_mode(SubViewport::UPDATE_ONCE);
        pedida = true;
        frame_pedido = _n_frame();
        t_medida = ahora;
        luz_vista = gl_luz_version();
        dir_vista = dir;
    }

    // En el hilo de render: pedir los pixeles de las dos filas.
    void _pedir_lectura() {
        RenderingServer* rs = RenderingServer::get_singleton();
        RenderingDevice* rd = rs->get_rendering_device();
        if (!rd) return;
        for (int f = 0; f < 2; f++) {
            if (!vp[f]) continue;
            Ref<ViewportTexture> vt = vp[f]->get_texture();
            if (vt.is_null()) continue;
            const RID t = rs->texture_get_rd_texture(vt->get_rid());
            if (!t.is_valid()) continue;
            rd->call("texture_get_data_async", t, 0,
                     f == 0 ? callable_mp(this, &GLSonda::_leido_a) : callable_mp(this, &GLSonda::_leido_b));
        }
    }
    void _leido(int f, const PackedByteArray& d) {
        std::lock_guard<std::mutex> g(cerrojo);
        leido[f] = d;
        nuevo[f] = true;
    }
    void _leido_a(const PackedByteArray& d) { _leido(0, d); }
    void _leido_b(const PackedByteArray& d) { _leido(1, d); }

    // El centro del cuadro i de una fila leida (RGBA de 16 bits o de 32).
    static bool _pixel(const PackedByteArray& d, int ancho, int i, Vector3& sale) {
        const int x = i * 16 + 8, y = 8;
        const int64_t n = (int64_t)ancho * 16;
        if (n <= 0) return false;
        const int64_t bpp = d.size() / n;
        const uint8_t* p = d.ptr();
        const int64_t o = ((int64_t)y * ancho + x) * bpp;
        if (o + bpp > d.size()) return false;
        if (bpp == 8) {
            uint16_t h[3];
            memcpy(h, p + o, 6);
            sale = Vector3(gl_medio(h[0]), gl_medio(h[1]), gl_medio(h[2]));
            return true;
        }
        if (bpp == 16) {
            float c[3];
            memcpy(c, p + o, 12);
            sale = Vector3(c[0], c[1], c[2]);
            return true;
        }
        return false;
    }

    void _global(const char* n, const Variant& v) {
        RenderingServer::get_singleton()->global_shader_parameter_set(StringName(n), v);
    }

    // Lo leido a los globales de la luz propia.
    void _aplicar_lecturas() {
        PackedByteArray d[2];
        bool hay[2] = {false, false};
        {
            std::lock_guard<std::mutex> g(cerrojo);
            for (int f = 0; f < 2; f++) {
                if (!nuevo[f]) continue;
                d[f] = leido[f];
                hay[f] = true;
                nuevo[f] = false;
            }
        }
        RenderingServer* rs = RenderingServer::get_singleton();
        if (hay[0]) {
            Vector3 c[8];
            bool ok = true;
            for (int i = 0; i < 8 && ok; i++) ok = _pixel(d[0], 8 * 16, i, c[i]);
            if (ok) {
                for (int i = 0; i < 6; i++) rs->global_shader_parameter_set(StringName(String("gl_amb_") + GL_LUZ_EJES[i]), c[i]);
                ult_sol = Vector3(MAX(c[7].x - c[6].x, 0.0f), MAX(c[7].y - c[6].y, 0.0f), MAX(c[7].z - c[6].z, 0.0f));
                con_datos = true;
            }
        }
        if (hay[1]) {
            // Sin reflejo del cielo en el entorno: nada que reflejar.
            WorldEnvironment* we = _we();
            Ref<Environment> env = we ? we->get_environment() : Ref<Environment>();
            const bool refleja = env.is_valid() && env->get_reflection_source() != Environment::REFLECTION_SOURCE_DISABLED;
            Vector3 c[6];
            bool ok = true;
            for (int i = 0; i < 6 && ok; i++) ok = _pixel(d[1], 6 * 16, i, c[i]);
            if (ok)
                for (int i = 0; i < 6; i++)
                    rs->global_shader_parameter_set(StringName(String("gl_ibl_") + GL_LUZ_EJES[i]), refleja ? c[i] : Vector3());
        }
    }

    // Antes de la primera lectura: el color del ambiente y del sol de Godot,
    // para que las piezas no salgan negras los primeros frames.
    void _estimar(DirectionalLight3D* sol) {
        WorldEnvironment* we = _we();
        Ref<Environment> env = we ? we->get_environment() : Ref<Environment>();
        if (env.is_null()) return;
        const Color a = env->get_ambient_light_color().srgb_to_linear();
        const float e = env->get_ambient_light_energy();
        const Vector3 amb(a.r * e, a.g * e, a.b * e);
        RenderingServer* rs = RenderingServer::get_singleton();
        for (int i = 0; i < 6; i++) {
            rs->global_shader_parameter_set(StringName(String("gl_amb_") + GL_LUZ_EJES[i]), amb);
            rs->global_shader_parameter_set(StringName(String("gl_ibl_") + GL_LUZ_EJES[i]), amb);
        }
        const Color s = sol->get_color().srgb_to_linear();
        const float se = sol->is_visible_in_tree() ? sol->get_param(Light3D::PARAM_ENERGY) : 0.0f;
        ult_sol = Vector3(s.r * se, s.g * se, s.b * se);
        estimado = true;
    }

    // El sol (direccion y especular) y las 8 luces que mas cuentan: dentro de
    // la vista y, de esas, las mas cercanas a la camara (contando su alcance).
    void _luces(DirectionalLight3D* sol, Camera3D* cam3) {
        RenderingServer* rs = RenderingServer::get_singleton();
        const Vector3 dir = sol->get_global_transform().basis.get_column(2).normalized();
        if (dir != ult_dir) { _global("gl_sol_dir", dir); ult_dir = dir; }
        const float esp = sol->get_param(Light3D::PARAM_SPECULAR);
        const Vector4 sc(ult_sol.x, ult_sol.y, ult_sol.z, esp);
        if (sc != sol_puesto) { _global("gl_sol_col", sc); sol_puesto = sc; }

        struct Cand { float s; Light3D* l; };
        Cand mejor[GL_LUZ_MAX];
        int n = 0;
        if (cam3) {
            const Vector3 ojo = cam3->get_global_position();
            const TypedArray<Plane> fr = cam3->get_frustum();
            LocalVector<Plane> planos;
            for (int i = 0; i < fr.size(); i++) planos.push_back((Plane)fr[i]);
            for (const KeyValue<uint64_t, bool>& e : gl_luces_registro()) {
                // Las de Roblox estan ocultas para Godot (roblox_luces.h): el valor
                // dice si alumbran (Enabled y en el Workspace), apuntado por la luz.
                if (!e.value) continue;
                Light3D* l = Object::cast_to<Light3D>(ObjectDB::get_instance(ObjectID(e.key)));
                if (!l) continue;
                const float r = l->get_param(Light3D::PARAM_RANGE);
                if (r <= 0.0f || l->get_param(Light3D::PARAM_ENERGY) <= 0.0f) continue;
                const Vector3 p = l->get_global_position();
                bool fuera = false;
                for (const Plane& pl : planos)
                    if (pl.distance_to(p) > r) { fuera = true; break; }
                if (fuera) continue;
                const float s = p.distance_to(ojo) - r;
                if (n < GL_LUZ_MAX) mejor[n++] = {s, l};
                else if (s < mejor[GL_LUZ_MAX - 1].s) mejor[GL_LUZ_MAX - 1] = {s, l};
                else continue;
                for (int k = n - 1; k > 0 && mejor[k].s < mejor[k - 1].s; k--) {
                    const Cand t = mejor[k];
                    mejor[k] = mejor[k - 1];
                    mejor[k - 1] = t;
                }
            }
        }
        static const char* nombres[4] = {"gl_lp", "gl_lc", "gl_ld", "gl_la"};
        for (int i = 0; i < n; i++) {
            Light3D* l = mejor[i].l;
            const Transform3D t = l->get_global_transform();
            const float r = MAX(l->get_param(Light3D::PARAM_RANGE), 0.001f);
            const Color c = l->get_color().srgb_to_linear();
            const float e = l->get_param(Light3D::PARAM_ENERGY) * (l->is_negative() ? -1.0f : 1.0f);
            Vector4 v[4];
            v[0] = Vector4(t.origin.x, t.origin.y, t.origin.z, 1.0f / r);
            v[1] = Vector4(c.r * e, c.g * e, c.b * e, l->get_param(Light3D::PARAM_SIZE));
            const Vector3 fw = -t.basis.get_column(2).normalized();
            const bool foco = Object::cast_to<SpotLight3D>(l) != nullptr;
            v[2] = Vector4(fw.x, fw.y, fw.z, foco ? Math::cos(Math::deg_to_rad(l->get_param(Light3D::PARAM_SPOT_ANGLE))) : -2.0f);
            v[3] = Vector4(l->get_param(Light3D::PARAM_ATTENUATION),
                           1.0f / MAX(l->get_param(Light3D::PARAM_SPOT_ATTENUATION), 0.0001f),
                           l->get_param(Light3D::PARAM_SPECULAR) * 2.0f, 0.0f);
            for (int k = 0; k < 4; k++) {
                if (v[k] == ult[k][i]) continue;
                ult[k][i] = v[k];
                rs->global_shader_parameter_set(StringName(String(nombres[k]) + String::num_int64(i)), v[k]);
            }
        }
        if (n != ult_n) { _global("gl_luz_n", n); ult_n = n; }
        static const bool traza = OS::get_singleton()->get_environment("GL_LUZ_TRAZA") == "1";
        if (traza && _n_frame() % 60 == 0) {
            UtilityFunctions::print("[LUZ] registradas=", (int64_t)gl_luces_registro().size(), " elegidas=", n,
                                    n > 0 ? String(" 1a: p=") + String(Vector3(ult[0][0].x, ult[0][0].y, ult[0][0].z)) +
                                            " c=" + String(Vector4(ult[1][0])) + " d=" + String(Vector4(ult[2][0])) +
                                            " a=" + String(Vector4(ult[3][0])) : String(),
                                    " sol=", Vector4(ult_sol.x, ult_sol.y, ult_sol.z, 0), " datos=", con_datos);
        }
    }

    void _frame() {
        if (!gl_postfx_disponible()) return;
        if (!montada && !_montar()) return;
        DirectionalLight3D* sol = _sol();
        if (!sol) return;
        // El sol no alumbra los cuadros de la sonda salvo el suyo.
        const uint32_t mascara = (sol->get_cull_mask() & ~GL_CAPA_SONDA) | GL_CAPA_SONDA_SOL;
        if (sol->get_cull_mask() != mascara) sol->set_cull_mask(mascara);
        const Vector3 dir = sol->get_global_transform().basis.get_column(2).normalized();   // hacia el sol
        _medir(dir, Time::get_singleton()->get_ticks_msec());
        if (!gl_luz_propia_activa()) return;
        gl_luz_globales();
        if (!con_datos && !estimado) _estimar(sol);
        _aplicar_lecturas();
        _luces(sol, get_viewport() ? get_viewport()->get_camera_3d() : nullptr);
    }

protected:
    static void _bind_methods() {}

public:
    // La textura de la fila A (para las sombras) y si ya tiene una medida.
    bool lista() const { return lista_gpu; }
    RID textura_a() const {
        if (!vp[0]) return RID();
        Ref<ViewportTexture> vt = vp[0]->get_texture();
        return vt.is_valid() ? RenderingServer::get_singleton()->texture_get_rd_texture(vt->get_rid()) : RID();
    }

    void _notification(int p_what) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (p_what == NOTIFICATION_ENTER_TREE) gl_sonda_id() = (uint64_t)get_instance_id();
        else if (p_what == NOTIFICATION_READY) {
            // Tambien con el arbol en pausa: la camara puede moverse igual.
            set_process_mode(PROCESS_MODE_ALWAYS);
            set_process(true);
        }
        else if (p_what == NOTIFICATION_PROCESS) _frame();
        else if (p_what == NOTIFICATION_EXIT_TREE && gl_sonda_id() == (uint64_t)get_instance_id()) gl_sonda_id() = 0;
    }
};

inline GLSonda* gl_sonda() {
    return Object::cast_to<GLSonda>(ObjectDB::get_instance(ObjectID(gl_sonda_id())));
}

#endif // GL_SONDA_H

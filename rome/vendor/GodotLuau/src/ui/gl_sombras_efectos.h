#ifndef GL_SOMBRAS_EFECTOS_H
#define GL_SOMBRAS_EFECTOS_H

// Los dos CompositorEffect de las sombras propias (ver gl_sombras.h):
//  - GLSombraCopia, en cada camara del sol: copia su profundidad a un mapa.
//  - GLSombraPantalla, en el WorldEnvironment del juego: pinta la sombra.
// Los tres shaders de computo se compilan una vez, en un hilo del
// WorkerThreadPool (glslang tarda decenas de ms), y se comparten.

#include "gl_postfx.h"            // includes de RD, gl_postfx_disponible
#include "gl_sombras_shaders.h"
#include <godot_cpp/classes/render_scene_data.hpp>
#include <godot_cpp/templates/local_vector.hpp>

struct GLSombraRD {
    std::atomic<int> estado{0};   // 0 sin empezar, 1 compilando, 2 binarios, 3 fallo, 4 creados
    PackedByteArray binario[3];   // copia, mascara, aplicar
    RID sh[3], pip[3];
    RID cerca, repite;            // muestreo sin filtro: con borde y en mosaico
    int64_t tarea = -1;
    RenderingDevice* rd = nullptr;
};
inline GLSombraRD& gl_sombra_rd() { static GLSombraRD r; return r; }

inline void gl_sombra_compilar_hilo() {
    GLSombraRD& g = gl_sombra_rd();
    RenderingDevice* r = RenderingServer::get_singleton()->get_rendering_device();
    const String fuentes[3] = {
        String(GL_SOMBRA_COPIA),
        String(GL_SOMBRA_COMUN) + String(GL_SOMBRA_MASCARA),
        String(GL_SOMBRA_COMUN) + String(GL_SOMBRA_APLICAR),
    };
    for (int i = 0; i < 3; i++) {
        Ref<RDShaderSource> f;
        f.instantiate();
        f->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
        f->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, fuentes[i]);
        Ref<RDShaderSPIRV> spirv = r ? r->shader_compile_spirv_from_source(f) : Ref<RDShaderSPIRV>();
        const String error = spirv.is_valid() ? spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE) : String("sin SPIR-V");
        if (!error.is_empty()) {
            UtilityFunctions::push_error("GLSombras: ", error);
            g.estado = 3;
            return;
        }
        g.binario[i] = r->shader_compile_binary_from_spirv(spirv, "GLSombras");
        if (g.binario[i].is_empty()) {
            g.estado = 3;
            return;
        }
    }
    g.estado = 2;
}

inline void gl_sombra_empezar_compilacion() {
    GLSombraRD& g = gl_sombra_rd();
    int esperado = 0;
    if (!g.estado.compare_exchange_strong(esperado, 1)) return;
    g.tarea = WorkerThreadPool::get_singleton()->add_task(callable_mp_static(&gl_sombra_compilar_hilo), false, "GLSombras");
}

// En el hilo de render: crea los shaders desde el binario la primera vez.
inline bool gl_sombra_rd_listo() {
    GLSombraRD& g = gl_sombra_rd();
    const int e = g.estado.load();
    if (e == 4) return true;
    if (e == 0) gl_sombra_empezar_compilacion();
    if (e != 2) return false;
    if (g.tarea >= 0) {
        WorkerThreadPool::get_singleton()->wait_for_task_completion(g.tarea);
        g.tarea = -1;
    }
    RenderingDevice* rd = RenderingServer::get_singleton()->get_rendering_device();
    if (!rd) return false;
    for (int i = 0; i < 3; i++) {
        g.sh[i] = rd->shader_create_from_bytecode(g.binario[i]);
        g.pip[i] = g.sh[i].is_valid() ? rd->compute_pipeline_create(g.sh[i]) : RID();
        g.binario[i] = PackedByteArray();
        if (!g.pip[i].is_valid()) { g.estado = 3; return false; }
    }
    for (int k = 0; k < 2; k++) {
        Ref<RDSamplerState> s;
        s.instantiate();
        s->set_min_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
        s->set_mag_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
        const RenderingDevice::SamplerRepeatMode m = k == 0 ? RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE
                                                            : RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT;
        s->set_repeat_u(m);
        s->set_repeat_v(m);
        (k == 0 ? g.cerca : g.repite) = rd->sampler_create(s);
    }
    g.rd = rd;
    g.estado = 4;
    return true;
}

// Al cerrar (register_types): con el servidor de render todavia vivo.
inline void gl_sombra_rd_liberar() {
    GLSombraRD& g = gl_sombra_rd();
    if (g.tarea >= 0) {
        WorkerThreadPool::get_singleton()->wait_for_task_completion(g.tarea);
        g.tarea = -1;
    }
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!g.rd || !rs || rs->get_rendering_device() != g.rd) return;
    for (int i = 0; i < 3; i++) {
        if (g.pip[i].is_valid()) g.rd->free_rid(g.pip[i]);
        if (g.sh[i].is_valid()) g.rd->free_rid(g.sh[i]);
        g.pip[i] = g.sh[i] = RID();
    }
    if (g.cerca.is_valid()) g.rd->free_rid(g.cerca);
    if (g.repite.is_valid()) g.rd->free_rid(g.repite);
    g.cerca = g.repite = RID();
    g.rd = nullptr;
    g.estado = 0;
}

inline Ref<RDUniform> gl_sombra_u_tex(int b, const RID& t, const RID& muestreo) {
    Ref<RDUniform> u;
    u.instantiate();
    u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
    u->set_binding(b);
    u->add_id(muestreo);
    u->add_id(t);
    return u;
}

inline Ref<RDUniform> gl_sombra_u_img(int b, const RID& t) {
    Ref<RDUniform> u;
    u.instantiate();
    u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    u->set_binding(b);
    u->add_id(t);
    return u;
}

inline RID gl_sombra_textura(RenderingDevice* rd, const Vector2i& t, RenderingDevice::DataFormat formato) {
    Ref<RDTextureFormat> f;
    f.instantiate();
    f->set_format(formato);
    f->set_width(t.x);
    f->set_height(t.y);
    f->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_STORAGE_BIT);
    Ref<RDTextureView> v;
    v.instantiate();
    return rd->texture_create(f, v);
}

// ── En cada camara del sol: su profundidad, como D = dot(P, -sol) ─────────
class GLSombraCopia : public CompositorEffect {
    GDCLASS(GLSombraCopia, CompositorEffect);

    RenderingDevice* rd = nullptr;

protected:
    static void _bind_methods() {}

public:
    RID destino;              // el atlas de baldosas; vacio = mapa propio
    Vector2i origen;
    float cerca = -1500.0f, lejos = 1500.0f, desfase = 0.0f;
    RID mapa;                 // propio (el mapa de cada frame)
    Vector2i tam;
    std::atomic<uint32_t> copias{0};

    GLSombraCopia() { set_effect_callback_type(EFFECT_CALLBACK_TYPE_POST_OPAQUE); }

    ~GLSombraCopia() {
        RenderingServer* rs = RenderingServer::get_singleton();
        if (rd && rs && rs->get_rendering_device() == rd && mapa.is_valid()) rd->free_rid(mapa);
    }

    void _render_callback(int32_t p_tipo, RenderData* p_datos) override {
        if (p_tipo != EFFECT_CALLBACK_TYPE_POST_OPAQUE || !p_datos || !gl_sombra_rd_listo()) return;
        Ref<RenderSceneBuffers> b = p_datos->get_render_scene_buffers();
        RenderSceneBuffersRD* rb = Object::cast_to<RenderSceneBuffersRD>(b.ptr());
        if (!rb) return;
        const Vector2i t = rb->get_internal_size();
        if (t.x < 1 || t.y < 1) return;
        GLSombraRD& g = gl_sombra_rd();
        rd = g.rd;
        RID dst = destino;
        Vector2i o = origen;
        if (!dst.is_valid()) {
            if (t != tam || !mapa.is_valid()) {
                if (mapa.is_valid()) rd->free_rid(mapa);
                mapa = gl_sombra_textura(rd, t, RenderingDevice::DATA_FORMAT_R32_SFLOAT);
                tam = t;
            }
            dst = mapa;
            o = Vector2i();
        }
        TypedArray<RDUniform> u;
        u.push_back(gl_sombra_u_tex(0, rb->get_depth_layer(0), g.cerca));
        u.push_back(gl_sombra_u_img(1, dst));
        const float e[8] = {(float)t.x, (float)t.y, cerca, lejos, desfase, (float)o.x, (float)o.y, 0.0f};
        PackedByteArray bytes;
        bytes.resize(sizeof(e));
        memcpy(bytes.ptrw(), e, sizeof(e));
        const int64_t lista = rd->compute_list_begin();
        rd->compute_list_bind_compute_pipeline(lista, g.pip[0]);
        rd->compute_list_bind_uniform_set(lista, UniformSetCacheRD::get_cache(g.sh[0], 0, u), 0);
        rd->compute_list_set_push_constant(lista, bytes, (uint32_t)bytes.size());
        rd->compute_list_dispatch(lista, (t.x + 7) / 8, (t.y + 7) / 8, 1);
        rd->compute_list_end();
        copias.fetch_add(1);
    }
};

// Los datos de GLSombraPantalla: 24 vec4, mismo orden que el bloque P del shader.
static const int GL_SOMBRA_DATOS = 24 * 4;

// ── En la camara del juego: pinta la sombra tras la pasada opaca ──────────
// Con la luz propia de las piezas (core/gl_luz_propia.h) va ANTES de la pasada
// opaca (tras la prepasada de profundidad) y solo hace la mascara: la aplican
// los shaders de las piezas, que quitan exactamente la luz del sol.
class GLSombraPantalla : public CompositorEffect {
    GDCLASS(GLSombraPantalla, CompositorEffect);

    RenderingDevice* rd = nullptr;
    RID ubo;
    Vector2i masc_tam;
    // Al crecer la ventana la mascara se rehace; la vieja vive unos frames
    // mas, porque el material de las piezas la sigue apuntando hasta que el
    // hilo principal le pone la nueva.
    struct Vieja { RID rid; uint64_t frame; };
    LocalVector<Vieja> viejas;

    void _soltar_viejas(bool todas) {
        const uint64_t f = Engine::get_singleton()->get_frames_drawn();
        for (uint32_t i = 0; i < viejas.size();) {
            if (todas || f > viejas[i].frame + 3) {
                if (rd && viejas[i].rid.is_valid()) rd->free_rid(viejas[i].rid);
                viejas.remove_at_unordered(i);
            } else {
                i++;
            }
        }
    }

protected:
    static void _bind_methods() {}

public:
    float datos[GL_SOMBRA_DATOS] = {};
    RID atlas;          // el atlas de delante
    RID sonda;          // textura RD de la sonda
    uint64_t din_id = 0;   // GLSombraCopia del mapa de cada frame
    bool en_shader = false;              // solo la mascara, antes de la pasada opaca
    RID masc;                            // la mascara (luz, distancia) a media resolucion
    std::atomic<uint32_t> masc_gen{0};   // sube cada vez que la mascara se rehace
    std::atomic<int32_t> masc_w{0}, masc_h{0};   // lo que se usa de ella

    GLSombraPantalla() {
        set_effect_callback_type(EFFECT_CALLBACK_TYPE_POST_OPAQUE);
        set_enabled(false);
    }

    ~GLSombraPantalla() {
        RenderingServer* rs = RenderingServer::get_singleton();
        if (!rd || !rs || rs->get_rendering_device() != rd) return;
        _soltar_viejas(true);
        if (masc.is_valid()) rd->free_rid(masc);
        if (ubo.is_valid()) rd->free_rid(ubo);
    }

    void gl_en_shader(bool si) {
        en_shader = si;
        set_effect_callback_type(si ? EFFECT_CALLBACK_TYPE_PRE_OPAQUE : EFFECT_CALLBACK_TYPE_POST_OPAQUE);
    }

    void _render_callback(int32_t p_tipo, RenderData* p_datos) override {
        const int32_t quiere = en_shader ? EFFECT_CALLBACK_TYPE_PRE_OPAQUE : EFFECT_CALLBACK_TYPE_POST_OPAQUE;
        if (p_tipo != quiere || !p_datos || !gl_sombra_rd_listo()) return;
        if (!atlas.is_valid() || !sonda.is_valid()) return;
        Ref<RenderSceneBuffers> b = p_datos->get_render_scene_buffers();
        RenderSceneBuffersRD* rb = Object::cast_to<RenderSceneBuffersRD>(b.ptr());
        if (!rb) return;
        const Vector2i t = rb->get_internal_size();
        if (t.x < 2 || t.y < 2) return;
        GLSombraRD& g = gl_sombra_rd();
        rd = g.rd;
        _soltar_viejas(false);
        const Vector2i hs((t.x + 1) / 2, (t.y + 1) / 2);
        // Solo crece: una ventana mas pequeña usa un trozo de la que hay.
        if (!masc.is_valid() || hs.x > masc_tam.x || hs.y > masc_tam.y) {
            if (masc.is_valid()) viejas.push_back({masc, Engine::get_singleton()->get_frames_drawn()});
            masc_tam = Vector2i(MAX(hs.x, masc_tam.x), MAX(hs.y, masc_tam.y));
            masc = gl_sombra_textura(rd, masc_tam, RenderingDevice::DATA_FORMAT_R16G16_SFLOAT);
            masc_gen.fetch_add(1);
        }
        masc_w.store(hs.x);
        masc_h.store(hs.y);
        RID din;
        if (GLSombraCopia* c = Object::cast_to<GLSombraCopia>(ObjectDB::get_instance(ObjectID(din_id))))
            din = c->mapa;
        float d[GL_SOMBRA_DATOS];
        memcpy(d, datos, sizeof(d));
        d[5 * 4 + 0] = (float)t.x;
        d[5 * 4 + 1] = (float)t.y;
        // La camara CON LA QUE SE DIBUJA este frame, no la que habia al pasar el
        // nodo por _process: el script de camara del juego la mueve despues
        // (RenderStepped), y en una toma rapida (la presentacion de la montaña
        // de Build a Mountain) la profundidad y la camara no casaban: zonas
        // enteras salian en sombra sin nada encima.
        RenderSceneData* sd = p_datos->get_render_scene_data();
        if (sd) {
            const Transform3D ct = sd->get_cam_transform();
            const Projection pr = sd->get_cam_projection();
            for (int c = 0; c < 3; c++) {
                const Vector3 e = ct.basis.get_column(c);
                d[c * 4 + 0] = e.x; d[c * 4 + 1] = e.y; d[c * 4 + 2] = e.z; d[c * 4 + 3] = 0.0f;
            }
            d[3 * 4 + 0] = ct.origin.x; d[3 * 4 + 1] = ct.origin.y; d[3 * 4 + 2] = ct.origin.z; d[3 * 4 + 3] = 0.0f;
            // Es la proyeccion YA corregida para el dibujo: Y invertida y Z al
            // reves (d = 1 en el plano cercano, 0 en el lejano), asi que
            // get_z_near/far no sirven: d(L) = P[3][2] / L - P[2][2].
            const float a = pr.columns[3][2], c22 = pr.columns[2][2];
            if (Math::abs(pr.columns[0][0]) > 1e-6f && Math::abs(pr.columns[1][1]) > 1e-6f && a > 0.0f && c22 > -0.5f) {
                d[4 * 4 + 0] = 1.0f / Math::abs(pr.columns[0][0]);
                d[4 * 4 + 1] = 1.0f / Math::abs(pr.columns[1][1]);
                d[4 * 4 + 2] = a / (1.0f + c22);
                d[4 * 4 + 3] = c22 > 1e-9f ? a / c22 : 1e6f;
            }
            static const bool traza = OS::get_singleton()->get_environment("GL_SOMBRA_TRAZA") == "1";
            if (traza && Engine::get_singleton()->get_frames_drawn() % 120 == 0) {
                UtilityFunctions::print("[SOMBRA] proceso o=", Vector3(datos[12], datos[13], datos[14]), " z=", Vector3(datos[8], datos[9], datos[10]),
                                        " lente=", Vector4(datos[16], datos[17], datos[18], datos[19]));
                UtilityFunctions::print("[SOMBRA] dibujo  o=", ct.origin, " z=", ct.basis.get_column(2),
                                        " lente=", Vector4(d[16], d[17], d[18], d[19]), " P=", pr);
            }
        }
        if (!din.is_valid()) d[7 * 4 + 3] = 0.0f;
        PackedByteArray bytes;
        bytes.resize(sizeof(d));
        memcpy(bytes.ptrw(), d, sizeof(d));
        if (!ubo.is_valid()) ubo = rd->uniform_buffer_create((uint32_t)bytes.size(), bytes);
        else rd->buffer_update(ubo, 0, (uint32_t)bytes.size(), bytes);
        Ref<RDUniform> ub;
        ub.instantiate();
        ub->set_uniform_type(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER);
        ub->set_binding(4);
        ub->add_id(ubo);
        const RID prof = rb->get_depth_layer(0);
        TypedArray<RDUniform> um;
        um.push_back(gl_sombra_u_img(0, masc));
        um.push_back(gl_sombra_u_tex(1, prof, g.cerca));
        um.push_back(gl_sombra_u_tex(3, atlas, g.repite));
        um.push_back(ub);
        um.push_back(gl_sombra_u_tex(5, din.is_valid() ? din : atlas, g.cerca));
        gl_marca_gpu(rd, "GL sombra: mascara");
        int64_t lista = rd->compute_list_begin();
        rd->compute_list_bind_compute_pipeline(lista, g.pip[1]);
        rd->compute_list_bind_uniform_set(lista, UniformSetCacheRD::get_cache(g.sh[1], 0, um), 0);
        rd->compute_list_dispatch(lista, (hs.x + 7) / 8, (hs.y + 7) / 8, 1);
        rd->compute_list_end();
        if (en_shader) {
            gl_marca_gpu(rd, "GL sombra: fin");
            return;
        }
        TypedArray<RDUniform> ua;
        ua.push_back(gl_sombra_u_img(0, rb->get_color_layer(0)));
        ua.push_back(gl_sombra_u_tex(1, prof, g.cerca));
        ua.push_back(gl_sombra_u_tex(2, masc, g.cerca));
        ua.push_back(ub);
        ua.push_back(gl_sombra_u_tex(6, sonda, g.cerca));
        gl_marca_gpu(rd, "GL sombra: aplicar");
        lista = rd->compute_list_begin();
        rd->compute_list_bind_compute_pipeline(lista, g.pip[2]);
        rd->compute_list_bind_uniform_set(lista, UniformSetCacheRD::get_cache(g.sh[2], 0, ua), 0);
        rd->compute_list_dispatch(lista, (t.x + 7) / 8, (t.y + 7) / 8, 1);
        rd->compute_list_end();
        gl_marca_gpu(rd, "GL sombra: fin");
    }
};

#endif // GL_SOMBRAS_EFECTOS_H

#ifndef GL_POSTFX_H
#define GL_POSTFX_H

// ════════════════════════════════════════════════════════════════════
//  Posproceso propio en un CompositorEffect: el bloom de BloomEffect.
//
//  MEDIDO (UHD 770, Build a Mountain a 1457x711): el glow de Godot 4.7
//  costaba 1,5 ms de GPU con un solo nivel encendido (1,13 su pasada y 0,23
//  el muestreo en el tonemap). Con las mismas cuentas hechas aqui, ~0,4.
//
//  Son las cuentas del glow de Godot (copy.glsl y tonemap.glsl de 4.7):
//   - cada nivel es la mitad del anterior (el 0, la mitad de la pantalla) y
//     se difumina con los 9 pesos de Godot, aqui en 5 lecturas bilineales;
//   - el primer nivel pasa por el "tonemap" que quita las lucecitas sueltas
//     y despues por el umbral (smoothstep sobre el maximo de RGB ya
//     multiplicado por la exposicion) y el tope de luminancia;
//   - se suma antes del tonemap, con el peso de cada nivel por la intensidad.
//  Asi el ajuste contra Roblox (BloomEffect: nivel log2(Size/12) a peso 0.6)
//  se ve igual que con el glow de Godot.
//
//  Solo en Forward+: el Mobile guarda el color a la mitad y el Compatibility
//  no tiene CompositorEffect. Alli, en el editor y con GL_GLOW_GODOT=1 (para
//  medir A/B) sigue el glow de Godot.
// ════════════════════════════════════════════════════════════════════

#include <atomic>
#include <cstring>
#include <godot_cpp/classes/compositor.hpp>
#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/uniform_set_cache_rd.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// GL_MARCAS=1: marcas de tiempo de GPU dentro de nuestros efectos (con
// --gpu-profile salen como tramos propios en vez de sumarse a los de Godot).
inline void gl_marca_gpu(RenderingDevice* rd, const char* nombre) {
    static int on = -1;
    if (on < 0) on = OS::get_singleton() && OS::get_singleton()->get_environment("GL_MARCAS") == "1" ? 1 : 0;
    if (on && rd) rd->capture_timestamp(nombre);
}

// Lo que pide un BloomEffect, en los terminos del glow de Godot.
struct GLBloomAjuste {
    bool  on = false;
    float niveles[7] = {0, 0, 0, 0, 0, 0, 0};
    float intensidad = 0.4f;
    float fuerza = 1.0f;    // glow_strength
    float minimo = 0.0f;    // glow_bloom
    float umbral = 0.95f;   // glow_hdr_bleed_threshold
    float escala = 2.0f;    // glow_hdr_bleed_scale
    float tope = 8.0f;      // glow_hdr_luminance_cap
};

// Baja a la resolucion del nivel (bilineal = media de 2x2) y, en el primer
// nivel, aplica el "tonemap" de Godot contra las lucecitas sueltas.
static const char* GL_POSTFX_BAJAR = R"(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D fuente;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2D destino;
layout(push_constant, std430) uniform Empuje { vec2 tam; float antiluz; float r; } p;
void main() {
	ivec2 id = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(id, ivec2(p.tam)))) return;
	vec4 c = textureLod(fuente, (vec2(id) + 0.5) / p.tam, 0.0);
	if (p.antiluz > 0.0) c /= 1.0 + dot(c.rgb, vec3(0.299, 0.587, 0.114) * p.antiluz);
	imageStore(destino, id, c);
}
)";

// Los 9 pesos de Godot (0.2024, 0.1790, 0.1240, 0.0672, 0.0285) en 5 lecturas
// bilineales. La pasada vertical cierra el nivel: deshace el tonemap, aplica
// la fuerza y, en el primero, la exposicion, el umbral y el tope.
static const char* GL_POSTFX_DIFUMINAR = R"(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D fuente;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2D destino;
layout(push_constant, std430) uniform Empuje {
	vec2 tam; vec2 paso;
	float fuerza; float antiluz; float exposicion; float minimo;
	float umbral; float escala; float tope; float cierre;
} p;
void main() {
	ivec2 id = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(id, ivec2(p.tam)))) return;
	vec2 uv = (vec2(id) + 0.5) / p.tam;
	vec4 c = textureLod(fuente, uv, 0.0) * 0.2024;
	c += (textureLod(fuente, uv + p.paso * 1.409241, 0.0) + textureLod(fuente, uv - p.paso * 1.409241, 0.0)) * 0.3030;
	c += (textureLod(fuente, uv + p.paso * 3.297806, 0.0) + textureLod(fuente, uv - p.paso * 3.297806, 0.0)) * 0.0957;
	if (p.cierre > 0.5) {
		if (p.antiluz > 0.0) c /= 1.0 - dot(c.rgb, vec3(0.299, 0.587, 0.114) * p.antiluz);
		c *= p.fuerza;
		if (p.cierre > 1.5) {
			c *= p.exposicion;
			float l = max(c.r, max(c.g, c.b));
			c = min(c * max(smoothstep(p.umbral, p.umbral + p.escala, l), p.minimo), vec4(p.tope));
		}
	}
	imageStore(destino, id, c);
}
)";

// Suma los niveles al color, antes del tonemap. El tonemap de Godot multiplica
// el color por la exposicion y el glow no: por eso se divide aqui.
static const char* GL_POSTFX_SUMAR = R"(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, set = 0, binding = 0) uniform restrict image2D color;
layout(set = 0, binding = 1) uniform sampler2D niveles[7];
layout(push_constant, std430) uniform Empuje { vec2 tam; float exposicion; float r; float peso[8]; } p;
void main() {
	ivec2 id = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(id, ivec2(p.tam)))) return;
	vec2 uv = (vec2(id) + 0.5) / p.tam;
	vec3 g = vec3(0.0);
	for (int i = 0; i < 7; i++) {
		if (p.peso[i] > 0.0) g += textureLod(niveles[i], uv, 0.0).rgb * p.peso[i];
	}
	// Casi toda la pantalla no tiene halo: sin leer ni escribir el color (es
	// lo caro de esta pasada, el ancho de banda de la imagen entera).
	if (max(g.r, max(g.g, g.b)) < 0.0002) return;
	vec4 c = imageLoad(color, id);
	c.rgb += g / p.exposicion;
	imageStore(color, id, c);
}
)";

class GLPostFX : public CompositorEffect {
    GDCLASS(GLPostFX, CompositorEffect);

    GLBloomAjuste aj;
    uint64_t env_id = 0;   // su exposicion se lee en cada frame
    RenderingDevice* rd = nullptr;
    bool roto = false;
    RID sh[3], pip[3];     // bajar, difuminar, sumar
    RID muestreo;
    RID tex_a[7], tex_b[7];
    Vector2i tam[7];
    Vector2i tam_pantalla;
    int creados = 0;

    // Pasar el GLSL a SPIR-V (glslang) tardaba 90 ms la primera vez: un tiron
    // al aparecer el mundo. Va en un hilo del WorkerThreadPool, que no toca la
    // GPU (Godot compila asi sus propios shaders); hasta que acaba no hay bloom.
    // En el hilo de render solo se crean los shaders desde el binario.
    PackedByteArray binario[3];
    std::atomic<int> estado{0};   // 0 sin empezar, 1 compilando, 2 listo, 3 fallo
    int64_t tarea = -1;
    uint64_t t_hilo = 0;

    void _compilar_hilo() {
        const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
        RenderingDevice* r = RenderingServer::get_singleton()->get_rendering_device();
        const char* fuentes[3] = {GL_POSTFX_BAJAR, GL_POSTFX_DIFUMINAR, GL_POSTFX_SUMAR};
        for (int i = 0; i < 3; i++) {
            Ref<RDShaderSource> fuente;
            fuente.instantiate();
            fuente->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
            fuente->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, String(fuentes[i]));
            Ref<RDShaderSPIRV> spirv = r ? r->shader_compile_spirv_from_source(fuente) : Ref<RDShaderSPIRV>();
            const String error = spirv.is_valid() ? spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE) : String("sin SPIR-V");
            if (!error.is_empty()) {
                UtilityFunctions::push_error("GLPostFX: ", error);
                estado = 3;
                return;
            }
            binario[i] = r->shader_compile_binary_from_spirv(spirv, "GLPostFX");
            if (binario[i].is_empty()) {
                estado = 3;
                return;
            }
        }
        t_hilo = Time::get_singleton()->get_ticks_usec() - t0;
        estado = 2;
    }

    void _empezar_compilacion() {
        int esperado = 0;
        if (!estado.compare_exchange_strong(esperado, 1)) return;
        tarea = WorkerThreadPool::get_singleton()->add_task(callable_mp(this, &GLPostFX::_compilar_hilo), false, "GLPostFX");
    }

    void _esperar_tarea() {
        if (tarea < 0) return;
        WorkerThreadPool::get_singleton()->wait_for_task_completion(tarea);
        tarea = -1;
    }

    bool _preparar() {
        _esperar_tarea();
        rd = RenderingServer::get_singleton()->get_rendering_device();
        if (!rd) return false;
        const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
        for (int i = 0; i < 3; i++) {
            sh[i] = rd->shader_create_from_bytecode(binario[i]);
            if (!sh[i].is_valid()) return false;
            pip[i] = rd->compute_pipeline_create(sh[i]);
            if (!pip[i].is_valid()) return false;
            binario[i] = PackedByteArray();
        }
        Ref<RDSamplerState> s;
        s.instantiate();
        s->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        s->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        muestreo = rd->sampler_create(s);
        if (OS::get_singleton()->get_environment("GL_POSTFX_TRAZA") == "1")
            UtilityFunctions::print("[GLPostFX] glslang en el hilo ", (int64_t)t_hilo / 1000, " ms; shaders creados en ",
                (int64_t)(Time::get_singleton()->get_ticks_usec() - t0) / 1000, " ms");
        return muestreo.is_valid();
    }

    RID _textura(const Vector2i& t) {
        Ref<RDTextureFormat> f;
        f.instantiate();
        f->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
        f->set_width(t.x);
        f->set_height(t.y);
        f->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_STORAGE_BIT);
        Ref<RDTextureView> v;
        v.instantiate();
        return rd->texture_create(f, v);
    }

    void _liberar_niveles() {
        for (int i = 0; i < 7; i++) {
            if (tex_a[i].is_valid()) rd->free_rid(tex_a[i]);
            if (tex_b[i].is_valid()) rd->free_rid(tex_b[i]);
            tex_a[i] = RID();
            tex_b[i] = RID();
        }
        creados = 0;
    }

    void _niveles(const Vector2i& pantalla, int hasta) {
        if (pantalla == tam_pantalla && creados > hasta) return;
        _liberar_niveles();
        Vector2i t = pantalla;
        for (int i = 0; i <= hasta; i++) {
            t = Vector2i(MAX(t.x >> 1, 1), MAX(t.y >> 1, 1));
            tam[i] = t;
            tex_a[i] = _textura(t);
            tex_b[i] = _textura(t);
        }
        tam_pantalla = pantalla;
        creados = hasta + 1;
    }

    Ref<RDUniform> _u_tex(int b, const RID& t) {
        Ref<RDUniform> u;
        u.instantiate();
        u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        u->set_binding(b);
        u->add_id(muestreo);
        u->add_id(t);
        return u;
    }

    static Ref<RDUniform> _u_img(int b, const RID& t) {
        Ref<RDUniform> u;
        u.instantiate();
        u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u->set_binding(b);
        u->add_id(t);
        return u;
    }

    void _pasada(int i, const TypedArray<RDUniform>& unis, const float* empuje, int floats, const Vector2i& t) {
        PackedByteArray bytes;
        bytes.resize(floats * 4);
        memcpy(bytes.ptrw(), empuje, floats * 4);
        const int64_t lista = rd->compute_list_begin();
        rd->compute_list_bind_compute_pipeline(lista, pip[i]);
        rd->compute_list_bind_uniform_set(lista, UniformSetCacheRD::get_cache(sh[i], 0, unis), 0);
        rd->compute_list_set_push_constant(lista, bytes, (uint32_t)bytes.size());
        rd->compute_list_dispatch(lista, (t.x + 7) / 8, (t.y + 7) / 8, 1);
        rd->compute_list_end();
    }

protected:
    static void _bind_methods() {}

public:
    GLPostFX() {
        set_effect_callback_type(EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
        set_access_resolved_color(true);
        set_enabled(false);
    }

    ~GLPostFX() {
        _esperar_tarea();
        RenderingServer* rs = RenderingServer::get_singleton();
        if (!rd || !rs || rs->get_rendering_device() != rd) return;
        _liberar_niveles();
        for (int i = 0; i < 3; i++) {
            if (pip[i].is_valid()) rd->free_rid(pip[i]);
            if (sh[i].is_valid()) rd->free_rid(sh[i]);
        }
        if (muestreo.is_valid()) rd->free_rid(muestreo);
    }

    void gl_poner_bloom(const GLBloomAjuste& a, const Ref<Environment>& env) {
        aj = a;
        env_id = env.is_valid() ? (uint64_t)env->get_instance_id() : 0;
        if (a.on) _empezar_compilacion();
        set_enabled(a.on);
    }

    void _render_callback(int32_t p_tipo, RenderData* p_datos) override {
        if (p_tipo != EFFECT_CALLBACK_TYPE_POST_TRANSPARENT || roto || !p_datos) return;
        const GLBloomAjuste a = aj;
        if (!a.on) return;
        // Como Godot: solo se calculan los niveles hasta el ultimo que pesa.
        int ultimo = -1;
        for (int i = 0; i < 7; i++)
            if (a.niveles[i] > 0.01f) ultimo = i;
        if (ultimo < 0) return;
        if (!rd) {
            const int e = estado.load();
            if (e == 0) _empezar_compilacion();
            if (e < 2) return;
            if (e == 3 || !_preparar()) {
                roto = true;
                return;
            }
        }
        Ref<RenderSceneBuffers> b = p_datos->get_render_scene_buffers();
        RenderSceneBuffersRD* rb = Object::cast_to<RenderSceneBuffersRD>(b.ptr());
        if (!rb) return;
        const Vector2i t = rb->get_internal_size();
        if (t.x < 4 || t.y < 4) return;
        _niveles(t, ultimo);
        float exposicion = 1.0f;
        if (Environment* e = Object::cast_to<Environment>(ObjectDB::get_instance(ObjectID(env_id))))
            exposicion = MAX(e->get_tonemap_exposure(), 0.0001f);
        const float antiluz = 1.0f / MAX(a.tope, 6.0f);
        const RID color = rb->get_color_layer(0);
        gl_marca_gpu(rd, "GL bloom: niveles");
        for (int n = 0; n <= ultimo; n++) {
            const Vector2i tn = tam[n];
            const float x = (float)tn.x, y = (float)tn.y;
            TypedArray<RDUniform> u0;
            u0.push_back(_u_tex(0, n == 0 ? color : tex_a[n - 1]));
            u0.push_back(_u_img(1, tex_a[n]));
            const float e0[4] = {x, y, n == 0 ? antiluz : 0.0f, 0.0f};
            _pasada(0, u0, e0, 4, tn);
            TypedArray<RDUniform> u1;
            u1.push_back(_u_tex(0, tex_a[n]));
            u1.push_back(_u_img(1, tex_b[n]));
            const float e1[12] = {x, y, 1.0f / x, 0.0f, a.fuerza, 0.0f, exposicion, a.minimo, a.umbral, a.escala, a.tope, 0.0f};
            _pasada(1, u1, e1, 12, tn);
            TypedArray<RDUniform> u2;
            u2.push_back(_u_tex(0, tex_b[n]));
            u2.push_back(_u_img(1, tex_a[n]));
            const float e2[12] = {x, y, 0.0f, 1.0f / y, a.fuerza, n == 0 ? antiluz : 0.0f, exposicion, a.minimo,
                                  a.umbral, a.escala, a.tope, n == 0 ? 2.0f : 1.0f};
            _pasada(1, u2, e2, 12, tn);
        }
        TypedArray<RDUniform> u3;
        u3.push_back(_u_img(0, color));
        Ref<RDUniform> lista;
        lista.instantiate();
        lista->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        lista->set_binding(1);
        float e3[12] = {(float)t.x, (float)t.y, exposicion, 0.0f, 0, 0, 0, 0, 0, 0, 0, 0};
        for (int i = 0; i < 7; i++) {
            lista->add_id(muestreo);
            lista->add_id(tex_a[MIN(i, ultimo)]);
            if (i <= ultimo && a.niveles[i] > 0.0001f) e3[4 + i] = a.niveles[i] * a.intensidad;
        }
        u3.push_back(lista);
        gl_marca_gpu(rd, "GL bloom: mezcla");
        _pasada(2, u3, e3, 12, t);
        gl_marca_gpu(rd, "GL bloom: fin");
    }
};

// El posproceso propio solo va en Forward+, fuera del editor y sin GL_GLOW_GODOT=1.
inline bool gl_postfx_disponible() {
    static int v = -1;
    if (v < 0) {
        v = 0;
        RenderingServer* rs = RenderingServer::get_singleton();
        if (rs && rs->get_rendering_device() && !Engine::get_singleton()->is_editor_hint() &&
            OS::get_singleton()->get_environment("GL_GLOW_GODOT") != "1" &&
            rs->has_method("get_current_rendering_method"))
            v = String(rs->call("get_current_rendering_method")) == "forward_plus" ? 1 : 0;
    }
    return v == 1;
}

// El GLPostFX del Compositor de ese WorldEnvironment; lo crea si no hay. Si el
// juego ya tenia un Compositor, se le añade al final.
inline GLPostFX* gl_postfx_de(WorldEnvironment* we) {
    if (!we || !gl_postfx_disponible()) return nullptr;
    Ref<Compositor> comp = we->get_compositor();
    if (comp.is_null()) {
        comp.instantiate();
        we->set_compositor(comp);
    }
    TypedArray<CompositorEffect> efectos = comp->get_compositor_effects();
    for (int i = 0; i < efectos.size(); i++) {
        Ref<CompositorEffect> e = efectos[i];
        if (GLPostFX* fx = Object::cast_to<GLPostFX>(e.ptr())) return fx;
    }
    Ref<GLPostFX> fx;
    fx.instantiate();
    efectos.push_back(fx);
    comp->set_compositor_effects(efectos);
    return fx.ptr();
}

#endif // GL_POSTFX_H

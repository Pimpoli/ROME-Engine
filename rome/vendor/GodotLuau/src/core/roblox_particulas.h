#ifndef ROBLOX_PARTICULAS_H
#define ROBLOX_PARTICULAS_H

// ════════════════════════════════════════════════════════════════════
//  ParticleEmitter de Roblox sobre GPUParticles3D.
//  Antes solo entendia Rate (como CANTIDAD, no por segundo), Lifetime, Color
//  (un color fijo), Speed y Enabled: ni textura, ni tamaño ni transparencia en
//  el tiempo, ni direccion, ni forma. Ahora las propiedades son las de Roblox y
//  llegan igual del importador que de un script (ver gl_secuencias.h).
//
//  Rendimiento: el material de proceso, el de dibujo y el quad se COMPARTEN
//  entre emisores identicos (un brainrot trae cinco y una tanda son decenas).
//  Cambiar una propiedad pide la variante que toca; no muta la compartida.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/gpu_particles3d.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/particle_process_material.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/gradient.hpp>
#include <godot_cpp/classes/gradient_texture1_d.hpp>
#include <godot_cpp/classes/gradient_texture2d.hpp>
#include <godot_cpp/classes/curve.hpp>
#include <godot_cpp/classes/curve_texture.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include "gl_stream.h"
#include "gl_servicios.h"   // gl_en_workspace
#include "gl_secuencias.h"
#include "gl_caras.h"        // gl_normal_de_cara
#include "gl_clone.h"       // GL_META_INTERNO
#include "roblox_gui.h"     // gl_gui_load_image: copia local, cache o descarga

using namespace godot;

// La textura por defecto de Roblox (sparkles_main) no esta en disco: un punto
// blanco difuminado se le parece mucho mas que un cuadrado.
inline Ref<GradientTexture2D>& gl_tex_punto_ref() { static Ref<GradientTexture2D> t; return t; }
inline Ref<Texture2D> gl_tex_punto() {
    Ref<GradientTexture2D>& t = gl_tex_punto_ref();
    if (t.is_null()) {
        Ref<Gradient> g;
        g.instantiate();
        g->set_color(0, Color(1, 1, 1, 1));
        g->set_color(1, Color(1, 1, 1, 0));
        t.instantiate();
        t->set_gradient(g);
        t->set_fill(GradientTexture2D::FILL_RADIAL);
        t->set_fill_from(Vector2(0.5f, 0.5f));
        t->set_fill_to(Vector2(0.5f, 0.0f));
        t->set_width(64);
        t->set_height(64);
    }
    return t;
}

class ParticleEmitter : public GPUParticles3D {
    GDCLASS(ParticleEmitter, GPUParticles3D);

    // ── Propiedades de Roblox, con sus valores por defecto ──
    bool    enabled = true;
    float   rate = 20.0f;
    Variant lifetime_v = Vector2(5, 10);
    Variant speed_v = Vector2(5, 5);
    Vector2 spread;
    Vector3 acceleration;
    float   drag = 0.0f;
    int     direccion = 1;          // Enum.NormalId.Top
    Variant color_v;                // ColorSequence
    Variant transparency_v;         // NumberSequence
    Variant size_v;                 // NumberSequence
    String  texture = "rbxasset://textures/particles/sparkles_main.dds";
    float   light_emission = 0.0f;
    float   light_influence = 0.0f;
    float   brightness = 1.0f;
    Variant rotation_v = Vector2();
    Variant rot_speed_v = Vector2();
    float   z_offset = 0.0f;
    int     orientacion = 0;        // Enum.ParticleOrientation
    bool    locked = false;
    float   vel_herencia = 0.0f;
    int     forma = 0, forma_estilo = 0, forma_inout = 0;
    float   forma_parcial = 1.0f;
    float   time_scale = 1.0f;
    Variant squash_v;
    int     flip_layout = 0, flip_mode = 0;
    Variant flip_fps_v = Vector2(1, 1);
    bool    flip_random = false;
    bool    wind_drag = false;

    bool    rehacer_pendiente = false;
    LocalVector<uint64_t> rafagas;   // GPUParticles3D de un disparo para Emit(n)

    struct GLEmisorRecursos { Ref<ParticleProcessMaterial> pm; Ref<Mesh> quad; };
    static HashMap<String, GLEmisorRecursos>& _cache() {
        static HashMap<String, GLEmisorRecursos> c;
        return c;
    }
    // Si el unico que tiene el recurso es el cache, se puede soltar. Sin esto
    // un juego que anime el color del emisor mete una entrada NUEVA por tono.
    static void _purgar_cache(int tope) {
        HashMap<String, GLEmisorRecursos>& c = _cache();
        if ((int)c.size() <= tope) return;
        LocalVector<String> sobran;
        for (const KeyValue<String, GLEmisorRecursos>& kv : c)
            if (kv.value.pm.is_valid() && kv.value.pm->get_reference_count() <= 1)
                sobran.push_back(kv.key);
        for (uint32_t i = 0; i < sobran.size(); i++) c.erase(sobran[i]);
    }

public:
    // Se vacia al cerrar: si no, los recursos siguen vivos y Godot avisa de
    // fugas al salir (ver los otros caches en gl_chunks / gl_ui_modifiers).
    static void gl_clear_emitter_cache() { _cache().clear(); gl_tex_punto_ref().unref(); }

private:
    // La forma y el tamaño de donde salen: la pieza padre; de un Attachment, un punto.
    Vector3 _tamano_padre() const {
        if (Node* p = get_parent())
            if (p->is_class("RobloxPart")) return (Vector3)p->get("Size");
        return Vector3();
    }

    Ref<Texture2D> _textura() {
        if (texture.is_empty() || texture.begins_with("rbxasset://")) return gl_tex_punto();
        Ref<Texture2D> t = gl_gui_load_image(this, texture);
        // Mientras se descarga, gl_gui_load_image da un blanco liso: aqui queda
        // mejor el punto difuminado que un cuadrado.
        if (t.is_null() || t.ptr() == gl_tex_blanca().ptr()) return gl_tex_punto();
        return t;
    }

    String _clave(const Ref<Texture2D>& tx, const Vector3& tam) const {
        const Variant partes[] = {
            lifetime_v, speed_v, spread, acceleration, drag, direccion, color_v,
            transparency_v, size_v, light_emission, light_influence, brightness,
            rotation_v, rot_speed_v, orientacion, forma, forma_estilo, forma_inout,
            flip_layout, flip_mode, flip_fps_v, flip_random, tam,
        };
        uint32_t h = 2166136261u;
        for (const Variant& p : partes) h = (h ^ p.hash()) * 16777619u;
        return String::num_uint64(h) + "|" + String::num_uint64(tx.is_valid() ? (uint64_t)tx->get_instance_id() : 0);
    }

    void _construir(GLEmisorRecursos& r, const Ref<Texture2D>& tx, const Vector3& tam) {
        const Vector2 vida = gl_rango(lifetime_v, Vector2(5, 10));
        const float L = Math::max(vida.y, 0.01f);
        const Vector2 vel = gl_rango(speed_v, Vector2(5, 5));
        Ref<ParticleProcessMaterial> m;
        m.instantiate();
        // Lifetime es un rango: el nodo lleva el maximo y esto el reparto.
        m->set_lifetime_randomness(Math::clamp(1.0f - Math::max(vida.x, 0.0f) / L, 0.0f, 1.0f));

        // Velocidad: en la cara EmissionDirection (Box) o hacia fuera de la
        // forma (Sphere/Cylinder/Disc), con su abertura.
        const bool radial = forma != 0 && tam != Vector3();
        if (radial) {
            const float signo = forma_inout == 1 ? -1.0f : 1.0f;
            m->set_param_min(ParticleProcessMaterial::PARAM_RADIAL_VELOCITY, vel.x * signo);
            m->set_param_max(ParticleProcessMaterial::PARAM_RADIAL_VELOCITY, vel.y * signo);
        } else {
            m->set_direction(gl_normal_de_cara(direccion));
            m->set_spread(Math::clamp(Math::max(spread.x, spread.y), 0.0f, 180.0f));
            m->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, vel.x);
            m->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, vel.y);
        }
        // Roblox no aplica la gravedad del mundo: solo Acceleration.
        m->set_gravity(acceleration);
        // Drag de Roblox: pierde la MITAD de la velocidad cada 1/Drag s. Godot
        // frena en linea recta: se usa el ritmo inicial de esa caida.
        if (drag > 0.0f) {
            const float d = (vel.x + vel.y) * 0.5f * 0.6931f * drag;
            m->set_param_min(ParticleProcessMaterial::PARAM_DAMPING, d);
            m->set_param_max(ParticleProcessMaterial::PARAM_DAMPING, d);
        }
        const Vector2 rot = gl_rango(rotation_v, Vector2());
        m->set_param_min(ParticleProcessMaterial::PARAM_ANGLE, rot.x);
        m->set_param_max(ParticleProcessMaterial::PARAM_ANGLE, rot.y);
        const Vector2 giro = gl_rango(rot_speed_v, Vector2());
        m->set_param_min(ParticleProcessMaterial::PARAM_ANGULAR_VELOCITY, giro.x);
        m->set_param_max(ParticleProcessMaterial::PARAM_ANGULAR_VELOCITY, giro.y);

        // Size: la curva va normalizada al mayor valor y la escala lo devuelve.
        const GLSecNum tam_s = gl_sec_num(size_v, 1.0f);
        float smax = 0.0f, emax = 0.0f;
        for (uint32_t i = 0; i < tam_s.v.size(); i++) { smax = Math::max(smax, tam_s.v[i]); emax = Math::max(emax, tam_s.e[i]); }
        smax = Math::max(smax, 0.001f);
        Ref<Curve> curva;
        curva.instantiate();
        for (uint32_t i = 0; i < tam_s.t.size(); i++)
            curva->add_point(Vector2(Math::clamp(tam_s.t[i], 0.0f, 1.0f), Math::clamp(tam_s.v[i] / smax, 0.0f, 1.0f)));
        if (curva->get_point_count() == 1) curva->add_point(Vector2(1.0f, curva->get_point_position(0).y));
        Ref<CurveTexture> ct;
        ct.instantiate();
        ct->set_curve(curva);
        m->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, ct);
        const float e_rel = Math::clamp(emax / smax, 0.0f, 1.0f);   // Envelope: variacion por particula
        m->set_param_min(ParticleProcessMaterial::PARAM_SCALE, smax * (1.0f - e_rel));
        m->set_param_max(ParticleProcessMaterial::PARAM_SCALE, smax * (1.0f + e_rel));

        // Color y transparencia en el tiempo, juntos en un degradado.
        const GLSecColor col = gl_sec_color(color_v, Color(1, 1, 1));
        const GLSecNum tr = gl_sec_num(transparency_v, 0.0f);
        LocalVector<float> tiempos;
        for (uint32_t i = 0; i < col.t.size(); i++) tiempos.push_back(col.t[i]);
        for (uint32_t i = 0; i < tr.t.size(); i++) tiempos.push_back(tr.t[i]);
        tiempos.sort();
        PackedFloat32Array offs;
        PackedColorArray colores;
        float previo = -1.0f;
        for (uint32_t i = 0; i < tiempos.size(); i++) {
            const float t = Math::clamp(tiempos[i], 0.0f, 1.0f);
            if (t - previo < 1e-4f) continue;
            previo = t;
            Color c = col.en(t);
            c.r *= brightness; c.g *= brightness; c.b *= brightness;
            c.a = Math::clamp(1.0f - tr.en(t), 0.0f, 1.0f);
            offs.push_back(t);
            colores.push_back(c);
        }
        Ref<Gradient> g;
        g.instantiate();
        g->set_offsets(offs);
        g->set_colors(colores);
        Ref<GradientTexture1D> gt;
        gt.instantiate();
        gt->set_gradient(g);
        m->set_color_ramp(gt);

        // Forma: la de la pieza padre; de un Attachment, un punto.
        if (tam == Vector3()) {
            m->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_POINT);
        } else if (forma == 1) {
            m->set_emission_shape(forma_estilo == 1 ? ParticleProcessMaterial::EMISSION_SHAPE_SPHERE_SURFACE
                                                    : ParticleProcessMaterial::EMISSION_SHAPE_SPHERE);
            m->set_emission_sphere_radius(tam.x * 0.5f);
        } else if (forma == 2 || forma == 3) {
            const float radio = tam.x * 0.5f;
            m->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_RING);
            m->set_emission_ring_axis(Vector3(0, 1, 0));
            m->set_emission_ring_height(forma == 2 ? tam.y : 0.0f);
            m->set_emission_ring_radius(radio);
            m->set_emission_ring_inner_radius(forma_estilo == 1 ? radio : 0.0f);
        } else {
            m->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
            m->set_emission_box_extents(tam * 0.5f);
        }

        // Flipbook: Godot cuenta la animacion en vueltas por vida.
        static const int celdas[] = { 1, 2, 4, 8 };
        const int lado = celdas[CLAMP(flip_layout, 0, 3)];
        if (lado > 1) {
            const float cuadros = (float)(lado * lado);
            const Vector2 fps = gl_rango(flip_fps_v, Vector2(1, 1));
            if (flip_mode == 3) {   // Random: un cuadro al azar y quieto
                m->set_param_min(ParticleProcessMaterial::PARAM_ANIM_OFFSET, 0.0f);
                m->set_param_max(ParticleProcessMaterial::PARAM_ANIM_OFFSET, 1.0f);
            } else {
                const float vueltas_min = fps.x * vida.x / cuadros, vueltas_max = fps.y * L / cuadros;
                m->set_param_min(ParticleProcessMaterial::PARAM_ANIM_SPEED, flip_mode == 1 ? 1.0f : vueltas_min);
                m->set_param_max(ParticleProcessMaterial::PARAM_ANIM_SPEED, flip_mode == 1 ? 1.0f : vueltas_max);
                if (flip_random) m->set_param_max(ParticleProcessMaterial::PARAM_ANIM_OFFSET, 1.0f);
            }
        }
        if (orientacion == 2) m->set_particle_flag(ParticleProcessMaterial::PARTICLE_FLAG_ALIGN_Y_TO_VELOCITY, true);
        r.pm = m;

        // Material de dibujo.
        Ref<StandardMaterial3D> mat;
        mat.instantiate();
        mat->set_shading_mode(light_influence < 0.5f ? BaseMaterial3D::SHADING_MODE_UNSHADED
                                                     : BaseMaterial3D::SHADING_MODE_PER_PIXEL);
        mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        mat->set_blend_mode(light_emission >= 0.5f ? BaseMaterial3D::BLEND_MODE_ADD : BaseMaterial3D::BLEND_MODE_MIX);
        mat->set_billboard_mode(orientacion == 2 ? BaseMaterial3D::BILLBOARD_FIXED_Y : BaseMaterial3D::BILLBOARD_PARTICLES);
        mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);   // el degradado
        mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
        if (tx.is_valid()) mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tx);
        if (lado > 1) {
            mat->set_particles_anim_h_frames(lado);
            mat->set_particles_anim_v_frames(lado);
            mat->set_particles_anim_loop(flip_mode == 0 || flip_mode == 2);
        }
        Ref<QuadMesh> q;
        q.instantiate();
        q->set_size(Vector2(1.0f, 1.0f));   // Size de Roblox = el ancho en studs
        q->set_material(mat);
        r.quad = q;
    }

    // Lo que va al nodo (no a los recursos compartidos): vida, cantidad, caja.
    void _ajustar_nodo(GPUParticles3D* p, const Vector3& tam) {
        const Vector2 vida = gl_rango(lifetime_v, Vector2(5, 10));
        const float L = Math::max(vida.y, 0.01f);
        p->set_lifetime(L);
        p->set_use_local_coordinates(locked);
        p->set_speed_scale(Math::max(time_scale, 0.0f));
        // Godot recorta las particulas fuera de esta caja: con la de fabrica
        // (8 unidades) un chorro rapido desaparecia en cuanto se alejaba.
        const Vector2 vel = gl_rango(speed_v, Vector2(5, 5));
        const float vmax = Math::max(Math::abs(vel.x), Math::abs(vel.y));
        const float r = Math::min(vmax * L + 0.5f * acceleration.length() * L * L
                                  + gl_sec_num(size_v, 1.0f).maximo() + tam.length(), 4000.0f) + 1.0f;
        p->set_visibility_aabb(AABB(Vector3(-r, -r, -r), Vector3(2 * r, 2 * r, 2 * r)));
    }

    void _rehacer() {
        rehacer_pendiente = false;
        const Vector3 tam = _tamano_padre();
        const Ref<Texture2D> tx = _textura();
        const String k = _clave(tx, tam);
        GLEmisorRecursos* hay = _cache().getptr(k);
        if (!hay) {
            GLEmisorRecursos nuevo;
            _construir(nuevo, tx, tam);
            _cache()[k] = nuevo;
            _purgar_cache(512);
            hay = _cache().getptr(k);
        }
        set_process_material(hay->pm);
        set_draw_pass_mesh(0, hay->quad);
        _ajustar_nodo(this, tam);
        // Rate va por amount_ratio: cambiarlo no reinicia el sistema (cambiar
        // amount si, y los scripts lo tocan cada frame). La capacidad solo crece.
        const float L = Math::max(gl_rango(lifetime_v, Vector2(5, 10)).y, 0.01f);
        const int quiero = CLAMP((int)Math::ceil(Math::max(rate, 0.0f) * L), 1, 4096);
        if (quiero > get_amount()) set_amount(quiero);
        set_amount_ratio(Math::clamp((float)quiero / (float)get_amount(), 0.0f, 1.0f));
        for (uint32_t i = 0; i < rafagas.size(); i++)
            if (GPUParticles3D* b = gl_cast<GPUParticles3D>(ObjectDB::get_instance(ObjectID(rafagas[i])))) {
                b->set_process_material(hay->pm);
                b->set_draw_pass_mesh(0, hay->quad);
                _ajustar_nodo(b, tam);
            }
        _aplicar_pausa();
    }
    // Llego la textura pedida por streaming (gl_streaming_texturas.h): hasta
    // entonces se ve el punto difuminado.
    void _gl_textura_lista() { _pedir_rehacer(); }
    void _pedir_rehacer() {
        if (rehacer_pendiente || !is_inside_tree()) return;
        rehacer_pendiente = true;
        call_deferred("_gl_rehacer");
    }

    // Un emisor ENCENDIDO tickea cada frame aunque nadie lo vea. Roblox no
    // simula lo que no se ve, asi que aqui igual: oculto => se pausa.
    void _aplicar_pausa() {
        const bool se_ve = is_inside_tree() && is_visible_in_tree();
        set_emitting(enabled && rate > 0.0f && se_ve);
    }

    GPUParticles3D* _rafaga_libre() {
        GPUParticles3D* primera = nullptr;
        for (uint32_t i = 0; i < rafagas.size(); i++) {
            GPUParticles3D* b = gl_cast<GPUParticles3D>(ObjectDB::get_instance(ObjectID(rafagas[i])));
            if (!b) continue;
            if (!primera) primera = b;
            if (!b->is_emitting()) return b;
        }
        if (rafagas.size() >= 4 && primera) return primera;   // todas ocupadas: se reusa
        GPUParticles3D* b = memnew(GPUParticles3D);
        b->set_meta(GL_META_INTERNO, true);
        b->set_one_shot(true);
        b->set_explosiveness_ratio(1.0f);
        b->set_emitting(false);
        add_child(b, false, Node::INTERNAL_MODE_BACK);
        b->set_process_material(get_process_material());
        b->set_draw_pass_mesh(0, get_draw_pass_mesh(0));
        _ajustar_nodo(b, _tamano_padre());
        rafagas.push_back((uint64_t)b->get_instance_id());
        return b;
    }

protected:
    static void _bind_methods() {
#define GL_PE_PROP(TIPO, NOMBRE, set, get)                                              \
        ClassDB::bind_method(D_METHOD("set_" #NOMBRE, "v"), &ParticleEmitter::set);    \
        ClassDB::bind_method(D_METHOD("get_" #NOMBRE), &ParticleEmitter::get);         \
        ADD_PROPERTY(PropertyInfo(Variant::TIPO, #NOMBRE), "set_" #NOMBRE, "get_" #NOMBRE);
#define GL_PE_VAR(NOMBRE, set, get)                                                     \
        ClassDB::bind_method(D_METHOD("set_" #NOMBRE, "v"), &ParticleEmitter::set);    \
        ClassDB::bind_method(D_METHOD("get_" #NOMBRE), &ParticleEmitter::get);         \
        ADD_PROPERTY(PropertyInfo(Variant::NIL, #NOMBRE, PROPERTY_HINT_NONE, "",       \
                     PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT), "set_" #NOMBRE, "get_" #NOMBRE);
        GL_PE_PROP(BOOL, Enabled, set_enabled_rbx, get_enabled_rbx)
        GL_PE_PROP(FLOAT, Rate, set_rate, get_rate)
        GL_PE_VAR(Lifetime, set_lifetime_rbx, get_lifetime_rbx)
        GL_PE_VAR(Speed, set_speed, get_speed)
        GL_PE_PROP(VECTOR2, SpreadAngle, set_spread, get_spread)
        GL_PE_PROP(VECTOR3, Acceleration, set_acceleration, get_acceleration)
        GL_PE_PROP(FLOAT, Drag, set_drag, get_drag)
        GL_PE_PROP(INT, EmissionDirection, set_direccion, get_direccion)
        GL_PE_VAR(Color, set_color_seq, get_color_seq)
        GL_PE_VAR(Transparency, set_transparency, get_transparency)
        GL_PE_VAR(Size, set_size_seq, get_size_seq)
        GL_PE_PROP(STRING, Texture, set_texture_rbx, get_texture_rbx)
        GL_PE_PROP(FLOAT, LightEmission, set_light_emission, get_light_emission)
        GL_PE_PROP(FLOAT, LightInfluence, set_light_influence, get_light_influence)
        GL_PE_PROP(FLOAT, Brightness, set_brightness, get_brightness)
        GL_PE_VAR(Rotation, set_rotation_rbx, get_rotation_rbx)
        GL_PE_VAR(RotSpeed, set_rot_speed, get_rot_speed)
        GL_PE_PROP(FLOAT, ZOffset, set_z_offset, get_z_offset)
        GL_PE_PROP(INT, Orientation, set_orientacion, get_orientacion)
        GL_PE_PROP(BOOL, LockedToPart, set_locked, get_locked)
        GL_PE_PROP(FLOAT, VelocityInheritance, set_vel_herencia, get_vel_herencia)
        GL_PE_PROP(INT, Shape, set_forma, get_forma)
        GL_PE_PROP(INT, ShapeStyle, set_forma_estilo, get_forma_estilo)
        GL_PE_PROP(INT, ShapeInOut, set_forma_inout, get_forma_inout)
        GL_PE_PROP(FLOAT, ShapePartial, set_forma_parcial, get_forma_parcial)
        GL_PE_PROP(FLOAT, TimeScale, set_time_scale, get_time_scale)
        GL_PE_VAR(Squash, set_squash, get_squash)
        GL_PE_PROP(INT, FlipbookLayout, set_flip_layout, get_flip_layout)
        GL_PE_PROP(INT, FlipbookMode, set_flip_mode, get_flip_mode)
        GL_PE_VAR(FlipbookFramerate, set_flip_fps, get_flip_fps)
        GL_PE_PROP(BOOL, FlipbookStartRandom, set_flip_random, get_flip_random)
        GL_PE_PROP(BOOL, WindAffectsDrag, set_wind_drag, get_wind_drag)
#undef GL_PE_PROP
#undef GL_PE_VAR
        ClassDB::bind_method(D_METHOD("Emit", "count"), &ParticleEmitter::Emit);
        ClassDB::bind_method(D_METHOD("Clear"), &ParticleEmitter::Clear);
        ClassDB::bind_method(D_METHOD("_gl_rehacer"), &ParticleEmitter::_rehacer);
        ClassDB::bind_method(D_METHOD("_gl_textura_lista"), &ParticleEmitter::_gl_textura_lista);
        // El fetcher de assets llama set_image al terminar la descarga.
        ClassDB::bind_method(D_METHOD("set_image", "id"), &ParticleEmitter::set_image);
    }

    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) {
            gl_stream_alta(this);
            // SOLO SE DIBUJA LO QUE CUELGA DE WORKSPACE. Un emisor guardado en
            // ReplicatedStorage es una PLANTILLA que el juego clona; en Roblox no
            // se ve (medido: 8 emisores soltando particulas EN EL ORIGEN).
            set_visible(gl_en_workspace(this));
            if (!Engine::get_singleton()->is_editor_hint()) _rehacer();   // la forma depende del padre
        }
        if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_VISIBILITY_CHANGED) _aplicar_pausa();
    }

public:
    // Cada setter pide rehacer UNA vez por frame (un script suele poner diez
    // propiedades seguidas al crear el emisor).
    void  set_enabled_rbx(bool v)        { enabled = v; _aplicar_pausa(); }
    bool  get_enabled_rbx() const        { return enabled; }
    void  set_rate(float v)              { rate = Math::max(v, 0.0f); _pedir_rehacer(); }
    float get_rate() const               { return rate; }
    void  set_lifetime_rbx(const Variant& v) { lifetime_v = gl_rango(v, Vector2(5, 10)); _pedir_rehacer(); }
    Variant get_lifetime_rbx() const     { return lifetime_v; }
    void  set_speed(const Variant& v)    { speed_v = gl_rango(v, Vector2(5, 5)); _pedir_rehacer(); }
    Variant get_speed() const            { return speed_v; }
    void  set_spread(const Vector2& v)   { spread = v; _pedir_rehacer(); }
    Vector2 get_spread() const           { return spread; }
    void  set_acceleration(const Vector3& v) { acceleration = v; _pedir_rehacer(); }
    Vector3 get_acceleration() const     { return acceleration; }
    void  set_drag(float v)              { drag = Math::max(v, 0.0f); _pedir_rehacer(); }
    float get_drag() const               { return drag; }
    void  set_direccion(int v)           { direccion = CLAMP(v, 0, 5); _pedir_rehacer(); }
    int   get_direccion() const          { return direccion; }
    void  set_color_seq(const Variant& v){ color_v = v; _pedir_rehacer(); }
    Variant get_color_seq() const        { return color_v.get_type() == Variant::NIL ? Variant(gl_sec_color_const(Color(1, 1, 1))) : color_v; }
    void  set_transparency(const Variant& v) { transparency_v = v; _pedir_rehacer(); }
    Variant get_transparency() const     { return transparency_v.get_type() == Variant::NIL ? Variant(gl_sec_num_const(0.0f)) : transparency_v; }
    void  set_size_seq(const Variant& v) { size_v = v; _pedir_rehacer(); }
    Variant get_size_seq() const         { return size_v.get_type() == Variant::NIL ? Variant(gl_sec_num_const(1.0f)) : size_v; }
    void  set_texture_rbx(const String& v) { texture = v; _pedir_rehacer(); }
    String get_texture_rbx() const       { return texture; }
    void  set_light_emission(float v)    { light_emission = v; _pedir_rehacer(); }
    float get_light_emission() const     { return light_emission; }
    void  set_light_influence(float v)   { light_influence = v; _pedir_rehacer(); }
    float get_light_influence() const    { return light_influence; }
    void  set_brightness(float v)        { brightness = Math::max(v, 0.0f); _pedir_rehacer(); }
    float get_brightness() const         { return brightness; }
    void  set_rotation_rbx(const Variant& v) { rotation_v = gl_rango(v, Vector2()); _pedir_rehacer(); }
    Variant get_rotation_rbx() const     { return rotation_v; }
    void  set_rot_speed(const Variant& v){ rot_speed_v = gl_rango(v, Vector2()); _pedir_rehacer(); }
    Variant get_rot_speed() const        { return rot_speed_v; }
    void  set_z_offset(float v)          { z_offset = v; }
    float get_z_offset() const           { return z_offset; }
    void  set_orientacion(int v)         { orientacion = CLAMP(v, 0, 3); _pedir_rehacer(); }
    int   get_orientacion() const        { return orientacion; }
    void  set_locked(bool v)             { locked = v; _pedir_rehacer(); }
    bool  get_locked() const             { return locked; }
    void  set_vel_herencia(float v)      { vel_herencia = v; }
    float get_vel_herencia() const       { return vel_herencia; }
    void  set_forma(int v)               { forma = CLAMP(v, 0, 3); _pedir_rehacer(); }
    int   get_forma() const              { return forma; }
    void  set_forma_estilo(int v)        { forma_estilo = CLAMP(v, 0, 1); _pedir_rehacer(); }
    int   get_forma_estilo() const       { return forma_estilo; }
    void  set_forma_inout(int v)         { forma_inout = CLAMP(v, 0, 2); _pedir_rehacer(); }
    int   get_forma_inout() const        { return forma_inout; }
    void  set_forma_parcial(float v)     { forma_parcial = v; }
    float get_forma_parcial() const      { return forma_parcial; }
    void  set_time_scale(float v)        { time_scale = v; _pedir_rehacer(); }
    float get_time_scale() const         { return time_scale; }
    void  set_squash(const Variant& v)   { squash_v = v; }
    Variant get_squash() const           { return squash_v.get_type() == Variant::NIL ? Variant(gl_sec_num_const(0.0f)) : squash_v; }
    void  set_flip_layout(int v)         { flip_layout = CLAMP(v, 0, 3); _pedir_rehacer(); }
    int   get_flip_layout() const        { return flip_layout; }
    void  set_flip_mode(int v)           { flip_mode = CLAMP(v, 0, 3); _pedir_rehacer(); }
    int   get_flip_mode() const          { return flip_mode; }
    void  set_flip_fps(const Variant& v) { flip_fps_v = gl_rango(v, Vector2(1, 1)); _pedir_rehacer(); }
    Variant get_flip_fps() const         { return flip_fps_v; }
    void  set_flip_random(bool v)        { flip_random = v; _pedir_rehacer(); }
    bool  get_flip_random() const        { return flip_random; }
    void  set_wind_drag(bool v)          { wind_drag = v; }
    bool  get_wind_drag() const          { return wind_drag; }

    void set_image(const String& id) { if (id == texture) _pedir_rehacer(); }

    // ParticleEmitter:Emit(n): una rafaga suelta que NO toca el estado continuo
    // del emisor, con las mismas propiedades (forma, velocidad, color...).
    void Emit(int count) {
        if (!is_inside_tree()) return;
        if (rehacer_pendiente) _rehacer();
        const int cuantas = CLAMP(count <= 0 ? 16 : count, 1, 1000);
        GPUParticles3D* b = _rafaga_libre();
        if (b->get_amount() != cuantas) b->set_amount(cuantas);
        b->restart();
    }
    // ParticleEmitter:Clear(): quita las particulas vivas.
    void Clear() {
        if (!is_inside_tree()) return;
        restart();
        _aplicar_pausa();
        for (uint32_t i = 0; i < rafagas.size(); i++)
            if (GPUParticles3D* b = gl_cast<GPUParticles3D>(ObjectDB::get_instance(ObjectID(rafagas[i])))) {
                b->restart();
                b->set_emitting(false);
            }
    }

    ParticleEmitter() {
        set_meta("_gl_bridge", true);
        set_meta("_gl_efecto", true);
        set_amount(1);
    }
};

#endif // ROBLOX_PARTICULAS_H

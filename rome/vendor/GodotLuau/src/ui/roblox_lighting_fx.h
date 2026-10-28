#ifndef ROBLOX_LIGHTING_FX_H
#define ROBLOX_LIGHTING_FX_H

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include "../core/gl_graphics.h"   // gl_calidad_bloom
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/procedural_sky_material.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/camera_attributes_practical.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>   // BlurEffect: overlay de pantalla
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/image.hpp>          // ColorCorrection: LUT 3D
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/image_texture3d.hpp>
#include <godot_cpp/classes/texture.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include "gl_cielo_caja.h"                      // Sky: skybox de Roblox
#include "gl_tono_roblox.h"                     // curva de tonos + ColorCorrection
#include "gl_postfx.h"                          // BloomEffect: bloom propio en Forward+

using namespace godot;

// Definidas en roblox_gui_imagen.h, que va despues en la misma unidad.
inline Ref<Texture2D> gl_gui_load_image(Node* owner, const String& path);
inline Ref<Texture2D> gl_tex_blanca();
inline Ref<Texture2D> gl_tex_vacia();
// Definida tras ColorCorrectionEffect (mas abajo).
inline void gl_tono_reconstruir(Node* nodo);

// ── Tree-wide helpers ─────────────────────────────────────────────────
inline WorldEnvironment* _lx_find_we(Node* n) {
    if (!n) return nullptr;
    WorldEnvironment* we = gl_cast<WorldEnvironment>(n);
    if (we) return we;
    for (int i = 0; i < n->get_child_count(); i++) {
        WorldEnvironment* f = _lx_find_we(n->get_child(i));
        if (f) return f;
    }
    return nullptr;
}
// El DataModel al que pertenece este nodo: se sube hasta el hijo directo de la
// raiz del SceneTree. Hace falta porque en el test multijugador local conviven
// VARIOS DataModel en el mismo arbol, y cada uno tiene su WorldEnvironment.
inline Node* _lx_mundo_de(Node* n) {
    if (!n || !n->is_inside_tree()) return nullptr;
    Node* raiz = (Node*)n->get_tree()->get_root();
    Node* p = n;
    while (p && p->get_parent() && p->get_parent() != raiz) p = p->get_parent();
    return p ? p : raiz;
}

inline WorldEnvironment* _lx_get_we(Node* self) {
    if (!self || !self->is_inside_tree()) return nullptr;
    // CACHE por ObjectID. Buscar el WorldEnvironment recorria el arbol ENTERO
    // (67.000 nodos en el place de prueba) con un cast_to por nodo — y esto lo
    // llama CADA setter de efecto de Lighting (Blur, Bloom, ColorCorrection,
    // Atmosphere...), que es justo lo que los juegos TWEENEAN a 60 fps.
    // La cache va por DataModel, no en una estatica suelta, porque en el test
    // multijugador local hay varios mundos a la vez y cada uno tiene el suyo.
    Node* mundo = _lx_mundo_de(self);
    if (!mundo) return nullptr;
    static HashMap<uint64_t, uint64_t> cache;   // mundo -> WorldEnvironment
    const uint64_t k = (uint64_t)mundo->get_instance_id();
    WorldEnvironment* we = nullptr;
    if (const uint64_t* hay = cache.getptr(k))
        we = gl_cast<WorldEnvironment>(ObjectDB::get_instance(ObjectID(*hay)));
    // Se revalida: si lo sacaron del arbol o murio, se vuelve a buscar.
    if (!we || !we->is_inside_tree()) {
        we = _lx_find_we(mundo);
        if (!we) we = _lx_find_we((Node*)self->get_tree()->get_root());
        if (we) cache[k] = (uint64_t)we->get_instance_id();
        else    cache.erase(k);
    }
    return we;
}
inline Ref<Environment> _lx_get_env(Node* self) {
    WorldEnvironment* we = _lx_get_we(self);
    if (!we) return {};
    return we->get_environment();
}
inline ProceduralSkyMaterial* _lx_get_sky_mat(Node* self) {
    Ref<Environment> env = _lx_get_env(self);
    if (!env.is_valid()) return nullptr;
    Ref<Sky> sky = env->get_sky();
    if (!sky.is_valid()) return nullptr;
    Ref<Material> mat = sky->get_material();
    return gl_cast<ProceduralSkyMaterial>(mat.ptr());
}
inline WorldEnvironment* _lx_find_we_node(Node* self) {
    if (!self || !self->is_inside_tree()) return nullptr;
    return _lx_find_we((Node*)self->get_tree()->get_root());
}
// Busca el primer nodo de una clase en el arbol (p.ej. el servicio Lighting,
// para devolverle el control de la niebla cuando un Atmosphere se apaga).
inline Node* _lx_find_class(Node* n, const char* cls) {
    if (!n) return nullptr;
    if (n->is_class(cls)) return n;
    for (int i = 0; i < n->get_child_count(); i++)
        if (Node* f = _lx_find_class(n->get_child(i), cls)) return f;
    return nullptr;
}

// ── UN EFECTO DE POST-PROCESO SOLO ACTUA BAJO Lighting ───────────────
// En Roblox, BlurEffect / DepthOfFieldEffect / ColorCorrectionEffect /
// BloomEffect / SunRaysEffect solo hacen algo si su Parent es Lighting. En
// cualquier otro sitio son objetos inertes — plantillas que los scripts meten
// en Lighting cuando toca y sacan al terminar.
//
// Aqui se aplicaban al ENTRAR AL ARBOL, colgaran de donde colgaran. En el place
// de prueba eso significa que el DepthOfField guardado en
//   ReplicatedStorage/Modules/ClimaEventManager/Assets/StormFolder
// —la plantilla de la TORMENTA— tenia la pantalla desenfocada de forma
// PERMANENTE. Se reconoce porque el difuminado depende de la PROFUNDIDAD: lo
// lejano borroso, lo cercano nitido, y cambia segun lo que tengas detras.
// Ademas hay 3 BlurEffect, 3 ColorCorrection y 2 Bloom en el mismo saco.
//
// Con esto, meter el efecto en Lighting lo enciende y sacarlo lo apaga, que es
// justo como lo usan los juegos.
inline bool gl_fx_en_lighting(const Node* n) {
    const Node* p = n ? n->get_parent() : nullptr;
    return p && p->is_class("Lighting");
}

// ════════════════════════════════════════════════════════════════════
//  AtmosphereNode — Roblox Atmosphere equivalent
// ════════════════════════════════════════════════════════════════════
class AtmosphereNode : public Node {
    GDCLASS(AtmosphereNode, Node);

    // Defaults 1:1 con el API dump de Roblox (robloxapi.github.io/ref/class/Atmosphere):
    //   Density 0.395, Offset 0, Color [200,170,108], Decay [92,60,14],
    //   Glare 0, Haze 0.  (Decay es un COLOR en Roblox, no un numero: el tinte de
    //   la atmosfera LEJOS del sol; solo visible con Haze>0 y Glare>0.)
    // https://create.roblox.com/docs/reference/engine/classes/Atmosphere
    float density          = 0.395f;
    float offset           = 0.0f;
    Color color            = Color(0.7843f, 0.6667f, 0.4235f);   // [200,170,108]
    Color decay            = Color(0.3608f, 0.2353f, 0.0549f);   // [92,60,14]
    float glare            = 0.0f;
    float haze             = 0.0f;
    bool  enabled          = true;   // Roblox Atmosphere.Enabled
    bool  _saliendo        = false;  // en EXIT_TREE aun cuelga de Lighting

    void _apply() {
        // Solo actua colgado de Lighting (como los efectos, ver gl_fx_en_lighting).
        // Una plantilla fuera de Lighting no toca la niebla de nadie.
        const bool en_lighting = gl_fx_en_lighting(this);
        if (!en_lighting && !_saliendo) return;
        Ref<Environment> env = _lx_get_env(this);
        if (!env.is_valid()) return;
        env->set_volumetric_fog_enabled(false);   // demasiado caro y no es 1:1
        if (!enabled || _saliendo) {
            env->set_fog_enabled(false);
            env->set_fog_density(0.0f);
            _bruma_al_cielo(Color(0, 0, 0), 0.0f, 0.0f);
            // Sin Atmosphere activo, la niebla clasica FogStart/FogEnd de Lighting
            // vuelve a mandar (en Roblox el Atmosphere la anulaba mientras vivia).
            if (is_inside_tree())
                if (Node* l = _lx_find_class((Node*)get_tree()->get_root(), "Lighting"))
                    l->call_deferred("_gl_reapply");
            return;
        }
        // ── MEDIDO en Play de Studio (2026-10-08) ─────────────────────────
        // Tableros blanco/gris/negro a 25..800 studs con 15 ajustes distintos,
        // invirtiendo la curva de tonos. Las cifras de antes (Density^2.5 x
        // 0.012 + Haze x 0.0008, el Color como niebla y la mitad de ella sobre el
        // cielo) eran a ojo: en Build a Mountain la niebla salia el doble de
        // espesa, blanca y lavando el cielo azul entero.
        //  * La extincion SOLO depende de Density: sigma = 0.102 * D^3.785 por
        //    stud (0.1 -> nada a 800 studs; 0.28 -> 0,00085; 0.5 -> 0,0069; 0.8 ->
        //    0,0455). Haze, Offset, Color, Decay y Glare no la cambian.
        //  * El color de la bruma (mirando lejos del sol) va de Color a Decay segun
        //    Haze (con 1.2 ya es Decay), en lineal gamma 2, y su brillo crece un
        //    poco con Density y con Haze. Con los de Build a Mountain sale
        //    (0.076, 0.157, 0.367), medido; el resto de casos, a menos de un 10 %.
        //  * El cielo solo lo vela Haze (Density y Offset no lo tocan): poco con
        //    1.2 y solo cerca del horizonte, entero con 3. Se pinta en el skybox
        //    (gl_cielo_caja.h), no con la niebla de Godot, que lo vela parejo.
        //  * Offset no cambio nada medible (ni objetos ni cielo sobre el
        //    horizonte); Glare solo cuenta mirando al sol: pendientes.
        const float dens = Math::clamp(density, 0.0f, 1.0f);
        const float sigma = 0.102f * Math::pow(dens, 3.785f);
        const Color c2(color.r * color.r, color.g * color.g, color.b * color.b);
        const Color d2(decay.r * decay.r, decay.g * decay.g, decay.b * decay.b);
        const Color croma = c2.lerp(d2, Math::clamp(0.35f + 0.54f * haze, 0.0f, 1.0f));
        const float mc = Math::max(Math::max(croma.r, croma.g), Math::max(croma.b, 0.0001f));
        const float brillo = 0.367f * (1.0f + 0.6f * (dens - 0.28f)) * (0.85f + 0.125f * Math::min(haze, 1.2f));
        const Color bruma(croma.r / mc * brillo, croma.g / mc * brillo, croma.b / mc * brillo);
        const float bruma_cielo = 0.029f * Math::pow(Math::max(haze, 0.0f) / 1.2f, 3.66f);
        const bool on = sigma > 0.0000001f;
        // Bajo el horizonte, la bruma entera y x1.44 (medido; ver gl_cielo_caja.h).
        _bruma_al_cielo(bruma, bruma_cielo, on ? 1.44f : 0.0f);
        env->set_fog_enabled(on);
        if (!on) { env->set_fog_density(0.0f); return; }
        env->set_fog_mode(Environment::FOG_MODE_EXPONENTIAL);
        env->set_fog_density(sigma);
        const float mb = Math::max(Math::max(bruma.r, bruma.g), Math::max(bruma.b, 0.0001f));
        env->set_fog_light_color(Color(bruma.r / mb, bruma.g / mb, bruma.b / mb).linear_to_srgb());
        env->set_fog_light_energy(mb);
        env->set_fog_sky_affect(0.0f);
        env->set_fog_aerial_perspective(0.0f);
        env->set_fog_sun_scatter(0.0f);
    }

    // El velo del cielo va al Sky de Lighting (si es un skybox, lo pinta el).
    void _bruma_al_cielo(const Color& bruma, float k, float suelo) {
        Node* l = get_parent();
        if (!l) return;
        for (int i = 0; i < l->get_child_count(); i++)
            if (l->get_child(i)->has_method("_gl_bruma")) l->get_child(i)->call("_gl_bruma", bruma, k, suelo);
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_reaplicar"), &AtmosphereNode::_gl_reaplicar);
        ClassDB::bind_method(D_METHOD("set_enabled","v"), &AtmosphereNode::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),     &AtmosphereNode::get_enabled);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"Enabled"),"set_enabled","get_enabled");
        ClassDB::bind_method(D_METHOD("set_density","v"), &AtmosphereNode::set_density);
        ClassDB::bind_method(D_METHOD("get_density"),     &AtmosphereNode::get_density);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Density",PROPERTY_HINT_RANGE,"0,1,0.001"),"set_density","get_density");
        ClassDB::bind_method(D_METHOD("set_offset","v"), &AtmosphereNode::set_offset);
        ClassDB::bind_method(D_METHOD("get_offset"),     &AtmosphereNode::get_offset);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Offset",PROPERTY_HINT_RANGE,"0,1,0.001"),"set_offset","get_offset");
        ClassDB::bind_method(D_METHOD("set_color","c"), &AtmosphereNode::set_color);
        ClassDB::bind_method(D_METHOD("get_color"),     &AtmosphereNode::get_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"Color"),"set_color","get_color");
        // Decay es COLOR (el tinte de la atmosfera lejos del sol), como en Roblox.
        // Antes era un float inventado y el Decay de un place importado se perdia.
        ClassDB::bind_method(D_METHOD("set_decay","c"), &AtmosphereNode::set_decay);
        ClassDB::bind_method(D_METHOD("get_decay"),     &AtmosphereNode::get_decay);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"Decay"),"set_decay","get_decay");
        // Glare/Haze: los ejemplos de la doc usan 0..1 y 2.8; el slider de Studio
        // admite mas (los topes 0..1 de antes recortaban places importados).
        ClassDB::bind_method(D_METHOD("set_glare","v"), &AtmosphereNode::set_glare);
        ClassDB::bind_method(D_METHOD("get_glare"),     &AtmosphereNode::get_glare);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Glare",PROPERTY_HINT_RANGE,"0,10,0.001,or_greater"),"set_glare","get_glare");
        ClassDB::bind_method(D_METHOD("set_haze","v"), &AtmosphereNode::set_haze);
        ClassDB::bind_method(D_METHOD("get_haze"),     &AtmosphereNode::get_haze);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Haze",PROPERTY_HINT_RANGE,"0,10,0.001,or_greater"),"set_haze","get_haze");
    }
    // EXIT_TREE tambien: en Roblox sacar (o destruir) el Atmosphere de Lighting
    // lo APAGA. Antes _apply() no miraba ni el padre ni la salida: al sacarlo
    // la niebla se quedaba puesta, y un Atmosphere guardado como plantilla en
    // ReplicatedStorage la encendia al cargar el juego.
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _apply();
        if (p_what == NOTIFICATION_EXIT_TREE) { _saliendo = true; _apply(); _saliendo = false; }
    }

public:
    // El Sky lo llama al montar su skybox, por si el Atmosphere entro antes.
    void _gl_reaplicar() { if (is_inside_tree()) _apply(); }
    void set_enabled(bool v)          { enabled = v;                                if(is_inside_tree()) _apply(); }
    bool get_enabled() const          { return enabled; }
    void set_density(float v)         { density = Math::clamp(v,0.0f,1.0f);        if(is_inside_tree()) _apply(); }
    float get_density() const         { return density; }
    void set_offset(float v)          { offset  = Math::clamp(v,0.0f,1.0f);        if(is_inside_tree()) _apply(); }
    float get_offset() const          { return offset; }
    void set_color(Color c)           { color = c;                                  if(is_inside_tree()) _apply(); }
    Color get_color() const           { return color; }
    void set_decay(Color c)           { decay = c;                                  if(is_inside_tree()) _apply(); }
    Color get_decay() const           { return decay; }
    void set_glare(float v)           { glare = Math::clamp(v,0.0f,100.0f);         if(is_inside_tree()) _apply(); }
    float get_glare() const           { return glare; }
    void set_haze(float v)            { haze  = Math::clamp(v,0.0f,100.0f);         if(is_inside_tree()) _apply(); }
    float get_haze() const            { return haze; }
};

// ════════════════════════════════════════════════════════════════════
//  LightingSkyNode — 6 sky presets with full day/night cycle shaders
//  All custom shaders react to LIGHT0_DIRECTION.y (sun elevation)
//  so moving the sun (via Lighting.ClockTime) auto-updates the sky.
// ════════════════════════════════════════════════════════════════════
class LightingSkyNode : public Node {
    GDCLASS(LightingSkyNode, Node);

public:
    enum SkyPreset { PRESET_PROCEDURAL=0, PRESET_ATMOSPHERIC, PRESET_CARTOON, PRESET_ANIME, PRESET_NIGHT, PRESET_SUNSET };

private:
    int   sky_preset        = PRESET_PROCEDURAL;
    int   _cur_preset       = -1;
    Ref<ShaderMaterial> _smat;

    // ── Core sky colors ───────────────────────────────────────────
    Color sky_color         = Color(0.06f, 0.14f, 0.55f);
    Color horizon_color     = Color(0.42f, 0.68f, 0.92f);
    Color ground_color      = Color(0.10f, 0.08f, 0.06f);
    Color dawn_color        = Color(0.90f, 0.35f, 0.12f);
    float sky_exposure      = 1.0f;
    float sky_curve         = 0.09f;
    float sun_angular_size  = 21.0f;

    // ── Atmospheric scattering ────────────────────────────────────
    float rayleigh_strength = 1.0f;
    Color rayleigh_color    = Color(0.26f, 0.47f, 1.00f);
    float mie_strength      = 0.5f;
    float mie_eccentricity  = 0.80f;
    Color mie_color         = Color(1.00f, 0.90f, 0.70f);

    // ── Horizon fog ───────────────────────────────────────────────
    Color horizon_fog_color  = Color(0.60f, 0.75f, 0.90f);
    float horizon_fog_amount = 0.30f;
    float horizon_sharpness  = 4.0f;

    // ── Sunset / dusk ─────────────────────────────────────────────
    Color sunset_color      = Color(1.00f, 0.35f, 0.08f);
    float sunset_strength   = 1.0f;
    Color sky_mid_color     = Color(0.50f, 0.20f, 0.60f);

    // ── Sun disk ─────────────────────────────────────────────────
    float sun_disk_scale    = 1.0f;
    float sun_energy        = 1.0f;
    float sun_corona_size   = 1.0f;
    Color sun_disk_color    = Color(1.00f, 0.95f, 0.80f);
    float ground_energy     = 0.5f;

    // ── Stars ─────────────────────────────────────────────────────
    float star_density      = 30.0f;
    float star_brightness   = 0.80f;
    float star_size         = 1.0f;
    float star_twinkle_speed= 1.0f;

    // ── Night-specific ────────────────────────────────────────────
    Color moon_color        = Color(0.95f, 0.95f, 0.85f);
    float moon_size         = 1.0f;
    float moon_energy       = 1.0f;
    Color nebula_color      = Color(0.20f, 0.10f, 0.30f);
    float nebula_strength   = 0.30f;
    float milky_way_strength= 0.50f;

    // ── Clouds ───────────────────────────────────────────────────
    float cloud_amount      = 0.0f;
    Color cloud_color       = Color(1.00f, 0.98f, 0.95f);

    // ── Global ────────────────────────────────────────────────────
    float sky_saturation    = 1.0f;

    // ─────────────────────────────────────────────────────────────
    //  SHADER: ATMOSPHERIC
    //  Full day/night cycle via LIGHT0_DIRECTION.y
    //  Dawn/dusk warm tones, stars at night, FBM clouds, ozone layer
    // ─────────────────────────────────────────────────────────────
    static const char* _shader_atmospheric() { return
R"SHADER(shader_type sky;
render_mode use_half_res_pass;
const float PI = 3.14159265359;
uniform vec3 sky_top_color : source_color = vec3(0.06,0.14,0.55);
uniform vec3 sky_horizon_color : source_color = vec3(0.42,0.68,0.92);
uniform vec3 dawn_color : source_color = vec3(0.90,0.35,0.12);
uniform vec3 ground_color : source_color = vec3(0.08,0.07,0.05);
uniform float rayleigh_strength : hint_range(0.0,5.0) = 1.0;
uniform vec3 rayleigh_color : source_color = vec3(0.26,0.47,1.00);
uniform float mie_strength : hint_range(0.0,5.0) = 0.5;
uniform float mie_eccentricity : hint_range(-0.99,0.99) = 0.80;
uniform vec3 mie_color : source_color = vec3(1.00,0.90,0.70);
uniform vec3 horizon_fog_color : source_color = vec3(0.60,0.75,0.90);
uniform float horizon_fog_amount : hint_range(0.0,1.0) = 0.30;
uniform float horizon_sharpness : hint_range(0.5,20.0) = 4.0;
uniform vec3 sunset_color : source_color = vec3(1.00,0.35,0.08);
uniform float sunset_strength : hint_range(0.0,3.0) = 1.0;
uniform float sun_disk_scale : hint_range(0.0,10.0) = 1.0;
uniform float sun_energy : hint_range(0.0,16.0) = 1.0;
uniform float sun_corona_size : hint_range(0.0,5.0) = 1.0;
uniform vec3 sun_disk_color : source_color = vec3(1.00,0.95,0.80);
uniform float ground_energy : hint_range(0.0,4.0) = 0.5;
uniform float star_density : hint_range(0.0,100.0) = 30.0;
uniform float star_brightness : hint_range(0.0,2.0) = 0.8;
uniform float star_size : hint_range(0.1,5.0) = 1.0;
uniform float star_twinkle_speed : hint_range(0.0,5.0) = 1.0;
uniform float cloud_amount : hint_range(0.0,1.0) = 0.0;
uniform vec3 cloud_color : source_color = vec3(1.0,0.98,0.95);
uniform float sky_exposure : hint_range(0.0,8.0) = 1.0;
uniform float sky_saturation : hint_range(0.0,3.0) = 1.0;

float hash21(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise2(vec2 p){
    vec2 i=floor(p);vec2 f=fract(p);f=f*f*(3.0-2.0*f);
    return mix(mix(hash21(i),hash21(i+vec2(1,0)),f.x),
               mix(hash21(i+vec2(0,1)),hash21(i+vec2(1,1)),f.x),f.y);
}
float fbm(vec2 p){
    return noise2(p)*0.500+noise2(p*2.1)*0.250
          +noise2(p*4.2)*0.125+noise2(p*8.4)*0.0625;
}
float mie_phase(float c,float g){
    float g2=g*g;
    return(1.0-g2)/(4.0*PI*pow(max(1.0+g2-2.0*g*c,0.0001),1.5));
}
float rayleigh_phase(float c){return(3.0/(16.0*PI))*(1.0+c*c);}
float star_field(vec3 d,float t){
    if(d.y<-0.05)return 0.0;
    float u=atan(d.z,d.x)/(2.0*PI)+0.5;
    float v=asin(clamp(d.y,-1.0,1.0))/PI+0.5;
    vec2 uv=vec2(u,v)*(50.0*star_size);
    vec2 id=floor(uv);vec2 fd=fract(uv)-0.5;
    float h=hash21(id);float h2=hash21(id+vec2(3.7,8.1));
    if(h>star_density*0.01)return 0.0;
    float twinkle=0.7+0.3*sin(t*star_twinkle_speed*(1.0+h2*4.0));
    float d2=length(fd);
    return max(0.0,0.003/(d2*d2+0.0002))*twinkle*star_brightness;
}
void sky(){
    vec3 dir=EYEDIR;
    if(dir.y<-0.001){COLOR=ground_color*ground_energy;return;}
    float alt=max(dir.y,0.0);
    vec3 sun_dir=LIGHT0_DIRECTION;
    float cos_t=clamp(dot(dir,sun_dir),-1.0,1.0);
    float sun_elev=sun_dir.y;

    // Time factors driven by sun elevation
    float t_day   =clamp(sun_elev*2.5+0.5,0.0,1.0);
    float t_sunset=pow(max(0.0,1.0-abs(sun_elev)*6.0),1.5)*step(-0.15,sun_elev);
    float t_night =clamp(-sun_elev*4.0,0.0,1.0);

    // Sky gradient colors transition with time of day
    vec3 top  =mix(mix(sky_top_color, vec3(0.06,0.06,0.22),t_sunset*0.6),vec3(0.006,0.012,0.05),t_night);
    vec3 horiz=mix(mix(sky_horizon_color,dawn_color,t_sunset),vec3(0.02,0.03,0.09),t_night);

    float sky_t=pow(1.0-alt,2.5);
    vec3 col=mix(top,horiz,clamp(sky_t,0.0,1.0));

    // Rayleigh + Mie scattering (day only)
    col+=rayleigh_color*rayleigh_phase(cos_t)*rayleigh_strength*t_day*0.4;
    col+=mie_color*mie_phase(cos_t,mie_eccentricity)*mie_strength*t_day*0.2;

    // Horizon haze (warm tint at dusk)
    float horiz_f=pow(max(0.0,1.0-alt*horizon_sharpness),5.0);
    vec3 fog_tint=mix(horizon_fog_color,sunset_color*0.9,t_sunset);
    col=mix(col,fog_tint,horiz_f*horizon_fog_amount*(1.0-t_night*0.7));

    // Sunset glow cone around sun
    float ss_cone=pow(max(0.0,cos_t+0.2),3.0);
    float ss_low =pow(max(0.0,1.0-alt*5.0),3.0);
    col=mix(col,sunset_color,ss_cone*ss_low*t_sunset*sunset_strength*0.6);

    // Ozone layer (subtle blue-green at zenith during day)
    col+=vec3(-0.015,0.025,0.04)*pow(alt,1.5)*t_day*0.2;

    // FBM clouds (lit by time of day)
    if(cloud_amount>0.0&&alt>0.005){
        vec2 cu=vec2(atan(dir.z,dir.x)/6.28318+0.5,alt)*vec2(2.8,2.0);
        float cn=fbm(cu*2.5);
        float cloud=smoothstep(1.0-cloud_amount*0.65,1.0,cn)*clamp(alt*6.0,0.0,1.0);
        vec3 cloud_lit=mix(cloud_color*0.55,cloud_color,t_day);
        cloud_lit=mix(cloud_lit,sunset_color*0.9+cloud_color*0.6,t_sunset*0.55);
        col=mix(col,cloud_lit,cloud*0.88);
    }

    // Stars appear as sun sets
    if(star_density>0.0&&star_brightness>0.0){
        float sv=clamp(0.6-sun_elev*5.0,0.0,1.0);sv*=sv;
        if(sv>0.001)col+=vec3(star_field(dir,TIME))*sv;
    }

    // Sun disk — color shifts orange-red near horizon
    if(LIGHT0_ENABLED){
        float sun_h=1.0-clamp(sun_elev*4.0+0.3,0.0,1.0);
        vec3 act_sun=mix(sun_disk_color,vec3(1.0,0.42,0.06),sun_h*0.85);
        float sa=acos(clamp(cos_t,-1.0,1.0));
        float sr=0.018*max(sun_disk_scale,0.001);
        float cr=sr*(1.0+sun_corona_size*3.0);
        float corona=exp(-sa*sa/max(cr*cr,0.0001));
        col+=act_sun*LIGHT0_COLOR*LIGHT0_ENERGY*corona*sun_energy*0.45;
        if(sa<sr){
            float tt=sa/max(sr,0.0001);
            float limb=mix(0.5,1.0,sqrt(max(0.0,1.0-tt*tt)));
            col+=act_sun*LIGHT0_COLOR*LIGHT0_ENERGY*limb*sun_energy*6.0;
        }
    }

    col*=sky_exposure;
    float luma=dot(col,vec3(0.299,0.587,0.114));
    col=mix(vec3(luma),col,sky_saturation);
    COLOR=max(col,vec3(0.0));
}
)SHADER"; }

    // ─────────────────────────────────────────────────────────────
    //  SHADER: CARTOON  — cel-shaded with day/night + FBM clouds
    // ─────────────────────────────────────────────────────────────
    static const char* _shader_cartoon() { return
R"SHADER(shader_type sky;
const float PI=3.14159265359;
uniform vec3 sky_top_color : source_color = vec3(0.20,0.50,1.00);
uniform vec3 sky_horizon_color : source_color = vec3(0.70,0.85,1.00);
uniform vec3 night_sky_color : source_color = vec3(0.03,0.04,0.16);
uniform vec3 ground_color : source_color = vec3(0.28,0.52,0.18);
uniform vec3 sun_disk_color : source_color = vec3(1.00,0.95,0.70);
uniform vec3 sunset_color : source_color = vec3(1.00,0.50,0.10);
uniform vec3 dawn_color : source_color = vec3(0.90,0.35,0.12);
uniform float sun_disk_scale : hint_range(0.0,10.0) = 2.0;
uniform float sun_energy : hint_range(0.0,8.0) = 1.0;
uniform float sky_exposure : hint_range(0.0,4.0) = 1.0;
uniform vec3 cloud_color : source_color = vec3(1.00,1.00,1.00);
uniform float cloud_amount : hint_range(0.0,1.0) = 0.3;
uniform float star_density : hint_range(0.0,100.0) = 40.0;
uniform float star_brightness : hint_range(0.0,2.0) = 0.8;

float hash21c(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise2c(vec2 p){
    vec2 i=floor(p);vec2 f=fract(p);f=f*f*(3.0-2.0*f);
    return mix(mix(hash21c(i),hash21c(i+vec2(1,0)),f.x),
               mix(hash21c(i+vec2(0,1)),hash21c(i+vec2(1,1)),f.x),f.y);
}
float fbmc(vec2 p){
    return noise2c(p)*0.5+noise2c(p*2.1)*0.25+noise2c(p*4.2)*0.125;
}
void sky(){
    vec3 dir=EYEDIR;
    if(dir.y<0.0){COLOR=ground_color*sky_exposure;return;}
    float sun_elev=LIGHT0_DIRECTION.y;
    float t_day   =clamp(sun_elev*2.5+0.5,0.0,1.0);
    float t_sunset=pow(max(0.0,1.0-abs(sun_elev)*5.0),2.0)*step(-0.2,sun_elev);
    float t_night =clamp(-sun_elev*4.0,0.0,1.0);
    float t=pow(clamp(dir.y,0.0,1.0),0.5);

    // Sky colors
    vec3 sky_top =mix(sky_top_color,night_sky_color,t_night);
    vec3 sky_horiz=mix(sky_horizon_color,mix(sky_horizon_color,dawn_color,t_sunset),1.0-t_day*0.5);
    sky_horiz=mix(sky_horiz,night_sky_color*1.5,t_night);
    vec3 col=mix(sky_horiz,sky_top,t);

    // Cel-shading bands
    float band=floor(t*5.0)/5.0;
    col*=0.93+0.07*smoothstep(band,band+0.04,t);

    // FBM clouds
    if(cloud_amount>0.0&&dir.y>0.01){
        vec2 cu=vec2(atan(dir.z,dir.x)/6.28318+0.5,dir.y)*vec2(3.0,2.0);
        float cn=fbmc(cu*3.0);
        float cloud=smoothstep(1.0-cloud_amount*0.7,1.0,cn)*clamp(dir.y*4.0,0.0,1.0);
        vec3 cloud_lit=mix(cloud_color*0.6,cloud_color,t_day);
        cloud_lit=mix(cloud_lit,sunset_color*0.8+cloud_color*0.5,t_sunset*0.5);
        col=mix(col,cloud_lit,cloud*0.9);
    }

    // Stars at night
    if(t_night>0.05&&star_density>0.0){
        vec2 uv=vec2(atan(dir.z,dir.x)/(2.0*PI)+0.5,
                     asin(clamp(dir.y,-1.0,1.0))/PI+0.5)*50.0;
        vec2 id=floor(uv);vec2 fd=fract(uv)-0.5;
        float h=hash21c(id);
        if(h<=star_density*0.01){
            float d=length(fd);
            col+=vec3(max(0.0,0.004/(d*d+0.0003)))*star_brightness*t_night;
        }
    }

    // Sun disk (color shifts at horizon)
    if(LIGHT0_ENABLED){
        float cos_t=dot(dir,LIGHT0_DIRECTION);
        float sa=acos(clamp(cos_t,-1.0,1.0));
        float sr=0.04*sun_disk_scale;
        if(sa<sr*1.3){
            float h2=smoothstep(sr*1.3,sr*0.7,sa);
            float sun_h=1.0-clamp(sun_elev*3.0+0.3,0.0,1.0);
            vec3 sun_col=mix(sun_disk_color,sunset_color,sun_h*0.8);
            col=mix(col,sun_col*sun_energy*LIGHT0_ENERGY,h2*max(t_day,0.1));
        }
    }
    COLOR=col*sky_exposure;
}
)SHADER"; }

    // ─────────────────────────────────────────────────────────────
    //  SHADER: ANIME  — pastel gradient bands, full day/night cycle
    // ─────────────────────────────────────────────────────────────
    static const char* _shader_anime() { return
R"SHADER(shader_type sky;
const float PI=3.14159265359;
uniform vec3 sky_top_color : source_color = vec3(0.25,0.50,0.90);
uniform vec3 sky_horizon_color : source_color = vec3(0.80,0.85,1.00);
uniform vec3 night_top_color : source_color = vec3(0.02,0.03,0.12);
uniform vec3 ground_color : source_color = vec3(0.40,0.35,0.30);
uniform vec3 dawn_color : source_color = vec3(0.90,0.35,0.12);
uniform vec3 sunset_color : source_color = vec3(1.00,0.55,0.25);
uniform float sunset_strength : hint_range(0.0,2.0) = 1.0;
uniform vec3 sun_disk_color : source_color = vec3(1.00,0.98,0.90);
uniform float sun_disk_scale : hint_range(0.0,10.0) = 1.5;
uniform float sun_energy : hint_range(0.0,8.0) = 1.0;
uniform float sky_exposure : hint_range(0.0,4.0) = 1.0;
uniform float star_density : hint_range(0.0,100.0) = 25.0;
uniform float star_brightness : hint_range(0.0,2.0) = 0.7;
uniform vec3 cloud_color : source_color = vec3(1.00,0.95,0.90);
uniform float cloud_amount : hint_range(0.0,1.0) = 0.2;

float hash21a(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise2a(vec2 p){
    vec2 i=floor(p);vec2 f=fract(p);f=f*f*(3.0-2.0*f);
    return mix(mix(hash21a(i),hash21a(i+vec2(1,0)),f.x),
               mix(hash21a(i+vec2(0,1)),hash21a(i+vec2(1,1)),f.x),f.y);
}
float fbma(vec2 p){
    return noise2a(p)*0.5+noise2a(p*2.1)*0.25+noise2a(p*4.2)*0.125;
}
void sky(){
    vec3 dir=EYEDIR;
    if(dir.y<-0.05){COLOR=ground_color*sky_exposure;return;}
    float sun_elev=LIGHT0_DIRECTION.y;
    float t_day   =clamp(sun_elev*2.5+0.5,0.0,1.0);
    float t_sunset=pow(max(0.0,1.0-abs(sun_elev)*5.0),1.5)*step(-0.15,sun_elev);
    float t_night =clamp(-sun_elev*3.5,0.0,1.0);
    float alt=clamp(dir.y,0.0,1.0);
    float t1=clamp(alt*2.5,0.0,1.0);

    vec3 top  =mix(sky_top_color,night_top_color,t_night);
    vec3 horiz=mix(sky_horizon_color,
                   mix(sky_horizon_color,dawn_color,t_sunset*1.2),1.0-t_day*0.5);
    horiz=mix(horiz,vec3(0.03,0.04,0.10),t_night);
    vec3 col=mix(horiz,top,t1);

    // Anime gradient bands
    col*=0.94+0.06*smoothstep(0.0,0.09,fract(alt*6.0));

    if(LIGHT0_ENABLED){
        float cos_t=dot(dir,LIGHT0_DIRECTION);

        // Sunset warm spread near sun at horizon
        float ns=pow(max(0.0,cos_t),3.0)*pow(max(0.0,1.0-alt*4.0),2.0);
        col=mix(col,sunset_color,ns*t_sunset*sunset_strength*0.7);

        // Stars at night
        float sv=t_night*clamp(alt+0.1,0.0,1.0);
        if(star_density>0.0&&sv>0.01){
            vec2 uv=vec2(atan(dir.z,dir.x)/(2.0*PI)+0.5,
                         asin(clamp(dir.y,-1.0,1.0))/PI+0.5)*40.0;
            vec2 id=floor(uv);vec2 fd=fract(uv)-0.5;
            float h2=hash21a(id);
            if(h2<=star_density*0.01){
                float d=length(fd);
                col+=vec3(max(0.0,0.004/(d*d+0.0003)))*star_brightness*sv;
            }
        }

        // Sun disk (fades at night)
        float sa=acos(clamp(cos_t,-1.0,1.0));
        float sr=0.025*sun_disk_scale;
        if(sa<sr*1.5&&t_day>0.05){
            float h2=smoothstep(sr*1.5,sr*0.6,sa);
            float sun_h=1.0-clamp(sun_elev*3.0+0.3,0.0,1.0);
            vec3 sun_col=mix(sun_disk_color,sunset_color,sun_h*0.75);
            col=mix(col,sun_col*sun_energy*LIGHT0_ENERGY,h2*0.9);
        }
    }

    // Soft clouds
    if(cloud_amount>0.0&&alt>0.02){
        vec2 cu=vec2(atan(dir.z,dir.x)/6.28318+0.5,alt)*vec2(2.5,3.0);
        float cn=fbma(cu*2.5);
        float cloud=smoothstep(1.0-cloud_amount*0.6,1.0,cn)*clamp(alt*5.0,0.0,1.0);
        vec3 cloud_lit=mix(cloud_color*0.6,cloud_color,t_day);
        cloud_lit=mix(cloud_lit,sunset_color*0.9+cloud_color*0.5,t_sunset*0.5);
        col=mix(col,cloud_lit,cloud*0.65);
    }

    COLOR=col*sky_exposure;
}
)SHADER"; }

    // ─────────────────────────────────────────────────────────────
    //  SHADER: NIGHT  — deep starfield, Milky Way, moon with craters
    //  Dawn horizon glow appears when sun rises below horizon
    // ─────────────────────────────────────────────────────────────
    static const char* _shader_night() { return
R"SHADER(shader_type sky;
render_mode use_half_res_pass;
const float PI=3.14159265359;
uniform vec3 sky_top_color : source_color = vec3(0.006,0.012,0.06);
uniform vec3 sky_horizon_color : source_color = vec3(0.025,0.038,0.10);
uniform vec3 ground_color : source_color = vec3(0.012,0.012,0.022);
uniform float star_density : hint_range(0.0,100.0) = 65.0;
uniform float star_brightness : hint_range(0.0,3.0) = 1.2;
uniform float star_size : hint_range(0.1,5.0) = 1.0;
uniform float star_twinkle_speed : hint_range(0.0,5.0) = 1.5;
uniform vec3 moon_color : source_color = vec3(0.95,0.95,0.85);
uniform float moon_size : hint_range(0.0,5.0) = 1.0;
uniform float moon_energy : hint_range(0.0,4.0) = 1.0;
uniform vec3 nebula_color : source_color = vec3(0.20,0.10,0.30);
uniform float nebula_strength : hint_range(0.0,1.0) = 0.30;
uniform float milky_way_strength : hint_range(0.0,2.0) = 0.50;
uniform vec3 dawn_color : source_color = vec3(0.90,0.35,0.12);
uniform float sky_exposure : hint_range(0.0,4.0) = 1.0;

float hash21n(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise2n(vec2 p){
    vec2 i=floor(p);vec2 f=fract(p);f=f*f*(3.0-2.0*f);
    return mix(mix(hash21n(i),hash21n(i+vec2(1,0)),f.x),
               mix(hash21n(i+vec2(0,1)),hash21n(i+vec2(1,1)),f.x),f.y);
}
float stars(vec3 dir,float time){
    if(dir.y<-0.1)return 0.0;
    float u=atan(dir.z,dir.x)/(2.0*PI)+0.5;
    float v=asin(clamp(dir.y,-1.0,1.0))/PI+0.5;
    vec2 uv=vec2(u,v)*(60.0*star_size);
    vec2 id=floor(uv);vec2 fd=fract(uv)-0.5;
    float h=hash21n(id);float h2=hash21n(id+vec2(5.3,9.1));
    if(h>star_density*0.01)return 0.0;
    float twinkle=0.6+0.4*sin(time*star_twinkle_speed*(1.0+h2*5.0));
    float d=length(fd);
    return max(0.0,0.004/(d*d+0.0002))*twinkle*star_brightness;
}
float milky_way_band(vec3 dir){
    float b=atan(dir.z,dir.x)/PI;
    float band=abs(dir.y*2.0-b*0.2);
    float mw=noise2n(vec2(b*4.0,dir.y*8.0))*0.5
             +noise2n(vec2(b*8.0,dir.y*16.0))*0.25
             +noise2n(vec2(b*16.0,dir.y*32.0))*0.125;
    return mw*max(0.0,1.0-band*5.0);
}
void sky(){
    vec3 dir=EYEDIR;
    if(dir.y<-0.05){COLOR=ground_color*sky_exposure;return;}
    float alt=clamp(dir.y,0.0,1.0);
    vec3 col=mix(sky_horizon_color,sky_top_color,pow(alt,0.5));

    col+=vec3(stars(dir,TIME));

    if(milky_way_strength>0.0&&alt>0.0){
        float mw=milky_way_band(dir);
        col+=nebula_color*mw*nebula_strength;
        col+=vec3(mw*0.08*milky_way_strength);
    }

    if(LIGHT0_ENABLED){
        // Moon appears at position opposite to sun direction
        vec3 moon_dir=-LIGHT0_DIRECTION;
        float cos_m=dot(dir,moon_dir);
        float ma=acos(clamp(cos_m,-1.0,1.0));
        float mr=0.015*moon_size;
        if(ma<mr*1.5){
            float mh=smoothstep(mr*1.5,mr*0.7,ma);
            float tt=ma/max(mr,0.0001);
            float limb=mix(0.55,1.0,sqrt(max(0.0,1.0-tt*tt)));
            // Subtle crater texture using noise
            float crater=noise2n(dir.xz*80.0+vec2(1.3,2.7))*0.12+0.88;
            col=mix(col,moon_color*limb*moon_energy*crater,mh);
        }
        // Moon glow halo
        float mg=exp(-ma*ma/0.04);
        col+=moon_color*mg*moon_energy*0.05;

        // Dawn/dusk horizon glow when sun is near horizon below us
        float sun_elev=LIGHT0_DIRECTION.y;
        float dawn_factor=clamp(sun_elev*12.0+0.9,0.0,1.0)*pow(max(0.0,1.0-alt*10.0),2.5);
        if(dawn_factor>0.001){
            float cos_dawn=dot(dir,vec3(LIGHT0_DIRECTION.x,0.0,LIGHT0_DIRECTION.z));
            col=mix(col,dawn_color,dawn_factor*pow(max(0.0,cos_dawn),2.0)*0.7);
        }
    }

    col*=sky_exposure;
    COLOR=max(col,vec3(0.0));
}
)SHADER"; }

    // ─────────────────────────────────────────────────────────────
    //  SHADER: SUNSET  — dramatic tricolor sky, full day/night cycle
    //  Starts warm at sunset, transitions to night with moon
    // ─────────────────────────────────────────────────────────────
    static const char* _shader_sunset() { return
R"SHADER(shader_type sky;
render_mode use_half_res_pass;
const float PI=3.14159265359;
uniform vec3 zenith_color : source_color = vec3(0.05,0.10,0.35);
uniform vec3 sky_mid_color : source_color = vec3(0.50,0.20,0.60);
uniform vec3 sky_horizon_color : source_color = vec3(1.00,0.45,0.10);
uniform vec3 sun_glow_color : source_color = vec3(1.00,0.80,0.20);
uniform vec3 night_top_color : source_color = vec3(0.008,0.012,0.05);
uniform vec3 dawn_color : source_color = vec3(0.90,0.35,0.12);
uniform vec3 ground_color : source_color = vec3(0.08,0.05,0.04);
uniform float sun_disk_scale : hint_range(0.0,10.0) = 1.5;
uniform float sun_energy : hint_range(0.0,16.0) = 1.5;
uniform vec3 sun_disk_color : source_color = vec3(1.00,0.85,0.50);
uniform float sun_corona_size : hint_range(0.0,5.0) = 2.0;
uniform vec3 horizon_fog_color : source_color = vec3(1.00,0.60,0.30);
uniform float horizon_fog_amount : hint_range(0.0,1.0) = 0.6;
uniform float sky_exposure : hint_range(0.0,8.0) = 1.0;
uniform float star_density : hint_range(0.0,100.0) = 25.0;
uniform float star_brightness : hint_range(0.0,2.0) = 0.6;
uniform float cloud_amount : hint_range(0.0,1.0) = 0.15;
uniform vec3 cloud_color : source_color = vec3(1.00,0.70,0.50);

float hash21s(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise2s(vec2 p){
    vec2 i=floor(p);vec2 f=fract(p);f=f*f*(3.0-2.0*f);
    return mix(mix(hash21s(i),hash21s(i+vec2(1,0)),f.x),
               mix(hash21s(i+vec2(0,1)),hash21s(i+vec2(1,1)),f.x),f.y);
}
float fbms(vec2 p){
    return noise2s(p)*0.5+noise2s(p*2.1)*0.25+noise2s(p*4.2)*0.125;
}
void sky(){
    vec3 dir=EYEDIR;
    if(dir.y<-0.02){COLOR=ground_color*sky_exposure;return;}
    float sun_elev=LIGHT0_DIRECTION.y;
    float t_day   =clamp(sun_elev*2.5+0.5,0.0,1.0);
    float t_sunset=pow(max(0.0,1.0-abs(sun_elev)*5.5),1.5)*step(-0.15,sun_elev);
    float t_night =clamp(-sun_elev*4.0,0.0,1.0);
    float alt=clamp(dir.y,0.0,1.0);

    // Tricolor sunset gradient
    float t1=clamp(alt*3.0,0.0,1.0);
    float t2=clamp((alt-0.33)*3.0,0.0,1.0);
    vec3 horizon=mix(mix(sky_horizon_color,dawn_color,t_sunset*0.5),vec3(0.03,0.04,0.10),t_night);
    vec3 mid    =mix(sky_mid_color,vec3(0.04,0.04,0.15),t_night);
    vec3 top    =mix(zenith_color,night_top_color,t_night);
    vec3 col=mix(horizon,mid,t1);
    col=mix(col,top,t2);

    // Horizon fog
    float horiz=pow(max(0.0,1.0-alt*8.0),4.0);
    vec3 fog_h=mix(horizon_fog_color,vec3(0.03,0.04,0.10),t_night);
    col=mix(col,fog_h,horiz*horizon_fog_amount);

    if(LIGHT0_ENABLED){
        float cos_t=dot(dir,LIGHT0_DIRECTION);
        // Sun glow spread
        float glow=pow(max(0.0,cos_t),5.0)*clamp(1.0-alt*3.0,0.0,1.0)*(1.0-t_night);
        col=mix(col,sun_glow_color,glow*0.6);

        // Sun disk with horizon color shift
        float sa=acos(clamp(cos_t,-1.0,1.0));
        float sr=0.02*sun_disk_scale;
        float cr=sr*(1.0+sun_corona_size*2.0);
        float corona=exp(-sa*sa/max(cr*cr,0.0001));
        float sun_h=1.0-clamp(sun_elev*4.0+0.3,0.0,1.0);
        vec3 act_sun=mix(sun_disk_color,vec3(1.0,0.42,0.08),sun_h*0.8);
        col+=act_sun*LIGHT0_ENERGY*corona*sun_energy*0.6*(1.0-t_night);
        if(sa<sr){
            float tt=sa/max(sr,0.0001);
            float limb=mix(0.4,1.0,sqrt(max(0.0,1.0-tt*tt)));
            col+=act_sun*LIGHT0_ENERGY*limb*sun_energy*4.0*(1.0-t_night);
        }

        // Moon at night
        if(t_night>0.1){
            vec3 moon_dir=-LIGHT0_DIRECTION;
            float cos_m=dot(dir,moon_dir);
            float ma=acos(clamp(cos_m,-1.0,1.0));
            float mr=0.018;
            if(ma<mr*1.4){
                float mh=smoothstep(mr*1.4,mr*0.8,ma);
                col=mix(col,vec3(0.95,0.95,0.85)*0.9,mh*t_night);
            }
            float mg=exp(-ma*ma/0.05);
            col+=vec3(0.95,0.95,0.85)*mg*0.04*t_night;
        }
    }

    // Stars
    float sv=clamp(0.6-sun_elev*5.0,0.0,1.0);sv*=sv;
    if(star_density>0.0&&sv>0.001){
        vec2 uv=vec2(atan(dir.z,dir.x)/(2.0*PI)+0.5,
                     asin(clamp(dir.y,-1.0,1.0))/PI+0.5)*50.0;
        vec2 id=floor(uv);vec2 fd=fract(uv)-0.5;
        float h=hash21s(id);
        if(h<=star_density*0.01){
            float d=length(fd);
            col+=vec3(max(0.0,0.003/(d*d+0.0002)))*star_brightness*sv;
        }
    }

    // Warm sunset clouds
    if(cloud_amount>0.0&&alt>0.01){
        vec2 cu=vec2(atan(dir.z,dir.x)/6.28318+0.5,alt)*vec2(2.8,2.0);
        float cn=fbms(cu*2.5);
        float cloud=smoothstep(1.0-cloud_amount*0.65,1.0,cn)*clamp(alt*6.0,0.0,1.0);
        col=mix(col,cloud_color,cloud*0.85*(1.0-t_night*0.8));
    }

    col*=sky_exposure;
    COLOR=max(col,vec3(0.0));
}
)SHADER"; }

    static const char* _shader_for_preset(int p) {
        switch (p) {
            case PRESET_ATMOSPHERIC: return _shader_atmospheric();
            case PRESET_CARTOON:     return _shader_cartoon();
            case PRESET_ANIME:       return _shader_anime();
            case PRESET_NIGHT:       return _shader_night();
            case PRESET_SUNSET:      return _shader_sunset();
            default: return _shader_atmospheric();
        }
    }

    void _apply_procedural() {
        Ref<Environment> env = _lx_get_env(this);
        if (!env.is_valid()) return;
        Ref<Sky> sky_obj = env->get_sky();
        if (!sky_obj.is_valid()) return;
        Ref<Material> mat = sky_obj->get_material();
        Ref<ProceduralSkyMaterial> pmat = mat;
        if (!pmat.is_valid()) {
            pmat.instantiate();
            sky_obj->set_material(pmat);
        }
        pmat->set_sky_top_color(sky_color);
        pmat->set_sky_horizon_color(horizon_color);
        pmat->set_sky_curve(sky_curve);
        pmat->set_ground_bottom_color(ground_color);
        pmat->set_ground_horizon_color(horizon_color.lightened(0.15f));
        pmat->set_sun_angle_max(sun_angular_size);
        pmat->set_sky_energy_multiplier(sky_exposure);
        pmat->set_energy_multiplier(sky_exposure);
    }

    void _update_shader_uniforms() {
        if (!_smat.is_valid()) return;
        _smat->set_shader_parameter("sky_top_color",      sky_color);
        _smat->set_shader_parameter("sky_horizon_color",  horizon_color);
        _smat->set_shader_parameter("sky_mid_color",      sky_mid_color);
        _smat->set_shader_parameter("zenith_color",       sky_color);
        _smat->set_shader_parameter("dawn_color",         dawn_color);
        _smat->set_shader_parameter("ground_color",       ground_color);
        _smat->set_shader_parameter("rayleigh_strength",  rayleigh_strength);
        _smat->set_shader_parameter("rayleigh_color",     rayleigh_color);
        _smat->set_shader_parameter("mie_strength",       mie_strength);
        _smat->set_shader_parameter("mie_eccentricity",   mie_eccentricity);
        _smat->set_shader_parameter("mie_color",          mie_color);
        _smat->set_shader_parameter("horizon_fog_color",  horizon_fog_color);
        _smat->set_shader_parameter("horizon_fog_amount", horizon_fog_amount);
        _smat->set_shader_parameter("horizon_sharpness",  horizon_sharpness);
        _smat->set_shader_parameter("sunset_color",       sunset_color);
        _smat->set_shader_parameter("sunset_strength",    sunset_strength);
        _smat->set_shader_parameter("sun_glow_color",     sun_disk_color);
        _smat->set_shader_parameter("sun_disk_scale",     sun_disk_scale);
        _smat->set_shader_parameter("sun_energy",         sun_energy);
        _smat->set_shader_parameter("sun_corona_size",    sun_corona_size);
        _smat->set_shader_parameter("sun_disk_color",     sun_disk_color);
        _smat->set_shader_parameter("ground_energy",      ground_energy);
        _smat->set_shader_parameter("star_density",       star_density);
        _smat->set_shader_parameter("star_brightness",    star_brightness);
        _smat->set_shader_parameter("star_size",          star_size);
        _smat->set_shader_parameter("star_twinkle_speed", star_twinkle_speed);
        _smat->set_shader_parameter("cloud_amount",       cloud_amount);
        _smat->set_shader_parameter("cloud_color",        cloud_color);
        _smat->set_shader_parameter("sky_exposure",       sky_exposure);
        _smat->set_shader_parameter("sky_saturation",     sky_saturation);
        _smat->set_shader_parameter("moon_color",         moon_color);
        _smat->set_shader_parameter("moon_size",          moon_size);
        _smat->set_shader_parameter("moon_energy",        moon_energy);
        _smat->set_shader_parameter("nebula_color",       nebula_color);
        _smat->set_shader_parameter("nebula_strength",    nebula_strength);
        _smat->set_shader_parameter("milky_way_strength", milky_way_strength);
        _smat->set_shader_parameter("night_sky_color",    Color(0.03f, 0.04f, 0.16f));
        _smat->set_shader_parameter("night_top_color",    Color(0.02f, 0.03f, 0.12f));
    }

    // ── Skybox de Roblox (ver gl_cielo_caja.h) ────────────────────────
    Ref<ShaderMaterial> _caja;
    bool _caja_activa = false;
    HashMap<String, Ref<Texture2D>> _caja_texs;   // id -> textura con mipmaps
    Color _bruma_col;          // velo del Atmosphere (lo manda AtmosphereNode)
    float _bruma_k = 0.0f;
    float _bruma_suelo = 0.0f; // bajo el horizonte: bruma entera (0 = sin Atmosphere)

    Variant _prop_roblox(const char* k, const Variant& def) const {
        const StringName n(k);
        return has_meta(n) ? get_meta(n) : def;
    }
    // Textura de un id con mipmaps; vacia mientras se baja (el descargador
    // llama a set_image al terminar y se reintenta) o si no se puede bajar.
    Ref<Texture2D> _caja_tex(const String& id) {
        if (id.is_empty()) return Ref<Texture2D>();
        if (Ref<Texture2D>* t = _caja_texs.getptr(id)) return *t;
        // Por streaming (gl_streaming_texturas.h): las caras se leen en hilos
        // con sus mipmaps (eran ~70 ms del arranque) y, hasta que estan las
        // seis, se queda el cielo procedural. Al llegar, _gl_textura_lista.
        // Tambien del cache de descargas (el cielo de Build a Mountain vive
        // ahi): si no, pasaba por la UI y los mipmaps salian de leer la textura
        // de vuelta de la GPU.
        String cruda = gl_is_roblox_asset(id) ? gl_local_asset_file(id) : String();
        if (cruda.is_empty() && gl_is_roblox_asset(id)) cruda = gl_cache_asset_path(id);
        if (!cruda.is_empty() && gl_tex_streaming_activo() &&
            !(gl_tex_motor_streaming() && !gl_local_asset_path(id).is_empty())) {
            const Ref<Texture2D> t = gl_tex_obtener_mipmaps(cruda, this);
            if (t.is_valid()) _caja_texs.insert(id, t);
            return t;
        }
        // La imagen del archivo con sus mipmaps, sin pasar por una textura:
        // sacar la imagen de una textura es leerla DE LA GPU, esperandola
        // (gl_cielo_con_mipmaps). Asi queda para lo que no tiene archivo.
        Ref<Texture2D> t = gl_cielo_cara(id);
        if (t.is_null()) {
            t = gl_gui_load_image(this, id);
            if (!t.is_valid() || t == gl_tex_blanca()) return Ref<Texture2D>();
            t = gl_cielo_con_mipmaps(t);
        }
        _caja_texs.insert(id, t);
        return t;
    }
    // true si el Sky trae sus seis caras y ya estan todas: entonces manda el
    // skybox. Mientras falte alguna se queda el cielo procedural (medio cubo
    // en blanco seria peor).
    bool _aplicar_caja() {
        static const char* caras[6] = { "SkyboxRt", "SkyboxLf", "SkyboxUp", "SkyboxDn", "SkyboxBk", "SkyboxFt" };
        static const char* unis[6]  = { "cara_rt", "cara_lf", "cara_up", "cara_dn", "cara_bk", "cara_ft" };
        const bool estaba = _caja_activa;
        _caja_activa = false;
        Ref<Texture2D> tex[6];
        bool alguna = false;
        for (int i = 0; i < 6; i++) {
            const String id = _prop_roblox(caras[i], String());
            alguna = alguna || !id.is_empty();
            tex[i] = _caja_tex(id);
        }
        if (!alguna) return false;
        for (int i = 0; i < 6; i++) if (!tex[i].is_valid()) return false;
        Ref<Environment> env = _lx_get_env(this);
        if (!env.is_valid()) return false;
        Ref<Sky> sky_obj = env->get_sky();
        if (!sky_obj.is_valid()) return false;
        if (!_caja.is_valid()) {
            Ref<Shader> sh;
            sh.instantiate();
            sh->set_code(String(gl_cielo_caja_codigo()));
            _caja.instantiate();
            _caja->set_shader(sh);
        }
        for (int i = 0; i < 6; i++) _caja->set_shader_parameter(unis[i], tex[i]);
        // Sol y luna: la textura del juego; las de Roblox (rbxasset://) no.
        const String sol_id = _prop_roblox("SunTextureId", String());
        const String luna_id = _prop_roblox("MoonTextureId", String());
        const Ref<Texture2D> st = sol_id.begins_with("rbxasset://") ? Ref<Texture2D>() : _caja_tex(sol_id);
        const Ref<Texture2D> lt = luna_id.begins_with("rbxasset://") ? Ref<Texture2D>() : _caja_tex(luna_id);
        _caja->set_shader_parameter("sol_tex", st);
        _caja->set_shader_parameter("sol_con_tex", st.is_valid());
        _caja->set_shader_parameter("luna_tex", lt);
        _caja->set_shader_parameter("luna_con_tex", lt.is_valid());
        const float luna_grados = (float)_prop_roblox("MoonAngularSize", 11.0f);
        _caja->set_shader_parameter("sol_tam", Math::tan(Math::deg_to_rad(sun_angular_size * 0.5f)));
        _caja->set_shader_parameter("luna_tam", Math::tan(Math::deg_to_rad(luna_grados * 0.5f)));
        _caja->set_shader_parameter("astros", (bool)_prop_roblox("CelestialBodiesShown", true));
        _caja->set_shader_parameter("estrellas", Math::clamp((float)_prop_roblox("StarCount", 3000) / 5000.0f, 0.0f, 1.0f));
        const Vector3 g = _prop_roblox("SkyboxOrientation", Vector3());
        const Basis giro = Basis::from_euler(Vector3(Math::deg_to_rad(g.x), Math::deg_to_rad(g.y), Math::deg_to_rad(g.z)),
                                             EULER_ORDER_YXZ);
        _caja->set_shader_parameter("giro", giro.inverse());
        _caja->set_shader_parameter("bruma_col", Vector3(_bruma_col.r, _bruma_col.g, _bruma_col.b));
        _caja->set_shader_parameter("bruma_k", _bruma_k);
        _caja->set_shader_parameter("bruma_suelo", _bruma_suelo);
        sky_obj->set_material(_caja);
        _smat = Ref<ShaderMaterial>();
        _cur_preset = -1;
        const bool primera = !estaba;
        _caja_activa = true;
        _gl_astros();
        // El velo del Atmosphere, por si este entro antes que el skybox.
        if (primera && get_parent())
            for (int i = 0; i < get_parent()->get_child_count(); i++)
                if (get_parent()->get_child(i)->has_method("_gl_reaplicar"))
                    get_parent()->get_child(i)->call_deferred("_gl_reaplicar");
        return true;
    }

public:
    // Lighting lo llama al cambiar la hora: el sol y la luna van donde los pone
    // Lighting:GetSunDirection() (ver Lighting::get_sun_direction).
    void _gl_astros() {
        if (!_caja_activa || !_caja.is_valid()) return;
        Node* l = get_parent();
        if (!l || !l->has_method("get_sun_direction")) return;
        _caja->set_shader_parameter("sol_dir", (Vector3)l->call("get_sun_direction"));
        _caja->set_shader_parameter("luna_dir", (Vector3)l->call("get_moon_direction"));
    }
    // El Atmosphere de Lighting manda aqui su velo del cielo.
    void _gl_bruma(const Color& col, float k, float suelo) {
        _bruma_col = col;
        _bruma_k = k;
        _bruma_suelo = suelo;
        if (_caja.is_valid()) {
            _caja->set_shader_parameter("bruma_col", Vector3(col.r, col.g, col.b));
            _caja->set_shader_parameter("bruma_k", k);
            _caja->set_shader_parameter("bruma_suelo", suelo);
        }
    }
    // El descargador de assets la llama cuando llega una cara; y el puente de
    // Luau cuando un script cambia una cara o el sol.
    void set_image(const String&) { if (is_inside_tree()) _apply(); }
    void _gl_cielo_cambio() { if (is_inside_tree()) _apply(); }
    void _gl_textura_lista() { if (is_inside_tree()) _apply(); }   // llego una cara (streaming)
private:

    void _apply() {
        if (!is_inside_tree()) return;
        if (_aplicar_caja()) return;
        if (sky_preset == PRESET_PROCEDURAL) {
            _cur_preset = PRESET_PROCEDURAL;
            _smat = Ref<ShaderMaterial>();
            _apply_procedural();
            return;
        }
        bool needs_rebuild = (sky_preset != _cur_preset || !_smat.is_valid());
        if (needs_rebuild) {
            _cur_preset = sky_preset;
            Ref<Shader> sh;
            sh.instantiate();
            sh->set_code(String(_shader_for_preset(sky_preset)));
            _smat.instantiate();
            _smat->set_shader(sh);
        }
        Ref<Environment> env = _lx_get_env(this);
        if (env.is_valid()) {
            Ref<Sky> sky_obj = env->get_sky();
            if (sky_obj.is_valid()) sky_obj->set_material(_smat);
        }
        _update_shader_uniforms();
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_image","id"),    &LightingSkyNode::set_image);
        ClassDB::bind_method(D_METHOD("_gl_textura_lista"), &LightingSkyNode::_gl_textura_lista);
        ClassDB::bind_method(D_METHOD("_gl_astros"),        &LightingSkyNode::_gl_astros);
        ClassDB::bind_method(D_METHOD("_gl_bruma","c","k","s"), &LightingSkyNode::_gl_bruma);
        ClassDB::bind_method(D_METHOD("_gl_cielo_cambio"),  &LightingSkyNode::_gl_cielo_cambio);
        ClassDB::bind_method(D_METHOD("set_sky_preset","v"), &LightingSkyNode::set_sky_preset);
        ClassDB::bind_method(D_METHOD("get_sky_preset"),     &LightingSkyNode::get_sky_preset);
        ADD_PROPERTY(PropertyInfo(Variant::INT,"SkyPreset",PROPERTY_HINT_ENUM,
            "Procedural,Atmospheric,Cartoon,Anime,Night,Sunset"),"set_sky_preset","get_sky_preset");

        ClassDB::bind_method(D_METHOD("set_sky_color","c"),    &LightingSkyNode::set_sky_color);
        ClassDB::bind_method(D_METHOD("get_sky_color"),        &LightingSkyNode::get_sky_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"SkyColor"),"set_sky_color","get_sky_color");
        ClassDB::bind_method(D_METHOD("set_horizon_color","c"),&LightingSkyNode::set_horizon_color);
        ClassDB::bind_method(D_METHOD("get_horizon_color"),    &LightingSkyNode::get_horizon_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"HorizonColor"),"set_horizon_color","get_horizon_color");
        ClassDB::bind_method(D_METHOD("set_dawn_color","c"),   &LightingSkyNode::set_dawn_color);
        ClassDB::bind_method(D_METHOD("get_dawn_color"),       &LightingSkyNode::get_dawn_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"DawnColor"),"set_dawn_color","get_dawn_color");
        ClassDB::bind_method(D_METHOD("set_sky_mid_color","c"),&LightingSkyNode::set_sky_mid_color);
        ClassDB::bind_method(D_METHOD("get_sky_mid_color"),    &LightingSkyNode::get_sky_mid_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"SkyMidColor"),"set_sky_mid_color","get_sky_mid_color");
        ClassDB::bind_method(D_METHOD("set_ground_color","c"), &LightingSkyNode::set_ground_color);
        ClassDB::bind_method(D_METHOD("get_ground_color"),     &LightingSkyNode::get_ground_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"GroundColor"),"set_ground_color","get_ground_color");

        ClassDB::bind_method(D_METHOD("set_sky_exposure","v"),&LightingSkyNode::set_sky_exposure);
        ClassDB::bind_method(D_METHOD("get_sky_exposure"),    &LightingSkyNode::get_sky_exposure);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"SkyExposure",PROPERTY_HINT_RANGE,"0,8,0.01"),"set_sky_exposure","get_sky_exposure");
        ClassDB::bind_method(D_METHOD("set_sky_saturation","v"),&LightingSkyNode::set_sky_saturation);
        ClassDB::bind_method(D_METHOD("get_sky_saturation"),    &LightingSkyNode::get_sky_saturation);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"SkySaturation",PROPERTY_HINT_RANGE,"0,3,0.01"),"set_sky_saturation","get_sky_saturation");
        ClassDB::bind_method(D_METHOD("set_sky_curve","v"),&LightingSkyNode::set_sky_curve);
        ClassDB::bind_method(D_METHOD("get_sky_curve"),    &LightingSkyNode::get_sky_curve);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"SkyCurve",PROPERTY_HINT_RANGE,"0,1,0.001"),"set_sky_curve","get_sky_curve");
        ClassDB::bind_method(D_METHOD("set_sun_angular_size","v"),&LightingSkyNode::set_sun_angular_size);
        ClassDB::bind_method(D_METHOD("get_sun_angular_size"),    &LightingSkyNode::get_sun_angular_size);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"SunAngularSize",PROPERTY_HINT_RANGE,"0,90,0.1"),"set_sun_angular_size","get_sun_angular_size");

        ClassDB::bind_method(D_METHOD("set_rayleigh_strength","v"),&LightingSkyNode::set_rayleigh_strength);
        ClassDB::bind_method(D_METHOD("get_rayleigh_strength"),    &LightingSkyNode::get_rayleigh_strength);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"RayleighStrength",PROPERTY_HINT_RANGE,"0,5,0.01"),"set_rayleigh_strength","get_rayleigh_strength");
        ClassDB::bind_method(D_METHOD("set_rayleigh_color","c"),&LightingSkyNode::set_rayleigh_color);
        ClassDB::bind_method(D_METHOD("get_rayleigh_color"),    &LightingSkyNode::get_rayleigh_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"RayleighColor"),"set_rayleigh_color","get_rayleigh_color");
        ClassDB::bind_method(D_METHOD("set_mie_strength","v"),&LightingSkyNode::set_mie_strength);
        ClassDB::bind_method(D_METHOD("get_mie_strength"),    &LightingSkyNode::get_mie_strength);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"MieStrength",PROPERTY_HINT_RANGE,"0,5,0.01"),"set_mie_strength","get_mie_strength");
        ClassDB::bind_method(D_METHOD("set_mie_eccentricity","v"),&LightingSkyNode::set_mie_eccentricity);
        ClassDB::bind_method(D_METHOD("get_mie_eccentricity"),    &LightingSkyNode::get_mie_eccentricity);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"MieEccentricity",PROPERTY_HINT_RANGE,"-0.99,0.99,0.01"),"set_mie_eccentricity","get_mie_eccentricity");
        ClassDB::bind_method(D_METHOD("set_mie_color","c"),&LightingSkyNode::set_mie_color);
        ClassDB::bind_method(D_METHOD("get_mie_color"),    &LightingSkyNode::get_mie_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"MieColor"),"set_mie_color","get_mie_color");

        ClassDB::bind_method(D_METHOD("set_horizon_fog_color","c"),&LightingSkyNode::set_horizon_fog_color);
        ClassDB::bind_method(D_METHOD("get_horizon_fog_color"),    &LightingSkyNode::get_horizon_fog_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"HorizonFogColor"),"set_horizon_fog_color","get_horizon_fog_color");
        ClassDB::bind_method(D_METHOD("set_horizon_fog_amount","v"),&LightingSkyNode::set_horizon_fog_amount);
        ClassDB::bind_method(D_METHOD("get_horizon_fog_amount"),    &LightingSkyNode::get_horizon_fog_amount);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"HorizonFogAmount",PROPERTY_HINT_RANGE,"0,1,0.001"),"set_horizon_fog_amount","get_horizon_fog_amount");
        ClassDB::bind_method(D_METHOD("set_horizon_sharpness","v"),&LightingSkyNode::set_horizon_sharpness);
        ClassDB::bind_method(D_METHOD("get_horizon_sharpness"),    &LightingSkyNode::get_horizon_sharpness);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"HorizonSharpness",PROPERTY_HINT_RANGE,"0.5,20,0.1"),"set_horizon_sharpness","get_horizon_sharpness");

        ClassDB::bind_method(D_METHOD("set_sunset_color","c"),&LightingSkyNode::set_sunset_color);
        ClassDB::bind_method(D_METHOD("get_sunset_color"),    &LightingSkyNode::get_sunset_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"SunsetColor"),"set_sunset_color","get_sunset_color");
        ClassDB::bind_method(D_METHOD("set_sunset_strength","v"),&LightingSkyNode::set_sunset_strength);
        ClassDB::bind_method(D_METHOD("get_sunset_strength"),    &LightingSkyNode::get_sunset_strength);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"SunsetStrength",PROPERTY_HINT_RANGE,"0,3,0.01"),"set_sunset_strength","get_sunset_strength");

        ClassDB::bind_method(D_METHOD("set_sun_disk_scale","v"),&LightingSkyNode::set_sun_disk_scale);
        ClassDB::bind_method(D_METHOD("get_sun_disk_scale"),    &LightingSkyNode::get_sun_disk_scale);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"SunDiskScale",PROPERTY_HINT_RANGE,"0,10,0.01"),"set_sun_disk_scale","get_sun_disk_scale");
        ClassDB::bind_method(D_METHOD("set_sun_energy","v"),&LightingSkyNode::set_sun_energy);
        ClassDB::bind_method(D_METHOD("get_sun_energy"),    &LightingSkyNode::get_sun_energy);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"SunEnergy",PROPERTY_HINT_RANGE,"0,16,0.01"),"set_sun_energy","get_sun_energy");
        ClassDB::bind_method(D_METHOD("set_sun_corona_size","v"),&LightingSkyNode::set_sun_corona_size);
        ClassDB::bind_method(D_METHOD("get_sun_corona_size"),    &LightingSkyNode::get_sun_corona_size);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"SunCoronaSize",PROPERTY_HINT_RANGE,"0,5,0.01"),"set_sun_corona_size","get_sun_corona_size");
        ClassDB::bind_method(D_METHOD("set_sun_disk_color","c"),&LightingSkyNode::set_sun_disk_color);
        ClassDB::bind_method(D_METHOD("get_sun_disk_color"),    &LightingSkyNode::get_sun_disk_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"SunDiskColor"),"set_sun_disk_color","get_sun_disk_color");
        ClassDB::bind_method(D_METHOD("set_ground_energy","v"),&LightingSkyNode::set_ground_energy);
        ClassDB::bind_method(D_METHOD("get_ground_energy"),    &LightingSkyNode::get_ground_energy);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"GroundEnergy",PROPERTY_HINT_RANGE,"0,4,0.01"),"set_ground_energy","get_ground_energy");

        ClassDB::bind_method(D_METHOD("set_star_density","v"),&LightingSkyNode::set_star_density);
        ClassDB::bind_method(D_METHOD("get_star_density"),    &LightingSkyNode::get_star_density);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"StarDensity",PROPERTY_HINT_RANGE,"0,100,0.1"),"set_star_density","get_star_density");
        ClassDB::bind_method(D_METHOD("set_star_brightness","v"),&LightingSkyNode::set_star_brightness);
        ClassDB::bind_method(D_METHOD("get_star_brightness"),    &LightingSkyNode::get_star_brightness);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"StarBrightness",PROPERTY_HINT_RANGE,"0,3,0.01"),"set_star_brightness","get_star_brightness");
        ClassDB::bind_method(D_METHOD("set_star_size","v"),&LightingSkyNode::set_star_size);
        ClassDB::bind_method(D_METHOD("get_star_size"),    &LightingSkyNode::get_star_size);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"StarSize",PROPERTY_HINT_RANGE,"0.1,5,0.01"),"set_star_size","get_star_size");
        ClassDB::bind_method(D_METHOD("set_star_twinkle_speed","v"),&LightingSkyNode::set_star_twinkle_speed);
        ClassDB::bind_method(D_METHOD("get_star_twinkle_speed"),    &LightingSkyNode::get_star_twinkle_speed);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"StarTwinkleSpeed",PROPERTY_HINT_RANGE,"0,5,0.01"),"set_star_twinkle_speed","get_star_twinkle_speed");

        ClassDB::bind_method(D_METHOD("set_cloud_amount","v"),&LightingSkyNode::set_cloud_amount);
        ClassDB::bind_method(D_METHOD("get_cloud_amount"),    &LightingSkyNode::get_cloud_amount);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"CloudAmount",PROPERTY_HINT_RANGE,"0,1,0.01"),"set_cloud_amount","get_cloud_amount");
        ClassDB::bind_method(D_METHOD("set_cloud_color","c"),&LightingSkyNode::set_cloud_color);
        ClassDB::bind_method(D_METHOD("get_cloud_color"),    &LightingSkyNode::get_cloud_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"CloudColor"),"set_cloud_color","get_cloud_color");

        ClassDB::bind_method(D_METHOD("set_moon_color","c"),&LightingSkyNode::set_moon_color);
        ClassDB::bind_method(D_METHOD("get_moon_color"),    &LightingSkyNode::get_moon_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"MoonColor"),"set_moon_color","get_moon_color");
        ClassDB::bind_method(D_METHOD("set_moon_size","v"),&LightingSkyNode::set_moon_size);
        ClassDB::bind_method(D_METHOD("get_moon_size"),    &LightingSkyNode::get_moon_size);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"MoonSize",PROPERTY_HINT_RANGE,"0,5,0.01"),"set_moon_size","get_moon_size");
        ClassDB::bind_method(D_METHOD("set_moon_energy","v"),&LightingSkyNode::set_moon_energy);
        ClassDB::bind_method(D_METHOD("get_moon_energy"),    &LightingSkyNode::get_moon_energy);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"MoonEnergy",PROPERTY_HINT_RANGE,"0,4,0.01"),"set_moon_energy","get_moon_energy");
        ClassDB::bind_method(D_METHOD("set_nebula_color","c"),&LightingSkyNode::set_nebula_color);
        ClassDB::bind_method(D_METHOD("get_nebula_color"),    &LightingSkyNode::get_nebula_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"NebulaColor"),"set_nebula_color","get_nebula_color");
        ClassDB::bind_method(D_METHOD("set_nebula_strength","v"),&LightingSkyNode::set_nebula_strength);
        ClassDB::bind_method(D_METHOD("get_nebula_strength"),    &LightingSkyNode::get_nebula_strength);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"NebulaStrength",PROPERTY_HINT_RANGE,"0,1,0.01"),"set_nebula_strength","get_nebula_strength");
        ClassDB::bind_method(D_METHOD("set_milky_way_strength","v"),&LightingSkyNode::set_milky_way_strength);
        ClassDB::bind_method(D_METHOD("get_milky_way_strength"),    &LightingSkyNode::get_milky_way_strength);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"MilkyWayStrength",PROPERTY_HINT_RANGE,"0,2,0.01"),"set_milky_way_strength","get_milky_way_strength");
    }
    // EXIT_TREE tambien: en Roblox sacar (o destruir) un efecto de Lighting lo
    // APAGA. Sin esto, el clima que borra sus clones dejaba la pantalla
    // borrosa/brillante el resto de la partida. _apply() ya toma el camino
    // de apagado porque gl_fx_en_lighting() es false fuera del arbol.
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_EXIT_TREE) _apply();
    }

public:
    void set_sky_preset(int v)          { sky_preset = v;                                  if(is_inside_tree()) _apply(); }
    int  get_sky_preset() const         { return sky_preset; }
    void set_sky_color(Color c)         { sky_color = c;                                   if(is_inside_tree()) _apply(); }
    Color get_sky_color() const         { return sky_color; }
    void set_horizon_color(Color c)     { horizon_color = c;                               if(is_inside_tree()) _apply(); }
    Color get_horizon_color() const     { return horizon_color; }
    void set_dawn_color(Color c)        { dawn_color = c;                                  if(is_inside_tree()) _apply(); }
    Color get_dawn_color() const        { return dawn_color; }
    void set_sky_mid_color(Color c)     { sky_mid_color = c;                               if(is_inside_tree()) _apply(); }
    Color get_sky_mid_color() const     { return sky_mid_color; }
    void set_ground_color(Color c)      { ground_color = c;                                if(is_inside_tree()) _apply(); }
    Color get_ground_color() const      { return ground_color; }
    void set_sky_exposure(float v)      { sky_exposure = Math::clamp(v,0.0f,8.0f);         if(is_inside_tree()) _apply(); }
    float get_sky_exposure() const      { return sky_exposure; }
    void set_sky_saturation(float v)    { sky_saturation = Math::clamp(v,0.0f,3.0f);       if(is_inside_tree()) _apply(); }
    float get_sky_saturation() const    { return sky_saturation; }
    void set_sky_curve(float v)         { sky_curve = Math::clamp(v,0.0f,1.0f);            if(is_inside_tree()) _apply(); }
    float get_sky_curve() const         { return sky_curve; }
    void set_sun_angular_size(float v)  { sun_angular_size = Math::clamp(v,0.0f,90.0f);    if(is_inside_tree()) _apply(); }
    float get_sun_angular_size() const  { return sun_angular_size; }
    void set_rayleigh_strength(float v) { rayleigh_strength = Math::clamp(v,0.0f,5.0f);    if(is_inside_tree()) _apply(); }
    float get_rayleigh_strength() const { return rayleigh_strength; }
    void set_rayleigh_color(Color c)    { rayleigh_color = c;                               if(is_inside_tree()) _apply(); }
    Color get_rayleigh_color() const    { return rayleigh_color; }
    void set_mie_strength(float v)      { mie_strength = Math::clamp(v,0.0f,5.0f);         if(is_inside_tree()) _apply(); }
    float get_mie_strength() const      { return mie_strength; }
    void set_mie_eccentricity(float v)  { mie_eccentricity = Math::clamp(v,-0.99f,0.99f);  if(is_inside_tree()) _apply(); }
    float get_mie_eccentricity() const  { return mie_eccentricity; }
    void set_mie_color(Color c)         { mie_color = c;                                    if(is_inside_tree()) _apply(); }
    Color get_mie_color() const         { return mie_color; }
    void set_horizon_fog_color(Color c) { horizon_fog_color = c;                            if(is_inside_tree()) _apply(); }
    Color get_horizon_fog_color() const { return horizon_fog_color; }
    void set_horizon_fog_amount(float v){ horizon_fog_amount = Math::clamp(v,0.0f,1.0f);   if(is_inside_tree()) _apply(); }
    float get_horizon_fog_amount() const{ return horizon_fog_amount; }
    void set_horizon_sharpness(float v) { horizon_sharpness = Math::clamp(v,0.5f,20.0f);   if(is_inside_tree()) _apply(); }
    float get_horizon_sharpness() const { return horizon_sharpness; }
    void set_sunset_color(Color c)      { sunset_color = c;                                 if(is_inside_tree()) _apply(); }
    Color get_sunset_color() const      { return sunset_color; }
    void set_sunset_strength(float v)   { sunset_strength = Math::clamp(v,0.0f,3.0f);      if(is_inside_tree()) _apply(); }
    float get_sunset_strength() const   { return sunset_strength; }
    void set_sun_disk_scale(float v)    { sun_disk_scale = Math::clamp(v,0.0f,10.0f);      if(is_inside_tree()) _apply(); }
    float get_sun_disk_scale() const    { return sun_disk_scale; }
    void set_sun_energy(float v)        { sun_energy = Math::clamp(v,0.0f,16.0f);          if(is_inside_tree()) _apply(); }
    float get_sun_energy() const        { return sun_energy; }
    void set_sun_corona_size(float v)   { sun_corona_size = Math::clamp(v,0.0f,5.0f);      if(is_inside_tree()) _apply(); }
    float get_sun_corona_size() const   { return sun_corona_size; }
    void set_sun_disk_color(Color c)    { sun_disk_color = c;                               if(is_inside_tree()) _apply(); }
    Color get_sun_disk_color() const    { return sun_disk_color; }
    void set_ground_energy(float v)     { ground_energy = Math::clamp(v,0.0f,4.0f);        if(is_inside_tree()) _apply(); }
    float get_ground_energy() const     { return ground_energy; }
    void set_star_density(float v)      { star_density = Math::clamp(v,0.0f,100.0f);       if(is_inside_tree()) _apply(); }
    float get_star_density() const      { return star_density; }
    void set_star_brightness(float v)   { star_brightness = Math::clamp(v,0.0f,3.0f);      if(is_inside_tree()) _apply(); }
    float get_star_brightness() const   { return star_brightness; }
    void set_star_size(float v)         { star_size = Math::clamp(v,0.1f,5.0f);            if(is_inside_tree()) _apply(); }
    float get_star_size() const         { return star_size; }
    void set_star_twinkle_speed(float v){ star_twinkle_speed = Math::clamp(v,0.0f,5.0f);   if(is_inside_tree()) _apply(); }
    float get_star_twinkle_speed() const{ return star_twinkle_speed; }
    void set_cloud_amount(float v)      { cloud_amount = Math::clamp(v,0.0f,1.0f);         if(is_inside_tree()) _apply(); }
    float get_cloud_amount() const      { return cloud_amount; }
    void set_cloud_color(Color c)       { cloud_color = c;                                  if(is_inside_tree()) _apply(); }
    Color get_cloud_color() const       { return cloud_color; }
    void set_moon_color(Color c)        { moon_color = c;                                   if(is_inside_tree()) _apply(); }
    Color get_moon_color() const        { return moon_color; }
    void set_moon_size(float v)         { moon_size = Math::clamp(v,0.0f,5.0f);            if(is_inside_tree()) _apply(); }
    float get_moon_size() const         { return moon_size; }
    void set_moon_energy(float v)       { moon_energy = Math::clamp(v,0.0f,4.0f);          if(is_inside_tree()) _apply(); }
    float get_moon_energy() const       { return moon_energy; }
    void set_nebula_color(Color c)      { nebula_color = c;                                 if(is_inside_tree()) _apply(); }
    Color get_nebula_color() const      { return nebula_color; }
    void set_nebula_strength(float v)   { nebula_strength = Math::clamp(v,0.0f,1.0f);      if(is_inside_tree()) _apply(); }
    float get_nebula_strength() const   { return nebula_strength; }
    void set_milky_way_strength(float v){ milky_way_strength = Math::clamp(v,0.0f,2.0f);   if(is_inside_tree()) _apply(); }
    float get_milky_way_strength() const{ return milky_way_strength; }
};

// ════════════════════════════════════════════════════════════════════
//  SunRaysNode — God-rays / sun shafts via glow
// ════════════════════════════════════════════════════════════════════
class SunRaysNode : public Node {
    GDCLASS(SunRaysNode, Node);

    float intensity   = 0.25f;
    float spread      = 1.0f;
    Color ray_color   = Color(1.00f, 0.95f, 0.70f);
    float bloom_boost = 0.5f;
    bool  enabled     = true;   // Roblox SunRaysEffect.Enabled

    // NO toca el Environment. Antes imitaba los rayos con el glow, que es EL
    // MISMO que usa BloomEffect: el que se aplicara el ultimo pisaba al otro, y
    // con Intensity < 0.01 lo APAGABA. En Build a Mountain (Bloom 0.35/24/1.6 y
    // SunRays 0.05/0.5) el bloom del juego se perdia entero. Los rayos de
    // Roblox solo se ven mirando al sol; quedan pendientes de un efecto propio.
    void _apply() {}

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_enabled","v"), &SunRaysNode::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),     &SunRaysNode::get_enabled);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"Enabled"),"set_enabled","get_enabled");
        ClassDB::bind_method(D_METHOD("set_intensity","v"), &SunRaysNode::set_intensity);
        ClassDB::bind_method(D_METHOD("get_intensity"),     &SunRaysNode::get_intensity);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Intensity",PROPERTY_HINT_RANGE,"0,1,0.001"),"set_intensity","get_intensity");
        ClassDB::bind_method(D_METHOD("set_spread","v"), &SunRaysNode::set_spread);
        ClassDB::bind_method(D_METHOD("get_spread"),     &SunRaysNode::get_spread);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Spread",PROPERTY_HINT_RANGE,"0,1,0.001"),"set_spread","get_spread");
        ClassDB::bind_method(D_METHOD("set_ray_color","c"), &SunRaysNode::set_ray_color);
        ClassDB::bind_method(D_METHOD("get_ray_color"),     &SunRaysNode::get_ray_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"RayColor"),"set_ray_color","get_ray_color");
        ClassDB::bind_method(D_METHOD("set_bloom_boost","v"), &SunRaysNode::set_bloom_boost);
        ClassDB::bind_method(D_METHOD("get_bloom_boost"),     &SunRaysNode::get_bloom_boost);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"BloomBoost",PROPERTY_HINT_RANGE,"0,2,0.01"),"set_bloom_boost","get_bloom_boost");
    }
    // EXIT_TREE tambien: en Roblox sacar (o destruir) un efecto de Lighting lo
    // APAGA. Sin esto, el clima que borra sus clones dejaba la pantalla
    // borrosa/brillante el resto de la partida. _apply() ya toma el camino
    // de apagado porque gl_fx_en_lighting() es false fuera del arbol.
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_EXIT_TREE) _apply();
    }

public:
    void set_enabled(bool v)      { enabled     = v;                         if(is_inside_tree()) _apply(); }
    bool get_enabled() const      { return enabled; }
    void set_intensity(float v)   { intensity   = Math::clamp(v,0.0f,1.0f); if(is_inside_tree()) _apply(); }
    float get_intensity() const   { return intensity; }
    void set_spread(float v)      { spread      = Math::clamp(v,0.0f,1.0f); if(is_inside_tree()) _apply(); }
    float get_spread() const      { return spread; }
    void set_ray_color(Color c)   { ray_color   = c;                         if(is_inside_tree()) _apply(); }
    Color get_ray_color() const   { return ray_color; }
    void set_bloom_boost(float v) { bloom_boost = Math::clamp(v,0.0f,2.0f); if(is_inside_tree()) _apply(); }
    float get_bloom_boost() const { return bloom_boost; }
};

// ════════════════════════════════════════════════════════════════════
//  BloomEffect — HDR bloom / glow
// ════════════════════════════════════════════════════════════════════
class BloomEffect : public Node {
    GDCLASS(BloomEffect, Node);

    // Defaults 1:1 con Roblox: Intensity 0.4, Size 24 (PIXELES), Threshold 0.95.
    // https://create.roblox.com/docs/reference/engine/classes/BloomEffect
    // (El Size anterior era un 0..1 inventado: el 24 de un place importado se
    // clampeaba a 1 y el bloom salia irreconocible.)
    float intensity    = 0.4f;
    float size         = 24.0f;
    float threshold    = 0.95f;
    float hdr_scale    = 2.0f;
    float lum_cap      = 8.0f;
    // Roblox documenta el bloom como "blending ADITIVO", no softlight.
    int   blend_mode   = (int)Environment::GLOW_BLEND_MODE_ADDITIVE;
    bool  enabled      = true;   // Roblox BloomEffect.Enabled

    void _apply() {
        // Fuera de Lighting el efecto es inerte (ver gl_fx_en_lighting). Y en
        // las calidades 1-5 tampoco: Roblox no dibuja el halo hasta Level12
        // (medido con las farolas de Build a Mountain).
        const bool act = enabled && gl_fx_en_lighting(this) && gl_calidad_bloom();
        Ref<Environment> env = _lx_get_env(this);
        if (!env.is_valid()) return;
        // Size en PIXELES = hasta donde llega el halo. MEDIDO con las farolas de
        // neon de Build a Mountain (Size 24): el halo de Roblox cae a la mitad a
        // ~8 px del borde y se acaba a ~25. Eso es SOLO el nivel 1 de Godot (el
        // nivel i difumina ~2^(i+2) px): a peso 0.6 el perfil sale igual
        // (+66/+40/+11 de rojo a 6/10/18 px, Roblox +67/+40/+15). Antes iban
        // los niveles 0-4 a la vez: halo cuatro veces mas ancho y sin fuerza.
        // Con otro Size se reparte entre los dos niveles vecinos de log2(Size/12).
        // Size 0 en Roblox = sin sangrado.
        float pesos[7] = {0, 0, 0, 0, 0, 0, 0};
        const float k = Math::clamp(Math::log(Math::max(size, 1.0f) / 12.0f) / Math::log(2.0f), 0.0f, 6.0f);
        const int k0 = (int)Math::floor(k);
        const float f = k - (float)k0;
        if (size >= 0.5f) {
            pesos[k0] = 0.6f * (1.0f - f);
            if (k0 < 6) pesos[k0 + 1] = 0.6f * f;
        }
        // En Forward+ el bloom va por gl_postfx.h (mismas cuentas, ~1 ms menos
        // de GPU) y el glow de Godot se queda apagado.
        if (blend_mode == (int)Environment::GLOW_BLEND_MODE_ADDITIVE) {
            if (GLPostFX* fx = gl_postfx_de(_lx_get_we(this))) {
                env->set_glow_enabled(false);
                GLBloomAjuste b;
                b.on = act && intensity > 0.001f && size >= 0.5f;
                for (int i = 0; i < 7; i++) b.niveles[i] = pesos[i];
                b.intensidad = intensity;
                b.umbral = threshold;
                b.escala = hdr_scale;
                b.tope = lum_cap;
                fx->gl_poner_bloom(b, env);
                return;
            }
        }
        env->set_glow_enabled(act && intensity > 0.001f);
        if (!act || intensity < 0.001f) return;
        env->set_glow_normalized(false);
        env->set_glow_intensity(intensity);
        // Explicito: SunRaysNode sube glow_strength a 1.5+ y nadie lo bajaba,
        // asi que tras la primera tormenta el glow se quedaba un 50% mas fuerte
        // el resto de la partida (colores quemados).
        env->set_glow_strength(1.0f);
        env->set_glow_bloom(0.0f);   // sin "bloom global": solo lo que pasa el umbral
        // Threshold opera sobre valores HDR PRE-tonemap, igual que el bleed
        // threshold de Godot (por eso la plantilla 2021 de Roblox usa Threshold=2
        // sin que bloomee todo).
        env->set_glow_hdr_bleed_threshold(threshold);
        env->set_glow_hdr_bleed_scale(hdr_scale);
        env->set_glow_hdr_luminance_cap(lum_cap);
        env->set_glow_blend_mode((Environment::GlowBlendMode)blend_mode);
        for (int i = 0; i < 7; i++) env->set_glow_level(i, pesos[i]);
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_enabled","v"), &BloomEffect::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),     &BloomEffect::get_enabled);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"Enabled"),"set_enabled","get_enabled");
        ClassDB::bind_method(D_METHOD("set_intensity","v"), &BloomEffect::set_intensity);
        ClassDB::bind_method(D_METHOD("get_intensity"),     &BloomEffect::get_intensity);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Intensity",PROPERTY_HINT_RANGE,"0,3,0.001"),"set_intensity","get_intensity");
        ClassDB::bind_method(D_METHOD("set_size","v"), &BloomEffect::set_size);
        ClassDB::bind_method(D_METHOD("get_size"),     &BloomEffect::get_size);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Size",PROPERTY_HINT_RANGE,"0,100,0.1"),"set_size","get_size");
        ClassDB::bind_method(D_METHOD("set_threshold","v"), &BloomEffect::set_threshold);
        ClassDB::bind_method(D_METHOD("get_threshold"),     &BloomEffect::get_threshold);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Threshold",PROPERTY_HINT_RANGE,"0,8,0.01"),"set_threshold","get_threshold");
        ClassDB::bind_method(D_METHOD("set_hdr_scale","v"), &BloomEffect::set_hdr_scale);
        ClassDB::bind_method(D_METHOD("get_hdr_scale"),     &BloomEffect::get_hdr_scale);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"HdrScale",PROPERTY_HINT_RANGE,"0.5,4,0.01"),"set_hdr_scale","get_hdr_scale");
        ClassDB::bind_method(D_METHOD("set_lum_cap","v"), &BloomEffect::set_lum_cap);
        ClassDB::bind_method(D_METHOD("get_lum_cap"),     &BloomEffect::get_lum_cap);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"LuminanceCap",PROPERTY_HINT_RANGE,"0,32,0.1"),"set_lum_cap","get_lum_cap");
        ClassDB::bind_method(D_METHOD("set_blend_mode","v"), &BloomEffect::set_blend_mode);
        ClassDB::bind_method(D_METHOD("get_blend_mode"),     &BloomEffect::get_blend_mode);
        ADD_PROPERTY(PropertyInfo(Variant::INT,"BlendMode",PROPERTY_HINT_ENUM,
            "Additive,Screen,Softlight,Replace,Mix"),"set_blend_mode","get_blend_mode");
        ClassDB::bind_method(D_METHOD("_gl_reaplicar"), &BloomEffect::_gl_reaplicar);
    }
public:
    void _gl_reaplicar() { if (is_inside_tree()) _apply(); }   // cambio de calidad
protected:
    // EXIT_TREE tambien: en Roblox sacar (o destruir) un efecto de Lighting lo
    // APAGA. Sin esto, el clima que borra sus clones dejaba la pantalla
    // borrosa/brillante el resto de la partida. _apply() ya toma el camino
    // de apagado porque gl_fx_en_lighting() es false fuera del arbol.
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_EXIT_TREE) _apply();
    }

public:
    void set_enabled(bool v)     { enabled    = v;                          if(is_inside_tree()) _apply(); }
    bool get_enabled() const     { return enabled; }
    void set_intensity(float v)  { intensity  = Math::clamp(v,0.0f,3.0f);  if(is_inside_tree()) _apply(); }
    float get_intensity() const  { return intensity; }
    // BloomEffect.Size en Roblox va de 0 a 100 (por defecto 24), no de 0 a 1:
    // con el clamp a 1 cualquier place o script que escribiera su valor real
    // se quedaba clavado en 1 y el bloom desaparecia.
    void set_size(float v)       { size       = Math::clamp(v,0.0f,100.0f); if(is_inside_tree()) _apply(); }
    float get_size() const       { return size; }
    void set_threshold(float v)  { threshold  = Math::clamp(v,0.0f,8.0f);  if(is_inside_tree()) _apply(); }
    float get_threshold() const  { return threshold; }
    void set_hdr_scale(float v)  { hdr_scale  = Math::clamp(v,0.5f,4.0f);  if(is_inside_tree()) _apply(); }
    float get_hdr_scale() const  { return hdr_scale; }
    void set_lum_cap(float v)    { lum_cap    = Math::clamp(v,0.0f,32.0f); if(is_inside_tree()) _apply(); }
    float get_lum_cap() const    { return lum_cap; }
    void set_blend_mode(int v)   { blend_mode = v;                          if(is_inside_tree()) _apply(); }
    int  get_blend_mode() const  { return blend_mode; }
};

// ════════════════════════════════════════════════════════════════════
//  BlurEffect — Full-scene blur (Roblox BlurEffect)
// ════════════════════════════════════════════════════════════════════
// ── UN SOLO CameraAttributes para todo el mundo ──────────────────────────────
//  Blur y DepthOfField escribian cada uno un CameraAttributesPractical NUEVO en
//  el mismo WorldEnvironment: el ultimo en aplicarse borraba la configuracion
//  del otro (en Roblox conviven). Ahora se reutiliza el que ya haya y cada
//  efecto toca SOLO sus campos.
inline Ref<CameraAttributesPractical> _lx_cam_attrs(WorldEnvironment* we) {
    if (!we) return Ref<CameraAttributesPractical>();
    Ref<CameraAttributesPractical> cam = we->get_camera_attributes();
    if (cam.is_null()) {
        cam.instantiate();
        we->set_camera_attributes(cam);
    }
    return cam;
}

class BlurEffect : public Node {
    GDCLASS(BlurEffect, Node);

    float size       = 0.0f;
    float near_size  = 0.0f;
    float far_size   = 0.0f;
    bool  enabled    = true;   // Roblox BlurEffect.Enabled

    void _apply() {
        // Fuera de Lighting el efecto es inerte (ver gl_fx_en_lighting).
        const bool act = enabled && gl_fx_en_lighting(this);
        WorldEnvironment* we = _lx_find_we_node(this);
        if (!we) return;
        Ref<CameraAttributesPractical> cam = _lx_cam_attrs(we);
        if (cam.is_null()) return;
        float effective = Math::max(size, Math::max(near_size, far_size));
        if (!act || effective < 0.01f) {
            cam->set_dof_blur_far_enabled(false);
            cam->set_dof_blur_near_enabled(false);
        } else {
            float far_amt  = Math::max(size, far_size);
            float near_amt = Math::max(size, near_size);
            cam->set_dof_blur_far_enabled(far_amt > 0.01f);
            cam->set_dof_blur_far_distance(1.0f + (1.0f - far_amt * 0.018f) * 200.0f);
            cam->set_dof_blur_far_transition(far_amt * 3.0f);
            cam->set_dof_blur_near_enabled(near_amt > 0.01f);
            cam->set_dof_blur_near_distance((1.0f - near_amt * 0.018f) * 5.0f);
            cam->set_dof_blur_near_transition(near_amt * 1.5f);
            cam->set_dof_blur_amount(Math::clamp(effective / 56.0f, 0.0f, 1.0f));
        }
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_enabled","v"), &BlurEffect::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),     &BlurEffect::get_enabled);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"Enabled"),"set_enabled","get_enabled");
        ClassDB::bind_method(D_METHOD("set_size","v"),      &BlurEffect::set_size);
        ClassDB::bind_method(D_METHOD("get_size"),          &BlurEffect::get_size);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Size",PROPERTY_HINT_RANGE,"0,56,0.1"),"set_size","get_size");
        ClassDB::bind_method(D_METHOD("set_near_size","v"), &BlurEffect::set_near_size);
        ClassDB::bind_method(D_METHOD("get_near_size"),     &BlurEffect::get_near_size);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"NearBlur",PROPERTY_HINT_RANGE,"0,56,0.1"),"set_near_size","get_near_size");
        ClassDB::bind_method(D_METHOD("set_far_size","v"),  &BlurEffect::set_far_size);
        ClassDB::bind_method(D_METHOD("get_far_size"),      &BlurEffect::get_far_size);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"FarBlur",PROPERTY_HINT_RANGE,"0,56,0.1"),"set_far_size","get_far_size");
    }
    // EXIT_TREE tambien: en Roblox sacar (o destruir) un efecto de Lighting lo
    // APAGA. Sin esto, el clima que borra sus clones dejaba la pantalla
    // borrosa/brillante el resto de la partida. _apply() ya toma el camino
    // de apagado porque gl_fx_en_lighting() es false fuera del arbol.
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_EXIT_TREE) _apply();
    }

public:
    void set_enabled(bool v)    { enabled   = v;                         if(is_inside_tree()) _apply(); }
    bool get_enabled() const    { return enabled; }
    void set_size(float v)      { size      = Math::clamp(v,0.0f,56.0f); if(is_inside_tree()) _apply(); }
    float get_size() const      { return size; }
    void set_near_size(float v) { near_size = Math::clamp(v,0.0f,56.0f); if(is_inside_tree()) _apply(); }
    float get_near_size() const { return near_size; }
    void set_far_size(float v)  { far_size  = Math::clamp(v,0.0f,56.0f); if(is_inside_tree()) _apply(); }
    float get_far_size() const  { return far_size; }
};

// ════════════════════════════════════════════════════════════════════
//  ColorCorrectionEffect — brightness, contrast, saturation, tint
// ════════════════════════════════════════════════════════════════════
class ColorCorrectionEffect : public Node {
    GDCLASS(ColorCorrectionEffect, Node);

    // ── SEMANTICA DE ROBLOX, no de Godot ─────────────────────────────────
    // En Roblox el NEUTRO de estas tres propiedades es 0, y el rango util va de
    // -1 a 1 (Saturation admite mas): 0.2 significa "un 20% MAS saturado".
    // En Godot el neutro del adjustment del Environment es 1.0, y 0.2 significa
    // "casi blanco y negro". Guardar el valor de Roblox tal cual en un campo con
    // neutro 1.0 era EL bug historico de "todo gris": el place de prueba trae
    // Saturation=0.2 / Contrast=0.1 y salia el mundo lavado. Los parches
    // "antigris" de la 1.16.6 en adelante tapaban esto apagando el ajuste
    // entero, lo que a su vez dejaba ColorCorrectionEffect muerto.
    // Aqui se guarda lo que dice Roblox y se convierte al aplicar (1 + valor).
    // https://create.roblox.com/docs/reference/engine/classes/ColorCorrectionEffect
    float brightness  = 0.0f;   // Roblox: -1..1, 0 = neutro
    float contrast    = 0.0f;   // Roblox: -1..1, 0 = neutro
    float saturation  = 0.0f;   // Roblox: -1..  , 0 = neutro
    float gamma       = 1.0f;   // no existe en Roblox: neutro 1.0 (uso interno)
    Color tint_color  = Color(1.0f, 1.0f, 1.0f);
    float tint_amount = 0.0f;
    bool  enabled     = true;   // Roblox ColorCorrectionEffect.Enabled
    bool  _saliendo   = false;  // en EXIT_TREE aun cuelga de Lighting

    bool _neutro() const {
        return fabsf(brightness) < 0.001f && fabsf(contrast) < 0.001f && fabsf(saturation) < 0.001f &&
               fabsf(tint_color.r - 1.0f) < 0.002f && fabsf(tint_color.g - 1.0f) < 0.002f &&
               fabsf(tint_color.b - 1.0f) < 0.002f;
    }

    // ── La formula EXACTA de Roblox, MEDIDA (2026-10-08) ──────────────
    // Carta de 60 muestras en Play de Studio con solo este efecto: Roblox lo
    // aplica en espacio de pantalla, despues de la curva de tonos (formula en
    // gl_tono_correccion; error medio 0,55 sobre 255, sin el efecto 7,9). El
    // ajuste del Environment de Godot no lo puede hacer: su brillo MULTIPLICA y
    // su saturacion usa la media de los tres canales. Por eso va en la LUT de la
    // curva de tonos (gl_tono_roblox.h), con todos los ColorCorrection activos
    // de Lighting encadenados, como hace Roblox. Fuera de Lighting es inerte.
    void _apply() {
        Node* l = is_inside_tree() ? get_parent() : nullptr;
        if (!l || !l->is_class("Lighting")) return;
        gl_tono_reconstruir(this);
    }

public:
    bool gl_activa() const { return enabled && !_saliendo && !_neutro(); }
    GLCorreccion gl_correccion() const {
        GLCorreccion k;
        k.brillo = brightness; k.contraste = contrast; k.saturacion = saturation; k.tinte = tint_color;
        return k;
    }
protected:

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_enabled","v"), &ColorCorrectionEffect::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),     &ColorCorrectionEffect::get_enabled);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"Enabled"),"set_enabled","get_enabled");
        ClassDB::bind_method(D_METHOD("set_brightness","v"), &ColorCorrectionEffect::set_brightness);
        ClassDB::bind_method(D_METHOD("get_brightness"),     &ColorCorrectionEffect::get_brightness);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Brightness",PROPERTY_HINT_RANGE,"-1,1,0.001"),"set_brightness","get_brightness");
        ClassDB::bind_method(D_METHOD("set_contrast","v"), &ColorCorrectionEffect::set_contrast);
        ClassDB::bind_method(D_METHOD("get_contrast"),     &ColorCorrectionEffect::get_contrast);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Contrast",PROPERTY_HINT_RANGE,"-1,1,0.001"),"set_contrast","get_contrast");
        ClassDB::bind_method(D_METHOD("set_saturation","v"), &ColorCorrectionEffect::set_saturation);
        ClassDB::bind_method(D_METHOD("get_saturation"),     &ColorCorrectionEffect::get_saturation);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Saturation",PROPERTY_HINT_RANGE,"-1,5,0.001"),"set_saturation","get_saturation");
        ClassDB::bind_method(D_METHOD("set_gamma","v"), &ColorCorrectionEffect::set_gamma);
        ClassDB::bind_method(D_METHOD("get_gamma"),     &ColorCorrectionEffect::get_gamma);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Gamma",PROPERTY_HINT_RANGE,"0.1,4,0.01"),"set_gamma","get_gamma");
        ClassDB::bind_method(D_METHOD("set_tint_color","c"), &ColorCorrectionEffect::set_tint_color);
        ClassDB::bind_method(D_METHOD("get_tint_color"),     &ColorCorrectionEffect::get_tint_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"TintColor"),"set_tint_color","get_tint_color");
        ClassDB::bind_method(D_METHOD("set_tint_amount","v"), &ColorCorrectionEffect::set_tint_amount);
        ClassDB::bind_method(D_METHOD("get_tint_amount"),     &ColorCorrectionEffect::get_tint_amount);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"TintAmount",PROPERTY_HINT_RANGE,"0,1,0.001"),"set_tint_amount","get_tint_amount");
    }
    // EXIT_TREE tambien: en Roblox sacar (o destruir) un efecto de Lighting lo
    // APAGA. Sin esto, el clima que borra sus clones dejaba la pantalla
    // borrosa/brillante el resto de la partida. Al salir todavia cuelga de
    // Lighting: _saliendo lo deja fuera de la LUT de los que se quedan.
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _apply();
        if (p_what == NOTIFICATION_EXIT_TREE) { _saliendo = true; _apply(); _saliendo = false; }
    }

public:
    void set_enabled(bool v)      { enabled     = v;                          if(is_inside_tree()) _apply(); }
    bool get_enabled() const      { return enabled; }
    void set_brightness(float v)  { brightness  = Math::clamp(v,-1.0f,1.0f); if(is_inside_tree()) _apply(); }
    float get_brightness() const  { return brightness; }
    void set_contrast(float v)    { contrast    = Math::clamp(v,-1.0f,1.0f); if(is_inside_tree()) _apply(); }
    float get_contrast() const    { return contrast; }
    void set_saturation(float v)  { saturation  = Math::clamp(v,-1.0f,5.0f); if(is_inside_tree()) _apply(); }
    float get_saturation() const  { return saturation; }
    void set_gamma(float v)       { gamma       = Math::clamp(v,0.1f,4.0f);  if(is_inside_tree()) _apply(); }
    float get_gamma() const       { return gamma; }
    void set_tint_color(Color c)  { tint_color  = c;                          if(is_inside_tree()) _apply(); }
    Color get_tint_color() const  { return tint_color; }
    void set_tint_amount(float v) { tint_amount = Math::clamp(v,0.0f,1.0f);  if(is_inside_tree()) _apply(); }
    float get_tint_amount() const { return tint_amount; }
};

// La curva de tonos de Roblox (gl_tono_roblox.h) con los ColorCorrection
// activos de Lighting encima, en el Environment del mundo de `nodo`. La llaman
// el Workspace al montar el render y cada ColorCorrection al cambiar.
inline void gl_tono_reconstruir(Node* nodo) {
    Ref<Environment> env = _lx_get_env(nodo);
    if (!env.is_valid()) return;
    LocalVector<GLCorreccion> lista;
    Node* mundo = _lx_mundo_de(nodo);
    Node* l = mundo ? mundo->get_node_or_null(NodePath("Lighting")) : nullptr;
    if (l) {
        for (int i = 0; i < l->get_child_count(); i++) {
            const ColorCorrectionEffect* cc = gl_cast<ColorCorrectionEffect>(l->get_child(i));
            if (cc && cc->gl_activa()) lista.push_back(cc->gl_correccion());
        }
    }
    gl_tono_aplicar(env, lista);
}

// ════════════════════════════════════════════════════════════════════
//  DepthOfFieldEffect — camera depth-of-field blur
// ════════════════════════════════════════════════════════════════════
class DepthOfFieldEffect : public Node {
    GDCLASS(DepthOfFieldEffect, Node);

    float far_distance   = 100.0f;
    float far_transition = 10.0f;
    float near_distance  = 2.0f;
    float near_transition= 1.0f;
    float blur_amount    = 0.1f;
    bool  far_enabled    = false;
    bool  near_enabled   = false;
    bool  enabled        = true;   // Roblox DepthOfFieldEffect.Enabled (maestro)

    void _apply() {
        // Fuera de Lighting el efecto es inerte (ver gl_fx_en_lighting).
        const bool act = enabled && gl_fx_en_lighting(this);
        WorldEnvironment* we = _lx_find_we_node(this);
        if (!we) return;
        Ref<CameraAttributesPractical> cam = _lx_cam_attrs(we);
        if (cam.is_null()) return;
        if (!act) {
            cam->set_dof_blur_far_enabled(false);
            cam->set_dof_blur_near_enabled(false);
            return;
        }
        cam->set_dof_blur_far_enabled(far_enabled);
        if (far_enabled) {
            cam->set_dof_blur_far_distance(far_distance);
            cam->set_dof_blur_far_transition(far_transition);
        }
        cam->set_dof_blur_near_enabled(near_enabled);
        if (near_enabled) {
            cam->set_dof_blur_near_distance(near_distance);
            cam->set_dof_blur_near_transition(near_transition);
        }
        cam->set_dof_blur_amount(blur_amount);
        // (Antes solo se INSTALABA si algo estaba activo: al pasar FarEnabled de
        //  true a false el objeto nuevo no se instalaba nunca y el desenfoque
        //  viejo seguia puesto para siempre. Ahora se muta el compartido, asi
        //  que apagar apaga de verdad.)
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_enabled","v"), &DepthOfFieldEffect::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),     &DepthOfFieldEffect::get_enabled);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"Enabled"),"set_enabled","get_enabled");
        ClassDB::bind_method(D_METHOD("set_far_enabled","v"),    &DepthOfFieldEffect::set_far_enabled);
        ClassDB::bind_method(D_METHOD("get_far_enabled"),        &DepthOfFieldEffect::get_far_enabled);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"FarEnabled"),"set_far_enabled","get_far_enabled");
        ClassDB::bind_method(D_METHOD("set_near_enabled","v"),   &DepthOfFieldEffect::set_near_enabled);
        ClassDB::bind_method(D_METHOD("get_near_enabled"),       &DepthOfFieldEffect::get_near_enabled);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"NearEnabled"),"set_near_enabled","get_near_enabled");
        ClassDB::bind_method(D_METHOD("set_far_distance","v"),   &DepthOfFieldEffect::set_far_distance);
        ClassDB::bind_method(D_METHOD("get_far_distance"),       &DepthOfFieldEffect::get_far_distance);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"FarDistance",PROPERTY_HINT_RANGE,"0,5000,0.1"),"set_far_distance","get_far_distance");
        ClassDB::bind_method(D_METHOD("set_far_transition","v"), &DepthOfFieldEffect::set_far_transition);
        ClassDB::bind_method(D_METHOD("get_far_transition"),     &DepthOfFieldEffect::get_far_transition);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"FarTransition",PROPERTY_HINT_RANGE,"0,1000,0.1"),"set_far_transition","get_far_transition");
        ClassDB::bind_method(D_METHOD("set_near_distance","v"),  &DepthOfFieldEffect::set_near_distance);
        ClassDB::bind_method(D_METHOD("get_near_distance"),      &DepthOfFieldEffect::get_near_distance);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"NearDistance",PROPERTY_HINT_RANGE,"0,1000,0.1"),"set_near_distance","get_near_distance");
        ClassDB::bind_method(D_METHOD("set_near_transition","v"),&DepthOfFieldEffect::set_near_transition);
        ClassDB::bind_method(D_METHOD("get_near_transition"),    &DepthOfFieldEffect::get_near_transition);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"NearTransition",PROPERTY_HINT_RANGE,"0,1000,0.1"),"set_near_transition","get_near_transition");
        ClassDB::bind_method(D_METHOD("set_blur_amount","v"),    &DepthOfFieldEffect::set_blur_amount);
        ClassDB::bind_method(D_METHOD("get_blur_amount"),        &DepthOfFieldEffect::get_blur_amount);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"BlurAmount",PROPERTY_HINT_RANGE,"0,1,0.001"),"set_blur_amount","get_blur_amount");
    }
    // EXIT_TREE tambien: en Roblox sacar (o destruir) un efecto de Lighting lo
    // APAGA. Sin esto, el clima que borra sus clones dejaba la pantalla
    // borrosa/brillante el resto de la partida. _apply() ya toma el camino
    // de apagado porque gl_fx_en_lighting() es false fuera del arbol.
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_EXIT_TREE) _apply();
    }

public:
    void set_enabled(bool v)         { enabled        = v;                                  if(is_inside_tree()) _apply(); }
    bool get_enabled() const         { return enabled; }
    void set_far_enabled(bool v)     { far_enabled    = v;                                  if(is_inside_tree()) _apply(); }
    bool get_far_enabled() const     { return far_enabled; }
    void set_near_enabled(bool v)    { near_enabled   = v;                                  if(is_inside_tree()) _apply(); }
    bool get_near_enabled() const    { return near_enabled; }
    void set_far_distance(float v)   { far_distance   = Math::clamp(v,0.0f,5000.0f);        if(is_inside_tree()) _apply(); }
    float get_far_distance() const   { return far_distance; }
    void set_far_transition(float v) { far_transition  = Math::clamp(v,0.0f,1000.0f);       if(is_inside_tree()) _apply(); }
    float get_far_transition() const { return far_transition; }
    void set_near_distance(float v)  { near_distance  = Math::clamp(v,0.0f,1000.0f);        if(is_inside_tree()) _apply(); }
    float get_near_distance() const  { return near_distance; }
    void set_near_transition(float v){ near_transition = Math::clamp(v,0.0f,1000.0f);       if(is_inside_tree()) _apply(); }
    float get_near_transition() const{ return near_transition; }
    void set_blur_amount(float v)    { blur_amount     = Math::clamp(v,0.0f,1.0f);           if(is_inside_tree()) _apply(); }
    float get_blur_amount() const    { return blur_amount; }
};

#endif // ROBLOX_LIGHTING_FX_H

#ifndef ROBLOX_WORKSPACE_H
#define ROBLOX_WORKSPACE_H

#include "../ui/gl_cola_ui.h"   // cola de UI con presupuesto por frame
#include "gl_cast.h"
#include <vector>
#include <godot_cpp/classes/node3d.hpp>
#include "gl_graphics.h"
#include "gl_ajustes.h"   // ajustes del jugador, sin depender del menu
#include "gl_autopsia.h"
#include "gl_assembly.h"
#include <godot_cpp/classes/engine.hpp>
#include "roblox_network.h"
#include "roblox_terrain.h"
#include "roblox_sound.h"   // gl_sonidos_sueltos: PlayOnRemove y PlayLocalSound
// Curva de tonos de Roblox + ColorCorrection (roblox_lighting_fx.h, mas abajo
// en la misma unidad).
inline void gl_tono_reconstruir(godot::Node* nodo);
#include <godot_cpp/classes/physics_server3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>          // clic en el mundo
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>      // rayo del raton
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/gpu_particles3d.hpp>   // recorte de particulas lejanas
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/capsule_mesh.hpp>      // <-- INDISPENSABLE
#include <godot_cpp/classes/capsule_shape3d.hpp>   // <-- INDISPENSABLE
#include <godot_cpp/classes/resource_loader.hpp>   // malla de avatar por defecto
#include <godot_cpp/classes/resource_saver.hpp>    // guardar environment editable
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>      // personaje del usuario (.glb cara arreglada)
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/procedural_sky_material.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/os.hpp>   // shell_open (link del motor)
#include <godot_cpp/classes/input.hpp>   // estado del raton (sliders del menu)
#include <godot_cpp/classes/viewport_texture.hpp>   // capturas de pantalla (Galeria)   // volumen maestro para el menu en Luau
#include "roblox_player.h"
#include "humanoid.h"
#include "gl_avatar.h"
#include "roblox_part.h"
#include "roblox_services.h"
#include "roblox_interactive.h"
#include "roblox_billboard.h"     // SurfaceGui: clics sobre carteles del mundo
#include "gl_chunks.h"
#include "roblox_constraints.h"   // tick central de constraints
#include "gl_carga_estado.h"      // pantalla de carga por defecto

using namespace godot;

class RobloxWorkspace : public Node3D {
    GDCLASS(RobloxWorkspace, Node3D);

private:
    // Roblox Workspace properties
    float gravity                      = 196.2f;
    float fallen_parts_destroy_height  = -500.0f;
    bool  streaming_enabled            = false;
    float air_density                  = 0.0f;
    bool  touches_use_collision_groups = false;

    // ── Muerte por vacío (1.15) ──────────────────────────────────────────
    // Antes caer no hacía nada: el jugador atravesaba el suelo y seguía cayendo
    // para siempre. En Roblox FallenPartsDestroyHeight destruye lo que cae por
    // debajo (y mata al personaje cuyo HRP baja de ahí). Aquí, además, se puede
    // AUTODETECTAR: tomar la Y del objeto ANCLADO más bajo del mundo y restarle
    // un margen — así funciona en cualquier mapa sin calcular la altura a mano.
    // Configurable: apagar el auto y usar el límite fijo, o cambiar el margen.
    // 0 = automatico (30 Hz en mapas grandes, 60 en pequeños); >0 = fijo
    int   physics_fps          = 0;
    bool  auto_fall_height     = true;    // true = calcular desde el mapa
    float fall_height_margin   = 150.0f;  // se resta a la Y anclada más baja
    double _fall_scan_timer     = 0.0;    // recomputar el suelo cada ~1s, no por frame
    double _sombra_timer        = 0.0;    // recorte de casters lejanos
    Vector3 _sombra_ojo         = Vector3(1e9f, 1e9f, 1e9f);  // fuerza el 1er pase
    bool   _ground_cached       = false;  // el suelo se calcula una vez
    float  _ground_value        = -500.0f;
    double _fall_destroy_timer  = 0.0;    // barrer partes caidas cada ~0.3s, no por frame
    float  _fall_effective      = -500.0f; // umbral vigente (cache)
    // Streaming por distancia (ver _stream_tick). 1024 es el valor por defecto
    // de Roblox y cumple de sobra el minimo de 512 que se pidio.
    float    stream_radius      = 1024.0f;
    // ¿lo escribio el place a mano? Si no, manda la distancia del nivel de
    // calidad (gl_view_studs_actual); si si, actua ademas como tope.
    bool     stream_radius_explicito = false;
    uint32_t _stream_cursor     = 0;      // por donde iba el barrido repartido
    double   _stream_saldo      = 0.0;    // piezas que le tocan (por segundo, no por frame)
    // Reparto del Physics LOD, contado a lo largo de una vuelta completa.
    uint32_t _lod_est = 0, _lod_din = 0, _lod_mec = 0, _lod_frz = 0, _lod_ccd = 0;
    uint32_t _caidas_cursor     = 0;      // idem para el barrido de piezas caidas
    double   _conjuntos_timer   = 0.0;    // reconstruccion diferida de los conjuntos

    void _apply_gravity() {
        // El PERSONAJE tambien cae con esto. En Roblox, Workspace.Gravity manda
        // sobre todas las BaseParts, jugador incluido; aqui solo entraba en el
        // espacio de fisica (o sea, solo los RigidBody). Se publica para que el
        // Humanoid lo lea sin incluir este fichero (ver gl_physlod.h).
        gl_workspace_gravity_ref() = gravity;
        if (!is_inside_tree()) return;
        Viewport* vp = get_viewport();
        if (!vp) return;
        Ref<World3D> world = vp->find_world_3d();
        if (!world.is_valid()) return;
        RID space = world->get_space();
        // El servidor de fisica puede estar ya destruido (al cerrar el juego):
        // sin esta comprobacion salian miles de errores "PhysicsServer3D
        // get_singleton() is null" en la consola durante el apagado.
        PhysicsServer3D* ps = PhysicsServer3D::get_singleton();
        if (space.is_valid() && ps)
            ps->area_set_param(space, PhysicsServer3D::AREA_PARAM_GRAVITY, Variant(gravity));
    }

    // Generates the Roblox-style grid texture
    //// Genera la textura de cuadros tipo Roblox
    Ref<ImageTexture> generar_textura_grid() {
        Ref<Image> img = Image::create_empty(64, 64, false, Image::FORMAT_RGBA8);
        Color color_fondo(0.64, 0.635, 0.647);  // gris piedra medio de Roblox (RGB 163,162,165)
        Color color_linea(0.5, 0.5, 0.52);       // lineas de stud, mas oscuras para que se vean
        
        img->fill(color_fondo);
        for (int i = 0; i < 64; i++) {
            img->set_pixel(i, 0, color_linea);
            img->set_pixel(0, i, color_linea);
        }
        return ImageTexture::create_from_image(img);
    }

    // Crea el material de cielo azul clasico de Roblox (azul suave, no saturado).
    // Se usa para entornos nuevos y para REPARAR uno cuyo cielo se perdio.
    Ref<Sky> _gl_make_roblox_sky() {
        Ref<Sky> sky; sky.instantiate();
        Ref<ProceduralSkyMaterial> m; m.instantiate();
        m->set_sky_top_color(Color(0.306, 0.588, 0.804));
        m->set_sky_horizon_color(Color(0.667, 0.804, 0.902));
        m->set_ground_bottom_color(Color(0.529, 0.549, 0.588));
        m->set_ground_horizon_color(Color(0.667, 0.804, 0.902));
        sky->set_material(m);
        return sky;
    }

    // Aplica el look de iluminacion estilo Roblox "Future" a un Environment + sol:
    // tonemapping filmico (antes era Linear plano), glow sutil (bloom real para
    // Neon y brillos), SSAO (profundidad barata), luz ambiental del cielo y
    // sombras suaves del sol (PCSS, Forward+). Idempotente: se llama al crear la
    // escena en el editor Y en runtime sobre un Environment ya existente, asi el
    // look mejora tambien en proyectos viejos sin recrear la escena.
    //
    // ── ANTIGRIS (1.16.6) ────────────────────────────────────────────────
    // El bug de "todo gris" al crear un Game 3D era un Environment guardado con
    // el AJUSTE de color a saturacion 0.2 (grisaceo) + niebla densa, que se
    // recargaba desde el .tres "editable" en cada escena nueva. Ahora esta
    // funcion GARANTIZA que jamas pueda salir gris: apaga el adjustment, fuerza
    // el fondo a CIELO valido y deja la niebla suave. Como se llama sobre CADA
    // entorno (nuevo o existente), repara tambien escenas ya hechas.
    static bool _gl_find_class_in(Node* n, const char* cls) {
        if (!n) return false;
        if (n->is_class(cls)) return true;
        for (int i = 0; i < n->get_child_count(); i++)
            if (_gl_find_class_in(n->get_child(i), cls)) return true;
        return false;
    }
    // ¿Hay un efecto de Lighting que gobierne esta caracteristica del Environment?
    // Si lo hay, el Workspace NO debe forzarla: asi un BloomEffect/Atmosphere con
    // Enabled=false se queda apagado en vez de reactivarse cada vez que se entra a
    // la escena o al jugar. (glow/fog encendidos NO pueden dar gris, asi que aqui
    // si es seguro "respetar" el efecto — al reves del adjustment, que siempre off.)
    bool _gl_lighting_has(const char* cls) const {
        Node* dm = get_parent();
        if (!dm) return false;
        Node* lighting = dm->get_node_or_null(NodePath("Lighting"));
        return _gl_find_class_in(lighting ? lighting : dm, cls);
    }
    // ¿Existe el servicio Lighting? Si existe, ES EL quien manda sobre el ambiente
    // y la niebla (como en Roblox): el Workspace solo pone el look base cuando no
    // hay Lighting, para no pisar Ambient/OutdoorAmbient/Fog del juego.
    bool _gl_has_lighting() const {
        Node* dm = get_parent();
        if (!dm) return false;
        Node* l = dm->get_node_or_null(NodePath("Lighting"));
        return l && l->is_class("Lighting");
    }

    void _apply_roblox_render(const Ref<Environment>& env, DirectionalLight3D* sun) {
        if (env.is_valid()) {
            // 1) Ajuste de color: solo se apaga si NO hay un ColorCorrectionEffect
            //    vivo en el juego. El "antigris" de la 1.16.6 lo apagaba SIEMPRE, y
            //    como el adjustment del Environment es el UNICO canal por el que
            //    ColorCorrectionEffect puede actuar, el efecto quedaba muerto por
            //    diseño. La distincion correcta: un .tres podrido trae el ajuste
            //    guardado SIN ningun efecto que lo pida (se repara apagandolo);
            //    con un ColorCorrectionEffect presente, manda EL (y sus propios
            //    valores neutros por defecto ya no pueden dar gris).
            //    (Ahora el ajuste lleva SIEMPRE la curva de tonos de Roblox; ver
            //    el paso del tonemapper, mas abajo.)
            // 2) Fondo = cielo SIEMPRE. Un BG_COLOR/CANVAS dejaba pantalla plana.
            if (env->get_background() != Environment::BG_SKY)
                env->set_background(Environment::BG_SKY);
            Ref<Sky> sky = env->get_sky();
            if (!sky.is_valid() || !sky->get_material().is_valid())
                env->set_sky(_gl_make_roblox_sky());

            // Tonemapper: el "Default" de Roblox, MEDIDO con una carta de colores
            // en Play (2026-10-08): es la curva ACES con white 2 aplicada a cada
            // canal por separado, sin la matriz que mezcla colores del ACES de
            // Godot (con ella el verde puro salia palido). Se monta con Reinhard
            // + una LUT, junto con los ColorCorrectionEffect (gl_tono_roblox.h).
            gl_tono_reconstruir(this);
            // La exposicion la manda Lighting (2^ExposureCompensation); solo se
            // resetea aqui cuando no hay Lighting que la gobierne.
            if (!_gl_has_lighting())
                env->set_tonemap_exposure(1.0f);

            // Glow: solo se fuerza si NO hay Bloom/SunRays que lo controle (asi un
            // Bloom con Enabled=false no se reactiva).
            if (!_gl_lighting_has("BloomEffect") && !_gl_lighting_has("SunRaysNode")) {
                env->set_glow_enabled(true);
                env->set_glow_blend_mode(Environment::GLOW_BLEND_MODE_SCREEN);
                env->set_glow_intensity(0.5f);
                env->set_glow_strength(1.0f);
                env->set_glow_bloom(0.1f);
                env->set_glow_hdr_bleed_threshold(0.95f);
                env->set_glow_hdr_bleed_scale(2.0f);
                env->set_glow_level(2, 0.6f);
                env->set_glow_level(3, 0.4f);
                env->set_glow_level(4, 0.2f);
            }

            // Sin oclusion ambiental de pantalla: Roblox no la tiene y costaba
            // 1,5 ms de GPU (ver la tabla de gl_graphics.h). Los parametros se
            // dejan suaves por si el jugador la enciende en Custom: con 1.5/1.5
            // todo lo techado se iba a negro.
            env->set_ssao_enabled(false);
            env->set_ssao_radius(1.0f);
            env->set_ssao_intensity(0.5f);
            env->set_ssao_power(1.0f);

            // Ambiente: solo lo fija el Workspace si NO hay servicio Lighting.
            // Con Lighting presente manda el (Ambient/OutdoorAmbient de Roblox).
            if (!_gl_has_lighting()) {
                env->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
                // Misma razon que en Lighting::_apply_env: con 1.0 el cielo se
                // come el color ambiental y la escena sale lechosa y plana.
                env->set_ambient_light_sky_contribution(0.7f);
                env->set_ambient_light_energy(1.0f);
            }

            // Niebla por defecto: NINGUNA, como Roblox (FogEnd=100000 = sin
            // niebla; solo existe si el juego la pide via Lighting.FogStart/End o
            // un Atmosphere). Antes se forzaba aqui una "atmosfera sutil" que
            // difuminaba lo lejano en mapas donde Roblox no difumina nada.
            // https://create.roblox.com/docs/reference/engine/classes/Lighting#FogEnd
            if (!_gl_lighting_has("AtmosphereNode") && !_gl_has_lighting()) {
                env->set_fog_enabled(false);
                // densidad 0: aunque la curva de calidad reencienda el flag de
                // niebla, con densidad 0 sigue siendo invisible (no-gris seguro).
                env->set_fog_density(0.0f);
            }
        }
        if (sun) {
            sun->set_shadow(true);
            sun->set_param(Light3D::PARAM_ENERGY, 1.0f);
            // El resto de params de sombra (tamaño, bias, filtro) los fija
            // gl_apply_graphics_quality según el nivel — un solo sitio (1.15).
        }
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("build_local_character"),                &RobloxWorkspace::build_local_character);   // LoadCharacter (1.14.10)
        ClassDB::bind_method(D_METHOD("_gl_aplicar_ajustes"),                  &RobloxWorkspace::_gl_aplicar_ajustes);
        ClassDB::bind_method(D_METHOD("_gl_apply_quality_deferred"),           &RobloxWorkspace::_gl_apply_quality_deferred);
        ClassDB::bind_method(D_METHOD("_gl_tune_for_map_size"),                &RobloxWorkspace::_gl_tune_for_map_size);
        ClassDB::bind_method(D_METHOD("_gl_rebuild_chunks"),                   &RobloxWorkspace::_gl_rebuild_chunks);
        ClassDB::bind_method(D_METHOD("_gl_luz_propia_ab", "on"),              &RobloxWorkspace::_gl_luz_propia_ab);
        ClassDB::bind_method(D_METHOD("_gl_apagar_almacenes"),                 &RobloxWorkspace::_gl_apagar_almacenes);
        ClassDB::bind_method(D_METHOD("RebuildStaticChunks"),                  &RobloxWorkspace::_gl_rebuild_chunks);
        ClassDB::bind_method(D_METHOD("set_graphics_quality","level"),         &RobloxWorkspace::set_graphics_quality);
        ClassDB::bind_method(D_METHOD("get_graphics_quality"),                 &RobloxWorkspace::get_graphics_quality);
        ClassDB::bind_method(D_METHOD("SetGraphicsMode","mode"),               &RobloxWorkspace::set_graphics_mode);
        ClassDB::bind_method(D_METHOD("GetGraphicsMode"),                      &RobloxWorkspace::get_graphics_mode);
        ClassDB::bind_method(D_METHOD("SetAutoTargetFPS","fps"),               &RobloxWorkspace::set_auto_target_fps);
        ClassDB::bind_method(D_METHOD("GetAutoTargetFPS"),                     &RobloxWorkspace::get_auto_target_fps);
        ClassDB::bind_method(D_METHOD("SetGraphicsSetting","name","value"),    &RobloxWorkspace::set_custom_setting);
        ClassDB::bind_method(D_METHOD("SetViewDistance","level","studs"),      &RobloxWorkspace::SetViewDistance);
        ClassDB::bind_method(D_METHOD("GetViewDistance","level"),              &RobloxWorkspace::GetViewDistance);
        ClassDB::bind_method(D_METHOD("GetViewDistanceNow"),                   &RobloxWorkspace::GetViewDistanceNow);
        ClassDB::bind_method(D_METHOD("SetPhysicsLOD","name","value"),         &RobloxWorkspace::SetPhysicsLOD);
        ClassDB::bind_method(D_METHOD("GetPhysicsLOD","name"),                 &RobloxWorkspace::GetPhysicsLOD);
        ClassDB::bind_method(D_METHOD("SetAntiAliasing","mode"),               &RobloxWorkspace::SetAntiAliasing);
        ClassDB::bind_method(D_METHOD("GetAntiAliasing"),                      &RobloxWorkspace::GetAntiAliasing);
        ClassDB::bind_method(D_METHOD("GetGraphicsSetting","name"),            &RobloxWorkspace::get_custom_setting);
        ClassDB::bind_method(D_METHOD("SetVSync","on"),                        &RobloxWorkspace::set_vsync);
        ClassDB::bind_method(D_METHOD("GetVSync"),                             &RobloxWorkspace::get_vsync);
        // ── API que necesita el MENU EN LUAU (1.17) ──────────────────────
        // El menu de Esc pasa a ser un ModuleScript editable por el usuario, asi
        // que todo lo que antes tocaba el menu de C++ directamente tiene que estar
        // disponible desde Luau. Nombres en PascalCase, como el resto de la API.
        ClassDB::bind_method(D_METHOD("SetGraphicsQuality","level"),           &RobloxWorkspace::set_graphics_quality);
        ClassDB::bind_method(D_METHOD("GetGraphicsQuality"),                   &RobloxWorkspace::get_graphics_quality);
        ClassDB::bind_method(D_METHOD("SetMaxFPS","fps"),                      &RobloxWorkspace::gl_set_max_fps);
        ClassDB::bind_method(D_METHOD("GetMaxFPS"),                            &RobloxWorkspace::gl_get_max_fps);
        ClassDB::bind_method(D_METHOD("GetFPS"),                               &RobloxWorkspace::gl_get_fps);
        ClassDB::bind_method(D_METHOD("SetMasterVolume","pct"),                &RobloxWorkspace::gl_set_master_volume);
        ClassDB::bind_method(D_METHOD("GetMasterVolume"),                      &RobloxWorkspace::gl_get_master_volume);
        ClassDB::bind_method(D_METHOD("SetFullscreen","on"),                   &RobloxWorkspace::gl_set_fullscreen);
        ClassDB::bind_method(D_METHOD("GetFullscreen"),                        &RobloxWorkspace::gl_get_fullscreen);
        ClassDB::bind_method(D_METHOD("SetMenuOpen","on"),                     &RobloxWorkspace::gl_set_menu_open_lua);
        ClassDB::bind_method(D_METHOD("GetMenuOpen"),                          &RobloxWorkspace::gl_get_menu_open_lua);
        // Aparta el menu de ajustes de C++ para que manden los .luau del usuario.
        ClassDB::bind_method(D_METHOD("TakeScreenshot"),                       &RobloxWorkspace::gl_take_screenshot);
        ClassDB::bind_method(D_METHOD("ListScreenshots"),                      &RobloxWorkspace::gl_list_screenshots);
        ClassDB::bind_method(D_METHOD("DeleteScreenshot","path"),              &RobloxWorkspace::gl_delete_screenshot);
        ClassDB::bind_method(D_METHOD("GetEngineCredit"),                      &RobloxWorkspace::gl_engine_credit);
        ClassDB::bind_method(D_METHOD("GetEngineLink"),                        &RobloxWorkspace::gl_engine_link);
        ClassDB::bind_method(D_METHOD("OpenEngineLink"),                       &RobloxWorkspace::gl_open_engine_link);
        ClassDB::bind_method(D_METHOD("GetMemoryMB"),                          &RobloxWorkspace::gl_get_memory_mb);
        ClassDB::bind_method(D_METHOD("IsMouseDown"),                          &RobloxWorkspace::gl_is_mouse_down);
        ClassDB::bind_method(D_METHOD("ToggleChat"),                           &RobloxWorkspace::gl_toggle_chat);
        ClassDB::bind_method(D_METHOD("UseLuauMenu","on"),                     &RobloxWorkspace::gl_use_luau_menu);
        ClassDB::bind_method(D_METHOD("IsLuauMenu"),                           &RobloxWorkspace::gl_is_luau_menu);
        // Rehace las piezas del mundo que falten (cielo, sol, camara, terrain).
        // La llama el importador de .rbxl despues de llenar el Workspace.
        ClassDB::bind_method(D_METHOD("EnsureEnvironment"),                    &RobloxWorkspace::gl_ensure_environment);
        ClassDB::bind_method(D_METHOD("set_physics_fps","v"),                  &RobloxWorkspace::set_physics_fps);
        ClassDB::bind_method(D_METHOD("get_physics_fps"),                      &RobloxWorkspace::get_physics_fps);
        ADD_PROPERTY(PropertyInfo(Variant::INT,"PhysicsFPS",PROPERTY_HINT_RANGE,"0,240,1"),
            "set_physics_fps","get_physics_fps");
        ClassDB::bind_method(D_METHOD("set_gravity","g"),                      &RobloxWorkspace::set_gravity);
        ClassDB::bind_method(D_METHOD("get_gravity"),                          &RobloxWorkspace::get_gravity);
        ClassDB::bind_method(D_METHOD("set_fallen_parts_destroy_height","h"),  &RobloxWorkspace::set_fallen_parts_destroy_height);
        ClassDB::bind_method(D_METHOD("get_fallen_parts_destroy_height"),      &RobloxWorkspace::get_fallen_parts_destroy_height);
        ClassDB::bind_method(D_METHOD("set_auto_fall_height","b"),             &RobloxWorkspace::set_auto_fall_height);
        ClassDB::bind_method(D_METHOD("get_auto_fall_height"),                 &RobloxWorkspace::get_auto_fall_height);
        ClassDB::bind_method(D_METHOD("set_fall_height_margin","m"),           &RobloxWorkspace::set_fall_height_margin);
        ClassDB::bind_method(D_METHOD("get_fall_height_margin"),               &RobloxWorkspace::get_fall_height_margin);
        ClassDB::bind_method(D_METHOD("set_streaming_enabled","b"),            &RobloxWorkspace::set_streaming_enabled);
        ClassDB::bind_method(D_METHOD("get_streaming_enabled"),                &RobloxWorkspace::get_streaming_enabled);
        ClassDB::bind_method(D_METHOD("set_air_density","d"),                  &RobloxWorkspace::set_air_density);
        ClassDB::bind_method(D_METHOD("get_air_density"),                      &RobloxWorkspace::get_air_density);
        ClassDB::bind_method(D_METHOD("set_touches_use_collision_groups","b"), &RobloxWorkspace::set_touches_use_collision_groups);
        ClassDB::bind_method(D_METHOD("get_touches_use_collision_groups"),     &RobloxWorkspace::get_touches_use_collision_groups);

        ADD_GROUP("Mundo","");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Gravity",PROPERTY_HINT_RANGE,"0,1000,0.1"),
            "set_gravity","get_gravity");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"FallenPartsDestroyHeight",PROPERTY_HINT_RANGE,"-10000,0,1"),
            "set_fallen_parts_destroy_height","get_fallen_parts_destroy_height");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"AutoFallHeight"),
            "set_auto_fall_height","get_auto_fall_height");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"FallHeightMargin",PROPERTY_HINT_RANGE,"0,5000,1"),
            "set_fall_height_margin","get_fall_height_margin");
        ClassDB::bind_method(D_METHOD("set_stream_radius","r"), &RobloxWorkspace::set_stream_radius);
        ClassDB::bind_method(D_METHOD("get_stream_radius"),     &RobloxWorkspace::get_stream_radius);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"StreamingTargetRadius",PROPERTY_HINT_RANGE,"128,8192,16"),
            "set_stream_radius","get_stream_radius");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"StreamingEnabled"),
            "set_streaming_enabled","get_streaming_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"AirDensity",PROPERTY_HINT_RANGE,"0,10,0.01"),
            "set_air_density","get_air_density");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"TouchesUseCollisionGroups"),
            "set_touches_use_collision_groups","get_touches_use_collision_groups");
    }

    // Busca un hijo directo del tipo pedido. Se comprueba por TIPO y no por
    // "¿tiene hijos?" porque un Workspace que llega poblado (por ejemplo desde
    // el importador de .rbxl) tambien necesita cielo, sol y camara: con la
    // comprobacion antigua se quedaba sin Environment y la escena salia GRIS.
    template <class T>
    T* _gl_find_child_of_type() const {
        for (int i = 0; i < get_child_count(); i++)
            if (T* t = gl_cast<T>(get_child(i))) return t;
        return nullptr;
    }

    void _notification(int p_what) {
        // Tambien AL JUGAR, no solo en el editor. Estaba limitado a
        // is_editor_hint() y era la causa de que un place importado de .rbxl se
        // viera oscuro y sin cielo: al abrirlo en el juego nadie creaba el
        // WorldEnvironment, y sin WorldEnvironment el servicio Lighting no tiene
        // donde escribir NADA (su _find_world_env devuelve null), asi que se
        // perdian de golpe el ambiente, el cielo, el tonemapper y la niebla:
        // quedaba la luz direccional pelada sobre un fondo gris plano.
        // Es idempotente: cada pieza se crea solo si falta.
        if (p_what == NOTIFICATION_ENTER_TREE) {
            gl_ensure_environment();
            set_physics_process(true);   // tick central de constraints (ver abajo)
            // Clics sobre el mundo (carteles SurfaceGui y ClickDetector). Va en
            // unhandled: si el clic ya lo consumio un boton de la pantalla, en
            // Roblox tampoco llega a la escena.
            set_process_unhandled_input(true);
        }
        // Al cerrar, liberar lo que un script creo con Instance.new y nunca
        // colgo de nadie. Se hace AQUI porque el Workspace existe en cualquier
        // juego: la raiz de un place IMPORTADO es un Node pelado llamado "Game"
        // (no un RobloxTemplate), asi que engancharlo alli no cubria el caso y
        // salia "N ObjectDB instances were leaked at exit" en cada partida.
        else if (p_what == NOTIFICATION_EXIT_TREE
                 && !Engine::get_singleton()->is_editor_hint()) {
            gl_free_orphan_instances();
        }
    }

public:
    // Crea las piezas del mundo que falten (cielo, sol, baseplate, terrain,
    // camara). Es IDEMPOTENTE: cada pieza se crea solo si no hay ya una de su
    // tipo, asi que se puede llamar sobre un Workspace vacio o sobre uno recien
    // rellenado por el importador.
    void gl_ensure_environment() {
        // No exige estar dentro del arbol: el importador puede preparar el
        // Workspace antes de colgarlo. Solo el owner necesita la escena.
        Node* root = nullptr;
        if (is_inside_tree() && get_tree()) root = get_tree()->get_edited_scene_root();
        if (!root) root = this;

        WorldEnvironment*   we  = _gl_find_child_of_type<WorldEnvironment>();
        DirectionalLight3D*  sol = _gl_find_child_of_type<DirectionalLight3D>();

        // ── REPARAR el entorno existente SIEMPRE (antigris 1.16.6) ──────────
        // Antes esto solo corria si faltaba alguna pieza; una escena YA COMPLETA
        // (Workspace importado o game.tscn viejo) con el Environment gris salia
        // por el return de abajo sin tocarse y se quedaba gris. Ahora, si hay un
        // WorldEnvironment, se le aplica el look garantizado-no-gris antes de
        // cualquier atajo. Si su Environment vive en un .tres (el "editable"
        // rotado) y estaba gris, se reescribe reparado para que no vuelva nunca.
        if (we) {
            Ref<Environment> env = we->get_environment();
            if (!env.is_valid()) {
                env.instantiate();
                env->set_sky(_gl_make_roblox_sky());
                env->set_background(Environment::BG_SKY);
                we->set_environment(env);
            }
            Ref<Sky> sky_cur = env->get_sky();
            // Un adjustment encendido solo es sintoma de ".tres podrido" si NO hay
            // un ColorCorrectionEffect que lo este gobernando legitimamente.
            const bool was_gray = (env->is_adjustment_enabled() && !_gl_lighting_has("ColorCorrectionEffect"))
                               || env->get_background() != Environment::BG_SKY
                               || !sky_cur.is_valid()
                               || (env->is_fog_enabled() && env->get_fog_density() > 0.01f);
            env->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
            env->set_ambient_light_energy(1.15);
            _apply_roblox_render(env, sol);
            if (was_gray && Engine::get_singleton()->is_editor_hint()) {
                const String rp = env->get_path();
                if (!rp.is_empty())
                    ResourceSaver::get_singleton()->save(env, rp);
            }
        }

        const bool need_env  = (we  == nullptr);
        const bool need_sun  = (sol == nullptr);
        const bool need_cam  = _gl_find_child_of_type<Camera3D>() == nullptr;
        const bool need_terr = get_node_or_null(NodePath("Terrain")) == nullptr;
        // El Baseplate solo se crea en un Workspace VACIO: si ya hay geometria
        // (mapa importado) una placa de 1000x1000 studs estorbaria.
        const bool need_base = (get_child_count() == 0);

        if (!need_env && !need_sun && !need_cam && !need_terr && !need_base) return;

        // 2. SOL (se crea antes que el entorno para que el look se aplique a ambos)
        if (need_sun) {
            sol = memnew(DirectionalLight3D);
            sol->set_name("SunLight");
            sol->set_rotation_degrees(Vector3(-45, 45, 0));
            sol->set_shadow(true);
            add_child(sol);
            sol->set_owner(root);
        }

        // 1. CIELO AZUL ROBLOX + ENVIRONMENT — embebido en la escena (no un .tres
        // compartido que se corrompia y volvia todo gris). Editable inline en el
        // inspector del WorldEnvironment.
        if (need_env) {
            Ref<Environment> env; env.instantiate();
            env->set_sky(_gl_make_roblox_sky());
            env->set_background(Environment::BG_SKY);
            env->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
            env->set_ambient_light_energy(1.15);
            _apply_roblox_render(env, sol);

            WorldEnvironment* env_node = memnew(WorldEnvironment);
            env_node->set_name("WorldEnvironment");
            env_node->set_environment(env);
            add_child(env_node);
            env_node->set_owner(root);
        }

        // 3. BASEPLATE — una RobloxPart de verdad, COMO EN ROBLOX: seleccionas
        // "Baseplate" y cambias su propiedad Color en el inspector; la textura de
        // cuadricula (archivo editable en GodotLuau/assets/textures/) se tiñe con
        // ese color. Solo en un mundo vacio: sobre un mapa importado estorbaria.
        if (need_base) {
            RobloxPart* bp = memnew(RobloxPart);
            bp->set_name("Baseplate");
            add_child(bp);            // ENTER_TREE crea su material interno
            bp->set_owner(root);
            bp->set_size(Vector3(1000, 1, 1000));
            bp->set_anchored(true);
            bp->set_color(Color(0.42f, 0.42f, 0.45f));   // gris Studio (cambiable)
            ResourceLoader* rload = ResourceLoader::get_singleton();
            const String tex_path = "res://GodotLuau/assets/textures/baseplate_grid.png";
            Ref<Texture2D> grid_tex;
            if (rload && rload->exists(tex_path)) grid_tex = rload->load(tex_path);
            if (grid_tex.is_null()) grid_tex = generar_textura_grid();
            bp->gl_apply_grid_texture(grid_tex, 125.0f);   // celda menor = 2 studs
        }

        // 3b. TERRAIN — Workspace.Terrain (como Roblox): vacío al inicio, se
        // llena por código con Terrain:FillBlock/FillBall (p.ej. MundoVoxel).
        if (need_terr) {
            RobloxTerrain* terr = memnew(Terrain);
            terr->set_name("Terrain");
            add_child(terr);
            terr->set_owner(root);
        }

        // 4. CÁMARA ACTUAL — visible en el árbol como en Roblox.
        // workspace.CurrentCamera es la cámara que renderiza el juego: en runtime
        // la controla el jugador, en el editor ves su posición.
        if (need_cam) {
            Camera3D* current_cam = memnew(Camera3D);
            current_cam->set_name("CurrentCamera");
            current_cam->set_position(Vector3(0.0f, 5.0f, 10.0f));
            current_cam->set_rotation_degrees(Vector3(-15.0f, 0.0f, 0.0f));
            add_child(current_cam);
            current_cam->set_owner(root);
        }

        GL_DEBUG_PRINT("[GodotLuau] RobloxWorkspace: entorno asegurado en el editor.");
    }


    // ── Getters / Setters (properties match Roblox Workspace API) ─
    void  set_gravity(float g)                     { gravity = Math::max(g, 0.0f); _apply_gravity(); }
    float get_gravity() const                      { return gravity; }
    // Fijar la altura a mano = quiero un límite FIJO (apaga el auto), como pedir
    // explícitamente ese valor. Durante la carga de escena el nodo aún no está
    // "ready" y solo se restaura el valor guardado (sin intención del usuario),
    // así que ahí NO se toca el auto; tras ready, un setter sí es intención.
    void  set_fallen_parts_destroy_height(float h) {
        fallen_parts_destroy_height = h;
        _fall_effective = h;
        if (is_node_ready()) auto_fall_height = false;
    }
    float get_fallen_parts_destroy_height() const  { return fallen_parts_destroy_height; }
    void  set_auto_fall_height(bool b)   { auto_fall_height = b; }
    bool  get_auto_fall_height() const   { return auto_fall_height; }
    // Umbral VIGENTE (lo que ve un script): en auto es el calculado del mapa, no
    // el -500 guardado. El getter de arriba se queda con el valor guardado para
    // que la escena conserve el ajuste fijo del usuario.
    float get_effective_fall_height() const { return auto_fall_height ? _fall_effective : fallen_parts_destroy_height; }
    void  set_fall_height_margin(float m){ fall_height_margin = Math::max(m, 0.0f); }
    float get_fall_height_margin() const { return fall_height_margin; }
    void  set_streaming_enabled(bool b)            { streaming_enabled = b; }
    bool  get_streaming_enabled() const            { return streaming_enabled; }
    void  set_air_density(float d)                 { air_density = Math::max(d, 0.0f); }
    float get_air_density() const                  { return air_density; }
    void  set_touches_use_collision_groups(bool b) { touches_use_collision_groups = b; }
    bool  get_touches_use_collision_groups() const { return touches_use_collision_groups; }

    SpawnLocation* _find_spawn_r(Node* n) {
        if (!n) return nullptr;
        if (SpawnLocation* s = gl_cast<SpawnLocation>(n)) return s;
        for (int i = 0; i < n->get_child_count(); i++)
            if (SpawnLocation* f = _find_spawn_r(n->get_child(i))) return f;
        return nullptr;
    }

    void _collect_spawns_r(Node* n, std::vector<SpawnLocation*>& out) {
        if (!n) return;
        if (SpawnLocation* s = gl_cast<SpawnLocation>(n)) { out.push_back(s); return; }
        for (int i = 0; i < n->get_child_count(); i++) _collect_spawns_r(n->get_child(i), out);
    }

    // Elige el spawn del jugador según su equipo (1.15). Antes se cogía SIEMPRE
    // el primer SpawnLocation del árbol, así que en un juego por equipos todos
    // aparecían en el mismo sitio y había que recolocarlos a mano. Regla de
    // Roblox: un spawn con Neutral=true acepta a cualquiera; con Neutral=false
    // solo a quien tenga su TeamColor. Entre los válidos se elige al azar.
    SpawnLocation* _pick_spawn_for_local_player() {
        std::vector<SpawnLocation*> all;
        _collect_spawns_r(this, all);
        if (all.empty()) return nullptr;

        String team_color;
        if (Node* dm = get_parent()) {
            if (Node* players = dm->get_node_or_null("Players")) {
                for (int i = 0; i < players->get_child_count(); i++) {
                    Node* po = players->get_child(i);
                    if (!po || !po->is_class("PlayerObject")) continue;
                    if (!po->has_method("_gl_is_local") || !(bool)po->call("_gl_is_local")) continue;
                    team_color = String(po->call("_gl_team_color_name"));
                    break;
                }
            }
        }

        std::vector<SpawnLocation*> ok;
        for (SpawnLocation* s : all) {
            if (!s->get_enabled()) continue;
            if (s->get_neutral() || (!team_color.is_empty() && s->get_team_color() == team_color))
                ok.push_back(s);
        }
        // Sin spawn válido para su equipo: mejor aparecer en cualquiera que caer
        // al vacío en (0,5,0) sin explicación.
        std::vector<SpawnLocation*>& pool = ok.empty() ? all : ok;
        return pool[(int)(UtilityFunctions::randi() % pool.size())];
    }

    void _ready() override {
        if (Engine::get_singleton()->is_editor_hint()) return;

        _apply_gravity();
        _gl_aplicar_aa();   // FXAA de serie, como Roblox (ver SetAntiAliasing)
        // La sonda de luz (ui/gl_sonda.h: mide la luz de Godot para la luz propia
        // de las piezas y las sombras propias) y las sombras del sol propias
        // (ui/gl_sombras.h), que se encienden solas en las calidades que las
        // usan. Por nombre: esos headers van despues.
        if (Node* s = Object::cast_to<Node>(ClassDB::instantiate(StringName("GLSonda")))) {
            s->set_name("GLSonda");
            add_child(s, false, Node::INTERNAL_MODE_BACK);
        }
        if (Node* s = Object::cast_to<Node>(ClassDB::instantiate(StringName("GLSombras")))) {
            s->set_name("GLSombras");
            add_child(s, false, Node::INTERNAL_MODE_BACK);
        }
        // La pantalla de carga por defecto, como la de Roblox (ui/gl_carga_defecto.h).
        gl_carga_defecto_crear(this);

        // Reparar el entorno al jugar (antigris 1.16.6): aunque una escena vieja
        // tenga su Environment guardado gris (adjustment de saturacion baja, niebla
        // densa), _apply_roblox_render lo fuerza a un look correcto — jamas gris.
        // Ya no se recarga ningun .tres compartido (era la fuente del gris que
        // volvia una y otra vez).
        {
            WorldEnvironment* we = nullptr;
            DirectionalLight3D* sun = nullptr;
            for (int i = 0; i < get_child_count(); i++) {
                Node* c = get_child(i);
                if (!we)  we  = gl_cast<WorldEnvironment>(c);
                if (!sun) sun = gl_cast<DirectionalLight3D>(c);
            }
            Ref<Environment> env = we ? we->get_environment() : Ref<Environment>();
            if (!env.is_valid() && we) {
                env.instantiate();
                env->set_sky(_gl_make_roblox_sky());
                env->set_background(Environment::BG_SKY);
                we->set_environment(env);
            }
            if (env.is_valid() || sun) _apply_roblox_render(env, sun);
        }

        // ── Terrain: asegurar que exista (escenas viejas sin Terrain guardado) ──
        if (!get_node_or_null("Terrain")) {
            RobloxTerrain* terr = memnew(Terrain);
            terr->set_name("Terrain");
            call_deferred("add_child", terr);   // el padre está montando hijos en _ready
        }

        // ── RunService oculto: como en Roblox, existe pero no se ve en el editor ──
        if (get_parent() && !get_parent()->get_node_or_null("RunService")) {
            Node* rs = gl_cast<Node>(ClassDB::instantiate(StringName("RunService")));
            // add_child diferido: en _ready el padre (game) aun esta montando sus
            // hijos y Godot rechaza un add_child directo ("Parent node is busy").
            // El deferred corre antes que los scripts (iniciar_corrutina tambien
            // es diferido y se encola despues), asi RunService existe a tiempo.
            if (rs) { rs->set_name("RunService"); get_parent()->call_deferred("add_child", rs); }
        }

        // ── NetworkService: SOLO se auto-crea si el juego se lanzó desde la barra
        //    "Jugadores" del editor (multijugador local). Los juegos normales no
        //    reciben un servicio de red que no pidieron. ──
        if (gl_mp_autostart_requested() && get_parent() && !get_parent()->get_node_or_null("NetworkService")) {
            Node* ns = gl_cast<Node>(ClassDB::instantiate(StringName("NetworkService")));
            if (ns) { ns->set_name("NetworkService"); get_parent()->call_deferred("add_child", ns); }
        }

        // ── Ajustes del jugador ───────────────────────────────────────────
        // Se cargan y se aplican AQUI, sin depender de ninguna interfaz. Antes
        // esto pasaba porque se creaba el menu de Escape de C++ y era EL quien,
        // en su _ready, leia el .cfg: o sea que los ajustes guardados dependian
        // de que existiera un nodo de interfaz. El menu ahora es de Luau
        // (StarterPlayerScripts/Modules/Menu, editable), asi que los ajustes
        // viven por su cuenta en core/gl_ajustes.h.
        gl_ajustes_cargar();
        call_deferred("_gl_aplicar_ajustes");

        build_local_character();
        set_process(true);   // muerte por vacío (1.15)

        // ── Ritmo de física adaptativo (rendimiento) ─────────────────────
        // El coste del tick de física crece con el numero de piezas. En un mapa
        // grande bajar de 60 a 30 Hz da FPS sin que se note en el juego (el
        // movimiento se interpola). En mapas pequeños se mantiene 60 Hz para
        // conservar la sensacion exacta de Roblox.
        call_deferred("_gl_tune_for_map_size");
        call_deferred("_gl_rebuild_chunks");
        call_deferred("_gl_apagar_almacenes");   // los almacenes no simulan

        // Calidad gráfica (1.15): aplica sombras suaves REALES, bias anti-acne,
        // SSAO/glow y escala según el nivel guardado. Diferido para que el sol y
        // el WorldEnvironment ya estén en el árbol. Esto arregla las sombras feas.
        call_deferred("_gl_apply_quality_deferred");
    }

    // Ajustes que dependen del TAMAÑO del mapa. Va DIFERIDO: en _ready el arbol
    // puede no estar completo todavia y el conteo salia bajo, con lo que la
    // fisica se quedaba en 60 Hz (se perdia ~16% de FPS sin que se notara).
    void _gl_tune_for_map_size() {
        const int nparts = _gl_count_parts(this);
        if (physics_fps > 0) {
            Engine::get_singleton()->set_physics_ticks_per_second(physics_fps);
        } else {
            // 60 Hz SIEMPRE (es lo que hace Roblox: su Heartbeat va a 60).
            //
            // Aqui hubo un tiempo `nparts > 1500 ? 30 : 60`, porque bajar a 30 Hz
            // daba ~16% de FPS medios en mapas grandes. Ese trato ya no sale a
            // cuenta: con el resto arreglado (Vulkan, tope de recuperacion de
            // fisica, cache de propiedades) se midio el mismo place a las tres
            // frecuencias, un proceso por cada una:
            //
            //     Hz   FPS   p99      peor frame
            //     10   84,4  58,2 ms   169,0 ms
            //     30   83,8  30,2 ms    62,8 ms
            //     60   82,3  24,9 ms    31,3 ms
            //
            // O sea: 1,8% de FPS medios a cambio de PARTIR POR LA MITAD el peor
            // frame. Y tiene sentido — cuanto mas grande el paso, mas trabajo se
            // amontona en un solo frame y mas gordo el tiron. Lo que se nota
            // jugando es el peor frame, no la media.
            (void)nparts;
            Engine::get_singleton()->set_physics_ticks_per_second(60);
        }

        // ── Distancia de sombra adaptativa en mapas grandes ──────────────
        // Un rango de sombra amplio obliga a redibujar medio mundo en el shadow
        // map cada frame. (El SSAO no se toca aqui: lo gobierna Lighting segun
        // Technology, y ponerlo aqui solo creaba un conflicto — Lighting reaplica
        // despues y lo volvia a encender.)
        if (nparts > 1500) {
            for (int i = 0; i < get_child_count(); i++)
                if (DirectionalLight3D* dl = gl_cast<DirectionalLight3D>(get_child(i)))
                    dl->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, 120.0f);
        }

    }

    // ── RECORTE DE CASTERS LEJANOS ───────────────────────────────────────
    // Esto NO es una optimizacion: sin esto el mapa de sombras se rompe ENTERO.
    //
    // Godot ajusta el rango de profundidad de la sombra direccional para que
    // quepan TODOS los que proyectan. Un place de Roblox puede tener escenario a
    // kilometros (el de prueba tiene un camino con montañas a 23.000 unidades, y
    // NPC que se van hasta 37.600). Medido: la caja de los que proyectan mide
    // 44.818 unidades de diagonal, mientras que la zona jugable mide 3.519.
    //
    // Meter 44.000 unidades en la precision de un depth buffer deja resolucion
    // cero, y entonces TODO lee como "en sombra": media pantalla negra con un
    // borde recto que barre el mundo al mover la camara. Medido con el brillo
    // medio del suelo (0-255) y cuanta sombra hay respecto a apagarlas:
    //
    //     sin recorte ....... brillo  60,1   sombra 102,4   <- pantalla negra
    //     recorte a 3000 .... brillo 154,3   sombra   8,2   <- correcto
    //     recorte a 1500 .... brillo 154,3   sombra   8,2
    //
    // Y con el recorte puesto, el valor del "pancake" deja de importar (0, 4 y 20
    // dan lo mismo), que era por donde yo lo habia intentado antes sin acertar.
    //
    // Ojo: el fallo es INTERMITENTE. Los NPC caminan, asi que el que proyecta mas
    // lejos cambia de una partida a otra; por debajo de cierta distancia no rompe
    // nada y por encima sale el manchon. Por eso "a veces se ve bien".
    //
    // Se recalcula solo cuando el jugador se ha movido de verdad: recorrer el
    // arbol es lo caro, comparar distancias no.
    void  set_stream_radius(float r) {
        stream_radius = Math::max(128.0f, r);
        stream_radius_explicito = true;   // el place manda: pasa a ser un tope
    }
    float get_stream_radius() const  { return stream_radius; }

    // ── Distancia de vision por nivel de calidad ──────────────────────
    // workspace:SetViewDistance(8, 4096) / workspace:GetViewDistance(8).
    // Del 8 en adelante viene "infinita" (16.384 studs, ver gl_graphics.h).
    void  SetViewDistance(int level, float studs) { gl_view_studs_set(level, studs); }
    float GetViewDistance(int level) const        { return gl_view_studs(level); }
    float GetViewDistanceNow() const              { return gl_view_studs_actual(); }

    // ── Physics LOD ───────────────────────────────────────────────────
    // workspace:SetPhysicsLOD("NearRadius", 300). Nombres en gl_physlod.h.
    bool  SetPhysicsLOD(const String& name, float value) { return gl_physlod_set(name, value); }

    // ── Anti-aliasing (1.17) ──────────────────────────────────────────────
    // Roblox suaviza los bordes en las calidades altas: sin eso bailan con
    // dientes de sierra al mover la camara. El defecto es FXAA (desde la
    // calidad 8, ver gl_aplicar_aa); el juego puede elegir con
    // workspace:SetAntiAliasing(0..4): 0=Off 1=FXAA 2=MSAA2x 3=MSAA4x 4=MSAA8x.
    void SetAntiAliasing(int mode) {
        gl_aa_modo() = Math::clamp(mode, 0, 4);
        _gl_aplicar_aa();
    }
    int GetAntiAliasing() const { return gl_aa_modo(); }
    void _gl_aplicar_aa() { gl_aplicar_aa(get_viewport()); }
    float GetPhysicsLOD(const String& name) const {
        // Coste de rehacer los conjuntos: lo mas caro que hace el Workspace de
        // vez en cuando, y por tanto lo primero que hay que poder mirar.
        const GLAsmStats& s = gl_asm_stats();
        if (name == "AssemblyRebuildUs")    return (float)s.ultima_us;
        if (name == "AssemblyRebuildMaxUs") return (float)s.peor_us;
        if (name == "AssemblyRebuilds")     return (float)s.veces;
        if (name == "AsmF1Union")   return (float)s.f1_union;
        if (name == "AsmF2Grupos")  return (float)s.f2_grupos;
        if (name == "AsmF3Huellas") return (float)s.f3_huellas;
        if (name == "AsmF4Quitar")  return (float)s.f4_quitar;
        if (name == "AsmF5Montar")  return (float)s.f5_montar;
        if (name == "AsmJuntas")    return (float)s.n_juntas;
        if (name == "AsmGrupos")    return (float)s.n_grupos;
        if (name == "AsmIntactos")  return (float)s.n_intactos;
        if (name == "AsmRehechos")  return (float)s.n_rehechos;
        const GLPasoStats& p = gl_paso_stats();
        if (name == "PasoUs")        return (float)p.constraints_us;
        if (name == "PasoPeorUs")    return (float)p.peor_constraints_us;
        if (name == "PasoMediaUs")   return p.pasos ? (float)p.constraints_acum / (float)p.pasos : 0.0f;
        if (name == "PasoDespiertos")return (float)p.despiertos;
        if (name == "Pasos")         return (float)p.pasos;
        if (name == "ProcUs")        return (float)p.proc_us;
        if (name == "StreamUs")      return (float)p.stream_us;
        if (name == "CaidasUs")      return (float)p.caidas_us;
        if (name == "ConjUs")        return (float)p.conj_us;
        if (name == "CambiosDisparados") return (float)p.cambios_disparados;
        if (name == "DescLlamadas")  return (float)p.desc_llamadas;
        if (name == "DescDespachos") return (float)p.desc_despachos;
        if (name == "LuaKB")         return (float)gl_lua_memoria_kb();
        if (name == "LuaVMs")        return (float)gl_lua_vms();
        // Coste del codigo Luau DEL JUEGO por frame, por señal.
        if (name == "HeartbeatUs")     return (float)p.hb_us;
        if (name == "RenderSteppedUs") return (float)p.rs_us;
        if (name == "SteppedUs")       return (float)p.st_us;
        if (name == "HeartbeatN")      return (float)p.hb_n;
        if (name == "RenderSteppedN")  return (float)p.rs_n;
        if (name == "SteppedN")        return (float)p.st_n;
        if (name == "HoverUs")         return (float)p.hover_us;
        return gl_physlod_get(name);
    }

    // Conjuntos rigidos (assemblies) y streaming por distancia: los cuerpos
    // de estos metodos, con sus explicaciones, estan en roblox_workspace_mundo.h.
    uint64_t _uf_raiz(HashMap<uint64_t, uint64_t>& padre, uint64_t x);
    void _uf_unir(HashMap<uint64_t, uint64_t>& padre, uint64_t a, uint64_t b);

    // Huella de cada conjunto YA montado: id de la raiz -> mezcla de los ids de
    // sus miembros. Ver la nota de _reconstruir_conjuntos.
    HashMap<uint64_t, uint64_t> _huella_grupo;
    static uint64_t _huella_de(const LocalVector<RobloxPart*>& piezas);

    bool _resolver_grupo(const LocalVector<uint64_t>& ids, LocalVector<RobloxPart*>& piezas,
                         RobloxPart*& raiz, bool& alguna_anclada);

    void _reconstruir_conjuntos();

    // ── STREAMING POR DISTANCIA, repartido en frames ──────────────────
    // Sustituye al recorrido del arbol entero que habia antes (60.000 nodos de
    // golpe cada vez que la camara se movia 60 unidades: ~10 ms clavados justo
    // mientras el jugador anda). Ahora se repasa un TROZO de la lista plana por
    // frame, asi que el coste por frame es constante y no depende del mapa, y
    // las cosas aparecen y desaparecen poco a poco en vez de todas de golpe.
    //
    // Radios: StreamingTargetRadius manda en el dibujado (por defecto 1024,
    // como Roblox). Las particulas tienen el suyo, mucho mas corto: un emisor
    // mide poco y mas alla de ~220 no se distingue, y apagarlos fue el mayor
    // salto de FPS medido del place (42,8 -> 56,4).
    static const uint32_t GL_STREAM_POR_FRAME = 400;

    void _stream_tick();

    void gl_recortar_sombras_lejanas();
    void _recorrer_recortes(Node* n, const Vector3& ojo, float radio2, float radio_part2);

    // ── LOS ALMACENES NO SIMULAN (semantica de Roblox) ───────────────────
    // En Roblox, lo que esta en ReplicatedStorage / ServerStorage / StarterGui /
    // StarterPack son PLANTILLAS: no se dibujan ni se simulan hasta que algo las
    // clona dentro del Workspace. Aqui son nodos normales de Godot, asi que el
    // motor los procesaba a todos cada frame.
    //
    // Cuanto pesa eso, medido en el place de prueba: de los 2.341 emisores de
    // particulas, **2.296 (el 98%) estaban en los almacenes**:
    //     Workspace ............ 41
    //     ReplicatedStorage ... 1.986
    //     ServerStorage ....... 310
    // y las 2.296 se estaban simulando sin que nadie las viera. Apagar los
    // emisores fue el salto mas grande de todo el reparto del frame (+8,8 FPS).
    //
    // El arreglo es poner el MODO DE PROCESO del servicio, no el de cada hijo:
    // los hijos van en PROCESS_MODE_INHERIT y heredan del padre. Asi, cuando un
    // script clona una plantilla y la mete en el Workspace, el clon hereda del
    // Workspace y se activa solo, sin tener que tocarlo. Y como el clon conserva
    // su INHERIT, :Clone() sigue funcionando igual.
    //
    // NO se tocan ServerScriptService ni ReplicatedFirst: ahi los scripts SI
    // corren en Roblox.
    void _gl_apagar_almacenes() {
        Node* game = get_parent();
        if (!game) return;
        static const char* ALMACENES[] = {
            "ReplicatedStorage", "ServerStorage", "StarterGui", "StarterPack"
        };
        for (int i = 0; i < game->get_child_count(); i++) {
            Node* svc = game->get_child(i);
            const String nombre = svc->get_name();
            for (const char* a : ALMACENES) {
                if (nombre == String(a)) {
                    svc->set_process_mode(Node::PROCESS_MODE_DISABLED);
                    GL_DEBUG_PRINT("[GodotLuau] Almacen inerte: ", nombre,
                                   " (no simula, como en Roblox)");
                    break;
                }
            }
        }
    }

    // Solo para medir (A/B en el mismo frame congelado): la luz propia de las
    // piezas (gl_luz_propia.h) encendida o apagada en todas a la vez.
    void _gl_luz_propia_ab(bool on) {
        gl_luz_propia_forzada() = on ? -1 : 0;
        for (const KeyValue<uint64_t, bool>& kv : gl_partes_workspace())
            if (RobloxPart* p = Object::cast_to<RobloxPart>(ObjectDB::get_instance(ObjectID(kv.key))))
                p->gl_refrescar_material();
        _gl_rebuild_chunks();
    }

    // Agrupa la geometria estatica en zonas (ver gl_chunks.h). Se puede volver a
    // llamar en cualquier momento: primero deshace el agrupado anterior.
    void _gl_rebuild_chunks() {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (!gl_custom().static_batching) { gl_clear_static_chunks(this); return; }
        const int n = gl_build_static_chunks(this, gl_custom().chunk_size);
        GL_DEBUG_PRINT("[GodotLuau] Chunks: ", n, " piezas agrupadas.");
    }

    void _gl_aplicar_ajustes() { gl_ajustes_aplicar(this); }

    void _gl_apply_quality_deferred() {
        gl_apply_graphics_quality(gl_graphics_level(), this);
        // V-Sync al arrancar: Godot lo trae ACTIVADO por ajuste de proyecto y eso
        // clava los FPS en la tasa de refresco (o en su mitad). Aqui se fuerza el
        // valor que manda el jugador — el menu ya cargo el suyo antes que este
        // diferido, asi que esto respeta su eleccion y solo cubre el caso de un
        // juego sin menu de ajustes.
        gl_apply_vsync(gl_vsync_enabled(), this);
    }

    static int _gl_count_parts(Node* n) {
        int c = gl_cast<RigidBody3D>(n) ? 1 : 0;
        for (int i = 0; i < n->get_child_count(); i++) c += _gl_count_parts(n->get_child(i));
        return c;
    }
    // Lo que cuesta en el tick de fisica son las piezas DINAMICAS: un mapa con
    // 5.000 piezas ancladas y 10 sueltas no necesita bajar el ritmo. Antes se
    // miraba el total y se penalizaba a mapas grandes pero quietos.
    static int _gl_count_dynamic_parts(Node* n) {
        int c = 0;
        if (gl_cast<RigidBody3D>(n)) {
            Variant a = n->get("Anchored");
            if (a.get_type() == Variant::BOOL && !(bool)a) c = 1;
        }
        for (int i = 0; i < n->get_child_count(); i++) c += _gl_count_dynamic_parts(n->get_child(i));
        return c;
    }
    void set_physics_fps(int v) {
        physics_fps = Math::clamp(v, 0, 240);
        if (is_node_ready() && !Engine::get_singleton()->is_editor_hint() && physics_fps > 0)
            Engine::get_singleton()->set_physics_ticks_per_second(physics_fps);
    }
    int get_physics_fps() const { return physics_fps; }

    // API de calidad gráfica para Lua (1.15): el módulo de ajustes en Modules la
    // usa. workspace:SetGraphicsQuality(1..10) / workspace:GetGraphicsQuality().
    // Cada setter deja constancia en los ajustes y pide guardar. Antes NADIE
    // guardaba lo que el jugador tocaba desde el menu de Luau (solo escribia el
    // .cfg el menu viejo de C++), asi que al reabrir el juego estaba todo como
    // antes. El guardado va agrupado, no en cada movimiento de barra.
    void set_graphics_quality(int level) {
        gl_apply_graphics_quality(level, this);
        gl_ajustes().quality = CLAMP(level, 1, 10);
        gl_ajustes_cambiaron();
    }
    int  get_graphics_quality() const    { return gl_graphics_level(); }

    // ── Modos de calidad (Automatic / Manual / Custom) ───────────────────
    // 0 = Automatic (ajusta solo para sostener los FPS), 1 = Manual (nivel fijo),
    // 2 = Custom (cada ajuste por separado). La UI vive en el Module de Luau.
    // Acepta el numero (0/1/2) y tambien el NOMBRE ("Automatic"/"Manual"/
    // "Custom"), que es lo que sale natural escribiendo Luau. Con un int pelado,
    // Workspace:SetGraphicsMode("Custom") se convertia a 0 y dejaba el juego en
    // Automatic SIN decir nada: el ajuste parecia no existir.
    void set_graphics_mode(const Variant& modo) {
        int mode;
        if (modo.get_type() == Variant::STRING || modo.get_type() == Variant::STRING_NAME) {
            const String s = String(modo).to_lower();
            if      (s.begins_with("man")) mode = GL_QUALITY_MANUAL;
            else if (s.begins_with("cus")) mode = GL_QUALITY_CUSTOM;
            else                           mode = GL_QUALITY_AUTOMATIC;
        } else {
            mode = CLAMP((int)modo, 0, 2);
        }
        gl_quality_mode() = mode;
        gl_apply_graphics_quality(gl_graphics_level(), this);
        gl_ajustes().gfx_mode = mode;
        gl_ajustes_cambiaron();
    }
    int  get_graphics_mode() const { return gl_quality_mode(); }
    // Objetivo del modo Automatic. El techo se deja alto (480) porque en pantallas
    // de 144/240 Hz un objetivo de 240 es legitimo; el suelo son 15 FPS.
    void set_auto_target_fps(int fps) { gl_auto_target_fps() = CLAMP(fps, 15, 480); }
    int  get_auto_target_fps() const  { return gl_auto_target_fps(); }

    // ── V-Sync ───────────────────────────────────────────────────────────
    // Roblox no lo expone en su menu, pero aqui hace falta: Godot lo trae
    // ACTIVADO y eso ata los FPS a la tasa de refresco (ver gl_apply_vsync).
    void set_vsync(bool on) {
        gl_apply_vsync(on, this);
        gl_ajustes().vsync = on;
        gl_ajustes_cambiaron();
    }
    bool get_vsync() const  { return gl_vsync_enabled(); }

    // ── Soporte del MENU EN LUAU (1.17) ──────────────────────────────────
    // El menu de Esc era una clase de C++ (ya borrada) y por tanto NO se
    // podia tocar: el usuario no podia anadir filas, quitar opciones ni
    // rediseñarlo. Ahora el menu vive en ModuleScripts .luau editables y esto es
    // lo que necesita para hacer su trabajo desde Luau.
    void  gl_set_max_fps(int fps)    {
        Engine::get_singleton()->set_max_fps(fps < 0 ? 0 : fps);
        gl_ajustes().fps_idx = gl_fps_indice_de(fps < 0 ? 0 : fps);
        gl_ajustes_cambiaron();
    }
    int   gl_get_max_fps() const     { return Engine::get_singleton()->get_max_fps(); }
    double gl_get_fps() const        { return Engine::get_singleton()->get_frames_per_second(); }
    void  gl_set_master_volume(double pct) {
        gl_ajustes().volume = CLAMP(pct, 0.0, 100.0);
        gl_ajustes_cambiaron();
        if (AudioServer* as = AudioServer::get_singleton()) {
            as->set_bus_mute(0, pct <= 0.5);
            as->set_bus_volume_db(0, (float)UtilityFunctions::linear_to_db(Math::max(pct, 0.5) / 100.0));
        }
    }
    double gl_get_master_volume() const {
        AudioServer* as = AudioServer::get_singleton();
        if (!as || as->is_bus_mute(0)) return 0.0;
        return Math::clamp(UtilityFunctions::db_to_linear(as->get_bus_volume_db(0)) * 100.0, 0.0, 100.0);
    }
    void gl_set_fullscreen(bool on) {
        if (is_inside_tree() && get_tree() && get_tree()->get_root())
            get_tree()->get_root()->set_mode(on ? Window::MODE_FULLSCREEN : Window::MODE_WINDOWED);
    }
    bool gl_get_fullscreen() const {
        if (!is_inside_tree() || !get_tree() || !get_tree()->get_root()) return false;
        return get_tree()->get_root()->get_mode() == Window::MODE_FULLSCREEN;
    }
    // Congela al jugador y hace que la rueda desplace el menu en vez de dar zoom
    // (lo mismo que hacia el menu de C++ al abrirse).
    void gl_set_menu_open_lua(bool on) { gl_set_menu_open(on); }
    bool gl_get_menu_open_lua() const  { return gl_menu_open(); }

    // El menu .luau se declara dueno del Esc y el de C++ se aparta. Se puede
    // volver a false para recuperar el de fabrica.
    // ── Capturas de pantalla (pestana Galeria del menu) ──────────────────
    // Se guardan en user://screenshots/, que en Windows vive en
    // %APPDATA%/Godot/app_userdata/<juego>/. Devuelve la ruta de la nueva.
    String gl_take_screenshot() {
        if (!is_inside_tree()) return String();
        Viewport* vp = get_viewport();
        if (!vp) return String();
        Ref<ViewportTexture> tex = vp->get_texture();
        if (tex.is_null()) return String();
        Ref<Image> img = tex->get_image();
        if (img.is_null() || img->is_empty()) return String();

        Ref<DirAccess> d = DirAccess::open("user://");
        if (d.is_valid() && !d->dir_exists("screenshots")) d->make_dir("screenshots");

        // Nombre correlativo: se busca el primer hueco libre. Sin fecha a proposito,
        // que Time no esta disponible en todos los contextos del juego.
        String path;
        for (int i = 1; i < 10000; i++) {
            path = String("user://screenshots/shot_") + String::num_int64(i) + ".png";
            if (!FileAccess::file_exists(path)) break;
        }
        return img->save_png(path) == OK ? path : String();
    }
    PackedStringArray gl_list_screenshots() const {
        PackedStringArray out;
        Ref<DirAccess> d = DirAccess::open("user://screenshots");
        if (d.is_null()) return out;
        d->list_dir_begin();
        String f = d->get_next();
        while (!f.is_empty()) {
            if (!d->current_is_dir() && f.to_lower().ends_with(".png"))
                out.push_back(String("user://screenshots/") + f);
            f = d->get_next();
        }
        d->list_dir_end();
        out.sort();
        return out;
    }

    // Borra UNA captura. Guardado a prueba de abusos: solo dentro de
    // user://screenshots/ y sin subir directorios.
    bool gl_delete_screenshot(String path) {
        if (!path.begins_with("user://screenshots/") || path.contains("..")) return false;
        Ref<DirAccess> d = DirAccess::open("user://screenshots");
        if (d.is_null()) return false;
        return d->remove(path.get_file()) == OK;
    }
    // Marca de agua del motor (pestaña Ayuda). Vive en C++ a proposito: cambiarla
    // exige recompilar la DLL, no basta con editar un .luau.
    String gl_engine_credit() const { return String("GodotLuau // Developed by PimpoliDev"); }
    String gl_engine_link() const   { return String("https://github.com/Pimpoli/GodotLuau"); }
    void gl_open_engine_link()      { OS::get_singleton()->shell_open(gl_engine_link()); }
    // ¿Boton izquierdo pulsado AHORA? Lo usa el arrastre de los sliders del
    // menu: en vez de fiarse de que llegue el evento de soltar (que puede
    // perderse si se suelta fuera de la ventana), se pregunta el estado real.
    bool gl_is_mouse_down() const {
        return Input::get_singleton()->is_mouse_button_pressed(MOUSE_BUTTON_LEFT);
    }
    double gl_get_memory_mb() const {
        return (double)OS::get_singleton()->get_static_memory_usage() / (1024.0 * 1024.0);
    }
    // Alterna el chat (boton de la topbar del menu .luau). Devuelve el estado nuevo.
    bool gl_toggle_chat() {
        Node* raiz = is_inside_tree() ? (Node*)get_tree()->get_root() : nullptr;
        Node* chat = raiz ? gl_find_by_class(raiz, "RobloxChat") : nullptr;
        if (!chat) return false;
        return (bool)chat->call("gl_toggle_chat");
    }

    void gl_use_luau_menu(bool on) {
        // Reusa el flag que ya existia para esto (gl_runtime.h). Ojo al sentido:
        // el flag dice si manda el menu NATIVO, asi que se invierte.
        gl_set_native_menu_enabled(!on);
        // Al de C++ se le avisa para que se oculte YA (o vuelva a mostrarse).
        if (get_parent()) {
            if (Node* sm = get_parent()->get_node_or_null(NodePath("SettingsMenu")))
                if (sm->has_method("gl_set_superseded")) sm->call("gl_set_superseded", on);
        }
    }
    bool gl_is_luau_menu() const { return !gl_native_menu_enabled(); }

    // Ajustes del modo Custom (cada uno independiente). El mapeo nombre→campo y
    // los limites estan en gl_custom_set (gl_graphics.h), que tambien usa el menu
    // de ajustes; aqui solo quedan los efectos que necesitan el nodo del mundo.
    void set_custom_setting(String name, float value) {
        if (!gl_custom_set(name, value)) return;   // nombre desconocido: no tocar nada
        gl_ajustes_cambiaron();
        // Occlusion culling: cambia desde que tamaño una pieza tapa, asi que hay
        // que recalcular cuales son occluders (mas occluders = descarta mejor pero
        // cuesta mas CPU; ese es el equilibrio del culling por software).
        if (name == "Occlusion" || name == "OcclusionSize") _gl_refresh_occluders(this);
        // Transparencia por borrado de pixeles: hay que rehacer los materiales
        // (cambia el modo de transparencia de cada pieza translucida).
        else if (name == "PixelTransparency") _gl_refresh_transparency(this);
        else if (name == "StaticBatching" || name == "ChunkSize" || name == "MergeZonePhysics")
            _gl_rebuild_chunks();
        gl_apply_graphics_quality(gl_graphics_level(), this);
    }
    float get_custom_setting(String name) const { return gl_custom_get(name); }

    // Rehace los occluders de todas las piezas (al cambiar los ajustes de
    // occlusion hay que recalcular cuales tapan y cuales ya no).
    // Rehace el material de las piezas translucidas al cambiar el modo de
    // transparencia (las opacas no se tocan: no cambian).
    static void _gl_refresh_transparency(Node* n) {
        if (n->is_class("RobloxPart")) {
            Variant t = n->get("Transparency");
            if (t.get_type() == Variant::FLOAT && (float)t > 0.001f)
                n->set("Transparency", (float)t);   // el setter rehace el material
        }
        for (int i = 0; i < n->get_child_count(); i++) _gl_refresh_transparency(n->get_child(i));
    }

    static void _gl_refresh_occluders(Node* n) {
        if (n->is_class("RobloxPart") && n->has_method("_gl_apply_occluder"))
            n->call("_gl_apply_occluder");
        for (int i = 0; i < n->get_child_count(); i++) _gl_refresh_occluders(n->get_child(i));
    }

    // ── Muerte por vacío (1.15) ──────────────────────────────────────────
    void _process(double delta) override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        // Cronometro siempre puesto y baratisimo (dos lecturas de reloj): sin el
        // no se puede saber si un frame largo es trabajo nuestro o del motor.
        Time* _T = Time::get_singleton();
        const uint64_t _t_proc = _T->get_ticks_usec();

        // Autopsia del frame largo (solo con GL_AUTOPSIA=1).
        gl_aut_frame();
        GL_AUT_SCOPE(AUT_P_WORKSPACE);

        gl_sonidos_sueltos_drenar(this);

        // ── LAS DOS COLAS CON PRESUPUESTO ────────────────────────────
        // Una oleada del juego crea ~2.000 nodos de golpe y el frame siguiente
        // se va en 99-117 ms FUERA de _process (el vaciado de las llamadas
        // diferidas de Godot). Se probo a repartir tambien ese trabajo de UI en
        // una cola propia (ui/gl_cola_ui.h) y NO cambia nada: A/B alternado de
        // dos rondas, 36/35 tirones sin cola contra 35/36 con ella. Ese fichero
        // se queda con la medida escrita; aqui solo drena la de montajes.
        //
        // OJO: gl_drenar_montajes() estaba definida y NO LA LLAMABA NADIE, asi
        // que la cola de montaje era codigo muerto y todo el montaje se pagaba
        // en linea. Aqui empieza a servir para algo.
        gl_drenar_montajes();
        // Y la de formas exactas de las mallas (gl_malla_propia.h): la carga de
        // un mapa con cientos de MeshParts ya no se paga entera en un frame.
        gl_drenar_formas();

        // Modo Automatic: ajusta la calidad sola para sostener los FPS objetivo
        // (no hace nada en Manual/Custom).
        gl_auto_quality_tick(delta, this);
        // Escribe el .cfg si algo cambio (agrupado, no en cada movimiento de barra)
        gl_ajustes_tick(delta);

        // Recorte de sombras y particulas lejanas. El umbral de movimiento es
        // 60 unidades porque las particulas se recortan a 220: con un umbral
        // grande se notaria el salto al acercarse. Recorrer el arbol es lo caro;
        // comparar distancias no.
        {
            GLAutTiempo _aut_sombras(AUT_SOMBRAS);
            const uint64_t _t = _T->get_ticks_usec();
            _stream_tick();
            gl_paso_stats().stream_us = _T->get_ticks_usec() - _t;
        }

        // Recalcular el umbral cada ~1s (no por frame: escanear el mapa entero
        // sale caro y el suelo casi nunca cambia).
        _fall_scan_timer -= delta;
        if (_fall_scan_timer <= 0.0) {
            _fall_scan_timer = 1.0;
            if (auto_fall_height) {
                // El suelo se calcula UNA vez y se reutiliza: recorrer todo el
                // arbol cada segundo costaba caro y el punto mas bajo del mapa no
                // cambia (salvo que se construya por debajo, caso raro; se
                // recalcula al respawnear con _gl_invalidate_ground).
                if (_ground_cached) {
                    _fall_effective = _ground_value;
                } else {
                bool found = false;
                float lowest = 0.0f;
                _scan_lowest_anchored(this, lowest, found);
                // Sin geometría anclada (mapa solo de terreno o dinámico): mantener
                // el fijo por defecto, para no matar a todos en +infinito.
                _fall_effective = found ? (lowest - fall_height_margin) : fallen_parts_destroy_height;
                _ground_value = _fall_effective;
                _ground_cached = true;
                }
            } else {
                _fall_effective = fallen_parts_destroy_height;
            }
        }

        // Matar al personaje LOCAL si su HRP cae por debajo. Los muñecos remotos
        // no se tocan aquí (los mata su propia máquina). El auto-respawn (F1.2)
        // lo revive tras RespawnTime.
        for (int i = 0; i < get_child_count(); i++) {
            Node* ch = get_child(i);
            if (!ch || !(ch->is_class("RobloxPlayer") || ch->is_class("RobloxPlayer2D"))) continue;
            Node3D* ref = gl_cast<Node3D>(ch->get_node_or_null(NodePath("HumanoidRootPart")));
            Node3D* body = ref ? ref : gl_cast<Node3D>(ch);
            if (!body) continue;
            if (body->get_global_position().y >= _fall_effective) continue;
            Node* hum = ch->get_node_or_null(NodePath("Humanoid"));
            if (hum && hum->has_method("get_health") && (double)hum->call("get_health") > 0.0)
                hum->call("set_health", 0.0);
        }

        // Destruir Parts sueltas (no ancladas, fuera de un personaje) que caen por
        // debajo — es lo que dice el nombre FallenPartsDestroyHeight en Roblox.
        // THROTTLE (rendimiento): recorrer TODO el arbol cada frame costaba carisimo
        // en mapas grandes (38K nodos -> un vector por nodo por frame). Cae por
        // debajo del suelo no necesita precision de frame: basta cada ~0.3s.
        {
            GLAutTiempo _aut_caidas(AUT_CAIDAS);
            const uint64_t _t = _T->get_ticks_usec();
            _destruir_caidas();
            gl_paso_stats().caidas_us = _T->get_ticks_usec() - _t;
        }

        // Conjuntos: solo si algo los ensucio, y como mucho una vez cada 0,2 s
        // para que una tanda de piezas no dispare varias reconstrucciones.
        if (gl_ensamblajes_sucios()) {
            _conjuntos_timer -= delta;
            if (_conjuntos_timer <= 0.0) {
                _conjuntos_timer = 0.2;
                gl_ensamblajes_sucios() = false;
                gl_anclaje_sucio() = false;   // la reconstruccion ya lo resuelve
                const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
                _reconstruir_conjuntos();
                GLAsmStats& st = gl_asm_stats();
                st.ultima_us = Time::get_singleton()->get_ticks_usec() - t0;
                if (st.ultima_us > st.peor_us) st.peor_us = st.ultima_us;
                st.veces++;
            }
        } else if (gl_anclaje_sucio()) {
            // Repaso BARATO: los conjuntos son los mismos, solo hay que mirar si
            // alguno tiene ahora una pieza anclada (o ya no la tiene). No se
            // toca ni una forma de colision, que es lo que costaba.
            gl_anclaje_sucio() = false;
            _repasar_anclaje_conjuntos();
        }
        GLPasoStats& ps = gl_paso_stats();
        ps.proc_us = _T->get_ticks_usec() - _t_proc;
        ps.conj_us = gl_asm_stats().ultima_us;
    }

    // Recorre los conjuntos ya formados y pone la raiz congelada o suelta segun
    // si algun miembro esta anclado. Es lo unico que cambia cuando un script
    // escribe Anchored: ni el agrupado ni la masa ni la forma se mueven.
    void _repasar_anclaje_conjuntos() {
        for (const KeyValue<uint64_t, LocalVector<uint64_t>>& kv : gl_miembros_de_raiz()) {
            RobloxPart* raiz = gl_cast<RobloxPart>(
                ObjectDB::get_instance(ObjectID(kv.key)));
            if (!raiz) continue;
            bool alguna = false;
            for (uint32_t i = 0; i < kv.value.size() && !alguna; i++)
                if (RobloxPart* m = gl_cast<RobloxPart>(
                        ObjectDB::get_instance(ObjectID(kv.value[i]))))
                    alguna = m->get_anchored();
            if (raiz->is_freeze_enabled() != alguna) {
                if (raiz->get_freeze_mode() != RigidBody3D::FREEZE_MODE_STATIC)
                    raiz->set_freeze_mode(RigidBody3D::FREEZE_MODE_STATIC);
                raiz->set_freeze_enabled(alguna);
            }
        }
    }

private:
    void _scan_lowest_anchored(Node* n, float& lowest, bool& found) {
        if (!n) return;
        if (n->is_class("RobloxPart")) {
            Variant anc = n->get("Anchored");
            if (anc.get_type() == Variant::BOOL && (bool)anc) {
                if (Node3D* p = gl_cast<Node3D>(n)) {
                    float y = p->get_global_position().y;
                    if (!found || y < lowest) { lowest = y; found = true; }
                }
            }
        }
        for (int i = 0; i < n->get_child_count(); i++) _scan_lowest_anchored(n->get_child(i), lowest, found);
    }

    // ¿este nodo está dentro de un personaje? (tiene un Humanoid por ancestro)
    static bool _under_character(Node* n) {
        for (Node* a = n ? n->get_parent() : nullptr; a; a = a->get_parent())
            if (a->get_node_or_null(NodePath("Humanoid"))) return true;
        return false;
    }
    // FallenPartsDestroyHeight. Antes recorria TODO el arbol cada 0,3 s (ver el
    // comentario de gl_piezas_sueltas en roblox_part.h: 60.000 nodos, un
    // std::vector por nodo, 65-110 ms de tiron tres veces por segundo). Solo se
    // puede caer lo que no esta anclado, asi que se recorre ese registro y ya.
    // REPARTIDO POR FRAMES. Antes esto barria TODAS las piezas sueltas de golpe
    // cada 0,3 s. Medido: ese frame salia a 15-23 ms con la mediana en 10, y se
    // repetia cada 0,30 / 0,60 / 0,90 s como un reloj. El barrido en si son
    // 0,7 ms; lo caro es lo que ARRASTRA — las piezas que se liberan, cuya
    // destruccion Godot hace al final del frame, toda junta.
    //
    // Ahora se mira un TROZO por frame y se limita cuantas se liberan en cada
    // uno. El trabajo por frame es constante y pequeño, y las liberaciones se
    // reparten en vez de amontonarse. Con 400 por frame a 90 FPS se repasa la
    // lista entera unas 20 veces por segundo: de sobra para algo que solo tiene
    // que notar que una pieza se ha caido del mapa.
    static const uint32_t GL_CAIDAS_POR_FRAME = 400;
    static const uint32_t GL_LIBERAR_POR_FRAME = 8;

    void _destruir_caidas() {
        LocalVector<uint64_t>& lista = gl_lista_sueltas();
        if (lista.is_empty()) return;
        uint32_t miradas = GL_CAIDAS_POR_FRAME;
        uint32_t liberadas = 0;
        while (miradas-- > 0 && !lista.is_empty()) {
            if (_caidas_cursor >= lista.size()) _caidas_cursor = 0;
            const uint64_t id = lista[_caidas_cursor];
            RobloxPart* p = gl_cast<RobloxPart>(ObjectDB::get_instance(ObjectID(id)));
            bool fuera = (!p || !p->is_inside_tree() || !gl_piezas_sueltas().has(id));
            if (!fuera && p->get_global_position().y < _fall_effective) {
                // Solo dentro del Workspace (una pieza guardada en
                // ReplicatedStorage no se cae), y nunca un trozo de personaje.
                // Las dos comprobaciones suben por los ancestros, pero solo las
                // paga lo que ya esta por debajo del suelo, que es una rareza.
                if (is_ancestor_of(p) && !_under_character(p)) {
                    p->queue_free();
                    fuera = true;
                    if (++liberadas >= GL_LIBERAR_POR_FRAME) {
                        // Tope de destrucciones por frame: el resto en el
                        // siguiente. Es lo que evita el paron.
                        gl_suelta_baja(id);
                        lista[_caidas_cursor] = lista[lista.size() - 1];
                        lista.resize(lista.size() - 1);
                        return;
                    }
                }
            }
            if (fuera) {
                gl_suelta_baja(id);
                lista[_caidas_cursor] = lista[lista.size() - 1];
                lista.resize(lista.size() - 1);
                continue;               // no se avanza: hay una nueva aqui
            }
            _caidas_cursor++;
        }
    }

public:
    // UN SOLO paso de fisica para TODOS los constraints del juego. Antes cada
    // WeldConstraint tenia el suyo propio: con 2332 en el place de prueba eran
    // ~140.000 cruces de frontera C++/Godot por segundo, y apagarlos daba +35%
    // de FPS. Ver el tick central en roblox_constraints.h.
    void _physics_process(double delta) override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        // Cuanto cuesta NUESTRO trabajo del paso, para poder separarlo del
        // trabajo del solver. Sin esto solo se sabe que el frame largo cae en un
        // frame con paso de fisica, que no dice de quien es la culpa.
        Time* T = Time::get_singleton();
        const uint64_t t0 = T->get_ticks_usec();
        gl_constraints_tick(delta);
        const uint64_t us = T->get_ticks_usec() - t0;
        GLPasoStats& p = gl_paso_stats();
        p.pasos++;
        p.constraints_us = us;
        p.constraints_acum += us;
        if (us > p.peor_constraints_us) p.peor_constraints_us = us;
        p.despiertos = gl_constraints_despiertos();
    }

    // ── Clic sobre el mundo 3D ──────────────────────────────────────────
    // Un SurfaceGui pegado a una pieza se dibujaba pero no se podia pulsar, y
    // ClickDetector::fire_click no lo llamaba NADIE. Aqui se tira el rayo desde
    // la camara por donde esta el raton y se reparte el clic.
    void _unhandled_input(const Ref<InputEvent>& p_event) override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        Ref<InputEventMouseButton> mb = p_event;
        if (mb.is_null() || !mb->is_pressed()) return;
        if ((int)mb->get_button_index() != MOUSE_BUTTON_LEFT) return;
        if (!is_inside_tree()) return;

        Viewport* vp = get_viewport();
        if (!vp) return;
        Camera3D* cam = vp->get_camera_3d();
        if (!cam || !cam->is_inside_tree()) return;

        // Con el cursor atrapado (primera persona / shift-lock) Roblox apunta al
        // centro de la pantalla, no a la ultima posicion conocida del raton.
        Vector2 raton = mb->get_position();
        if (Input::get_singleton()->get_mouse_mode() == Input::MOUSE_MODE_CAPTURED)
            raton = vp->get_visible_rect().size * 0.5f;

        Ref<World3D> w3 = cam->get_world_3d();
        if (w3.is_null()) return;
        PhysicsDirectSpaceState3D* ss = w3->get_direct_space_state();
        if (!ss) return;

        const Vector3 desde = cam->project_ray_origin(raton);
        const Vector3 dir   = cam->project_ray_normal(raton);
        Ref<PhysicsRayQueryParameters3D> q;
        q.instantiate();
        q->set_from(desde);
        q->set_to(desde + dir * 1000.0f);   // el alcance del raton de Roblox
        Dictionary hit = gl_raycast_roblox(ss, q);   // CanQuery de Roblox
        if (hit.is_empty()) return;

        Node* golpeado = gl_cast<Node>(hit.get("collider", Variant()));
        if (!golpeado) return;
        gl_repartir_clic_mundo(golpeado, (Vector3)hit.get("position", Variant(Vector3())),
                               gl_jugador_local(this));
    }

    // El Player local, para pasarlo como primer argumento de MouseClick igual
    // que Roblox. Nulo si todavia no existe.
    static Node* gl_jugador_local(Node* desde) {
        if (!desde || !desde->is_inside_tree()) return nullptr;
        Node* padre = desde->get_parent();
        if (!padre) return nullptr;
        Node* players = padre->get_node_or_null(NodePath("Players"));
        if (!players) return nullptr;
        for (int i = 0; i < players->get_child_count(); i++) {
            Node* p = players->get_child(i);
            if (p && p->is_class("PlayerObject")) return p;
        }
        return players->get_child_count() > 0 ? players->get_child(0) : nullptr;
    }

    // Construye el personaje local (spawn + rig + Humanoid + scripts). Extraído
    // de _ready para que Player:LoadCharacter() pueda rehacerlo (1.14.10).
    void build_local_character() {
        // ── 1. Find StarterPlayer (sibling of Workspace in the scene) ──────────
        //// ── 1. Buscar StarterPlayer (hermano del Workspace en la escena) ────────
        Node* starter_player = nullptr;
        if (get_parent()) {
            starter_player = get_parent()->get_node_or_null("StarterPlayer");
        }

        // ── 1.5. Un jugador, UN personaje ───────────────────────────────────
        // Si ya cuelga un "LocalPlayer" del Workspace hay que quitarlo ANTES de
        // crear el nuevo. Sin esto Godot no se queja: renombra el segundo a
        // @Character@N y el juego se queda con DOS personajes. El que controlas
        // es uno y los scripts del place (StarterPlayerScripts) se clonan en el
        // otro, asi que el sintoma no se parece nada a la causa — sale
        // "aparezco muerto", "los scripts no hacen nada", "los personajes salen
        // donde no es" y WaitForChild colgado para siempre esperando hijos que
        // estan en el gemelo.
        // Pasa si la escena guardada trae un personaje dentro o si alguien llama
        // dos veces. Solo por NOMBRE EXACTO: en multijugador los personajes de
        // los demas tambien cuelgan del Workspace y no se pueden tocar.
        for (int i = get_child_count() - 1; i >= 0; i--) {
            Node* viejo = get_child(i);
            if (!viejo || viejo->get_name() != StringName("LocalPlayer")) continue;
            remove_child(viejo);
            viejo->queue_free();
        }

        // ── 2. Create the player character ──────────────────────────────────────
        //// ── 2. Crear el personaje del jugador ────────────────────────────────────
        RobloxPlayer* p = memnew(Character);
        p->set_name("LocalPlayer");
        // Spawnear en un SpawnLocation acorde a su equipo (como Roblox)
        Vector3 spawn_pos(0, 5, 0);
        if (SpawnLocation* sl = _pick_spawn_for_local_player()) spawn_pos = sl->get_spawn_position();
        p->set_position(spawn_pos);
        add_child(p);

        // ── 3. Personaje por defecto (como Roblox) ──────────────────────
        // Prioridad: StarterPlayer/StarterCharacter (clonado, como Roblox) →
        // avatar R6 por partes con animaciones clasicas → capsula gris.
        // La FISICA siempre es la capsula.
        {
            Node3D* character = gl_build_character(this);
            if (character) {
                // El tipo de avatar (R6/R15) pasa al personaje: el nodo del rig
                // desaparece al aplanar.
                if (character->has_meta("_gl_rig")) p->set_meta("_gl_rig", character->get_meta("_gl_rig"));
                p->add_child(character);
                // Aplanar el rig R6 propio: partes + HumanoidRootPart pasan a ser
                // hijos DIRECTOS del personaje (como el Model plano de Roblox), así
                // Character:FindFirstChild("HumanoidRootPart") resuelve.
                gl_flatten_r6_character(p, character);
            } else {
                MeshInstance3D* m = memnew(MeshInstance3D);
                m->set_name("Mesh");
                Ref<CapsuleMesh> cm; cm.instantiate();
                m->set_mesh(cm);
                Ref<StandardMaterial3D> mat; mat.instantiate();
                mat->set_albedo(Color(0.55, 0.55, 0.55));
                mat->set_roughness(0.95);
                m->set_material_override(mat);
                p->add_child(m);
            }

            CollisionShape3D* c = memnew(CollisionShape3D);
            Ref<CapsuleShape3D> cs; cs.instantiate();
            c->set_shape(cs);
            p->add_child(c);
            // Avatar a escala real: la capsula a su medida y el punto de
            // aparicion subido lo que mide de la HRP a los pies (antes 1).
            const float pies = gl_ajustar_al_avatar(p, c, nullptr);
            if (pies > 1.0f) p->set_position(p->get_position() + Vector3(0.0f, pies - 1.0f, 0.0f));
        }

        // ── 4. Always ensure a Humanoid exists (movement) ───────────────────────
        //// ── 4. Siempre garantizar Humanoid (movimiento) ──────────────────────────
        if (!p->get_node_or_null("Humanoid")) {
            Humanoid* hum = memnew(Humanoid);
            hum->set_name("Humanoid");
            // StarterPlayer manda sobre los valores por defecto (gl_avatar.h).
            // Antes el personaje nacia SIEMPRE con 16/50.
            if (Node* game = get_parent())
                gl_aplicar_starter_player(game->get_node_or_null(NodePath("StarterPlayer")), hum);
            p->add_child(hum);
        }
        if (Node* hum = p->get_node_or_null("Humanoid")) {
            gl_asegurar_animator(hum);
            gl_ajustar_al_avatar(p, nullptr, hum);   // RigType y HipHeight del avatar
        }

        // ── 5. Clone StarterCharacterScripts onto the character ─────────────────
        //// ── 5. Clonar StarterCharacterScripts al personaje ───────────────────────
        if (starter_player) {
            Node* char_scripts = starter_player->get_node_or_null("StarterCharacterScripts");
            if (char_scripts) {
                TypedArray<Node> hijos = char_scripts->get_children();
                for (int i = 0; i < hijos.size(); i++) {
                    Node* hijo = gl_cast<Node>(hijos[i]);
                    if (hijo) {
                        p->add_child(hijo->duplicate());
                    }
                }
            }
        }
        // Como Roblox: si el juego no trae su Animate, el personaje lleva el de
        // fabrica (sin codigo: al arrancar toma la plantilla de DefaultScripts).
        if (!p->get_node_or_null(NodePath("Animate"))) {
            if (Node* an = gl_cast<Node>(ClassDB::instantiate(StringName("LocalScript")))) {
                an->set_name("Animate");
                p->add_child(an);
            }
        }
        if (starter_player) {
            // ── 7. Clone StarterPlayerScripts onto the player ───────────────────
            //// ── 7. Clonar StarterPlayerScripts al jugador ────────────────────────
            Node* player_scripts = starter_player->get_node_or_null("StarterPlayerScripts");
            if (player_scripts) {
                TypedArray<Node> hijos = player_scripts->get_children();
                for (int i = 0; i < hijos.size(); i++) {
                    Node* hijo = gl_cast<Node>(hijos[i]);
                    if (hijo) {
                        p->add_child(hijo->duplicate());
                    }
                }
            }

            GL_DEBUG_PRINT("[GodotLuau] Player scripts loaded from StarterPlayer.");
        }
    }
};

#include "roblox_workspace_mundo.h"   // conjuntos rigidos y streaming (cuerpos de los metodos)

#endif
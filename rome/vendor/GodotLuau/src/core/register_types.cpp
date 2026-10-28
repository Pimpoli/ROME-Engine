#include "gl_cabeceras.h"
#include "gl_muestreo.h"
// HandleAdornment (ui/gl_adornos.h): solo los registra este TU, nadie mas los
// nombra en C++ (Luau e importador los encuentran por ClassDB).
#include "gl_adornos.h"

// Vivia sola en luau_script.cpp; un TU entero solo para esto costaba ~8 s.
ScriptLanguage *LuauScript::_get_language() const {
    return LuauLanguage::get_singleton();
}

static LuauLanguage* luau_lang   = nullptr;
static Ref<LuauLoader>  luau_loader;
static Ref<LuauSaver>   luau_saver;

void initialize_luau_module(ModuleInitializationLevel p_level) {

    // ── SERVERS level: register the Luau scripting language
    //// ── Nivel SERVERS: registrar el lenguaje Luau
    if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
        ClassDB::register_class<LuauScript>();
        ClassDB::register_class<LuauLanguage>();
        luau_lang = memnew(LuauLanguage);
        Engine::get_singleton()->register_script_language(luau_lang);

        // ── JOLT: el motor de fisica que se parece a Roblox ──────────────
        // Godot trae dos motores 3D. El suyo propio y Jolt. Jolt es el que se
        // comporta como Roblox (que usa PhysX/Jolt-like): apilar cajas, empujar
        // personajes y frenar cuerpos anclados se sienten igual, y aguanta
        // miles de cuerpos como los que trae un place importado.
        //
        // El proyecto plantilla ya traia los LIMITES de Jolt configurados
        // (jolt_physics_3d/limits/...) pero le faltaba la linea que de verdad lo
        // ACTIVA, asi que se estaba corriendo con el motor de Godot y con la
        // configuracion de Jolt sin usar. Aqui se pone tambien para quien
        // instale la extension en un proyecto suyo.
        //
        // Godot lee este ajuste al arrancar los servidores, ANTES de que corra
        // este codigo: por eso se guarda y se avisa de que hace falta reiniciar.
        // A partir del segundo arranque ya entra solo.
        if (ProjectSettings* ps = ProjectSettings::get_singleton()) {
            const String actual = ps->get_setting("physics/3d/physics_engine", "DEFAULT");
            if (actual != "Jolt Physics") {
                ps->set_setting("physics/3d/physics_engine", "Jolt Physics");
                ps->save();
                UtilityFunctions::print(
                    "[GodotLuau] Motor de fisica 3D cambiado a Jolt (estaba en \"",
                    actual, "\"). REINICIA el proyecto para que entre en vigor.");
            }

            // ── Vulkan en vez de D3D12 ────────────────────────────────────
            // Godot 4.7 trae D3D12 por defecto en Windows. Medido con el place
            // de prueba en una Intel UHD 770, dos veces y en los dos ordenes:
            //     D3D12 .... 63 FPS | mediana 15,2 ms | p90 18,4 | p99 34
            //     Vulkan ... 93 FPS | mediana 10,0 ms | p90 13,3 | p99 27
            // +45%, y el p99 baja de los 33 ms: son justo los frames que se ven
            // como un tiron. Igual que con Jolt, el driver se elige ANTES de
            // cargar extensiones, asi que se deja guardado y entra al reiniciar.
            // fallback_to_d3d12 cubre las maquinas sin Vulkan.
            //
            // El vacio tambien cuenta: en un proyecto recien creado el ajuste no
            // aparece en project.godot y get_setting devuelve "". COMPROBADO en
            // una instalacion desde cero (2026-08-29): Godot 4.7 ya arranca en
            // Vulkan por su cuenta en Windows —RenderingServer reporta Vulkan
            // 1.4.323— asi que en 4.7 esta rama no cambia nada y no se guarda
            // (Godot omite del fichero los valores iguales al de fabrica).
            // Se deja por si una version futura vuelve a D3D12 o si alguien lo
            // pone a mano; NO es una mejora de rendimiento, el driver ya era el
            // bueno.
            const String drv = ps->get_setting("rendering/rendering_device/driver.windows", "");
            if (drv.is_empty() || drv == "d3d12") {
                ps->set_setting("rendering/rendering_device/driver.windows", "vulkan");
                ps->set_setting("rendering/rendering_device/fallback_to_d3d12", true);
                ps->save();
                UtilityFunctions::print(
                    "[GodotLuau] Driver de render cambiado a Vulkan (estaba en D3D12; "
                    "medido +45% de FPS). REINICIA el proyecto para que entre en vigor.");
            }
        }
    }

    // ── SCENE level: register all nodes and services
    //// ── Nivel SCENE: registrar todos los nodos y servicios
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        gl_muestreo_iniciar(Time::get_singleton()->get_ticks_usec());   // solo con GL_MUESTREO=1

        // Limites de Jolt Physics (respaldo para juegos exportados; en el
        // editor el plugin ya los persiste en project.godot). Godot limita
        // Jolt a 10240 cuerpos: un mundo tipo Roblox lo supera y las Parts
        // extra quedan sin colision. Jolt cachea estos valores al arrancar y
        // solo los relee con la señal settings_changed (diferida), asi que
        // ademas de setearlos la emitimos a mano ANTES de que exista el
        // primer espacio fisico (este init corre antes del SceneTree).
        // ── TEXTO 3D NITIDO A CUALQUIER DISTANCIA ────────────────────
        // Los carteles del mundo (BillboardGui/SurfaceGui) se dibujan con
        // Label3D, y un Label3D normal hornea la letra en un atlas de pixeles:
        // de cerca va bien, pero al alejarse se minifica y el texto sale
        // sucio y hormigueando — que es como se ven "V.I.P!" o "Join the group
        // and Like the game" a media distancia.
        // Con MSDF la letra se guarda como campo de distancia y se reconstruye
        // nitida a cualquier escala. Hay que pedirlo ANTES de que se cree la
        // fuente por defecto, o sea aqui: este init corre antes del SceneTree.
        if (ProjectSettings* ps0 = ProjectSettings::get_singleton()) {
            const char* k = "gui/theme/default_font_multichannel_signed_distance_field";
            if (!(bool)ps0->get_setting(k, false)) ps0->set_setting(k, true);
        }

        if (ProjectSettings* ps = ProjectSettings::get_singleton()) {
            struct { const char* key; int64_t val; } limits[] = {
                { "physics/jolt_physics_3d/limits/max_bodies",              262144 },
                { "physics/jolt_physics_3d/limits/max_body_pairs",          131072 },
                { "physics/jolt_physics_3d/limits/max_contact_constraints",  40960 },
                { "physics/jolt_physics_3d/limits/temporary_memory_buffer_size", 128 },
            };
            bool changed = false;
            for (auto& l : limits) {
                if ((int64_t)ps->get_setting(l.key, 0) < l.val) {
                    ps->set_setting(l.key, l.val);
                    changed = true;
                }
            }
            if (changed) ps->emit_signal("settings_changed");

            // ── Recuperacion de fisica: 1 paso por frame, no 8 ────────────
            // Por defecto Godot, cuando un frame se alarga, intenta RECUPERAR el
            // tiempo perdido corriendo hasta 8 pasos de fisica seguidos en el
            // frame siguiente. Con miles de cuerpos eso convierte un tiron en un
            // paron, que deja el reloj aun mas atrasado, que provoca mas pasos:
            // una espiral. Medido en el place de prueba (6.500 cuerpos):
            //
            //     pasos  FPS   p99      peor frame  picos>60ms  >150ms
            //       8    67,6  112,9ms   273 ms         41        13
            //       2    76,3   57,3ms   100 ms         20         0
            //       1    84,1   39,4ms    66,7ms          2         0
            //
            // Roblox hace justo esto: nunca recupera, deja que el reloj se
            // dilate. Con la fisica a 30 Hz y el juego por encima de 30 FPS,
            // un paso por frame sobra; solo se nota por debajo de 30 FPS, y ahi
            // vale mil veces mas que el mundo vaya algo lento a que se congele.
            // 2 y no 1: con 1 paso, por debajo de 60 FPS el reloj de fisica corre
            // MAS LENTO que el tiempo real (a 25 FPS, al 42%): el jugador andaba
            // "a camara lenta" y las animaciones iban con retraso. Con 2 pasos el
            // tiempo real se sostiene hasta 30 FPS, que es el suelo que defiende
            // la calidad automatica; y la espiral de recuperacion sigue capada
            // (medido: 2 pasos = 76 FPS, p99 57 ms, 0 picos >150 ms).
            ps->set_setting("physics/common/max_physics_steps_per_frame", 2);
        }
        if (Engine* eng = Engine::get_singleton()) eng->set_max_physics_steps_per_frame(2);

        // Las clases se registran desde los registro_*.cpp (ver el final de
        // gl_cabeceras.h), en este orden, que es el de siempre.
        gl_registro_scripts();
        gl_registro_mundo();
        gl_registro_importador();
        gl_registro_servicios();
        gl_registro_red();
        gl_registro_fisica();
        gl_registro_gui_texto();
        gl_registro_gui_imagen();
        gl_registro_efectos();
        gl_registro_juego();
        gl_registro_extra();
        gl_registro_adornos();

        // .lua script loader and saver
        //// Cargador y guardador de scripts .lua
        ClassDB::register_class<LuauLoader>();
        ClassDB::register_class<LuauSaver>();
        luau_loader.instantiate();
        ResourceLoader::get_singleton()->add_resource_format_loader(luau_loader);
        luau_saver.instantiate();
        ResourceSaver::get_singleton()->add_resource_format_saver(luau_saver);

        // Ensure the icons folder exists (required for node colors in the editor)
        //// Asegurar carpeta de iconos (necesaria para colores de nodos en editor)
        if (Engine::get_singleton()->is_editor_hint()) {
            Ref<DirAccess> dir = DirAccess::open("res://");
            if (dir.is_valid() && !dir->dir_exists("GodotLuau/icons")) {
                dir->make_dir_recursive("GodotLuau/icons");
                GL_DEBUG_PRINT("[GodotLuau] Carpeta 'res://GodotLuau/icons/' creada.");
            }
        }

        {
            // Desde la 1.17 todo lo de la extension vive en res://GodotLuau/.
            // El sitio viejo (res://Version) se sigue leyendo para no dejar sin
            // version a los proyectos que aun no han migrado.
            String version = "?";
            Ref<FileAccess> ver_f = FileAccess::open("res://GodotLuau/Version", FileAccess::READ);
            if (ver_f.is_null()) ver_f = FileAccess::open("res://Version", FileAccess::READ);
            if (ver_f.is_valid()) version = ver_f->get_line().strip_edges();

            // User-facing banner: English only (international).
            // ASCII puro: las consolas con otra codificación no lo deforman.
            UtilityFunctions::print(
                String("[GodotLuau] ////////////////////////////////////////////////////\n")
                + "[GodotLuau]   GodotLuau " + version + " - by PimpoliDev\n"
                + "[GodotLuau]   Luau System for Godot [Active]\n"
                + "[GodotLuau] ////////////////////////////////////////////////////"
            );

            Engine::get_singleton()->set_meta(
                "_godotluau_wm",
                String("GodotLuau ") + version + String(" by PimpoliDev | github.com/Pimpoli/GodotLuau")
            );
        }
    }

    // Nivel EDITOR: el importador de .rbxl anade su entrada en
    // Proyecto > Herramientas. Solo existe dentro del editor.
    if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
        ClassDB::register_internal_class<RBXImportPlugin>();
        EditorPlugins::add_by_type<RBXImportPlugin>();
    }
}

void uninitialize_luau_module(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        gl_muestreo_volcar();
        if (Engine::get_singleton()->has_meta("_godotluau_wm"))
            Engine::get_singleton()->remove_meta("_godotluau_wm");
        // Caches estaticos de materiales: se sueltan AQUI, mientras el
        // RenderingServer sigue vivo. Al vivir en un static local, si no se
        // vacian se destruyen al descargar la DLL, cuando el servidor ya no
        // esta: sus RID quedan fugados y ~BaseMaterial3D suelta errores.
        gl_shared_materials().clear();
        gl_tex_blanca_libera();   // la textura blanca por defecto (misma trampa)
        gls_liberar();            // shaders y texturas de studs/Material (idem)
        gl_luz_liberar();         // shaders y materiales de la luz propia (gl_luz_propia.h)
        gl_variantes_liberar();   // texturas de las MaterialVariant (idem)
        // Los otros DOS caches compartidos faltaban aqui, y son los que
        // producian la tormenta de errores al cerrar: ~2000 "RenderingServer::
        // get_singleton() is null" (las mallas) y otros ~2000 de PhysicsServer3D
        // (las formas de colision), porque sus Ref se soltaban al descargar la
        // DLL, cuando esos servidores ya no existen.
        gl_shared_meshes().clear();
        gl_shared_shapes().clear();
        // Las de las mallas propias (MeshPart, Union, FileMesh): mismo problema.
        gl_formas_de_malla().clear();
        gl_puntos_de_malla().clear();
        gl_mallas_locales().clear();
        gl_tex_streaming_liberar();     // cargas de texturas en vuelo (gl_streaming_texturas.h)
        gl_texturas_crudas().clear();   // imagenes sueltas leidas una vez (gl_asset.h)
        gl_clips_limpiar();   // clips de animacion (StringName/String en estaticos)
        gl_clips_integrados_limpiar();   // los de los avatares R6/R15
        gl_avatar_rig_limpiar();         // sus mallas y materiales (RID vivos)
        gl_contextos_cerrar_todos();   // las VM de Luau que sigan abiertas (una por contexto, F8)
        gl_wgui_pixel_blanco_ref().unref();   // el pixel blanco de los carteles en el mundo
        gl_wgui_soltar_degradado();           // shaders del texto con degradado de los carteles
        // Mallas unitarias del agrupado por chunks (Box/Sphere/Cylinder) y el
        // shader compartido de UIGradient: mismos static locales, mismo problema.
        gl_clear_unit_meshes();
        gl_clear_gradient_shader();
        gl_fuentes_soltar();            // las fuentes de Roblox (gl_fuentes.h)
        ParticleEmitter::gl_clear_emitter_cache();
        gl_sonidos_sueltos().clear();   // streams de PlayOnRemove sin lanzar al cerrar
        gl_nav_limpiar();               // mallas de Pathfinding a medio hornear
        gl_shader_cinta_libera();       // shaders de Beam/Trail
        gl_sombra_rd_liberar();         // shaders de las sombras propias (gl_sombras_efectos.h)
        gl_adornos_liberar();           // shaders y mallas de los HandleAdornment
        gl_vf_liberar();                // la curva de tono de los ViewportFrame
        gl_editables_liberar();         // subidas pendientes de EditableImage/Mesh (tras cerrar las VM)
        RobloxTerrain::_gl_clear_material_cache();
        // Quitarlos del ResourceLoader/Saver NO basta: estos Ref son ESTATICOS y
        // seguian sujetando su referencia, asi que los dos objetos vivian hasta la
        // descarga de la DLL — despues de que Godot cuente los objetos vivos. De
        // ahi el "2 ObjectDB instances were leaked at exit" de cada ejecucion.
        // Soltarlos aqui los destruye a tiempo.
        if (luau_loader.is_valid()) {
            ResourceLoader::get_singleton()->remove_resource_format_loader(luau_loader);
            luau_loader.unref();
        }
        if (luau_saver.is_valid()) {
            ResourceSaver::get_singleton()->remove_resource_format_saver(luau_saver);
            luau_saver.unref();
        }
    }
    if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
        if (luau_lang) {
            Engine::get_singleton()->unregister_script_language(luau_lang);
            memdelete(luau_lang);
            luau_lang = nullptr;
        }
    }
}

extern "C" {
GDExtensionBool GDE_EXPORT luau_extension_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    const GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization* r_initialization)
{
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_luau_module);
    init_obj.register_terminator(uninitialize_luau_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SERVERS);
    return init_obj.init();
}
}

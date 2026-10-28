#ifndef GL_LIGHTING_H
#define GL_LIGHTING_H

// El servicio Lighting. Va aparte de roblox_services.h (que se acercaba a las
// 2.000 lineas) y se incluye desde alli, en el mismo sitio en que estaba: usa
// lo que ese archivo incluye antes (gl_cast, gl_graphics, WorldEnvironment...).

// ════════════════════════════════════════════════════════════════════
//  LIGHTING AND ENVIRONMENT
////  ILUMINACIÓN Y AMBIENTE
// ════════════════════════════════════════════════════════════════════

class Lighting : public Node {
    GDCLASS(Lighting, Node);

public:
    enum LightingPreset {
        PRESET_DEFAULT=0, PRESET_REALISTIC, PRESET_CARTOON,
        PRESET_ANIME, PRESET_FLAT, PRESET_RETRO,
        PRESET_FOGGY, PRESET_SUNSET, PRESET_NIGHT
    };

private:
    // ── Core ─────────────────────────────────────────────────────
    // Valores POR DEFECTO IDENTICOS a los de Roblox Studio (no aproximados):
    // Brightness 2, ClockTime 14, GeographicLatitude 41.7, Ambient NEGRO (000),
    // OutdoorAmbient gris 128, FogColor gris 192, ShadowSoftness 0.2.
    // El Ambient por defecto de Roblox es NEGRO: ponerlo en gris (como estaba)
    // lavaba toda la escena y era una de las diferencias mas visibles.
    float brightness           = 2.0f;
    float clock_time           = 14.0f;
    float geographic_latitude  = 41.7f;
    Color ambient              = Color(0.0f, 0.0f, 0.0f);
    Color outdoor_ambient      = Color(0.5019608f, 0.5019608f, 0.5019608f);
    bool  global_shadows       = true;
    float shadow_softness      = 0.2f;
    // ── Environment ──────────────────────────────────────────────
    float env_diffuse_scale    = 1.0f;
    float env_specular_scale   = 1.0f;
    float exposure_comp        = 0.0f;
    // ── Fog ──────────────────────────────────────────────────────
    float fog_start            = 0.0f;
    float fog_end              = 100000.0f;
    float fog_density          = 0.0f;
    Color fog_color            = Color(0.7529412f, 0.7529412f, 0.7529412f);  // Roblox: gris 192
    // ── Color shifts (Roblox ColorShift_Top / ColorShift_Bottom) ──
    Color color_shift_top      = Color(0, 0, 0);
    Color color_shift_bottom   = Color(0, 0, 0);
    // Technology con los valores REALES de Enum.Technology de Roblox:
    //   Legacy=0, Voxel=1, Compatibility=2, ShadowMap=3, Future=4, Unified=5
    // Antes estaban inventados (Compatibility=0, Legacy=1, Future=2...): un
    // place con Future (4) activaba aqui el camino Voxel/SDFGI y uno con
    // Compatibility (2) el de Future — solo ShadowMap coincidia por casualidad.
    int   technology           = 3; // ShadowMap default (igual que Roblox moderno)
    // ── Día/noche automático ─────────────────────────────────────
    bool  day_night_cycle      = false;
    float day_length_minutes   = 10.0f;  // minutos reales por día completo (24h)
    // ── Preset ───────────────────────────────────────────────────
    int lighting_preset        = PRESET_DEFAULT;

    // Cache de nodos (evita recorrer el árbol entero en cada cambio)
    mutable uint64_t cached_we_id  = 0;
    mutable uint64_t cached_sun_id = 0;

    // ── Tree helpers ─────────────────────────────────────────────
    WorldEnvironment* _find_we_r(Node* n) const {
        if (!n) return nullptr;
        WorldEnvironment* we = gl_cast<WorldEnvironment>(n);
        if (we) return we;
        for (int i = 0; i < n->get_child_count(); i++) {
            WorldEnvironment* f = _find_we_r(n->get_child(i));
            if (f) return f;
        }
        return nullptr;
    }
    WorldEnvironment* _find_world_env() const {
        if (cached_we_id) {
            WorldEnvironment* we = gl_cast<WorldEnvironment>(ObjectDB::get_instance(cached_we_id));
            if (we && we->is_inside_tree()) return we;
            cached_we_id = 0;
        }
        if (!is_inside_tree()) return nullptr;
        WorldEnvironment* we = _find_we_r((Node*)get_tree()->get_root());
        if (we) cached_we_id = we->get_instance_id();
        return we;
    }
    Ref<Environment> _get_env() const {
        WorldEnvironment* we = _find_world_env();
        if (!we) return {};
        return we->get_environment();
    }
    DirectionalLight3D* _find_sun_r(Node* n) const {
        if (!n) return nullptr;
        DirectionalLight3D* d = gl_cast<DirectionalLight3D>(n);
        if (d) return d;
        for (int i = 0; i < n->get_child_count(); i++) {
            DirectionalLight3D* f = _find_sun_r(n->get_child(i));
            if (f) return f;
        }
        return nullptr;
    }
    DirectionalLight3D* _find_sun() const {
        if (cached_sun_id) {
            DirectionalLight3D* d = gl_cast<DirectionalLight3D>(ObjectDB::get_instance(cached_sun_id));
            if (d && d->is_inside_tree()) return d;
            cached_sun_id = 0;
        }
        if (!is_inside_tree()) return nullptr;
        DirectionalLight3D* d = _find_sun_r((Node*)get_tree()->get_root());
        if (d) cached_sun_id = d->get_instance_id();
        return d;
    }

    // ¿Hay un Atmosphere vivo? (normalmente hijo de este mismo Lighting).
    // Con Atmosphere, Roblox ignora la niebla clasica FogStart/FogEnd/FogColor.
    static bool _find_class_r(Node* n, const char* cls) {
        if (!n) return false;
        if (n->is_class(cls)) return true;
        for (int i = 0; i < n->get_child_count(); i++)
            if (_find_class_r(n->get_child(i), cls)) return true;
        return false;
    }
    bool _has_atmosphere() const {
        return _find_class_r(const_cast<Lighting*>(this), "AtmosphereNode");
    }

public:
    // ── Sol y luna 1:1 con Roblox ────────────────────────────────────
    // Sacado de Lighting:GetSunDirection() en Studio (6 latitudes x 10 horas,
    // 2026-10-08), coincide en los 4 decimales: el sol recorre un circulo
    // inclinado (latitud - 23.5) grados.
    //   phi = rad(lat - 23.5), th = 2*pi*ClockTime/24
    //   sol  = (cos(phi) sin(th), -cos(phi) cos(th), sin(phi))
    //   luna = (-sol.x, -sol.y, sol.z)
    // El modelo de antes (elevacion por el seno de la hora, acimut fijo de
    // 45 + lat*0.3 y un techo de 62 grados) ponia el sol en otro sitio: las
    // sombras caian hacia otro lado y con otra longitud que en Roblox.
    Vector3 get_sun_direction() const {
        const float phi = Math::deg_to_rad(geographic_latitude - 23.5f);
        const float th = (float)Math_TAU * clock_time / 24.0f;
        const float r = Math::cos(phi);
        return Vector3(r * Math::sin(th), -r * Math::cos(th), Math::sin(phi));
    }
    Vector3 get_moon_direction() const {
        const Vector3 s = get_sun_direction();
        return Vector3(-s.x, -s.y, s.z);
    }
private:

    // Orienta la luz para que venga DESDE `dir` (su -Z apunta a -dir).
    static void _apuntar_luz(DirectionalLight3D* luz, const Vector3& dir) {
        const Vector3 arriba = Math::abs(dir.y) > 0.999f ? Vector3(0, 0, 1) : Vector3(0, 1, 0);
        luz->set_basis(Basis::looking_at(-dir, arriba));
    }

    void _apply_sun() {
        DirectionalLight3D* sun = _find_sun();
        if (!sun) return;
        const Vector3 dir_sol = get_sun_direction();
        // Calidad 1-3: sin sombras, como Roblox. Con las sombras propias
        // pintando (ui/gl_sombras.h), la de Godot no.
        sun->set_shadow(global_shadows && gl_calidad_sombras() && !gl_sombras_propias_listas());
        gl_luz_version()++;
        // ── ShadowSoftness (0..1, Roblox trae 0.5) ───────────────────────
        // ANTES: se metia en PARAM_SIZE (el tamaño angular del sol). Dos fallos
        // en uno. Primero, la escala no pega: 0.2 x 0.8 = 0,16 GRADOS, que a la
        // altura de un personaje son 0,014 studs de penumbra — invisible.
        // Segundo y peor, cualquier PARAM_SIZE mayor que cero manda el shader
        // de Godot al camino PCSS, donde el blur se IGNORA; y como el PCSS abre
        // la penumbra segun lo lejos que este el bloqueador del suelo, los pies
        // del personaje (que lo tocan) salian con el borde de canto duro por
        // mucha calidad que hubiera. Eso era "de graficos medios en adelante no
        // hay degradado".
        // AHORA: PARAM_SIZE = 0 (camino PCF, degradado de ancho fijo y estable)
        // y ShadowSoftness elige POSICION dentro del rango de texeles que la
        // calidad puede muestrear sin dejar pasos. Corre DESPUES de
        // gl_apply_graphics_quality, de ahi que el rango se publique alli.
        gl_shadow_softness_juego() = shadow_softness;
        sun->set_param(Light3D::PARAM_SIZE, 0.0f);
        sun->set_param(Light3D::PARAM_SHADOW_BLUR,
                       gl_blur_desde_softness(shadow_softness));

        // ── FIEL A ROBLOX ────────────────────────────────────────────────
        // Brightness ES la intensidad de la luz del sol, directa. Antes se
        // multiplicaba por una curva "artistica" y ademas el sol se teñia solo de
        // naranja al atardecer: Roblox NO hace eso (el color calido viene del
        // Atmosphere/ColorShift, no del sol). Aqui solo se apaga bajo el horizonte
        // (de noche manda la luna/ambiente), con un desvanecido corto para no dar
        // un corte seco justo en el horizonte: en el horizonte (ocaso y amanecer,
        // ClockTime 6 y 18) todavia hay luz, como en Roblox.
        float above = Math::clamp(dir_sol.y * 5.0f + 0.42f, 0.0f, 1.0f);

        // ── LUNA (1:1 con Roblox) ────────────────────────────────────────
        // De noche Roblox NO deja el mundo a oscuras planas: la luna ilumina con
        // una luz tenue y azulada desde el lado opuesto al sol. Antes aqui la luz
        // se apagaba del todo y la noche salia plana y sin relieve. Se reutiliza
        // el mismo DirectionalLight (como hace Roblox con su sol/luna opuestos):
        // si el sol esta bajo el horizonte, se apunta a la posicion de la luna.
        const bool night = (above <= 0.001f);
        _apuntar_luz(sun, night ? get_moon_direction() : dir_sol);
        if (night) {
            sun->set_param(Light3D::PARAM_ENERGY, 0.35f);
            sun->set_color(Color(0.55f, 0.66f, 0.95f));   // luz lunar fria
            sun->set_param(Light3D::PARAM_INDIRECT_ENERGY, env_diffuse_scale * 0.5f);
            sun->set_param(Light3D::PARAM_SPECULAR,        0.5f);
            return;
        }
        // ── Brightness y ColorShift_Top, MEDIDOS (2026-10-08) ─────────────
        // Carta de grises en Play de Studio, solo sol y sin efectos, invirtiendo
        // la curva de tonos (ACES white 2, la que encaja con Roblox): una pieza
        // blanca de cara al sol recibe Brightness / pi de energia de Godot
        // (3,2 -> 1,03 medido, 1,6 -> 0,52). Los factores de antes (x0.75,
        // x0.5) eran a ojo y con colores sRGB: con la conversion gamma 2 de
        // gl_color_pieza el sol tiene que ser este.
        // ColorShift_Top NO se suma al blanco: tine el sol conservando su media.
        // Con c = ColorShift_Top en lineal (gamma 2) y m la media de sus canales,
        // cada canal del sol vale 1 + 1.7 (c - m). Medido: gris 0.5 no cambia
        // nada; rojo (1,0,0) da (2.13, 0.42, 0.42); el calido de Build a Mountain
        // (1, .957, .871) da (1.17, 1.04, 0.78).
        const Color c(color_shift_top.r * color_shift_top.r, color_shift_top.g * color_shift_top.g,
                      color_shift_top.b * color_shift_top.b);
        const float media = (c.r + c.g + c.b) / 3.0f;
        const float lr = Math::max(1.0f + 1.7f * (c.r - media), 0.0f);
        const float lg = Math::max(1.0f + 1.7f * (c.g - media), 0.0f);
        const float lb = Math::max(1.0f + 1.7f * (c.b - media), 0.0f);
        // ── El cielo junto al sol, MEDIDO (carta con toda la luz del juego) ──
        // Con Ambient, OutdoorAmbient y EnvironmentDiffuseScale puestos, una cara
        // al sol recibe MAS que sol + ambiente: un extra azulado que sigue a N.L,
        // como la luz del cielo que rodea al sol. Sin el, lo que da el sol se
        // quedaba corto de azul (carta: caras al sol B -7/-8 niveles; adoquin de
        // la camara 1 B 152 contra 171; talud de la cantera 97 contra 110). En
        // Build a Mountain vale (0.015, 0.077, 0.21) por unidad de Brightness/pi
        // y de EnvironmentDiffuseScale; con el, la carta baja de 4,7 a 3,8
        // niveles. La formula de ColorShift_Top de arriba sale de la carta con
        // SOLO sol (EDS 0), donde este extra no aparece.
        const float b = brightness / (float)Math_PI;
        const float ex = Math::max(env_diffuse_scale, 0.0f);
        const float sr = b * (lr + ex * 0.0147f);
        const float sg = b * (lg + ex * 0.0766f);
        const float sb = b * (lb + ex * 0.206f);
        // El canal mayor va a la energia; el color de la luz (sRGB) queda en 0..1.
        const float mayor = Math::max(Math::max(sr, sg), Math::max(sb, 0.0001f));
        sun->set_color(Color(sr / mayor, sg / mayor, sb / mayor).linear_to_srgb());
        sun->set_param(Light3D::PARAM_ENERGY, mayor * above);
        sun->set_param(Light3D::PARAM_INDIRECT_ENERGY, env_diffuse_scale);
        // EnvironmentSpecularScale es el reflejo del CIELO, no el brillo del sol:
        // con el sol a 0.6 (lo de antes) las caras de cara al sol salian 7-15
        // niveles mas oscuras que en Roblox (carta en Play). A 1, con la
        // rugosidad medida de SmoothPlastic, la carta baja de 4,8 a 4,7.
        sun->set_param(Light3D::PARAM_SPECULAR, 1.0f);
    }

    void _apply_env() {
        Ref<Environment> env = _get_env();
        if (!env.is_valid()) return;
        gl_luz_version()++;   // la sonda de las sombras propias vuelve a medir

        // ── Luz ambiente, MEDIDA (carta de grises en Play, 2026-10-08) ────
        // Las cifras de antes (energia 1.8, 0.35, 1.0; mezcla de cielo 0.7 y 0.3)
        // salian de comparar capturas a ojo. Midiendo cada termino por separado
        // con la curva de tonos invertida, Roblox hace esto:
        //  * Ambient y OutdoorAmbient NO se suman: manda el mayor por canal. Con
        //    solo uno de los dos, o con los dos, sale lo mismo a cielo abierto.
        //  * Como todos sus colores, pasan a lineal con gamma 2 (c*c), y cada
        //    unidad vale 0.637 (~2/pi) de energia de Godot: con OutdoorAmbient
        //    (150,154,168) una pieza blanca recibe (0.221, 0.233, 0.279).
        //  * EnvironmentDiffuseScale suma aparte la luz del cielo (su skybox), que
        //    depende de hacia donde mira la cara: azul arriba, mas calida de lado.
        //    Es justo el ambiente de cielo de Godot, con peso 1 por unidad.
        // Godot no suma color y cielo: los MEZCLA con sky_contribution. Se
        // reparte la energia para que salga color*a + cielo*EDS:
        //   energia = a + EDS, mezcla = EDS / energia, color = ambiente / a.
        // ColorShift_Bottom se suma al ambiente con la misma escala.
        Color amb(Math::max(ambient.r * ambient.r, outdoor_ambient.r * outdoor_ambient.r),
                  Math::max(ambient.g * ambient.g, outdoor_ambient.g * outdoor_ambient.g),
                  Math::max(ambient.b * ambient.b, outdoor_ambient.b * outdoor_ambient.b));
        amb.r += color_shift_bottom.r * color_shift_bottom.r;
        amb.g += color_shift_bottom.g * color_shift_bottom.g;
        amb.b += color_shift_bottom.b * color_shift_bottom.b;
        amb = Color(amb.r * 0.637f, amb.g * 0.637f, amb.b * 0.637f);
        const float a = Math::max(Math::max(amb.r, amb.g), amb.b);
        const float cielo = Math::max(env_diffuse_scale, 0.0f);
        const float energia = a + cielo;
        env->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
        env->set_ambient_light_color(a > 0.00001f ? Color(amb.r / a, amb.g / a, amb.b / a).linear_to_srgb()
                                                  : Color(0, 0, 0));
        env->set_ambient_light_energy(energia);
        env->set_ambient_light_sky_contribution(energia > 0.00001f ? cielo / energia : 0.0f);
        // ExposureCompensation de Roblox esta en PASOS (EV): "+1 indica el doble de
        // exposicion y -1 la mitad" → factor 2^EC ANTES del tonemap, que es
        // exactamente lo que hace tonemap_exposure en Godot.
        // https://create.roblox.com/docs/reference/engine/classes/Lighting#ExposureCompensation
        env->set_tonemap_exposure(Math::pow(2.0f, exposure_comp));

        // Niebla clasica: LINEAL por PROFUNDIDAD de FogStart a FogEnd con FogColor
        // tal cual (como Roblox; sin lerps de atardecer/noche). Si el juego tiene
        // un Atmosphere bajo Lighting, la niebla clasica SE IGNORA — es la regla de
        // Roblox ("when using an Atmosphere the classic fog properties are ignored"):
        // https://create.roblox.com/docs/reference/engine/classes/Atmosphere
        if (!_has_atmosphere()) {
            bool use_depth_fog = (fog_end < 99000.0f && fog_end > fog_start);
            bool use_exp_fog   = (fog_density > 0.001f);
            env->set_fog_enabled(use_depth_fog || use_exp_fog);
            env->set_fog_light_color(fog_color);
            env->set_fog_sky_affect(0.0f);   // el cielo de Roblox no se come la niebla
            if (use_depth_fog) {
                env->set_fog_mode(Environment::FOG_MODE_DEPTH);
                env->set_fog_depth_begin(fog_start);
                env->set_fog_depth_end(fog_end);
                env->set_fog_depth_curve(1.0f);   // lineal, como Roblox
                env->set_fog_density(1.0f);
            } else if (use_exp_fog) {
                env->set_fog_mode(Environment::FOG_MODE_EXPONENTIAL);
                env->set_fog_density(fog_density * 0.08f);
            } else {
                // Sin niebla: densidad 0 ademas del flag. Si la curva de calidad
                // reenciende fog_enabled, con densidad 0 sigue invisible (antes
                // quedaba una densidad vieja y salia una neblina fantasma a lo lejos).
                env->set_fog_density(0.0f);
            }
        }

        // ── Technology → como se calcula la ILUMINACION (igual que Roblox) ──
        // OJO: Technology NO controla el bloom. Antes este switch encendia/apagaba
        // el glow, y como ShadowMap es el valor por defecto, apagaba el glow SIEMPRE
        // — pisando al BloomEffect del juego (que ya respeta su propio Enabled).
        // Aqui solo se tocan oclusion/luz indirecta, que es lo que cambia de verdad
        // entre las tecnologias de Roblox.
        // Ninguna tecnologia de Roblox hace AO de pantalla (SSAO), luz indirecta
        // de pantalla (SSIL) ni iluminacion global por campos de distancia
        // (SDFGI): Future da luz por pixel y sombras de todas las fuentes, y Voxel
        // es una rejilla barata. Encenderlas aqui oscurecia rincones que en Roblox
        // se ven limpios y costaba GPU de sobra (solo la SSAO, 1,5 ms en una UHD
        // 770 a 720p). La SSAO tampoco se toca: la decide la calidad (en Custom
        // el jugador puede encenderla y esto no debe pisarla).
        (void)technology;
        env->set_ssil_enabled(false);
        env->set_sdfgi_enabled(false);
    }

    // El CIELO sigue a ClockTime, como en Roblox: azul de dia, oscuro de noche.
    // Sin esto, a medianoche el sol se apagaba pero el cielo seguia azul brillante
    // (la diferencia mas visible con Roblox). Si el usuario puso un Sky propio
    // (LightingSkyNode con su shader) no se toca nada.
    void _apply_sky() {
        Ref<Environment> env = _get_env();
        if (!env.is_valid()) return;
        Ref<Sky> sky = env->get_sky();
        if (!sky.is_valid()) return;
        Ref<ProceduralSkyMaterial> m = sky->get_material();
        if (!m.is_valid()) return;   // Sky personalizado (shader): no tocarlo

        // 0 noche, 1 dia, segun la altura del sol (y de su direccion, -1..1)
        const float day = Math::clamp(get_sun_direction().y * 3.0f + 0.5f, 0.0f, 1.0f);

        const Color day_top(0.306f, 0.588f, 0.804f), night_top(0.016f, 0.024f, 0.075f);
        const Color day_hor(0.667f, 0.804f, 0.902f), night_hor(0.047f, 0.059f, 0.129f);
        const Color day_gnd(0.529f, 0.549f, 0.588f), night_gnd(0.024f, 0.028f, 0.047f);

        m->set_sky_top_color(night_top.lerp(day_top, day));
        m->set_sky_horizon_color(night_hor.lerp(day_hor, day));
        m->set_ground_bottom_color(night_gnd.lerp(day_gnd, day));
        m->set_ground_horizon_color(night_hor.lerp(day_hor, day));
        m->set_energy_multiplier(0.12f + 0.88f * day);
    }

    void _apply_all() {
        _apply_sun(); _apply_env(); _apply_sky();
        // El skybox (Sky) dibuja el sol y la luna donde los pone Lighting.
        for (int i = 0; i < get_child_count(); i++)
            if (get_child(i)->has_method("_gl_astros")) get_child(i)->call("_gl_astros");
    }

    void _apply_preset(int p) {
        switch (p) {
        case PRESET_REALISTIC:
            brightness=3.0f; shadow_softness=0.1f; env_diffuse_scale=1.0f;
            env_specular_scale=1.0f; exposure_comp=0.0f; fog_density=0.0f;
            ambient=Color(0.3f,0.3f,0.35f); outdoor_ambient=Color(0.5f,0.55f,0.6f);
            break;
        case PRESET_CARTOON:
            brightness=2.5f; shadow_softness=0.5f; env_diffuse_scale=1.2f;
            env_specular_scale=0.3f; exposure_comp=0.1f; fog_density=0.0f;
            ambient=Color(0.5f,0.5f,0.6f); outdoor_ambient=Color(0.6f,0.65f,0.8f);
            break;
        case PRESET_ANIME:
            brightness=2.8f; shadow_softness=0.0f; env_diffuse_scale=1.1f;
            env_specular_scale=0.5f; exposure_comp=0.2f; fog_density=0.0f;
            ambient=Color(0.45f,0.45f,0.55f); outdoor_ambient=Color(0.55f,0.6f,0.75f);
            break;
        case PRESET_FLAT:
            brightness=2.0f; shadow_softness=1.0f; env_diffuse_scale=2.0f;
            env_specular_scale=0.0f; exposure_comp=0.0f; fog_density=0.0f;
            ambient=Color(0.6f,0.6f,0.6f); global_shadows=false;
            break;
        case PRESET_RETRO:
            brightness=1.8f; shadow_softness=0.8f; env_diffuse_scale=0.8f;
            env_specular_scale=0.2f; exposure_comp=-0.2f; fog_density=0.02f;
            ambient=Color(0.35f,0.30f,0.25f); fog_color=Color(0.8f,0.75f,0.6f);
            break;
        case PRESET_FOGGY:
            brightness=1.5f; shadow_softness=0.4f; env_diffuse_scale=0.9f;
            env_specular_scale=0.4f; exposure_comp=-0.1f; fog_density=0.15f;
            fog_end=500.0f; ambient=Color(0.5f,0.52f,0.55f);
            fog_color=Color(0.75f,0.80f,0.85f);
            break;
        case PRESET_SUNSET:
            clock_time=18.5f; brightness=2.0f; shadow_softness=0.3f;
            env_diffuse_scale=1.0f; env_specular_scale=0.8f; exposure_comp=0.1f;
            fog_density=0.02f; ambient=Color(0.45f,0.30f,0.20f);
            fog_color=Color(1.0f,0.6f,0.3f);
            break;
        case PRESET_NIGHT:
            clock_time=22.0f; brightness=0.15f; shadow_softness=0.6f;
            env_diffuse_scale=0.3f; env_specular_scale=0.5f; exposure_comp=0.5f;
            fog_density=0.005f; ambient=Color(0.05f,0.06f,0.12f);
            fog_color=Color(0.05f,0.07f,0.15f);
            break;
        default: // PRESET_DEFAULT
            brightness=2.0f; clock_time=14.0f; shadow_softness=0.2f;
            env_diffuse_scale=1.0f; env_specular_scale=1.0f; exposure_comp=0.0f;
            fog_density=0.0f; fog_end=100000.0f; global_shadows=true;
            ambient=Color(0.4f,0.4f,0.4f); fog_color=Color(0.75f,0.85f,1.0f);
            break;
        }
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_reapply"),             &Lighting::_gl_reapply);
        ClassDB::bind_method(D_METHOD("set_lighting_preset","v"), &Lighting::set_lighting_preset);
        ClassDB::bind_method(D_METHOD("get_lighting_preset"),     &Lighting::get_lighting_preset);
        ADD_PROPERTY(PropertyInfo(Variant::INT,"LightingPreset",PROPERTY_HINT_ENUM,
            "Default,Realistic,Cartoon,Anime,Flat,Retro,Foggy,Sunset,Night"),
            "set_lighting_preset","get_lighting_preset");

        ClassDB::bind_method(D_METHOD("set_brightness","v"), &Lighting::set_brightness);
        ClassDB::bind_method(D_METHOD("get_brightness"),     &Lighting::get_brightness);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Brightness",PROPERTY_HINT_RANGE,"0,10,0.01,or_greater"),"set_brightness","get_brightness");

        ClassDB::bind_method(D_METHOD("get_sun_direction"),  &Lighting::get_sun_direction);
        ClassDB::bind_method(D_METHOD("get_moon_direction"), &Lighting::get_moon_direction);
        ClassDB::bind_method(D_METHOD("set_clock_time","v"), &Lighting::set_clock_time);
        ClassDB::bind_method(D_METHOD("get_clock_time"),     &Lighting::get_clock_time);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"ClockTime",PROPERTY_HINT_RANGE,"0,24,0.1"),"set_clock_time","get_clock_time");

        ClassDB::bind_method(D_METHOD("set_geographic_latitude","v"), &Lighting::set_geographic_latitude);
        ClassDB::bind_method(D_METHOD("get_geographic_latitude"),     &Lighting::get_geographic_latitude);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"GeographicLatitude",PROPERTY_HINT_RANGE,"-90,90,0.1"),"set_geographic_latitude","get_geographic_latitude");

        ClassDB::bind_method(D_METHOD("set_ambient","c"), &Lighting::set_ambient);
        ClassDB::bind_method(D_METHOD("get_ambient"),     &Lighting::get_ambient);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"Ambient"),"set_ambient","get_ambient");

        ClassDB::bind_method(D_METHOD("set_outdoor_ambient","c"), &Lighting::set_outdoor_ambient);
        ClassDB::bind_method(D_METHOD("get_outdoor_ambient"),     &Lighting::get_outdoor_ambient);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"OutdoorAmbient"),"set_outdoor_ambient","get_outdoor_ambient");

        ClassDB::bind_method(D_METHOD("set_global_shadows","v"), &Lighting::set_global_shadows);
        ClassDB::bind_method(D_METHOD("get_global_shadows"),     &Lighting::get_global_shadows);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"GlobalShadows"),"set_global_shadows","get_global_shadows");

        ClassDB::bind_method(D_METHOD("set_shadow_softness","v"), &Lighting::set_shadow_softness);
        ClassDB::bind_method(D_METHOD("get_shadow_softness"),     &Lighting::get_shadow_softness);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"ShadowSoftness",PROPERTY_HINT_RANGE,"0,1,0.001"),"set_shadow_softness","get_shadow_softness");

        ClassDB::bind_method(D_METHOD("set_env_diffuse_scale","v"), &Lighting::set_env_diffuse_scale);
        ClassDB::bind_method(D_METHOD("get_env_diffuse_scale"),     &Lighting::get_env_diffuse_scale);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"EnvironmentDiffuseScale",PROPERTY_HINT_RANGE,"0,2,0.01"),"set_env_diffuse_scale","get_env_diffuse_scale");

        ClassDB::bind_method(D_METHOD("set_env_specular_scale","v"), &Lighting::set_env_specular_scale);
        ClassDB::bind_method(D_METHOD("get_env_specular_scale"),     &Lighting::get_env_specular_scale);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"EnvironmentSpecularScale",PROPERTY_HINT_RANGE,"0,2,0.01"),"set_env_specular_scale","get_env_specular_scale");

        ClassDB::bind_method(D_METHOD("set_exposure_comp","v"), &Lighting::set_exposure_comp);
        ClassDB::bind_method(D_METHOD("get_exposure_comp"),     &Lighting::get_exposure_comp);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"ExposureCompensation",PROPERTY_HINT_RANGE,"-5,5,0.01"),"set_exposure_comp","get_exposure_comp");

        ClassDB::bind_method(D_METHOD("set_fog_start","v"), &Lighting::set_fog_start);
        ClassDB::bind_method(D_METHOD("get_fog_start"),     &Lighting::get_fog_start);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"FogStart",PROPERTY_HINT_RANGE,"0,10000,1"),"set_fog_start","get_fog_start");

        ClassDB::bind_method(D_METHOD("set_fog_end","v"), &Lighting::set_fog_end);
        ClassDB::bind_method(D_METHOD("get_fog_end"),     &Lighting::get_fog_end);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"FogEnd",PROPERTY_HINT_RANGE,"0,100000,1"),"set_fog_end","get_fog_end");

        ClassDB::bind_method(D_METHOD("set_fog_density","v"), &Lighting::set_fog_density);
        ClassDB::bind_method(D_METHOD("get_fog_density"),     &Lighting::get_fog_density);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"FogDensity",PROPERTY_HINT_RANGE,"0,1,0.001"),"set_fog_density","get_fog_density");

        ClassDB::bind_method(D_METHOD("set_fog_color","c"), &Lighting::set_fog_color);
        ClassDB::bind_method(D_METHOD("get_fog_color"),     &Lighting::get_fog_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"FogColor"),"set_fog_color","get_fog_color");

        ClassDB::bind_method(D_METHOD("set_color_shift_top","c"),    &Lighting::set_color_shift_top);
        ClassDB::bind_method(D_METHOD("get_color_shift_top"),        &Lighting::get_color_shift_top);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"ColorShift_Top"),  "set_color_shift_top","get_color_shift_top");

        ClassDB::bind_method(D_METHOD("set_color_shift_bottom","c"), &Lighting::set_color_shift_bottom);
        ClassDB::bind_method(D_METHOD("get_color_shift_bottom"),     &Lighting::get_color_shift_bottom);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"ColorShift_Bottom"),"set_color_shift_bottom","get_color_shift_bottom");

        ClassDB::bind_method(D_METHOD("set_technology","t"), &Lighting::set_technology);
        ClassDB::bind_method(D_METHOD("get_technology"),     &Lighting::get_technology);
        ADD_PROPERTY(PropertyInfo(Variant::INT,"Technology",PROPERTY_HINT_ENUM,
            "Legacy:0,Voxel:1,Compatibility:2,ShadowMap:3,Future:4,Unified:5"),
            "set_technology","get_technology");

        // TimeOfDay estilo Roblox ("14:30:00") — espejo editable de ClockTime
        ClassDB::bind_method(D_METHOD("set_time_of_day","t"), &Lighting::set_time_of_day);
        ClassDB::bind_method(D_METHOD("get_time_of_day"),     &Lighting::get_time_of_day);
        ADD_PROPERTY(PropertyInfo(Variant::STRING,"TimeOfDay",PROPERTY_HINT_NONE,"",
            PROPERTY_USAGE_EDITOR), "set_time_of_day","get_time_of_day");

        ClassDB::bind_method(D_METHOD("GetMinutesAfterMidnight"), &Lighting::get_minutes_after_midnight);
        ClassDB::bind_method(D_METHOD("SetMinutesAfterMidnight","m"), &Lighting::set_minutes_after_midnight);

        // Ciclo día/noche automático
        ClassDB::bind_method(D_METHOD("set_day_night_cycle","v"), &Lighting::set_day_night_cycle);
        ClassDB::bind_method(D_METHOD("get_day_night_cycle"),     &Lighting::get_day_night_cycle);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"DayNightCycle"),"set_day_night_cycle","get_day_night_cycle");

        ClassDB::bind_method(D_METHOD("set_day_length_minutes","v"), &Lighting::set_day_length_minutes);
        ClassDB::bind_method(D_METHOD("get_day_length_minutes"),     &Lighting::get_day_length_minutes);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"DayLengthMinutes",PROPERTY_HINT_RANGE,"0.5,120,0.5"),
            "set_day_length_minutes","get_day_length_minutes");
    }

    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) {
            if (!_enforce_game_parent(this)) return;
            _apply_all();
            if (!Engine::get_singleton()->is_editor_hint() && day_night_cycle)
                set_process(true);
        } else if (p_what == NOTIFICATION_READY) {
            // Re-aplicar DESPUES de que el Workspace monte su entorno: el
            // Workspace corre su _ready despues del ENTER_TREE de Lighting y
            // fijaba ambient/exposicion por su cuenta, pisando estos valores.
            // Con esto manda Lighting, que es quien debe mandar (como Roblox).
            call_deferred("_gl_reapply");
        } else if (p_what == NOTIFICATION_PROCESS) {
            if (!day_night_cycle || Engine::get_singleton()->is_editor_hint()) {
                set_process(false);
                return;
            }
            // Avanza el reloj: 24h del juego en day_length_minutes minutos reales
            double dt = get_process_delta_time();
            clock_time = Math::fmod(clock_time + (float)(dt * 24.0 / (day_length_minutes * 60.0)), 24.0f);
            _apply_all();
        }
    }

public:
    // Re-aplica todo (lo llama el READY diferido para ganarle al Workspace).
    void _gl_reapply() { if (is_inside_tree()) _apply_all(); }

    void set_lighting_preset(int v) {
        lighting_preset = v;
        _apply_preset(v);
        if (is_inside_tree()) _apply_all();
    }
    int  get_lighting_preset() const    { return lighting_preset; }

    // Sin tope en 4: el slider de Studio llega a 10 y por script Roblox admite mas
    // (la doc no documenta clamp). El tope anterior (4) recortaba places importados.
    // https://create.roblox.com/docs/reference/engine/classes/Lighting#Brightness
    void set_brightness(float v)        { brightness = Math::clamp(v,0.0f,100.0f);  if(is_inside_tree()) _apply_all(); }
    float get_brightness() const        { return brightness; }
    void set_clock_time(float v)        { clock_time = Math::fmod(Math::abs(v),24.0f); if(is_inside_tree()) _apply_all(); }
    float get_clock_time() const        { return clock_time; }
    void set_geographic_latitude(float v){ geographic_latitude = Math::clamp(v,-90.0f,90.0f); if(is_inside_tree()) _apply_all(); }
    float get_geographic_latitude() const{ return geographic_latitude; }
    void set_ambient(Color c)           { ambient = c;                               if(is_inside_tree()) _apply_env(); }
    Color get_ambient() const           { return ambient; }
    void set_outdoor_ambient(Color c)   { outdoor_ambient = c;                       if(is_inside_tree()) _apply_env(); }
    Color get_outdoor_ambient() const   { return outdoor_ambient; }
    void set_global_shadows(bool v)     { global_shadows = v;                        if(is_inside_tree()) _apply_sun(); }
    bool get_global_shadows() const     { return global_shadows; }
    void set_shadow_softness(float v)   { shadow_softness = Math::clamp(v,0.0f,1.0f); if(is_inside_tree()) _apply_sun(); }
    float get_shadow_softness() const   { return shadow_softness; }
    void set_env_diffuse_scale(float v) { env_diffuse_scale = Math::clamp(v,0.0f,2.0f); if(is_inside_tree()) _apply_all(); }
    float get_env_diffuse_scale() const { return env_diffuse_scale; }
    void set_env_specular_scale(float v){ env_specular_scale = Math::clamp(v,0.0f,2.0f); if(is_inside_tree()) _apply_all(); }
    float get_env_specular_scale() const{ return env_specular_scale; }
    void set_exposure_comp(float v)     { exposure_comp = Math::clamp(v,-5.0f,5.0f); if(is_inside_tree()) _apply_env(); }
    float get_exposure_comp() const     { return exposure_comp; }
    void set_fog_start(float v)         { fog_start = v;                             if(is_inside_tree()) _apply_env(); }
    float get_fog_start() const         { return fog_start; }
    void set_fog_end(float v)           { fog_end = v;                               if(is_inside_tree()) _apply_env(); }
    float get_fog_end() const           { return fog_end; }
    void set_fog_density(float v)       { fog_density = Math::clamp(v,0.0f,1.0f);   if(is_inside_tree()) _apply_env(); }
    float get_fog_density() const       { return fog_density; }
    void set_fog_color(Color c)          { fog_color = c;                             if(is_inside_tree()) _apply_env(); }
    Color get_fog_color() const          { return fog_color; }
    void  set_color_shift_top(Color c)   { color_shift_top = c;    if(is_inside_tree()) _apply_sun(); }
    Color get_color_shift_top() const    { return color_shift_top; }
    void  set_color_shift_bottom(Color c){ color_shift_bottom = c; if(is_inside_tree()) _apply_env(); }
    Color get_color_shift_bottom() const { return color_shift_bottom; }
    void  set_technology(int t)          { technology = Math::clamp(t, 0, 4); if(is_inside_tree()) _apply_env(); }
    int   get_technology() const         { return technology; }

    // ── TimeOfDay estilo Roblox ("HH:MM:SS") ─────────────────────
    void set_time_of_day(const String& s) {
        PackedStringArray p = s.split(":");
        float h  = p.size() > 0 ? (float)p[0].to_int() : 0.0f;
        float m  = p.size() > 1 ? (float)p[1].to_int() : 0.0f;
        float sc = p.size() > 2 ? (float)p[2].to_int() : 0.0f;
        set_clock_time(h + m / 60.0f + sc / 3600.0f);
    }
    String get_time_of_day() const {
        int total = (int)Math::round(clock_time * 3600.0f);
        int h = (total / 3600) % 24, m = (total % 3600) / 60, s = total % 60;
        return String::num_int64(h).pad_zeros(2) + ":" +
               String::num_int64(m).pad_zeros(2) + ":" +
               String::num_int64(s).pad_zeros(2);
    }
    void  set_minutes_after_midnight(float m) { set_clock_time(Math::fmod(Math::abs(m), 1440.0f) / 60.0f); }
    float get_minutes_after_midnight() const  { return clock_time * 60.0f; }

    // ── Ciclo día/noche automático ───────────────────────────────
    void set_day_night_cycle(bool v) {
        day_night_cycle = v;
        if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint())
            set_process(v);
    }
    bool get_day_night_cycle() const { return day_night_cycle; }
    void set_day_length_minutes(float v)  { day_length_minutes = Math::clamp(v, 0.5f, 120.0f); }
    float get_day_length_minutes() const  { return day_length_minutes; }
};

#endif // GL_LIGHTING_H

#ifndef GL_GRAPHICS_H
#define GL_GRAPHICS_H

// ════════════════════════════════════════════════════════════════════
//  Calidad gráfica estilo Roblox (1.15) — 10 niveles (1..10)
//  Un solo sitio que aplica TODO: escala de render, sombras (filtro soft
//  real, resolución del atlas, bias del sol), SSAO, glow y niebla. Lo usan
//  la iluminación por defecto del Workspace y el sistema de ajustes.
//
//  El problema de las sombras "con puntos" y "difuminado raro" era esto:
//  Godot por defecto usa el filtro de sombra soft en calidad BAJA (un
//  dithered que parece borrar píxeles) y el sol tenía un tamaño de penumbra
//  enorme (1.5) sin bias → acne + ruido. Aquí se sube el filtro a HIGH/ULTRA,
//  se ajusta el bias y se baja el tamaño para una sombra limpia y suave.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/rendering_server.hpp>
#include "gl_cast.h"
#include "gl_capas.h"   // capas y estado de las sombras propias (ui/gl_sombras.h)
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// Nivel de calidad efectivo actual (1..10). Lo lee quien necesite saberlo.
inline int& gl_graphics_level() { static int lvl = 8; return lvl; }

// ── V-Sync (sincronia vertical) ──────────────────────────────────────
// APAGADO por defecto, a proposito. Dos razones:
//   · Roblox NO ata los FPS a la tasa de refresco: su menu no tiene V-Sync, lo
//     unico que limita es el "Maximum Framerate" que elige el jugador.
//   · Godot deja V-Sync ACTIVADO por defecto, y con V-Sync el frame tiene que
//     caber en el intervalo del monitor: si no cabe, espera al vblank SIGUIENTE
//     y los FPS caen a la mitad exacta (medido: frames de 34,5 ms en un monitor
//     de 60 Hz = 30 FPS clavados). Ese era el "tope de 30 FPS".
// Quien prefiera imagen sin tearing lo enciende desde el menu de ajustes.
inline bool& gl_vsync_enabled() { static bool on = false; return on; }

// Aplica el V-Sync YA (el ajuste de proyecto solo se lee al arrancar, asi que no
// sirve para un toggle en vivo). `any` es cualquier nodo del arbol: de el se saca
// la ventana, que en el multijugador local no tiene por que ser la principal.
inline void gl_apply_vsync(bool on, Node* any = nullptr) {
    gl_vsync_enabled() = on;
    DisplayServer* ds = DisplayServer::get_singleton();
    if (!ds) return;
    int32_t wid = DisplayServer::MAIN_WINDOW_ID;
    if (any && any->is_inside_tree()) {
        if (Window* w = any->get_window()) wid = w->get_window_id();
    }
    ds->window_set_vsync_mode(on ? DisplayServer::VSYNC_ENABLED : DisplayServer::VSYNC_DISABLED, wid);
    // Se deja tambien en el ajuste de proyecto (en memoria, no se guarda) para que
    // quede coherente con lo que consulte cualquier otra parte del motor.
    if (ProjectSettings* ps = ProjectSettings::get_singleton())
        ps->set_setting("display/window/vsync/vsync_mode",
                        on ? (int)DisplayServer::VSYNC_ENABLED : (int)DisplayServer::VSYNC_DISABLED);
}

// ── Modo de calidad (como Roblox) ────────────────────────────────────
//   0 = Automatic — el motor sube/baja la calidad solo para sostener los FPS
//   1 = Manual    — un nivel fijo 1..10 (lo clasico)
//   2 = Custom    — cada ajuste por separado (resolucion/FSR, sombras, vista…)
enum GLQualityMode { GL_QUALITY_AUTOMATIC = 0, GL_QUALITY_MANUAL = 1, GL_QUALITY_CUSTOM = 2 };
// Por defecto AUTOMATIC, como Roblox: es lo que garantiza FPS jugables en
// cualquier maquina y mapa (baja la calidad sola cuando hace falta y la sube
// cuando sobra margen). Manual/Custom quedan para quien quiera mandar el.
inline int& gl_quality_mode() { static int m = GL_QUALITY_AUTOMATIC; return m; }

// FPS objetivo del modo Automatic (Roblox apunta a una experiencia fluida).
// 60, la referencia de Roblox. Antes estaba en 45 y el efecto era el contrario
// del buscado: el ajuste se estabiliza rondando el objetivo, asi que el juego se
// quedaba clavado en ~45 FPS y PARECIA que el motor imponia un tope.
inline int& gl_auto_target_fps() { static int f = 60; return f; }

// ── Suelo del modo Automatic ─────────────────────────────────────────
// El ajuste automatico NUNCA baja de aqui, aunque no llegue al objetivo. Por
// debajo del nivel 5 se apagan las sombras y el glow y la resolucion interna se
// queda al 50-70%: el juego deja de parecerse a Roblox (era la causa de "la
// iluminacion se ve pesima"). Mejor pocos FPS que un juego que no se reconoce.
// Suelo del modo Automatico. Estuvo en 5 ("sombras suaves + glow + niebla"),
// y en una grafica integrada eso deja el juego clavado a 30 FPS PARA SIEMPRE:
// el ajuste llega al suelo, sigue sin alcanzar el objetivo y se rinde. Medido
// en el place de prueba con una Intel UHD 770: suelo 5 -> 30 FPS; el mismo mapa
// a calidad 1 iba a 60. Automatico tiene que poder llegar donde haga falta —
// es lo que hace Roblox. Quien quiera fijar el aspecto usa Manual o Custom.
// CORRECCION (2026-08-10, medido): el suelo estuvo en 2, y el nivel 2 NO TIENE
// SOMBRAS — el modo Automatico acababa dejando el juego sin una sola sombra.
// El motivo de bajarlo era "con suelo 5 se queda clavado a 30 FPS", pero eso se
// midio cuando el cuello era la CPU (Players:GetPlayers tardaba 33 ms, ver
// gl_servicios.h). Con eso arreglado se vuelve a medir y bajar la calidad NO
// sube los FPS en esta maquina:
//     nivel 10 -> 30,2 FPS    nivel 7 -> 30,0 FPS    nivel 6 -> 30,0 FPS
// (lo que queda es coste de draw calls, que la calidad no toca). O sea que se
// estaban tirando las sombras a cambio de NADA. El suelo es el primer nivel
// con sombras: el 4 desde el 09/10 (el 3 las perdio para ir como Roblox).
static const int   GL_AUTO_MIN_LEVEL = 4;
static const float GL_AUTO_MIN_SCALE = 0.75f;  // resolucion interna minima del ajuste fino

// ── Limites de la distancia de vista ─────────────────────────────────
// (Definidos aqui arriba porque los usa el clamp de los ajustes Custom.
// El porque de cada numero esta explicado junto a la tabla, mas abajo.)
static const float GL_VIEW_INFINITA   = 16384.0f;
static const float GL_VIEW_INFINITA_2 = 32768.0f;
static const float GL_VIEW_MINIMA     = 512.0f;

// ── Ajustes del modo Custom ──────────────────────────────────────────
// Cada uno es independiente; asi el jugador puede, por ejemplo, bajar sombras
// pero mantener la resolucion nativa.
struct GLCustomSettings {
    float render_scale   = 1.0f;   // 0.25..1.0 (con FSR sube la nitidez al reescalar)
    int   upscaler       = 2;      // 0=Bilinear 1=FSR 2=FSR2
    float sharpness      = 0.5f;   // nitidez del FSR
    int   shadow_quality = 4;      // 0=off .. 5=ultra
    float view_distance  = 1.0f;   // 0.25..2.0 (multiplica el LOD/distancia de sombra)
    // Distancia de vista DIRECTA en studs (1 stud = 1 unidad de posicion).
    // Es lo que ve el jugador en el menu: "2048 studs" dice mas que "150%".
    // El multiplicador de arriba se queda solo para escalar la sombra.
    float view_studs     = 1024.0f;  // 512..32768
    bool  ssao           = false;  // Roblox no tiene AO de pantalla (ver la tabla de niveles)
    bool  glow           = true;
    bool  fog            = true;
    // ── Occlusion culling (no dibujar lo tapado por paredes) ──────────
    bool  occlusion      = false;  // apagado por defecto (ver gl_occlusion_enabled)
    // Transparencia por BORRADO DE PIXELES (alpha hash) en vez de mezcla real.
    // La transparencia de verdad obliga a la GPU a ordenar y mezclar cada pieza
    // con lo que hay detras; borrando pixeles se dibuja como si fuera opaca y
    // sale mucho mas barato (a cambio se ve un punteado fino de cerca).
    bool  pixel_transparency = false;
    // Agrupado de geometria estatica por zonas (chunks): dibuja en UNA llamada
    // todas las piezas ancladas iguales de una misma zona.
    bool  static_batching = true;
    float chunk_size      = 128.0f;
    // Unir las colisiones de cada zona en un cuerpo estatico. Apagado: medido que
    // empeora en mapas dispersos (ver gl_chunks.h).
    bool  merge_zone_physics = false;
    float occlusion_size = 8.0f;   // tamaño minimo (studs) para que una pieza tape
    int   occlusion_bvh  = 1;      // 0=rapido 1=equilibrado 2=preciso (coste CPU)
};
inline GLCustomSettings& gl_custom() { static GLCustomSettings c; return c; }

// Ajuste Custom por NOMBRE. Vive aqui (y no solo en el Workspace) porque hay dos
// clientes: la API de Lua (workspace:SetGraphicsSetting) y las filas del menu de
// ajustes; asi el mapeo nombre→campo y los limites estan escritos UNA vez.
// Devuelve false si el nombre no existe (para que quien llame no reaplique nada).
// Tabla de studs por nivel (definida mas abajo): tambien se toca por nombre,
// con "ViewDistance1".."ViewDistance10", para que el menu de ajustes no
// necesite fontaneria aparte.
inline void  gl_view_studs_set(int level, float studs);
inline float gl_view_studs(int level);

inline bool gl_custom_set(const String& name, float value) {
    GLCustomSettings& c = gl_custom();
    // ANTES de la rama por nivel: "ViewDistanceStuds" tambien empieza por
    // "ViewDistance", y si cayera alli substr(12)="Studs" -> nivel 0 -> se
    // tiraba el valor en silencio (el ajuste "no guardaba", medido en la sonda).
    if (name == "ViewDistanceStuds") { c.view_studs = CLAMP(value, GL_VIEW_MINIMA, GL_VIEW_INFINITA_2); return true; }
    if (name.begins_with("ViewDistance") && name.length() > 12) {
        const int lvl = name.substr(12).to_int();
        if (lvl >= 1 && lvl <= 10) { gl_view_studs_set(lvl, value); return true; }
        return false;
    }
    if      (name == "RenderScale")   c.render_scale   = CLAMP(value, 0.25f, 1.0f);
    else if (name == "Upscaler")      c.upscaler       = (int)CLAMP(value, 0.0f, 2.0f);
    else if (name == "Sharpness")     c.sharpness      = CLAMP(value, 0.0f, 2.0f);
    else if (name == "ShadowQuality") c.shadow_quality = (int)CLAMP(value, 0.0f, 5.0f);
    else if (name == "ViewDistance")  c.view_distance  = CLAMP(value, 0.25f, 2.0f);
    else if (name == "SSAO")          c.ssao           = value > 0.5f;
    else if (name == "Glow")          c.glow           = value > 0.5f;
    else if (name == "Fog")           c.fog            = value > 0.5f;
    else if (name == "Occlusion")     c.occlusion      = value > 0.5f;
    else if (name == "OcclusionSize") c.occlusion_size = CLAMP(value, 2.0f, 64.0f);
    else if (name == "OcclusionQuality") c.occlusion_bvh = (int)CLAMP(value, 0.0f, 2.0f);
    else if (name == "PixelTransparency") c.pixel_transparency = value > 0.5f;
    else if (name == "StaticBatching") c.static_batching = value > 0.5f;
    else if (name == "ChunkSize")     c.chunk_size     = CLAMP(value, 16.0f, 512.0f);
    else if (name == "MergeZonePhysics") c.merge_zone_physics = value > 0.5f;
    else return false;
    return true;
}
inline float gl_custom_get(const String& name) {
    const GLCustomSettings& c = gl_custom();
    if (name == "ViewDistanceStuds") return c.view_studs;   // antes de la rama por nivel (ver el set)
    if (name.begins_with("ViewDistance") && name.length() > 12) {
        const int lvl = name.substr(12).to_int();
        return (lvl >= 1 && lvl <= 10) ? gl_view_studs(lvl) : 0.0f;
    }
    if (name == "RenderScale")   return c.render_scale;
    if (name == "Upscaler")      return (float)c.upscaler;
    if (name == "Sharpness")     return c.sharpness;
    if (name == "ShadowQuality") return (float)c.shadow_quality;
    if (name == "ViewDistance")  return c.view_distance;
    if (name == "SSAO")          return c.ssao ? 1.0f : 0.0f;
    if (name == "Glow")          return c.glow ? 1.0f : 0.0f;
    if (name == "Fog")           return c.fog ? 1.0f : 0.0f;
    if (name == "Occlusion")        return c.occlusion ? 1.0f : 0.0f;
    if (name == "OcclusionSize")    return c.occlusion_size;
    if (name == "OcclusionQuality") return (float)c.occlusion_bvh;
    if (name == "PixelTransparency") return c.pixel_transparency ? 1.0f : 0.0f;
    if (name == "StaticBatching")    return c.static_batching ? 1.0f : 0.0f;
    if (name == "ChunkSize")         return c.chunk_size;
    if (name == "MergeZonePhysics")  return c.merge_zone_physics ? 1.0f : 0.0f;
    return 0.0f;
}

// ── DISTANCIA DE VISION POR NIVEL DE CALIDAD ─────────────────────────
// Cuantos studs alrededor del jugador se dibujan, por cada nivel 1..10. Es lo
// que gobierna el streaming (ver _stream_tick en roblox_workspace.h).
//
// A partir del nivel 8 la vision es "infinita": entre comillas porque de verdad
// son 16.384 studs. No hay infinito real posible — el radio se compara con una
// distancia al cuadrado, y ademas el propio motor recorta por el frustum y por
// el z-far de la camara. 16.384 es mas largo que cualquier place de Roblox
// (el de prueba mide 3.519 de zona jugable y 44.818 de esquina a esquina
// contando el escenario de fondo), asi que se comporta como si no hubiera
// limite: nunca se ve nada aparecer.
//
// Los diez valores los puede cambiar el jugador uno a uno (ViewDistance1..10
// en los ajustes, o workspace:SetViewDistance(nivel, studs)).
//
// GL_VIEW_INFINITA (16384) es el "infinito" del nivel 8. GL_VIEW_INFINITA_2
// (32768) es el de verdad, niveles 9 y 10: un stud = 1 unidad de posicion, o
// sea que el jugador ve hasta 32.768 unidades desde donde esta. Es tambien el
// TOPE ABSOLUTO de todo clamp de vista: mas alla el z-buffer pierde precision
// y las piezas lejanas empiezan a parpadear entre si (z-fighting).
// (Las constantes viven arriba, junto a los ajustes Custom que las usan.)

// SUELO DE 512 (GL_VIEW_MINIMA): ni en calidad 1 se ve menos de 512 studs. Es
// un minimo pedido a proposito — por debajo se nota el mundo apareciendo.

inline float* gl_view_studs_tabla() {
    //                  1     2     3      4      5      6      7      8       9/10
    static float T[10] = { 512, 640, 768, 1024, 1536, 2048, 3072,
                           GL_VIEW_INFINITA, GL_VIEW_INFINITA_2, GL_VIEW_INFINITA_2 };
    return T;
}
inline float gl_view_studs(int level) {
    return gl_view_studs_tabla()[CLAMP(level, 1, 10) - 1];
}
inline void gl_view_studs_set(int level, float studs) {
    gl_view_studs_tabla()[CLAMP(level, 1, 10) - 1] = CLAMP(studs, GL_VIEW_MINIMA, GL_VIEW_INFINITA_2);
}
inline bool gl_view_es_infinita(int level) { return gl_view_studs(level) >= GL_VIEW_INFINITA - 0.5f; }

// La distancia que toca AHORA MISMO.
//
// En CUSTOM el nivel no significa nada (el jugador eligio cada ajuste por
// separado), asi que ahi manda su propio mando: ViewDistanceStuds, en studs
// DIRECTOS (512..32768). Antes era un multiplicador 0.25..2.0 sobre 1024 y
// nadie sabia cuanto veia de verdad; el multiplicador se queda solo para
// escalar el alcance de la sombra.
inline float gl_view_studs_actual() {
    if (gl_quality_mode() == GL_QUALITY_CUSTOM)
        return CLAMP(gl_custom().view_studs, GL_VIEW_MINIMA, GL_VIEW_INFINITA_2);
    return gl_view_studs(gl_graphics_level());
}

// ── Hasta donde se dibuja el TEXTO de un cartel del mundo ────────────────
//
// MEDIDO en el place de prueba: de 2.299 llamadas de dibujo por frame, 1.837
// eran Label3D — el texto de los carteles reflejados al mundo. 48.240
// triangulos en total, o sea 24 triangulos por llamada: la GPU esta parada y
// la CPU se va entera en pedir dibujos.
//
// Se dibujaban TODOS porque el MaxDistance por defecto de Roblox es infinito.
// Pero habia una incoherencia: una pieza pequeña SI se recorta por distancia
// (ver _apply_lod en roblox_part.h), asi que el brainrot desaparecia a lo lejos
// y su cartel de precio seguia dibujandose sobre el vacio.
//
// Roblox tampoco dibuja texto hasta el infinito: su renderizador corta los
// carteles mucho antes, y a esa distancia el texto son dos pixeles ilegibles.
// MEDIDO en el place de prueba, distancia de la camara a cada Label3D visible:
//     mediana 4.721 studs   ·   p90 19.380   ·   maxima 23.111
//     a menos de 200 studs:  170 de 1.730  (10%)
//     a menos de 400 studs:  281 de 1.730  (16%)
//     a menos de 600 studs:  309 de 1.730  (18%)
// O sea que el 84% del texto que se dibujaba estaba tan lejos que no se veia.
//
// El tope es un cuarto de la vista elegida, entre 150 y 500 studs. A 500 studs
// un cartel de 4 studs de alto ocupa unos 5 pixeles: ya no se lee.
//
// SOLO se aplica cuando el juego NO dijo nada (MaxDistance infinito, que es el
// valor de fabrica de Roblox). Si el juego puso un MaxDistance a proposito, ese
// manda y no se toca: puede querer una flecha de objetivo visible a 2.000 studs.
inline float gl_wgui_dist_max() {
    const float vista = gl_view_studs_actual();
    // Con la vista en "infinita" (el nivel 8, que es el de fabrica) NO se puede
    // devolver "sin limite": el texto no es geometria. Una pieza a 4.000 studs
    // sigue siendo una silueta que se ve; un texto a 4.000 studs es medio pixel.
    // Por eso aqui hay un techo absoluto ademas del que marca la vista.
    const float base = (vista >= GL_VIEW_INFINITA - 0.5f) ? 4096.0f : vista;
    return CLAMP(base * 0.25f, 150.0f, 500.0f);
}

// Sube cada vez que la vista efectiva cambia. Cada pieza guarda la generacion
// con la que calculo su LOD; si no coincide, lo recalcula. Se lleva sola (no
// hay que acordarse de avisar desde cada setter de calidad, que es justo como
// se colaba el bug de "cambio la calidad y no pasa nada").
inline uint32_t gl_view_gen() {
    static uint32_t gen = 1;
    static float ultimo = -1.0f;
    const float v = gl_view_studs_actual();
    if (v != ultimo) { ultimo = v; gen++; }
    return gen;
}

// ── DEGRADADO DE LA SOMBRA: LOS DOS CAMINOS DE GODOT ─────────────────
// Godot tiene DOS formas de suavizar la sombra direccional, y son EXCLUYENTES.
// Cual se usa lo decide PARAM_SIZE (el tamano angular del sol):
//
//   PARAM_SIZE > 0  -> PCSS. Busca el bloqueador y abre la penumbra segun lo
//       LEJOS que este del suelo. Es lo fisicamente correcto... y es justo lo
//       que NO queremos aqui: el pie del personaje TOCA el suelo, asi que la
//       distancia es cero y la sombra sale de canto duro. Ademas muestrea con
//       un disco rotado al azar por pixel: si se agranda el angulo para que se
//       note, salen los puntitos que hervian al mover la camara.
//   PARAM_SIZE = 0  -> PCF. Promedia un disco FIJO de N texeles del mapa de
//       sombra, igual de ancho pegue o no pegue al suelo. Estable, sin ruido,
//       y es lo que se parece a Roblox (su ShadowSoftness tampoco endurece por
//       contacto).
//
// EL FALLO QUE HABIA: se ponia PARAM_SIZE = ShadowSoftness x 0.8. Con el 0.2
// que pide este place eso son 0,16 GRADOS -> tan(0,16) = 0,0028, o sea 0,014
// studs de penumbra a la altura de la cabeza. Invisible. Pero al ser > 0
// activaba el camino PCSS, y entonces el PARAM_SHADOW_BLUR que si daba
// degradado quedaba IGNORADO por el shader. De ahi "de graficos medios en
// adelante no se agrega degradado": no es que fuera poco, es que no se aplicaba.
//
// Ahora la palanca es el BLUR (radio del PCF, en texeles) y PARAM_SIZE se queda
// en 0. La calidad publica aqui el rango permitido y todo el que escriba pasa
// por gl_clamp_blur, porque el servicio Lighting escribe DESPUES que la calidad.
// Tres anclas, no dos: el valor de EN MEDIO es el aspecto por defecto y no se
// interpola desde los extremos. Ver gl_blur_desde_softness.
inline float& gl_blur_suelo()   { static float v = 1.0f; return v; }
inline float& gl_blur_defecto() { static float v = 2.0f; return v; }
inline float& gl_blur_tope()    { static float v = 4.0f; return v; }
inline float  gl_clamp_blur(float v) {
    return CLAMP(v, gl_blur_suelo(), gl_blur_tope());
}

// Lo ultimo que pidio el juego con Lighting.ShadowSoftness (0..1, Roblox trae
// 0.5). Lo publica el servicio; la calidad lo necesita para recolocar el blur
// dentro del rango nuevo cuando se cambia de nivel.
inline float& gl_shadow_softness_juego() { static float v = 0.5f; return v; }

// ShadowSoftness elige POSICION dentro del rango que permite la calidad, no un
// valor absoluto: asi nunca se cae fuera de lo que el filtro puede muestrear
// sin dejar pasos. Si el filtro no da para degradado, el rango es [0,0] y sale
// sombra dura, que es lo correcto en calidad baja.
//
// EL ANCLA ES 0.2, NO 0.5. Ese es el ShadowSoftness POR DEFECTO de Roblox (no
// el 0.5 que uno supondria de un 0..1), o sea el aspecto que tiene el 99% de
// los places — incluido el de prueba. Repartir el rango de forma lineal metia
// ese 0.2 al 20% del recorrido, casi pegado a la sombra dura: de ahi que "de
// graficos medios en adelante no se agregue degradado".
inline float gl_blur_desde_softness(float s) {
    const float t = CLAMP(s, 0.0f, 1.0f);
    const float DEF = 0.2f;
    return t <= DEF
        ? gl_blur_suelo()   + (gl_blur_defecto() - gl_blur_suelo())   * (t / DEF)
        : gl_blur_defecto() + (gl_blur_tope()    - gl_blur_defecto()) * ((t - DEF) / (1.0f - DEF));
}

// Busca el primer nodo de una clase bajo la raíz (sun / WorldEnvironment).
inline Node* gl_find_by_class(Node* n, const char* cls) {
    if (!n) return nullptr;
    if (n->is_class(cls)) return n;
    for (int i = 0; i < n->get_child_count(); i++)
        if (Node* r = gl_find_by_class(n->get_child(i), cls)) return r;
    return nullptr;
}

// ── CACHE DE LAS BUSQUEDAS POR CLASE ────────────────────────────────────────
//  gl_find_by_class es un descenso recursivo con is_class() por nodo, y cada
//  is_class cruza la frontera de la GDExtension. Un cambio de calidad hacia SEIS
//  de estas busquedas; con el place de prueba (37.511 nodos) eso son ~225.000
//  comprobaciones por cambio — y las tres que NO encuentran nada (BloomEffect,
//  SunRaysNode, AtmosphereNode) recorren el arbol ENTERO cada vez. Como la
//  calidad automatica reevalua cada 5 s y puede tocar algo en cada evaluacion,
//  salia un tiron periodico.
//
//  Se recuerda el ObjectID por clase (y tambien el "no existe", que es un hecho
//  igual de estable). Si el nodo muere, se vuelve a buscar.
inline Node* gl_buscar_clase_cache(Node* root, const char* cls) {
    struct Entrada { uint64_t id; bool buscado; };
    static HashMap<String, Entrada> cache;
    const String clave = String(cls);
    HashMap<String, Entrada>::Iterator it = cache.find(clave);
    if (it) {
        if (it->value.id != 0) {
            if (Node* n = gl_cast<Node>(ObjectDB::get_instance(ObjectID(it->value.id))))
                return n;                       // sigue vivo
        } else if (it->value.buscado) {
            return nullptr;                     // se busco y no habia
        }
    }
    Node* n = gl_find_by_class(root, cls);
    Entrada e;
    e.id = n ? (uint64_t)n->get_instance_id() : 0;
    e.buscado = true;
    cache[clave] = e;
    return n;
}

// Parámetros de calidad por nivel. Curva pensada para que del 1 al 10 haya un
// salto notable pero coherente (como los 1..10 de Roblox).
struct GLQualityDef {
    float render_scale;      // escala 3D del viewport
    int   shadow_filter;     // RenderingServer::ShadowQuality (0 hard .. 5 ultra)
    int   atlas_size;        // resolución del shadow map direccional
    bool  ssao;
    bool  glow;
    bool  fog;
    bool  shadows;
    // ── Lo que de verdad decide si una sombra se ve bien ──────────────
    // No es el "filtro": es cuantos TEXELES del mapa de sombra caen sobre cada
    // unidad del mundo. Y eso sale de dividir el atlas entre la distancia que
    // tiene que cubrir. Con UNA sola cascada (SHADOW_ORTHOGONAL) es directo:
    //
    //   texeles/unidad = atlas / dist
    //
    // Cuanto MAS corto el alcance, mas fina la sombra cercana — que es la que
    // se mira. El ancho del degradado se pide aparte, en texeles, junto al
    // filtro (ver RADIO_FILTRO abajo).
    float shadow_dist;       // alcance de la sombra direccional (unidades)
    float normal_bias;       // a mas resolucion, menos bias hace falta
};

inline GLQualityDef gl_quality_def(int level) {
    level = level < 1 ? 1 : (level > 10 ? 10 : level);
    // {escala, filtro, atlas, ssao, glow, fog, shadows, dist, bias}
    //
    // Densidad resultante (texeles por unidad = atlas / dist):
    //   niv 3: 2048/100 = 20     niv  7: 4096/170 = 24
    //   niv 4: 4096/120 = 34     niv  8: 4096/185 = 22
    //   niv 5: 4096/140 = 29     niv 10: 4096/205 = 20
    // De 4 a 10 la densidad baja poco a proposito: lo que crece es el ALCANCE
    // y el ancho del degradado, no la nitidez, porque a 20 texeles por stud la
    // silueta de un personaje (2 studs) ya se resuelve con 40 texeles.
    // TOPE DE 8192 EN EL ATLAS, a proposito. El mapa de sombra direccional es una
    // textura de profundidad cuadrada: 8192 son ~268 MB de VRAM, pero 16384 se
    // van a ~1 GB. En una grafica integrada (que tira de la RAM del sistema) eso
    // no es "mas calidad", es quedarse sin memoria. La nitidez de los niveles
    // altos se consigue con la DISTANCIA, que es gratis.
    // ── Los niveles 7-10 bajaron de atlas 8192 a 4096 (MEDIDO) ──────────
    // Con 8192 la sola GPU de la calidad 10 eran 37 ms en una UHD 770 — ni
    // con la CPU gratis se llega a 60 FPS. El "10" de Roblox usa mapas de
    // ~4096: nuestra "10" era MAS cara que la de Roblox sin verse mejor. La
    // densidad de texeles se conserva acortando el alcance a la par
    // (tope dist = atlas/12, ver el clamp de abajo): sombra igual de nitida
    // donde se mira, la mitad de relleno y de casters que redibujar.
    static const GLQualityDef T[10] = {
        // ── LA ESCALA DE RENDER ES 1.0 EN TODOS LOS NIVELES ──────────────
        // Roblox no rebaja la resolucion en ningun nivel: quita sombras,
        // efectos y distancia, y la imagen sigue nativa. Rebajarla difumina
        // todo lo del mundo (los Label3D de los carteles se emborronaban junto
        // al HUD nitido). Los niveles 1-2 iban al 65/80 % con FSR2, y FSR2
        // costaba 9,3 ms de GPU en una UHD 770: la calidad 1 iba a 68,7 FPS y
        // la 4 a 109 (medido 09/10 en Build a Mountain, 1457x711).
        //
        // ── CORTES DE ROBLOX (medidos en Studio el 09/10, Build a Mountain) ─
        // Fotos con settings().Rendering.QualityLevel en Play; nivel n de 1..10
        // ~ Level 2n de 21:
        //   sombras: no en Level04-06 (niveles 2-3), si desde Level08 (4);
        //   bloom y montanas lejanas: no hasta Level10 (5), si desde Level12 (6);
        //   bordes suavizados: no hasta Level14 (7), si en Level21 (10).
        // El bloom va en gl_calidad_bloom() y el FXAA en gl_aplicar_aa().
        {1.00f, 0, 1024, false, false, false, false, 80.0f, 1.4f},  // 1
        {1.00f, 0, 1024, false, false, false, false, 90.0f, 1.3f},  // 2
        {1.00f, 0, 1024, false, false, true,  false, 100.0f, 1.2f}, // 3
        // El 4 es el primero con sombras. Con el filtro 0 (UNA muestra) no hay
        // forma de tapar el acne (bandas por todo el suelo): filtro 2.
        // Atlas 2048 con la MISMA densidad que tenian con 4096 (34 y 29 texeles
        // por stud): la sombra cercana es identica y llega la mitad de lejos.
        // Medido en Build a Mountain (UHD 770): borrar y dibujar el mapa pasa
        // de 1,47 a 0,43 ms de GPU.
        {1.00f, 2, 2048, false, false, true,  true ,  60.0f, 1.1f}, // 4 degradado corto
        {1.00f, 3, 2048, false, false, true,  true ,  70.0f, 1.0f}, // 5
        // ── SIN SSAO EN NINGUN NIVEL (2026-10-08) ──────────────────────────
        // Roblox no tiene oclusion ambiental de pantalla en ningun nivel ni
        // tecnologia: la AO oscurecia rincones que en Roblox se ven limpios. Y
        // era cara: 1,5 ms de GPU en una UHD 770 a 720p (medido con
        // --gpu-profile en Build a Mountain). Queda como opcion en Custom.
        // ── FILTRO DE SOMBRA 3 EN LOS NIVELES ALTOS (2026-10-08) ───────────
        // El filtro 5 (ULTRA) muestrea el mapa de sombra decenas de veces por
        // pixel. Medido a 1920x1080 en una UHD 770: pasada opaca 10,1 ms con el
        // 5 y 4,6 ms con el 3 (con el 2, 4,3). El degradado por defecto (4
        // texeles) cabe de sobra en el 3; lo que se pierde es solo el tope de un
        // ShadowSoftness muy alto (8 texeles en vez de 16).
        {1.00f, 3, 4096, false, true,  true,  true , 150.0f, 0.9f}, // 6
        {1.00f, 3, 4096, false, true,  true,  true , 170.0f, 0.8f}, // 7
        {1.00f, 3, 4096, false, true,  true,  true , 185.0f, 0.7f}, // 8
        {1.00f, 3, 4096, false, true,  true,  true , 195.0f, 0.6f}, // 9
        {1.00f, 3, 4096, false, true,  true,  true , 205.0f, 0.5f}, // 10
    };
    return T[level - 1];
}

// Lo que la calidad deja encender, como en Roblox (cortes medidos: ver la tabla).
// En Custom decide el jugador.
inline bool gl_calidad_sombras() {
    if (gl_quality_mode() == GL_QUALITY_CUSTOM) return gl_custom().shadow_quality > 0;
    return gl_quality_def(gl_graphics_level()).shadows;
}
// BloomEffect del juego: sin halo en las calidades 1-5.
inline bool gl_calidad_bloom() {
    if (gl_quality_mode() == GL_QUALITY_CUSTOM) return gl_custom().glow;
    return gl_graphics_level() >= 6;
}

// workspace:SetAntiAliasing(0..4): 0=Off 1=FXAA 2=MSAA2x 3=MSAA4x 4=MSAA8x.
// El FXAA de serie solo desde la calidad 8: Roblox no suaviza los bordes hasta
// Level14 (calidad 7) y si en Level21. Cuesta ~0,6 ms de GPU en una UHD 770.
// El MSAA lo pide el juego a proposito y no depende de la calidad.
inline int& gl_aa_modo() { static int m = 1; return m; }
inline void gl_aplicar_aa(Viewport* vp) {
    if (!vp) return;
    const int m = gl_aa_modo();
    const bool fxaa = m == 1 && (gl_quality_mode() == GL_QUALITY_CUSTOM || gl_graphics_level() >= 8);
    vp->set_screen_space_aa(fxaa ? Viewport::SCREEN_SPACE_AA_FXAA : Viewport::SCREEN_SPACE_AA_DISABLED);
    Viewport::MSAA msaa = Viewport::MSAA_DISABLED;
    if (m == 2) msaa = Viewport::MSAA_2X;
    else if (m == 3) msaa = Viewport::MSAA_4X;
    else if (m == 4) msaa = Viewport::MSAA_8X;
    vp->set_msaa_3d(msaa);
}

// Aplica un nivel de calidad (1..10) a TODA la escena. `any` es cualquier nodo
// dentro del árbol (para llegar a la raíz, el viewport y el sol).
// ── Occlusion culling ────────────────────────────────────────────────
// Godot trae un occlusion culling POR SOFTWARE: cada frame rasteriza en la CPU
// las piezas marcadas como "occluder" y descarta lo que quede detras. Ojo al
// equilibrio que menciona todo el mundo: cuantos mas occluders y mas precisa la
// estructura (BVH), mejor se descarta pero mas CPU cuesta; con pocos, no tapa
// casi nada. Por eso NO se marcan todas las piezas: solo las grandes y opacas
// (paredes, suelos, edificios), que son las que de verdad tapan.
//
// Config efectiva por modo:
//   Automatic / Manual — se enciende en calidad media/alta con umbral segun el
//                        nivel (en calidad baja se apaga: la CPU ya va justa).
//   Custom             — lo decide el jugador (Occlusion, OcclusionSize,
//                        OcclusionQuality).
inline bool gl_occlusion_enabled(int level) {
    if (gl_quality_mode() == GL_QUALITY_CUSTOM) return gl_custom().occlusion;
    // MEDIDO en un mapa de 6.500 piezas: encenderlo baja de 17.3 a 11.4 FPS
    // (-34%). El rasterizado por software en la CPU cuesta mas de lo que ahorra
    // cuando la geometria son miles de piezas sueltas y pequeñas (el caso de un
    // place de Roblox). Rinde cuando hay POCOS muros grandes que tapan mucho.
    // Por eso en Automatic/Manual viene APAGADO y se deja como opcion en Custom.
    (void)level;
    return false;
}
inline float gl_occlusion_min_size(int level) {
    if (gl_quality_mode() == GL_QUALITY_CUSTOM)
        return CLAMP(gl_custom().occlusion_size, 2.0f, 64.0f);
    // Niveles bajos: solo piezas MUY grandes tapan (pocos occluders = poca CPU).
    return level >= 8 ? 6.0f : (level >= 6 ? 10.0f : 16.0f);
}
inline void gl_apply_occlusion_settings(int level, Node* any = nullptr) {
    const bool on = gl_occlusion_enabled(level);
    // CLAVE: el occlusion culling se activa POR VIEWPORT. Tocar solo el ajuste de
    // proyecto no hace nada en una sesion ya arrancada (solo fija el valor por
    // defecto), que es justo por lo que "no se notaba" al principio.
    if (any && any->is_inside_tree()) {
        if (Viewport* vp = any->get_viewport()) vp->set_use_occlusion_culling(on);
    }
    if (ProjectSettings* ps = ProjectSettings::get_singleton()) {
        ps->set_setting("rendering/occlusion_culling/use_occlusion_culling", on);
        int bvh = (gl_quality_mode() == GL_QUALITY_CUSTOM)
                      ? CLAMP(gl_custom().occlusion_bvh, 0, 2)
                      : (level >= 8 ? 2 : (level >= 5 ? 1 : 0));
        ps->set_setting("rendering/occlusion_culling/bvh_build_quality", bvh);
    }
}

inline void gl_apply_graphics_quality(int level, Node* any) {
    level = level < 1 ? 1 : (level > 10 ? 10 : level);
    gl_graphics_level() = level;
    GLQualityDef q = gl_quality_def(level);

    // En Custom cada ajuste lo decide el jugador, no la curva del nivel.
    if (gl_quality_mode() == GL_QUALITY_CUSTOM) {
        const GLCustomSettings& c = gl_custom();
        q.render_scale  = c.render_scale;
        q.shadow_filter = CLAMP(c.shadow_quality, 0, 5);
        q.shadows       = c.shadow_quality > 0;
        q.ssao          = c.ssao;
        q.glow          = c.glow;
        q.fog           = c.fog;
        // Tope 4096 tambien en Custom (MEDIDO: con 8192 la GPU de las sombras
        // sola eran ~18 ms extra en la UHD 770; la tabla de niveles ya bajo a
        // 4096 y aqui seguia el 8192 colado).
        q.atlas_size    = c.shadow_quality >= 2 ? 4096 : 2048;
        // En Custom, la fila "Shadow Quality" tiene que mandar TAMBIEN en el
        // alcance y el bias. Antes esos dos seguian saliendo de la tabla del
        // NIVEL, que en Custom no significa nada (puede haberse quedado en 1),
        // asi que poner las sombras en Ultra no cambiaba nada mas que el filtro.
        // El ancho del degradado no va aqui: sale del filtro, igual que en los
        // niveles fijos.
        static const float DIST_CUSTOM[6] = { 120, 150, 220, 300, 420, 600 };
        static const float BIAS_CUSTOM[6] = { 1.4f, 1.3f, 1.1f, 0.9f, 0.7f, 0.5f };
        const int sq = CLAMP(c.shadow_quality, 0, 5);
        q.shadow_dist   = DIST_CUSTOM[sq];
        q.normal_bias   = BIAS_CUSTOM[sq];
    }

    gl_apply_occlusion_settings(level, any);

    RenderingServer* rs = RenderingServer::get_singleton();
    if (rs) {
        // El filtro soft REAL: esto es lo que quita el "dithered" y da una
        // penumbra suave de verdad en vez de píxeles sueltos.
        rs->directional_soft_shadow_filter_set_quality((RenderingServer::ShadowQuality)q.shadow_filter);
        rs->positional_soft_shadow_filter_set_quality((RenderingServer::ShadowQuality)q.shadow_filter);
        // Profundidad de 16 bits: la mitad de memoria que borrar y escribir cada
        // frame, con la misma resolucion. Medido (UHD 770, atlas 4096): preparar
        // 0,97 -> 0,58 ms y dibujar 1,24 -> 0,80 ms de GPU.
        rs->directional_shadow_atlas_set_size(q.atlas_size, true);
        // Glow con reescalado LINEAL: el bicubico muestrea cada nivel varias
        // veces por pixel al componerlo en el tonemap (1,72 -> 0,99 ms a 720p)
        // y en un bloom de Roblox (radio de decenas de pixeles) no se nota.
        rs->environment_glow_set_use_bicubic_upscale(false);
    }

    if (!any || !any->is_inside_tree()) return;
    Node* root = (Node*)any->get_tree()->get_root();

    // ── Escala de render 3D + reescalado ─────────────────────────────────
    // En Custom manda lo que haya elegido el jugador (tambien FSR2). Fuera de
    // Custom la escala es 1.0 (ver la tabla); si alguna vez baja, FSR1 y no
    // FSR2: en una integrada FSR2 cuesta mas de lo que ahorra (9,3 ms de GPU
    // a 1457x711 en una UHD 770, con el 3D al 65 %).
    if (Viewport* vp = any->get_viewport()) {
        const bool custom = (gl_quality_mode() == GL_QUALITY_CUSTOM);
        const float scale = custom ? gl_custom().render_scale : q.render_scale;
        vp->set_scaling_3d_scale(scale);
        int up = custom ? gl_custom().upscaler : (scale < 0.99f ? 1 : 0);
        vp->set_scaling_3d_mode((Viewport::Scaling3DMode)CLAMP(up, 0, 2));
        vp->set_fsr_sharpness(custom ? gl_custom().sharpness : 0.5f);
        gl_aplicar_aa(vp);
    }

    // Sol: sombra on/off, tamaño de penumbra y bias que evita el acne.
    // Si existe el servicio Lighting, EL manda sobre GlobalShadows y ShadowSoftness
    // (como en Roblox), pero la calidad puede quitarlas: Roblox no dibuja
    // sombras en las calidades 1-3 aunque GlobalShadows este puesto.
    Node* lighting = gl_buscar_clase_cache(root, "Lighting");
    const bool has_lighting = lighting != nullptr;
    if (Node* sn = gl_buscar_clase_cache(root, "DirectionalLight3D")) {
        if (DirectionalLight3D* dl = gl_cast<DirectionalLight3D>(sn)) {
            static const StringName k_gs("GlobalShadows");
            const bool del_juego = has_lighting ? (bool)lighting->get(k_gs) : true;
            // Con las sombras propias pintando (ui/gl_sombras.h), la de Godot no.
            dl->set_shadow(del_juego && gl_calidad_sombras() && !gl_sombras_propias_listas());
            // SIEMPRE 0: es lo que elige el camino PCF (degradado de ancho
            // fijo) en vez del PCSS, que con el sol casi puntual de Roblox
            // deja la sombra dura. Ver el bloque de gl_clamp_blur arriba.
            dl->set_param(Light3D::PARAM_SIZE, 0.0f);
            // Bias: suficiente para matar el acne (los "puntos") sin peter-panning.
            // Con el pancake apagado (ver abajo) hay menos precision de
            // profundidad, asi que se usan los valores por defecto de Godot, que
            // dejan mas margen. Medido: con pancake 0 el suelo sale igual de
            // limpio con 0.06/1.0 que con 0.10/2.0, y estos aguantan mejor.
            // El bias va con la RESOLUCION: cuanto mas fino el mapa de sombra,
            // menos hay que separarla para evitar el acne — y menos se despega
            // del objeto (ese "peter-panning" que hace que la sombra parezca
            // pegada a otro sitio). Con 2.0 fijo y poca resolucion la sombra
            // salia flotando y con el borde a escalones.
            dl->set_param(Light3D::PARAM_SHADOW_NORMAL_BIAS, q.normal_bias);
            dl->set_param(Light3D::PARAM_SHADOW_BIAS, 0.06f);
            // ── CUANTO OSCURECE LA SOMBRA ────────────────────────────
            // En Roblox una sombra no apaga la superficie: la baja un escalon
            // y se sigue viendo el color. En Godot, por defecto, la sombra
            // quita TODA la luz del sol y lo que queda es solo el ambiente —
            // con un place que pone Ambient 0.27 y OutdoorAmbient NEGRO (este),
            // lo techado se va practicamente a negro.
            //
            // El error de los tres intentos anteriores fue tocar el AMBIENTE
            // para arreglarlo. Medido aqui con capturas y muestreo de pixeles:
            // subir el ambiente aclara MAS lo iluminado que lo sombreado (se
            // suma a todo), asi que lavaba la escena sin rescatar la sombra.
            //   ambiente 0.27 -> 0.50 : sombra 0.09 -> 0.12   sol 0.16 -> 0.31
            //   opacidad 1.00 -> 0.75 : sombra 0.21 -> 0.36   sol casi igual
            // Esta es la palanca que toca: afecta SOLO a lo que esta en sombra.
            // ACTUALIZADO 2026-10-08, ya con el ambiente MEDIDO (ver
            // Lighting::_apply_env): el 0.75 tapaba que el ambiente estaba mal.
            // En Roblox, bajo un techo y con solo sol, llega un 2,5 % del sol
            // (carta de grises en Play): la sombra es casi entera y lo que se ve
            // en ella es el Ambient/OutdoorAmbient y el cielo. Con 0.75 el
            // tablero en sombra salia +35 niveles mas claro que en Roblox.
            dl->set_param(Light3D::PARAM_SHADOW_OPACITY, 0.975f);

            // ── Sombras estables (v1.17) ─────────────────────────────
            // Sintomas que arregla, tal cual se veian: trozos de una misma
            // sombra que aparecian o desaparecian AL GIRAR LA CAMARA, y bordes
            // con pixeles que "se borran".
            //
            //  · Las cascadas (4 mapas de sombra por distancia) cambiaban de
            //    una a otra con un corte seco. Al girar, un objeto saltaba de
            //    cascada y su sombra aparecia/desaparecia de golpe.
            //    set_blend_splits mezcla el paso de una a la siguiente.
            //  · La penumbra suave de Godot se muestrea de forma aleatoria: con
            //    un angulo grande y pocas muestras salen esos puntitos que
            //    parpadean. Se acota el angulo al filtro que hay de verdad.
            // ── UNA SOLA CASCADA ────────────────────────────────────
            // Con 4 cascadas hay TRES fronteras donde la sombra cambia de
            // resolucion. Al andar o girar, los objetos cruzan esas fronteras y
            // la sombra cambia delante de ti: es el "las sombras nunca se ponen
            // de acuerdo / dependen de lo cerca que este la camara".
            // Con una sola, la densidad es la MISMA en todo el alcance y no hay
            // nada que cruzar. Medido en el place (banco de sombras, mismo
            // encuadre): mismos FPS que con 4 (30,0) y 190 draw calls MENOS
            // (482 -> 292), porque cada objeto se dibuja una vez en el mapa de
            // sombra en lugar de hasta cuatro.
            //   4 cascadas 340 .......... 30,0 FPS  482 draws
            //   1 cascada  120 .......... 30,0 FPS  292 draws
            //   sin sombras ............. 36,6 FPS  229 draws
            // El alcance baja (ver la tabla) porque ahora TODO el mapa de
            // sombra se reparte en esa distancia: 4096 texeles / 120 studs = 34
            // por stud, que es mas nitido de lo que era la primera cascada.
            dl->set_shadow_mode(DirectionalLight3D::SHADOW_ORTHOGONAL);
            dl->set_blend_splits(false);
            dl->set_param(Light3D::PARAM_SHADOW_FADE_START, 0.9f);
            // ── DEGRADADO DE LA SOMBRA SEGUN LA CALIDAD ─────────────────
            // Godot suaviza el borde PROMEDIANDO varios puntos del mapa de
            // sombra. Con pocas muestras no sale un degradado: salen PUNTITOS
            // (agujeros dentro de la sombra, que hierven al mover la camara).
            // O sea que lo que uno puede abrir el degradado depende de cuantas
            // muestras haya, y eso lo fija el filtro (1/4/8/16/32/64).
            //
            // ── SE PIDE EN TEXELES, NO EN "BLUR" ────────────────────────
            // Godot ya multiplica el blur por un radio propio de cada filtro
            // (1.0 / 1.0 / 1.5 / 2.0 / 3.0 / 4.0 de HARD a ULTRA), asi que el
            // mismo blur da un degradado MUY distinto segun la calidad. Aqui se
            // pide directamente el ancho final en texeles del mapa de sombra y
            // se divide entre ese radio: asi la tabla dice lo que se ve.
            //
            // Cuantos texeles se puede permitir cada filtro: el promedio se
            // reparte entre las muestras que haya (1/4/8/16/32/64), y abrir el
            // disco mas de la cuenta con pocas muestras es lo que sacaba los
            // "pasos" dentro de la sombra.
            static const float RADIO_FILTRO[6] = { 1.0f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
            const int   filtro   = CLAMP(q.shadow_filter, 0, 5);
            const float radio_q  = RADIO_FILTRO[filtro];
            // ── LA CALIDAD NO ENSANCHA EL DEGRADADO ─────────────────────
            // Error del primer intento: dar mas texeles a mas calidad. Medido
            // con capturas del personaje al sol: 3,8 texeles (calidad 5) deja
            // ver brazos y piernas en la sombra; 5,8 (calidad 10) la convierte
            // en un manchon donde no se distingue la silueta. La calidad tiene
            // que MUESTREAR MEJOR el mismo ancho, no dibujar otro aspecto — que
            // ademas seria raro: subir los graficos no deberia cambiar el look.
            // Asi que el valor por defecto es el mismo para todos los filtros y
            // lo unico que crece con las muestras es hasta donde puede llegar
            // ShadowSoftness sin dejar pasos.
            //
            // ── EL SUELO DE 2 TEXELES NO ES ESTETICA: ES EL ANTIACNE ────
            // Un mapa de sombra guarda la profundidad por texeles, asi que en
            // una superficie inclinada el texel entero se compara con UN solo
            // valor: media superficie sale "en sombra" y media no, y aparecen
            // BANDAS por todo el suelo (el "acne"). El bias las reduce pero no
            // las quita; lo que las borra es promediar varios texeles.
            // Medido en el place: con blur 0 salen bandas diagonales por todo
            // el suelo en cuanto la camara mira rasante; con 2,5 texeles
            // desaparecen. Por eso el suelo del rango NUNCA baja de ~2,5 en
            // ningun filtro que sepa promediar.
            //
            // Filtro 0 (HARD) es el unico que no puede: Godot toma UNA muestra
            // y el blur ni se lee. Ahi no hay antiacne posible — por eso la
            // tabla de niveles ya no usa el filtro 0 con las sombras
            // encendidas (el primero con sombras, el 4, usa el 2), y en Custom ShadowQuality 0
            // significa sombras APAGADAS.
            const float tex_suelo = (filtro <= 0) ? 0.0f : 2.5f;
            const float tex_def   = (filtro <= 0) ? 0.0f
                                  : (filtro == 1) ? 2.5f : 4.0f;
            const float tex_tope  = (filtro <= 0) ? 0.0f
                                  : (filtro == 1) ?  2.5f   // solo antiacne, sin degradado
                                  : (filtro == 2) ?  6.0f
                                  : (filtro == 3) ?  8.0f
                                  : (filtro == 4) ? 12.0f : 16.0f;
            // La calidad manda un RANGO, no un valor: [suelo, tope]. Dentro
            // sigue decidiendo el juego con su ShadowSoftness, pero ya no puede
            // quedarse tan corto que no se vea degradado — que es exactamente lo
            // que pasaba: 0,16 grados de sol es una sombra dura, y encima
            // activaba el PCSS y anulaba el blur.
            gl_blur_suelo()   = tex_suelo / radio_q;
            gl_blur_defecto() = tex_def   / radio_q;
            gl_blur_tope()    = tex_tope  / radio_q;
            // Se reaplica por si el Lighting escribio antes que esta pasada
            // (al cambiar de calidad el rango cambia y el valor de antes se
            // queda fuera). Si no hay Lighting, se parte del 0.5 de Roblox.
            const float soft = has_lighting
                ? gl_shadow_softness_juego()
                : 0.5f;
            dl->set_param(Light3D::PARAM_SHADOW_BLUR, gl_blur_desde_softness(soft));
            // Rango de sombra: cuanto mas corto, menos mundo hay que redibujar en
            // el mapa de sombras cada frame. En Custom lo escala ViewDistance.
            // 200 unidades eran MUY poco: un personaje de Roblox mide 5, asi que
            // las sombras se cortaban a 40 alturas de personaje y el mapa se veia
            // plano de medio campo hacia fuera. Se sube a 900 (y 450 en calidad
            // baja). Lo que hacia inviable subirlo era el manchon negro, y eso ya
            // no depende del alcance sino del recorte de casters lejanos
            // (ver gl_recortar_sombras_lejanas en roblox_workspace.h).
            // Alcance por NIVEL (ver la tabla): es la otra mitad de la densidad
            // de texeles. Acortarlo es lo que hace que la sombra cercana — la
            // que se mira — tenga detalle de verdad.
            float sdist = q.shadow_dist;
            if (gl_quality_mode() == GL_QUALITY_CUSTOM)
                sdist = q.shadow_dist * CLAMP(gl_custom().view_distance, 0.25f, 2.0f);
            // ── TOPE POR DENSIDAD DE TEXELES ─────────────────────────────
            // Lo de arriba es la INTENCION; esto es la garantia. Medido en el
            // proyecto de prueba, el alcance acababa en 2000 aunque la tabla
            // pedia 700: en Custom el multiplicador de ViewDistance estira el
            // valor, y ademas cualquier otro sitio que escriba esta propiedad
            // (o el propio valor por defecto de Godot, si esta rama no llega a
            // correr) manda por encima de la tabla. Con 2000 y atlas 4096 salen
            // 34 texeles por unidad: de ahi los escalones que se veian "en
            // ultra".
            //
            // Despejando de la formula de la cabecera de GLQualityDef:
            //   texeles/unidad = (atlas/2) / (dist * 0.06)   >= objetivo
            //   dist <= atlas / (2 * 0.06 * objetivo) = atlas / 12   (objetivo 100)
            //
            // O sea: 682 unidades con atlas 8192, 341 con 4096. Se queda el
            // MENOR de los dos, asi que la calidad puede pedir menos alcance
            // pero nunca puede pedir uno que deje la sombra a escalones.
            // ── TOPE RECALCULADO PARA UNA CASCADA ────────────────────
            // El atlas/12 salia de la formula de CUATRO cascadas, donde la
            // primera cubria el 6% del alcance:
            //     texeles/unidad = (atlas/2) / (dist * 0.06)
            // Con una sola cascada el mapa entero cubre TODO el alcance:
            //     texeles/unidad = atlas / dist
            // O sea que el atlas/12 dejaba solo 12 texeles por stud (medido en
            // el place: alcance 341 con atlas 4096). Para que la sombra de un
            // personaje de ~5 studs tenga forma de personaje y no de mancha
            // hacen falta 20 o mas: dist <= atlas / 20.
            const float tope_dist = (float)q.atlas_size / 20.0f;
            if (sdist > tope_dist) sdist = tope_dist;
            dl->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, sdist);

            // ── PANCAKE A 0: red de seguridad, NO la causa ───────────────
            // CORRECCION de lo que puse aqui antes. Yo habia culpado al pancake
            // del manchon negro que tapaba media pantalla. Se equivocaba: la
            // causa real son los CASTERS LEJANISIMOS estirando el rango de
            // profundidad del mapa de sombras (ver gl_recortar_sombras_lejanas
            // en roblox_workspace.h, con las medidas).
            //
            // Lo que el pancake si cambia es COMO FALLA cuando eso pasa. Medido
            // (brillo medio del suelo 0-255 / cuanta sombra hay):
            //
            //   SIN recorte:  pancake 0  -> 154,3 / 8,1   (se pierde sombra)
            //                 pancake>=1 ->  33,3 / 129   (pantalla negra)
            //   CON recorte:  pancake 0, 4 y 20 dan lo MISMO (154,3 / 8,2)
            //
            // O sea: con el recorte puesto da igual el valor. Se deja en 0
            // porque si algun place se escapa del recorte, perder algo de sombra
            // es mucho menos grave que quedarse la pantalla en negro.
            dl->set_param(Light3D::PARAM_SHADOW_PANCAKE_SIZE, 0.0f);
        }
    }

    // Environment: SSAO / glow / niebla según el nivel. PERO glow/niebla NO se
    // tocan si hay un efecto de Lighting que los gobierna (Bloom/SunRays → glow,
    // Atmosphere → niebla): asi un efecto con Enabled=false no se reactiva al
    // aplicar la calidad. SSAO no lo controla ningun efecto de Roblox, va libre.
    if (Node* wen = gl_buscar_clase_cache(root, "WorldEnvironment")) {
        if (WorldEnvironment* we = gl_cast<WorldEnvironment>(wen)) {
            Ref<Environment> env = we->get_environment();
            if (env.is_valid()) {
                const bool has_glow_fx = gl_buscar_clase_cache(root, "BloomEffect") ||
                                         gl_buscar_clase_cache(root, "SunRaysNode");
                const bool has_fog_fx  = gl_buscar_clase_cache(root, "AtmosphereNode");
                env->set_ssao_enabled(q.ssao);
                // Oclusion SUAVE. Este es el ULTIMO que escribe, asi que si no
                // se pone aqui manda lo que traiga el .tres del proyecto (1.5),
                // que ennegrece todo lo techado. Roblox tiene AO, pero discreta.
                env->set_ssao_intensity(0.5f);
                env->set_ssao_power(1.0f);
                if (!has_glow_fx) env->set_glow_enabled(q.glow);
                if (!has_fog_fx)  env->set_fog_enabled(q.fog);
                // El BloomEffect del juego mira la calidad (gl_calidad_bloom).
                static const StringName k_reap("_gl_reaplicar");
                if (Node* bloom = gl_buscar_clase_cache(root, "BloomEffect"))
                    if (bloom->has_method(k_reap)) bloom->call(k_reap);
            }
        }
    }
}

// ── Modo Automatic ───────────────────────────────────────────────────
// Como el "Automatic" de Roblox: vigila los FPS y sube o baja el nivel de
// calidad solo, buscando mantenerse por encima del objetivo (60 FPS por
// defecto). Cambia de UN nivel cada vez y espera entre ajustes para no ir
// oscilando arriba y abajo. Llamar cada frame; no hace nada en otros modos.
//
// IMPORTANTE: esto ajusta CALIDAD, nunca FPS. No limita nada: si la maquina da
// 300 FPS con todo al maximo, se queda en el maximo y no vuelve a tocar nada (el
// tope de FPS lo pone solo el jugador con "Max FPS"). Y tampoco baja de
// GL_AUTO_MIN_LEVEL / GL_AUTO_MIN_SCALE: si ni con el suelo se llega al objetivo,
// se aceptan menos FPS antes que dejar el juego sin sombras y borroso.
inline void gl_auto_quality_tick(double delta, Node* any) {
    if (gl_quality_mode() != GL_QUALITY_AUTOMATIC || !any) return;
    static double acc = 0.0, cooldown = 0.0;
    static int frames = 0;

    if (delta > 0.0) frames++;
    acc += delta;
    if (cooldown > 0.0) cooldown -= delta;
    if (acc < 5.0 || frames < 10) return;      // evaluar cada ~5 segundos

    // ── FPS = fotogramas / tiempo ────────────────────────────────────────
    // Antes se hacia la media de 1/delta, y eso NO es los FPS: es la media de
    // las velocidades instantaneas, que la dominan los frames cortos. Justo al
    // cambiar la escala 3D sale un frame de ~1 ms (el viewport reasigna sus
    // render targets y se salta el dibujado), y ese solo frame cuenta como
    // 1000 FPS: en una ventana de 30 frames a 30 FPS la media se iba a 62.
    // O sea que el propio tiron convencia al ajuste de que sobraban FPS, subia
    // la calidad, volvia a faltar, la bajaba... Medido en el place de prueba:
    // 27 cambios de calidad/escala en 60 s, uno cada 2,2 s, cada uno un tiron.
    const double fps = (double)frames / acc;
    acc = 0.0; frames = 0;
    if (cooldown > 0.0) return;

    const int target = gl_auto_target_fps();
    // Banda de histeresis alrededor del objetivo: con el objetivo por defecto
    // (60) da exactamente 35/65 — por debajo de 35 se baja calidad, por encima
    // de 65 se sube, y ENTRE medias no se toca nada. Sin banda, el ajuste
    // oscilaba (bajar -> sobran FPS -> subir -> faltan -> bajar...), un tiron
    // por cada cambio.
    const double fps_bajo = (double)(target * 7) / 12.0;
    const double fps_alto = (double)(target * 13) / 12.0;
    const int lvl = gl_graphics_level();
    Viewport* vp = any->is_inside_tree() ? any->get_viewport() : nullptr;
    // La escala se LEE del viewport en vez de guardarla aparte: asi no se queda
    // desfasada cuando el jugador pasa por Manual/Custom y vuelve a Automatic.
    float auto_scale = vp ? vp->get_scaling_3d_scale() : 1.0f;

    // Si se entra desde Manual con un nivel por debajo del suelo, se sube al suelo.
    if (lvl < GL_AUTO_MIN_LEVEL) {
        gl_apply_graphics_quality(GL_AUTO_MIN_LEVEL, any);
        cooldown = 2.0;
        return;
    }
    // Techo alcanzado (nivel 10, resolucion nativa y FPS de sobra): NADA que hacer.
    // Sin este corte se seguia entrando cada segundo a "subir" lo que ya estaba
    // arriba, y era lo que hacia parecer que el motor sujetaba los FPS.
    if (lvl >= 10 && auto_scale >= 0.999f && fps >= (double)target) return;

    if (fps < fps_bajo) {
        // Primero se ajusta la RESOLUCION en pasos pequeños (5%), que apenas se
        // nota; solo cuando se agota ese margen se baja el nivel entero.
        // ── POR QUE BILINEAR Y NO FSR2 ───────────────────────────────
        //
        // Aqui estaba SCALING_3D_MODE_FSR2, y era EL PARON GRANDE del juego.
        //
        // Medido: en 300 s de partida, el UNICO frame por encima de 300 ms
        // caia exactamente en el instante en que este ajuste cambiaba el modo
        // de escalado de 0 (bilineal) a 2 (FSR2):
        //
        //     [CALIDAD] t=49,5 s  escala 1,000 -> 0,950   modo 0 -> 2
        //     frame en t=50 s: 769 ms, de los cuales 749 FUERA de _process
        //
        // Repetido tres veces con el mismo resultado, y el paron SIGUE al
        // cambio de modo: al retrasarlo a t=87,6 s, el frame de 725 ms se fue
        // con el. Los cambios de escala POSTERIORES (0,95 -> 0,90 -> 0,85...)
        // no cuestan nada: solo se paga el PRIMER cambio de modo, que reasigna
        // todos los destinos de render del viewport y monta el contexto de
        // FSR2. En headless (sin dibujar) el paron no existe: el peor frame de
        // 300 s son 273 ms.
        //
        // Y era absurdo por donde se mire: este ajuste existe para GANAR FPS y
        // se gastaba 750 ms de golpe en conseguirlo — 45 frames perdidos que no
        // recupera en un minuto de juego. Roblox no hace nada parecido.
        //
        // Bilineal es el modo de fabrica: no monta contexto, no reserva nada y
        // no compila shaders, asi que cambiar de escala es gratis. Se ve algo
        // mas blando al reescalar que FSR2, y a cambio no hay congelacion.
        // FSR2 sigue disponible para quien lo quiera desde la calidad Custom,
        // donde es el jugador el que decide y no un ajuste automatico.
        if (auto_scale > GL_AUTO_MIN_SCALE + 0.001f) {
            auto_scale = Math::max(GL_AUTO_MIN_SCALE, auto_scale - 0.05f);
            if (vp) { vp->set_scaling_3d_scale(auto_scale);
                      vp->set_scaling_3d_mode(Viewport::SCALING_3D_MODE_BILINEAR); }
            cooldown = 1.0;
        } else if (lvl > GL_AUTO_MIN_LEVEL) {
            // Al bajar de nivel se CONSERVA la escala que tanto costo encontrar.
            // Antes el nivel nuevo la reponia a la suya (1.0) y habia que volver
            // a bajarla de 0,05 en 0,05: una escalera infinita 1.00→0.75, bajar
            // nivel, 1.00→0.75, bajar nivel... con su tiron en cada peldaño.
            const float guardada = auto_scale;
            gl_apply_graphics_quality(lvl - 1, any);
            if (vp && guardada < 0.999f) {
                vp->set_scaling_3d_scale(guardada);
                vp->set_scaling_3d_mode(Viewport::SCALING_3D_MODE_BILINEAR);  // ver arriba
            }
            cooldown = 6.0;   // bajar de nivel se nota: no encadenar bajadas
        }
        // En el suelo: no se toca nada mas. Se aceptan los FPS que salgan.
    } else if (fps > fps_alto) {
        if (auto_scale < 0.999f) {
            auto_scale = Math::min(1.0f, auto_scale + 0.05f);
            if (vp) vp->set_scaling_3d_scale(auto_scale);
            cooldown = 6.0;
        } else if (lvl < 10) {
            gl_apply_graphics_quality(lvl + 1, any);
            cooldown = 10.0;                     // subir con MUCHA prudencia
        }
    }
}

#endif // GL_GRAPHICS_H

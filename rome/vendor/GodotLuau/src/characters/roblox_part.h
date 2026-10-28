#ifndef ROBLOX_PART_H
#define ROBLOX_PART_H

#include <godot_cpp/classes/os.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/rendering_server.hpp>   // dibujar sin nodo hijo
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/area3d.hpp>
#include <godot_cpp/classes/convex_polygon_shape3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/occluder_instance3d.hpp>
#include <godot_cpp/classes/box_occluder3d.hpp>
#include "gl_graphics.h"
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/sphere_shape3d.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/cylinder_shape3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/physics_material.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "lua.h"
#include "lualib.h"
#include "gl_errors.h"

#include "gl_runtime.h"   // gl_state_alive, GodotObjectWrapper, gow_set
#include "gl_clone.h"   // Clone() rapido + marca de hijos internos
#include "gl_stream.h"  // registro plano para el streaming por distancia
#include "gl_servicios.h" // gl_purgar_cache: los caches de recursos tienen tope
#include "gl_colgroups.h" // CollisionGroup de Roblox -> capas/mascaras de Godot
#include "gl_assembly.h"  // conjuntos rigidos estilo Roblox
#include "gl_physlod.h"   // nivel de detalle de la fisica (clases, divisor, CCD)
#include "gl_superficies.h" // studs/inlet por cara + texturas de Material
// Definida en gl_variantes.h (al final: necesita gl_asset.h y RobloxPart).
inline bool gl_variante_de(godot::Node* pieza, int material, godot::Ref<godot::Texture2D>& tex, float& studs);
// Definidas en gl_viewport_frame.h: el ViewportFrame del que cuelga una pieza
// (el primero hacia arriba sin pasar de Workspace), si dibuja, y el aviso de
// que entro algo en el.
inline int gl_vf_activos();
inline godot::Node* gl_vf_de(const godot::Node* n, bool* bajo_worldmodel);
inline bool gl_vf_dibuja(godot::Node* vf);
inline void gl_vf_pieza_entra(godot::Node* vf);
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>

using namespace godot;

// Cache GLOBAL de materiales compartidos: todas las Parts con el mismo
// aspecto (Material, Color, Transparency, Reflectance) usan UN SOLO
// StandardMaterial3D. Sin esto, 14.000 bloques = 14.000 materiales unicos:
// el heap de descriptores de D3D12 se desborda ("Cannot create uniform set")
// y cada draw arrastra un estado distinto (lag). Asi lo agrupa Roblox.
// (static local en funcion, NO global: ver gotcha del error 1114 en gl_errors.h)
inline HashMap<uint64_t, Ref<StandardMaterial3D>>& gl_shared_materials() {
    static HashMap<uint64_t, Ref<StandardMaterial3D>> cache;
    return cache;
}

// ── Registro de piezas SUELTAS (Anchored = false) ─────────────────────
// Lo consulta FallenPartsDestroyHeight. Antes recorria el arbol ENTERO cada
// 0,3 s buscando piezas que se hubieran caido: en el place de prueba son
// ~60.000 nodos, y por CADA UNO construia un std::vector de sus hijos y
// preguntaba is_class("RobloxPart") + get("Anchored") (un Variant) +
// get_global_position(). Medido: 65-110 ms de tiron, tres veces por segundo,
// pasara lo que pasara en el juego. Como solo se puede caer lo que NO esta
// anclado, basta con que cada pieza se apunte aqui.
// Lista PLANA + mapa de pertenencia, igual que el registro de streaming: la
// lista se puede recorrer POR TROZOS entre frames (un HashMap no, sus
// iteradores no sobreviven a que alguien inserte). Hace falta porque el barrido
// pasa de ser "todo de golpe cada 0,3 s" a "un poco cada frame".
inline LocalVector<uint64_t>& gl_lista_sueltas() {
    static LocalVector<uint64_t> v;
    return v;
}
inline HashMap<uint64_t, bool>& gl_piezas_sueltas() {
    static HashMap<uint64_t, bool> reg;
    return reg;
}
inline void gl_suelta_alta(uint64_t id) {
    if (gl_piezas_sueltas().has(id)) return;
    gl_piezas_sueltas()[id] = true;
    gl_lista_sueltas().push_back(id);
}
inline void gl_suelta_baja(uint64_t id) { gl_piezas_sueltas().erase(id); }

// ── Cache de MALLAS y FORMAS por tamaño (rendimiento, 1.16.8) ─────────
// Antes CADA part creaba su propia BoxMesh/BoxShape3D: un mapa importado de
// 38.000 piezas = 38.000 mallas y 38.000 formas de colision unicas, aunque la
// mayoria midan exactamente lo mismo. Compartiendolas por tamaño el motor puede
// reutilizar el recurso (y agrupar draws), y la fisica reutiliza la forma.
// IMPORTANTE: una malla del cache NO se puede mutar (la usan muchas piezas);
// al cambiar de tamaño se pide OTRA al cache (ver _refresh_shape).
inline HashMap<uint64_t, Ref<Mesh>>& gl_shared_meshes() {
    static HashMap<uint64_t, Ref<Mesh>> cache;
    return cache;
}
inline HashMap<uint64_t, Ref<Shape3D>>& gl_shared_shapes() {
    static HashMap<uint64_t, Ref<Shape3D>> cache;
    return cache;
}
// Topes: por encima de esto se intenta soltar lo que ya no usa nadie. Un mapa
// importado grande ronda las 2.000 variantes de malla, asi que 4.000 deja
// margen de sobra y solo salta cuando algo esta generando variantes sin fin.
static const int GL_TOPE_CACHE_MALLAS    = 4096;
static const int GL_TOPE_CACHE_MATERIALES = 4096;

// Clave estable: forma + tamaño cuantizado (evita ruido de float y limita el
// numero de variantes; 0.01 studs es mas fino de lo que se aprecia).
// ══════════════════════════════════════════════════════════════════════
//  APARICION REPARTIDA EN VARIOS FRAMES (como el streaming de Roblox)
//
//  El problema, medido frame a frame: el juego de prueba mete ~1.850 nodos en
//  UN solo frame cada 3,00 segundos exactos, y ese frame dura 290 ms. Medido
//  con 1.700 piezas en aislado:
//        meterlas al arbol .... 31,9 ms
//        frame siguiente ...... 55,7 ms   (los servidores de Godot)
//
//  Roblox no lo hace de golpe: reparte la aparicion. Aqui igual — las primeras
//  piezas del frame se montan al momento (para que un spawn normal de una o dos
//  piezas no cambie en nada) y, cuando se agota el presupuesto, el resto pasan a
//  una cola que se drena en los frames siguientes con OTRO presupuesto.
//
//  Una pieza "pendiente" no tiene malla ni forma de colision todavia: existe en
//  el arbol, los scripts la ven y pueden leerla y escribirla con normalidad, y
//  al montarse se aplican los valores que tenga EN ESE MOMENTO. O sea que
//  configurar la pieza antes de que aparezca sigue funcionando igual.
// ══════════════════════════════════════════════════════════════════════
struct GLColaMontaje {
    LocalVector<ObjectID> cola;
    uint32_t cursor = 0;
    uint64_t frame = 0;
    uint64_t us_gastados = 0;
    uint64_t montadas_directo = 0, montadas_en_cola = 0;   // para medir
};
inline GLColaMontaje& gl_cola_montaje() { static GLColaMontaje c; return c; }
inline bool gl_perfil_montaje() {
    static const bool on = OS::get_singleton()
        && OS::get_singleton()->get_environment("GL_MONTAJE") == "1";
    return on;
}
class RobloxPart;
// Drena la cola con presupuesto de tiempo. La llama RobloxWorkspace cada frame.
void gl_drenar_montajes();
// Presupuestos. El de entrada es pequeño a proposito: lo que pase de ahi es
// justo lo que producia el tiron.
static const uint64_t GL_US_MONTAJE_ENTRADA = 3000;   // 3 ms montando al entrar
static const uint64_t GL_US_MONTAJE_COLA    = 4000;   // 4 ms drenando la cola

#include "gl_cunas.h"   // Wedge y CornerWedge: malla y forma de verdad

// ── LA CLAVE ES UN ENTERO, NO UNA CADENA ──────────────────────────────────
//
// Esto construia un String: cuatro String::num_int64 (que formatean el numero
// a texto, en UTF-32 y con su reserva de memoria) mas tres concatenaciones,
// cada una con OTRA reserva. Siete asignaciones de memoria por llamada, y se
// llama en cada montaje y en cada escritura de Size.
//
// Medido con GL_AUTOPSIA en el frame de una oleada: ~3.800 escrituras del
// puente costando 37-52 ms, o sea 10-13 us por escritura, contra 0,44 us por
// LECTURA. Esa diferencia de 25x no es la escalera de strcmp (esa se paga
// igual en los dos lados): es el texto.
//
// Empaquetado: 4 bits de forma + 20 bits por eje. 20 bits = 1.048.575, y con
// el cuantizado a 0,05 studs eso son 52.428 studs de lado — mucho mas que el
// tope de 2.048 de Roblox. Se recorta por si acaso: que un tamaño absurdo
// colisione es preferible a que se salga del campo y pise otra forma.
typedef uint64_t GLClaveForma;

inline uint64_t gl_eje_q(float v) {
    int64_t q = (int64_t)Math::round((double)v * 20.0);
    if (q < 0) q = 0;
    if (q > 0xFFFFF) q = 0xFFFFF;
    return (uint64_t)q;
}

inline GLClaveForma gl_shape_key(int shape, const Vector3& s) {
    return ((uint64_t)(shape & 0xF) << 60)
         | (gl_eje_q(s.x) << 40)
         | (gl_eje_q(s.y) << 20)
         |  gl_eje_q(s.z);
}

// La caja unitaria que comparten todos los bloques con studs. Usa el hueco de
// forma 15, que no existe: asi no puede chocar con ninguna clave de verdad.
static const GLClaveForma GL_CLAVE_BLOQUE_UNIDAD = ((uint64_t)0xF << 60);

class RobloxPart : public RigidBody3D {
    GDCLASS(RobloxPart, RigidBody3D);

private:
    // ── Por que el DIBUJADO sigue siendo un nodo hijo ─────────────────
    // Se probó a quitarlo y hablar directamente con el servidor de render (un
    // RID en vez de un nodo). Quitaba ~7.900 nodos del place… y salía PEOR:
    //
    //      MeshInstance3D (nodo)          mediana 21,2 ms
    //      RID + aviso de transformacion  mediana 28,6 ms
    //      RID sin aviso (roto a drede)   mediana 25,0 ms
    //
    // El motivo: el servidor de render NO sigue al nodo solo. Hay que empujarle
    // la transformacion cada vez que la pieza se mueve, y hacerlo desde una
    // GDExtension (una llamada por pieza y por frame, cruzando la frontera con
    // el motor) cuesta mas que lo que hace el arbol de escena por dentro.
    // La COLISION sí se pudo meter dentro de la pieza (ver forma_id) porque el
    // servidor de fisica sigue al cuerpo el solo: no hay nada que empujar.
    MeshInstance3D* mesh_instance = nullptr;
    // ── Por qué hace falta el ObjectID además del puntero ─────────────
    // El puntero se guardaba y NO se limpiaba nunca: no hay EXIT_TREE ni
    // PREDELETE. Si el Luau del juego destruye esa malla (un :Destroy(), un
    // reemplazo de modelo, una recarga), el puntero se queda colgando; el
    // `if (!mesh_instance) return` lo daba por bueno y se escribia en memoria
    // ya liberada. Se veia como una rafaga de
    //     ERROR: Parameter "instance" is null.
    //        at: instance_geometry_set_material_override
    //        at: instance_geometry_set_visibility_range
    // y acababa en un SIGSEGV dentro de la extension al moverse por el mapa.
    // El ObjectID es un indice a la tabla viva de objetos: comprobarlo cuesta
    // una busqueda de array, y solo se hace en la ENTRADA de cada metodo.
    ObjectID mesh_id;
    // ¿Sigue viva la malla? Si no, se suelta el puntero y todo lo de abajo
    // ve un nullptr honesto en vez de basura.
    bool _mi_vivo() const {
        if (!mesh_instance) return false;
        if (ObjectDB::get_instance(mesh_id) == nullptr) {
            const_cast<RobloxPart*>(this)->mesh_instance = nullptr;
            return false;
        }
        return true;
    }
    bool vis_visible      = true;
    bool vis_oculta_chunk = false;   // la escondio el agrupado por zonas
    // Anclada pero moviendose por script (sombras propias, ui/gl_sombras.h).
    bool sombra_moviendo = false;
    uint64_t sombra_ultimo_mov = 0;   // frame del ultimo movimiento
    Vector3 sombra_centro;            // donde esta en el mapa de sombra guardado
    bool sombra_fuera_zona = false;  // esta lejisimos: no entra al mapa de sombras
    bool fuera_de_alcance = false;   // fuera del radio de streaming: no se dibuja
    bool montaje_pendiente = false;  // entro al arbol pero aun no se ha montado
    // La pieza es en realidad una malla de la nube (SpecialMesh FileMesh) que no
    // tenemos: se dibujaria su caja, y esas cajas son enormes. Ver RBXMesh.
    bool sin_malla_nube = false;
    // SpecialMesh.Scale y .Offset: solo cambian lo que se ve, no el Size ni la
    // forma fisica (antes se multiplicaba el Size de la pieza).
    Vector3 malla_escala = Vector3(1, 1, 1);
    Vector3 malla_offset;
    // Malla propia (MeshPart, Union, FileMesh) en vez de la de su forma. Ver
    // gl_malla_propia.h.
    Ref<Mesh> malla_propia;
    bool malla_propia_ajusta = true;   // su caja se estira a Size (MeshPart, Union)
    bool malla_propia_caja = false;    // CollisionFidelity = Box: choca como su caja
    bool textura_sin_tinte = false;    // TextureID de una malla: el Color no la tiñe
    bool malla_con_colores = false;    // Union con el color de cada pieza (vertices)
    bool forma_exacta_pendiente = false;  // con su caja hasta que la cola haga la exacta
    bool forzar_forma_exacta = false;     // la pide la cola: construirla ya
    void _poner_malla_propia(const Ref<Shape3D>& forma_normal, bool girada);
    // ── Colisión SIN nodo hijo ────────────────────────────────────────
    // Antes cada pieza colgaba un CollisionShape3D. Un CollisionObject3D (de
    // quien hereda RigidBody3D) sabe llevar sus formas él mismo con la API de
    // "shape owners", que es justo para esto. Se ahorra UN NODO POR PIEZA:
    // en el place de prueba, 7.891 nodos menos, con sus 18 propiedades cada uno
    // que Godot ya no tiene que copiar ni recorrer.
    //  -1 = todavía no tiene forma.
    int  forma_id       = -1;
    // ── Conjunto (assembly) ───────────────────────────────────────────
    Ref<Shape3D> forma_propia;          // la forma de ESTA pieza (para la raiz)
    bool         forma_girada = false;  // el cuarto de vuelta del cilindro
    // Shape owners EXTRA que lleva una raiz: uno por miembro del conjunto, y de
    // que pieza es cada uno (mismo indice).
    LocalVector<int> owners_miembros;
    LocalVector<uint64_t> owners_pieza;
    bool         soy_miembro  = false;  // no-raiz de un conjunto de dos o mas
    // ── Physics LOD (ver gl_physlod.h) ────────────────────────────────
    uint8_t  lod_clase   = GL_LOD_DINAMICO;
    uint8_t  lod_div     = 1;      // cada cuantos pasos le toca trabajar
    uint8_t  lod_fase    = 0;      // desfase: reparte el grupo entre frames
    bool     lod_ccd     = false;  // CCD encendido por el LOD
    bool     lod_congelada = false; // la congelo el LOD (no Anchored, no la red)
    bool     lod_hijo_activo = false;  // le cuelga algo con _physics_process
    bool     en_workspace = false;     // cuelgo del Workspace (no de un almacen)
    // Cuelgo de un ViewportFrame (gl_viewport_frame.h): me dibujo en su mundo
    // y no existo para la fisica del juego. Con un WorldModel en medio sigo
    // procesando (juntas y animaciones), pero tampoco tengo fisica.
    bool     en_vf = false;
    bool     vf_mundo = false;
    uint64_t vf_id = 0;
    uint16_t lod_hijos_vistos = 0xFFFF; // fuerza el primer repaso de hijos
    bool forma_activa   = false;
    Ref<StandardMaterial3D> material;
    Ref<ShaderMaterial> material_luz;   // el de la luz propia, si el material es propio (no compartido)
    bool material_shared = false;   // true = viene del cache global (NO mutarlo)
    bool usa_superficies = false;   // dibuja studs/inlet con el shader de caras
    Ref<PhysicsMaterial>    phys_mat;

    // Active shape meshes (only one set of refs valid at a time)
    // (las mallas/formas ya no se guardan por pieza: vienen del cache compartido)
    OccluderInstance3D* occluder = nullptr;   // solo en piezas grandes y opacas

    // ── BasePart core properties ──────────────────────────────────
    bool    anchored      = true;
    Color   color         = Color(0.63f, 0.64f, 0.65f);
    Vector3 size          = Vector3(4, 1, 2);
    float   transparency  = 0.0f;
    bool    can_collide   = true;
    bool    cast_shadow   = true;
    bool    can_touch     = true;
    bool    can_query     = true;
    // BasePart.RootPriority: con la que Roblox elige la raiz de un conjunto.
    // El importador ya la escribia (776 veces en el place de prueba) pero no
    // existia como propiedad, asi que se perdia por el camino.
    int     root_priority = 0;
    // BasePart.CollisionGroup de Roblox. Se traduce a capa+mascara de Godot
    // (ver gl_colgroups.h): la capa es el bit del grupo y la mascara dice con
    // que grupos choca. Antes esto no existia y los grupos no hacian nada.
    String   collision_group = "Default";
    int      col_idx         = 0;
    uint32_t col_gen_visto   = 0;
    uint32_t lod_gen_visto   = 0;   // generacion de vista con la que se calculo el LOD
    bool    touch_monitoring = false;  // contact monitor activado bajo demanda
    Area3D* touch_area       = nullptr; // solape para Touched (ver _ensure_touch_area)
    bool    locked        = false;
    bool    massless      = false;
    float   reflectance   = 0.0f;
    // Fuerza la transparencia por BORRADO DE PIXELES en ESTA pieza, aunque el
    // jugador no lo tenga activado en los graficos. Para optimizar a mano las
    // piezas donde el punteado no se nota (cristales, humo, vallas...).
    bool    dither_transparency = false;
    // Shape: 0=Block 1=Ball 2=Cylinder 3=Wedge 4=CornerWedge 5=Truss (TrussPart)
    int     roblox_shape  = 0;
    // Material: 0=Plastic 1=SmoothPlastic 2=Neon 3=Wood 4=WoodPlanks
    //           5=Marble 6=Slate 7=Concrete 8=Granite 9=Brick
    //           10=Metal 11=CorrodedMetal 12=DiamondPlate 13=Foil
    //           14=Grass 15=Ice 16=Glass 17=Sand 18=Fabric
    //           19=Rock 20=Snow 21=Cobblestone 22=Pebble
    //           23=Ground 24=Mud 25=Sandstone 26=Basalt 27=CrackedLava
    //           28=Glacier 29=LeafyGrass 30=Salt 31=Limestone 32=Pavement
    //           33=Asphalt 34=ForceField 35=Water 36=Air
    // (0-22 no se reordenan: escenas y Enum.Material existentes dependen de ellos)
    int     roblox_material = 0;

    // Surface types per face (0=Smooth 1=Glue 2=Weld 3=Studs 4=Inlet 5=Universal 6=Hinge 7=Motor)
    int top_surface    = 0;
    int bottom_surface = 0;
    int front_surface  = 0;
    int back_surface   = 0;
    int left_surface   = 0;
    int right_surface  = 0;

    // Physical material properties
    float density           = 0.7f;
    float friction_val      = 0.3f;
    float elasticity        = 0.0f;
    float friction_weight   = 1.0f;
    float elasticity_weight = 1.0f;

    // ── Internal helpers ──────────────────────────────────────────
    // ¿esta pieza usa transparencia por borrado de pixeles? Si el jugador lo activo
    // en los graficos O la pieza lo fuerza desde el diseño del juego.
    bool _gl_use_pixel_transparency() const {
        return dither_transparency || gl_custom().pixel_transparency;
    }

    void _update_albedo() {
        if (!material.is_valid()) return;
        material->set_albedo(gl_color_pieza(Color(color.r, color.g, color.b, 1.0f - transparency)));
    }

    // Clave visual de la part: dos parts con la misma clave comparten material.
    // Mismo motivo que gl_shape_key: esto eran SIETE formateos a texto y seis
    // concatenaciones, trece reservas de memoria por llamada, en un camino que
    // recorre cada escritura de Color, Material, Transparency o Reflectance.
    // Todo cabe de sobra en 64 bits: el material en 16, cinco bytes de aspecto
    // y un bit de bandera.
    uint64_t _material_key() const {
        const uint64_t r = (uint64_t)CLAMP((int)Math::round(color.r * 255.0f), 0, 255);
        const uint64_t g = (uint64_t)CLAMP((int)Math::round(color.g * 255.0f), 0, 255);
        const uint64_t b = (uint64_t)CLAMP((int)Math::round(color.b * 255.0f), 0, 255);
        const uint64_t t = (uint64_t)CLAMP((int)Math::round(transparency * 255.0f), 0, 255);
        const uint64_t x = (uint64_t)CLAMP((int)Math::round(reflectance  * 255.0f), 0, 255);
        uint64_t k = ((uint64_t)(roblox_material & 0xFFFF) << 48)
             | (r << 40) | (g << 32) | (b << 24) | (t << 16) | (x << 8)
             | (uint64_t)(_gl_use_pixel_transparency() ? 1 : 0);
        // Con MaterialVariant el material compartido lleva SU textura: otra clave.
        if (var_tex.is_valid()) {
            k ^= ((uint64_t)var_tex->get_instance_id() + 0x632BE59BD9B4E019ULL) * 0xC2B2AE3D27D4EB4FULL;
            k ^= (uint64_t)(var_studs * 1024.0f) * 0x165667B19E3779F9ULL;
        }
        return k;
    }

    // La MaterialVariant que le toca a la pieza (gl_variantes.h), resuelta en
    // cada repintado: su ColorMap y cada cuantos studs se repite.
    Ref<Texture2D> var_tex;
    float var_studs = 10.0f;

    // Textura de la Part (PERSISTIDA): la usa el Baseplate para la cuadricula
    // estilo Studio y cualquier Part puede llevar la suya. Se TIÑE con Color.
    Ref<Texture2D> part_texture;
    float part_texture_scale = 1.0f;

    void _apply_part_texture() {
        if (!material.is_valid()) return;
        // Un material del cache lo comparten miles de piezas: tocarlo aqui
        // le borraba la textura del Material (Brick, Wood...) a todas ellas.
        // Los compartidos se configuran solo en _configure_material.
        if (material_shared) return;
        // Union con UsePartColor = false: el color va en cada vertice.
        material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, malla_con_colores);
        material->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, malla_con_colores);
        if (malla_con_colores) material->set_albedo(Color(1, 1, 1, material->get_albedo().a));
        // Sin textura propia NO se toca nada: aqui se borraba el ALBEDO, y como
        // esto corre al final de _configure_material, le quitaba a la pieza la
        // textura del Material (ladrillo, madera) recien puesta. El material se
        // deja limpio arriba, en el reset de _configure_material.
        if (!part_texture.is_valid()) return;
        material->set_texture(StandardMaterial3D::TEXTURE_ALBEDO, part_texture);
        material->set_uv1_scale(Vector3(part_texture_scale, part_texture_scale, 1.0f));
        // Anisotropo: visto de canto, sin el la textura se emborrona (ver gl_tex
        // en gl_superficies.h).
        material->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC);
        if (textura_sin_tinte) material->set_albedo(Color(1, 1, 1, material->get_albedo().a));
    }

public:
    // Repinta con lo que haya ahora (p. ej. MaterialVariant nueva o cambiada).
    void gl_refrescar_material() { _apply_material_visual(); }
    void set_part_texture(const Ref<Texture2D>& t) { part_texture = t; _apply_material_visual(); }
    Ref<Texture2D> get_part_texture() const { return part_texture; }
    void set_part_texture_scale(float s) { part_texture_scale = Math::max(s, 0.001f); _apply_material_visual(); }
    float get_part_texture_scale() const { return part_texture_scale; }

    // Conveniencia del Baseplate (aplica textura + escala + suelo mate)
    void gl_apply_grid_texture(const Ref<Texture2D>& tex, float uv_scale) {
        part_texture = tex;
        part_texture_scale = uv_scale;
        _apply_material_visual();   // con textura propia el material es dedicado
        if (material.is_valid() && !material_shared) material->set_roughness(0.9f);
    }

private:

    // El material de la pieza (color, Material de Roblox, texturas, studs) vive
    // en roblox_part_material.h, que se incluye al final de este archivo.
    void _apply_material_visual();

    // Aplica TODO el aspecto al material actual (solo materiales no compartidos
    // o recien creados para el cache).
    void _configure_material();

    // Pide al cache la malla + forma de colision de (forma, tamaño) actuales,
    // creandolas solo la primera vez que aparece esa combinacion. Sustituye a la
    // creacion por-pieza: miles de bloques del mismo tamaño comparten recurso.
    void _refresh_shape(int s) {
        if (!_mi_vivo()) return;
        const GLClaveForma key = gl_shape_key(s, size);
        HashMap<uint64_t, Ref<Mesh>>&   mcache = gl_shared_meshes();
        HashMap<uint64_t, Ref<Shape3D>>& scache = gl_shared_shapes();
        Ref<Mesh>*    fm = mcache.getptr(key);
        Ref<Shape3D>* fs = scache.getptr(key);

        Ref<Mesh>    mesh;
        Ref<Shape3D> shape;
        if (fm && fs) {
            mesh = *fm; shape = *fs;
        } else {
            const float r  = Math::min(size.x, size.z) * 0.5f;
            const float cr = Math::min(size.y, size.z) * 0.5f;
            switch (s) {
                case 1: {   // Ball
                    Ref<SphereMesh> m; m.instantiate();
                    m->set_radius(r); m->set_height(r * 2.0f);
                    Ref<SphereShape3D> c; c.instantiate();
                    c->set_radius(r);
                    mesh = m; shape = c;
                    break;
                }
                case 2: {   // Cylinder — en Roblox apunta al eje X (Godot en Y)
                    Ref<CylinderMesh> m; m.instantiate();
                    m->set_top_radius(cr); m->set_bottom_radius(cr); m->set_height(size.x);
                    Ref<CylinderShape3D> c; c.instantiate();
                    c->set_radius(cr); c->set_height(size.x);
                    mesh = m; shape = c;
                    break;
                }
                case 3: {   // Wedge — geometria real, no una caja
                    gl_geom_cuna(size, mesh, shape);
                    break;
                }
                case 4: {   // CornerWedge — idem
                    gl_geom_cuna_esquina(size, mesh, shape);
                    break;
                }
                case 5: {   // TrussPart: celosia que choca como su caja
                    gl_geom_truss(size, mesh, shape);
                    break;
                }
                default: {  // Block
                    Ref<BoxMesh> m; m.instantiate();
                    m->set_size(size);
                    Ref<BoxShape3D> c; c.instantiate();
                    c->set_size(size);
                    mesh = m; shape = c;
                    break;
                }
            }
            mcache[key] = mesh;
            scache[key] = shape;
            gl_purgar_cache(mcache, GL_TOPE_CACHE_MALLAS);
            gl_purgar_cache(scache, GL_TOPE_CACHE_MALLAS);
        }
        if (malla_propia.is_valid()) { _poner_malla_propia(shape, s == 2); return; }

        // ── Bloques con studs: caja UNITARIA + escala ─────────────────
        // El shader de studs necesita el tamaño de la pieza para poner un
        // teton por stud. Pasarlo como uniform de instancia no vale: Godot
        // solo admite 4096 objetos con uniforms de instancia y este mapa
        // tiene 4.551 piezas con studs ("Too many instances using shader
        // instance variables"). Con la caja unitaria el tamaño se lee de la
        // propia matriz del modelo dentro del shader, gratis y sin limite.
        // De regalo: TODOS los bloques con studs comparten UNA malla.
        if (usa_superficies && s == 0) {
            Ref<Mesh>* fu = mcache.getptr(GL_CLAVE_BLOQUE_UNIDAD);
            if (fu) {
                mesh = *fu;
            } else {
                Ref<BoxMesh> m; m.instantiate();
                m->set_size(Vector3(1, 1, 1));
                mesh = m;
                mcache[GL_CLAVE_BLOQUE_UNIDAD] = mesh;
            }
            mesh_instance->set_scale(size * malla_escala);
        } else if (mesh_instance->get_scale() != malla_escala) {
            mesh_instance->set_scale(malla_escala);
        }
        if (mesh_instance->get_position() != malla_offset) mesh_instance->set_position(malla_offset);

        // El cilindro de Roblox apunta al eje X y el de Godot al Y.
        mesh_instance->set_mesh(mesh);
        mesh_instance->set_rotation(s == 2 ? Vector3(0, 0, 1.5707964f) : Vector3());
        // La forma SÍ va sin nodo hijo, en el propio cuerpo (ver _forma_poner).
        if (forma_id >= 0 || can_collide) _forma_poner(shape, s == 2);
    }

    // LOD por tamaño (rendimiento): una pieza pequeña deja de dibujarse — y de
    // proyectar sombra — cuando esta lejos, porque a esa distancia ocupa menos de
    // un pixel. Las piezas grandes (suelos, paredes, edificios) se ven siempre.
    // La distancia es proporcional al tamaño, con un desvanecido para que no haga
    // "pop". Es lo que hace Roblox con las piezas lejanas.
    // 11) Una pieza con Transparency = 1 es INVISIBLE: se saca del render por
    // completo (asi la GPU no la procesa de ninguna forma). Vuelve sola en cuanto
    // deja de ser invisible. Ojo: se apaga solo el dibujado, la fisica sigue.
    // 10) Sombras: las piezas pequeñas NO proyectan sombra. Una pieza de menos de
    // ~2 studs apenas aporta una sombra apreciable, pero pasa igual por el mapa de
    // sombras; quitarlas alivia una pasada de render entera.
    void _apply_visibility_and_shadow() {
        if (!_mi_vivo()) return;
        const bool invisible = transparency >= 0.999f || sin_malla_nube || fuera_de_alcance;
        // Sombras propias (ui/gl_sombras.h): lo quieto va al mapa guardado y
        // cambiarlo redibuja su baldosa; lo suelto (y lo anclado mientras un
        // script lo mueve), al de cada frame; lo que no proyecta, a ninguno.
        if (anchored && en_workspace && (vis_visible || !invisible) && is_inside_tree()) {
            gl_sombras_tocar(get_global_position(), size.length() * 0.5f + 1.0f);
            if (!sombra_moviendo) sombra_centro = get_global_position();
        }
        const uint32_t capa = (!cast_shadow || sombra_fuera_zona) ? GL_CAPA_SIN_SOMBRA
                            : ((anchored && !sombra_moviendo) ? GL_CAPA_MUNDO : GL_CAPA_DINAMICA);
        if (mesh_instance->get_layer_mask() != capa) mesh_instance->set_layer_mask(capa);
        // Solo lo anclado avisa de que se mueve (lo suelto se mueve siempre).
        const bool avisar = anchored && en_workspace;
        if (is_transform_notification_enabled() != avisar) set_notify_transform(avisar);
        vis_visible = !invisible;
        mesh_instance->set_visible(vis_visible && !vis_oculta_chunk);
        if (invisible) return;

        // `sombra_fuera_zona` la pone RobloxWorkspace: una pieza a kilometros del
        // jugador NO proyecta. No es un capricho de rendimiento — sin eso el mapa
        // de sombras se rompe entero (ver gl_recortar_sombras_lejanas).
        if (!cast_shadow || sombra_fuera_zona) {
            mesh_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
            return;
        }
        // ── TODO lo que se ve, proyecta ───────────────────────────────
        // Aqui habia un `maxdim < 2.0` que dejaba SIN SOMBRA a toda pieza de
        // menos de 2 studs. En un mapa de Roblox eso es media escena: el
        // resultado era que unos objetos tenian sombra y sus vecinos no, sin
        // ninguna logica visible ("las sombras se borran entre ellas").
        // En Roblox proyecta todo lo que esta cerca; lo que decide es la
        // DISTANCIA, y de eso se encarga sombra_fuera_zona (ver
        // gl_stream_estado, que ademas recorta antes las piezas pequeñas).
        mesh_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_ON);
    }

    void _apply_lod() {
        if (!_mi_vivo()) return;
        // En un ViewportFrame la distancia es la de SU camara, casi siempre a
        // un par de studs: sin recorte.
        if (en_vf) { mesh_instance->set_visibility_range_end(0.0f); return; }
        const float maxdim = Math::max(size.x, Math::max(size.y, size.z));
        if (maxdim >= 8.0f) {                       // piezas grandes: sin limite
            mesh_instance->set_visibility_range_end(0.0f);
            return;
        }
        // Sin desvanecido: el fade obliga a dibujar la pieza con transparencia
        // mientras se apaga, y eso cuesta mas de lo que ahorra (medido).
        //
        // ESTA DISTANCIA TIENE QUE SEGUIR A LA VISTA ELEGIDA. Antes era fija
        // (60..720 studs pasara lo que pasara con la calidad), asi que poner
        // calidad 10 "32768 studs" no cambiaba absolutamente nada: las piezas de
        // menos de 8 studs seguian esfumandose a un par de cientos de studs y el
        // mapa se deshacia igual que en calidad 1. La vista que anuncia el menu
        // tiene que ser la que se ve.
        const float vista = gl_view_studs_actual();
        if (vista >= GL_VIEW_INFINITA - 0.5f) {   // vista "infinita": sin recorte
            mesh_instance->set_visibility_range_end(0.0f);
            return;
        }
        // 1024 studs es la vista para la que estaba calibrada la tabla original.
        const float escala = vista / 1024.0f;
        const float dist = Math::max(60.0f, maxdim * 90.0f) * escala;
        mesh_instance->set_visibility_range_end(dist);
    }

    // ── Occluder (occlusion culling) ─────────────────────────────────────
    // Marca la pieza como "tapadora" para que Godot descarte lo que quede detras.
    // SOLO piezas grandes, opacas y ancladas: son las que de verdad tapan (muros,
    // suelos, edificios). Marcar piezas pequeñas dispararia el coste de CPU sin
    // ocultar casi nada — ese es el equilibrio del occlusion culling por software.
    void _gl_apply_occluder() {
        const float minsize = gl_occlusion_min_size(gl_graphics_level());
        // Lo de un ViewportFrame no tapa nada del mundo (ni del visor).
        const bool wants = gl_occlusion_enabled(gl_graphics_level()) && !en_vf
                        && anchored && transparency < 0.05f
                        && size.x >= minsize && size.y >= minsize && size.z >= minsize;
        if (!wants) {
            if (occluder) { occluder->queue_free(); occluder = nullptr; }
            return;
        }
        if (!occluder) {
            occluder = memnew(OccluderInstance3D);
            add_child(occluder);
            occluder->set_owner(nullptr);   // interno: no se guarda en la escena
        }
        Ref<BoxOccluder3D> box = occluder->get_occluder();
        if (!box.is_valid()) { box.instantiate(); }
        box->set_size(size);
        occluder->set_occluder(box);
    }

    // ── Forma de colisión propia (sin nodo hijo) ──────────────────────
    void _forma_asegurar() {
        if (forma_id < 0) forma_id = (int)create_shape_owner(this);
    }
    // Pone la forma y su giro. El cilindro de Roblox apunta al eje X y el de
    // Godot al Y, de ahi el cuarto de vuelta.
    void _forma_poner(const Ref<Shape3D>& s, bool girado) {
        _forma_asegurar();
        forma_propia = s;            // la necesita el conjunto para copiarla a la raiz
        forma_girada = girado;
        shape_owner_clear_shapes((uint32_t)forma_id);
        if (s.is_valid()) shape_owner_add_shape((uint32_t)forma_id, s);
        Transform3D t;
        if (girado) t.basis = Basis(Vector3(0, 0, 1), 1.5707964f);
        shape_owner_set_transform((uint32_t)forma_id, t);
        _gl_forma_apagada(!forma_activa);
    }
    void _forma_activar(bool on) {
        forma_activa = on;
        if (forma_id < 0) {
            if (!on) return;          // sin forma y sin colision: nada que hacer
            _forma_asegurar();
            _refresh_shape(roblox_shape);
            return;
        }
        _gl_forma_apagada(!on);
    }
    bool _forma_desactivada() const {
        return forma_id < 0 || !forma_activa;
    }
    // ¿Lleva forma de colision encendida? Si choca, si. Si NO choca pero se
    // puede consultar, tambien: en Roblox los rayos y GetPartBoundsInBox ven las
    // piezas CanCollide=false (salvo CanQuery=false), y aqui apagarles la forma
    // las hacia invisibles. Excepcion: un conjunto soldado, cuya forma la lleva
    // la raiz y solo con los miembros que chocan.
    bool _gl_quiere_forma() const {
        if (can_collide) return true;
        if (gl_sin_fantasmas()) return false;   // A/B de rendimiento (GL_SIN_FANTASMAS=1)
        return can_query && !soy_miembro && owners_miembros.is_empty();
    }
    // Pieza que no choca: su forma va en GL_CAPA_CONSULTA, que ninguna mascara
    // de cuerpo incluye (la atraviesa todo, la ven las consultas).
    bool _gl_es_fantasma() const { return !can_collide && owners_miembros.is_empty(); }

    // Crea el collider solo cuando hace falta (CanCollide=true). Una pieza
    // decorativa sin colision no debe costar nada a la fisica.
    void _ensure_collider() {
        if (forma_id >= 0 || !is_inside_tree()) return;
        _forma_activar(true);
    }

    void _apply_shape_internal(int s) { _refresh_shape(s); }

    void _update_shape_dims() {
        // Las mallas/formas son COMPARTIDAS: no se pueden mutar (afectaria a todas
        // las piezas de ese tamaño). Se pide la variante del nuevo tamaño.
        _refresh_shape(roblox_shape);
        _update_mass_from_density();
        _apply_lod();   // la distancia de LOD depende del tamaño
        _gl_apply_occluder();
    }

    void _update_mass_from_density() {
        if (massless) { set_mass(0.001f); return; }
        float vol = size.x * size.y * size.z;
        set_mass(Math::max(density * vol, 0.001f));
    }

    void _update_phys_mat() {
        if (!phys_mat.is_valid()) return;
        // Comparar antes de escribir: cada set emite "changed" en el recurso, y
        // esa señal hace que el RigidBody3D recargue el parametro en el
        // PhysicsServer. Poner el mismo numero costaba dos viajes al servidor.
        if (phys_mat->get_friction() != friction_val) phys_mat->set_friction(friction_val);
        if (phys_mat->get_bounce()   != elasticity)   phys_mat->set_bounce(elasticity);
    }

    // ── Escrituras al cuerpo que solo pasan si CAMBIAN algo ───────────────
    //
    // Los setters de RigidBody3D no comparan: set_freeze_enabled y
    // set_freeze_mode llaman los dos a _apply_body_mode() -> body_set_mode
    // SIEMPRE, aunque les pases el valor que ya tenian. Y en Jolt cambiar el
    // modo de un cuerpo que YA esta en el espacio le cambia la capa de
    // broadphase, lo que se traduce en un RemoveBodies + AddBodies de UNO EN
    // UNO — justo el caso que la documentacion de Jolt marca como patologico.
    //
    // El montaje de una pieza llamaba a set_freeze_enabled DOS veces (una en
    // _montar_ahora y otra en _apply_ancestor_visibility, con el mismo valor
    // cuando la pieza es visible) y a set_freeze_mode una tercera con
    // FREEZE_MODE_STATIC, que ademas ya es el valor de fabrica de Godot. Tres
    // body_set_mode por pieza; en una oleada de 420 piezas, 1.260.
    //
    // Anchored ya estaba medido como la segunda escritura mas cara del puente:
    // 152 us. Comparar dos bool no cuesta nada.
    // Dentro de un ViewportFrame la pieza no tiene fisica: ni Anchored = false,
    // ni el LOD, ni CanCollide la sacan de ahi (en Roblox no cae ni choca).
    void _gl_congelar(bool on) {
        if (en_vf) on = true;
        if (is_freeze_enabled() == on) return;
        set_freeze_enabled(on);
    }
    void _gl_modo_congelado(RigidBody3D::FreezeMode m) {
        if (get_freeze_mode() == m) return;
        set_freeze_mode(m);
    }
    void _gl_forma_apagada(bool off) {
        if (en_vf) off = true;
        if (forma_id < 0) return;
        if (is_shape_owner_disabled((uint32_t)forma_id) == off) return;
        shape_owner_set_disabled((uint32_t)forma_id, off);
    }

protected:
    static void _bind_methods() {
        // ── Anchored ────────────────────────────────────────────────
        ClassDB::bind_method(D_METHOD("set_anchored","a"), &RobloxPart::set_anchored);
        ClassDB::bind_method(D_METHOD("get_anchored"),     &RobloxPart::get_anchored);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"Anchored"),"set_anchored","get_anchored");

        // ── Textura de la Part (persistida; se tiñe con Color) ──────
        ClassDB::bind_method(D_METHOD("set_part_texture","t"), &RobloxPart::set_part_texture);
        ClassDB::bind_method(D_METHOD("get_part_texture"),     &RobloxPart::get_part_texture);
        ClassDB::bind_method(D_METHOD("set_part_texture_scale","s"), &RobloxPart::set_part_texture_scale);
        ClassDB::bind_method(D_METHOD("get_part_texture_scale"),     &RobloxPart::get_part_texture_scale);
        ADD_PROPERTY(PropertyInfo(Variant::OBJECT,"Texture",PROPERTY_HINT_RESOURCE_TYPE,"Texture2D"),
            "set_part_texture","get_part_texture");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"TextureScale",PROPERTY_HINT_RANGE,"0.01,500,0.01"),
            "set_part_texture_scale","get_part_texture_scale");

        // ── Color / BrickColor ──────────────────────────────────────
        ClassDB::bind_method(D_METHOD("set_color","c"), &RobloxPart::set_color);
        ClassDB::bind_method(D_METHOD("get_color"),     &RobloxPart::get_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"Color"),"set_color","get_color");
        ClassDB::bind_method(D_METHOD("set_brick_color","c"), &RobloxPart::set_color);
        ClassDB::bind_method(D_METHOD("get_brick_color"),     &RobloxPart::get_color);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,"BrickColor"),"set_brick_color","get_brick_color");

        // ── Size ────────────────────────────────────────────────────
        ClassDB::bind_method(D_METHOD("set_size","s"), &RobloxPart::set_size);
        ClassDB::bind_method(D_METHOD("get_size"),     &RobloxPart::get_size);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3,"Size"),"set_size","get_size");

        // ── Transparency / Reflectance ──────────────────────────────
        ClassDB::bind_method(D_METHOD("set_transparency","t"), &RobloxPart::set_transparency);
        ClassDB::bind_method(D_METHOD("get_transparency"),     &RobloxPart::get_transparency);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Transparency",PROPERTY_HINT_RANGE,"0,1,0.01"),"set_transparency","get_transparency");

        ClassDB::bind_method(D_METHOD("set_reflectance","r"), &RobloxPart::set_reflectance);
        ClassDB::bind_method(D_METHOD("get_reflectance"),     &RobloxPart::get_reflectance);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Reflectance",PROPERTY_HINT_RANGE,"0,1,0.01"),"set_reflectance","get_reflectance");

        // ── Shape ───────────────────────────────────────────────────
        ClassDB::bind_method(D_METHOD("set_roblox_shape","s"), &RobloxPart::set_roblox_shape);
        ClassDB::bind_method(D_METHOD("get_roblox_shape"),     &RobloxPart::get_roblox_shape);
        ADD_PROPERTY(PropertyInfo(Variant::INT,"Shape",PROPERTY_HINT_ENUM,
            "Block:0,Ball:1,Cylinder:2,Wedge:3,CornerWedge:4,Truss:5"),
            "set_roblox_shape","get_roblox_shape");

        // ── Material ────────────────────────────────────────────────
        ClassDB::bind_method(D_METHOD("set_roblox_material","m"), &RobloxPart::set_roblox_material);
        ClassDB::bind_method(D_METHOD("get_roblox_material"),     &RobloxPart::get_roblox_material);
        ADD_PROPERTY(PropertyInfo(Variant::INT,"Material",PROPERTY_HINT_ENUM,
            "Plastic:0,SmoothPlastic:1,Neon:2,Wood:3,WoodPlanks:4,"
            "Marble:5,Slate:6,Concrete:7,Granite:8,Brick:9,"
            "Metal:10,CorrodedMetal:11,DiamondPlate:12,Foil:13,"
            "Grass:14,Ice:15,Glass:16,Sand:17,Fabric:18,"
            "Rock:19,Snow:20,Cobblestone:21,Pebble:22,"
            "Ground:23,Mud:24,Sandstone:25,Basalt:26,CrackedLava:27,"
            "Glacier:28,LeafyGrass:29,Salt:30,Limestone:31,Pavement:32,"
            "Asphalt:33,ForceField:34,Water:35,Air:36"),
            "set_roblox_material","get_roblox_material");

        // ── Collision flags ─────────────────────────────────────────
        ClassDB::bind_method(D_METHOD("set_can_collide","c"), &RobloxPart::set_can_collide);
        ClassDB::bind_method(D_METHOD("get_can_collide"),     &RobloxPart::get_can_collide);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"CanCollide"),"set_can_collide","get_can_collide");

        ClassDB::bind_method(D_METHOD("set_can_touch","c"), &RobloxPart::set_can_touch);
        ClassDB::bind_method(D_METHOD("get_can_touch"),     &RobloxPart::get_can_touch);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"CanTouch"),"set_can_touch","get_can_touch");

        ClassDB::bind_method(D_METHOD("set_can_query","c"), &RobloxPart::set_can_query);
        ClassDB::bind_method(D_METHOD("get_can_query"),     &RobloxPart::get_can_query);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"CanQuery"),"set_can_query","get_can_query");

        ClassDB::bind_method(D_METHOD("set_root_priority","p"), &RobloxPart::set_root_priority);
        ClassDB::bind_method(D_METHOD("get_root_priority"),     &RobloxPart::get_root_priority);
        ADD_PROPERTY(PropertyInfo(Variant::INT,"RootPriority",PROPERTY_HINT_RANGE,"-127,127,1"),
                     "set_root_priority","get_root_priority");

        ClassDB::bind_method(D_METHOD("set_collision_group","g"), &RobloxPart::set_collision_group);
        ClassDB::bind_method(D_METHOD("get_collision_group"),     &RobloxPart::get_collision_group);
        ADD_PROPERTY(PropertyInfo(Variant::STRING,"CollisionGroup"),
                     "set_collision_group","get_collision_group");

        // ── Rendering flags ─────────────────────────────────────────
        ClassDB::bind_method(D_METHOD("_gl_apply_occluder"), &RobloxPart::_gl_apply_occluder);
        ClassDB::bind_method(D_METHOD("set_dither_transparency","b"), &RobloxPart::set_dither_transparency);
        ClassDB::bind_method(D_METHOD("get_dither_transparency"),     &RobloxPart::get_dither_transparency);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"DitherTransparency"),
            "set_dither_transparency","get_dither_transparency");
        ClassDB::bind_method(D_METHOD("set_cast_shadow","b"), &RobloxPart::set_cast_shadow);
        ClassDB::bind_method(D_METHOD("get_cast_shadow"),     &RobloxPart::get_cast_shadow);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"CastShadow"),"set_cast_shadow","get_cast_shadow");

        ClassDB::bind_method(D_METHOD("set_locked","b"), &RobloxPart::set_locked);
        ClassDB::bind_method(D_METHOD("get_locked"),     &RobloxPart::get_locked);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"Locked"),"set_locked","get_locked");

        ClassDB::bind_method(D_METHOD("set_massless","b"), &RobloxPart::set_massless);
        ClassDB::bind_method(D_METHOD("get_massless"),     &RobloxPart::get_massless);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"Massless"),"set_massless","get_massless");

        // ── Physical properties ─────────────────────────────────────
        ADD_GROUP("Physics","");
        ClassDB::bind_method(D_METHOD("set_density","d"),          &RobloxPart::set_density);
        ClassDB::bind_method(D_METHOD("get_density"),              &RobloxPart::get_density);
        ClassDB::bind_method(D_METHOD("set_friction_val","f"),     &RobloxPart::set_friction_val);
        ClassDB::bind_method(D_METHOD("get_friction_val"),         &RobloxPart::get_friction_val);
        ClassDB::bind_method(D_METHOD("set_elasticity","e"),       &RobloxPart::set_elasticity);
        ClassDB::bind_method(D_METHOD("get_elasticity"),           &RobloxPart::get_elasticity);
        ClassDB::bind_method(D_METHOD("set_friction_weight","w"),  &RobloxPart::set_friction_weight);
        ClassDB::bind_method(D_METHOD("get_friction_weight"),      &RobloxPart::get_friction_weight);
        ClassDB::bind_method(D_METHOD("set_elasticity_weight","w"),&RobloxPart::set_elasticity_weight);
        ClassDB::bind_method(D_METHOD("get_elasticity_weight"),    &RobloxPart::get_elasticity_weight);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Density",PROPERTY_HINT_RANGE,"0.001,100,0.001"),"set_density","get_density");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Friction",PROPERTY_HINT_RANGE,"0,2,0.01"),      "set_friction_val","get_friction_val");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"Elasticity",PROPERTY_HINT_RANGE,"0,1,0.01"),    "set_elasticity","get_elasticity");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"FrictionWeight",PROPERTY_HINT_RANGE,"0,100,0.01"),"set_friction_weight","get_friction_weight");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"ElasticityWeight",PROPERTY_HINT_RANGE,"0,100,0.01"),"set_elasticity_weight","get_elasticity_weight");

        // ── Surface types ───────────────────────────────────────────
        ADD_GROUP("Surfaces","");
        static const char* SURF = "Smooth:0,Glue:1,Weld:2,Studs:3,Inlet:4,Universal:5,Hinge:6,Motor:7";
        ClassDB::bind_method(D_METHOD("set_top_surface","s"),    &RobloxPart::set_top_surface);
        ClassDB::bind_method(D_METHOD("get_top_surface"),        &RobloxPart::get_top_surface);
        ClassDB::bind_method(D_METHOD("set_bottom_surface","s"), &RobloxPart::set_bottom_surface);
        ClassDB::bind_method(D_METHOD("get_bottom_surface"),     &RobloxPart::get_bottom_surface);
        ClassDB::bind_method(D_METHOD("set_front_surface","s"),  &RobloxPart::set_front_surface);
        ClassDB::bind_method(D_METHOD("get_front_surface"),      &RobloxPart::get_front_surface);
        ClassDB::bind_method(D_METHOD("set_back_surface","s"),   &RobloxPart::set_back_surface);
        ClassDB::bind_method(D_METHOD("get_back_surface"),       &RobloxPart::get_back_surface);
        ClassDB::bind_method(D_METHOD("set_left_surface","s"),   &RobloxPart::set_left_surface);
        ClassDB::bind_method(D_METHOD("get_left_surface"),       &RobloxPart::get_left_surface);
        ClassDB::bind_method(D_METHOD("set_right_surface","s"),  &RobloxPart::set_right_surface);
        ClassDB::bind_method(D_METHOD("get_right_surface"),      &RobloxPart::get_right_surface);
        ADD_PROPERTY(PropertyInfo(Variant::INT,"TopSurface",PROPERTY_HINT_ENUM,SURF),    "set_top_surface","get_top_surface");
        ADD_PROPERTY(PropertyInfo(Variant::INT,"BottomSurface",PROPERTY_HINT_ENUM,SURF), "set_bottom_surface","get_bottom_surface");
        ADD_PROPERTY(PropertyInfo(Variant::INT,"FrontSurface",PROPERTY_HINT_ENUM,SURF),  "set_front_surface","get_front_surface");
        ADD_PROPERTY(PropertyInfo(Variant::INT,"BackSurface",PROPERTY_HINT_ENUM,SURF),   "set_back_surface","get_back_surface");
        ADD_PROPERTY(PropertyInfo(Variant::INT,"LeftSurface",PROPERTY_HINT_ENUM,SURF),   "set_left_surface","get_left_surface");
        ADD_PROPERTY(PropertyInfo(Variant::INT,"RightSurface",PROPERTY_HINT_ENUM,SURF),  "set_right_surface","get_right_surface");

        // ── Signals ─────────────────────────────────────────────────
        ADD_SIGNAL(MethodInfo("Touched",    PropertyInfo(Variant::OBJECT,"hit_node")));
        ADD_SIGNAL(MethodInfo("TouchEnded", PropertyInfo(Variant::OBJECT,"hit_node")));
        ClassDB::bind_method(D_METHOD("_on_body_entered","body"), &RobloxPart::_on_body_entered);
        // Para sondas: montar ya una pieza aparcada y construir ya su forma exacta.
        ClassDB::bind_method(D_METHOD("gl_montar_si_pendiente"), &RobloxPart::gl_montar_si_pendiente);
        ClassDB::bind_method(D_METHOD("gl_forma_exacta_ya"),     &RobloxPart::gl_forma_exacta_ya);
        ClassDB::bind_method(D_METHOD("_on_body_exited","body"),  &RobloxPart::_on_body_exited);
        ClassDB::bind_method(D_METHOD("_on_luau_part_touched","hit_node","luau_ref","lua_state_ptr"),
                             &RobloxPart::_on_luau_part_touched);
    }

    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) {
            // ── LIMPIAR los hijos internos de escenas VIEJAS ──────────
            // Antes cada pieza colgaba un MeshInstance3D y un CollisionShape3D.
            // Ya no hacen falta: la malla se dibuja hablando con el servidor de
            // render y la forma la lleva el propio cuerpo. Si una escena
            // guardada los trae, se tiran (asi se repara sola).
            // ── ADOPTAR la malla que ya exista y TIRAR los colliders viejos ──
            // La malla sigue siendo un nodo hijo (ver el comentario de arriba).
            // Los CollisionShape3D de escenas guardadas ya no hacen falta: la
            // forma la lleva el propio cuerpo, asi que se tiran (y de paso se
            // repara una escena que los traiga).
            // Al RE-entrar en el arbol el puntero de la vez anterior puede estar
            // colgando: se comprueba antes de decidir si hay que adoptar o crear.
            // ── APARICION REPARTIDA: PROBADA Y REVERTIDA ─────────────────
            // Se implemento la cola (montar unas pocas piezas por frame, como el
            // streaming de Roblox) y MIDIO PEOR:
            //     directo ..... 54,2 FPS medio | p90 16,8 ms | 31 picos en 40 s
            //     con cola .... 48,2 FPS medio | p90 23,1 ms | 43 picos
            //
            // El motivo lo dio el contador de la propia cola: por cada tanda de
            // brainrots entran ~400 RobloxPart, que a 18,8 us son ~7,5 ms de los
            // 290 ms del pico. Las piezas son el 3%. Repartir el 3% no arregla
            // nada y añade trabajo por frame.
            //
            // Donde SI esta el coste (medido metiendo 1.700 nodos de cada clase):
            //     RobloxTextLabel ... 259 ms   (un Label de Godot pelado ya son 136)
            //     RobloxFrame ....... 115 ms
            //     RobloxPart .........  27 ms
            // O sea que el tiron es la UI, no la geometria. La maquinaria de la
            // cola se deja arriba por si se aplica a las clases de UI, que es
            // donde tendria sentido.
            //
            // ── PIEZA GUARDADA: SIN MONTAR ──────────────────────────────
            // En un almacen (ServerStorage, ReplicatedStorage, Lighting...) no se
            // dibuja ni choca, y aun asi se montaba entera para apagarla justo
            // despues: 100-250 us por pieza, 2 ms en una MeshPart. En Build a
            // Mountain eran 4 de los 5,6 s que tardaba en montarse el juego
            // (30.000 piezas de plantillas y copias en ServerStorage), con la
            // ventana congelada. Se queda aparcada y pendiente: se monta al
            // entrar en el Workspace (un reparent vuelve a pasar por aqui y un
            // Clone es una pieza nueva) o si algo la necesita antes
            // (gl_montar_si_pendiente).
            _gl_vf_resolver();
            if (!en_vf && gl_visual_hidden_by_ancestor(this)) {
                montaje_pendiente = true;
                _update_mass_from_density();   // GetMass vale tambien guardada
                _apply_ancestor_visibility();  // invisible y fuera del espacio fisico
            } else {
                _montar_ahora();
            }
            _apuntarse_si_suelta();
            // ¿Cuelgo del Workspace? Se resuelve UNA vez al entrar al arbol (y
            // otra vez si me reparentan, que es cuando vuelve a pasar por aqui).
            // Antes lo preguntaba la reconstruccion de conjuntos con
            // is_ancestor_of por cada junta, y eso sube el arbol entero: con
            // 2.077 soldaduras son 4.154 subidas, varias veces por segundo.
            en_workspace = false;
            for (Node* a = get_parent(); a; a = a->get_parent())
                if (a->is_class("RobloxWorkspace")) { en_workspace = true; break; }
            if (en_workspace) gl_partes_workspace()[(uint64_t)get_instance_id()] = true;
            else              gl_partes_workspace().erase((uint64_t)get_instance_id());
            gl_stream_alta(this);
            gl_anclajes_cambiaron();   // un weld dormido puede tener trabajo otra vez
        }
        else if (p_what == NOTIFICATION_TRANSFORM_CHANGED) {
            // Un script mueve algo anclado: al mapa de sombra de cada frame (su
            // sitio de antes se redibuja sin ella) hasta que lleve un rato quieta.
            if (anchored && en_workspace && vis_visible && cast_shadow) {
                sombra_ultimo_mov = Engine::get_singleton()->get_process_frames();
                if (!sombra_moviendo) {
                    sombra_moviendo = true;
                    gl_sombras_tocar(sombra_centro, size.length() * 0.5f + 1.0f);
                    gl_sombras_moviles().insert((uint64_t)get_instance_id());
                    _apply_visibility_and_shadow();
                }
            }
        }
        else if (p_what == NOTIFICATION_EXIT_TREE) {
            if (anchored && en_workspace && vis_visible) gl_sombras_tocar(get_global_position(), size.length() * 0.5f + 1.0f);
            if (sombra_moviendo) gl_sombras_moviles().erase((uint64_t)get_instance_id());
            gl_suelta_baja((uint64_t)get_instance_id());
            gl_partes_workspace().erase((uint64_t)get_instance_id());
            en_workspace = false;
            en_vf = false;
            vf_mundo = false;
            vf_id = 0;
            gl_anclajes_cambiaron();
        }
    }

    // ¿Cuelgo de un ViewportFrame? Va lo PRIMERO del montaje: el LOD, el
    // occluder y la fisica dependen de la respuesta. Sin visores vivos no se
    // sube por el arbol (lo de una plantilla lo oculta su almacen).
    void _gl_vf_resolver() {
        bool wm = false;
        Node* vf = gl_vf_activos() > 0 ? gl_vf_de(this, &wm) : nullptr;
        en_vf = vf != nullptr;
        vf_mundo = wm;
        vf_id = vf ? (uint64_t)vf->get_instance_id() : 0;
        if (en_vf && (fuera_de_alcance || sombra_fuera_zona)) {
            // El streaming y la sombra lejana miden desde la camara del jugador,
            // que aqui no pinta nada.
            fuera_de_alcance = false;
            sombra_fuera_zona = false;
        }
    }

    // Todo lo que cuesta al aparecer una pieza. Se llama al entrar al arbol si
    // hay presupuesto, o mas tarde desde la cola. Es idempotente: aplica los
    // valores que la pieza tenga EN ESE MOMENTO.
    void _montar_ahora() {
        GLAutTiempo _aut_montaje(AUT_MONTAJE);
        montaje_pendiente = false;
        _gl_vf_resolver();
        {
            _mi_vivo();
            for (int i = get_child_count() - 1; i >= 0; i--) {
                Node* c = get_child(i);
                if (MeshInstance3D* m = gl_cast<MeshInstance3D>(c)) {
                    if (!mesh_instance) mesh_instance = m;
                    else { remove_child(m); m->queue_free(); }
                    continue;
                }
                if (gl_cast<CollisionShape3D>(c)) {
                    remove_child(c);
                    c->queue_free();
                }
            }
            if (!mesh_instance) {
                mesh_instance = memnew(MeshInstance3D);
                add_child(mesh_instance, false, Node::INTERNAL_MODE_BACK);   // interna: GetChildren no la ve
            }
            // El ObjectID se apunta AQUI, junto al puntero, para que los dos
            // vayan siempre a la par (ver _mi_vivo arriba).
            mesh_id = mesh_instance->get_instance_id();
            // Sin owner: no se guarda en la escena, la recrea la clase.
            mesh_instance->set_owner(nullptr);
            // Y no se copia al clonar: son 36 propiedades que se tiran, porque
            // el clon la vuelve a fabricar aqui mismo.
            mesh_instance->set_meta(GL_META_INTERNO, true);

            if (!phys_mat.is_valid()) {
                phys_mat.instantiate();
                set_physics_material_override(phys_mat);
            }

            _apply_shape_internal(roblox_shape);
            _apply_material_visual();
            _update_phys_mat();
            _update_mass_from_density();

            _gl_congelar(anchored);
            // Forma SOLO si la pieza choca o se puede consultar: en un mapa
            // importado hay miles de piezas decorativas con CanCollide=false y
            // CanQuery=false que no deben pesar en la fisica (en el place de
            // prueba, 2.390 de 6.742). CanCollide seteado ANTES del Parent
            // (patron Roblox: configurar todo y parentear al final) se aplica aqui.
            _forma_activar(_gl_quiere_forma());
            _aplicar_grupo_colision();   // capa/mascara segun su CollisionGroup

            _apply_visibility_and_shadow();
            _apply_lod();
            _gl_apply_occluder();

            // El contact monitor NO se activa aqui: con miles de parts el
            // monitoreo de contactos de Jolt es carisimo. Se activa recien
            // cuando alguien conecta Touched/TouchEnded (_ensure_touch_monitoring).

            // Visibilidad por ancestro (1.15): una Part en ReplicatedStorage/
            // Lighting/Players… no se renderiza. Se re-evalua en CADA ENTER_TREE,
            // asi un reparent (p.ej. de ReplicatedStorage a Workspace) la muestra.
            _apply_ancestor_visibility();
        }
    }

public:
    // La llama el drenaje de la cola. Si la pieza ya se monto (porque un setter
    // la desperto antes), no hace nada.
    void gl_montar_si_pendiente() {
        if (montaje_pendiente) _montar_ahora();
    }
    // La llama la cola de formas (gl_drenar_formas): la forma exacta de la malla, ya.
    // Aparcada o fuera del arbol no: al montarse vuelve a apuntarse si hace falta.
    void gl_forma_exacta_ya() {
        if (!forma_exacta_pendiente) return;
        forma_exacta_pendiente = false;
        if (!is_inside_tree() || montaje_pendiente || malla_propia.is_null()) return;
        forzar_forma_exacta = true;
        _apply_shape_internal(roblox_shape);
        forzar_forma_exacta = false;
    }
    bool gl_montaje_pendiente() const { return montaje_pendiente; }

private:
    // Alta/baja en el registro de piezas sueltas (ver gl_piezas_sueltas arriba).
    // Solo cuenta si esta DENTRO del arbol: fuera no tiene posicion en el mundo.
    void _apuntarse_si_suelta() {
        const uint64_t id = (uint64_t)get_instance_id();
        if (!anchored && is_inside_tree()) gl_suelta_alta(id);
        else                               gl_suelta_baja(id);
    }

    void _apply_ancestor_visibility() {
        // En un ViewportFrame se ve si el visor dibuja (en su propio mundo, ver
        // gl_viewport_frame.h); en uno que es plantilla, no.
        Node* vf = en_vf ? gl_cast<Node>(ObjectDB::get_instance(ObjectID(vf_id))) : nullptr;
        const bool hidden = vf ? !gl_vf_dibuja(vf) : gl_visual_hidden_by_ancestor(this);
        // Aparecer o desaparecer sin salir del arbol tambien cambia su sombra.
        if (is_visible() == hidden && anchored && en_workspace && is_inside_tree())
            gl_sombras_tocar(get_global_position(), size.length() * 0.5f + 1.0f);
        set_visible(!hidden);
        // Fuera del mundo tampoco debe colisionar (en Roblox no está en el espacio
        // físico). Al volver a Workspace se restaura la forma que le toca (la
        // de las que no chocan tambien: es la que ven las consultas). Un
        // miembro de un conjunto no la recupera nunca: la lleva la raiz.
        _gl_forma_apagada((hidden || soy_miembro) ? true : !forma_activa);
        // …y tampoco debe existir para la física. Apagar la colisión no basta:
        // el cuerpo seguía DENTRO del espacio de Jolt, ocupando sitio en el
        // broadphase. En el place de prueba, 6102 de las 7891 piezas estaban
        // GUARDADAS fuera del Workspace (plantillas de brainrots, VFX, tools) y
        // aun así vivían en el espacio físico; sacarlas da un buen mordisco al
        // frame. En Roblox la física existe SOLO dentro del Workspace.
        //
        // process_mode = DISABLED en un CollisionObject3D lo saca del espacio
        // (Godot usa DISABLE_MODE_REMOVE por defecto) y además deja inerte todo
        // lo que cuelgue de él, que es justo lo que hace un almacén de Roblox.
        // Al clonarlo dentro del Workspace, su ENTER_TREE vuelve a pasar por
        // aquí y lo devuelve a la vida.
        // Lo de un ViewportFrame tambien queda inerte (en Roblox ahi no se mueve
        // nada solo), salvo bajo un WorldModel: ahi trabajan sus Motor6D y sus
        // animaciones, pero sigue congelado y sin forma (_gl_congelar y
        // _gl_forma_apagada lo fuerzan), asi que tampoco cae ni choca.
        const bool inerte = hidden || (en_vf && !vf_mundo);
        set_process_mode(inerte ? Node::PROCESS_MODE_DISABLED : Node::PROCESS_MODE_INHERIT);
        _gl_modo_congelado(RigidBody3D::FREEZE_MODE_STATIC);
        _gl_congelar(hidden ? true : anchored);
        // Al final del frame el visor pasa mis instancias de render a su mundo.
        if (vf && !hidden) gl_vf_pieza_entra(vf);
    }

public:
    RobloxPart() {}

    // ── Lo que antes se leia del MeshInstance3D hijo ──────────────────
    // Lo usan el agrupado por zonas (gl_chunks) y el retrato en vivo del menu
    // (ViewportFrame), que antes recorrian los hijos buscando la malla.
    Ref<Mesh>     gl_malla() const     { return _mi_vivo() ? mesh_instance->get_mesh() : Ref<Mesh>(); }
    Ref<Material> gl_material() const  { return material; }
    // El material que el motor DIBUJA, que no siempre es `material`: una pieza
    // con studs lleva puesto el ShaderMaterial de superficies (ver
    // _apply_material_visual). El agrupado por zonas necesita este, no el otro,
    // porque si no juntaba en un mismo grupo piezas que se pintan distinto.
    Ref<Material> gl_material_render() const {
        return _mi_vivo() ? mesh_instance->get_material_override() : Ref<Material>();
    }
    // Su capa de render (la de las sombras propias, gl_capas.h): el agrupado la
    // copia, si no un grupo mezclaria piezas que proyectan con las que no.
    uint32_t gl_capa_render() const {
        return _mi_vivo() ? mesh_instance->get_layer_mask() : 1u;
    }
    // Transformacion de la MALLA en el mundo (incluye el giro del cilindro).
    Transform3D   gl_xf_malla() const {
        if (!_mi_vivo()) return is_inside_tree() ? get_global_transform() : get_transform();
        return (is_inside_tree() ? get_global_transform() : get_transform()) * mesh_instance->get_transform();
    }
    bool gl_se_dibuja() const { return _mi_vivo() && mesh_instance->is_visible(); }
    // La llama RobloxWorkspace cada pocos segundos. Barata: si no cambia, no
    // toca el servidor de render.
    // La llama RBXMesh cuando la pieza es en realidad un FileMesh de la nube.
    void gl_sin_malla_de_nube(bool si) {
        if (sin_malla_nube == si) return;
        sin_malla_nube = si;
        _apply_visibility_and_shadow();
    }
    // La llaman RBXMeshPart, la Union y el FileMesh de RBXMesh. Una malla vacia
    // devuelve la pieza a la de su forma.
    void gl_malla_propia(const Ref<Mesh>& m, bool ajusta, bool caja = false) {
        if (malla_propia == m && malla_propia_ajusta == ajusta && malla_propia_caja == caja) return;
        malla_propia = m;
        malla_propia_ajusta = ajusta;
        malla_propia_caja = caja;
        if (_mi_vivo()) { _apply_material_visual(); _refresh_shape(roblox_shape); }
    }
    Ref<Mesh> gl_malla_propia_actual() const { return malla_propia; }
    void gl_malla_con_colores(bool si) {
        if (malla_con_colores == si) return;
        malla_con_colores = si;
        _apply_material_visual();
    }
    void gl_textura_de_malla(const Ref<Texture2D>& t) {
        textura_sin_tinte = t.is_valid();
        set_part_texture(t);
    }
    // La llama RBXMesh: escala y desplazamiento de lo que se ve.
    void gl_malla_especial(const Vector3& escala, const Vector3& offset) {
        if (malla_escala == escala && malla_offset == offset) return;
        malla_escala = escala;
        malla_offset = offset;
        if (mesh_instance) _apply_shape_internal(roblox_shape);
    }
    void gl_sombra_lejana(bool fuera) {
        if (en_vf) fuera = false;   // ver _gl_vf_resolver
        if (sombra_fuera_zona == fuera) return;
        sombra_fuera_zona = fuera;
        _apply_visibility_and_shadow();
    }

    // Streaming: la llama el barrido del Workspace con la distancia AL CUADRADO
    // (asi no hay raiz por pieza) y los dos radios. Resuelve dibujado y sombra de
    // una sola vez, y solo toca el servidor de render si algo cambio de verdad.
    // Histeresis del 10%: sin ella una pieza justo en el borde parpadearia al
    // andar de un lado a otro.
    void gl_stream_estado(float d2, float r_vis2, float r_som2) {
        // Lo de un ViewportFrame no se aleja del jugador: lo mira su camara.
        if (en_vf) return;
        const bool lejos_vis = fuera_de_alcance ? (d2 > r_vis2 * 0.81f) : (d2 > r_vis2);
        // ── QUIEN PROYECTA SOMBRA: SOLO LO DICE CastShadow ────────────
        // Aqui hubo primero un corte seco por tamaño (nada de menos de 2 studs
        // proyectaba nunca) y luego un corte por tamaño+distancia. Los dos
        // producian lo mismo: unos objetos con sombra y sus vecinos sin ella,
        // sin logica visible.
        // Medido en el banco de sombras: restringir proyectores NO AHORRA NADA
        // (30,0 FPS y 482 draw calls con y sin). Asi que se hace lo de Roblox —
        // proyecta todo lo solido y solo se libra quien tenga CastShadow=false
        // (eso se mira en _apply_visibility_and_shadow). Lo unico que queda es
        // el radio de sombra, que es el mismo para todos.
        const bool lejos_som = d2 > r_som2;
        if (lejos_vis == fuera_de_alcance && lejos_som == sombra_fuera_zona) return;
        fuera_de_alcance = lejos_vis;
        sombra_fuera_zona = lejos_som;
        _apply_visibility_and_shadow();
    }
    bool gl_stream_fuera() const { return fuera_de_alcance; }

    // ── PHYSICS LOD ───────────────────────────────────────────────────
    // La llama el mismo barrido repartido del streaming, que ya trae la
    // distancia al cuadrado calculada: clasificar sale gratis. Un barrido
    // completo del mapa tarda ~0,27 s (400 piezas por frame de 6.500), que
    // sobra para reaccionar a un cambio de velocidad o de cercania.
    //
    // Aqui NO se cambia la frecuencia del motor (no se puede, ver gl_physlod.h):
    // se decide la clase, el divisor de NUESTRA logica y las dos palancas que
    // Godot si tiene por cuerpo — CCD y congelar.
    void gl_lod_evaluar(float d2) {
        if (en_vf) return;   // sin fisica: nada que repartir
        GLPhysLodCfg& c = gl_physlod();
        if (!c.activo) {
            if (lod_congelada) { lod_congelada = false; _gl_congelar(anchored); }
            lod_div = 1; lod_fase = 0;
            return;
        }

        // ── 1. Clase ──────────────────────────────────────────────────
        // "Mecanismo" = conjunto de dos o mas piezas, o algo colgando que
        // trabaja cada paso de fisica (movers, constraints con logica propia).
        // Se detecta por is_physics_processing() en vez de por clase: es un bool
        // ya calculado, no hace falta comparar nombres, y coge tambien lo que
        // añadamos mañana. Solo se repasa cuando cambia el numero de hijos.
        if (lod_hijos_vistos != (uint16_t)get_child_count()) {
            lod_hijos_vistos = (uint16_t)get_child_count();
            lod_hijo_activo = false;
            for (int i = 0; i < get_child_count(); i++) {
                Node* h = get_child(i);
                if (h && h->is_physics_processing()) { lod_hijo_activo = true; break; }
            }
        }
        const bool mecanismo = soy_miembro || !owners_miembros.is_empty() || lod_hijo_activo;
        lod_clase = mecanismo ? GL_LOD_MECANISMO
                              : (anchored ? GL_LOD_ESTATICO : GL_LOD_DINAMICO);

        const float cerca2 = c.cerca * c.cerca;
        const bool  cerca  = d2 <= cerca2;

        // ── 2. Hz objetivo ────────────────────────────────────────────
        float hz;
        if (lod_clase == GL_LOD_MECANISMO) {
            // Se ignora la distancia a proposito: variar el paso de un sistema
            // con restricciones desestabiliza el solver (las piezas vibran, se
            // atraviesan o salen disparadas). Un mecanismo no se toca nunca.
            hz = c.hz_mecanismo;
        } else if (lod_clase == GL_LOD_ESTATICO) {
            hz = cerca ? c.hz_cerca : c.hz_lejos;
        } else {
            if (!cerca) {
                hz = c.hz_lejos;
            } else {
                hz = c.hz_base;
                const float v = get_linear_velocity().length();
                if (v > c.vel_umbral)
                    hz += Math::floor((v - c.vel_umbral) / c.vel_paso) * c.hz_paso;
                hz = Math::min(hz, c.hz_max);
            }
        }
        // Empuje por evento: si alguien escucha Touched, el impacto tiene que
        // detectarse bien aunque la pieza este lejos.
        if (touch_monitoring) hz = Math::max(hz, c.hz_cerca);

        // ── 3. Divisor + DESFASE ──────────────────────────────────────
        const uint8_t div = gl_lod_divisor(hz);
        if (div != lod_div) {
            lod_div  = div;
            // Sin esto todas las piezas del grupo trabajarian el mismo frame:
            // un frame con el 100% del trabajo y el siguiente con el 0%. Ese
            // diente de sierra ES un bajon periodico de FPS.
            lod_fase = div > 1 ? (uint8_t)((uint64_t)get_instance_id() % (uint64_t)div) : 0;
        }

        // ── 4. CCD: el unico "mas Hz" real por cuerpo ─────────────────
        // Histeresis: se enciende al llegar al umbral y no se apaga hasta caer
        // al 75%, para que una pieza rondando el limite no reconfigure el
        // solver en cada barrido.
        const bool quiero_ccd = (lod_clase == GL_LOD_MECANISMO) || hz >= c.hz_ccd;
        if (quiero_ccd && !lod_ccd) {
            lod_ccd = true;
            set_use_continuous_collision_detection(true);
        } else if (!quiero_ccd && lod_ccd && hz < c.hz_ccd * 0.75f) {
            lod_ccd = false;
            set_use_continuous_collision_detection(false);
        }

        // ── 5. Congelar lo lejano y quieto: el "0 Hz" ─────────────────
        // Mejor que bajar a 30 Hz: un cuerpo congelado sale del broadphase
        // dinamico y cuesta cero, no la mitad.
        //
        // Reglas de seguridad, en este orden:
        //   · Anclada, mecanismo o con Touched -> no se toca nunca.
        //   · Si la congelo OTRO (la red, un script, el conjunto), no se toca:
        //     el LOD solo deshace lo que hizo el LOD.
        //   · Solo se congela lo que ya esta EN REPOSO. Congelar algo que cae
        //     lo dejaria clavado en el aire.
        if (!c.congelar_lejanas || anchored || lod_clase == GL_LOD_MECANISMO || touch_monitoring) {
            if (lod_congelada) { lod_congelada = false; _gl_congelar(false); }
            return;
        }
        if (lod_congelada && !is_freeze_enabled()) lod_congelada = false;  // lo descongelo otro

        const float lejos2 = cerca2 * c.congelar_factor * c.congelar_factor;
        if (!lod_congelada) {
            if (d2 > lejos2) c.n_lejos++;
            if (is_freeze_enabled()) {                       // no es mia: fuera
                if (d2 > lejos2) c.n_ajenas++;
                return;
            }
            if (d2 > lejos2
                && (get_linear_velocity().length_squared()  >= 0.25f
                 || get_angular_velocity().length_squared() >= 0.25f)) c.n_moviendo++;
            // En reposo = dormida por Jolt, no "lenta en esta muestra": al chocar
            // con el suelo la velocidad cae a 0 un paso con la pieza aun hundida,
            // y congelarla ahi la dejaba clavada dentro. Jolt solo la duerme
            // tras ~0,5 s quieta de verdad. (Lo tapaba la red, que descongelaba
            // todo cada 0,05 s.)
            if (d2 > lejos2 && is_sleeping()) {
                lod_congelada = true;
                _gl_modo_congelado(RigidBody3D::FREEZE_MODE_STATIC);
                _gl_congelar(true);
            }
        } else if (d2 < lejos2 * 0.49f) {                    // histeresis del 30%
            lod_congelada = false;
            _gl_congelar(false);
        }
    }

    // ¿Le toca a esta pieza el trabajo logico de este paso de fisica?
    bool    gl_lod_tickea() const { return gl_lod_toca(lod_div, lod_fase); }
    uint8_t gl_lod_clase() const  { return lod_clase; }
    uint8_t gl_lod_div() const    { return lod_div; }
    bool    gl_lod_ccd() const    { return lod_ccd; }
    bool    gl_lod_congelada() const { return lod_congelada; }
    // Lo descongela si lo congelo el LOD (un NPC lejano al que le mandan andar).
    void    gl_lod_despertar() { if (lod_congelada) { lod_congelada = false; _gl_congelar(false); } }
    bool    gl_en_workspace() const  { return en_workspace; }
    // La usa gl_chunks para apagar la pieza cuando su geometria pasa al MultiMesh.
    void gl_ocultar_por_chunk(bool oculta) {
        vis_oculta_chunk = oculta;
        if (_mi_vivo()) mesh_instance->set_visible(vis_visible && !vis_oculta_chunk);
    }
    // La llama GLSombras (ui/gl_sombras.h): si la anclada que movia un script
    // lleva `espera` frames quieta, vuelve al mapa de sombra guardado (y su
    // baldosa se redibuja con ella). true = ya no hace falta mirarla.
    bool gl_sombra_parada(uint64_t frame, uint64_t espera) {
        if (!sombra_moviendo) return true;
        if (frame - sombra_ultimo_mov < espera) return false;
        sombra_moviendo = false;
        _apply_visibility_and_shadow();
        return true;
    }

    // Activa el monitoreo de contactos SOLO cuando el juego lo necesita
    // (alguien conecto Touched/TouchEnded). Idempotente.
    void _ensure_touch_monitoring() {
        if (touch_monitoring) return;
        touch_monitoring = true;
        set_contact_monitor(true);
        set_max_contacts_reported(10);
        connect("body_entered", Callable(this, "_on_body_entered"));
        connect("body_exited",  Callable(this, "_on_body_exited"));
        _ensure_touch_area();
    }

    // ── Y UN AREA, QUE ES LA QUE DE VERDAD FUNCIONA ───────────────────────
    // El contact_monitor de un RigidBody solo reporta si el cuerpo es DINAMICO.
    // Una pieza con Anchored = true es un cuerpo estatico en Jolt, y los
    // estaticos no generan callbacks de contacto: medido, Touched y TouchEnded
    // no saltaban NUNCA sobre una pieza anclada. Y en Roblox si saltan — es el
    // caso normal (el boton de la tienda, la zona de venta, el suelo que mata…).
    // Un Area3D con la misma forma detecta el solape sea el cuerpo estatico o
    // no, que es justo la semantica de Touched en Roblox.
    // Se crea SOLO cuando el juego conecta Touched, asi que no cuesta nada en
    // las piezas que no lo usan.
    void _ensure_touch_area() {
        if (touch_area) return;
        touch_area = memnew(Area3D);
        touch_area->set_name("__gl_touch__");
        touch_area->set_meta(GL_META_INTERNO, true);   // interno: no se clona ni se guarda
        touch_area->set_monitoring(true);
        touch_area->set_monitorable(false);
        // Como el area del ProximityPrompt: el juego mete sus cosas en capas
        // propias, asi que se escucha en todas.
        // Menos la capa de consultas: Touched sigue sin ver las piezas que no
        // chocan (antes ni tenian forma).
        touch_area->set_collision_mask(~GL_CAPA_CONSULTA);
        CollisionShape3D* cs = memnew(CollisionShape3D);
        Ref<Shape3D> f = forma_propia;
        if (f.is_null()) {
            Ref<BoxShape3D> caja; caja.instantiate();
            caja->set_size(size);
            f = caja;
        }
        cs->set_shape(f);
        touch_area->add_child(cs);
        add_child(touch_area, false, Node::INTERNAL_MODE_BACK);
        touch_area->connect("body_entered", Callable(this, "_on_body_entered"));
        touch_area->connect("body_exited",  Callable(this, "_on_body_exited"));
    }

    // Si el que entra es un personaje se reporta su HumanoidRootPart, no el
    // personaje entero: en Roblox Touched entrega una PARTE y los scripts hacen
    // `hit.Parent:FindFirstChild("Humanoid")` (1.14.11).
    // Los handlers Lua corren DENTRO del flush de física: se marca el flag
    // para que Destroy/.Parent difieran el remove_child (ver gl_runtime.h).
    void _on_body_entered(Node* body) {
        if (body && body != this && can_touch) {
            const bool prev = gl_en_callback_fisica();
            gl_en_callback_fisica() = true;
            emit_signal("Touched", gl_touch_reported_node(body));
            gl_en_callback_fisica() = prev;
        }
    }
    void _on_body_exited(Node* body) {
        if (body && body != this && can_touch) {
            const bool prev = gl_en_callback_fisica();
            gl_en_callback_fisica() = true;
            emit_signal("TouchEnded", gl_touch_reported_node(body));
            gl_en_callback_fisica() = prev;
        }
    }

    // Como tarea del planificador y no con lua_pcall: el debounce tipico de
    // Touched (`task.wait(1)` dentro del manejador) daba "attempt to yield
    // across metamethod/C-call boundary".
    void _on_luau_part_touched(Node* p_hit, int p_ref, int64_t ptr_L) {
        lua_State* L = (lua_State*)ptr_L;
        // El estado Luau puede haberse cerrado (script destruido/recargado): no usarlo.
        if (!gl_state_alive(L) || !p_hit) return;
        lua_State* th = lua_newthread(L);
        lua_getref(L, p_ref);
        if (lua_isfunction(L, -1)) {
            lua_xmove(L, th, 1);
            GodotObjectWrapper* wrap = (GodotObjectWrapper*)lua_newuserdata(th, sizeof(GodotObjectWrapper));
            gow_set(wrap, p_hit);
            luaL_getmetatable(th, "GodotObject");
            lua_setmetatable(th, -2);
            gl_resume_handler(th, 1);
        } else { lua_pop(L, 1); }
        lua_pop(L, 1);
    }

    // ── Anchored ───────────────────────────────────────────────────
    // Anclada = la fisica no la mueve (como en Roblox). Se congela en modo
    // STATIC y no en el KINEMATIC por defecto: un mapa importado trae miles de
    // parts ancladas y en kinematico el simulador las sigue procesando (6000
    // parts ~ 10 FPS). En estatico salen del calculo y se pueden seguir moviendo
    // por codigo cambiando su transform.
    // ── OJO con el early-out ─────────────────────────────────────────
    // Anchored era la segunda escritura mas CARA de todo el puente: 152 us por
    // asignacion (37 veces mas que una rotacion), 799 veces cada 10 s = 122 ms.
    // El motivo es que set_freeze_enabled habla con el servidor de fisica, y en
    // Jolt pasar un cuerpo de dinamico a estatico lo mueve de capa de broadphase
    // — con 6.500 cuerpos en la escena eso no es gratis.
    //
    // Los scripts de Roblox reasignan Anchored constantemente con el MISMO valor
    // (al clonar, al configurar, en bucles). Si no cambia, no hay nada que
    // decirle a la fisica. El estado inicial lo aplica ENTER_TREE, asi que
    // saltarse una asignacion redundante no puede dejar el cuerpo desincronizado.
    void set_anchored(bool a) {
        if (anchored == a) return;
        anchored = a;
        // En Roblox, si UNA pieza del conjunto esta anclada, el conjunto ENTERO
        // lo esta. Pero eso NO reagrupa nada: los mismos miembros, la misma
        // raiz, la misma forma — solo cambia si la raiz se congela. Rehacer los
        // conjuntos enteros por cada escritura de Anchored era un tiron gratis
        // varias veces por segundo (los scripts de Roblox escriben Anchored sin
        // parar). Ahora solo se pide el repaso barato del anclaje.
        gl_anclaje_conjunto_cambio();
        // Cambia la respuesta a "¿este weld tiene trabajo?": se despiertan los
        // constraints dormidos (ver gl_gen_anclajes en gl_runtime.h).
        gl_anclajes_cambiaron();
        _apuntarse_si_suelta();
        // Anchored manda sobre el LOD: si el juego decide anclar o soltar, la
        // congelacion pasa a ser suya y el LOD deja de reclamarla.
        lod_congelada = false;
        _gl_modo_congelado(RigidBody3D::FREEZE_MODE_STATIC);
        // Un miembro sigue congelado: lo lleva la raiz. Es el idioma de Roblox
        // (soldar el modelo y luego desanclar todas sus piezas), y soltarlo lo
        // dejaba como cuerpo dinamico sin forma, cayendo entre tick y tick.
        _gl_congelar(a || soy_miembro);
        // Una malla propia choca exacta anclada y con su casco suelta.
        if (malla_propia.is_valid() && malla_propia_ajusta) _refresh_shape(roblox_shape);
        _apply_visibility_and_shadow();   // la capa de sombra (quieta o de cada frame)
    }
    bool get_anchored() const { return anchored; }

    // ── Color ──────────────────────────────────────────────────────
    //
    // TODOS estos setters salen antes si el valor no cambia. No es una manía:
    // los scripts de Roblox reasignan propiedades con el mismo valor sin parar
    // (es el idioma normal: "asegúrate de que está en rojo" cada frame), y aquí
    // cada asignación reconstruía de verdad la forma en Jolt (152 us medidos en
    // Anchored), volvía a hornear el occluder o fabricaba ~13 Strings para la
    // clave del material compartido. Comparar primero es gratis.
    void set_color(Color c) {
        if (color == c) return;
        color = c;
        _apply_material_visual();   // nueva clave visual -> material del cache
    }
    Color get_color() const { return color; }

    // ── Size ───────────────────────────────────────────────────────
    void set_size(Vector3 s) {
        if (size == s) return;
        // Lo que tapaba con el tamaño de antes tambien se redibuja (sombras propias).
        if (anchored && en_workspace && vis_visible && is_inside_tree())
            gl_sombras_tocar(get_global_position(), size.length() * 0.5f + 1.0f);
        size = s;
        _update_shape_dims();
        _apply_visibility_and_shadow();
    }
    Vector3 get_size() const { return size; }

    // ── Transparency ───────────────────────────────────────────────
    void set_transparency(float t) {
        const float nuevo = Math::clamp(t, 0.0f, 1.0f);
        if (transparency == nuevo) return;
        transparency = nuevo;
        _apply_material_visual();
        // Al llegar a 1 la pieza se saca del render; al bajar de 1, vuelve.
        _apply_visibility_and_shadow();
        _gl_apply_occluder();   // una pieza invisible ya no puede tapar
    }
    float get_transparency() const { return transparency; }

    // ── Reflectance ────────────────────────────────────────────────
    void set_reflectance(float r) {
        const float nuevo = Math::clamp(r, 0.0f, 1.0f);
        if (reflectance == nuevo) return;
        reflectance = nuevo;
        _apply_material_visual();
    }
    float get_reflectance() const { return reflectance; }

    // ── Shape ──────────────────────────────────────────────────────
    void set_roblox_shape(int s) {
        if (roblox_shape == s) return;
        roblox_shape = s;
        _apply_shape_internal(s);
        _apply_material_visual();
    }
    int get_roblox_shape() const { return roblox_shape; }

    // ── Material ───────────────────────────────────────────────────
    void set_roblox_material(int m) {
        if (roblox_material == m) return;
        roblox_material = m;
        _apply_material_visual();
    }
    int  get_roblox_material() const { return roblox_material; }

    // GetTouchingParts: las piezas cuya forma se cruza con la de esta. Como en
    // Roblox, dos piezas que solo se rozan sin solaparse no cuentan, y una
    // pieza con CanCollide = false solo ve algo si tiene Touched conectado.
    // Un personaje cuenta como su HumanoidRootPart, igual que en Touched.
    // La forma de la pieza y donde esta, para usarla de consulta (GetPartsInPart,
    // GetTouchingParts). Sin forma propia (nacio sin colision), su caja.
    void gl_forma_en_mundo(Ref<Shape3D>& f, Transform3D& t) {
        f = forma_propia;
        if (f.is_null()) {
            Ref<BoxShape3D> caja; caja.instantiate();
            caja->set_size(size);
            f = caja;
        }
        t = get_global_transform();
        if (forma_girada) t.basis = t.basis * Basis(Vector3(0, 0, 1), 1.5707964f);
    }

    void gl_piezas_tocando(Vector<Node*>& out) {
        if (!is_inside_tree()) return;
        if (!can_collide && !touch_area) return;
        Ref<World3D> mundo = get_world_3d();
        PhysicsDirectSpaceState3D* space = mundo.is_valid() ? mundo->get_direct_space_state() : nullptr;
        if (!space) return;
        Ref<Shape3D> f;
        Transform3D t;
        gl_forma_en_mundo(f, t);
        Ref<PhysicsShapeQueryParameters3D> q;
        q.instantiate();
        q->set_shape(f);
        q->set_transform(t);
        // La capa de consultas no: las piezas que no chocan no se tocan.
        q->set_collision_mask(~GL_CAPA_CONSULTA);
        TypedArray<RID> excluir;
        excluir.push_back(get_rid());
        q->set_exclude(excluir);
        TypedArray<Dictionary> res = space->intersect_shape(q, 256);
        const uint64_t mi_raiz = gl_raiz_ensamblaje((uint64_t)get_instance_id());
        for (int i = 0; i < res.size(); i++) {
            Dictionary d = res[i];
            Node* n = gl_cast<Node>(d.get("collider", Variant()).operator Object*());
            if (!n || n == this) continue;
            if (RobloxPart* rp = gl_cast<RobloxPart>(n)) {
                // El miembro del conjunto que se toco, no la raiz; y nada de
                // contarse con las piezas soldadas a ella misma.
                n = rp->gl_pieza_de_forma((int)d.get("shape", -1));
                if (!n || n == this) continue;
                if (gl_raiz_ensamblaje((uint64_t)n->get_instance_id()) == mi_raiz) continue;
            } else {
                n = n->get_node_or_null(NodePath("HumanoidRootPart"));
                if (!n) continue;
            }
            if (out.find(n) < 0) out.push_back(n);
        }
    }

    // ── Collision ──────────────────────────────────────────────────
    // Transparencia por borrado de pixeles SOLO en esta pieza (aunque el jugador
    // no lo tenga activado). Util para optimizar a mano desde el diseño del juego.
    void set_dither_transparency(bool b) {
        if (dither_transparency == b) return;
        dither_transparency = b;
        _apply_material_visual();            // cambia la clave visual del material
        _apply_visibility_and_shadow();
    }
    bool get_dither_transparency() const { return dither_transparency; }

    void set_can_collide(bool c) {
        if (can_collide == c && (c ? forma_id >= 0 : true)) return;
        can_collide = c;
        const bool forma = _gl_quiere_forma();
        if (forma) _ensure_collider();   // se crea bajo demanda (no existe si nacio sin colision)
        _forma_activar(forma);
        _aplicar_grupo_colision();       // de su grupo a la capa de consultas, o al reves
    }
    bool get_can_collide() const { return can_collide; }

    // ══════════════════════════════════════════════════════════════════
    //  CONJUNTOS (assemblies) — ver gl_assembly.h
    //  Estas tres las usa el constructor de conjuntos del Workspace.
    // ══════════════════════════════════════════════════════════════════

    // Quita las formas de los miembros que llevara pegadas y vuelve a ser una
    // pieza normal. Idempotente.
    void gl_conjunto_deshacer() {
        for (uint32_t i = 0; i < owners_miembros.size(); i++)
            if (owners_miembros[i] >= 0) remove_shape_owner((uint32_t)owners_miembros[i]);
        owners_miembros.clear();
        owners_pieza.clear();
        if (soy_miembro) {
            soy_miembro = false;
            // Recupera su propia colision segun su CanCollide de siempre.
            _forma_activar(_gl_quiere_forma());
        }
        // Y su propia fisica: la congelacion era del conjunto. Sin esto, la
        // pieza de una soldadura destruida se quedaba clavada en el aire. Si
        // vuelve a un conjunto en esta misma pasada, se congela otra vez.
        _gl_congelar(anchored);
        _aplicar_grupo_colision();
        set_center_of_mass_mode(RigidBody3D::CENTER_OF_MASS_MODE_AUTO);
        _update_mass_from_density();
    }

    // La pieza pasa a ser MIEMBRO (no raiz): se queda sin colision propia — la
    // lleva la raiz — y congelada. Su posicion ya se la reimpone el tick del
    // weld, que es justo el comportamiento de seguidor que hace falta.
    void gl_conjunto_ser_miembro() {
        soy_miembro = true;
        _gl_forma_apagada(true);
        _gl_modo_congelado(RigidBody3D::FREEZE_MODE_STATIC);
        _gl_congelar(true);
    }

    // Le cuelga a la RAIZ la forma de un miembro, con su transformacion
    // relativa. Asi el conjunto colisiona como una sola pieza compuesta.
    void gl_conjunto_anadir_forma(const Ref<Shape3D>& forma, const Transform3D& relativa, uint64_t pieza_id = 0) {
        if (forma.is_null()) return;
        const int oid = (int)create_shape_owner(this);
        shape_owner_add_shape((uint32_t)oid, forma);
        shape_owner_set_transform((uint32_t)oid, relativa);
        owners_miembros.push_back(oid);
        owners_pieza.push_back(pieza_id);
        // Una raiz que no choca pero lleva miembros que si: deja de ser
        // fantasma (su propia forma se apaga y el cuerpo vuelve a su grupo).
        _forma_activar(_gl_quiere_forma());
        _aplicar_grupo_colision();
    }

    // ¿De que pieza es la forma numero `shape_idx` de este cuerpo? La propia o la
    // de un miembro del conjunto. Los rayos y las consultas devuelven asi la
    // pieza que se toco de verdad, no la raiz del conjunto soldado.
    Node* gl_pieza_de_forma(int shape_idx) {
        if (owners_miembros.is_empty() || shape_idx < 0) return this;
        const uint32_t owner = shape_find_owner(shape_idx);
        for (uint32_t i = 0; i < owners_miembros.size() && i < owners_pieza.size(); i++) {
            if ((uint32_t)owners_miembros[i] != owner) continue;
            Node* m = gl_cast<Node>(ObjectDB::get_instance(ObjectID(owners_pieza[i])));
            return m ? m : this;
        }
        return this;
    }

    Ref<Shape3D> gl_forma_propia() const { return forma_propia; }
    bool gl_forma_girada() const { return forma_girada; }
    bool gl_es_miembro() const { return soy_miembro; }

    // Masa REAL de la pieza para el conjunto (Massless no aporta, como Roblox).
    float gl_masa_propia() const {
        if (massless) return 0.0f;
        return Math::max(density * size.x * size.y * size.z, 0.0f);
    }
    // La raiz recibe la masa y el centro de masas del conjunto entero.
    void gl_conjunto_masa(float masa, const Vector3& centro_local) {
        set_mass(Math::max(masa, 0.001f));
        set_center_of_mass_mode(RigidBody3D::CENTER_OF_MASS_MODE_CUSTOM);
        set_center_of_mass(centro_local);
    }

    void set_root_priority(int p) {
        const int np = Math::clamp(p, -127, 127);   // el rango de Roblox
        if (root_priority == np) return;
        root_priority = np;
        gl_ensamblajes_cambiaron();   // puede cambiar quien es la raiz
    }
    int  get_root_priority() const { return root_priority; }

    void set_can_touch(bool c)  { can_touch = c; }
    bool get_can_touch() const  { return can_touch; }
    void set_can_query(bool c) {
        if (can_query == c) return;
        can_query = c;
        if (can_collide) return;   // si choca, la forma ya esta encendida
        const bool forma = _gl_quiere_forma();
        if (forma) _ensure_collider();
        _forma_activar(forma);
    }
    bool get_can_query() const  { return can_query; }

    // ── CollisionGroup (Roblox) ───────────────────────────────────────
    void set_collision_group(const String& g) {
        const String nuevo = g.is_empty() ? String("Default") : g;
        if (collision_group == nuevo && col_gen_visto == gl_colgrupos().generacion) return;
        collision_group = nuevo;
        col_idx = gl_colgrupo_indice(nuevo);
        _aplicar_grupo_colision();
    }
    String get_collision_group() const { return collision_group; }

    // Lo llama tambien el barrido del Workspace cuando cambia la matriz de
    // colisionabilidad, para que las piezas ya creadas se enteren.
    void _aplicar_grupo_colision() {
        // CollisionObject3D tampoco compara: cada set va derecho al
        // PhysicsServer. Casi todas las piezas de un place estan en el grupo
        // Default, o sea que estas dos escrituras eran identicas para miles de
        // piezas y aun asi se pagaban una por una.
        // Si no choca, su forma (si la lleva) es solo para las consultas.
        const bool fantasma = _gl_es_fantasma();
        const uint32_t capa = fantasma ? GL_CAPA_CONSULTA : (1u << (uint32_t)col_idx);
        const uint32_t masc = fantasma ? 0u : gl_colgrupo_mascara(col_idx);
        if (get_collision_layer() != capa) set_collision_layer(capa);
        if (get_collision_mask()  != masc) set_collision_mask(masc);
        col_gen_visto = gl_colgrupos().generacion;
    }
    bool gl_grupo_al_dia() const { return col_gen_visto == gl_colgrupos().generacion; }

    // El LOD de dibujado depende de la vista elegida, asi que hay que rehacerlo
    // cuando el jugador cambia la calidad EN MARCHA. Lo llama el barrido del
    // Workspace, que ya pasa por todas las piezas; comparar dos enteros cuando
    // no ha cambiado nada no cuesta.
    void gl_lod_refrescar(uint32_t gen) {
        if (lod_gen_visto == gen) return;
        lod_gen_visto = gen;
        _apply_lod();
    }

    // ── Rendering ──────────────────────────────────────────────────
    void set_cast_shadow(bool b) {
        cast_shadow = b;
        _apply_visibility_and_shadow();   // respeta ademas el filtro por tamaño
    }
    bool get_cast_shadow() const { return cast_shadow; }

    void set_locked(bool b)   { locked = b; }
    bool get_locked() const   { return locked; }
    void set_massless(bool b) { massless = b; _update_mass_from_density(); }
    bool get_massless() const { return massless; }

    // ── Physical material ──────────────────────────────────────────
    void  set_density(float d)          { density = Math::max(d, 0.001f); _update_mass_from_density(); }
    float get_density() const           { return density; }
    void  set_friction_val(float f)     { friction_val = Math::max(f, 0.0f); _update_phys_mat(); }
    float get_friction_val() const      { return friction_val; }
    void  set_elasticity(float e)       { elasticity = Math::clamp(e, 0.0f, 1.0f); _update_phys_mat(); }
    float get_elasticity() const        { return elasticity; }
    void  set_friction_weight(float w)  { friction_weight = Math::max(w, 0.0f); }
    float get_friction_weight() const   { return friction_weight; }
    void  set_elasticity_weight(float w){ elasticity_weight = Math::max(w, 0.0f); }
    float get_elasticity_weight() const { return elasticity_weight; }

    float get_mass_roblox() const {
        if (massless) return 0.0f;
        return density * size.x * size.y * size.z;
    }

    // ── Surfaces ───────────────────────────────────────────────────
    // Cambiar una cara cambia el material (studs o liso), asi que hay que
    // volver a pedirlo. Salen antes si el valor no cambia: los scripts
    // reasignan superficies sin parar.
    void set_top_surface(int s)     { if (top_surface == s) return; top_surface = s; _apply_material_visual(); }
    int  get_top_surface() const    { return top_surface; }
    void set_bottom_surface(int s)  { if (bottom_surface == s) return; bottom_surface = s; _apply_material_visual(); }
    int  get_bottom_surface() const { return bottom_surface; }
    void set_front_surface(int s)   { if (front_surface == s) return; front_surface = s; _apply_material_visual(); }
    int  get_front_surface() const  { return front_surface; }
    void set_back_surface(int s)    { if (back_surface == s) return; back_surface = s; _apply_material_visual(); }
    int  get_back_surface() const   { return back_surface; }
    void set_left_surface(int s)    { if (left_surface == s) return; left_surface = s; _apply_material_visual(); }
    int  get_left_surface() const   { return left_surface; }
    void set_right_surface(int s)   { if (right_surface == s) return; right_surface = s; _apply_material_visual(); }
    int  get_right_surface() const  { return right_surface; }

    // ── Roblox Position/Orientation/Velocity aliases ───────────────
    // Fuera del arbol, global_position de Godot avisa por consola y devuelve la
    // identidad. En Roblox una parte sin padre conserva su Position, asi que se
    // usa la local (es el caso normalisimo de Instance.new + configurar + Parent).
    void    set_position_roblox(Vector3 p)    { if (is_inside_tree()) set_global_position(p); else set_position(p); }
    Vector3 get_position_roblox() const       { return is_inside_tree() ? get_global_position() : get_position(); }
    void    set_orientation_roblox(Vector3 d) { set_rotation_degrees(d); }
    Vector3 get_orientation_roblox() const    { return get_rotation_degrees(); }
    void    set_velocity_roblox(Vector3 v)    { set_linear_velocity(v); }
    Vector3 get_velocity_roblox() const       { return get_linear_velocity(); }
    void    set_angular_velocity_roblox(Vector3 v) { set_angular_velocity(v); }
    Vector3 get_angular_velocity_roblox() const    { return get_angular_velocity(); }
};

// ── Part (1.15): el nombre SIN "Roblox" ──────────────────────────────────────
// El usuario ve y crea "Part", no "RobloxPart". Es una subclase fina: hereda TODO
// el comportamiento y, al ser Part IS-A RobloxPart, funciona en cada sitio que
// castea a RobloxPart. La clase base RobloxPart se registra como INTERNA (oculta
// del "Add Node" del editor) pero sigue siendo cargable, así las escenas viejas
// con type="RobloxPart" no se rompen; el actualizador las migra a Part.
class Part : public RobloxPart {
    GDCLASS(Part, RobloxPart);
protected:
    static void _bind_methods() {}
public:
    Part() {}
};

#include "roblox_part_consultas.h"   // asientos, raiz del conjunto y rayos de Roblox
#include "roblox_part_material.h"    // color, Material de Roblox, texturas y studs

// Drena la cola de montaje con presupuesto de tiempo. Va con un cursor en vez de
// borrar por delante: quitar del principio de un vector es O(n) y la cola puede
// tener miles de piezas.
inline void gl_drenar_montajes() {
    GLColaMontaje& c = gl_cola_montaje();
    if (c.cursor >= c.cola.size()) {
        if (!c.cola.is_empty()) { c.cola.clear(); c.cursor = 0; }
        return;
    }
    const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
    while (c.cursor < c.cola.size()) {
        if (Time::get_singleton()->get_ticks_usec() - t0 > GL_US_MONTAJE_COLA) break;
        Object* o = ObjectDB::get_instance(c.cola[c.cursor]);
        c.cursor++;
        RobloxPart* p = gl_cast<RobloxPart>(o);
        if (p && p->is_inside_tree()) p->gl_montar_si_pendiente();
    }
    if (c.cursor >= c.cola.size()) { c.cola.clear(); c.cursor = 0; }
    if (gl_perfil_montaje()) {
        static uint64_t ultimo = 0;
        const uint64_t ahora = Time::get_singleton()->get_ticks_msec();
        if (ahora - ultimo > 2000) {
            ultimo = ahora;
            UtilityFunctions::print("[MON] cola=", (int64_t)(c.cola.size() - c.cursor),
                                    "  directo=", (int64_t)c.montadas_directo,
                                    "  encolada=", (int64_t)c.montadas_en_cola);
        }
    }
}

#include "gl_malla_propia.h"   // MeshPart, Union y FileMesh con su malla de verdad
#include "gl_variantes.h"      // MaterialVariant: texturas propias del juego

#endif // ROBLOX_PART_H
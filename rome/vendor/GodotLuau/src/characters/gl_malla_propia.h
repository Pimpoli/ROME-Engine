#ifndef GL_MALLA_PROPIA_H
#define GL_MALLA_PROPIA_H

// ════════════════════════════════════════════════════════════════════
//  Malla propia de una pieza: la de una MeshPart, la de una Union (el CSG
//  de sus piezas) o el FileMesh de una SpecialMesh. Va aparte de
//  roblox_part.h (2.000 lineas) y se incluye al final de el.
//
//  Como en Roblox:
//   · MeshPart y Union: la caja de la malla se estira al Size de la pieza, y
//     chocan con su malla (exacta si esta anclada, el casco convexo si no; con
//     CollisionFidelity = Box, como su caja).
//   · FileMesh: la malla a su tamaño por SpecialMesh.Scale y desplazada por
//     Offset. Solo cambia el dibujo: la pieza sigue chocando como su forma.
//
//  Las mallas salen de assets/rbx/<id>.<ext> (ver gl_asset.h): las escribe el
//  importador de .gdlu, o las pone el usuario a mano (un .obj exportado de
//  Studio vale). Sin malla la pieza se queda con su caja, como hasta ahora.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/convex_polygon_shape3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/templates/hashfuncs.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include "gl_asset.h"

// Forma de choque de una malla con su escala y desplazamiento ya aplicados (la
// forma va en el propio cuerpo y Jolt no admite escalar formas a lo loco).
// Compartida: miles de piezas iguales usan la misma.
inline HashMap<uint64_t, Ref<Shape3D>>& gl_formas_de_malla() {
    static HashMap<uint64_t, Ref<Shape3D>> m;
    return m;
}
inline uint64_t gl_forma_clave(const Ref<Mesh>& m, const Vector3& esc, const Vector3& off, bool concava) {
    uint32_t h = hash_murmur3_one_64((uint64_t)m->get_instance_id());
    h = hash_murmur3_one_float(esc.x, h); h = hash_murmur3_one_float(esc.y, h); h = hash_murmur3_one_float(esc.z, h);
    h = hash_murmur3_one_float(off.x, h); h = hash_murmur3_one_float(off.y, h); h = hash_murmur3_one_float(off.z, h);
    return ((uint64_t)hash_fmix32(h) << 1) | (concava ? 1u : 0u);
}
// La forma si ya esta hecha; nula si no (no la construye).
inline Ref<Shape3D> gl_forma_hecha(const Ref<Mesh>& m, const Vector3& esc, const Vector3& off, bool concava) {
    if (Ref<Shape3D>* hay = gl_formas_de_malla().getptr(gl_forma_clave(m, esc, off, concava))) return *hay;
    return Ref<Shape3D>();
}
// Las caras (o el casco convexo) de cada malla, sin escalar, leidas UNA vez por
// malla. Leerlas de una malla importada es leerlas de la GPU: get_faces() y
// create_convex_shape() esperan a que la grafica termine todo lo pendiente. Se
// hacia por cada tamaño distinto de la misma malla (16 ms de esperas en el
// primer frame de Build a Mountain).
inline HashMap<uint64_t, PackedVector3Array>& gl_puntos_de_malla() {
    static HashMap<uint64_t, PackedVector3Array> m;
    return m;
}
inline PackedVector3Array gl_puntos_malla(const Ref<Mesh>& m, bool concava) {
    HashMap<uint64_t, PackedVector3Array>& cache = gl_puntos_de_malla();
    const uint64_t clave = ((uint64_t)m->get_instance_id() << 1) | (concava ? 1u : 0u);
    if (const PackedVector3Array* hay = cache.getptr(clave)) return *hay;
    // Las mallas que hace el importador (MeshPart del .gdlu, uniones) traen sus
    // caras dentro (meta _gl_caras, que va en el .res): sin GPU de por medio.
    static const StringName k_caras("_gl_caras");
    const bool con_caras = m->has_meta(k_caras);
    PackedVector3Array p;
    if (concava) {
        p = con_caras ? PackedVector3Array(m->get_meta(k_caras)) : m->get_faces();
    } else if (con_caras) {
        // El casco lo calcula el servidor de fisica con los vertices (sin repetir).
        const PackedVector3Array caras = m->get_meta(k_caras);
        HashSet<Vector3> vistos;
        for (int i = 0; i < caras.size(); i++) {
            if (vistos.has(caras[i])) continue;
            vistos.insert(caras[i]);
            p.push_back(caras[i]);
        }
    } else {
        const Ref<ConvexPolygonShape3D> base = m->create_convex_shape(true, false);
        if (base.is_valid()) p = base->get_points();
    }
    if (cache.size() >= 2048) cache.clear();   // tope: ids de mallas ya liberadas
    cache.insert(clave, p);
    return p;
}

inline Ref<Shape3D> gl_forma_de_malla(const Ref<Mesh>& m, const Vector3& esc, const Vector3& off, bool concava) {
    const uint64_t clave = gl_forma_clave(m, esc, off, concava);
    HashMap<uint64_t, Ref<Shape3D>>& cache = gl_formas_de_malla();
    if (Ref<Shape3D>* hay = cache.getptr(clave)) return *hay;

    Ref<Shape3D> forma;
    if (concava) {
        PackedVector3Array caras = gl_puntos_malla(m, true);
        for (int i = 0; i < caras.size(); i++) caras[i] = caras[i] * esc + off;
        Ref<ConcavePolygonShape3D> c;
        c.instantiate();
        c->set_faces(caras);
        c->set_backface_collision_enabled(true);   // mallas abiertas: chocan por las dos caras
        forma = c;
    } else {
        PackedVector3Array puntos = gl_puntos_malla(m, false);
        for (int i = 0; i < puntos.size(); i++) puntos[i] = puntos[i] * esc + off;
        Ref<ConvexPolygonShape3D> c;
        c.instantiate();
        c->set_points(puntos);
        forma = c;
    }
    cache[clave] = forma;
    gl_purgar_cache(cache, GL_TOPE_CACHE_MALLAS);
    return forma;
}

// Las piezas que esperan su forma exacta (ids), con cursor: quitar del principio
// de un vector es O(n) y en la carga se apuntan cientos.
struct GLColaFormas { LocalVector<uint64_t> cola; uint32_t cursor = 0; };
inline GLColaFormas& gl_cola_formas() {
    static GLColaFormas c;
    return c;
}
static const uint64_t GL_US_FORMAS_COLA = 4000;   // 4 ms por frame
// Desde el _process del Workspace, junto a la cola de montaje.
inline void gl_drenar_formas() {
    GLColaFormas& c = gl_cola_formas();
    if (c.cursor >= c.cola.size()) {
        if (!c.cola.is_empty()) { c.cola.clear(); c.cursor = 0; }
        return;
    }
    const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
    while (c.cursor < c.cola.size()) {
        if (Time::get_singleton()->get_ticks_usec() - t0 > GL_US_FORMAS_COLA) break;
        RobloxPart* p = gl_cast<RobloxPart>(ObjectDB::get_instance(ObjectID(c.cola[c.cursor])));
        c.cursor++;
        if (p) p->gl_forma_exacta_ya();
    }
    if (c.cursor >= c.cola.size()) { c.cola.clear(); c.cursor = 0; }
}

inline void RobloxPart::_poner_malla_propia(const Ref<Shape3D>& forma_normal, bool girada) {
    const AABB caja = malla_propia->get_aabb();
    Vector3 esc = malla_escala, off = malla_offset;
    // MeshPart de una EditableMesh (gl_puente_editables.h): el MeshSize es el
    // de cuando se enlazo y la malla NO se recentra (sus coordenadas son las
    // de la pieza, escaladas por Size / MeshSize). Editarla no mueve ni
    // reescala lo que ya se ve.
    static const StringName k_ref("_gl_malla_ref");
    if (malla_propia_ajusta && has_meta(k_ref)) {
        const Vector3 ms = get_meta(k_ref);
        esc = Vector3(ms.x > 1e-4f ? size.x / ms.x : 1.0f, ms.y > 1e-4f ? size.y / ms.y : 1.0f,
                      ms.z > 1e-4f ? size.z / ms.z : 1.0f);
        off = Vector3();
    } else if (malla_propia_ajusta) {
        esc = Vector3(size.x / Math::max(caja.size.x, 0.001f), size.y / Math::max(caja.size.y, 0.001f),
                      size.z / Math::max(caja.size.z, 0.001f));
        off = -(caja.position + caja.size * 0.5f) * esc;
    }
    if (mesh_instance->get_mesh() != malla_propia) mesh_instance->set_mesh(malla_propia);
    mesh_instance->set_rotation(Vector3());
    mesh_instance->set_scale(esc);
    mesh_instance->set_position(off);
    Ref<Shape3D> forma = forma_normal;
    bool girar = girada;
    if (malla_propia_ajusta && !malla_propia_caja) {
        // Anclada no se mueve: su malla exacta. Suelta: el casco convexo (una
        // forma concava no puede ser un cuerpo dinamico).
        //
        // Construirla costaba 3-15 ms por malla y tamaño nuevos (el 94 % de lo
        // que tardaba en montarse el gimnasio de Build a Mountain: 864 ms, y 56
        // con la caja). Si no esta ya hecha, la pieza sale con su caja y la
        // exacta se construye despues en la cola, con presupuesto por frame
        // (gl_drenar_formas), salvo que la pida la propia cola.
        Ref<Shape3D> exacta = forzar_forma_exacta ? gl_forma_de_malla(malla_propia, esc, off, anchored)
                                                  : gl_forma_hecha(malla_propia, esc, off, anchored);
        if (exacta.is_valid()) {
            forma = exacta;
            girar = false;
        } else if (!forma_exacta_pendiente) {
            forma_exacta_pendiente = true;
            gl_cola_formas().cola.push_back((uint64_t)get_instance_id());
        }
    }
    if (forma_id >= 0 || can_collide) _forma_poner(forma, girar);
}

// La malla de un id de asset (rbxassetid://123, la URL de Roblox o una ruta
// res:// del proyecto). Vacia si no esta en el disco. Un .glb/.gltf/.escena se
// abre y se usa su primera malla.
inline HashMap<String, Ref<Mesh>>& gl_mallas_locales() {
    static HashMap<String, Ref<Mesh>> m;
    return m;
}
inline Ref<Mesh> gl_malla_local(const String& id) {
    if (id.is_empty()) return Ref<Mesh>();
    HashMap<String, Ref<Mesh>>& cache = gl_mallas_locales();
    if (Ref<Mesh>* hay = cache.getptr(id)) return *hay;
    ResourceLoader* rl = ResourceLoader::get_singleton();
    String ruta;
    if (id.begins_with("res://")) ruta = (rl && rl->exists(id)) ? id : String();
    else if (gl_is_roblox_asset(id)) ruta = gl_local_asset_path(id);
    Ref<Mesh> malla;
    if (rl && !ruta.is_empty()) {
        Ref<Resource> r = rl->load(ruta);
        malla = r;
        if (malla.is_null()) {
            Ref<PackedScene> escena = r;
            if (escena.is_valid()) {
                Node* raiz = escena->instantiate();
                TypedArray<Node> mallas = raiz ? raiz->find_children("*", "MeshInstance3D", true, false) : TypedArray<Node>();
                if (mallas.size() > 0)
                    if (MeshInstance3D* mi = gl_cast<MeshInstance3D>(mallas[0])) malla = mi->get_mesh();
                if (raiz) memdelete(raiz);
            }
        }
    }
    // Solo lo encontrado: un .gdlu puede escribir la malla DESPUES de que una
    // pieza la pidiera (gl_local_asset_path ya guarda los "no esta" por numero).
    if (malla.is_valid()) cache.insert(id, malla);
    return malla;
}
// La imagen de un id (TextureID de una MeshPart o de una SpecialMesh).
inline Ref<Texture2D> gl_textura_local(const String& id) {
    if (id.is_empty()) return Ref<Texture2D>();
    ResourceLoader* rl = ResourceLoader::get_singleton();
    const String ruta = id.begins_with("res://") ? id : gl_local_asset_path(id);
    if (rl && !ruta.is_empty() && rl->exists(ruta, "Texture2D")) {
        Ref<Texture2D> t = rl->load(ruta, "Texture2D");
        if (t.is_valid()) return t;
    }
    // Recien escrita por el importador de .gdlu: aun sin importar, se lee cruda
    // (una sola vez por archivo: gl_textura_cruda).
    return gl_textura_cruda(gl_is_roblox_asset(id) ? gl_local_asset_file(id) : String());
}

// La de una pieza 3D por streaming (gl_streaming_texturas.h): el PNG suelto, sin
// bloquear y con nivel de detalle por distancia; nula hasta que llega, y
// entonces `usuario` recibe _gl_textura_lista. Sin PNG suelto (solo importado),
// o con el streaming de Godot 4.8 encendido, como gl_textura_local.
inline Ref<Texture2D> gl_textura_local_3d(const String& id, Node3D* usuario, float radio) {
    if (id.is_empty()) return Ref<Texture2D>();
    if (!usuario || !gl_tex_streaming_activo() || !gl_tex_sabe_esperar(usuario)) return gl_textura_local(id);
    if (gl_tex_motor_streaming() && !gl_local_asset_path(id).is_empty()) return gl_textura_local(id);
    const String cruda = gl_is_roblox_asset(id) ? gl_local_asset_file(id) : String();
    if (cruda.is_empty()) return gl_textura_local(id);
    return gl_tex_obtener_3d(cruda, usuario, radio);
}

#endif // GL_MALLA_PROPIA_H

#ifndef GL_CHUNKS_H
#define GL_CHUNKS_H

// ════════════════════════════════════════════════════════════════════
//  Agrupado de geometria estatica por ZONAS (chunks) — rendimiento
//
//  El problema medido: un place importado tiene miles de piezas y CADA una es
//  un objeto que la GPU dibuja por separado. Ocultar toda la geometria daba
//  +22% de FPS, asi que ese dibujado es una de las dos grandes facturas.
//
//  La idea (la misma que usa Roblox): las piezas ANCLADAS no se mueven, asi que
//  se pueden dibujar EN GRUPO. Se agrupan por:
//        zona del mapa  +  misma malla  +  mismo material
//  y cada grupo pasa a ser UN MultiMesh: una sola llamada de dibujado para
//  cientos de piezas, en vez de cientos de llamadas.
//
//  Los nodos Part NO se tocan ni se borran: siguen existiendo con todas sus
//  propiedades para los scripts. Solo se OCULTA su MeshInstance (lo dibuja ya
//  el grupo). Por eso es reversible: gl_clear_static_chunks() lo deshace.
//
//  Solo entran piezas ancladas y visibles. Una pieza que se mueva, cambie de
//  color o se rompa deja su grupo desactualizado, por eso el sistema se puede
//  reconstruir en cualquier momento (gl_build_static_chunks) y se ofrece como
//  ajuste que el juego puede apagar.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/shape3d.hpp>
#include "gl_graphics.h"
#include "roblox_part.h"   // la malla y la forma ya viven DENTRO de la pieza
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// Nombre del nodo que cuelga del Workspace y contiene todos los grupos, para
// poder encontrarlos y borrarlos sin tocar nada mas.
#define GL_CHUNKS_ROOT "__GLStaticChunks"

// Mallas UNITARIAS (una por forma). La clave del agrupado deja de ser "la misma
// malla" y pasa a ser "la misma FORMA": el tamaño de cada pieza viaja en la
// escala de su instancia. Asi entran juntas piezas que miden distinto, que es
// lo que dejaba fuera a la mayoria de la geometria en la fase 1.
inline Ref<Mesh>& gl_unit_box() {
    static Ref<Mesh> m;
    if (!m.is_valid()) { Ref<BoxMesh> b; b.instantiate(); b->set_size(Vector3(1,1,1)); m = b; }
    return m;
}
inline Ref<Mesh>& gl_unit_sphere() {
    static Ref<Mesh> m;
    if (!m.is_valid()) { Ref<SphereMesh> sp; sp.instantiate(); sp->set_radius(0.5f); sp->set_height(1.0f); m = sp; }
    return m;
}
inline Ref<Mesh>& gl_unit_cylinder() {
    static Ref<Mesh> m;
    if (!m.is_valid()) { Ref<CylinderMesh> c; c.instantiate(); c->set_top_radius(0.5f); c->set_bottom_radius(0.5f); c->set_height(1.0f); m = c; }
    return m;
}
// Se sueltan al descargar la extension, MIENTRAS el RenderingServer sigue vivo.
// Al vivir en un static local, si no se vacian se destruyen cuando el servidor
// ya no esta: "Parameter RenderingServer::get_singleton() is null" desde
// ~PrimitiveMesh y sus RID contados como fugados al salir.
inline void gl_clear_unit_meshes() {
    gl_unit_box().unref();
    gl_unit_sphere().unref();
    gl_unit_cylinder().unref();
}

inline bool gl_unit_form(const Ref<Mesh>& src, Ref<Mesh>& out_unit, Vector3& out_scale, int& out_kind) {
    if (BoxMesh* b = gl_cast<BoxMesh>(src.ptr())) {
        out_unit = gl_unit_box(); out_scale = b->get_size(); out_kind = 0; return true;
    }
    if (SphereMesh* sp = gl_cast<SphereMesh>(src.ptr())) {
        const float d = sp->get_radius() * 2.0f;
        out_unit = gl_unit_sphere(); out_scale = Vector3(d, sp->get_height(), d); out_kind = 1; return true;
    }
    if (CylinderMesh* c = gl_cast<CylinderMesh>(src.ptr())) {
        const float d = c->get_top_radius() * 2.0f;
        out_unit = gl_unit_cylinder(); out_scale = Vector3(d, c->get_height(), d); out_kind = 2; return true;
    }
    return false;
}

// ── LA CLAVE DE UN GRUPO, EN UN SOLO SITIO ───────────────────────────────
//
// Antes esto se calculaba DOS veces (al recolectar y al ocultar) con el codigo
// copiado. Cualquier cambio en una copia y no en la otra deja piezas agrupadas
// que siguen dibujandose ellas mismas (doble dibujado) o al reves. Ahora las
// dos pasadas llaman aqui.
//
// Y admite mallas que NO son primitivas. MEDIDO en el place de prueba: de las
// 3.441 mallas visibles sin agrupar, 2.717 colgaban de un RBXMeshPart — mallas
// de Roblox, ANCLADAS y repetidas (el mismo modelo muchas veces). El agrupado
// solo aceptaba caja/esfera/cilindro, asi que se dibujaban una a una: 2.161
// llamadas de dibujo para 48.240 triangulos, o sea 22 triangulos por llamada.
// La GPU parada y la CPU entera pidiendo dibujos.
//
// Para una malla no primitiva el grupo es (zona + ESE recurso de malla +
// material) y la escala viaja en la transformacion de la instancia, que es
// justo para lo que sirve un MultiMesh.
inline bool gl_chunk_clave(RobloxPart* pieza, float chunk_size,
                           Ref<Mesh>& out_mesh, Vector3& out_scale,
                           Ref<Material>& out_mat, String& out_key) {
    if (!pieza) return false;
    Node3D* n3 = gl_cast<Node3D>(pieza);
    if (!n3 || !pieza->gl_se_dibuja()) return false;

    Ref<Mesh> mesh = pieza->gl_malla();
    if (mesh.is_null()) return false;
    out_mat = pieza->gl_material_render();

    int kind = 0;
    if (!gl_unit_form(mesh, out_mesh, out_scale, kind)) {
        // Malla de verdad (RBXMeshPart y compañia). Solo se agrupan las de UNA
        // superficie: con varias, el material_override del MultiMesh las
        // pintaria todas igual y se perderia el aspecto de la pieza.
        if (mesh->get_surface_count() != 1) return false;
        out_mesh  = mesh;
        out_scale = Vector3(1, 1, 1);
    }

    const Vector3 p = n3->is_inside_tree() ? n3->get_global_position() : n3->get_position();
    // La malla entra en la clave por su PUNTERO: para las primitivas es la
    // unitaria compartida (una por forma), y para las de Roblox es el recurso
    // concreto. Un solo campo cubre los dos casos.
    out_key = String::num_int64((int64_t)Math::floor(p.x / chunk_size)) + "_" +
              String::num_int64((int64_t)Math::floor(p.y / chunk_size)) + "_" +
              String::num_int64((int64_t)Math::floor(p.z / chunk_size)) + "|" +
              String::num_int64((int64_t)out_mesh.ptr()) + "|" +
              String::num_int64((int64_t)out_mat.ptr()) + "|" +
              String::num_int64((int64_t)pieza->gl_capa_render());
    return true;
}

struct GLChunkGroup {
    Ref<Mesh>          mesh;
    Ref<Material>      material;
    uint32_t           capa = 1;   // la de sus piezas (sombras propias, gl_capas.h)
    LocalVector<Transform3D> xforms;
};

// Recolecta las piezas ancladas y las reparte en grupos (zona+malla+material).
inline void _gl_collect_static(Node* n, float chunk_size,
                               HashMap<String, GLChunkGroup>& groups,
                               LocalVector<RobloxPart*>& hidden) {
    if (!n) return;

    // Solo piezas ancladas: las dinamicas se mueven y no se pueden agrupar.
    if (gl_cast<RigidBody3D>(n)) {
        Variant anc = n->get("Anchored");
        Variant tr  = n->get("Transparency");
        const bool anchored = (anc.get_type() == Variant::BOOL) && (bool)anc;
        const bool solid    = (tr.get_type() != Variant::FLOAT) || ((float)tr < 0.999f);
        Node3D* n3 = gl_cast<Node3D>(n);
        // La malla ya no es un nodo hijo: se le pregunta a la pieza (ver
        // gl_malla/gl_xf_malla en roblox_part.h).
        RobloxPart* pieza = gl_cast<RobloxPart>(n);
        if (anchored && solid && n3 && pieza) {
            Ref<Mesh> unit; Vector3 mscale; Ref<Material> mat; String key;
            if (gl_chunk_clave(pieza, chunk_size, unit, mscale, mat, key)) {
                GLChunkGroup* g = groups.getptr(key);
                if (!g) {
                    GLChunkGroup fresh;
                    fresh.mesh = unit;
                    fresh.material = mat;
                    fresh.capa = pieza->gl_capa_render();
                    groups.insert(key, fresh);
                    g = groups.getptr(key);
                }
                Transform3D t = pieza->gl_xf_malla();
                // scaled_local: la escala va en el espacio LOCAL de la pieza. Con
                // scaled() se aplicaria sobre los ejes del mundo y las piezas
                // rotadas saldrian deformadas. Para una malla de Roblox la escala
                // es (1,1,1) y esto no toca nada: su tamaño ya viene en gl_xf_malla.
                t.basis = t.basis.scaled_local(mscale);
                g->xforms.push_back(t);
                hidden.push_back(pieza);
            }
        }
    }
    for (int i = 0; i < n->get_child_count(); i++)
        _gl_collect_static(n->get_child(i), chunk_size, groups, hidden);
}

// Deshace el agrupado: borra los grupos y vuelve a mostrar las mallas propias.
inline void gl_clear_static_chunks(Node* workspace) {
    if (!workspace) return;
    Node* root = workspace->get_node_or_null(NodePath(GL_CHUNKS_ROOT));
    if (!root) return;
    // Volver a dibujar las piezas que este sistema apago.
    Array marked = root->get_meta("hidden", Array());
    for (int i = 0; i < marked.size(); i++) {
        Object* o = ObjectDB::get_instance((ObjectID)(uint64_t)(int64_t)marked[i]);
        if (RobloxPart* pz = gl_cast<RobloxPart>(o)) pz->gl_ocultar_por_chunk(false);
    }
    // Reactivar las formas que se apagaron al agruparlas. Ahora se apunta el
    // CUERPO, no un nodo collider: la forma vive dentro de la propia pieza.
    Array cols = root->get_meta("disabled_cols", Array());
    for (int i = 0; i < cols.size(); i++) {
        Object* o = ObjectDB::get_instance((ObjectID)(uint64_t)(int64_t)cols[i]);
        CollisionObject3D* co = gl_cast<CollisionObject3D>(o);
        if (!co) continue;
        PackedInt32Array owners = co->get_shape_owners();
        for (int k = 0; k < owners.size(); k++)
            co->shape_owner_set_disabled((uint32_t)owners[k], false);
    }
    workspace->remove_child(root);
    root->queue_free();
}

// ── FISICA por zonas ─────────────────────────────────────────────────
// Medido: el tick de fisica se lleva ~29% del tiempo, y el motivo es que hay
// MILES de cuerpos registrados (una pieza = un cuerpo). Aqui se juntan las formas
// de las piezas ancladas de una zona en UN SOLO cuerpo estatico y se apagan los
// colliders individuales: el broadphase pasa de miles de cuerpos a unas decenas.
//
// Se excluyen a proposito:
//   · piezas NO ancladas (se mueven: necesitan su propio cuerpo)
//   · piezas con Touched conectado (contact monitor activo): su collider tiene
//     que seguir siendo suyo o el evento dejaria de dispararse
//   · piezas sin colision (su CollisionShape ya viene desactivado)
// Una forma a agrupar: de que cuerpo sale, cual es y donde esta en el mundo.
// Ya NO se busca un CollisionShape3D hijo: las piezas llevan su forma dentro
// (ver "shape owners" en roblox_part.h), asi que se pregunta al cuerpo.
struct GLFormaZona {
    CollisionObject3D* cuerpo;
    uint32_t           owner;
    Ref<Shape3D>       forma;
    Transform3D        mundo;
};

inline void _gl_collect_colliders(Node* n, float chunk_size,
                                  HashMap<String, LocalVector<GLFormaZona>>& zones) {
    if (!n) return;
    if (RigidBody3D* rb = gl_cast<RigidBody3D>(n)) {
        Variant anc = n->get("Anchored");
        const bool anchored = (anc.get_type() == Variant::BOOL) && (bool)anc;
        const bool touch = rb->is_contact_monitor_enabled();
        // Solo lo que choca y esta en la capa 1 (grupo Default): el cuerpo de
        // zona choca como Default, asi que una pieza que no choca (capa de
        // consultas) o de otro grupo se volveria solida o cambiaria de grupo.
        const bool capa_default = rb->get_collision_layer() == 1u;
        if (anchored && !touch && capa_default && rb->is_inside_tree()) {
            PackedInt32Array owners = rb->get_shape_owners();
            for (int i = 0; i < owners.size(); i++) {
                const uint32_t ow = (uint32_t)owners[i];
                if (rb->is_shape_owner_disabled(ow)) continue;
                if (rb->shape_owner_get_shape_count(ow) <= 0) continue;
                Ref<Shape3D> f = rb->shape_owner_get_shape(ow, 0);
                if (!f.is_valid()) continue;
                const Vector3 p = rb->get_global_position();
                const String key =
                    String::num_int64((int64_t)Math::floor(p.x / chunk_size)) + "_" +
                    String::num_int64((int64_t)Math::floor(p.y / chunk_size)) + "_" +
                    String::num_int64((int64_t)Math::floor(p.z / chunk_size));
                LocalVector<GLFormaZona>* z = zones.getptr(key);
                if (!z) { zones.insert(key, LocalVector<GLFormaZona>()); z = zones.getptr(key); }
                GLFormaZona fz;
                fz.cuerpo = rb;
                fz.owner  = ow;
                fz.forma  = f;
                fz.mundo  = rb->get_global_transform() * rb->shape_owner_get_transform(ow);
                z->push_back(fz);
                break;   // una forma por pieza
            }
        }
    }
    for (int i = 0; i < n->get_child_count(); i++)
        _gl_collect_colliders(n->get_child(i), chunk_size, zones);
}

// Crea un cuerpo estatico por zona. Devuelve cuantas formas se agruparon.
inline int _gl_build_zone_bodies(Node* workspace, Node* root, float chunk_size,
                                 int min_group, Array& disabled_out) {
    HashMap<String, LocalVector<GLFormaZona>> zones;
    _gl_collect_colliders(workspace, chunk_size, zones);

    int merged = 0;
    for (HashMap<String, LocalVector<GLFormaZona>>::Iterator it = zones.begin(); it; ++it) {
        LocalVector<GLFormaZona>& list = it->value;
        if ((int)list.size() < min_group) continue;

        StaticBody3D* body = memnew(StaticBody3D);
        root->add_child(body);
        for (uint32_t i = 0; i < list.size(); i++) {
            GLFormaZona& f = list[i];
            if (!f.cuerpo || !f.forma.is_valid()) continue;
            // El cuerpo de zona tambien lleva sus formas sin nodo hijo.
            const uint32_t ow = body->create_shape_owner(body);
            body->shape_owner_add_shape(ow, f.forma);   // la forma viene del cache compartido
            body->shape_owner_set_transform(ow, body->get_global_transform().affine_inverse() * f.mundo);
            f.cuerpo->shape_owner_set_disabled(f.owner, true);   // su pieza deja de colisionar
            disabled_out.push_back((int64_t)(uint64_t)f.cuerpo->get_instance_id());
            merged++;
        }
    }
    return merged;
}

// Construye los grupos. Devuelve cuantas piezas se agruparon.
inline int gl_build_static_chunks(Node* workspace, float chunk_size = 128.0f, int min_group = 4) {
    if (!workspace) return 0;
    gl_clear_static_chunks(workspace);   // idempotente: primero se deshace lo anterior

    HashMap<String, GLChunkGroup> groups;
    LocalVector<RobloxPart*> candidates;
    _gl_collect_static(workspace, chunk_size, groups, candidates);

    Node3D* root = memnew(Node3D);
    root->set_name(GL_CHUNKS_ROOT);
    workspace->add_child(root);

    Array hidden_ids;
    int batched = 0;
    for (HashMap<String, GLChunkGroup>::Iterator it = groups.begin(); it; ++it) {
        GLChunkGroup& g = it->value;
        // Grupos muy pequeños no compensan: un MultiMesh de 2 piezas no ahorra
        // nada y añade un nodo mas.
        if ((int)g.xforms.size() < min_group) continue;

        Ref<MultiMesh> mm;
        mm.instantiate();
        mm->set_transform_format(MultiMesh::TRANSFORM_3D);
        mm->set_mesh(g.mesh);
        mm->set_instance_count((int)g.xforms.size());
        for (uint32_t i = 0; i < g.xforms.size(); i++)
            mm->set_instance_transform((int)i, g.xforms[i]);

        MultiMeshInstance3D* mmi = memnew(MultiMeshInstance3D);
        mmi->set_multimesh(mm);
        mmi->set_layer_mask(g.capa);
        if (g.material.is_valid()) mmi->set_material_override(g.material);
        root->add_child(mmi);
        batched += (int)g.xforms.size();
    }

    // Ocultar SOLO las mallas de los grupos que de verdad se crearon. Se hace en
    // una segunda pasada: si un grupo se descarto por pequeño, su pieza debe
    // seguir dibujandose ella misma.
    if (batched > 0) {
        for (uint32_t i = 0; i < candidates.size(); i++) {
            RobloxPart* pz = candidates[i];
            if (!pz) continue;
            Node3D* p3 = gl_cast<Node3D>(pz);
            if (!p3) continue;
            Ref<Mesh> unit; Vector3 mscale; Ref<Material> mat; String key;
            if (!gl_chunk_clave(pz, chunk_size, unit, mscale, mat, key)) continue;
            GLChunkGroup* g = groups.getptr(key);
            if (g && (int)g->xforms.size() >= min_group) {
                pz->gl_ocultar_por_chunk(true);
                hidden_ids.push_back((int64_t)(uint64_t)pz->get_instance_id());
            }
        }
    }
    root->set_meta("hidden", hidden_ids);

    // ── Fisica: un cuerpo estatico por zona ──────────────────────────────
    // APAGADA por defecto: MEDIDO que empeora (22.3 -> 17.8 fps). Un cuerpo con
    // cientos de formas dispersas tiene una caja envolvente enorme, asi que el
    // motor lo considera candidato de colision casi siempre, y encima se crean
    // miles de nodos de forma extra. Se deja disponible por si en un mapa muy
    // compacto (formas juntas, cajas pequeñas) si compensa.
    Array disabled_cols;
    const int merged = gl_custom().merge_zone_physics
        ? _gl_build_zone_bodies(workspace, root, chunk_size, min_group, disabled_cols) : 0;
    root->set_meta("disabled_cols", disabled_cols);
    if (merged > 0)
        UtilityFunctions::print("[GodotLuau] Chunks: ", batched, " piezas dibujadas en grupo, ",
                                merged, " colisiones unidas por zona.");
    return batched;
}

#endif // GL_CHUNKS_H

#ifndef RBX_UNIONES_H
#define RBX_UNIONES_H

// ════════════════════════════════════════════════════════════════════
//  UnionOperation: su malla sale del CSG de sus piezas.
//
//  Un .rbxl no trae la malla de una union en claro (MeshData2 va ofuscado y
//  lo normal es que viva en la nube), pero SI trae las piezas con las que se
//  hizo: ChildData2 es un modelo binario de Roblox (empieza por "<roblox!")
//  con cada Part, cuña, NegateOperation y union anidada, en coordenadas de la
//  propia union. Aqui se rehace con el CSG de Godot, se hornea a malla y se
//  guarda una sola vez por contenido (las copias de una union comparten
//  ChildData2: en Build A Mountain, 1.035 uniones son 5 mallas).
//
//  Con UsePartColor = false (lo normal) cada pieza conserva su color: va como
//  color de vertice. Con true, toda la union es de su Color.
// ════════════════════════════════════════════════════════════════════

#include "rbx_reader.h"
#include "rbx_prop.h"
#include <godot_cpp/classes/csg_combiner3d.hpp>
#include <godot_cpp/classes/csg_box3d.hpp>
#include <godot_cpp/classes/csg_sphere3d.hpp>
#include <godot_cpp/classes/csg_cylinder3d.hpp>
#include <godot_cpp/classes/csg_mesh3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/hashing_context.hpp>

namespace rbx {

struct PiezaCSG {
    String clase;
    Transform3D cf;
    Vector3 tam = Vector3(1, 1, 1);
    Vector3 tam_inicial;          // InitialSize de una operacion anidada
    bool tiene_tam_inicial = false;
    int forma = 1;                // Enum.PartType: Ball 0, Block 1, Cylinder 2
    Color color = Color(0.639f, 0.635f, 0.647f);
    PackedByteArray hijos;        // ChildData2/ChildData de una operacion anidada
    int32_t ref = -1;
    int32_t padre = -1;
};

inline bool es_operacion_csg(const String& c) {
    return c == "UnionOperation" || c == "NegateOperation" || c == "IntersectOperation";
}

// Las piezas de nivel superior de un ChildData (un .rbxm en memoria).
inline bool leer_piezas_csg(const PackedByteArray& datos, LocalVector<PiezaCSG>& out) {
    out.clear();
    RBXFile file;
    String err;
    if (datos.size() < 32 || !parse_container(datos, file, err)) return false;
    HashMap<uint32_t, String> clases;
    HashMap<uint32_t, Vector<int32_t>> refs_de;
    HashMap<int32_t, int> indice;     // referent -> posicion en `todas`
    LocalVector<PiezaCSG> todas;
    for (int k = 0; k < file.chunks.size(); k++) {
        if (file.chunks[k].name != "INST") continue;
        Reader r(file.chunks[k].data.ptr(), file.chunks[k].data.size());
        const uint32_t idx = r.u32le();
        const String nombre = r.str();
        r.u8();
        const uint32_t n = r.u32le();
        if (!r.ok() || n > 100000) continue;
        LocalVector<int32_t> refs; refs.resize(n);
        r.read_ref_array(n, refs.ptr());
        if (!r.ok()) continue;
        clases[idx] = nombre;
        Vector<int32_t> v;
        for (uint32_t i = 0; i < n; i++) {
            v.push_back(refs[i]);
            PiezaCSG p; p.clase = nombre; p.ref = refs[i];
            indice[refs[i]] = (int)todas.size();
            todas.push_back(p);
        }
        refs_de[idx] = v;
    }
    for (int k = 0; k < file.chunks.size(); k++) {
        if (file.chunks[k].name != "PROP") continue;
        Reader r(file.chunks[k].data.ptr(), file.chunks[k].data.size());
        const uint32_t idx = r.u32le();
        const String prop = r.str();
        const uint8_t tipo = r.u8();
        if (!r.ok()) continue;
        const Vector<int32_t>* refs = refs_de.getptr(idx);
        if (!refs || refs->is_empty()) continue;
        const uint32_t n = (uint32_t)refs->size();
        if (prop == "ChildData2" && tipo == PT_SHAREDSTRING) {
            LocalVector<uint32_t> ids;
            if (!decode_sharedstring_indices(r, n, ids)) continue;
            for (uint32_t i = 0; i < n; i++)
                if ((int)ids[i] < file.shared_raw.size()) todas[indice[(*refs)[i]]].hijos = file.shared_raw[ids[i]];
            continue;
        }
        if (prop == "ChildData" && tipo == PT_STRING) {
            for (uint32_t i = 0; i < n; i++) {
                PackedByteArray b = r.blob();
                if (!r.ok()) break;
                PiezaCSG& p = todas[indice[(*refs)[i]]];
                if (p.hijos.is_empty()) p.hijos = b;
            }
            continue;
        }
        if (prop != "CFrame" && prop != "size" && prop != "InitialSize" && prop != "shape" &&
            prop != "Color3uint8") continue;
        Vector<Variant> vals;
        if (!decode_column(r, tipo, n, file.shared_strings, vals) || vals.size() != (int)n) continue;
        for (uint32_t i = 0; i < n; i++) {
            PiezaCSG& p = todas[indice[(*refs)[i]]];
            const Variant& v = vals[i];
            if (prop == "CFrame" && v.get_type() == Variant::TRANSFORM3D) p.cf = v;
            else if (prop == "size" && v.get_type() == Variant::VECTOR3) p.tam = v;
            else if (prop == "InitialSize" && v.get_type() == Variant::VECTOR3) { p.tam_inicial = v; p.tiene_tam_inicial = true; }
            else if (prop == "shape") p.forma = (int)(int64_t)v;
            else if (prop == "Color3uint8" && v.get_type() == Variant::COLOR) p.color = v;
        }
    }
    for (int k = 0; k < file.chunks.size(); k++) {
        if (file.chunks[k].name != "PRNT") continue;
        Reader r(file.chunks[k].data.ptr(), file.chunks[k].data.size());
        r.u8();
        const uint32_t n = r.u32le();
        if (!r.ok() || n > 100000) break;
        LocalVector<int32_t> hijo, padre; hijo.resize(n); padre.resize(n);
        r.read_ref_array(n, hijo.ptr());
        r.read_ref_array(n, padre.ptr());
        if (!r.ok()) break;
        for (uint32_t i = 0; i < n; i++)
            if (int* pos = indice.getptr(hijo[i])) todas[*pos].padre = padre[i];
    }
    for (uint32_t i = 0; i < todas.size(); i++)
        if (todas[i].padre < 0 || !indice.has(todas[i].padre)) out.push_back(todas[i]);
    return !out.is_empty();
}

inline void anadir_piezas_csg(CSGCombiner3D* padre, const LocalVector<PiezaCSG>& piezas, int prof,
                              bool colores, bool interseccion);

inline CSGShape3D* nodo_csg(const PiezaCSG& p, int prof, bool colores) {
    CSGShape3D* s = nullptr;
    Transform3D t = p.cf;
    Ref<StandardMaterial3D> mat;
    if (colores) { mat.instantiate(); mat->set_albedo(gl_color_pieza(p.color)); }
    if (es_operacion_csg(p.clase)) {
        LocalVector<PiezaCSG> sub;
        if (prof < 8 && leer_piezas_csg(p.hijos, sub)) {
            CSGCombiner3D* comb = memnew(CSGCombiner3D);
            anadir_piezas_csg(comb, sub, prof + 1, colores, p.clase == "IntersectOperation");
            s = comb;
            // Su Size de ahora frente al de cuando se hizo (como la de arriba).
            if (p.tiene_tam_inicial && p.tam_inicial.x > 0.0f && p.tam_inicial.y > 0.0f && p.tam_inicial.z > 0.0f)
                t.basis = t.basis * Basis::from_scale(p.tam / p.tam_inicial);
        } else {
            CSGBox3D* b = memnew(CSGBox3D);
            b->set_size(p.tam);
            if (mat.is_valid()) b->set_material(mat);
            s = b;
        }
    } else if (p.clase == "WedgePart" || p.clase == "CornerWedgePart") {
        Ref<Mesh> malla;
        Ref<Shape3D> forma;
        if (p.clase == "WedgePart") gl_geom_cuna(p.tam, malla, forma);
        else gl_geom_cuna_esquina(p.tam, malla, forma);
        CSGMesh3D* m = memnew(CSGMesh3D);
        m->set_mesh(malla);
        if (mat.is_valid()) m->set_material(mat);
        s = m;
    } else if (p.forma == 0) {   // Ball: el lado mas corto
        CSGSphere3D* e = memnew(CSGSphere3D);
        e->set_radius(Math::min(p.tam.x, Math::min(p.tam.y, p.tam.z)) * 0.5f);
        e->set_radial_segments(24);
        e->set_rings(12);
        if (mat.is_valid()) e->set_material(mat);
        s = e;
    } else if (p.forma == 2) {   // Cylinder: en Roblox va por el eje X, en Godot por el Y
        CSGCylinder3D* c = memnew(CSGCylinder3D);
        c->set_radius(Math::min(p.tam.y, p.tam.z) * 0.5f);
        c->set_height(p.tam.x);
        c->set_sides(24);
        if (mat.is_valid()) c->set_material(mat);
        t.basis = t.basis * Basis(Vector3(0, 0, 1), (real_t)Math_PI * 0.5f);
        s = c;
    } else {
        CSGBox3D* b = memnew(CSGBox3D);
        b->set_size(p.tam);
        if (mat.is_valid()) b->set_material(mat);
        s = b;
    }
    s->set_transform(t);
    return s;
}

// Roblox: la suma de todo lo positivo menos lo negativo (no importa el orden).
// El CSG de Godot va en orden, asi que primero las que suman y luego las que
// restan. En una IntersectOperation, la primera y luego interseccion.
inline void anadir_piezas_csg(CSGCombiner3D* padre, const LocalVector<PiezaCSG>& piezas, int prof,
                              bool colores, bool interseccion) {
    bool primera = true;
    for (int pasada = 0; pasada < 2; pasada++) {
        for (uint32_t i = 0; i < piezas.size(); i++) {
            const bool resta = piezas[i].clase == "NegateOperation";
            if (resta != (pasada == 1)) continue;
            CSGShape3D* s = nodo_csg(piezas[i], prof, colores);
            if (!s) continue;
            if (resta) s->set_operation(CSGShape3D::OPERATION_SUBTRACTION);
            else if (interseccion && !primera) s->set_operation(CSGShape3D::OPERATION_INTERSECTION);
            else s->set_operation(CSGShape3D::OPERATION_UNION);
            primera = false;
            padre->add_child(s);
        }
    }
}

// Junta las superficies horneadas (una por material) en una sola; con colores,
// el de cada material pasa a color de vertice.
inline Ref<ArrayMesh> malla_unica(const Ref<ArrayMesh>& horneada, bool colores) {
    PackedVector3Array vs, ns;
    PackedVector2Array uvs;
    PackedColorArray cs;
    for (int s = 0; s < horneada->get_surface_count(); s++) {
        const Array a = horneada->surface_get_arrays(s);
        const PackedVector3Array v = a[Mesh::ARRAY_VERTEX];
        const PackedVector3Array n = a[Mesh::ARRAY_NORMAL];
        const PackedVector2Array uv = a[Mesh::ARRAY_TEX_UV];
        const PackedInt32Array idx = a[Mesh::ARRAY_INDEX];
        Color c(1, 1, 1);
        Ref<StandardMaterial3D> m = horneada->surface_get_material(s);
        if (m.is_valid()) c = m->get_albedo();
        const int total = idx.size() > 0 ? idx.size() : v.size();
        for (int i = 0; i < total; i++) {
            const int j = idx.size() > 0 ? idx[i] : i;
            if (j < 0 || j >= v.size()) continue;
            vs.push_back(v[j]);
            ns.push_back(j < n.size() ? n[j] : Vector3(0, 1, 0));
            uvs.push_back(j < uv.size() ? uv[j] : Vector2());
            if (colores) cs.push_back(c);
        }
    }
    if (vs.is_empty()) return Ref<ArrayMesh>();
    Array arr;
    arr.resize(Mesh::ARRAY_MAX);
    arr[Mesh::ARRAY_VERTEX] = vs;
    arr[Mesh::ARRAY_NORMAL] = ns;
    arr[Mesh::ARRAY_TEX_UV] = uvs;
    if (colores) arr[Mesh::ARRAY_COLOR] = cs;
    Ref<ArrayMesh> out;
    out.instantiate();
    out->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
    // Las caras para la colision, en la propia malla (ver gl_puntos_malla): sin
    // indices, los vertices ya son la lista de triangulos.
    out->set_meta("_gl_caras", vs);
    return out;
}

// La malla de una union a partir de su ChildData. Vacia si no se pudo.
inline Ref<ArrayMesh> malla_de_union(const PackedByteArray& datos, bool colores) {
    LocalVector<PiezaCSG> piezas;
    if (!leer_piezas_csg(datos, piezas)) return Ref<ArrayMesh>();
    CSGCombiner3D* raiz = memnew(CSGCombiner3D);
    anadir_piezas_csg(raiz, piezas, 0, colores, false);
    // Sin esperar a un frame ni meterlo en el arbol: _update_shape lo calcula ya.
    raiz->call("_update_shape");
    // bake_static_mesh llego en Godot 4.4: godot-cpp (API de 4.3) no lo trae.
    Ref<ArrayMesh> horneada = raiz->call("bake_static_mesh");
    memdelete(raiz);
    if (horneada.is_null() || horneada->get_surface_count() == 0) return Ref<ArrayMesh>();
    return malla_unica(horneada, colores);
}

// Huella del contenido (para compartir la malla entre copias de una union).
inline String huella_csg(const PackedByteArray& datos, bool colores) {
    Ref<HashingContext> h;
    h.instantiate();
    h->start(HashingContext::HASH_MD5);
    h->update(datos);
    const PackedByteArray d = h->finish();
    return d.hex_encode() + (colores ? "c" : "u");
}

} // namespace rbx

#endif // RBX_UNIONES_H

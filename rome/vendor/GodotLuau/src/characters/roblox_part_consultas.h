#ifndef ROBLOX_PART_CONSULTAS_H
#define ROBLOX_PART_CONSULTAS_H

// Lo que se pregunta SOBRE una pieza desde fuera: si es un asiento y quien lo
// ocupa, la raiz de su conjunto y los rayos con la semantica de Roblox. Salio
// de roblox_part.h (que la incluye al final) para que no pase de 2.000 lineas.

#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include "gl_assembly.h"
#include "roblox_part.h"   // la clase entera: este archivo se incluye al final de ella

using namespace godot;

// ── Asientos (Seat / VehicleSeat) ─────────────────────────────────────────────
// Son piezas normales con la clase de Roblox en la meta __rbx_class (la ponen
// el importador e Instance.new). Quien esta sentado y el mando del vehiculo van
// en metas del propio asiento; el Humanoid hace el resto.
inline bool gl_es_asiento(const Node* n) {
    static const StringName k("__rbx_class");
    if (!n || !n->has_meta(k)) return false;
    const String c = n->get_meta(k);
    return c == "Seat" || c == "VehicleSeat";
}
inline bool gl_es_asiento_vehiculo(const Node* n) {
    static const StringName k("__rbx_class");
    return n && n->has_meta(k) && String(n->get_meta(k)) == "VehicleSeat";
}
inline bool gl_asiento_deshabilitado(const Node* n) {
    static const StringName k("Disabled");
    return n && n->has_meta(k) && (bool)n->get_meta(k);
}
// El Humanoid sentado (Seat.Occupant), o nulo.
inline Node* gl_ocupante_de(const Node* asiento) {
    static const StringName k("_gl_ocupante");
    if (!asiento || !asiento->has_meta(k)) return nullptr;
    return gl_cast<Node>(ObjectDB::get_instance(ObjectID((uint64_t)(int64_t)asiento->get_meta(k))));
}

// La pieza RAIZ del conjunto al que pertenece `p`. Si no esta en ninguno, es
// ella misma: en Roblox una pieza suelta tambien es un conjunto, de un miembro.
inline RobloxPart* gl_raiz_de(RobloxPart* p) {
    if (!p) return nullptr;
    const uint64_t rid = gl_raiz_ensamblaje((uint64_t)p->get_instance_id());
    if (rid == (uint64_t)p->get_instance_id()) return p;
    RobloxPart* r = gl_cast<RobloxPart>(ObjectDB::get_instance(ObjectID(rid)));
    return r ? r : p;
}

// ── RAYCAST CON LA SEMANTICA DE ROBLOX ───────────────────────────────────────
// En Roblox, una pieza con `CanQuery = false` es INVISIBLE para los rayos: el
// rayo la atraviesa y sigue hasta lo que haya detras. Aqui `CanQuery` se
// guardaba y se podia leer, pero NINGUN rayo lo miraba, asi que una pieza
// marcada como no consultable seguia parando los rayos igual.
//
// No se puede resolver con la mascara de colision de Godot: la mascara no
// distingue "rayo" de "colision", y quitar el bit dejaria a la pieza sin chocar
// tampoco. Asi que se hace donde toca: si el rayo pega en algo no consultable,
// se excluye ESE cuerpo y se repite. Como CanQuery=false es raro, casi siempre
// acierta a la primera; el tope de saltos evita cualquier bucle.
// ¿Esta `n` dentro del filtro? En Roblox FilterDescendantsInstances filtra la
// instancia Y todos sus descendientes, de ahi el nombre.
inline bool gl_en_filtro(Node* n, const Vector<Node*>& filtro) {
    if (!n) return false;
    for (int i = 0; i < filtro.size(); i++) {
        Node* f = filtro[i];
        if (f && (f == n || f->is_ancestor_of(n))) return true;
    }
    return false;
}

// ── FilterDescendantsInstances / FilterType ──────────────────────────────────
// Se resuelve con este mismo bucle de saltos en vez de recolectar los RID de
// todos los descendientes: un filtro puede ser una carpeta con miles de piezas
// y recorrerla ENTERA en cada rayo saldria carisimo. Comprobar el nodo que se
// ha golpeado contra los pocos nodos raiz del filtro es O(profundidad).
//
// El tope de saltos sube cuando hay filtro porque el patron tipico de Roblox
// (ir metiendo en el filtro lo que se va golpeando hasta dar con el suelo)
// puede atravesar bastantes piezas seguidas.
inline Dictionary gl_raycast_roblox(PhysicsDirectSpaceState3D* space,
                                    const Ref<PhysicsRayQueryParameters3D>& params,
                                    int max_saltos = 16,
                                    const Vector<Node*>* filtro = nullptr,
                                    bool incluir = false,
                                    bool respetar_colision = false) {
    if (!space || params.is_null()) return Dictionary();
    const bool hay_filtro = filtro && !filtro->is_empty();
    if (hay_filtro && max_saltos < 64) max_saltos = 64;
    TypedArray<RID> excluir = params->get_exclude();
    for (int i = 0; i < max_saltos; i++) {
        Dictionary res = space->intersect_ray(params);
        if (res.is_empty()) return res;
        const Variant cv = res.get("collider", Variant());
        Node* golpeado = (cv.get_type() == Variant::OBJECT)
                       ? gl_cast<Node>(cv.operator Object*()) : nullptr;
        bool saltar = false;
        RobloxPart* p = gl_cast<RobloxPart>(golpeado);
        // En un conjunto soldado el cuerpo es la raiz: CanQuery, CanCollide y el
        // filtro se miran en el miembro que se toco de verdad.
        if (p) {
            if (RobloxPart* m = gl_cast<RobloxPart>(p->gl_pieza_de_forma((int)res.get("shape", -1)))) {
                p = m;
                golpeado = m;
            }
        }
        if (p && !p->get_can_query()) saltar = true;   // CanQuery = false
        if (p && respetar_colision && !p->get_can_collide()) saltar = true;   // RespectCanCollide
        if (!saltar && hay_filtro) {
            const bool dentro = gl_en_filtro(golpeado, *filtro);
            saltar = incluir ? !dentro : dentro;
        }
        if (!saltar) return res;
        const Variant vrid = res.get("rid", Variant());
        if (vrid.get_type() != Variant::RID) return res;   // sin RID no se puede excluir
        excluir.push_back(vrid);
        params->set_exclude(excluir);
    }
    return Dictionary();
}

#endif // ROBLOX_PART_CONSULTAS_H

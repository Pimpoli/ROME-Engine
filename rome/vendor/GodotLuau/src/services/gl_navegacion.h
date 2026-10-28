#ifndef GL_NAVEGACION_H
#define GL_NAVEGACION_H

// PathfindingService de verdad (F5 de la v1.17).
//
// Antes ComputeAsync devolvia una linea recta de dos puntos, atravesando
// paredes. Ahora cada calculo hornea una malla de navegacion en la zona del
// inicio y el final (con la mitad del trabajo en otro hilo) y pide el camino
// al NavigationServer3D:
//   1. las formas de colision de la zona, por una consulta de fisica (solo las
//      piezas que chocan; personajes fuera), se pasan a geometria;
//   2. se hornea con el radio, la altura y el salto del agente;
//   3. un mapa propio con esa region da el camino, que se reparte cada
//      WaypointSpacing studs y marca Jump donde hay que subir.
// El lado Luau (luau_stdlib.h) espera con task.wait() a que __gl_nav.estado
// diga que esta hecho, asi que ComputeAsync cede como en Roblox.

#include <godot_cpp/classes/navigation_server3d.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/navigation_mesh.hpp>
#include <godot_cpp/classes/navigation_mesh_source_geometry_data3d.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters3d.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/sphere_shape3d.hpp>
#include <godot_cpp/classes/cylinder_shape3d.hpp>
#include <godot_cpp/classes/capsule_shape3d.hpp>
#include <godot_cpp/classes/convex_polygon_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/collision_object3d.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/os.hpp>
#include <unordered_map>
#include "gl_colgroups.h"   // GL_CAPA_CONSULTA: las piezas que no chocan no estorban

struct GLNavPedido {
    int estado = 0;   // 0 horneando, 1 esperando al mapa, 2 hecho
    Ref<World3D> mundo;
    Ref<NavigationMesh> malla;
    Ref<NavigationMeshSourceGeometryData3D> fuente;
    RID mapa, region;
    Vector3 inicio, fin;
    uint64_t ws_id = 0;   // el Workspace del juego que pide (para su Terrain)
    float radio = 2.0f, alto = 5.0f, espaciado = 4.0f, margen = 40.0f;
    bool salta = true, parcial = false;
    int esperas = 0, intentos = 0;
    int status = 5;   // Enum.PathStatus.NoPath
    PackedVector3Array puntos;
    PackedInt32Array acciones;   // Enum.PathWaypointAction: 0 Walk, 1 Jump
};
inline std::unordered_map<int64_t, GLNavPedido>& gl_nav_pedidos() {
    static std::unordered_map<int64_t, GLNavPedido> m;
    return m;
}
inline void gl_nav_soltar(GLNavPedido& p) {
    NavigationServer3D* ns = NavigationServer3D::get_singleton();
    if (ns && p.region.is_valid()) ns->free_rid(p.region);
    if (ns && p.mapa.is_valid()) ns->free_rid(p.mapa);
    p.region = RID();
    p.mapa = RID();
    p.malla.unref();
    p.fuente.unref();
    p.mundo.unref();
}
// Al descargar la extension (register_types.cpp): los Ref estaticos no pueden
// llegar vivos a la salida del proceso.
inline void gl_nav_limpiar() {
    for (auto& kv : gl_nav_pedidos()) gl_nav_soltar(kv.second);
    gl_nav_pedidos().clear();
}

// Una cara, con el orden de vertices de Godot para que su normal salga hacia
// `fuera` (la parte de arriba de una pieza tiene que verse como suelo).
inline void gl_nav_triangulo(PackedVector3Array& f, Vector3 a, Vector3 b, Vector3 c, const Vector3& fuera) {
    if ((b - a).cross(c - a).dot(fuera) > 0.0f) { const Vector3 t = b; b = c; c = t; }
    f.push_back(a); f.push_back(b); f.push_back(c);
}
inline void gl_nav_caja(PackedVector3Array& f, const Vector3& h) {
    for (int eje = 0; eje < 3; eje++) {
        const int u = (eje + 1) % 3, v = (eje + 2) % 3;
        for (int s = -1; s <= 1; s += 2) {
            Vector3 n; n[eje] = (float)s;
            Vector3 p[4];
            const float su[4] = { -1, 1, 1, -1 }, sv[4] = { -1, -1, 1, 1 };
            for (int k = 0; k < 4; k++) {
                p[k][eje] = s * h[eje];
                p[k][u] = su[k] * h[u];
                p[k][v] = sv[k] * h[v];
            }
            gl_nav_triangulo(f, p[0], p[1], p[2], n);
            gl_nav_triangulo(f, p[0], p[2], p[3], n);
        }
    }
}

// Los personajes (el jugador, los NPC con Humanoid) no son obstaculos.
inline bool gl_nav_es_personaje(Object* o) {
    Node* n = gl_cast<Node>(o);
    if (!n) return false;
    if (gl_cast<CharacterBody3D>(n)) return true;
    for (int i = 0; i < 3 && n; i++, n = n->get_parent())
        if (n->get_node_or_null(NodePath("Humanoid"))) return true;
    return false;
}

// Caras del casco de una forma convexa pequeña (cuña: 6 puntos; esquina: 5),
// por fuerza bruta: cada trio de puntos con todos los demas a un mismo lado es
// una cara. Con la caja que la envuelve, la rampa de una cuña era un muro, y en
// Build a Mountain al foso de la cantera se baja por rampas de cuñas.
inline void gl_nav_casco(PackedVector3Array& f, const PackedVector3Array& pts) {
    const int n = pts.size();
    Vector3 centro;
    for (int i = 0; i < n; i++) centro += pts[i];
    centro /= (float)n;
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            for (int c = b + 1; c < n; c++) {
                const Vector3 nrm = (pts[b] - pts[a]).cross(pts[c] - pts[a]);
                if (nrm.length_squared() < 1e-8f) continue;
                bool pos = false, neg = false;
                for (int k = 0; k < n && !(pos && neg); k++) {
                    if (k == a || k == b || k == c) continue;
                    const float d = nrm.dot(pts[k] - pts[a]);
                    if (d > 1e-4f) pos = true;
                    else if (d < -1e-4f) neg = true;
                }
                if (pos && neg) continue;   // hay puntos a los dos lados: no es cara
                gl_nav_triangulo(f, pts[a], pts[b], pts[c], (pts[a] + pts[b] + pts[c]) / 3.0f - centro);
            }
}

// Añade a `todas` las caras de una forma, ya en el mundo, si toca la zona.
// `vale` dice (y se apunta la primera vez) si el cuerpo cuenta: personajes y
// lo que no esta en el Workspace, fuera. Se mira solo cuando una forma toca la
// zona, porque subir por los padres es lo caro.
inline void gl_nav_forma(const Ref<Shape3D>& f, const Transform3D& xf, const AABB& zona,
                         CollisionObject3D* cuerpo, int& vale, PackedVector3Array& todas) {
    if (f.is_null()) return;
    PackedVector3Array caras;
    Transform3D x = xf;
    AABB local;
    bool filtrar = false;   // trimesh enorme: solo los triangulos de la zona
    if (BoxShape3D* b = gl_cast<BoxShape3D>(f.ptr())) {
        local = AABB(-b->get_size() * 0.5f, b->get_size());
    } else if (SphereShape3D* s = gl_cast<SphereShape3D>(f.ptr())) {
        const float r = s->get_radius();
        local = AABB(Vector3(-r, -r, -r), Vector3(r, r, r) * 2.0f);
    } else if (CylinderShape3D* cy = gl_cast<CylinderShape3D>(f.ptr())) {
        const float r = cy->get_radius(), h = cy->get_height() * 0.5f;
        local = AABB(Vector3(-r, -h, -r), Vector3(r, h, r) * 2.0f);
    } else if (CapsuleShape3D* ca = gl_cast<CapsuleShape3D>(f.ptr())) {
        const float r = ca->get_radius(), h = ca->get_height() * 0.5f;
        local = AABB(Vector3(-r, -h, -r), Vector3(r, h, r) * 2.0f);
    } else if (ConvexPolygonShape3D* cv = gl_cast<ConvexPolygonShape3D>(f.ptr())) {
        const PackedVector3Array pts = cv->get_points();
        if (pts.is_empty()) return;
        local = AABB(pts[0], Vector3());
        for (int k = 1; k < pts.size(); k++) local.expand_to(pts[k]);
        if (!zona.intersects(x.xform(local))) return;
        if (vale < 0) vale = (!gl_nav_es_personaje(cuerpo) && gl_en_workspace(cuerpo)) ? 1 : 0;
        if (vale == 0) return;
        if (pts.size() <= 12) gl_nav_casco(caras, pts);
        else { x = x * Transform3D(Basis(), local.get_center()); gl_nav_caja(caras, local.size * 0.5f); }
    } else if (ConcavePolygonShape3D* cc = gl_cast<ConcavePolygonShape3D>(f.ptr())) {
        caras = cc->get_faces();
        if (caras.is_empty()) return;
        local = AABB(caras[0], Vector3());
        const Vector3* r = caras.ptr();
        for (int64_t k = 1; k < caras.size(); k++) local.expand_to(r[k]);
        filtrar = caras.size() > 9000;
    } else {
        return;
    }
    if (caras.is_empty()) {
        // Caja, esfera, cilindro o capsula: su caja.
        if (!zona.intersects(x.xform(local))) return;
        if (vale < 0) vale = (!gl_nav_es_personaje(cuerpo) && gl_en_workspace(cuerpo)) ? 1 : 0;
        if (vale == 0) return;
        x = x * Transform3D(Basis(), local.get_center());
        gl_nav_caja(caras, local.size * 0.5f);
    } else if (gl_cast<ConcavePolygonShape3D>(f.ptr())) {
        if (!zona.intersects(x.xform(local))) return;
        if (vale < 0) vale = (!gl_nav_es_personaje(cuerpo) && gl_en_workspace(cuerpo)) ? 1 : 0;
        if (vale == 0) return;
    }
    const Vector3* r = caras.ptr();
    const int64_t n = caras.size();
    if (filtrar) {
        for (int64_t k = 0; k + 2 < n; k += 3) {
            const Vector3 a = x.xform(r[k]), b = x.xform(r[k + 1]), c = x.xform(r[k + 2]);
            AABB t(a, Vector3());
            t.expand_to(b);
            t.expand_to(c);
            if (!zona.intersects(t)) continue;
            todas.push_back(a); todas.push_back(b); todas.push_back(c);
        }
        return;
    }
    const int64_t base = todas.size();
    todas.resize(base + n);
    Vector3* w = todas.ptrw();
    for (int64_t k = 0; k < n; k++) w[base + k] = x.xform(r[k]);
}

inline void gl_nav_cuerpo(CollisionObject3D* co, const AABB& zona, PackedVector3Array& todas, int& formas) {
    if (!co || !co->is_inside_tree()) return;
    // Lo que solo esta en la capa de consultas (CanCollide = false) no estorba.
    if ((co->get_collision_layer() & ~GL_CAPA_CONSULTA) == 0) return;
    const Transform3D xb = co->get_global_transform();
    const PackedInt32Array owners = co->get_shape_owners();
    int vale = -1;
    for (int k = 0; k < owners.size(); k++) {
        const uint32_t ow = (uint32_t)owners[k];
        if (co->is_shape_owner_disabled(ow)) continue;
        const Transform3D xo = xb * co->shape_owner_get_transform(ow);
        for (int s = 0; s < co->shape_owner_get_shape_count(ow); s++) {
            const int64_t antes = todas.size();
            gl_nav_forma(co->shape_owner_get_shape(ow, s), xo, zona, co, vale, todas);
            if (todas.size() > antes) formas++;
            if (vale == 0) return;
        }
    }
}
inline void gl_nav_arbol(Node* n, const AABB& zona, PackedVector3Array& todas, int& formas) {
    if (CollisionObject3D* co = gl_cast<CollisionObject3D>(n)) gl_nav_cuerpo(co, zona, todas, formas);
    for (int i = 0; i < n->get_child_count(); i++) gl_nav_arbol(n->get_child(i), zona, todas, formas);
}

// La geometria de la zona: cada pieza del Workspace UNA vez (sale de la lista
// que lleva el propio motor) y el Terrain. Antes salia de una consulta de
// fisica (intersect_shape), y con Jolt una malla trimesh da UN resultado por
// cada triangulo que toca la caja: en Build a Mountain, 124 mallas del gimnasio
// llenaban el cupo de 4.096 resultados con copias de si mismas (4,5 millones de
// vertices), el suelo se quedaba fuera, no habia camino y la guia del tutorial
// lo volvia a pedir cada 2 s: tirones de 90-130 ms.
inline int gl_nav_recoger(const AABB& zona, Node* ws, PackedVector3Array& todas) {
    int formas = 0;
    for (const KeyValue<uint64_t, bool>& kv : gl_partes_workspace())
        gl_nav_cuerpo(gl_cast<CollisionObject3D>(ObjectDB::get_instance(ObjectID(kv.key))), zona, todas, formas);
    if (ws) {
        static const NodePath k_terreno("Terrain");
        if (Node* t = ws->get_node_or_null(k_terreno)) gl_nav_arbol(t, zona, todas, formas);
    }
    return formas;
}

// Hornea la zona del pedido (otra vez si se reintenta con mas margen). false
// si el mundo ya no esta.
inline bool gl_nav_hornear(GLNavPedido& p) {
    PhysicsDirectSpaceState3D* espacio = p.mundo.is_valid() ? p.mundo->get_direct_space_state() : nullptr;
    NavigationServer3D* ns = NavigationServer3D::get_singleton();
    if (!espacio || !ns) return false;
    const uint64_t t0 = Time::get_singleton()->get_ticks_usec();

    // La zona: el inicio y el final con margen, mas alta para cuestas y escaleras.
    const float mv = 30.0f + p.alto;
    AABB zona(p.inicio, Vector3());
    zona.expand_to(p.fin);
    zona.position -= Vector3(p.margen, mv, p.margen);
    zona.size += Vector3(p.margen, mv, p.margen) * 2.0f;
    // Celda de 0,5 studs; en zonas grandes, mas gruesa para que hornear no se dispare.
    const float celda = MAX(0.5f, MAX(zona.size.x, zona.size.z) / 400.0f);

    // Todas las caras, ya en el mundo, y UNA sola llamada a add_faces: con una
    // por forma, cada una volvia a copiar todo lo acumulado.
    Ref<NavigationMeshSourceGeometryData3D> fuente;
    fuente.instantiate();
    PackedVector3Array todas;
    Node* ws = gl_cast<Node>(ObjectDB::get_instance(ObjectID(p.ws_id)));
    const int formas = gl_nav_recoger(zona, ws, todas);
    if (!todas.is_empty()) fuente->add_faces(todas, Transform3D());

    Ref<NavigationMesh> malla;
    malla.instantiate();
    malla->set_cell_size(celda);
    malla->set_cell_height(0.25f);
    // En celdas enteras: Godot los redondea igual, pero avisando cada vez.
    malla->set_agent_radius(Math::ceil(p.radio / celda - 0.001f) * celda);
    malla->set_agent_height(Math::ceil(p.alto / 0.25f - 0.001f) * 0.25f);
    malla->set_agent_max_climb(p.salta ? 7.0f : 2.0f);   // salto de Roblox: ~7 studs
    malla->set_agent_max_slope(60.0f);
    malla->set_filter_baking_aabb(zona);
    ns->bake_from_source_geometry_data_async(malla, fuente);
    // GL_NAV_TRAZA=1: lo que cuesta en el hilo principal recoger la geometria.
    static const bool traza = OS::get_singleton()->get_environment("GL_NAV_TRAZA") == "1";
    if (traza)
        UtilityFunctions::print("[NAV] zona ", zona.size, " celda ", celda, " formas ", formas,
                                " vertices ", fuente->get_vertices().size() / 3, " intento ", p.intentos,
                                " us ", (int64_t)(Time::get_singleton()->get_ticks_usec() - t0));
    p.malla = malla;
    p.fuente = fuente;
    p.estado = 0;
    p.esperas = 0;
    return true;
}

// Empieza un calculo. 0 si no hay mundo 3D.
inline int64_t gl_nav_pedir(Node* contexto, const Vector3& ini, const Vector3& fin, float radio, float alto,
                            bool salta, float espaciado, bool parcial) {
    Viewport* vp = contexto ? contexto->get_viewport() : nullptr;
    GLNavPedido p;
    p.mundo = vp ? vp->find_world_3d() : Ref<World3D>();
    static const NodePath k_ws("Workspace");
    for (Node* n = contexto; n; n = n->get_parent())
        if (Node* ws = n->get_node_or_null(k_ws)) { p.ws_id = (uint64_t)ws->get_instance_id(); break; }
    p.inicio = ini;
    p.fin = fin;
    p.radio = radio;
    p.alto = alto;
    p.salta = salta;
    p.espaciado = espaciado;
    p.parcial = parcial;
    p.margen = CLAMP(ini.distance_to(fin) * 0.5f + 20.0f, 40.0f, 160.0f);
    if (!gl_nav_hornear(p)) return 0;
    static int64_t siguiente = 0;
    const int64_t id = ++siguiente;
    gl_nav_pedidos()[id] = p;
    return id;
}

// El camino ya horneado: los puntos, repartidos y con sus saltos.
inline void gl_nav_resolver(GLNavPedido& p) {
    NavigationServer3D* ns = NavigationServer3D::get_singleton();
    p.status = 5;
    p.puntos.clear();
    p.acciones.clear();
    if (ns->map_get_iteration_id(p.mapa) == 0) return;   // nunca llego a sincronizar
    const Vector3 ini = ns->map_get_closest_point(p.mapa, p.inicio);
    static const bool traza = OS::get_singleton()->get_environment("GL_NAV_TRAZA") != "";
    if (traza) {
        const PackedVector3Array c = ns->map_get_path(p.mapa, p.inicio, p.fin, true);
        UtilityFunctions::print("[NAV] ruta de ", p.inicio, " a ", p.fin, " | inicio en la malla ", ini,
                                " | fin en la malla ", ns->map_get_closest_point(p.mapa, p.fin),
                                " | puntos ", c.size(), " ultimo ", c.size() > 0 ? c[c.size() - 1] : Vector3());
    }
    if (ini.distance_to(p.inicio) > p.alto + 6.0f) return;   // el inicio no esta sobre nada caminable
    const PackedVector3Array cam = ns->map_get_path(p.mapa, p.inicio, p.fin, true);
    if (cam.size() < 1) return;
    // ¿Llega? El destino suele ser el centro de una pieza o del personaje: se
    // mide en planta y con holgura en altura.
    const Vector3 ult = cam[cam.size() - 1];
    const Vector2 plano(ult.x - p.fin.x, ult.z - p.fin.z);
    const bool llega = plano.length() <= p.radio + 2.0f && Math::abs(ult.y - p.fin.y) <= p.alto + 4.0f;
    if (!llega && !p.parcial) return;
    p.status = llega ? 0 : 1;   // Success / ClosestNoPath
    const float paso = (p.espaciado > 0.0f && p.espaciado < 1e6f) ? MAX(p.espaciado, 0.5f) : 0.0f;
    p.puntos.push_back(cam[0]);
    for (int i = 1; i < cam.size(); i++) {
        const Vector3 a = cam[i - 1], b = cam[i];
        if (paso > 0.0f) {
            const int n = (int)Math::floor(a.distance_to(b) / paso);
            for (int k = 1; k <= n; k++) {
                const Vector3 c = a.lerp(b, (k * paso) / a.distance_to(b));
                if (c.distance_to(b) > paso * 0.25f) p.puntos.push_back(c);
            }
        }
        p.puntos.push_back(b);
    }
    // Jump en el punto desde el que hay que subir a un escalon mas alto que el
    // paso normal de un Humanoid.
    for (int i = 0; i < p.puntos.size(); i++) {
        const bool sube = i + 1 < p.puntos.size() && p.puntos[i + 1].y - p.puntos[i].y > 1.5f;
        p.acciones.push_back(sube ? 1 : 0);
    }
}

// Avanza el calculo. 0 = sigue, 1 = hecho, -1 = no existe.
inline int gl_nav_avanzar(int64_t id) {
    auto it = gl_nav_pedidos().find(id);
    if (it == gl_nav_pedidos().end()) return -1;
    GLNavPedido& p = it->second;
    NavigationServer3D* ns = NavigationServer3D::get_singleton();
    if (p.estado == 0) {
        if (ns->is_baking_navigation_mesh(p.malla)) return 0;
        p.mapa = ns->map_create();
        ns->map_set_up(p.mapa, Vector3(0, 1, 0));
        ns->map_set_cell_size(p.mapa, p.malla->get_cell_size());
        ns->map_set_cell_height(p.mapa, p.malla->get_cell_height());
        ns->map_set_active(p.mapa, true);
        p.region = ns->region_create();
        ns->region_set_map(p.region, p.mapa);
        ns->region_set_navigation_mesh(p.region, p.malla);
        p.estado = 1;
        return 0;
    }
    if (p.estado == 1) {
        // El mapa se sincroniza en un paso posterior del servidor: hasta que el
        // punto mas cercano no sea de NUESTRA region, aun no la tiene.
        // Antes de la primera sincronizacion no se le puede preguntar nada.
        const bool listo = ns->map_get_iteration_id(p.mapa) > 0 &&
                           ns->map_get_closest_point_owner(p.mapa, p.inicio) == p.region;
        if (!listo && ++p.esperas < 60) return 0;
        gl_nav_resolver(p);
        // Sin camino puede ser que el rodeo se salga de la zona: otra vez, mas
        // grande (dos veces como mucho).
        if (p.status != 0 && p.intentos < 2) {
            if (p.region.is_valid()) ns->free_rid(p.region);
            if (p.mapa.is_valid()) ns->free_rid(p.mapa);
            p.region = RID();
            p.mapa = RID();
            p.intentos++;
            p.margen *= 2.5f;
            if (gl_nav_hornear(p)) return 0;
        }
        gl_nav_soltar(p);
        p.estado = 2;
    }
    return 1;
}

// ── __gl_nav en cada VM ────────────────────────────────────────────────
// pedir(inicio, fin, radio, alto, salta, espaciado, parcial) -> id | nil
inline int gl_nav_lua_pedir(lua_State* L) {
    LuauVector3* a = check_vector3(L, 1);
    LuauVector3* b = check_vector3(L, 2);
    if (!a || !b) { lua_pushnil(L); return 1; }
    Node* sn = gl_nodo_script_de(L);
    const int64_t id = gl_nav_pedir(sn, Vector3(a->x, a->y, a->z), Vector3(b->x, b->y, b->z),
                                    (float)luaL_optnumber(L, 3, 2.0), (float)luaL_optnumber(L, 4, 5.0),
                                    lua_isnoneornil(L, 5) ? true : lua_toboolean(L, 5) != 0,
                                    (float)luaL_optnumber(L, 6, 4.0), lua_toboolean(L, 7) != 0);
    if (id == 0) lua_pushnil(L); else lua_pushnumber(L, (double)id);
    return 1;
}
// estado(id) -> nil mientras trabaja; (status, {Vector3...}, {accion...}) al acabar.
inline int gl_nav_lua_estado(lua_State* L) {
    const int64_t id = (int64_t)luaL_checknumber(L, 1);
    const int r = gl_nav_avanzar(id);
    if (r == 0) return 0;
    if (r < 0) { lua_pushnumber(L, 5); lua_newtable(L); lua_newtable(L); return 3; }
    GLNavPedido p = gl_nav_pedidos()[id];
    gl_nav_pedidos().erase(id);
    lua_pushnumber(L, p.status);
    lua_createtable(L, p.puntos.size(), 0);
    for (int i = 0; i < p.puntos.size(); i++) {
        push_vector3(L, p.puntos[i].x, p.puntos[i].y, p.puntos[i].z);
        lua_rawseti(L, -2, i + 1);
    }
    lua_createtable(L, p.acciones.size(), 0);
    for (int i = 0; i < p.acciones.size(); i++) {
        lua_pushnumber(L, p.acciones[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 3;
}
inline void gl_nav_registrar(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, gl_nav_lua_pedir, "pedir");
    lua_setfield(L, -2, "pedir");
    lua_pushcfunction(L, gl_nav_lua_estado, "estado");
    lua_setfield(L, -2, "estado");
    lua_setglobal(L, "__gl_nav");
}

#endif // GL_NAVEGACION_H

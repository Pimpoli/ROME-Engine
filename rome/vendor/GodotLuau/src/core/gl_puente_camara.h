#ifndef GL_PUENTE_CAMARA_H
#define GL_PUENTE_CAMARA_H
#include "gl_cast.h"

// ════════════════════════════════════════════════════════════════════
//  Camera de Roblox en el puente (lectura y escritura).
//  Se incluye desde luau_api.h, despues de sus ayudantes (push_vector3,
//  wrap_node, gl_raycast_roblox...). Va aparte para no seguir engordando
//  luau_api.h.
//
//  La camara del jugador cuelga de su rig (Camera3D -> SpringArm -> pivotes ->
//  RobloxPlayer) y es el rig quien la mueve; por eso CameraType, CameraSubject
//  y CFrame se le pasan a el. Cualquier otra Camera3D guarda esos valores en
//  metas, como antes.
// ════════════════════════════════════════════════════════════════════

inline RobloxPlayer* gl_rig_de_camara(Node* cam) {
    Node* p = cam ? cam->get_parent() : nullptr;
    for (int i = 0; p && i < 4; i++, p = p->get_parent())
        if (RobloxPlayer* rp = gl_cast<RobloxPlayer>(p)) return rp;
    return nullptr;
}

// Un Ray de verdad (el del stdlib): `typeof(r) == "Ray"` y r.Unit funcionan.
inline void gl_push_ray(lua_State* L, const Vector3& o, const Vector3& d) {
    lua_getglobal(L, "Ray");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "new");
        lua_remove(L, -2);
        if (lua_isfunction(L, -1)) {
            push_vector3(L, o.x, o.y, o.z);
            push_vector3(L, d.x, d.y, d.z);
            lua_call(L, 2, 1);
            return;
        }
    }
    lua_pop(L, 1);
    lua_pushnil(L);
}

inline Camera3D* gl_camara_de_upvalue(lua_State* L) {
    const uint64_t id = (uint64_t)(uintptr_t)lua_touserdata(L, lua_upvalueindex(1));
    return gl_cast<Camera3D>(ObjectDB::get_instance(ObjectID(id)));
}
inline void gl_push_closure_camara(lua_State* L, Camera3D* c, lua_CFunction f, const char* nombre) {
    lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)c->get_instance_id());
    lua_pushcclosure(L, f, nombre, 1);
}

// FieldOfView de Roblox es el vertical. Diagonal y MaxAxis salen del aspecto de
// la pantalla escalando la tangente del medio angulo.
inline float gl_camara_aspecto(Camera3D* c) {
    Viewport* vp = c->get_viewport();
    const Vector2 sz = vp ? vp->get_visible_rect().size : Vector2(1280, 720);
    return sz.y > 0.0f ? sz.x / sz.y : 1.0f;
}
inline float gl_fov_escalado(float fov, float factor) {
    return (float)Math::rad_to_deg(2.0 * Math::atan(Math::tan(Math::deg_to_rad((double)fov) * 0.5) * factor));
}
inline void gl_camara_poner_fov(Camera3D* c, float vertical) {
    vertical = Math::clamp(vertical, 1.0f, 120.0f);   // el rango de Roblox
    c->set_fov(vertical);
    if (gl_rig_de_camara(c)) gl_camara_estado().fov = vertical;
}

// Camera:GetPartsObscuringTarget(puntos, ignorar): las piezas entre la camara y
// cada punto. Se excluye cada cuerpo tocado y se repite hasta llegar al punto.
inline void gl_partes_que_tapan(Camera3D* c, const Vector<Vector3>& puntos,
                                const Vector<Node*>& ignorar, LocalVector<Node*>& salida) {
    if (!c->is_inside_tree() || c->get_world_3d().is_null()) return;
    PhysicsDirectSpaceState3D* space = c->get_world_3d()->get_direct_space_state();
    if (!space) return;
    const Vector3 ojo = c->get_global_position();
    HashMap<uint64_t, bool> vistas;
    for (int i = 0; i < puntos.size(); i++) {
        Ref<PhysicsRayQueryParameters3D> q;
        q.instantiate();
        q->set_from(ojo);
        q->set_to(puntos[i]);
        for (int vuelta = 0; vuelta < 64; vuelta++) {
            Dictionary res = gl_raycast_roblox(space, q, 64, &ignorar, false, false);
            if (res.is_empty()) break;
            const Variant cv = res.get("collider", Variant());
            Node* hit = (cv.get_type() == Variant::OBJECT)
                      ? gl_golpe_a_pieza(gl_cast<Node>(cv.operator Object*()), (int)res.get("shape", -1))
                      : nullptr;
            if (hit && !vistas.has((uint64_t)hit->get_instance_id())) {
                vistas[(uint64_t)hit->get_instance_id()] = true;
                salida.push_back(hit);
            }
            const Variant vrid = res.get("rid", Variant());
            if (vrid.get_type() != Variant::RID) break;
            TypedArray<RID> ex = q->get_exclude();
            ex.push_back(vrid);
            q->set_exclude(ex);
        }
    }
}

// ── Lectura. Devuelve cuantos valores dejo en la pila, o -1 si no es suya. ──
inline int gl_camara_index(lua_State* L, Camera3D* cam3, const char* key) {
    RobloxPlayer* rig = gl_rig_de_camara(cam3);
    switch (key[0]) {
    case 'V':
        if (strcmp(key, "ViewportSize") == 0) {
            Viewport* vp = cam3->get_viewport();
            const Vector2 sz = vp ? vp->get_visible_rect().size : Vector2(1280, 720);
            push_vector2(L, sz.x, sz.y);
            return 1;
        }
        if (strcmp(key, "ViewportPointToRay") == 0) break;
        return -1;
    case 'F':
        if (strcmp(key, "FieldOfView") == 0) { lua_pushnumber(L, cam3->get_fov()); return 1; }
        if (strcmp(key, "Focus") == 0) {
            static const StringName k("_gl_camera_focus");
            if (rig) { _gl_push_cframe_pos(L, rig->gl_camera_focus()); return 1; }
            if (cam3->has_meta(k)) { _gl_push_cframe_full(L, (Transform3D)cam3->get_meta(k)); return 1; }
            _gl_push_cframe_pos(L, cam3->is_inside_tree() ? cam3->get_global_position() : Vector3());
            return 1;
        }
        return -1;
    case 'D':
        if (strcmp(key, "DiagonalFieldOfView") == 0) {
            const float a = gl_camara_aspecto(cam3);
            lua_pushnumber(L, gl_fov_escalado(cam3->get_fov(), Math::sqrt(1.0f + a * a)));
            return 1;
        }
        return -1;
    case 'M':
        if (strcmp(key, "MaxAxisFieldOfView") == 0) {
            const float a = gl_camara_aspecto(cam3);
            lua_pushnumber(L, a > 1.0f ? gl_fov_escalado(cam3->get_fov(), a) : cam3->get_fov());
            return 1;
        }
        return -1;
    case 'C':
        // Enum.CameraType como EnumItem: con un numero, `== Enum.CameraType.X`
        // daba false siempre.
        if (strcmp(key, "CameraType") == 0) {
            static const StringName k("_gl_camera_type");
            const int t = rig ? rig->gl_get_camera_type()
                              : (cam3->has_meta(k) ? (int)(int64_t)cam3->get_meta(k) : 5);
            uis_push_enum(L, "CameraType", t);
            return 1;
        }
        if (strcmp(key, "CameraSubject") == 0) {
            static const StringName k("_gl_camera_subject");
            Node* sub = nullptr;
            if (rig) {
                sub = rig->gl_get_camera_subject();
                if (!sub) sub = rig->get_node_or_null("Humanoid");   // el de Roblox por defecto
            } else if (cam3->has_meta(k)) {
                sub = gl_cast<Node>(cam3->get_meta(k));
            }
            if (sub) wrap_node(L, sub); else lua_pushnil(L);
            return 1;
        }
        return -1;
    case 'N':
        if (strcmp(key, "NearPlaneZ") == 0) { lua_pushnumber(L, -cam3->get_near()); return 1; }
        return -1;
    case 'W':
        if (strcmp(key, "WorldToViewportPoint") == 0 || strcmp(key, "WorldToScreenPoint") == 0) break;
        return -1;
    case 'S':
        if (strcmp(key, "ScreenPointToRay") == 0) break;
        return -1;
    case 'G':
        if (strcmp(key, "GetPartsObscuringTarget") == 0 || strcmp(key, "GetRenderCFrame") == 0) break;
        return -1;
    default:
        return -1;
    }

    // ── Metodos ──
    // El cierre guarda el ObjectID, no el puntero: un script puede quedarse la
    // funcion y la camara morir con el personaje.
    if (strcmp(key, "WorldToViewportPoint") == 0 || strcmp(key, "WorldToScreenPoint") == 0) {
        // -> (Vector3(x, y, profundidad), enPantalla). Sin margen de la barra
        // superior: aqui GetGuiInset es (0, 0), asi que Screen y Viewport coinciden.
        gl_push_closure_camara(L, cam3, [](lua_State* pL) -> int {
            Camera3D* c = gl_camara_de_upvalue(pL);
            LuauVector3* v = check_vector3(pL, 2);
            if (!v) v = check_vector3(pL, 1);
            if (!c || !v || !c->is_inside_tree()) {
                push_vector3(pL, 0, 0, 0); lua_pushboolean(pL, false); return 2;
            }
            const Vector3 mundo(v->x, v->y, v->z);
            const Vector2 p = c->unproject_position(mundo);
            // Z de Roblox: distancia POR DELANTE de la camara (negativa detras).
            const Transform3D ct = c->get_global_transform();
            const float prof = -ct.basis.get_column(2).dot(mundo - ct.origin);
            push_vector3(pL, p.x, p.y, prof);
            bool dentro = prof > 0.0f;
            if (dentro) {
                Viewport* vp = c->get_viewport();
                const Vector2 sz = vp ? vp->get_visible_rect().size : Vector2(1280, 720);
                dentro = p.x >= 0 && p.y >= 0 && p.x <= sz.x && p.y <= sz.y;
            }
            lua_pushboolean(pL, dentro);
            return 2;
        }, "WorldToViewportPoint");
        return 1;
    }
    if (strcmp(key, "ScreenPointToRay") == 0 || strcmp(key, "ViewportPointToRay") == 0) {
        // (x, y, profundidad = 0) -> Ray unitario desde el pixel, hacia donde mira.
        gl_push_closure_camara(L, cam3, [](lua_State* pL) -> int {
            Camera3D* c = gl_camara_de_upvalue(pL);
            const int b = lua_type(pL, 1) == LUA_TNUMBER ? 1 : 2;
            const Vector2 p((float)luaL_checknumber(pL, b), (float)luaL_checknumber(pL, b + 1));
            const float prof = (float)luaL_optnumber(pL, b + 2, 0.0);
            if (!c || !c->is_inside_tree()) { gl_push_ray(pL, Vector3(), Vector3(0, 0, -1)); return 1; }
            const Vector3 d = c->project_ray_normal(p);
            gl_push_ray(pL, c->project_ray_origin(p) + d * prof, d);
            return 1;
        }, "ScreenPointToRay");
        return 1;
    }
    if (strcmp(key, "GetRenderCFrame") == 0) {
        gl_push_closure_camara(L, cam3, [](lua_State* pL) -> int {
            Camera3D* c = gl_camara_de_upvalue(pL);
            _gl_push_cframe_full(pL, (c && c->is_inside_tree()) ? c->get_global_transform() : Transform3D());
            return 1;
        }, "GetRenderCFrame");
        return 1;
    }
    // GetPartsObscuringTarget(puntos: {Vector3}, ignorar: {Instance}) -> {BasePart}
    gl_push_closure_camara(L, cam3, [](lua_State* pL) -> int {
        Camera3D* c = gl_camara_de_upvalue(pL);
        const int b = lua_istable(pL, 1) ? 1 : 2;
        Vector<Vector3> puntos;
        if (lua_istable(pL, b)) {
            const int n = (int)lua_objlen(pL, b);
            for (int i = 1; i <= n; i++) {
                lua_rawgeti(pL, b, i);
                if (LuauVector3* v = check_vector3(pL, -1)) puntos.push_back(Vector3(v->x, v->y, v->z));
                lua_pop(pL, 1);
            }
        }
        Vector<Node*> ignorar;
        gl_leer_filtro(pL, b + 1, ignorar);
        LocalVector<Node*> tapan;
        if (c) gl_partes_que_tapan(c, puntos, ignorar, tapan);
        lua_createtable(pL, (int)tapan.size(), 0);
        for (uint32_t i = 0; i < tapan.size(); i++) {
            wrap_node(pL, tapan[i]);
            lua_rawseti(pL, -2, (int)i + 1);
        }
        return 1;
    }, "GetPartsObscuringTarget");
    return 1;
}

// ── Escritura. Devuelve true si la propiedad era suya. ──
inline bool gl_camara_newindex(lua_State* L, Camera3D* cam3, const char* key) {
    RobloxPlayer* rig = gl_rig_de_camara(cam3);
    if (strcmp(key, "CameraType") == 0) {
        const int t = (int)gl_num_or(L, 3, 5.0);
        if (rig) rig->gl_set_camera_type(t);
        else     cam3->set_meta("_gl_camera_type", (int64_t)t);
        return true;
    }
    if (strcmp(key, "CameraSubject") == 0) {
        GodotObjectWrapper* sw = _gl_has_metatable(L, 3, "GodotObject")
                               ? (GodotObjectWrapper*)lua_touserdata(L, 3) : nullptr;
        Node* sub = sw ? gow_node(sw) : nullptr;
        if (rig) rig->gl_set_camera_subject(sub);
        if (sub) cam3->set_meta("_gl_camera_subject", sub);
        else if (cam3->has_meta("_gl_camera_subject")) cam3->remove_meta("_gl_camera_subject");
        return true;
    }
    if (strcmp(key, "FieldOfView") == 0) {
        gl_camara_poner_fov(cam3, (float)gl_num(L, 3));
        return true;
    }
    if (strcmp(key, "DiagonalFieldOfView") == 0 || strcmp(key, "MaxAxisFieldOfView") == 0) {
        const float a = gl_camara_aspecto(cam3);
        const float factor = key[0] == 'D' ? Math::sqrt(1.0f + a * a) : Math::max(a, 1.0f);
        const float mitad = (float)Math::deg_to_rad(Math::clamp(gl_num(L, 3), 1.0, 179.0)) * 0.5f;
        gl_camara_poner_fov(cam3, (float)Math::rad_to_deg(2.0 * Math::atan(Math::tan(mitad) / factor)));
        return true;
    }
    if (strcmp(key, "CFrame") == 0 && lua_istable(L, 3)) {
        const Transform3D t = _gl_read_cframe_tbl(L, 3);
        if (rig) rig->gl_camara_cframe(t);
        else if (cam3->is_inside_tree()) cam3->set_global_transform(t);
        else cam3->set_transform(t);
        return true;
    }
    if (strcmp(key, "Focus") == 0 && lua_istable(L, 3)) {
        cam3->set_meta("_gl_camera_focus", _gl_read_cframe_tbl(L, 3));
        return true;
    }
    return false;
}

#endif // GL_PUENTE_CAMARA_H

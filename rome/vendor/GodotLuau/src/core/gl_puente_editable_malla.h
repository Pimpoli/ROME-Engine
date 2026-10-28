#ifndef GL_PUENTE_EDITABLE_MALLA_H
#define GL_PUENTE_EDITABLE_MALLA_H
#include "gl_cast.h"

// ════════════════════════════════════════════════════════════════════
//  Los metodos de EditableMesh para Luau. Los incluye gl_puente_editables.h,
//  que pone el userdata, los ayudantes de argumentos y la tabla de nombres.
//  La malla en si (ids estables, caras, normales automaticas, rayos) vive en
//  gl_editable_malla.h.
// ════════════════════════════════════════════════════════════════════

inline GLEditableMesh* gl_em_de(lua_State* L, const char* fn, bool aunque_destruida = false) {
    GLEditableUD* ud = gl_ed_ud(L, 1, GL_ED_MALLA);
    if (!ud) luaL_error(L, "Expected ':' not '.' calling member function %s", fn);
    GLEditableMesh* em = gl_cast<GLEditableMesh>(ud->ref.ptr());
    if (!em || (em->destruida && !aunque_destruida)) luaL_error(L, "%s: the EditableMesh has been destroyed", fn);
    return em;
}
inline void gl_em_topologia(lua_State* L, GLEditableMesh* em, const char* fn) {
    if (em->fija) luaL_error(L, "%s: cannot add or remove elements of a fixed-size EditableMesh", fn);
}
inline double gl_em_num(lua_State* L, int idx, const char* fn) {
    if (lua_type(L, idx) != LUA_TNUMBER)
        luaL_error(L, "invalid argument #%d to '%s' (number expected, got %s)", idx - 1, fn, gl_ed_tipo(L, idx));
    return lua_tonumber(L, idx);
}
// Ids de cada tipo: el indice interno, o error como Roblox.
inline uint32_t gl_em_v(lua_State* L, GLEditableMesh* em, int idx, const char* fn) {
    uint32_t i = 0;
    if (!em->vert_ok(gl_em_num(L, idx, fn), i)) luaL_error(L, "%s: invalid vertex ID", fn);
    return i;
}
inline uint32_t gl_em_f(lua_State* L, GLEditableMesh* em, int idx, const char* fn) {
    uint32_t i = 0;
    if (!em->cara_ok(gl_em_num(L, idx, fn), i)) luaL_error(L, "%s: invalid face ID", fn);
    return i;
}
inline uint32_t gl_em_n(lua_State* L, GLEditableMesh* em, int idx, const char* fn) {
    uint32_t i = 0;
    if (!em->normal_ok(gl_em_num(L, idx, fn), i)) luaL_error(L, "%s: invalid normal ID", fn);
    return i;
}
inline uint32_t gl_em_uv(lua_State* L, GLEditableMesh* em, int idx, const char* fn) {
    uint32_t i = 0;
    if (!em->uv_ok(gl_em_num(L, idx, fn), i)) luaL_error(L, "%s: invalid UV ID", fn);
    return i;
}
inline uint32_t gl_em_c(lua_State* L, GLEditableMesh* em, int idx, const char* fn) {
    uint32_t i = 0;
    if (!em->color_ok(gl_em_num(L, idx, fn), i)) luaL_error(L, "%s: invalid color ID", fn);
    return i;
}
inline void gl_em_lista(lua_State* L, const std::vector<double>& ids) {
    lua_createtable(L, (int)ids.size(), 0);
    for (size_t i = 0; i < ids.size(); i++) {
        lua_pushnumber(L, ids[i]);
        lua_rawseti(L, -2, (int)i + 1);
    }
}
// La esquina de la cara f que usa el vertice v (-1 si no esta).
inline int gl_em_esquina(const GLEditableMesh* em, uint32_t f, uint32_t v) {
    const GLEMCara& cf = em->caras[f];
    for (int k = 0; k < cf.n; k++) if (cf.v[k] == v) return k;
    return -1;
}
inline void gl_em_push_impacto(lua_State* L, GLEditableMesh* em, const GLEMImpacto& r) {
    lua_pushnumber(L, em->id_cara(r.cara));
    push_vector3(L, r.punto.x, r.punto.y, r.punto.z);
    push_vector3(L, r.bari.x, r.bari.y, r.bari.z);
    for (int k = 0; k < 3; k++) lua_pushnumber(L, em->id_vert(r.v[k]));
}

// ── Altas y bajas ──────────────────────────────────────────────────
inline int gl_em_AddVertex(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "AddVertex");
    gl_em_topologia(L, em, "AddVertex");
    const Vector3 p = gl_ed_vec3(L, 2, "AddVertex");
    if (em->n_verts >= GL_EM_MAX_VERTICES)
        luaL_error(L, "AddVertex: EditableMesh vertex limit of %d reached", GL_EM_MAX_VERTICES);
    const uint32_t v = em->nuevo_vertice(p);
    em->marcar();
    lua_pushnumber(L, em->id_vert(v));
    return 1;
}
inline int gl_em_cara_nueva(lua_State* L, GLEditableMesh* em, const uint32_t* vs, int cuantos, const char* fn) {
    gl_em_topologia(L, em, fn);
    if (em->n_tris + (cuantos == 4 ? 2 : 1) > GL_EM_MAX_TRIANGULOS)
        luaL_error(L, "%s: EditableMesh triangle limit of %d reached", fn, GL_EM_MAX_TRIANGULOS);
    const uint32_t f = em->nueva_cara(vs, cuantos);
    lua_pushnumber(L, em->id_cara(f));
    return 1;
}
inline int gl_em_AddTriangle(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "AddTriangle");
    uint32_t vs[3];
    for (int k = 0; k < 3; k++) vs[k] = gl_em_v(L, em, 2 + k, "AddTriangle");
    return gl_em_cara_nueva(L, em, vs, 3, "AddTriangle");
}
// Lee una lista de ids de vertice (3 o 4 para una cara).
inline int gl_em_ids_vertices(lua_State* L, GLEditableMesh* em, int idx, uint32_t* vs, const char* fn) {
    if (!lua_istable(L, idx)) luaL_error(L, "invalid argument #%d to '%s' (table expected, got %s)", idx - 1, fn, gl_ed_tipo(L, idx));
    const int n = lua_objlen(L, idx);
    if (n != 3 && n != 4) luaL_error(L, "%s: a face needs 3 or 4 vertex IDs (got %d)", fn, n);
    for (int k = 0; k < n; k++) {
        lua_rawgeti(L, idx, k + 1);
        uint32_t i = 0;
        const bool ok = lua_type(L, -1) == LUA_TNUMBER && em->vert_ok(lua_tonumber(L, -1), i);
        lua_pop(L, 1);
        if (!ok) luaL_error(L, "%s: invalid vertex ID", fn);
        vs[k] = i;
    }
    return n;
}
inline int gl_em_AddFace(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "AddFace");
    uint32_t vs[4];
    const int n = gl_em_ids_vertices(L, em, 2, vs, "AddFace");
    return gl_em_cara_nueva(L, em, vs, n, "AddFace");
}
inline int gl_em_AddNormal(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "AddNormal");
    gl_em_topologia(L, em, "AddNormal");
    uint32_t i;
    if (lua_isnoneornil(L, 2)) i = em->nueva_normal(nullptr);
    else { const Vector3 n = gl_ed_vec3(L, 2, "AddNormal"); i = em->nueva_normal(&n); }
    lua_pushnumber(L, em->id_normal(i));
    return 1;
}
inline int gl_em_AddUV(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "AddUV");
    gl_em_topologia(L, em, "AddUV");
    lua_pushnumber(L, em->id_uv(em->nuevo_uv(gl_ed_vec2(L, 2, "AddUV"))));
    return 1;
}
inline int gl_em_AddColor(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "AddColor");
    gl_em_topologia(L, em, "AddColor");
    Color c = gl_ed_color(L, 2, "AddColor");
    c.a = (float)std::clamp(gl_em_num(L, 3, "AddColor"), 0.0, 1.0);
    lua_pushnumber(L, em->id_color(em->nuevo_color(c)));
    return 1;
}
inline int gl_em_RemoveFace(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "RemoveFace");
    gl_em_topologia(L, em, "RemoveFace");
    em->quitar_cara(gl_em_f(L, em, 2, "RemoveFace"));
    return 0;
}
// Lo que no usa ninguna cara: vertices, normales, uvs y colores.
inline int gl_em_RemoveUnused(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "RemoveUnused");
    gl_em_topologia(L, em, "RemoveUnused");
    std::vector<char> usa_n(em->normales.size(), 0), usa_uv(em->uvs.size(), 0), usa_c(em->colores.size(), 0);
    for (const GLEMCara& cf : em->caras) {
        if (!cf.vivo) continue;
        for (int k = 0; k < cf.n; k++) { usa_n[cf.nn[k]] = 1; usa_uv[cf.uv[k]] = 1; usa_c[cf.col[k]] = 1; }
    }
    std::vector<double> quitados;
    for (uint32_t i = 0; i < em->verts.size(); i++) {
        if (!em->verts[i].vivo || !em->verts[i].caras.empty()) continue;
        quitados.push_back(em->id_vert(i));
        GLEditableMesh::soltar(em->verts, em->libres_v, i);
        em->n_verts--;
    }
    for (uint32_t i = 0; i < em->normales.size(); i++)
        if (em->normales[i].vivo && !usa_n[i]) { quitados.push_back(em->id_normal(i)); GLEditableMesh::soltar(em->normales, em->libres_n, i); }
    for (uint32_t i = 0; i < em->uvs.size(); i++)
        if (em->uvs[i].vivo && !usa_uv[i]) { quitados.push_back(em->id_uv(i)); GLEditableMesh::soltar(em->uvs, em->libres_uv, i); }
    for (uint32_t i = 0; i < em->colores.size(); i++)
        if (em->colores[i].vivo && !usa_c[i]) { quitados.push_back(em->id_color(i)); GLEditableMesh::soltar(em->colores, em->libres_c, i); }
    em->marcar();
    gl_em_lista(L, quitados);
    return 1;
}
inline int gl_em_Clear(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "Clear");
    gl_em_topologia(L, em, "Clear");
    em->vaciar_todo();
    return 0;
}
// Cada quad en dos triangulos, por la diagonal mas corta. La cara vieja se
// queda con el primero.
inline int gl_em_Triangulate(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "Triangulate");
    const size_t total = em->caras.size();
    for (uint32_t f = 0; f < total; f++) {
        if (!em->caras[f].vivo || em->caras[f].n != 4) continue;
        int t[2][3];
        em->triangulos_de(em->caras[f], t);
        const GLEMCara vieja = em->caras[f];
        const uint32_t nf = GLEditableMesh::ocupar(em->caras, em->libres_f);
        GLEMCara& a = em->caras[f];
        GLEMCara& b = em->caras[nf];
        a.n = 3; b.n = 3;
        for (int e = 0; e < 3; e++) {
            a.v[e] = vieja.v[t[0][e]]; a.nn[e] = vieja.nn[t[0][e]]; a.uv[e] = vieja.uv[t[0][e]]; a.col[e] = vieja.col[t[0][e]];
            b.v[e] = vieja.v[t[1][e]]; b.nn[e] = vieja.nn[t[1][e]]; b.uv[e] = vieja.uv[t[1][e]]; b.col[e] = vieja.col[t[1][e]];
        }
        for (int k = 0; k < 4; k++) {
            std::vector<uint32_t>& lista = em->verts[vieja.v[k]].caras;
            lista.erase(std::remove(lista.begin(), lista.end(), f), lista.end());
        }
        for (int e = 0; e < 3; e++) {
            std::vector<uint32_t>& la = em->verts[em->caras[f].v[e]].caras;
            if (std::find(la.begin(), la.end(), f) == la.end()) la.push_back(f);
            std::vector<uint32_t>& lb = em->verts[em->caras[nf].v[e]].caras;
            if (std::find(lb.begin(), lb.end(), nf) == lb.end()) lb.push_back(nf);
        }
    }
    em->marcar();
    return 0;
}
// Junta los vertices a menos de `tolerancia`: se queda el primero y las
// caras pasan a usarlo. Devuelve {viejo = nuevo}.
inline int gl_em_MergeVertices(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "MergeVertices");
    gl_em_topologia(L, em, "MergeVertices");
    const float tol = (float)std::max(0.0, gl_em_num(L, 2, "MergeVertices"));
    const float celda = std::max(tol, 1e-6f);
    std::unordered_map<int64_t, std::vector<uint32_t>> rejilla;
    auto clave = [&](int64_t x, int64_t y, int64_t z) { return (x * 73856093) ^ (y * 19349663) ^ (z * 83492791); };
    std::vector<uint32_t> destino(em->verts.size(), UINT32_MAX);
    lua_newtable(L);
    for (uint32_t i = 0; i < em->verts.size(); i++) {
        if (!em->verts[i].vivo) continue;
        const Vector3 p = em->verts[i].p;
        const int64_t cx = (int64_t)std::floor(p.x / celda), cy = (int64_t)std::floor(p.y / celda), cz = (int64_t)std::floor(p.z / celda);
        uint32_t junto = UINT32_MAX;
        for (int64_t dx = -1; dx <= 1 && junto == UINT32_MAX; dx++)
            for (int64_t dy = -1; dy <= 1 && junto == UINT32_MAX; dy++)
                for (int64_t dz = -1; dz <= 1 && junto == UINT32_MAX; dz++) {
                    auto it = rejilla.find(clave(cx + dx, cy + dy, cz + dz));
                    if (it == rejilla.end()) continue;
                    for (uint32_t j : it->second)
                        if (em->verts[j].p.distance_to(p) <= tol) { junto = j; break; }
                }
        if (junto == UINT32_MAX) { rejilla[clave(cx, cy, cz)].push_back(i); continue; }
        destino[i] = junto;
        lua_pushnumber(L, em->id_vert(i));
        lua_pushnumber(L, em->id_vert(junto));
        lua_rawset(L, -3);
    }
    for (uint32_t i = 0; i < em->verts.size(); i++) {
        if (destino[i] == UINT32_MAX) continue;
        const uint32_t d = destino[i];
        for (uint32_t f : em->verts[i].caras) {
            GLEMCara& cf = em->caras[f];
            for (int k = 0; k < cf.n; k++) if (cf.v[k] == i) cf.v[k] = d;
            std::vector<uint32_t>& lista = em->verts[d].caras;
            if (std::find(lista.begin(), lista.end(), f) == lista.end()) lista.push_back(f);
        }
        em->verts[i].caras.clear();
        GLEditableMesh::soltar(em->verts, em->libres_v, i);
        em->n_verts--;
    }
    em->marcar();
    return 1;
}

// ── Valores ────────────────────────────────────────────────────────
inline int gl_em_GetPosition(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetPosition");
    const Vector3 p = em->verts[gl_em_v(L, em, 2, "GetPosition")].p;
    push_vector3(L, p.x, p.y, p.z);
    return 1;
}
inline int gl_em_SetPosition(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "SetPosition");
    const uint32_t v = gl_em_v(L, em, 2, "SetPosition");
    em->verts[v].p = gl_ed_vec3(L, 3, "SetPosition");
    em->marcar();
    return 0;
}
inline int gl_em_GetNormal(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetNormal");
    const Vector3 n = em->normal_de(gl_em_n(L, em, 2, "GetNormal"));
    push_vector3(L, n.x, n.y, n.z);
    return 1;
}
inline int gl_em_SetNormal(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "SetNormal");
    const uint32_t i = gl_em_n(L, em, 2, "SetNormal");
    em->normales[i].n = gl_ed_vec3(L, 3, "SetNormal");
    em->normales[i].autom = false;
    em->marcar(false);
    return 0;
}
inline int gl_em_ResetNormal(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "ResetNormal");
    em->normales[gl_em_n(L, em, 2, "ResetNormal")].autom = true;
    em->marcar();
    return 0;
}
inline int gl_em_GetUV(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetUV");
    const Vector2 uv = em->uvs[gl_em_uv(L, em, 2, "GetUV")].uv;
    push_vector2(L, uv.x, uv.y);
    return 1;
}
inline int gl_em_SetUV(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "SetUV");
    const uint32_t i = gl_em_uv(L, em, 2, "SetUV");
    em->uvs[i].uv = gl_ed_vec2(L, 3, "SetUV");
    em->marcar(false);
    return 0;
}
inline int gl_em_GetColor(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetColor");
    const Color c = em->colores[gl_em_c(L, em, 2, "GetColor")].c;
    push_color3(L, c.r, c.g, c.b);
    return 1;
}
inline int gl_em_SetColor(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "SetColor");
    const uint32_t i = gl_em_c(L, em, 2, "SetColor");
    const Color c = gl_ed_color(L, 3, "SetColor");
    Color& d = em->colores[i].c;
    d.r = c.r; d.g = c.g; d.b = c.b;
    em->marcar(false);
    return 0;
}
inline int gl_em_GetColorAlpha(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetColorAlpha");
    lua_pushnumber(L, em->colores[gl_em_c(L, em, 2, "GetColorAlpha")].c.a);
    return 1;
}
inline int gl_em_SetColorAlpha(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "SetColorAlpha");
    const uint32_t i = gl_em_c(L, em, 2, "SetColorAlpha");
    em->colores[i].c.a = (float)std::clamp(gl_em_num(L, 3, "SetColorAlpha"), 0.0, 1.0);
    em->marcar(false);
    return 0;
}

// ── Listas ─────────────────────────────────────────────────────────
template <typename T, typename F>
inline void gl_em_vivos(lua_State* L, const std::vector<T>& vec, F id) {
    std::vector<double> ids;
    for (uint32_t i = 0; i < vec.size(); i++) if (vec[i].vivo) ids.push_back(id(i));
    gl_em_lista(L, ids);
}
inline int gl_em_GetVertices(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetVertices");
    gl_em_vivos(L, em->verts, [em](uint32_t i) { return em->id_vert(i); });
    return 1;
}
inline int gl_em_GetFaces(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetFaces");
    gl_em_vivos(L, em->caras, [em](uint32_t i) { return em->id_cara(i); });
    return 1;
}
inline int gl_em_GetNormals(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetNormals");
    gl_em_vivos(L, em->normales, [em](uint32_t i) { return em->id_normal(i); });
    return 1;
}
inline int gl_em_GetUVs(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetUVs");
    gl_em_vivos(L, em->uvs, [em](uint32_t i) { return em->id_uv(i); });
    return 1;
}
inline int gl_em_GetColors(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetColors");
    gl_em_vivos(L, em->colores, [em](uint32_t i) { return em->id_color(i); });
    return 1;
}

// ── Caras: sus esquinas ────────────────────────────────────────────
// 0 vertices, 1 normales, 2 uvs, 3 colores.
inline int gl_em_cara_lista(lua_State* L, int que, const char* fn) {
    GLEditableMesh* em = gl_em_de(L, fn);
    const GLEMCara& cf = em->caras[gl_em_f(L, em, 2, fn)];
    std::vector<double> ids;
    for (int k = 0; k < cf.n; k++) {
        if (que == 0) ids.push_back(em->id_vert(cf.v[k]));
        else if (que == 1) ids.push_back(em->id_normal(cf.nn[k]));
        else if (que == 2) ids.push_back(em->id_uv(cf.uv[k]));
        else ids.push_back(em->id_color(cf.col[k]));
    }
    gl_em_lista(L, ids);
    return 1;
}
inline int gl_em_GetFaceVertices(lua_State* L) { return gl_em_cara_lista(L, 0, "GetFaceVertices"); }
inline int gl_em_GetFaceNormals(lua_State* L) { return gl_em_cara_lista(L, 1, "GetFaceNormals"); }
inline int gl_em_GetFaceUVs(lua_State* L) { return gl_em_cara_lista(L, 2, "GetFaceUVs"); }
inline int gl_em_GetFaceColors(lua_State* L) { return gl_em_cara_lista(L, 3, "GetFaceColors"); }

inline int gl_em_cara_poner(lua_State* L, int que, const char* fn) {
    GLEditableMesh* em = gl_em_de(L, fn);
    const uint32_t f = gl_em_f(L, em, 2, fn);
    if (!lua_istable(L, 3)) luaL_error(L, "invalid argument #2 to '%s' (table expected, got %s)", fn, gl_ed_tipo(L, 3));
    const int n = lua_objlen(L, 3);
    if (n != em->caras[f].n) luaL_error(L, "%s: expected %d IDs, one per corner of the face (got %d)", fn, (int)em->caras[f].n, n);
    uint32_t ids[4];
    for (int k = 0; k < n; k++) {
        lua_rawgeti(L, 3, k + 1);
        const double d = lua_type(L, -1) == LUA_TNUMBER ? lua_tonumber(L, -1) : 0.0;
        lua_pop(L, 1);
        uint32_t i = 0;
        const bool ok = que == 1 ? em->normal_ok(d, i) : (que == 2 ? em->uv_ok(d, i) : em->color_ok(d, i));
        if (!ok) luaL_error(L, "%s: invalid %s ID", fn, que == 1 ? "normal" : (que == 2 ? "UV" : "color"));
        ids[k] = i;
    }
    GLEMCara& cf = em->caras[f];
    for (int k = 0; k < n; k++) {
        if (que == 1) cf.nn[k] = ids[k];
        else if (que == 2) cf.uv[k] = ids[k];
        else cf.col[k] = ids[k];
    }
    em->marcar(que == 1);
    return 0;
}
inline int gl_em_SetFaceNormals(lua_State* L) { return gl_em_cara_poner(L, 1, "SetFaceNormals"); }
inline int gl_em_SetFaceUVs(lua_State* L) { return gl_em_cara_poner(L, 2, "SetFaceUVs"); }
inline int gl_em_SetFaceColors(lua_State* L) { return gl_em_cara_poner(L, 3, "SetFaceColors"); }

// Cambia los vertices de la cara (3 o 4). Las esquinas que ya habia conservan
// sus atributos; una esquina nueva los recibe como en AddFace.
inline int gl_em_SetFaceVertices(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "SetFaceVertices");
    const uint32_t f = gl_em_f(L, em, 2, "SetFaceVertices");
    uint32_t vs[4];
    const int n = gl_em_ids_vertices(L, em, 3, vs, "SetFaceVertices");
    const int antes = em->caras[f].n;
    if (n != antes) {
        gl_em_topologia(L, em, "SetFaceVertices");
        if (n > antes && em->n_tris + 1 > GL_EM_MAX_TRIANGULOS)
            luaL_error(L, "SetFaceVertices: EditableMesh triangle limit of %d reached", GL_EM_MAX_TRIANGULOS);
    }
    for (int k = 0; k < antes; k++) {
        std::vector<uint32_t>& lista = em->verts[em->caras[f].v[k]].caras;
        lista.erase(std::remove(lista.begin(), lista.end(), f), lista.end());
    }
    for (int k = antes; k < n; k++) {
        uint32_t nn, uv, c;
        em->atributos_para(vs[k], nn, uv, c);
        GLEMCara& cf = em->caras[f];
        cf.nn[k] = nn; cf.uv[k] = uv; cf.col[k] = c;
    }
    GLEMCara& cf = em->caras[f];
    for (int k = 0; k < n; k++) cf.v[k] = vs[k];
    em->n_tris += (n == 4 ? 2 : 1) - (antes == 4 ? 2 : 1);
    cf.n = (uint8_t)n;
    for (int k = 0; k < n; k++) {
        std::vector<uint32_t>& lista = em->verts[vs[k]].caras;
        if (std::find(lista.begin(), lista.end(), f) == lista.end()) lista.push_back(f);
    }
    em->marcar();
    return 0;
}

// ── Vertices: lo que los toca ──────────────────────────────────────
inline int gl_em_GetVertexFaces(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetVertexFaces");
    std::vector<double> ids;
    for (uint32_t f : em->verts[gl_em_v(L, em, 2, "GetVertexFaces")].caras) ids.push_back(em->id_cara(f));
    gl_em_lista(L, ids);
    return 1;
}
// Atributos de las esquinas que usan el vertice, sin repetir.
inline int gl_em_vertice_attrs(lua_State* L, int que, const char* fn) {
    GLEditableMesh* em = gl_em_de(L, fn);
    const uint32_t v = gl_em_v(L, em, 2, fn);
    std::vector<uint32_t> vistos;
    std::vector<double> ids;
    for (uint32_t f : em->verts[v].caras) {
        const GLEMCara& cf = em->caras[f];
        for (int k = 0; k < cf.n; k++) {
            if (cf.v[k] != v) continue;
            const uint32_t a = que == 1 ? cf.nn[k] : (que == 2 ? cf.uv[k] : cf.col[k]);
            if (std::find(vistos.begin(), vistos.end(), a) != vistos.end()) continue;
            vistos.push_back(a);
            ids.push_back(que == 1 ? em->id_normal(a) : (que == 2 ? em->id_uv(a) : em->id_color(a)));
        }
    }
    gl_em_lista(L, ids);
    return 1;
}
inline int gl_em_GetVertexNormals(lua_State* L) { return gl_em_vertice_attrs(L, 1, "GetVertexNormals"); }
inline int gl_em_GetVertexUVs(lua_State* L) { return gl_em_vertice_attrs(L, 2, "GetVertexUVs"); }
inline int gl_em_GetVertexColors(lua_State* L) { return gl_em_vertice_attrs(L, 3, "GetVertexColors"); }

inline int gl_em_vertice_cara(lua_State* L, int que, bool poner, const char* fn) {
    GLEditableMesh* em = gl_em_de(L, fn);
    const uint32_t v = gl_em_v(L, em, 2, fn);
    const uint32_t f = gl_em_f(L, em, 3, fn);
    const int k = gl_em_esquina(em, f, v);
    if (k < 0) luaL_error(L, "%s: the vertex is not part of the face", fn);
    GLEMCara& cf = em->caras[f];
    if (!poner) {
        lua_pushnumber(L, que == 1 ? em->id_normal(cf.nn[k]) : (que == 2 ? em->id_uv(cf.uv[k]) : em->id_color(cf.col[k])));
        return 1;
    }
    if (que == 1) cf.nn[k] = gl_em_n(L, em, 4, fn);
    else if (que == 2) cf.uv[k] = gl_em_uv(L, em, 4, fn);
    else cf.col[k] = gl_em_c(L, em, 4, fn);
    em->marcar(que == 1);
    return 0;
}
inline int gl_em_GetVertexFaceNormal(lua_State* L) { return gl_em_vertice_cara(L, 1, false, "GetVertexFaceNormal"); }
inline int gl_em_GetVertexFaceUV(lua_State* L) { return gl_em_vertice_cara(L, 2, false, "GetVertexFaceUV"); }
inline int gl_em_GetVertexFaceColor(lua_State* L) { return gl_em_vertice_cara(L, 3, false, "GetVertexFaceColor"); }
inline int gl_em_SetVertexFaceNormal(lua_State* L) { return gl_em_vertice_cara(L, 1, true, "SetVertexFaceNormal"); }
inline int gl_em_SetVertexFaceUV(lua_State* L) { return gl_em_vertice_cara(L, 2, true, "SetVertexFaceUV"); }
inline int gl_em_SetVertexFaceColor(lua_State* L) { return gl_em_vertice_cara(L, 3, true, "SetVertexFaceColor"); }

// Caras o vertices que usan un atributo (normal 1, uv 2, color 3).
inline int gl_em_con_attr(lua_State* L, int que, bool vertices, const char* fn) {
    GLEditableMesh* em = gl_em_de(L, fn);
    const uint32_t a = que == 1 ? gl_em_n(L, em, 2, fn) : (que == 2 ? gl_em_uv(L, em, 2, fn) : gl_em_c(L, em, 2, fn));
    std::vector<uint32_t> vistos;
    std::vector<double> ids;
    for (uint32_t f = 0; f < em->caras.size(); f++) {
        const GLEMCara& cf = em->caras[f];
        if (!cf.vivo) continue;
        for (int k = 0; k < cf.n; k++) {
            const uint32_t x = que == 1 ? cf.nn[k] : (que == 2 ? cf.uv[k] : cf.col[k]);
            if (x != a) continue;
            const uint32_t q = vertices ? cf.v[k] : f;
            if (std::find(vistos.begin(), vistos.end(), q) != vistos.end()) continue;
            vistos.push_back(q);
            ids.push_back(vertices ? em->id_vert(q) : em->id_cara(q));
        }
    }
    gl_em_lista(L, ids);
    return 1;
}
inline int gl_em_GetFacesWithNormal(lua_State* L) { return gl_em_con_attr(L, 1, false, "GetFacesWithNormal"); }
inline int gl_em_GetFacesWithUV(lua_State* L) { return gl_em_con_attr(L, 2, false, "GetFacesWithUV"); }
inline int gl_em_GetFacesWithColor(lua_State* L) { return gl_em_con_attr(L, 3, false, "GetFacesWithColor"); }
inline int gl_em_GetVerticesWithNormal(lua_State* L) { return gl_em_con_attr(L, 1, true, "GetVerticesWithNormal"); }
inline int gl_em_GetVerticesWithUV(lua_State* L) { return gl_em_con_attr(L, 2, true, "GetVerticesWithUV"); }
inline int gl_em_GetVerticesWithColor(lua_State* L) { return gl_em_con_attr(L, 3, true, "GetVerticesWithColor"); }

// Vecinos por arista.
inline int gl_em_GetAdjacentVertices(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetAdjacentVertices");
    const uint32_t v = gl_em_v(L, em, 2, "GetAdjacentVertices");
    std::vector<uint32_t> vistos;
    for (uint32_t f : em->verts[v].caras) {
        const GLEMCara& cf = em->caras[f];
        const int k = gl_em_esquina(em, f, v);
        if (k < 0) continue;
        const uint32_t ady[2] = { cf.v[(k + 1) % cf.n], cf.v[(k + cf.n - 1) % cf.n] };
        for (uint32_t a : ady)
            if (a != v && std::find(vistos.begin(), vistos.end(), a) == vistos.end()) vistos.push_back(a);
    }
    std::vector<double> ids;
    for (uint32_t a : vistos) ids.push_back(em->id_vert(a));
    gl_em_lista(L, ids);
    return 1;
}
inline int gl_em_GetAdjacentFaces(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetAdjacentFaces");
    const uint32_t f = gl_em_f(L, em, 2, "GetAdjacentFaces");
    const GLEMCara& cf = em->caras[f];
    std::vector<uint32_t> vistos;
    for (int k = 0; k < cf.n; k++) {
        const uint32_t a = cf.v[k], b = cf.v[(k + 1) % cf.n];
        for (uint32_t g : em->verts[a].caras) {
            if (g == f || std::find(vistos.begin(), vistos.end(), g) != vistos.end()) continue;
            if (gl_em_esquina(em, g, b) >= 0) vistos.push_back(g);
        }
    }
    std::vector<double> ids;
    for (uint32_t g : vistos) ids.push_back(em->id_cara(g));
    gl_em_lista(L, ids);
    return 1;
}

// ── Busquedas ──────────────────────────────────────────────────────
inline int gl_em_GetSize(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetSize");
    const Vector3 s = em->caja().size;
    push_vector3(L, s.x, s.y, s.z);
    return 1;
}
inline int gl_em_GetCenter(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "GetCenter");
    const AABB b = em->caja();
    const Vector3 c = b.position + b.size * 0.5f;
    push_vector3(L, c.x, c.y, c.z);
    return 1;
}
inline int gl_em_FindClosestVertex(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "FindClosestVertex");
    const Vector3 p = gl_ed_vec3(L, 2, "FindClosestVertex");
    float mejor = 1e30f;
    int64_t cual = -1;
    for (uint32_t i = 0; i < em->verts.size(); i++) {
        if (!em->verts[i].vivo) continue;
        const float d = em->verts[i].p.distance_squared_to(p);
        if (d < mejor) { mejor = d; cual = i; }
    }
    if (cual < 0) lua_pushnil(L);
    else lua_pushnumber(L, em->id_vert((uint32_t)cual));
    return 1;
}
inline int gl_em_FindVerticesWithinSphere(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "FindVerticesWithinSphere");
    const Vector3 c = gl_ed_vec3(L, 2, "FindVerticesWithinSphere");
    const float r = (float)gl_em_num(L, 3, "FindVerticesWithinSphere");
    std::vector<double> ids;
    for (uint32_t i = 0; i < em->verts.size(); i++)
        if (em->verts[i].vivo && em->verts[i].p.distance_to(c) <= r) ids.push_back(em->id_vert(i));
    gl_em_lista(L, ids);
    return 1;
}
inline int gl_em_FindClosestPointOnSurface(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "FindClosestPointOnSurface");
    const GLEMImpacto r = em->mas_cercano(gl_ed_vec3(L, 2, "FindClosestPointOnSurface"));
    if (!r.hay) { lua_pushnil(L); return 1; }
    gl_em_push_impacto(L, em, r);
    return 6;
}
inline int gl_em_RaycastLocal(lua_State* L) {
    GLEditableMesh* em = gl_em_de(L, "RaycastLocal");
    const Vector3 o = gl_ed_vec3(L, 2, "RaycastLocal");
    const Vector3 d = gl_ed_vec3(L, 3, "RaycastLocal");
    const GLEMImpacto r = em->rayo(o, d);
    if (!r.hay) { lua_pushnil(L); return 1; }
    gl_em_push_impacto(L, em, r);
    return 6;
}
// "v12", "f3", "n7"... con la version detras si el hueco se reutilizo.
inline int gl_em_IdDebugString(lua_State* L) {
    gl_em_de(L, "IdDebugString", true);
    const double d = gl_em_num(L, 2, "IdDebugString");
    static const char letras[] = { '?', 'v', 'f', 'n', 'u', 'c' };
    char txt[64];
    if (!(d >= 1.0) || d >= 9007199254740992.0) { lua_pushstring(L, "invalid"); return 1; }
    const uint64_t v = (uint64_t)d;
    const int tipo = (int)(v >> 44);
    const uint64_t idx = (v & 0xFFFFFFu);
    const uint64_t ver = (v >> 24) & 0xFFFFFu;
    if (tipo < 1 || tipo > 5 || idx == 0) { lua_pushstring(L, "invalid"); return 1; }
    if (ver) snprintf(txt, sizeof(txt), "%c%llu.%llu", letras[tipo], (unsigned long long)(idx - 1), (unsigned long long)ver);
    else snprintf(txt, sizeof(txt), "%c%llu", letras[tipo], (unsigned long long)(idx - 1));
    lua_pushstring(L, txt);
    return 1;
}
inline int gl_em_Destroy(lua_State* L) {
    gl_em_de(L, "Destroy", true)->destruir();
    return 0;
}
inline int gl_em_IsA(lua_State* L) {
    gl_em_de(L, "IsA", true);
    const char* c = luaL_checkstring(L, 2);
    lua_pushboolean(L, strcmp(c, "EditableMesh") == 0 || strcmp(c, "Object") == 0);
    return 1;
}

inline constexpr const char* kGLEMNombres[] = {
    "AddVertex", "AddTriangle", "AddFace", "AddNormal", "AddUV", "AddColor",
    "RemoveFace", "RemoveUnused", "Clear", "Triangulate", "MergeVertices",
    "GetPosition", "SetPosition", "GetNormal", "SetNormal", "ResetNormal",
    "GetUV", "SetUV", "GetColor", "SetColor", "GetColorAlpha", "SetColorAlpha",
    "GetVertices", "GetFaces", "GetNormals", "GetUVs", "GetColors",
    "GetFaceVertices", "GetFaceNormals", "GetFaceUVs", "GetFaceColors",
    "SetFaceVertices", "SetFaceNormals", "SetFaceUVs", "SetFaceColors",
    "GetVertexFaces", "GetVertexNormals", "GetVertexUVs", "GetVertexColors",
    "GetVertexFaceNormal", "GetVertexFaceUV", "GetVertexFaceColor",
    "SetVertexFaceNormal", "SetVertexFaceUV", "SetVertexFaceColor",
    "GetFacesWithNormal", "GetFacesWithUV", "GetFacesWithColor",
    "GetVerticesWithNormal", "GetVerticesWithUV", "GetVerticesWithColor",
    "GetAdjacentVertices", "GetAdjacentFaces", "GetSize", "GetCenter",
    "FindClosestVertex", "FindVerticesWithinSphere", "FindClosestPointOnSurface",
    "RaycastLocal", "IdDebugString", "Destroy", "IsA", "isA",
};
inline constexpr lua_CFunction kGLEMFunciones[] = {
    gl_em_AddVertex, gl_em_AddTriangle, gl_em_AddFace, gl_em_AddNormal, gl_em_AddUV, gl_em_AddColor,
    gl_em_RemoveFace, gl_em_RemoveUnused, gl_em_Clear, gl_em_Triangulate, gl_em_MergeVertices,
    gl_em_GetPosition, gl_em_SetPosition, gl_em_GetNormal, gl_em_SetNormal, gl_em_ResetNormal,
    gl_em_GetUV, gl_em_SetUV, gl_em_GetColor, gl_em_SetColor, gl_em_GetColorAlpha, gl_em_SetColorAlpha,
    gl_em_GetVertices, gl_em_GetFaces, gl_em_GetNormals, gl_em_GetUVs, gl_em_GetColors,
    gl_em_GetFaceVertices, gl_em_GetFaceNormals, gl_em_GetFaceUVs, gl_em_GetFaceColors,
    gl_em_SetFaceVertices, gl_em_SetFaceNormals, gl_em_SetFaceUVs, gl_em_SetFaceColors,
    gl_em_GetVertexFaces, gl_em_GetVertexNormals, gl_em_GetVertexUVs, gl_em_GetVertexColors,
    gl_em_GetVertexFaceNormal, gl_em_GetVertexFaceUV, gl_em_GetVertexFaceColor,
    gl_em_SetVertexFaceNormal, gl_em_SetVertexFaceUV, gl_em_SetVertexFaceColor,
    gl_em_GetFacesWithNormal, gl_em_GetFacesWithUV, gl_em_GetFacesWithColor,
    gl_em_GetVerticesWithNormal, gl_em_GetVerticesWithUV, gl_em_GetVerticesWithColor,
    gl_em_GetAdjacentVertices, gl_em_GetAdjacentFaces, gl_em_GetSize, gl_em_GetCenter,
    gl_em_FindClosestVertex, gl_em_FindVerticesWithinSphere, gl_em_FindClosestPointOnSurface,
    gl_em_RaycastLocal, gl_em_IdDebugString, gl_em_Destroy, gl_em_IsA, gl_em_IsA,
};
static_assert(sizeof(kGLEMNombres) / sizeof(kGLEMNombres[0]) == sizeof(kGLEMFunciones) / sizeof(kGLEMFunciones[0]),
              "kGLEMNombres y kGLEMFunciones deben ir a la par");

#endif // GL_PUENTE_EDITABLE_MALLA_H

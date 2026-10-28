#ifndef GL_PUENTE_EFECTOS_H
#define GL_PUENTE_EFECTOS_H

// ════════════════════════════════════════════════════════════════════
//  ParticleEmitter, Beam, Trail, Highlight, luces, Decal/Texture y
//  SpecialMesh en el puente: las propiedades que llevan TIPO de Roblox
//  (ColorSequence, NumberSequence, NumberRange, EnumItem) y las que el puente
//  generico de Node3D se comeria (Rotation, Orientation...).
//  Se incluye desde luau_api.h, despues de sus ayudantes.
//
//  Va primero en la lectura y en la escritura, pero solo cuesta algo si la
//  clave es una de estas y el nodo lleva la marca _gl_efecto: Size o Color de
//  una pieza, que son claves calientes, pagan una consulta de meta y siguen.
// ════════════════════════════════════════════════════════════════════

#include "roblox_particulas.h"
#include "gl_cast.h"
#include "roblox_luces.h"
#include "roblox_cintas.h"
#include "roblox_highlight.h"
#include "rbx_instances.h"

inline bool gl_clave_de_efecto(const char* k) {
    switch (k[0]) {
        case 'C': return strcmp(k, "Color") == 0 || strcmp(k, "Clear") == 0;
        case 'D': return strcmp(k, "DepthMode") == 0;
        case 'E': return strcmp(k, "EmissionDirection") == 0 || strcmp(k, "Emit") == 0 || strcmp(k, "Enabled") == 0;
        case 'F': return strcmp(k, "Face") == 0 || strcmp(k, "FlipbookFramerate") == 0 ||
                         strcmp(k, "FlipbookLayout") == 0 || strcmp(k, "FlipbookMode") == 0;
        case 'L': return strcmp(k, "Lifetime") == 0;
        case 'M': return strcmp(k, "MeshType") == 0;
        case 'O': return strcmp(k, "Orientation") == 0;
        case 'R': return strcmp(k, "Rotation") == 0 || strcmp(k, "RotSpeed") == 0;
        case 'S': return strcmp(k, "Size") == 0 || strcmp(k, "Speed") == 0 || strcmp(k, "Squash") == 0 ||
                         strcmp(k, "Shape") == 0 || strcmp(k, "ShapeStyle") == 0 || strcmp(k, "ShapeInOut") == 0;
        case 'T': return strcmp(k, "Transparency") == 0 || strcmp(k, "TextureMode") == 0;
        case 'W': return strcmp(k, "WidthScale") == 0;
        default:  return false;
    }
}
inline bool gl_es_efecto(Node* n) {
    static const StringName k("_gl_efecto");
    return n && n->has_meta(k);
}

// ── Tipos de Roblox de vuelta a Luau, con sus constructores del stdlib ──
inline bool gl_push_constructor(lua_State* L, const char* tipo, const char* fn = "new") {
    lua_getglobal(L, tipo);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return false; }
    lua_getfield(L, -1, fn);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }
    return true;
}
inline void gl_push_number_range(lua_State* L, const Vector2& r) {
    if (!gl_push_constructor(L, "NumberRange")) { lua_pushnil(L); return; }
    lua_pushnumber(L, r.x);
    lua_pushnumber(L, r.y);
    lua_call(L, 2, 1);
}
inline void gl_push_number_sequence(lua_State* L, const Variant& v, float defecto) {
    const GLSecNum s = gl_sec_num(v, defecto);
    if (!gl_push_constructor(L, "NumberSequence")) { lua_pushnil(L); return; }
    lua_createtable(L, (int)s.t.size(), 0);
    for (uint32_t i = 0; i < s.t.size(); i++) {
        if (!gl_push_constructor(L, "NumberSequenceKeypoint")) { lua_pushnil(L); }
        else {
            lua_pushnumber(L, s.t[i]);
            lua_pushnumber(L, s.v[i]);
            lua_pushnumber(L, s.e[i]);
            lua_call(L, 3, 1);
        }
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_call(L, 1, 1);
}
inline void gl_push_color_sequence(lua_State* L, const Variant& v) {
    const GLSecColor s = gl_sec_color(v, Color(1, 1, 1));
    if (!gl_push_constructor(L, "ColorSequence")) { lua_pushnil(L); return; }
    lua_createtable(L, (int)s.t.size(), 0);
    for (uint32_t i = 0; i < s.t.size(); i++) {
        if (!gl_push_constructor(L, "ColorSequenceKeypoint")) { lua_pushnil(L); }
        else {
            lua_pushnumber(L, s.t[i]);
            push_color3(L, s.c[i].r, s.c[i].g, s.c[i].b);
            lua_call(L, 2, 1);
        }
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_call(L, 1, 1);
}

// ── Lectura. Devuelve cuantos valores dejo en la pila, o -1 si no es suya. ──
inline int gl_efectos_index(lua_State* L, Node* n, const char* key) {
    if (!gl_clave_de_efecto(key) || !gl_es_efecto(n)) return -1;

    if (ParticleEmitter* pe = gl_cast<ParticleEmitter>(n)) {
        if (strcmp(key, "Color") == 0)        { gl_push_color_sequence(L, pe->get_color_seq()); return 1; }
        if (strcmp(key, "Transparency") == 0) { gl_push_number_sequence(L, pe->get_transparency(), 0.0f); return 1; }
        if (strcmp(key, "Size") == 0)         { gl_push_number_sequence(L, pe->get_size_seq(), 1.0f); return 1; }
        if (strcmp(key, "Squash") == 0)       { gl_push_number_sequence(L, pe->get_squash(), 0.0f); return 1; }
        if (strcmp(key, "Lifetime") == 0)     { gl_push_number_range(L, gl_rango(pe->get_lifetime_rbx(), Vector2(5, 10))); return 1; }
        if (strcmp(key, "Speed") == 0)        { gl_push_number_range(L, gl_rango(pe->get_speed(), Vector2(5, 5))); return 1; }
        if (strcmp(key, "Rotation") == 0)     { gl_push_number_range(L, gl_rango(pe->get_rotation_rbx(), Vector2())); return 1; }
        if (strcmp(key, "RotSpeed") == 0)     { gl_push_number_range(L, gl_rango(pe->get_rot_speed(), Vector2())); return 1; }
        if (strcmp(key, "FlipbookFramerate") == 0) { gl_push_number_range(L, gl_rango(pe->get_flip_fps(), Vector2(1, 1))); return 1; }
        if (strcmp(key, "EmissionDirection") == 0) { uis_push_enum(L, "NormalId", pe->get_direccion()); return 1; }
        if (strcmp(key, "Shape") == 0)        { uis_push_enum(L, "ParticleEmitterShape", pe->get_forma()); return 1; }
        if (strcmp(key, "ShapeStyle") == 0)   { uis_push_enum(L, "ParticleEmitterShapeStyle", pe->get_forma_estilo()); return 1; }
        if (strcmp(key, "ShapeInOut") == 0)   { uis_push_enum(L, "ParticleEmitterShapeInOut", pe->get_forma_inout()); return 1; }
        if (strcmp(key, "Orientation") == 0)  { uis_push_enum(L, "ParticleOrientation", pe->get_orientacion()); return 1; }
        if (strcmp(key, "FlipbookLayout") == 0) { uis_push_enum(L, "ParticleFlipbookLayout", pe->get_flip_layout()); return 1; }
        if (strcmp(key, "FlipbookMode") == 0) { uis_push_enum(L, "ParticleFlipbookMode", pe->get_flip_mode()); return 1; }
        if (strcmp(key, "Enabled") == 0)      { lua_pushboolean(L, pe->get_enabled_rbx()); return 1; }
        if (strcmp(key, "Emit") == 0 || strcmp(key, "Clear") == 0) {
            lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)pe->get_instance_id());
            if (key[0] == 'E')
                lua_pushcclosure(L, [](lua_State* LL) -> int {
                    const uint64_t id = (uint64_t)(uintptr_t)lua_touserdata(LL, lua_upvalueindex(1));
                    if (ParticleEmitter* x = gl_cast<ParticleEmitter>(ObjectDB::get_instance(ObjectID(id))))
                        x->Emit((int)luaL_optinteger(LL, 2, 16));
                    return 0;
                }, "Emit", 1);
            else
                lua_pushcclosure(L, [](lua_State* LL) -> int {
                    const uint64_t id = (uint64_t)(uintptr_t)lua_touserdata(LL, lua_upvalueindex(1));
                    if (ParticleEmitter* x = gl_cast<ParticleEmitter>(ObjectDB::get_instance(ObjectID(id)))) x->Clear();
                    return 0;
                }, "Clear", 1);
            return 1;
        }
        return -1;
    }
    if (GLCinta* c = gl_cast<GLCinta>(n)) {
        if (strcmp(key, "Color") == 0)        { gl_push_color_sequence(L, c->get_color_seq()); return 1; }
        if (strcmp(key, "Transparency") == 0) { gl_push_number_sequence(L, c->get_transparency(), 0.5f); return 1; }
        if (strcmp(key, "TextureMode") == 0)  { uis_push_enum(L, "TextureMode", c->get_texture_mode()); return 1; }
        if (strcmp(key, "Enabled") == 0)      { lua_pushboolean(L, c->get_enabled()); return 1; }
        if (Trail* t = gl_cast<Trail>(n)) {
            if (strcmp(key, "WidthScale") == 0) { gl_push_number_sequence(L, t->get_width_scale(), 1.0f); return 1; }
            if (strcmp(key, "Lifetime") == 0)   { lua_pushnumber(L, t->get_lifetime_rbx()); return 1; }
            if (strcmp(key, "Clear") == 0) {
                lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)t->get_instance_id());
                lua_pushcclosure(L, [](lua_State* LL) -> int {
                    const uint64_t id = (uint64_t)(uintptr_t)lua_touserdata(LL, lua_upvalueindex(1));
                    if (Trail* x = gl_cast<Trail>(ObjectDB::get_instance(ObjectID(id)))) x->Clear();
                    return 0;
                }, "Clear", 1);
                return 1;
            }
        }
        return -1;
    }
    if (Highlight* h = gl_cast<Highlight>(n)) {
        if (strcmp(key, "DepthMode") == 0) { uis_push_enum(L, "HighlightDepthMode", h->get_depth_mode()); return 1; }
        if (strcmp(key, "Enabled") == 0)   { lua_pushboolean(L, h->get_enabled()); return 1; }
        return -1;
    }
    if (strcmp(key, "Face") == 0) {
        static const StringName k_face("Face");
        uis_push_enum(L, "NormalId", (int)n->get(k_face));
        return 1;
    }
    if (strcmp(key, "MeshType") == 0) {
        if (RBXMesh* m = gl_cast<RBXMesh>(n)) { uis_push_enum(L, "MeshType", m->get_mesh_type()); return 1; }
    }
    return -1;
}

// ── Escritura. Devuelve true si la propiedad era suya. ──
// Lo que llega (secuencia, rango, EnumItem, Color3...) se convierte con
// gl_luau_to_variant y va a la propiedad de la clase, que sabe que hacer.
inline bool gl_efectos_newindex(lua_State* L, Node* n, const char* key) {
    if (!gl_clave_de_efecto(key) || !gl_es_efecto(n)) return false;
    if (!gl_has_property(n, String(key))) return false;
    Variant gv;
    if (!gl_luau_to_variant(L, 3, gv)) return false;
    n->set(StringName(key), gv);
    return true;
}

#endif // GL_PUENTE_EFECTOS_H

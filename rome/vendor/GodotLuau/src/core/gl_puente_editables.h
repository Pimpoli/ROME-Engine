#ifndef GL_PUENTE_EDITABLES_H
#define GL_PUENTE_EDITABLES_H

// ════════════════════════════════════════════════════════════════════
//  EditableImage, EditableMesh, Content y AssetService en el puente Luau.
//  Se incluye al final de luau_api_metodos.h: ve los ayudantes del puente
//  (push_vector3, wrap_node...) y las clases de GUI y de piezas.
//
//  · EditableImage y EditableMesh son Object de Roblox, no Instance: no
//    tienen Parent ni Name y se liberan con Destroy() o cuando nadie los usa.
//    Aqui son un userdata propio que sujeta el objeto de C++ (RefCounted).
//    typeof() da "Object". Hay UN userdata por objeto y VM (cache debil),
//    asi `img == otra` y las tablas por objeto funcionan como en Roblox.
//  · Content es una tabla del stdlib (luau_stdlib_editables.h); aqui solo se
//    lee y se crea.
//  · Las propiedades *Content (ImageContent, TextureContent...) van por
//    gl_index_content / gl_newindex_content. Un objeto puesto en una de ellas
//    se guarda en la meta _glc_<propiedad>, que lo sujeta (Content.fromObject
//    es una referencia fuerte en Roblox).
// ════════════════════════════════════════════════════════════════════

#include "gl_editables.h"
#include "gl_cast.h"

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include <new>
#include <cstdio>
#include <cstring>

inline constexpr int GL_ED_IMAGEN = 1;
inline constexpr int GL_ED_MALLA = 2;

// El primer campo es el de GodotObjectWrapper: el codigo del puente que lee
// un userdata como si fuera una instancia ve un objeto que no es un Node
// (gow_node da nullptr) y lo trata como nil.
struct GLEditableUD {
    uint64_t obj_id = 0;
    int tipo = 0;
    Ref<RefCounted> ref;
};
inline void gl_ed_dtor(void* p) { static_cast<GLEditableUD*>(p)->~GLEditableUD(); }
inline const char* gl_ed_mt(int tipo) { return tipo == GL_ED_IMAGEN ? "GLEditableImage" : "GLEditableMesh"; }

inline void gl_ed_push(lua_State* L, RefCounted* o, int tipo) {
    if (!o) { lua_pushnil(L); return; }
    lua_getfield(L, LUA_REGISTRYINDEX, "GL_EDITABLES");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_newtable(L);
        lua_pushstring(L, "v");
        lua_setfield(L, -2, "__mode");
        lua_setmetatable(L, -2);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "GL_EDITABLES");
    }
    lua_pushlightuserdata(L, o);
    lua_rawget(L, -2);
    if (lua_type(L, -1) == LUA_TUSERDATA) {
        GLEditableUD* ud = static_cast<GLEditableUD*>(lua_touserdata(L, -1));
        if (ud && ud->ref.ptr() == o) { lua_remove(L, -2); return; }
    }
    lua_pop(L, 1);
    GLEditableUD* ud = new (lua_newuserdatadtor(L, sizeof(GLEditableUD), gl_ed_dtor)) GLEditableUD();
    ud->obj_id = (uint64_t)o->get_instance_id();
    ud->tipo = tipo;
    ud->ref = Ref<RefCounted>(o);
    luaL_getmetatable(L, gl_ed_mt(tipo));
    lua_setmetatable(L, -2);
    lua_pushlightuserdata(L, o);
    lua_pushvalue(L, -2);
    lua_rawset(L, -4);
    lua_remove(L, -2);
}
// El userdata de un editable (tipo 0 = cualquiera), o nullptr.
inline GLEditableUD* gl_ed_ud(lua_State* L, int idx, int tipo) {
    if (lua_type(L, idx) != LUA_TUSERDATA || !lua_getmetatable(L, idx)) return nullptr;
    bool ok = false;
    for (int t = GL_ED_IMAGEN; t <= GL_ED_MALLA && !ok; t++) {
        if (tipo != 0 && tipo != t) continue;
        luaL_getmetatable(L, gl_ed_mt(t));
        ok = lua_rawequal(L, -1, -2) != 0;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return ok ? static_cast<GLEditableUD*>(lua_touserdata(L, idx)) : nullptr;
}

// ── Argumentos ────────────────────────────────────────────────────────
// El nombre del tipo para los errores: el de Roblox si lo tiene.
inline const char* gl_ed_tipo(lua_State* L, int idx) {
    const int t = lua_type(L, idx);
    if (t == LUA_TUSERDATA || t == LUA_TTABLE) {
        if (lua_getmetatable(L, idx)) {
            lua_rawgetfield(L, -1, "__type");
            const char* s = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
            lua_pop(L, 2);
            // __type vive en la metatabla, que sigue viva: el puntero vale.
            if (s) return s;
        }
        if (_gl_has_metatable(L, idx, "Vector3")) return "Vector3";
        if (_gl_has_metatable(L, idx, "Color3")) return "Color3";
        if (_gl_has_metatable(L, idx, "GodotObject")) return "Instance";
    }
    return lua_typename(L, t);
}
// Los argumentos se cuentan sin self, como en los errores de Roblox.
inline Vector2 gl_ed_vec2(lua_State* L, int idx, const char* fn) {
    if (lua_type(L, idx) == LUA_TTABLE) {
        lua_rawgetfield(L, idx, "X");
        lua_rawgetfield(L, idx, "Y");
        if (lua_isnumber(L, -2) && lua_isnumber(L, -1)) {
            const Vector2 v((float)lua_tonumber(L, -2), (float)lua_tonumber(L, -1));
            lua_pop(L, 2);
            return v;
        }
        lua_pop(L, 2);
    }
    luaL_error(L, "invalid argument #%d to '%s' (Vector2 expected, got %s)", idx - 1, fn, gl_ed_tipo(L, idx));
    return Vector2();
}
inline Vector3 gl_ed_vec3(lua_State* L, int idx, const char* fn) {
    if (LuauVector3* v = check_vector3(L, idx)) return Vector3(v->x, v->y, v->z);
    luaL_error(L, "invalid argument #%d to '%s' (Vector3 expected, got %s)", idx - 1, fn, gl_ed_tipo(L, idx));
    return Vector3();
}
inline Color gl_ed_color(lua_State* L, int idx, const char* fn) {
    if (LuauColor3* c = check_color3(L, idx)) return Color(c->r, c->g, c->b, 1.0f);
    luaL_error(L, "invalid argument #%d to '%s' (Color3 expected, got %s)", idx - 1, fn, gl_ed_tipo(L, idx));
    return Color();
}
inline float gl_ed_num(lua_State* L, int idx, const char* fn) {
    if (lua_type(L, idx) != LUA_TNUMBER)
        luaL_error(L, "invalid argument #%d to '%s' (number expected, got %s)", idx - 1, fn, gl_ed_tipo(L, idx));
    return (float)lua_tonumber(L, idx);
}
// Enum.ImageCombineType (EnumItem o numero); fuera de rango, el de por defecto.
inline int gl_ed_combinar(lua_State* L, int idx, int defecto) {
    const int v = (int)gl_num_or(L, idx, defecto);
    return (v >= GL_COMB_SOURCE_OVER && v <= GL_COMB_SUBTRACT) ? v : defecto;
}
// Un campo de una tabla de opciones en la cima (nil si no esta).
inline void gl_ed_campo(lua_State* L, int idx, const char* k) {
    if (lua_istable(L, idx)) lua_getfield(L, idx, k);
    else lua_pushnil(L);
}

// ════════════════════════════════════════════════════════════════════
//  EditableImage
// ════════════════════════════════════════════════════════════════════
inline GLEditableImage* gl_ei_de(lua_State* L, const char* fn, bool aunque_destruida = false) {
    GLEditableUD* ud = gl_ed_ud(L, 1, GL_ED_IMAGEN);
    if (!ud) luaL_error(L, "Expected ':' not '.' calling member function %s", fn);
    GLEditableImage* ei = gl_cast<GLEditableImage>(ud->ref.ptr());
    if (!ei || (ei->destruida && !aunque_destruida)) luaL_error(L, "%s: the EditableImage has been destroyed", fn);
    return ei;
}
// Otra EditableImage pasada como argumento (DrawImage...).
inline GLEditableImage* gl_ei_arg(lua_State* L, int idx, const char* fn) {
    GLEditableUD* ud = gl_ed_ud(L, idx, GL_ED_IMAGEN);
    if (!ud) luaL_error(L, "invalid argument #%d to '%s' (EditableImage expected, got %s)", idx - 1, fn, gl_ed_tipo(L, idx));
    GLEditableImage* ei = gl_cast<GLEditableImage>(ud->ref.ptr());
    if (!ei || ei->destruida) luaL_error(L, "%s: the source EditableImage has been destroyed", fn);
    return ei;
}
// Una region entera (posicion y tamano) que tiene que caber en la imagen.
inline void gl_ei_region(lua_State* L, GLEditableImage* ei, int ipos, const char* fn, int& x, int& y, int& w, int& h) {
    const Vector2 p = gl_ed_vec2(L, ipos, fn), s = gl_ed_vec2(L, ipos + 1, fn);
    x = (int)std::floor(p.x); y = (int)std::floor(p.y);
    w = (int)std::floor(s.x); h = (int)std::floor(s.y);
    if (w <= 0 || h <= 0) luaL_error(L, "%s: size must be greater than zero (got %d, %d)", fn, w, h);
    if (x < 0 || y < 0 || (int64_t)x + w > ei->ancho || (int64_t)y + h > ei->alto)
        luaL_error(L, "%s: the region (%d, %d) + (%d, %d) is outside the EditableImage (%d, %d)",
                   fn, x, y, w, h, ei->ancho, ei->alto);
}

inline int gl_ei_WritePixelsBuffer(lua_State* L) {
    GLEditableImage* ei = gl_ei_de(L, "WritePixelsBuffer");
    int x, y, w, h;
    gl_ei_region(L, ei, 2, "WritePixelsBuffer", x, y, w, h);
    size_t largo = 0;
    const void* datos = lua_tobuffer(L, 4, &largo);
    if (!datos) luaL_error(L, "invalid argument #3 to 'WritePixelsBuffer' (buffer expected, got %s)", gl_ed_tipo(L, 4));
    const size_t hace_falta = (size_t)w * (size_t)h * 4;
    if (largo < hace_falta)
        luaL_error(L, "WritePixelsBuffer: the buffer is too small (%d bytes for a %dx%d region, needs %d)",
                   (int)largo, w, h, (int)hace_falta);
    ei->escribir(x, y, w, h, static_cast<const uint8_t*>(datos));
    return 0;
}
inline int gl_ei_ReadPixelsBuffer(lua_State* L) {
    GLEditableImage* ei = gl_ei_de(L, "ReadPixelsBuffer");
    int x, y, w, h;
    gl_ei_region(L, ei, 2, "ReadPixelsBuffer", x, y, w, h);
    void* datos = lua_newbuffer(L, (size_t)w * (size_t)h * 4);
    ei->leer(x, y, w, h, static_cast<uint8_t*>(datos));
    return 1;
}
inline int gl_ei_DrawRectangle(lua_State* L) {
    GLEditableImage* ei = gl_ei_de(L, "DrawRectangle");
    const Vector2 p = gl_ed_vec2(L, 2, "DrawRectangle"), s = gl_ed_vec2(L, 3, "DrawRectangle");
    const Color c = gl_ed_color(L, 4, "DrawRectangle");
    const float t = gl_ed_num(L, 5, "DrawRectangle");
    const int modo = gl_ed_combinar(L, 6, GL_COMB_SOURCE_OVER);
    const int x = (int)std::floor(p.x), y = (int)std::floor(p.y);
    // La unica figura que no puede empezar fuera de la imagen.
    if (x < 0 || y < 0 || x >= ei->ancho || y >= ei->alto)
        luaL_error(L, "DrawRectangle: position (%d, %d) is outside the EditableImage (%d, %d)", x, y, ei->ancho, ei->alto);
    const int w = (int)std::floor(s.x), h = (int)std::floor(s.y);
    if (w > 0 && h > 0) ei->rectangulo(x, y, w, h, c, t, modo);
    return 0;
}
inline int gl_ei_DrawCircle(lua_State* L) {
    GLEditableImage* ei = gl_ei_de(L, "DrawCircle");
    const Vector2 cen = gl_ed_vec2(L, 2, "DrawCircle");
    const float r = std::floor(gl_ed_num(L, 3, "DrawCircle"));
    const Color c = gl_ed_color(L, 4, "DrawCircle");
    const float t = gl_ed_num(L, 5, "DrawCircle");
    const int modo = gl_ed_combinar(L, 6, GL_COMB_SOURCE_OVER);
    const bool suave = (int)gl_num_or(L, 7, 1) != 0;   // Enum.AntiAliasing, Enabled por defecto
    ei->circulo(cen.x, cen.y, r, c, t, modo, suave);
    return 0;
}
inline int gl_ei_DrawLine(lua_State* L) {
    GLEditableImage* ei = gl_ei_de(L, "DrawLine");
    const Vector2 a = gl_ed_vec2(L, 2, "DrawLine"), b = gl_ed_vec2(L, 3, "DrawLine");
    const Color c = gl_ed_color(L, 4, "DrawLine");
    const float t = gl_ed_num(L, 5, "DrawLine");
    const int modo = gl_ed_combinar(L, 6, GL_COMB_SOURCE_OVER);
    const bool suave = (int)gl_num_or(L, 7, 1) != 0;
    ei->linea(a.x, a.y, b.x, b.y, c, t, modo, suave);
    return 0;
}
inline int gl_ei_DrawTriangle(lua_State* L) {
    GLEditableImage* ei = gl_ei_de(L, "DrawTriangle");
    const Vector2 a = gl_ed_vec2(L, 2, "DrawTriangle"), b = gl_ed_vec2(L, 3, "DrawTriangle"), d = gl_ed_vec2(L, 4, "DrawTriangle");
    const Color c = gl_ed_color(L, 5, "DrawTriangle");
    const float t = gl_ed_num(L, 6, "DrawTriangle");
    ei->triangulo(a, b, d, c, t);
    return 0;
}
inline int gl_ei_DrawImage(lua_State* L) {
    GLEditableImage* ei = gl_ei_de(L, "DrawImage");
    const Vector2 p = gl_ed_vec2(L, 2, "DrawImage");
    GLEditableImage* src = gl_ei_arg(L, 3, "DrawImage");
    const int modo = gl_ed_combinar(L, 4, GL_COMB_SOURCE_OVER);
    ei->imagen((int)std::floor(p.x), (int)std::floor(p.y), src, modo);
    return 0;
}
inline int gl_ei_DrawImageTransformed(lua_State* L) {
    GLEditableImage* ei = gl_ei_de(L, "DrawImageTransformed");
    const Vector2 p = gl_ed_vec2(L, 2, "DrawImageTransformed");
    const Vector2 esc = gl_ed_vec2(L, 3, "DrawImageTransformed");
    const float rot = gl_ed_num(L, 4, "DrawImageTransformed");
    GLEditableImage* src = gl_ei_arg(L, 5, "DrawImageTransformed");
    int modo = GL_COMB_ALPHA_BLEND;
    bool pixelado = false;
    Vector2 pivote((float)src->ancho * 0.5f, (float)src->alto * 0.5f);
    if (lua_istable(L, 6)) {
        gl_ed_campo(L, 6, "CombineType");
        if (!lua_isnil(L, -1)) modo = gl_ed_combinar(L, lua_gettop(L), GL_COMB_ALPHA_BLEND);
        lua_pop(L, 1);
        gl_ed_campo(L, 6, "SamplingMode");
        if (!lua_isnil(L, -1)) pixelado = (int)gl_num_or(L, lua_gettop(L), 0) == 1;   // ResamplerMode.Pixelated
        lua_pop(L, 1);
        gl_ed_campo(L, 6, "PivotPoint");
        if (!lua_isnil(L, -1)) pivote = gl_ed_vec2(L, lua_gettop(L), "DrawImageTransformed");
        lua_pop(L, 1);
    } else if (!lua_isnoneornil(L, 6)) {
        luaL_error(L, "invalid argument #5 to 'DrawImageTransformed' (table expected, got %s)", gl_ed_tipo(L, 6));
    }
    ei->transformada(p, esc, rot, src, modo, pixelado, pivote);
    return 0;
}

// DrawImageProjected: proyecta la imagen del pincel (brushConfig.Decal) sobre
// la malla, como un proyector, y lo pinta en ESTA imagen a traves de los uv de
// la malla (esta imagen hace de textura de la malla). Cada triangulo se
// rasteriza en el espacio uv; cada pixel saca su punto 3D y se mira donde cae
// en el proyector. Las caras de espaldas al proyector no reciben nada y las
// muy inclinadas se desvanecen a partir de FadeAngle.
inline int gl_ei_DrawImageProjected(lua_State* L) {
    GLEditableImage* ei = gl_ei_de(L, "DrawImageProjected");
    GLEditableUD* mud = gl_ed_ud(L, 2, GL_ED_MALLA);
    if (!mud) luaL_error(L, "invalid argument #1 to 'DrawImageProjected' (EditableMesh expected, got %s)", gl_ed_tipo(L, 2));
    GLEditableMesh* em = gl_cast<GLEditableMesh>(mud->ref.ptr());
    if (!em || em->destruida) luaL_error(L, "DrawImageProjected: the EditableMesh has been destroyed");
    if (!lua_istable(L, 3)) luaL_error(L, "invalid argument #2 to 'DrawImageProjected' (table expected, got %s)", gl_ed_tipo(L, 3));
    if (!lua_istable(L, 4)) luaL_error(L, "invalid argument #3 to 'DrawImageProjected' (table expected, got %s)", gl_ed_tipo(L, 4));
    auto v3 = [&](const char* k) {
        lua_getfield(L, 3, k);
        const Vector3 v = gl_ed_vec3(L, lua_gettop(L), "DrawImageProjected");
        lua_pop(L, 1);
        return v;
    };
    const Vector3 dir = v3("Direction").normalized(), origen = v3("Position"), tam = v3("Size");
    Vector3 arriba = v3("Up");
    arriba = (arriba - dir * arriba.dot(dir)).normalized();
    if (dir.length_squared() < 1e-12f || arriba.length_squared() < 1e-12f || tam.x <= 0.0f || tam.y <= 0.0f)
        luaL_error(L, "DrawImageProjected: invalid projection (Direction, Up and Size must be valid)");
    const Vector3 derecha = dir.cross(arriba);
    lua_getfield(L, 4, "Decal");
    GLEditableImage* pincel = gl_ei_arg(L, lua_gettop(L), "DrawImageProjected");
    lua_pop(L, 1);
    gl_ed_campo(L, 4, "ColorBlendType");
    const int modo = lua_isnil(L, -1) ? GL_COMB_ALPHA_BLEND : gl_ed_combinar(L, lua_gettop(L), GL_COMB_ALPHA_BLEND);
    lua_pop(L, 1);
    gl_ed_campo(L, 4, "AlphaBlendType");
    const int alfa_modo = (int)gl_num_or(L, lua_gettop(L), 1);   // Enum.ImageAlphaType
    lua_pop(L, 1);
    gl_ed_campo(L, 4, "FadeAngle");
    const float fade = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 90.0f;
    lua_pop(L, 1);
    gl_ed_campo(L, 4, "BlendIntensity");
    const float intensidad = lua_isnumber(L, -1) ? std::clamp((float)lua_tonumber(L, -1), 0.0f, 1.0f) : 1.0f;
    lua_pop(L, 1);

    const int W = ei->ancho, H = ei->alto, pw = pincel->ancho, ph = pincel->alto;
    const float k = 1.0f / 255.0f;
    for (const GLEMCara& cf : em->caras) {
        if (!cf.vivo) continue;
        Vector3 n = em->normal_de_cara(cf);
        if (n.length_squared() < 1e-20f) continue;
        n = n.normalized();
        const float cosang = -n.dot(dir);
        if (cosang <= 0.0f) continue;   // de espaldas al proyector
        const float ang = std::acos(std::min(cosang, 1.0f)) * 57.29577951308232f;
        float peso = 1.0f;
        if (fade < 90.0f && ang > fade) peso = std::max(0.0f, (90.0f - ang) / (90.0f - fade));
        if (peso <= 0.0f) continue;
        int t[2][3];
        const int nt = em->triangulos_de(cf, t);
        for (int q = 0; q < nt; q++) {
            Vector2 uv[3];
            Vector3 pos[3];
            for (int e = 0; e < 3; e++) {
                const Vector2 u = em->uvs[cf.uv[t[q][e]]].uv;
                uv[e] = Vector2(u.x * (float)W, u.y * (float)H);
                pos[e] = em->verts[cf.v[t[q][e]]].p;
            }
            const float area = (uv[1].x - uv[0].x) * (uv[2].y - uv[0].y) - (uv[1].y - uv[0].y) * (uv[2].x - uv[0].x);
            if (std::fabs(area) < 1e-9f) continue;
            const int x0 = std::max((int)std::floor(std::min({ uv[0].x, uv[1].x, uv[2].x })), 0);
            const int x1 = std::min((int)std::ceil(std::max({ uv[0].x, uv[1].x, uv[2].x })), W - 1);
            const int y0 = std::max((int)std::floor(std::min({ uv[0].y, uv[1].y, uv[2].y })), 0);
            const int y1 = std::min((int)std::ceil(std::max({ uv[0].y, uv[1].y, uv[2].y })), H - 1);
            for (int j = y0; j <= y1; j++) {
                for (int i = x0; i <= x1; i++) {
                    const float px = (float)i + 0.5f, py = (float)j + 0.5f;
                    const float w0 = ((uv[1].x - px) * (uv[2].y - py) - (uv[1].y - py) * (uv[2].x - px)) / area;
                    const float w1 = ((uv[2].x - px) * (uv[0].y - py) - (uv[2].y - py) * (uv[0].x - px)) / area;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                    const Vector3 punto = pos[0] * w0 + pos[1] * w1 + pos[2] * w2;
                    const Vector3 rel = punto - origen;
                    const float lx = rel.dot(derecha), ly = rel.dot(arriba), lz = rel.dot(dir);
                    if (tam.z > 0.0f && (lz < 0.0f || lz > tam.z)) continue;
                    const float du = lx / tam.x + 0.5f, dv = 0.5f - ly / tam.y;
                    if (du < 0.0f || dv < 0.0f || du >= 1.0f || dv >= 1.0f) continue;
                    const uint8_t* s = pincel->px.data() + ((size_t)std::min((int)(dv * ph), ph - 1) * (size_t)pw +
                                                            (size_t)std::min((int)(du * pw), pw - 1)) * 4;
                    uint8_t* d = ei->pixel(i, j);
                    const uint8_t antes[4] = { d[0], d[1], d[2], d[3] };
                    gl_ei_combinar(d, s[0] * k, s[1] * k, s[2] * k, s[3] * k * intensidad * peso, modo);
                    if (alfa_modo == 2) d[3] = antes[3];                            // LockCanvasAlpha
                    else if (alfa_modo == 3) { d[0] = antes[0]; d[1] = antes[1]; d[2] = antes[2]; }   // LockCanvasColor
                }
            }
        }
    }
    ei->marcar();
    return 0;
}
inline int gl_ei_Destroy(lua_State* L) {
    gl_ei_de(L, "Destroy", true)->destruir();
    return 0;
}
inline int gl_ei_IsA(lua_State* L) {
    gl_ei_de(L, "IsA", true);
    const char* c = luaL_checkstring(L, 2);
    lua_pushboolean(L, strcmp(c, "EditableImage") == 0 || strcmp(c, "Object") == 0);
    return 1;
}

inline constexpr const char* kGLEINombres[] = {
    "WritePixelsBuffer", "ReadPixelsBuffer", "DrawRectangle", "DrawCircle", "DrawLine",
    "DrawTriangle", "DrawImage", "DrawImageTransformed", "DrawImageProjected", "Destroy", "IsA", "isA",
};
inline constexpr lua_CFunction kGLEIFunciones[] = {
    gl_ei_WritePixelsBuffer, gl_ei_ReadPixelsBuffer, gl_ei_DrawRectangle, gl_ei_DrawCircle, gl_ei_DrawLine,
    gl_ei_DrawTriangle, gl_ei_DrawImage, gl_ei_DrawImageTransformed, gl_ei_DrawImageProjected, gl_ei_Destroy,
    gl_ei_IsA, gl_ei_IsA,
};
static_assert(sizeof(kGLEINombres) / sizeof(kGLEINombres[0]) == sizeof(kGLEIFunciones) / sizeof(kGLEIFunciones[0]),
              "kGLEINombres y kGLEIFunciones deben ir a la par");

#include "gl_puente_editable_malla.h"   // los metodos de EditableMesh

// ── Metatablas ────────────────────────────────────────────────────────
// __index: primero los metodos (tabla en el upvalue 1, una busqueda), luego
// las propiedades. Lo demas es el error de miembro de Roblox.
inline int gl_ed_index(lua_State* L) {
    const char* key = lua_tostring(L, 2);
    if (key) {
        lua_pushvalue(L, 2);
        lua_rawget(L, lua_upvalueindex(1));
        if (!lua_isnil(L, -1)) return 1;
        lua_pop(L, 1);
    }
    GLEditableUD* ud = static_cast<GLEditableUD*>(lua_touserdata(L, 1));
    const int tipo = ud ? ud->tipo : 0;
    const char* clase = tipo == GL_ED_IMAGEN ? "EditableImage" : "EditableMesh";
    if (!key) luaL_error(L, "invalid key to '%s'", clase);
    if (strcmp(key, "ClassName") == 0 || strcmp(key, "className") == 0) { lua_pushstring(L, clase); return 1; }
    if (strcmp(key, "Changed") == 0) { _gl_push_inert_signal(L); return 1; }
    if (strcmp(key, "GetPropertyChangedSignal") == 0) {
        lua_pushcfunction(L, [](lua_State* pL) -> int { _gl_push_inert_signal(pL); return 1; }, "GetPropertyChangedSignal");
        return 1;
    }
    if (tipo == GL_ED_IMAGEN && strcmp(key, "Size") == 0) {
        GLEditableImage* ei = gl_cast<GLEditableImage>(ud->ref.ptr());
        push_vector2(L, ei ? (float)ei->ancho : 0.0f, ei ? (float)ei->alto : 0.0f);
        return 1;
    }
    if (tipo == GL_ED_MALLA && strcmp(key, "FixedSize") == 0) {
        GLEditableMesh* em = gl_cast<GLEditableMesh>(ud->ref.ptr());
        lua_pushboolean(L, em && em->fija);
        return 1;
    }
    luaL_error(L, "%s is not a valid member of %s", key, clase);
    return 0;
}
inline int gl_ed_newindex(lua_State* L) {
    GLEditableUD* ud = static_cast<GLEditableUD*>(lua_touserdata(L, 1));
    const char* clase = ud && ud->tipo == GL_ED_IMAGEN ? "EditableImage" : "EditableMesh";
    const char* key = lua_tostring(L, 2);
    if (!key) luaL_error(L, "invalid key to '%s'", clase);
    if (strcmp(key, "Size") == 0 || strcmp(key, "FixedSize") == 0 || strcmp(key, "ClassName") == 0)
        luaL_error(L, "Unable to assign property %s. Property is read only", key);
    luaL_error(L, "%s is not a valid member of %s", key, clase);
    return 0;
}
inline int gl_ed_tostring(lua_State* L) {
    GLEditableUD* ud = static_cast<GLEditableUD*>(lua_touserdata(L, 1));
    lua_pushstring(L, ud && ud->tipo == GL_ED_IMAGEN ? "EditableImage" : "EditableMesh");
    return 1;
}
inline void gl_ed_metatabla(lua_State* L, int tipo) {
    luaL_newmetatable(L, gl_ed_mt(tipo));
    lua_newtable(L);
    if (tipo == GL_ED_IMAGEN) {
        for (size_t i = 0; i < sizeof(kGLEINombres) / sizeof(kGLEINombres[0]); i++) {
            lua_pushcfunction(L, kGLEIFunciones[i], kGLEINombres[i]);
            lua_setfield(L, -2, kGLEINombres[i]);
        }
    } else {
        for (size_t i = 0; i < sizeof(kGLEMNombres) / sizeof(kGLEMNombres[0]); i++) {
            lua_pushcfunction(L, kGLEMFunciones[i], kGLEMNombres[i]);
            lua_setfield(L, -2, kGLEMNombres[i]);
        }
    }
    lua_pushcclosure(L, gl_ed_index, "__index", 1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, gl_ed_newindex, "__newindex");
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, gl_ed_tostring, "__tostring");
    lua_setfield(L, -2, "__tostring");
    lua_pushstring(L, "Object");
    lua_setfield(L, -2, "__type");
    lua_pop(L, 1);
}

// ════════════════════════════════════════════════════════════════════
//  Content
// ════════════════════════════════════════════════════════════════════
struct GLContenido {
    int tipo = 0;            // Enum.ContentSourceType: 0 None, 1 Uri, 2 Object
    String uri;
    Ref<RefCounted> obj;     // si el Object es un editable
    int obj_tipo = 0;
};
// false si en idx no hay un Content.
inline bool gl_contenido_leer(lua_State* L, int idx, GLContenido& c) {
    if (lua_type(L, idx) != LUA_TTABLE || gl_lua_tipo_tabla(L, idx) != "Content") return false;
    lua_rawgetfield(L, idx, "_t");
    c.tipo = (int)lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (c.tipo == 1) {
        lua_rawgetfield(L, idx, "_u");
        const char* s = lua_tostring(L, -1);
        c.uri = String::utf8(s ? s : "");
        lua_pop(L, 1);
    } else if (c.tipo == 2) {
        lua_rawgetfield(L, idx, "_o");
        if (GLEditableUD* ud = gl_ed_ud(L, lua_gettop(L), 0)) { c.obj = ud->ref; c.obj_tipo = ud->tipo; }
        lua_pop(L, 1);
    }
    return true;
}
inline void gl_contenido_push_none(lua_State* L) {
    lua_getglobal(L, "Content");
    if (lua_istable(L, -1)) {
        lua_rawgetfield(L, -1, "none");
        lua_remove(L, -2);
        return;
    }
    lua_pop(L, 1);
    lua_pushnil(L);
}
inline void gl_contenido_push_uri(lua_State* L, const String& uri) {
    if (uri.is_empty()) { gl_contenido_push_none(L); return; }
    if (!gl_push_constructor(L, "Content", "fromUri")) { lua_pushnil(L); return; }
    lua_pushstring(L, uri.utf8().get_data());
    lua_call(L, 1, 1);
}
inline void gl_contenido_push_obj(lua_State* L, RefCounted* o, int tipo) {
    if (!gl_push_constructor(L, "Content", "fromObject")) { lua_pushnil(L); return; }
    gl_ed_push(L, o, tipo);
    lua_call(L, 1, 1);
}

// ════════════════════════════════════════════════════════════════════
//  AssetService (la tabla del servicio esta en luau_stdlib_editables.h y
//  llama a estas funciones)
// ════════════════════════════════════════════════════════════════════
inline void gl_as_self(lua_State* L, const char* fn) {
    bool ok = false;
    if (lua_istable(L, 1)) {
        lua_rawgetfield(L, 1, "ClassName");
        ok = lua_isstring(L, -1) && strcmp(lua_tostring(L, -1), "AssetService") == 0;
        lua_pop(L, 1);
    }
    if (!ok) luaL_error(L, "Expected ':' not '.' calling member function %s", fn);
}
// Content obligatorio (argumento 1 tras self).
inline GLContenido gl_as_contenido(lua_State* L, int idx, const char* fn) {
    GLContenido c;
    if (!gl_contenido_leer(L, idx, c))
        luaL_error(L, "invalid argument #%d to '%s' (Content expected, got %s)", idx - 1, fn, gl_ed_tipo(L, idx));
    if (c.tipo == 0) luaL_error(L, "%s: the Content is empty (Content.none)", fn);
    return c;
}

inline int gl_as_CreateEditableImage(lua_State* L) {
    gl_as_self(L, "CreateEditableImage");
    int w = GL_EI_LADO_DEFECTO, h = GL_EI_LADO_DEFECTO;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "Size");
        if (!lua_isnil(L, -1)) {
            const Vector2 s = gl_ed_vec2(L, lua_gettop(L), "CreateEditableImage");
            w = (int)std::floor(s.x);
            h = (int)std::floor(s.y);
        }
        lua_pop(L, 1);
    } else if (!lua_isnoneornil(L, 2)) {
        luaL_error(L, "invalid argument #1 to 'CreateEditableImage' (table expected, got %s)", gl_ed_tipo(L, 2));
    }
    if (w < 1 || h < 1 || w > GL_EI_LADO_MAX || h > GL_EI_LADO_MAX)
        luaL_error(L, "CreateEditableImage: Size must be between (1, 1) and (%d, %d), got (%d, %d)",
                   GL_EI_LADO_MAX, GL_EI_LADO_MAX, w, h);
    Ref<GLEditableImage> ei;
    ei.instantiate();
    ei->crear(w, h);
    gl_ed_push(L, ei.ptr(), GL_ED_IMAGEN);
    return 1;
}

// La imagen de un uri: rbxassetid:// (copia local, cache o assets/rbx), o una
// ruta res:// / user://. Vacia si no esta en el disco.
inline Ref<Image> gl_as_cargar_imagen(const String& uri) {
    String ruta;
    if (gl_is_roblox_asset(uri)) {
        ruta = gl_local_asset_path(uri);
        if (ruta.is_empty()) ruta = gl_local_asset_file(uri);
        if (ruta.is_empty()) ruta = gl_cache_asset_path(uri);
    } else if (uri.begins_with("res://") || uri.begins_with("user://")) {
        ruta = uri;
    }
    if (ruta.is_empty()) return Ref<Image>();
    ResourceLoader* rl = ResourceLoader::get_singleton();
    if (rl && rl->exists(ruta, "Texture2D")) {
        Ref<Texture2D> t = rl->load(ruta, "Texture2D");
        if (t.is_valid()) {
            Ref<Image> im = t->get_image();
            if (im.is_valid() && !im->is_empty()) return im;
        }
    }
    if (FileAccess::file_exists(ruta)) {
        Ref<Image> im;
        im.instantiate();
        if (im->load(ruta) == OK && !im->is_empty()) return im;
    }
    return Ref<Image>();
}
inline int gl_as_CreateEditableImageAsync(lua_State* L) {
    gl_as_self(L, "CreateEditableImageAsync");
    const GLContenido c = gl_as_contenido(L, 2, "CreateEditableImageAsync");
    Ref<GLEditableImage> ei;
    ei.instantiate();
    if (c.tipo == 2) {
        GLEditableImage* src = gl_cast<GLEditableImage>(c.obj.ptr());
        if (!src) luaL_error(L, "CreateEditableImageAsync: the Content does not reference an EditableImage");
        if (src->destruida) luaL_error(L, "CreateEditableImageAsync: the source EditableImage has been destroyed");
        ei->crear(src->ancho, src->alto);
        ei->px = src->px;
    } else {
        Ref<Image> im = gl_as_cargar_imagen(c.uri);
        if (im.is_null()) luaL_error(L, "CreateEditableImageAsync: failed to load image '%s'", c.uri.utf8().get_data());
        // Como Roblox: nada pasa de 1024 por lado (se reduce sin deformar).
        Ref<Image> copia = im->duplicate();
        if (copia->is_compressed()) copia->decompress();
        const int mx = std::max(copia->get_width(), copia->get_height());
        if (mx > GL_EI_LADO_MAX) {
            const float f = (float)GL_EI_LADO_MAX / (float)mx;
            copia->resize(std::max(1, (int)(copia->get_width() * f)), std::max(1, (int)(copia->get_height() * f)),
                          Image::INTERPOLATE_BILINEAR);
        }
        ei->crear_desde(copia);
    }
    gl_ed_push(L, ei.ptr(), GL_ED_IMAGEN);
    return 1;
}

inline int gl_as_CreateEditableMesh(lua_State* L) {
    gl_as_self(L, "CreateEditableMesh");
    if (!lua_isnoneornil(L, 2) && !lua_istable(L, 2))
        luaL_error(L, "invalid argument #1 to 'CreateEditableMesh' (table expected, got %s)", gl_ed_tipo(L, 2));
    Ref<GLEditableMesh> em;
    em.instantiate();
    gl_ed_push(L, em.ptr(), GL_ED_MALLA);
    return 1;
}
inline int gl_as_CreateEditableMeshAsync(lua_State* L) {
    gl_as_self(L, "CreateEditableMeshAsync");
    const GLContenido c = gl_as_contenido(L, 2, "CreateEditableMeshAsync");
    bool fija = true;   // FixedSize: true por defecto
    gl_ed_campo(L, 3, "FixedSize");
    if (lua_isboolean(L, -1)) fija = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    Ref<GLEditableMesh> em;
    em.instantiate();
    if (c.tipo == 2) {
        GLEditableMesh* src = gl_cast<GLEditableMesh>(c.obj.ptr());
        if (!src) luaL_error(L, "CreateEditableMeshAsync: the Content does not reference an EditableMesh");
        if (src->destruida) luaL_error(L, "CreateEditableMeshAsync: the source EditableMesh has been destroyed");
        em->copiar_de(*src);
    } else {
        const Ref<Mesh> m = gl_malla_local(c.uri);
        if (m.is_null()) luaL_error(L, "CreateEditableMeshAsync: failed to load mesh '%s'", c.uri.utf8().get_data());
        if (!em->cargar_de(m))
            luaL_error(L, "CreateEditableMeshAsync: the mesh '%s' is over the EditableMesh limits (%d vertices, %d triangles)",
                       c.uri.utf8().get_data(), GL_EM_MAX_VERTICES, GL_EM_MAX_TRIANGULOS);
    }
    em->fija = fija;
    gl_ed_push(L, em.ptr(), GL_ED_MALLA);
    return 1;
}

// Enlaza una pieza con una EditableMesh: su vista, el MeshSize de ahora y
// la malla sin recentrar (gl_malla_propia.h, meta _gl_malla_ref): en Roblox
// las coordenadas de la malla son las de la pieza, escaladas por
// Size / MeshSize.
inline void gl_mp_enlazar(RobloxPart* p, GLEditableMesh* em, const Ref<ArrayMesh>& vista, const Vector3& tam_malla,
                          int fidelidad) {
    RBXMeshPart* mp = gl_cast<RBXMeshPart>(p);
    if (mp && !mp->get_mesh_id().is_empty()) mp->set_mesh_id(String());
    static const StringName k_ref("_gl_malla_ref");
    static const StringName k_obj("_glc_MeshContent");
    p->set_meta(k_ref, tam_malla);
    p->set_meta(k_obj, Ref<RefCounted>(em));
    if (mp) mp->set_mesh_size(tam_malla);
    else p->set_meta("MeshSize", tam_malla);
    p->gl_malla_propia(Ref<Mesh>(), true);   // fuerza a rehacer forma y escala
    p->gl_malla_propia(vista, true, fidelidad == 2);
    p->gl_malla_con_colores(em->tiene_colores);
}

inline int gl_as_CreateMeshPartAsync(lua_State* L) {
    gl_as_self(L, "CreateMeshPartAsync");
    const GLContenido c = gl_as_contenido(L, 2, "CreateMeshPartAsync");
    int colision = 0, render = 0, fluido = 0;
    if (lua_istable(L, 3)) {
        gl_ed_campo(L, 3, "CollisionFidelity"); colision = (int)gl_num_or(L, lua_gettop(L), 0); lua_pop(L, 1);
        gl_ed_campo(L, 3, "RenderFidelity");    render = (int)gl_num_or(L, lua_gettop(L), 0);    lua_pop(L, 1);
        gl_ed_campo(L, 3, "FluidFidelity");     fluido = (int)gl_num_or(L, lua_gettop(L), 0);    lua_pop(L, 1);
    } else if (!lua_isnoneornil(L, 3)) {
        luaL_error(L, "invalid argument #2 to 'CreateMeshPartAsync' (table expected, got %s)", gl_ed_tipo(L, 3));
    }
    GLEditableMesh* em = nullptr;
    if (c.tipo == 2) {
        em = gl_cast<GLEditableMesh>(c.obj.ptr());
        if (!em) luaL_error(L, "CreateMeshPartAsync: the Content does not reference an EditableMesh");
        if (em->destruida) luaL_error(L, "CreateMeshPartAsync: the EditableMesh has been destroyed");
        if (em->n_tris <= 0) luaL_error(L, "CreateMeshPartAsync: the EditableMesh has no faces");
    }
    RBXMeshPart* mp = memnew(RBXMeshPart);
    mp->set_name("MeshPart");
    mp->set_anchored(false);   // como cualquier pieza nueva de Roblox
    mp->set_collision_fidelity(colision);
    mp->set_render_fidelity(render);
    mp->set_meta("_glp_FluidFidelity", fluido);
    if (em) {
        const Ref<ArrayMesh> vista = em->nueva_vista();
        const Vector3 tam = vista->get_aabb().size;
        mp->set_size(Vector3(std::max(tam.x, 0.001f), std::max(tam.y, 0.001f), std::max(tam.z, 0.001f)));
        gl_mp_enlazar(mp, em, vista, tam, colision);
    } else {
        mp->set_mesh_id(c.uri);
        const Ref<Mesh> m = mp->gl_malla_propia_actual();
        const Vector3 tam = m.is_valid() ? m->get_aabb().size : Vector3(1, 1, 1);
        mp->set_mesh_size(tam);
        mp->set_size(Vector3(std::max(tam.x, 0.001f), std::max(tam.y, 0.001f), std::max(tam.z, 0.001f)));
    }
    wrap_node(L, mp);
    gl_track_orphan(mp);   // sin padre: lo libera el cierre si nadie lo cuelga
    return 1;
}

// ════════════════════════════════════════════════════════════════════
//  Propiedades *Content de las instancias
// ════════════════════════════════════════════════════════════════════
inline bool gl_clave_content(const char* key) {
    const size_t n = strlen(key);
    return n > 7 && key[n - 7] == 'C' && memcmp(key + n - 7, "Content", 7) == 0;
}
inline bool gl_es_meshpart(Node* n) { return strcmp(gl_roblox_classname(n), "MeshPart") == 0; }

// La propiedad de texto que va con cada *Content ("" si la clase no la tiene).
inline String gl_content_base(Node* n, const char* key) {
    const char* rc = gl_roblox_classname(n);
    auto es = [&](const char* c) { return strcmp(rc, c) == 0; };
    if (strcmp(key, "ImageContent") == 0) return (es("ImageLabel") || es("ImageButton")) ? "Image" : "";
    if (strcmp(key, "HoverImageContent") == 0) return es("ImageButton") ? "HoverImage" : "";
    if (strcmp(key, "PressedImageContent") == 0) return es("ImageButton") ? "PressedImage" : "";
    if (strcmp(key, "MeshContent") == 0) return es("MeshPart") ? "MeshId" : "";
    const bool calco = es("Decal") || es("Texture");
    if (strcmp(key, "TextureContent") == 0) {
        if (es("MeshPart")) return "TextureID";
        if (calco) return "Texture";
        if (es("Tool") || es("HopperBin") || es("BackpackItem")) return "TextureId";
        return "";
    }
    const bool superficie = calco || es("SurfaceAppearance") || es("MaterialVariant") || es("TerrainDetail");
    if (superficie) {
        if (strcmp(key, "ColorMapContent") == 0) return "ColorMap";
        if (strcmp(key, "NormalMapContent") == 0) return "NormalMap";
        if (strcmp(key, "MetalnessMapContent") == 0) return "MetalnessMap";
        if (strcmp(key, "RoughnessMapContent") == 0) return "RoughnessMap";
        if (strcmp(key, "EmissiveMaskContent") == 0 && !calco) return "EmissiveMaskContent";
    }
    if (es("Sky")) {
        if (strcmp(key, "SkyboxBackContent") == 0) return "SkyboxBk";
        if (strcmp(key, "SkyboxDownContent") == 0) return "SkyboxDn";
        if (strcmp(key, "SkyboxFrontContent") == 0) return "SkyboxFt";
        if (strcmp(key, "SkyboxLeftContent") == 0) return "SkyboxLf";
        if (strcmp(key, "SkyboxRightContent") == 0) return "SkyboxRt";
        if (strcmp(key, "SkyboxUpContent") == 0) return "SkyboxUp";
        if (strcmp(key, "SunTextureContent") == 0) return "SunTextureId";
        if (strcmp(key, "MoonTextureContent") == 0) return "MoonTextureId";
    }
    if (es("Mouse") && strcmp(key, "IconContent") == 0) return "Icon";
    return "";
}
inline String gl_content_leer_texto(Node* n, const String& base) {
    if (base == "TextureID") {
        static const StringName k_prop("TextureId");
        static const StringName k_meta("_glp_TextureId");
        if (gl_has_property(n, "TextureId")) return n->get(k_prop);
        return n->has_meta(k_meta) ? String(n->get_meta(k_meta)) : String();
    }
    if (gl_has_property(n, base)) {
        const Variant v = n->get(StringName(base));
        return v.get_type() == Variant::STRING ? String(v) : String();
    }
    const StringName m1(base), m2(String("_glp_") + base);
    if (n->has_meta(m1)) return n->get_meta(m1);
    if (n->has_meta(m2)) return n->get_meta(m2);
    return String();
}
inline void gl_content_escribir_texto(Node* n, const String& base, const String& s) {
    if (base == "Image") {
        if (RobloxImageLabel* il = gl_cast<RobloxImageLabel>(n)) { il->set_image(s); return; }
        if (RobloxImageButton* ib = gl_cast<RobloxImageButton>(n)) { ib->set_image(s); return; }
    }
    if (base == "TextureID") {
        if (gl_has_property(n, "TextureId")) n->set(StringName("TextureId"), s);
        else n->set_meta(StringName("_glp_TextureId"), s);
        return;
    }
    if (gl_has_property(n, base)) n->set(StringName(base), s);
    else n->set_meta(StringName(base), s);
}
// La textura de una EditableImage en lo que la pinta (nullptr: quitarla).
inline void gl_content_poner_textura(Node* n, const String& base, GLEditableImage* ei) {
    const Ref<Texture2D> t = ei ? ei->gl_textura() : Ref<Texture2D>();
    if (base == "Image") {
        if (!ei) return;   // el texto ya lo dejo puesto set_image
        if (RobloxImageLabel* il = gl_cast<RobloxImageLabel>(n)) il->gl_set_textura_objeto(t);
        else if (RobloxImageButton* ib = gl_cast<RobloxImageButton>(n)) ib->gl_set_textura_objeto(t);
        return;
    }
    if (base == "TextureID") {
        if (RobloxPart* p = gl_cast<RobloxPart>(n)) if (ei) p->gl_textura_de_malla(t);
        return;
    }
    if (base == "Texture" && n->has_method("gl_set_textura_objeto")) n->call("gl_set_textura_objeto", t);
}

inline int gl_mp_ApplyMesh(lua_State* L);

// Lectura: el objeto si lo hay, si no el texto como Content.fromUri.
// -1 si la clave no es de esta clase (sigue el error normal).
inline int gl_index_content(lua_State* L, Node* n, const char* key) {
    if (key[0] == 'A') {
        if (strcmp(key, "ApplyMesh") != 0 || !gl_es_meshpart(n)) return -1;
        lua_pushcfunction(L, gl_mp_ApplyMesh, "ApplyMesh");
        return 1;
    }
    const String base = gl_content_base(n, key);
    if (base.is_empty()) return -1;
    const StringName mk(String("_glc_") + key);
    if (n->has_meta(mk)) {
        Object* o = n->get_meta(mk);
        if (GLEditableImage* ei = gl_cast<GLEditableImage>(o)) { gl_contenido_push_obj(L, ei, GL_ED_IMAGEN); return 1; }
        if (GLEditableMesh* em = gl_cast<GLEditableMesh>(o)) { gl_contenido_push_obj(L, em, GL_ED_MALLA); return 1; }
    }
    gl_contenido_push_uri(L, gl_content_leer_texto(n, base));
    return 1;
}
// Escritura. false si la clave no es de esta clase.
inline bool gl_newindex_content(lua_State* L, Node* n, const char* key) {
    const String base = gl_content_base(n, key);
    if (base.is_empty()) return false;
    // Como en Roblox: la malla y su colision solo cambian juntas
    // (CreateMeshPartAsync / ApplyMesh).
    if (base == "MeshId") luaL_error(L, "Unable to assign property MeshContent. Script write access is restricted");
    GLContenido c;
    if (!gl_contenido_leer(L, 3, c))
        luaL_error(L, "Unable to assign property %s. Content expected, got %s", key, gl_ed_tipo(L, 3));
    const StringName mk(String("_glc_") + key);
    if (c.tipo == 2) {
        GLEditableImage* ei = gl_cast<GLEditableImage>(c.obj.ptr());
        if (!ei) luaL_error(L, "Unable to assign property %s. The Content must reference an EditableImage", key);
        gl_content_escribir_texto(n, base, String());   // quita lo que hubiera (y su meta)
        n->set_meta(mk, c.obj);
        gl_content_poner_textura(n, base, ei);
    } else {
        if (n->has_meta(mk)) n->remove_meta(mk);
        gl_content_poner_textura(n, base, nullptr);
        gl_content_escribir_texto(n, base, c.uri);
    }
    return true;
}

// MeshPart:ApplyMesh(otra): copia la malla (con su colision), la textura,
// las fidelidades y MeshSize de otra MeshPart.
inline int gl_mp_ApplyMesh(lua_State* L) {
    GodotObjectWrapper* w = _gl_has_metatable(L, 1, "GodotObject") ? (GodotObjectWrapper*)lua_touserdata(L, 1) : nullptr;
    RobloxPart* dst = w ? gl_cast<RobloxPart>(gow_node(w)) : nullptr;
    if (!dst || !gl_es_meshpart(dst)) luaL_error(L, "Expected ':' not '.' calling member function ApplyMesh");
    GodotObjectWrapper* w2 = _gl_has_metatable(L, 2, "GodotObject") ? (GodotObjectWrapper*)lua_touserdata(L, 2) : nullptr;
    RobloxPart* src = w2 ? gl_cast<RobloxPart>(gow_node(w2)) : nullptr;
    if (!src || !gl_es_meshpart(src)) luaL_error(L, "invalid argument #1 to 'ApplyMesh' (MeshPart expected, got %s)", gl_ed_tipo(L, 2));
    if (src == dst) return 0;
    RBXMeshPart* mdst = gl_cast<RBXMeshPart>(dst);
    RBXMeshPart* msrc = gl_cast<RBXMeshPart>(src);
    const int colision = msrc ? msrc->get_collision_fidelity() : 0;
    if (mdst) {
        mdst->set_collision_fidelity(colision);
        if (msrc) mdst->set_render_fidelity(msrc->get_render_fidelity());
    }
    static const StringName k_ff("_glp_FluidFidelity");
    if (src->has_meta(k_ff)) dst->set_meta(k_ff, src->get_meta(k_ff));
    // La malla.
    static const StringName k_obj("_glc_MeshContent");
    static const StringName k_ref("_gl_malla_ref");
    GLEditableMesh* em = src->has_meta(k_obj) ? gl_cast<GLEditableMesh>((Object*)src->get_meta(k_obj)) : nullptr;
    const Ref<Mesh> vista_src = src->gl_malla_propia_actual();
    const Ref<ArrayMesh> vista = vista_src;
    if (em && vista.is_valid()) {
        const Vector3 tam = src->has_meta(k_ref) ? (Vector3)src->get_meta(k_ref) : vista->get_aabb().size;
        gl_mp_enlazar(dst, em, vista, tam, colision);
    } else {
        if (dst->has_meta(k_obj)) dst->remove_meta(k_obj);
        if (dst->has_meta(k_ref)) dst->remove_meta(k_ref);
        const String id = msrc ? msrc->get_mesh_id() : String();
        if (mdst) {
            mdst->set_mesh_id(id);
            if (msrc) mdst->set_mesh_size(msrc->get_mesh_size());
        }
    }
    // La textura.
    static const StringName k_tex("_glc_TextureContent");
    if (src->has_meta(k_tex)) {
        GLEditableImage* ei = gl_cast<GLEditableImage>((Object*)src->get_meta(k_tex));
        gl_content_escribir_texto(dst, "TextureID", String());
        if (ei) {
            dst->set_meta(k_tex, src->get_meta(k_tex));
            gl_content_poner_textura(dst, "TextureID", ei);
        }
    } else {
        if (dst->has_meta(k_tex)) dst->remove_meta(k_tex);
        gl_content_escribir_texto(dst, "TextureID", gl_content_leer_texto(src, "TextureID"));
    }
    return 0;
}

// ════════════════════════════════════════════════════════════════════
//  Registro en cada VM (antes del stdlib, que lee estas globales)
// ════════════════════════════════════════════════════════════════════
inline void gl_editables_registrar(lua_State* L) {
    gl_ed_metatabla(L, GL_ED_IMAGEN);
    gl_ed_metatabla(L, GL_ED_MALLA);
    static const struct { const char* nombre; lua_CFunction fn; } globales[] = {
        { "__gl_as_CreateEditableImage", gl_as_CreateEditableImage },
        { "__gl_as_CreateEditableImageAsync", gl_as_CreateEditableImageAsync },
        { "__gl_as_CreateEditableMesh", gl_as_CreateEditableMesh },
        { "__gl_as_CreateEditableMeshAsync", gl_as_CreateEditableMeshAsync },
        { "__gl_as_CreateMeshPartAsync", gl_as_CreateMeshPartAsync },
    };
    for (const auto& g : globales) {
        lua_pushcfunction(L, g.fn, g.nombre);
        lua_setglobal(L, g.nombre);
    }
    // Para las sondas: imagenes y mallas vivas, y bytes de pixeles.
    lua_pushcfunction(L, [](lua_State* pL) -> int {
        lua_pushnumber(pL, (double)gl_ei_vivas());
        lua_pushnumber(pL, (double)gl_em_vivas());
        lua_pushnumber(pL, (double)gl_ei_bytes());
        return 3;
    }, "__gl_editables_contar");
    lua_setglobal(L, "__gl_editables_contar");
}

#endif // GL_PUENTE_EDITABLES_H

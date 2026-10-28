#ifndef GL_ATRIBUTOS_H
#define GL_ATRIBUTOS_H

// ════════════════════════════════════════════════════════════════════
//  Atributos de Roblox con TODOS sus tipos (F6 de la v1.17): string,
//  boolean, number, UDim, UDim2, BrickColor, Color3, Vector2, Vector3,
//  CFrame, EnumItem, NumberSequence, ColorSequence, NumberRange, Rect y Font.
//
//  Se guardan como metadato del nodo, vengan del importador, de SetAttribute
//  o de la red (la replicacion manda el Variant tal cual):
//    number  -> double       boolean -> bool        string -> String
//    Vector3 -> Vector3      Vector2 -> Vector2     Color3 -> Color
//    UDim2   -> Vector4      CFrame  -> Transform3D Rect   -> Rect2
//    UDim, NumberRange, BrickColor, EnumItem, NumberSequence, ColorSequence
//    y Font -> Dictionary con "__rbx" = el tipo y sus campos. Las secuencias
//    llevan "Puntos" en el formato de gl_secuencias.h.
//
//  Antes solo existian los siete primeros: un CFrame o un NumberRange puestos
//  con SetAttribute se guardaban como TEXTO, y GetAttribute devolvia un string.
// ════════════════════════════════════════════════════════════════════

inline Dictionary gl_attr_dict(const char* tipo) {
    Dictionary d;
    d["__rbx"] = String(tipo);
    return d;
}
inline Variant gl_attr_udim(float escala, int desplaza) {
    Dictionary d = gl_attr_dict("UDim");
    d["Scale"] = escala;
    d["Offset"] = desplaza;
    return d;
}
inline Variant gl_attr_rango(float minimo, float maximo) {
    Dictionary d = gl_attr_dict("NumberRange");
    d["Min"] = minimo;
    d["Max"] = maximo;
    return d;
}
inline Variant gl_attr_brickcolor(int numero) {
    Dictionary d = gl_attr_dict("BrickColor");
    d["Number"] = numero;
    return d;
}
inline Variant gl_attr_enum(const String& nombre_enum, int valor) {
    Dictionary d = gl_attr_dict("EnumItem");
    d["Enum"] = nombre_enum;
    d["Value"] = valor;
    return d;
}
inline Variant gl_attr_fuente(const String& familia, int peso, int estilo) {
    Dictionary d = gl_attr_dict("Font");
    d["Family"] = familia;
    d["Weight"] = peso;
    d["Style"] = estilo;
    return d;
}
// tipo: "NumberSequence" o "ColorSequence"; puntos: Array de Dictionary
// {Time, Value | Color, Envelope}.
inline Variant gl_attr_secuencia(const char* tipo, const Array& puntos) {
    Dictionary d = gl_attr_dict(tipo);
    d["Puntos"] = puntos;
    return d;
}

// typeof de una tabla del stdlib: el __type de su metatabla ("" si no tiene).
inline String gl_lua_tipo_tabla(lua_State* L, int idx) {
    if (!lua_getmetatable(L, idx)) return String();
    lua_getfield(L, -1, "__type");
    const String t = lua_isstring(L, -1) ? String(lua_tostring(L, -1)) : String();
    lua_pop(L, 2);
    return t;
}
inline double gl_lua_campo_num(lua_State* L, int idx, const char* k) {
    lua_getfield(L, idx, k);
    const double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}
// Un campo que puede ser un EnumItem o su numero (Font.Weight, Font.Style).
inline int gl_lua_campo_enum(lua_State* L, int idx, const char* k, int defecto) {
    lua_getfield(L, idx, k);
    int v = defecto;
    if (lua_isnumber(L, -1)) v = (int)lua_tonumber(L, -1);
    else if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "Value");
        if (lua_isnumber(L, -1)) v = (int)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return v;
}

// Valor de Luau -> atributo. false si Roblox no admite ese tipo como atributo
// (una tabla cualquiera, una Instance...).
inline bool gl_attr_desde_lua(lua_State* L, int idx, Variant& out) {
    if (idx < 0) idx = lua_gettop(L) + 1 + idx;
    switch (lua_type(L, idx)) {
        case LUA_TNUMBER:  out = (double)lua_tonumber(L, idx); return true;
        case LUA_TBOOLEAN: out = lua_toboolean(L, idx) != 0; return true;
        case LUA_TSTRING: {
            size_t n = 0;
            const char* s = lua_tolstring(L, idx, &n);
            out = String::utf8(s, (int)n);
            return true;
        }
        case LUA_TUSERDATA:
            if (LuauVector3* v = check_vector3(L, idx)) { out = Vector3(v->x, v->y, v->z); return true; }
            if (LuauColor3* c = check_color3(L, idx))   { out = Color(c->r, c->g, c->b);   return true; }
            return false;
        case LUA_TTABLE: break;
        default: return false;
    }
    const String t = gl_lua_tipo_tabla(L, idx);
    if (t == "Vector2") {
        out = Vector2((float)gl_lua_campo_num(L, idx, "X"), (float)gl_lua_campo_num(L, idx, "Y"));
        return true;
    }
    if (t == "UDim") {
        out = gl_attr_udim((float)gl_lua_campo_num(L, idx, "Scale"), (int)gl_lua_campo_num(L, idx, "Offset"));
        return true;
    }
    if (t == "UDim2") {
        lua_getfield(L, idx, "X");
        lua_getfield(L, idx, "Y");
        if (!lua_istable(L, -2) || !lua_istable(L, -1)) { lua_pop(L, 2); return false; }
        out = Vector4((float)gl_lua_campo_num(L, -2, "Scale"), (float)gl_lua_campo_num(L, -2, "Offset"),
                      (float)gl_lua_campo_num(L, -1, "Scale"), (float)gl_lua_campo_num(L, -1, "Offset"));
        lua_pop(L, 2);
        return true;
    }
    if (t == "CFrame") { out = _gl_read_cframe_tbl(L, idx); return true; }
    if (t == "NumberRange") {
        out = gl_attr_rango((float)gl_lua_campo_num(L, idx, "Min"), (float)gl_lua_campo_num(L, idx, "Max"));
        return true;
    }
    if (t == "Rect") {
        lua_getfield(L, idx, "Min");
        lua_getfield(L, idx, "Max");
        if (!lua_istable(L, -2) || !lua_istable(L, -1)) { lua_pop(L, 2); return false; }
        const float x0 = (float)gl_lua_campo_num(L, -2, "X"), y0 = (float)gl_lua_campo_num(L, -2, "Y");
        const float x1 = (float)gl_lua_campo_num(L, -1, "X"), y1 = (float)gl_lua_campo_num(L, -1, "Y");
        lua_pop(L, 2);
        out = Rect2(x0, y0, x1 - x0, y1 - y0);
        return true;
    }
    if (t == "BrickColor") { out = gl_attr_brickcolor((int)gl_lua_campo_num(L, idx, "Number")); return true; }
    if (t == "EnumItem") {
        // El nombre del enum sale de su tostring, "Enum.FontWeight.Bold": el
        // stdlib no lo guarda en el EnumItem (EnumType es la tabla del enum).
        size_t n = 0;
        const char* s = luaL_tolstring(L, idx, &n);
        String texto = String::utf8(s, (int)n);
        lua_pop(L, 1);
        if (!texto.begins_with("Enum.")) return false;
        texto = texto.substr(5);
        const int punto = texto.find(".");
        if (punto <= 0) return false;
        out = gl_attr_enum(texto.substr(0, punto), (int)gl_lua_campo_num(L, idx, "Value"));
        return true;
    }
    if (t == "NumberSequence" || t == "ColorSequence") {
        Variant puntos;
        if (!gl_luau_to_variant(L, idx, puntos) || puntos.get_type() != Variant::ARRAY) return false;
        out = gl_attr_secuencia(t == "NumberSequence" ? "NumberSequence" : "ColorSequence", puntos);
        return true;
    }
    if (t == "Font") {
        lua_getfield(L, idx, "Family");
        const String familia = lua_isstring(L, -1) ? String::utf8(lua_tostring(L, -1)) : String();
        lua_pop(L, 1);
        out = gl_attr_fuente(familia, gl_lua_campo_enum(L, idx, "Weight", 400),
                             gl_lua_campo_enum(L, idx, "Style", 0));
        return true;
    }
    return false;
}

// Deja en la pila el valor de un atributo con su tipo de Roblox.
inline void gl_push_attr_value(lua_State* L, const Variant& val) {
    switch (val.get_type()) {
        case Variant::FLOAT:
        case Variant::INT:  lua_pushnumber(L, (double)val); return;
        case Variant::BOOL: lua_pushboolean(L, (bool)val);  return;
        case Variant::STRING: lua_pushstring(L, String(val).utf8().get_data()); return;
        case Variant::VECTOR3: { const Vector3 v = val; push_vector3(L, v.x, v.y, v.z); return; }
        case Variant::VECTOR2: { const Vector2 v = val; push_vector2(L, v.x, v.y);      return; }
        case Variant::COLOR:   { const Color c = val;   push_color3(L, c.r, c.g, c.b);  return; }
        case Variant::VECTOR4: {   // UDim2
            const Vector4 u = val;
            if (!gl_push_constructor(L, "UDim2")) { lua_pushnil(L); return; }
            lua_pushnumber(L, u.x); lua_pushnumber(L, u.y);
            lua_pushnumber(L, u.z); lua_pushnumber(L, u.w);
            lua_call(L, 4, 1);
            return;
        }
        case Variant::TRANSFORM3D: _gl_push_cframe_full(L, (Transform3D)val); return;
        case Variant::RECT2: {
            const Rect2 r = val;
            if (!gl_push_constructor(L, "Rect")) { lua_pushnil(L); return; }
            lua_pushnumber(L, r.position.x); lua_pushnumber(L, r.position.y);
            lua_pushnumber(L, r.position.x + r.size.x); lua_pushnumber(L, r.position.y + r.size.y);
            lua_call(L, 4, 1);
            return;
        }
        case Variant::DICTIONARY: {
            const Dictionary d = val;
            const String t = d.get("__rbx", String());
            if (t == "UDim") {
                if (!gl_push_constructor(L, "UDim")) { lua_pushnil(L); return; }
                lua_pushnumber(L, (double)d.get("Scale", 0.0));
                lua_pushnumber(L, (double)(int64_t)d.get("Offset", 0));
                lua_call(L, 2, 1);
                return;
            }
            if (t == "NumberRange") {
                gl_push_number_range(L, Vector2((float)(double)d.get("Min", 0.0), (float)(double)d.get("Max", 0.0)));
                return;
            }
            if (t == "BrickColor") {
                if (!gl_push_constructor(L, "BrickColor")) { lua_pushnil(L); return; }
                lua_pushnumber(L, (double)(int64_t)d.get("Number", 194));
                lua_call(L, 1, 1);
                return;
            }
            if (t == "EnumItem") {
                const String e = d.get("Enum", String());
                uis_push_enum(L, e.utf8().get_data(), (int)(int64_t)d.get("Value", 0));
                return;
            }
            if (t == "NumberSequence") { gl_push_number_sequence(L, d.get("Puntos", Array()), 0.0f); return; }
            if (t == "ColorSequence")  { gl_push_color_sequence(L, d.get("Puntos", Array())); return; }
            if (t == "Font") {
                if (!gl_push_constructor(L, "Font")) { lua_pushnil(L); return; }
                lua_pushstring(L, String(d.get("Family", String())).utf8().get_data());
                uis_push_enum(L, "FontWeight", (int)(int64_t)d.get("Weight", 400));
                uis_push_enum(L, "FontStyle", (int)(int64_t)d.get("Style", 0));
                lua_call(L, 3, 1);
                return;
            }
            lua_pushnil(L);
            return;
        }
        default: lua_pushstring(L, String(val).utf8().get_data()); return;
    }
}

#endif // GL_ATRIBUTOS_H

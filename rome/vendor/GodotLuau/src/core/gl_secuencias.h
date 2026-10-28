#ifndef GL_SECUENCIAS_H
#define GL_SECUENCIAS_H

// ════════════════════════════════════════════════════════════════════
//  NumberSequence, ColorSequence y NumberRange de Roblox en C++.
//  Llegan siempre igual, del importador o del puente (gl_luau_to_variant):
//    · secuencia: Array de Dictionary {Time, Value | Color, Envelope}
//    · rango: Vector2(Min, Max)
//  y tambien sueltos (un numero, un Color) desde escenas viejas.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

struct GLSecNum {
    LocalVector<float> t, v, e;
    float en(float x) const {
        if (v.is_empty()) return 0.0f;
        if (x <= t[0]) return v[0];
        for (uint32_t i = 1; i < t.size(); i++) {
            if (x <= t[i]) {
                const float d = t[i] - t[i - 1];
                const float a = d > 1e-6f ? (x - t[i - 1]) / d : 1.0f;
                return Math::lerp(v[i - 1], v[i], a);
            }
        }
        return v[v.size() - 1];
    }
    float maximo() const { float m = 0.0f; for (uint32_t i = 0; i < v.size(); i++) m = Math::max(m, v[i] + e[i]); return m; }
};
struct GLSecColor {
    LocalVector<float> t;
    LocalVector<Color> c;
    Color en(float x) const {
        if (c.is_empty()) return Color(1, 1, 1);
        if (x <= t[0]) return c[0];
        for (uint32_t i = 1; i < t.size(); i++) {
            if (x <= t[i]) {
                const float d = t[i] - t[i - 1];
                return c[i - 1].lerp(c[i], d > 1e-6f ? (x - t[i - 1]) / d : 1.0f);
            }
        }
        return c[c.size() - 1];
    }
};

inline GLSecNum gl_sec_num(const Variant& v, float defecto) {
    GLSecNum s;
    if (v.get_type() == Variant::ARRAY) {
        const Array a = v;
        for (int i = 0; i < a.size(); i++) {
            if (a[i].get_type() != Variant::DICTIONARY) continue;
            const Dictionary d = a[i];
            s.t.push_back((float)(double)d.get("Time", 0.0));
            s.v.push_back((float)(double)d.get("Value", (double)defecto));
            s.e.push_back((float)(double)d.get("Envelope", 0.0));
        }
    } else if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
        s.t.push_back(0.0f); s.v.push_back((float)(double)v); s.e.push_back(0.0f);
    }
    if (s.t.is_empty()) { s.t.push_back(0.0f); s.v.push_back(defecto); s.e.push_back(0.0f); }
    return s;
}
inline GLSecColor gl_sec_color(const Variant& v, const Color& defecto) {
    GLSecColor s;
    if (v.get_type() == Variant::ARRAY) {
        const Array a = v;
        for (int i = 0; i < a.size(); i++) {
            if (a[i].get_type() != Variant::DICTIONARY) continue;
            const Dictionary d = a[i];
            s.t.push_back((float)(double)d.get("Time", 0.0));
            s.c.push_back((Color)d.get("Color", defecto));
        }
    } else if (v.get_type() == Variant::COLOR) {
        s.t.push_back(0.0f); s.c.push_back((Color)v);
    }
    if (s.t.is_empty()) { s.t.push_back(0.0f); s.c.push_back(defecto); }
    return s;
}
// NumberRange: Vector2(min, max) o un numero (min = max).
inline Vector2 gl_rango(const Variant& v, const Vector2& defecto) {
    if (v.get_type() == Variant::VECTOR2) { const Vector2 r = v; return Vector2(Math::min(r.x, r.y), Math::max(r.x, r.y)); }
    if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) { const float x = (float)(double)v; return Vector2(x, x); }
    return defecto;
}
// Las secuencias tal como se guardan, para valores por defecto.
inline Array gl_sec_num_const(float x) {
    Array a;
    for (int i = 0; i < 2; i++) {
        Dictionary d; d["Time"] = (double)i; d["Value"] = (double)x; d["Envelope"] = 0.0;
        a.push_back(d);
    }
    return a;
}
inline Array gl_sec_color_const(const Color& c) {
    Array a;
    for (int i = 0; i < 2; i++) {
        Dictionary d; d["Time"] = (double)i; d["Color"] = c; d["Envelope"] = 0.0;
        a.push_back(d);
    }
    return a;
}

#endif // GL_SECUENCIAS_H

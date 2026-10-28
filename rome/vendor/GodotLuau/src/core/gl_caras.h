#ifndef GL_CARAS_H
#define GL_CARAS_H

// Enum.NormalId de Roblox (Right = 0, Top = 1, Back = 2, Left = 3, Bottom = 4,
// Front = 5) en el espacio de la pieza: la normal de cada cara y una base cuyo
// -Z mira hacia fuera de ella (hacia donde alumbra un SpotLight de Godot).

#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

inline Vector3 gl_normal_de_cara(int cara) {
    switch (cara) {
        case 0: return Vector3(1, 0, 0);    // Right
        case 1: return Vector3(0, 1, 0);    // Top
        case 2: return Vector3(0, 0, 1);    // Back
        case 3: return Vector3(-1, 0, 0);   // Left
        case 4: return Vector3(0, -1, 0);   // Bottom
        default: return Vector3(0, 0, -1);  // Front
    }
}
inline Basis gl_base_mirando_a_cara(int cara) {
    const Vector3 n = gl_normal_de_cara(cara);
    const Vector3 arriba = Math::abs(n.y) > 0.99f ? Vector3(0, 0, -1) : Vector3(0, 1, 0);
    return Basis::looking_at(n, arriba);
}
// La mitad del tamaño de la pieza a lo largo de la normal de esa cara.
inline float gl_media_cara(const Vector3& tam, int cara) {
    const Vector3 n = gl_normal_de_cara(cara);
    return Math::abs(n.x) * tam.x * 0.5f + Math::abs(n.y) * tam.y * 0.5f + Math::abs(n.z) * tam.z * 0.5f;
}

#endif // GL_CARAS_H

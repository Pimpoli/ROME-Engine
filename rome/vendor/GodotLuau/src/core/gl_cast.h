#ifndef GL_CAST_H
#define GL_CAST_H

// ════════════════════════════════════════════════════════════════════
//  gl_cast<T>: el Object::cast_to de godot-cpp sin pasar por Godot.
//
//  cast_to copia un StringName (el nombre de la clase), pide a Godot la marca
//  de la clase, le pregunta si el objeto es de esa clase, busca el enlace del
//  objeto (con un cerrojo) y al final hace el dynamic_cast de C++: cinco
//  llamadas al motor y dos o tres cerrojos por conversion. El puente de Luau
//  convierte varias veces en cada acceso; medido con el muestreador
//  (GL_MUESTREO=1) en el arranque de Build a Mountain, ~200 ms de un frame de
//  2,8 s eran eso.
//
//  Basta con el dynamic_cast: todo Object* de godot-cpp es el enlace del
//  objeto, y godot-cpp lo crea con la clase MAS DERIVADA que conoce (la del
//  objeto o su antepasado registrado mas cercano; ver
//  get_object_instance_binding y ClassDB::get_instance_binding_callbacks). Si
//  el objeto es de la clase T, su enlace deriva de T y el dynamic_cast lo da.
//
//  Y ni siquiera el dynamic_cast cada vez: con MinGW compara los NOMBRES de los
//  tipos con strcmp subiendo por la herencia, y el puente prueba una cadena de
//  clases en cada escritura (363 ms de la carga de Build a Mountain). La
//  respuesta solo depende del tipo real del objeto, asi que se guarda por tipo
//  en una tabla pequeña de cada T: la siguiente vez es comparar un puntero.
//  Cada casilla es una palabra (puntero al type_info | resultado) escrita de
//  una vez: si dos hilos la pisan, lo peor es volver a preguntar.
// ════════════════════════════════════════════════════════════════════

#include <atomic>
#include <cstdint>
#include <typeinfo>
#include <godot_cpp/core/object.hpp>

template <typename T>
struct GLCastTabla {
    static std::atomic<uintptr_t>* casillas() {
        static std::atomic<uintptr_t> t[64] = {};
        return t;
    }
};

// 1 = es T, 0 = no lo es, -1 = no se sabe todavia.
template <typename T>
inline int gl_cast_conocido(const std::type_info* tipo) {
    const uintptr_t clave = (uintptr_t)tipo;
    const uintptr_t v = GLCastTabla<T>::casillas()[(clave >> 4) & 63].load(std::memory_order_relaxed);
    if ((v & ~(uintptr_t)1) != clave) return -1;
    return (int)(v & 1);
}

template <typename T>
inline void gl_cast_apuntar(const std::type_info* tipo, bool es) {
    const uintptr_t clave = (uintptr_t)tipo;
    GLCastTabla<T>::casillas()[(clave >> 4) & 63].store(clave | (es ? 1 : 0), std::memory_order_relaxed);
}

template <typename T>
inline T* gl_cast(godot::Object* o) {
    if (!o) return nullptr;
    const std::type_info* tipo = &typeid(*o);
    const int r = gl_cast_conocido<T>(tipo);
    if (r == 1) return static_cast<T*>(o);
    if (r == 0) return nullptr;
    T* t = dynamic_cast<T*>(o);
    gl_cast_apuntar<T>(tipo, t != nullptr);
    return t;
}

template <typename T>
inline const T* gl_cast(const godot::Object* o) {
    if (!o) return nullptr;
    const std::type_info* tipo = &typeid(*o);
    const int r = gl_cast_conocido<T>(tipo);
    if (r == 1) return static_cast<const T*>(o);
    if (r == 0) return nullptr;
    const T* t = dynamic_cast<const T*>(o);
    gl_cast_apuntar<T>(tipo, t != nullptr);
    return t;
}

#endif // GL_CAST_H

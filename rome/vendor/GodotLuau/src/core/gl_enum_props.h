#ifndef GL_ENUM_PROPS_H
#define GL_ENUM_PROPS_H

// ════════════════════════════════════════════════════════════════════
//  Propiedades enum de Roblox (F8 de la v1.17)
//  ----------------------------------------------------------------
//  En Roblox `parte.Shape` es un EnumItem (Enum.PartType.Block); aqui muchas
//  lecturas del puente devolvian el numero pelado (1). En Lua `1 == tabla` es
//  SIEMPRE false (__eq solo salta entre dos tablas), asi que
//  `parte.Shape == Enum.PartType.Block` fallaba en silencio en 81 propiedades.
//
//  La tabla sale de la API de Roblox (gl_enum_props_gen.h, generada). El
//  puente, al terminar una lectura que da un numero, pregunta aqui si esa
//  propiedad es un enum para esa clase y lo cambia por su EnumItem
//  (__gl_enum_item). Solo se paga en lecturas que dan numero: un hash del
//  nombre, y la clase solo si el nombre es de un enum.
// ════════════════════════════════════════════════════════════════════

#include "gl_enum_props_gen.h"

#include <cstdint>
#include <cstring>
#include <vector>

struct GLEnumPropIndice { uint32_t hash; int ini; int fin; };

inline uint32_t gl_enum_hash(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

// Las filas de una propiedad (todas las clases que la tienen), o nullptr si
// ningun enum de Roblox se llama asi.
inline const GLEnumPropIndice* gl_enum_indice(const char* key) {
    static const int TAM = 1024;   // > 2 x los 291 nombres: sondeo lineal corto
    static const std::vector<GLEnumPropIndice> tabla = [] {
        std::vector<GLEnumPropIndice> t(TAM, GLEnumPropIndice{ 0, -1, -1 });
        int i = 0;
        while (i < GL_ENUM_PROPS_GEN_N) {
            int j = i;
            while (j < GL_ENUM_PROPS_GEN_N && strcmp(GL_ENUM_PROPS_GEN[j].prop, GL_ENUM_PROPS_GEN[i].prop) == 0) j++;
            const uint32_t h = gl_enum_hash(GL_ENUM_PROPS_GEN[i].prop);
            size_t s = h & (TAM - 1);
            while (t[s].ini >= 0) s = (s + 1) & (TAM - 1);
            t[s] = GLEnumPropIndice{ h, i, j };
            i = j;
        }
        return t;
    }();
    if (!key || !key[0]) return nullptr;
    const uint32_t h = gl_enum_hash(key);
    size_t s = h & (TAM - 1);
    while (tabla[s].ini >= 0) {
        if (tabla[s].hash == h && strcmp(GL_ENUM_PROPS_GEN[tabla[s].ini].prop, key) == 0) return &tabla[s];
        s = (s + 1) & (TAM - 1);
    }
    return nullptr;
}

// El enum de esa propiedad en esa clase. Si la clase no esta en la tabla, vale
// el enum de la propiedad solo si es el mismo en todas las clases (Shape es
// PartType en una Part y ParticleEmitterShape en un ParticleEmitter: ahi no se
// adivina).
inline const char* gl_enum_para_clase(const GLEnumPropIndice* e, const char* clase) {
    if (!e) return nullptr;
    const char* unico = GL_ENUM_PROPS_GEN[e->ini].enumerado;
    bool mismo = true;
    for (int k = e->ini; k < e->fin; k++) {
        if (clase && strcmp(GL_ENUM_PROPS_GEN[k].clase, clase) == 0) return GL_ENUM_PROPS_GEN[k].enumerado;
        if (strcmp(GL_ENUM_PROPS_GEN[k].enumerado, unico) != 0) mismo = false;
    }
    return mismo ? unico : nullptr;
}

#endif // GL_ENUM_PROPS_H

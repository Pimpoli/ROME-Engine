#ifndef GL_MUESTREO_H
#define GL_MUESTREO_H

#include <cstdint>

// Muestreador de pila del hilo principal (gl_muestreo.cpp). Sin GL_MUESTREO=1
// en el entorno no hace nada.
void gl_muestreo_iniciar(uint64_t ticks_godot_us);
void gl_muestreo_volcar();

#endif // GL_MUESTREO_H

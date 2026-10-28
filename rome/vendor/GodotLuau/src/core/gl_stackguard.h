#ifndef GL_STACKGUARD_H
#define GL_STACKGUARD_H

// ════════════════════════════════════════════════════════════════════
//  Guardia de PILA del puente Lua <-> Godot.
//
//  El problema: un script puede encadenar recursion a traves del puente
//  (indexar -> metamétodo en C++ -> volver a Lua -> indexar...). Cada nivel
//  gasta MUCHISIMA mas pila de C que un nivel de Lua puro, asi que el limite de
//  llamadas de Luau no llega a saltar: el proceso se queda sin pila y se cae con
//  un segfault, sin mensaje y sin nada que mirar.
//
//  Aqui se mide cuanta pila queda ANTES de seguir bajando. Si queda poca, se
//  lanza un error de Lua normal ("stack overflow") en vez de reventar: el script
//  culpable muere con su linea y su traza, como en Roblox, y el juego sigue.
//
//  Coste: leer la direccion de una variable local y una comparacion. En Windows
//  los limites reales del hilo los da el sistema, asi que no hay falsos
//  positivos por adivinar el tamaño de la pila.
// ════════════════════════════════════════════════════════════════════

#include "lua.h"
#include "lualib.h"

#include <cstddef>
#include <cstdint>

// Se declara a mano en vez de incluir <windows.h>: esa cabecera define macros
// (MIN/MAX/small/IN/OUT...) que rompen la compilacion del resto del proyecto.
#if defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall
GetCurrentThreadStackLimits(uint64_t* LowLimit, uint64_t* HighLimit);
#endif

// Margen que se reserva: por debajo de esto se corta. Un nivel del puente puede
// gastar bastante, asi que hace falta holgura para desenrollar con seguridad.
inline constexpr size_t GL_STACK_MARGIN = 256 * 1024;

// Direccion del fondo de la pila del hilo actual (0 si no se puede saber).
inline char* gl_stack_floor() {
#if defined(_WIN32)
    uint64_t low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    return (char*)(uintptr_t)low;
#else
    return nullptr;
#endif
}

// Sin API del sistema: se recuerda la direccion mas alta vista y se pone un
// presupuesto fijo desde ahi.
//
// thread_local: cada hilo tiene SU pila, en un rango de direcciones distinto.
// Con una sola variable global, el primer hilo que pasara por aqui fijaba la
// marca y cualquier otro hilo con la pila en direcciones mas bajas se veia
// "agotado" nada mas entrar (falso positivo: "stack overflow" en un script que
// no recursaba), o al reves quedaba sin proteccion.
inline char*& gl_stack_high_water() {
    static thread_local char* hi = nullptr;
    return hi;
}

// true si queda poca pila para seguir bajando.
inline bool gl_stack_exhausted(void* here) {
    char* cur = (char*)here;
#if defined(_WIN32)
    // thread_local por el mismo motivo, y ademas por correccion: el limite
    // cacheado de OTRO hilo hace que cur < floor_addr, y esa resta en size_t
    // da un numero gigante → el guard no saltaba NUNCA y volvia el segfault
    // que este archivo existe para evitar.
    static thread_local char* floor_addr = nullptr;
    if (!floor_addr) floor_addr = gl_stack_floor();
    if (floor_addr) {
        if (cur < floor_addr) return true;   // por debajo del fondo: no hay margen
        return (size_t)(cur - floor_addr) < GL_STACK_MARGIN;
    }
#endif
    char*& hi = gl_stack_high_water();
    if (!hi || cur > hi) { hi = cur; return false; }
    // Presupuesto de 1 MB de crecimiento por el puente: mas que eso ya es
    // recursion patologica en cualquier juego real.
    return (size_t)(hi - cur) > (1024u * 1024u);
}

// Se pone al entrar en los metamétodos del puente. Si la pila esta al limite,
// corta con un error de Lua (que SI se puede capturar y reportar).
#define GL_STACK_GUARD(L)                                                        \
    do {                                                                         \
        char _gl_probe;                                                          \
        if (gl_stack_exhausted(&_gl_probe)) {                                    \
            luaL_error((L), "stack overflow (demasiada recursion en el script)"); \
        }                                                                        \
    } while (0)

#endif // GL_STACKGUARD_H

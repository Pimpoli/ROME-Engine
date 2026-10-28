#ifndef GL_PHYSLOD_H
#define GL_PHYSLOD_H

// ════════════════════════════════════════════════════════════════════════
//  PHYSICS LOD — nivel de detalle para la fisica, estilo "islas" de Roblox
//
//  QUE SE PUEDE Y QUE NO EN GODOT (leelo antes de tocar nada):
//
//  El paso de fisica de Godot es GLOBAL. Engine::physics_ticks_per_second es
//  uno para todo el mundo, y Jolt integra TODAS las islas despiertas con el
//  mismo dt en una sola llamada. NO existe "este cuerpo a 30 Hz y ese a 144".
//  Roblox si lo tiene (su solver esta hecho para pasos por isla); Jolt no.
//
//  Y subir el mundo entero a 144 Hz no es una opcion, por dos razones medidas:
//
//    1. Tenemos max_physics_steps_per_frame = 1 (es lo que mato la espiral de
//       recuperacion que daba frames de 273 ms). Con ese tope, cada frame
//       dibujado adelanta la fisica UN paso. A 144 Hz eso son 6,94 ms de
//       simulacion por frame; si el juego va a 90 FPS, pasan 11,1 ms reales.
//       O sea: EL JUEGO CORRERIA AL 62% DE VELOCIDAD. Subir el tope devuelve
//       la espiral. No hay termino medio.
//    2. Coste bruto: 6.500 cuerpos registrados a 144 Hz son 2,4 veces el
//       trabajo del solver que a 60. A 60 Hz el p99 medido es 24,9 ms.
//
//  Lo que SI es por cuerpo en Godot/Jolt, y por tanto lo unico con lo que se
//  puede construir un LOD de verdad:
//        · dormir / congelar (freeze)          -> el "0 Hz"
//        · CCD (continuous_cd)                 -> el "mas Hz" contra el tunel
//        · gravity_scale, damping, capas/mascaras
//        · monitoreo de contactos
//
//  ASI QUE AQUI "Hz" SIGNIFICA DOS COSAS DISTINTAS, a proposito:
//
//    a) Para el MOTOR: el mundo se queda a 60 Hz (el Heartbeat de Roblox).
//       "Menos Hz" se traduce a CONGELAR lo lejano y quieto (mejor que 30 Hz:
//       es 0). "Mas Hz" se traduce a ENCENDER CCD, que es literalmente la
//       funcion anti-tunel que se buscaba pidiendo 144 Hz — y es por cuerpo
//       y gratis mientras el cuerpo va lento.
//    b) Para NUESTRA logica en C++ (lo que corre por nodo cada paso): el
//       divisor de abajo es real y se aplica de verdad. Ahi es donde esta
//       nuestro coste, no en el solver.
//
//  EL DESFASE NO ES UN DETALLE. Si todos los cuerpos de un grupo de 30 Hz
//  tickan en los frames PARES, ese frame lleva el 100% del trabajo del grupo
//  y el siguiente el 0%: un diente de sierra, o sea justo el bajon periodico
//  de FPS que llevamos persiguiendo. Por eso cada pieza tiene su `lod_fase`
//  sacada de su id: el trabajo del grupo queda repartido entre los frames.
//
//  Y LO QUE NO SE HACE, tambien a proposito: NO se emite un tick por cuerpo
//  hacia Luau. Roblox dispara UN Stepped/Heartbeat por frame para todo el
//  juego; hacerlo por cuerpo serian 6.500 cruces de VM por paso (390.000/s),
//  y cada cruce nuestro cuesta lo suyo. Seria el cuello nuevo, y ademas
//  romperia el 1:1. El LOD se queda entero en C++.
// ════════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

// Las tres categorias del diseño.
enum GLLodClase : uint8_t {
    GL_LOD_ESTATICO  = 0,   // anclada / no interactua
    GL_LOD_DINAMICO  = 1,   // pieza suelta con la que se puede chocar
    GL_LOD_MECANISMO = 2,   // conjunto, junta o mover: NUNCA se le baja el ritmo
};

struct GLPhysLodCfg {
    bool  activo = true;

    // Radio en studs dentro del cual se considera que el jugador puede
    // interactuar con la pieza. Fuera de el nadie la va a tocar.
    float cerca = 250.0f;

    // Categoria 1 — estaticos
    float hz_cerca = 60.0f;
    float hz_lejos = 30.0f;

    // Categoria 2 — dinamicos: 60 de base y +hz_paso por cada vel_paso de
    // velocidad por encima de vel_umbral, con tope hz_max.
    float hz_base    = 60.0f;
    float vel_umbral = 20.0f;
    float vel_paso   = 10.0f;
    float hz_paso    = 20.0f;
    float hz_max     = 120.0f;

    // Categoria 3 — mecanismos. No se usa como ritmo del motor (no se puede,
    // ver la cabecera): marca "nunca bajar el ritmo y encender CCD".
    float hz_mecanismo = 144.0f;

    // A partir de aqui se enciende el CCD del cuerpo.
    float hz_ccd = 120.0f;

    // Congelar lo lejano Y quieto (el "sleeping = true" del diseño). El radio
    // es cerca * congelar_factor.
    bool  congelar_lejanas = true;
    float congelar_factor  = 3.0f;

    // Contadores del ultimo barrido completo, para el informe.
    uint32_t n_estatico = 0, n_dinamico = 0, n_mecanismo = 0;
    uint32_t n_congeladas = 0, n_ccd = 0;
    // Por que NO se congelan las que no se congelan (diagnostico). Las n_ las
    // van sumando las piezas durante la vuelta; al cerrarla se vuelcan a las p_,
    // que son las que se leen, y las n_ arrancan de cero otra vez.
    uint32_t n_lejos = 0;     // dinamicas mas alla del radio de congelacion
    uint32_t n_ajenas = 0;    // de esas, ya las tenia congeladas otro
    uint32_t n_moviendo = 0;  // de esas, no estaban en reposo
    uint32_t p_lejos = 0, p_ajenas = 0, p_moviendo = 0;
};

inline GLPhysLodCfg& gl_physlod() { static GLPhysLodCfg c; return c; }

// Hz objetivo -> cada cuantos pasos de fisica le toca trabajar a la pieza.
// Se ata al ritmo REAL del motor, no a 60 fijos: si algun dia el mundo corre a
// otra frecuencia, los divisores siguen significando lo mismo.
inline uint8_t gl_lod_divisor(float hz) {
    Engine* e = Engine::get_singleton();
    const float base = e ? (float)e->get_physics_ticks_per_second() : 60.0f;
    if (hz >= base - 0.5f) return 1;
    const int d = (int)Math::round(base / Math::max(hz, 1.0f));
    return (uint8_t)CLAMP(d, 1, 16);
}

inline uint64_t gl_lod_frame() {
    Engine* e = Engine::get_singleton();
    return e ? e->get_physics_frames() : 0;
}

// ¿Le toca a este divisor/desfase en el paso de ahora?
inline bool gl_lod_toca(uint8_t div, uint8_t fase) {
    if (div <= 1) return true;
    return ((gl_lod_frame() + (uint64_t)fase) % (uint64_t)div) == 0;
}

// Ajustes por NOMBRE, para el menu y para Lua (workspace:SetPhysicsLOD).
inline bool gl_physlod_set(const String& n, float v) {
    GLPhysLodCfg& c = gl_physlod();
    if      (n == "Enabled")        c.activo     = v > 0.5f;
    else if (n == "NearRadius")     c.cerca      = CLAMP(v, 16.0f, 4096.0f);
    else if (n == "NearHz")         c.hz_cerca   = CLAMP(v, 5.0f, 240.0f);
    else if (n == "FarHz")          c.hz_lejos   = CLAMP(v, 5.0f, 240.0f);
    else if (n == "BaseHz")         c.hz_base    = CLAMP(v, 5.0f, 240.0f);
    else if (n == "SpeedThreshold") c.vel_umbral = CLAMP(v, 0.0f, 500.0f);
    else if (n == "SpeedStep")      c.vel_paso   = CLAMP(v, 1.0f, 500.0f);
    else if (n == "HzStep")         c.hz_paso    = CLAMP(v, 1.0f, 120.0f);
    else if (n == "MaxHz")          c.hz_max     = CLAMP(v, 30.0f, 240.0f);
    else if (n == "MechanismHz")    c.hz_mecanismo = CLAMP(v, 30.0f, 480.0f);
    else if (n == "CCDHz")          c.hz_ccd     = CLAMP(v, 30.0f, 480.0f);
    else if (n == "FreezeFar")      c.congelar_lejanas = v > 0.5f;
    else if (n == "FreezeFactor")   c.congelar_factor  = CLAMP(v, 1.0f, 20.0f);
    else return false;
    return true;
}
inline float gl_physlod_get(const String& n) {
    const GLPhysLodCfg& c = gl_physlod();
    if (n == "Enabled")        return c.activo ? 1.0f : 0.0f;
    if (n == "NearRadius")     return c.cerca;
    if (n == "NearHz")         return c.hz_cerca;
    if (n == "FarHz")          return c.hz_lejos;
    if (n == "BaseHz")         return c.hz_base;
    if (n == "SpeedThreshold") return c.vel_umbral;
    if (n == "SpeedStep")      return c.vel_paso;
    if (n == "HzStep")         return c.hz_paso;
    if (n == "MaxHz")          return c.hz_max;
    if (n == "MechanismHz")    return c.hz_mecanismo;
    if (n == "CCDHz")          return c.hz_ccd;
    if (n == "FreezeFar")      return c.congelar_lejanas ? 1.0f : 0.0f;
    if (n == "FreezeFactor")   return c.congelar_factor;
    // Solo lectura: como quedo el reparto en el ultimo barrido.
    if (n == "CountStatic")    return (float)c.n_estatico;
    if (n == "CountDynamic")   return (float)c.n_dinamico;
    if (n == "CountMechanism") return (float)c.n_mecanismo;
    if (n == "CountFrozen")    return (float)c.n_congeladas;
    if (n == "CountCCD")       return (float)c.n_ccd;
    if (n == "CountFar")       return (float)c.p_lejos;
    if (n == "CountForeign")   return (float)c.p_ajenas;
    if (n == "CountMoving")    return (float)c.p_moviendo;
    return 0.0f;
}


// ── LA GRAVEDAD DEL MUNDO, ACCESIBLE SIN INCLUIR EL WORKSPACE ─────────
// En Roblox el personaje cae con Workspace.Gravity, igual que cualquier pieza.
// El Humanoid necesita ese numero, pero incluir roblox_workspace.h desde
// humanoid.h monta un ciclo de includes. Asi que el Workspace ESCRIBE aqui su
// valor cada vez que cambia y el Humanoid lo LEE. Un solo float.
// (static local en funcion, no global: ver el gotcha del orden de
// inicializacion en gl_errors.h)
inline float& gl_workspace_gravity_ref() {
    static float g = 196.2f;   // el valor por defecto de Roblox
    return g;
}
inline float gl_workspace_gravity() { return gl_workspace_gravity_ref(); }

#endif // GL_PHYSLOD_H

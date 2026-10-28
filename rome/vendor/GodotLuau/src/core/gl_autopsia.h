#ifndef GL_AUTOPSIA_H
#define GL_AUTOPSIA_H

// ════════════════════════════════════════════════════════════════════
//  AUTOPSIA DEL FRAME LARGO (solo con GL_AUTOPSIA=1 en el entorno)
//
//  Los monitores de Godot (Performance.TIME_PROCESS) van un frame por detras
//  del delta que ve _process, asi que en un pico dicen cualquier cosa (llegan a
//  dar "resto" NEGATIVO). Esto cronometra dentro del C++, con relojes propios,
//  y vuelca el reparto SOLO en los frames que se pasan del umbral.
//
//  Apagado no cuesta nada: la lectura del entorno se hace una vez.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/pair.hpp>

using namespace godot;

enum GLAutBucket {
    AUT_CLONE = 0,     // gl_clonar completo (raiz del arbol clonado)
    AUT_PARENT,        // escritura de .Parent desde Lua (incluye add_child entero)
    AUT_MONTAJE,       // RobloxPart::_montar_ahora  (ANIDADO dentro de PARENT)
    AUT_TAGS,          // CollectionService::_gl_node_added
    AUT_DESTROY,       // Destroy() desde Lua
    AUT_NUEVO,         // Instance.new
    AUT_SCRIPT,        // ScriptNode::_process de TODOS los scripts (los engloba)
    AUT_SALIR,         // RobloxPart saliendo del arbol / destruyendose
    AUT_LEE,           // lecturas del puente Luau->C++ (dentro de SCRIPTS)
    AUT_ESCRIBE,       // escrituras del puente (dentro de SCRIPTS)
    AUT_CAIDAS,        // barrido de FallenPartsDestroyHeight
    AUT_SOMBRAS,       // recorte de sombras/particulas lejanas
    // ── Turno POR FRAME de cada clase nuestra ────────────────────────
    // Los cubos de arriba miden sucesos (clonar, destruir, un script). Estos
    // miden lo que cuesta el _process de cada familia de nodos, que es donde
    // se iba el frame sin que ningun A/B por clase lo delatara.
    AUT_P_RUN,         // RunService::_process (hover + Heartbeat + RenderStepped)
    AUT_P_HOVER,       // solo el barrido de hover (DENTRO de AUT_P_RUN)
    AUT_P_RENDER,      // callbacks de RenderStepped del juego (DENTRO de AUT_P_RUN)
    AUT_P_HEART,       // callbacks de Heartbeat del juego  (DENTRO de AUT_P_RUN)
    AUT_P_LAYOUT,      // UIListLayout / UIGridLayout
    AUT_P_CARTEL,      // BillboardGui siguiendo a su Adornee
    AUT_P_WORKSPACE,   // RobloxWorkspace::_process entero
    AUT_MAX
};

struct GLAutopsia {
    uint64_t us[AUT_MAX] = {0};
    uint32_t n[AUT_MAX]  = {0};
    uint64_t t_frame = 0;
    int lua_kb = 0;
    int lua_kb_ant = 0;
    int lua_vms = 0;
    uint32_t frames = 0;
    uint32_t picos = 0;
};

inline GLAutopsia& gl_aut() { static GLAutopsia a; return a; }

inline bool gl_aut_on() {
    static int cache = -1;
    if (cache < 0) {
        OS* os = OS::get_singleton();
        cache = (os && os->get_environment("GL_AUTOPSIA") == "1") ? 1 : 0;
    }
    return cache == 1;
}

// Cronometro de ambito. Si la autopsia esta apagada no llama ni al reloj.
struct GLAutTiempo {
    int b;
    uint64_t t0;
    explicit GLAutTiempo(int bucket) : b(bucket), t0(0) {
        if (gl_aut_on()) t0 = Time::get_singleton()->get_ticks_usec();
    }
    ~GLAutTiempo() {
        if (!t0) return;
        GLAutopsia& a = gl_aut();
        a.us[b] += Time::get_singleton()->get_ticks_usec() - t0;
        a.n[b]++;
    }
};

#define GL_AUT_SCOPE(b) GLAutTiempo _gl_aut_scope_##__LINE__(b)

// Se llama UNA vez por frame (desde Workspace::_process). Mide el frame que
// acaba de terminar y, si fue largo, imprime a donde se fue el tiempo.
inline int gl_aut_umbral() {
    static int ms = -1;
    if (ms < 0) {
        OS* os = OS::get_singleton();
        const String v = os ? os->get_environment("GL_AUTOPSIA_MS") : String();
        ms = v.is_empty() ? 60 : (int)v.to_int();
        if (ms < 1) ms = 60;
    }
    return ms;
}

// ── Coste por CALLBACK del juego ──────────────────────────────────────
// Saber que "RunService cuesta 57 ms" no sirve para arreglar nada: hay que
// saber QUE funcion de QUE script se lo lleva. Aqui se acumula el tiempo de
// cada callback de Heartbeat/RenderStepped/Stepped, con su archivo y su linea.
// Solo existe con GL_AUTOPSIA=1; apagado no cuesta ni una suma.
struct GLCbCoste { uint64_t us = 0; uint32_t n = 0; };

inline HashMap<String, GLCbCoste>& gl_aut_cb() {
    static HashMap<String, GLCbCoste> m;
    return m;
}

inline void gl_aut_cb_anota(const String& quien, uint64_t us) {
    GLCbCoste& c = gl_aut_cb()[quien];
    c.us += us;
    c.n++;
}

// Cronometro de un PASO con nombre (va a la misma tabla [AUT-CB]): para partir
// el coste de una funcion por dentro. Apagado no lee ni el reloj.
struct GLAutPaso {
    const char* nombre;
    uint64_t t0;
    explicit GLAutPaso(const char* n) : nombre(n), t0(gl_aut_on() ? Time::get_singleton()->get_ticks_usec() : 0) {}
    ~GLAutPaso() { if (t0) gl_aut_cb_anota(String(nombre), Time::get_singleton()->get_ticks_usec() - t0); }
};
#define GL_AUT_PASO_CAT2(a, b) a##b
#define GL_AUT_PASO_CAT(a, b) GL_AUT_PASO_CAT2(a, b)
#define GL_AUT_PASO(nombre) GLAutPaso GL_AUT_PASO_CAT(_gl_paso_, __LINE__)(nombre)

// Saca los peores y vacia. `frames` es cuantos frames se lleva acumulando, para
// poder dar el coste POR FRAME (que es lo que se compara con los 33 ms).
inline void gl_aut_cb_volcar(uint32_t frames) {
    HashMap<String, GLCbCoste>& m = gl_aut_cb();
    if (m.is_empty() || frames == 0) return;
    // Sin Pair ni sort de plantillas: seleccion directa del mayor doce veces.
    // Son una docena de entradas, no hace falta nada mas fino.
    UtilityFunctions::print("[AUT-CB] coste por frame de cada callback del juego (",
                            frames, " frames):");
    for (int puesto = 0; puesto < 20; puesto++) {
        String mejor;
        uint64_t mejor_us = 0;
        for (const KeyValue<String, GLCbCoste>& kv : m) {
            if (kv.value.us > mejor_us) { mejor_us = kv.value.us; mejor = kv.key; }
        }
        if (mejor.is_empty()) break;
        UtilityFunctions::print("   ", String::num((double)mejor_us / 1000.0 / frames, 2),
                                " ms/frame  x", m[mejor].n, "  ", mejor);
        m.erase(mejor);
    }
    m.clear();
}

inline void gl_aut_frame() {
    if (!gl_aut_on()) return;
    const int umbral_ms = gl_aut_umbral();
    GLAutopsia& a = gl_aut();
    const uint64_t ahora = Time::get_singleton()->get_ticks_usec();
    if (a.t_frame != 0) {
        const uint64_t dur = ahora - a.t_frame;
        a.frames++;
        if (dur >= (uint64_t)umbral_ms * 1000) {
            a.picos++;
            static const char* kNombre[AUT_MAX] = {
                "Clone()", ".Parent", "  |-montaje", "tags", "Destroy()", "Instance.new",
                "SCRIPTS", "salir-del-arbol", "  |-puente-lee", "  |-puente-escribe",
                "barrido-caidas", "recorte-sombras",
                "RunService", "  |-hover", "  |-RenderStepped", "  |-Heartbeat",
                "UILayouts", "BillboardGui", "Workspace"
            };
            uint64_t contado = 0;
            String linea = String("[AUT] frame ") + String::num_int64((int64_t)(dur / 1000)) + " ms  ->";
            for (int i = 0; i < AUT_MAX; i++) {
                if (a.n[i] == 0) continue;
                // SCRIPT engloba a Clone/Parent/Destroy/new (los llama Lua); MONTAJE
                // va dentro de PARENT. Solo suman al total los que no se solapan.
                if (i == AUT_SCRIPT || i == AUT_SALIR || i == AUT_TAGS ||
                    i == AUT_CAIDAS || i == AUT_P_RUN || i == AUT_P_LAYOUT ||
                    i == AUT_P_CARTEL || i == AUT_P_WORKSPACE) contado += a.us[i];
                linea += String("  ") + kNombre[i] + " "
                       + String::num((double)a.us[i] / 1000.0, 1) + " ms x"
                       + String::num_int64((int64_t)a.n[i]);
            }
            linea += String("  [luau ") + String::num_int64(a.lua_kb) + " KB "
                   + (a.lua_kb >= a.lua_kb_ant ? "+" : "")
                   + String::num_int64((int64_t)a.lua_kb - (int64_t)a.lua_kb_ant)
                   + " en " + String::num_int64(a.lua_vms) + " VM]";
            linea += String("   || sin explicar ")
                   + String::num((double)((int64_t)dur - (int64_t)contado) / 1000.0, 1) + " ms";
            UtilityFunctions::print(linea);
        }
    }
    a.lua_kb_ant = a.lua_kb;
    a.t_frame = ahora;
    for (int i = 0; i < AUT_MAX; i++) { a.us[i] = 0; a.n[i] = 0; }
}

#endif // GL_AUTOPSIA_H

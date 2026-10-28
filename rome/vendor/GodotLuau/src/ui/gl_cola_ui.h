#ifndef GL_COLA_UI_H
#define GL_COLA_UI_H

// ════════════════════════════════════════════════════════════════════════
//  COLA DE TRABAJO DIFERIDO DE LA UI, CON PRESUPUESTO POR FRAME
//
//  EL PROBLEMA, MEDIDO (no supuesto). El frame de la oleada no es UNO, son DOS
//  seguidos, y son de naturaleza distinta. Partiendo el frame con dos nodos, uno
//  con la prioridad de _process mas baja posible y otro con la mas alta:
//
//      frame A:  total ~130 ms  ->  dentro de _process 110-140 ms,  fuera  7-26 ms
//      frame B:  total ~125 ms  ->  dentro de _process   1-32 ms,   fuera 99-117 ms
//
//  (la fisica es 0,5-1,7 ms en los dos, ya estaba descartada)
//
//  El frame B no esta en _process ni en la fisica: es el VACIADO DE LA COLA DE
//  LLAMADAS DIFERIDAS de Godot. La oleada crea ~2.000 nodos y cada uno encola
//  trabajo con call_deferred — el recolocado de cada Control y el _apply de cada
//  UICorner/UIStroke/UIGradient, que en este place son 4.843 — y Godot los
//  ejecuta TODOS de golpe al frame siguiente, sin freno.
//
//  RESULTADO: NO SIRVIO. A/B alternado de dos rondas de 152 s cada una, con el
//  interruptor GL_SIN_COLAS para comparar sin recompilar:
//        sin cola: mediana 25,1 y 29,1 ms · 36 y 35 tirones
//        con cola: mediana 29,6 y 25,0 ms · 35 y 36 tirones
//  Las medianas siguen el ORDEN de la corrida, no la configuracion, y los
//  tirones son los mismos. El fichero se queda porque la maquinaria (cola con
//  presupuesto y deduplicado por entero) esta bien y sirve para otra cosa, pero
//  HOY NO LO USA NADIE. Si vuelves a enchufarlo, mide A/B alternado: una sola
//  pasada da 4 ms de diferencia solo por el orden.
//
//  LA IDEA ERA la misma que ya usa el montaje de piezas (GLColaMontaje en
//  characters/roblox_part.h): en vez de call_deferred, se encola aqui y se
//  drena con un presupuesto de microsegundos por frame. El trabajo se hace
//  igual, repartido en varios frames en vez de en uno.
//
//  POR QUE NO SE NOTA: lo que se difiere es recolocar y repintar nodos que
//  ACABAN DE NACER. En un place de Roblox eso son los carteles y las interfaces
//  de los modelos que aparecen a lo lejos; para cuando el jugador los mira ya
//  llevan muchos frames colocados. Lo que ya estaba en pantalla no pasa por
//  aqui: solo se encola desde el camino de creacion.
//
//  DEDUPLICADO: el mismo nodo se encolaba muchas veces (un Control con tres
//  modificadores pedia tres recolocados identicos). La clave es el ObjectID por
//  ocho mas el indice del metodo, o sea un entero: sin construir cadenas.
// ════════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/string_name.hpp>

using namespace godot;

// Los metodos que se difieren. Son pocos y fijos: asi la clave del deduplicado
// cabe en un entero y no hay que construir un String por encolado.
enum GLUIMetodo {
    GLUI_RECOLOCAR = 0,   // _on_parent_resized
    GLUI_APLICAR   = 1,   // _apply  (UICorner / UIStroke / UIGradient / UIScale)
    GLUI_REAJUSTAR = 2,   // _gl_refit_text
    // NO metas aqui el arranque de las VM de Luau. Se probo y sale MUCHO PEOR:
    // peor frame 862 ms contra 213, y la mediana del ultimo tercio 32,9 ms
    // contra 27,3. Un script que arranca tarde retrasa a los que dependen de el
    // y el trabajo se acumula hasta soltarse de golpe. Los scripts arrancan por
    // call_deferred, como siempre.
    GLUI_MAX
};

inline const StringName& gl_ui_metodo_nombre(int m) {
    static const StringName kn[GLUI_MAX] = {
        StringName("_on_parent_resized"),
        StringName("_apply"),
        StringName("_gl_refit_text")
    };
    return kn[m < 0 || m >= GLUI_MAX ? 0 : m];
}

struct GLUITrabajo {
    uint64_t id;
    int metodo;
};

inline LocalVector<GLUITrabajo>& gl_cola_ui() {
    static LocalVector<GLUITrabajo> c; return c;
}
inline HashMap<uint64_t, bool>& gl_cola_ui_indice() {
    static HashMap<uint64_t, bool> m; return m;
}
inline uint32_t& gl_cola_ui_cursor() { static uint32_t c = 0; return c; }

// Presupuesto por frame. 3 ms: con el frame a 16 ms deja sitio de sobra para el
// resto, y una oleada de ~2.000 encolados se reparte en unos pocos frames.
static const uint64_t GL_US_COLA_UI = 3000;

// Interruptor SOLO para medir: con GL_SIN_COLAS=1 se vuelve al comportamiento
// de antes (call_deferred directo) sin recompilar, para poder comparar las dos
// configuraciones en la misma maquina y el mismo rato. Las medidas de una sola
// pasada mienten: la MISMA DLL ha dado 25,4 y 29,2 ms de mediana en dos
// corridas seguidas.
inline bool gl_colas_apagadas() {
    static int v = -1;
    if (v < 0) {
        OS* os = OS::get_singleton();
        v = (os && os->get_environment("GL_SIN_COLAS") == "1") ? 1 : 0;
    }
    return v == 1;
}

inline void gl_ui_encolar(Object* o, int metodo) {
    if (!o) return;
    if (gl_colas_apagadas()) { o->call_deferred(gl_ui_metodo_nombre(metodo)); return; }
    const uint64_t clave = ((uint64_t)o->get_instance_id() << 3) | (uint64_t)(metodo & 7);
    if (gl_cola_ui_indice().has(clave)) return;      // ya estaba pedido
    gl_cola_ui_indice()[clave] = true;
    GLUITrabajo t;
    t.id = (uint64_t)o->get_instance_id();
    t.metodo = metodo;
    gl_cola_ui().push_back(t);
}

// La llama el _process del Workspace, una vez por frame.
inline void gl_drenar_cola_ui(uint64_t presupuesto_us = GL_US_COLA_UI) {
    if (gl_colas_apagadas()) return;
    LocalVector<GLUITrabajo>& c = gl_cola_ui();
    uint32_t& cur = gl_cola_ui_cursor();
    if (cur >= c.size()) {
        if (!c.is_empty()) { c.clear(); gl_cola_ui_indice().clear(); cur = 0; }
        return;
    }
    Time* T = Time::get_singleton();
    const uint64_t t0 = T->get_ticks_usec();
    while (cur < c.size()) {
        const GLUITrabajo t = c[cur++];
        gl_cola_ui_indice().erase(((uint64_t)t.id << 3) | (uint64_t)(t.metodo & 7));
        Object* o = ObjectDB::get_instance(ObjectID(t.id));
        if (!o) continue;                             // se destruyo mientras esperaba
        const StringName& m = gl_ui_metodo_nombre(t.metodo);
        if (o->has_method(m)) o->call(m);
        if (T->get_ticks_usec() - t0 >= presupuesto_us) break;
    }
    // Compactar solo cuando se ha vaciado: mover el vector a cada elemento seria
    // mas caro que el propio trabajo.
    if (cur >= c.size()) { c.clear(); gl_cola_ui_indice().clear(); cur = 0; }
}

// Al descargar la extension: los ObjectID ya no valen nada.
inline void gl_cola_ui_vaciar() {
    gl_cola_ui().clear();
    gl_cola_ui_indice().clear();
    gl_cola_ui_cursor() = 0;
}

#endif // GL_COLA_UI_H

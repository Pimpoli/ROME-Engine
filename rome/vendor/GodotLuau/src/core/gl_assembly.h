#ifndef GL_ASSEMBLY_H
#define GL_ASSEMBLY_H

// ════════════════════════════════════════════════════════════════════
//  ASSEMBLIES (los "conjuntos" de Roblox)
//
//  En Roblox, un grupo de piezas unidas por juntas RIGIDAS (WeldConstraint,
//  Weld, Motor6D, Snap) NO son N cuerpos: son UNO SOLO. Tiene una masa, un
//  centro de masas, una velocidad, y una pieza raiz (AssemblyRootPart) que es
//  la que "posee" la fisica. Un golpe en cualquier miembro mueve el conjunto
//  entero, y si UNA sola pieza esta anclada, el conjunto entero esta anclado.
//
//  Aqui era otra cosa: cada pieza seguia siendo su propio cuerpo y el weld le
//  reimponia la posicion en cada paso. Se veia bien, pero:
//    · la masa era la de una sola pieza, no la del conjunto
//    · un golpe en un miembro NO empujaba nada (el weld se tragaba el impulso)
//    · peor: los miembros quedaban congelados, o sea que eran obstaculos
//      inamovibles contra los que chocaba todo lo demas
//    · AssemblyMass / AssemblyRootPart / AssemblyCenterOfMass no existian
//
//  Como se hace ahora: se agrupan las piezas por union-find sobre las juntas
//  rigidas, se elige la raiz con las reglas de Roblox y se MUEVEN LAS FORMAS de
//  todos los miembros al cuerpo raiz (cada una con su transformacion relativa).
//  Los miembros se quedan sin forma propia y congelados: sus posiciones ya las
//  reimpone el tick del weld, que es exactamente el comportamiento de seguidor
//  que hace falta. El resultado es UN cuerpo con la masa y la forma del
//  conjunto, como en Roblox.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>

using namespace godot;

// Sube cuando algo puede cambiar la FORMA de los conjuntos: una junta rigida
// que se crea, se destruye o se recablea, o una pieza que cambia de Anchored.
// NO la suben las altas y bajas normales del arbol (durante una tanda de
// brainrots entran cientos de piezas y reconstruir en cada una seria absurdo);
// para eso esta el marcado de "sucio" con reconstruccion diferida.
inline uint32_t& gl_gen_ensamblajes() { static uint32_t g = 1; return g; }
inline bool& gl_ensamblajes_sucios() { static bool s = true; return s; }

// ── ESPEJO DE JUNTAS RIGIDAS (pares de ids, nada mas) ────────────────
// MEDIDO: rehacer los conjuntos costaba 3,80 ms y 3,63 de esos (el 96%) se iban
// en UNA cosa: recorrer las 5.336 juntas registradas preguntandole a cada una si
// es rigida y de que dos piezas tira. Son 680 ns por junta, y no por hacer nada
// dificil: cada vuelta son tres Object::cast_to, y un cast_to de godot-cpp no es
// un static_cast — construye un StringName, pide el tag de clase por nombre,
// cruza la frontera de la extension y remata con un dynamic_cast. Tres de esos
// por junta, 5.336 juntas, casi una vez por segundo.
//
// La lista de pares no cambia entre reconstrucciones (medido: 51 conjuntos, 51
// intactos, 0 rehechos). Asi que se guarda ya resuelta, en enteros pelados, y el
// barrido pasa a ser aritmetica. Se rellena desde gl_constraints_tick, que YA
// tiene cada constraint en la mano cada paso de fisica, y solo cuando hace falta.
struct GLJuntaRigida { uint64_t p0, p1; };
inline LocalVector<GLJuntaRigida>& gl_juntas_rigidas() {
    static LocalVector<GLJuntaRigida> v;
    return v;
}
inline bool& gl_juntas_al_dia() { static bool b = false; return b; }

// Piezas que cuelgan del Workspace. Las juntas de un almacen (ReplicatedStorage,
// ServerStorage, un modelo guardado) no forman conjuntos: sin este filtro el
// agrupado sacaba 315 grupos en vez de 51 y luego habia que resolverlos todos
// para descartarlos. Cada pieza se apunta y se borra sola en ENTER/EXIT_TREE.
inline HashMap<uint64_t, bool>& gl_partes_workspace() {
    static HashMap<uint64_t, bool> m;
    return m;
}

inline void gl_ensamblajes_cambiaron() {
    gl_gen_ensamblajes()++;
    gl_ensamblajes_sucios() = true;
    gl_juntas_al_dia() = false;   // hay que volver a mirar quien esta unido a quien
}

// ── DOS TIPOS DE CAMBIO, Y NO CUESTAN LO MISMO ───────────────────────
// Rehacer los conjuntos es CARO: recorre todas las juntas, comprueba de quien
// desciende cada pieza, quita las formas de colision de las raices y las vuelve
// a poner. Con 2.077 soldaduras eso es un tiron entero, y se hacia en UN frame.
//
// Pero la mayoria de los avisos no cambian la forma de ningun conjunto:
//   · Cambiar Anchored NO reagrupa nada. Solo decide si la raiz se congela.
//     Y los scripts de Roblox escriben Anchored sin parar.
//   · Meter o sacar una pieza del arbol tampoco, salvo que lleve junta.
//
// Asi que se separan: `sucios` = hay que rehacerlo de verdad (topologia), y
// `anclaje_sucio` = solo hay que repasar quien se congela, que es barato y no
// toca la fisica salvo en el conjunto que de verdad cambio.
inline bool& gl_anclaje_sucio() { static bool s = false; return s; }
inline void gl_anclaje_conjunto_cambio() { gl_anclaje_sucio() = true; }

// Cuanto costo la ultima reconstruccion y la peor vista (microsegundos), para
// poder mirarlo desde una prueba sin adivinar.
// Coste de NUESTRA parte del paso de fisica (el tick central de constraints),
// separado del trabajo del solver. Ver _physics_process en roblox_workspace.h.
struct GLPasoStats {
    uint64_t pasos = 0;
    uint64_t constraints_us = 0, constraints_acum = 0, peor_constraints_us = 0;
    uint64_t despiertos = 0;      // constraints que NO estaban dormidos
    // Y lo que cuesta nuestro trabajo del frame normal (_process del Workspace),
    // troceado. Es la otra mitad del frame.
    uint64_t proc_us = 0, stream_us = 0, caidas_us = 0, conj_us = 0;
    // Cuantas veces se ha invocado el relay de DescendantAdded/Removing. Salta
    // por CADA nodo que entra o sale del arbol y por cada conexion viva, asi que
    // es lo primero que hay que mirar si el frame se alarga.
    uint64_t desc_llamadas = 0, desc_despachos = 0;
    // Manejadores de Changed / GetPropertyChangedSignal que se REANUDAN de
    // verdad. Antes de cablearlos, esto era 0 siempre: las señales existian pero
    // no saltaban, o sea que el juego NO corria ese codigo suyo.
    uint64_t cambios_disparados = 0;
    // Lo que tarda el codigo LUAU DEL JUEGO por frame, por señal. Es la otra
    // mitad grande del frame (medido: nuestro _process del Workspace son 0,4 ms
    // de 67 — todo lo demas es esto y el dibujado).
    uint64_t hb_us = 0, rs_us = 0, st_us = 0;
    uint32_t hb_n = 0, rs_n = 0, st_n = 0;   // conectados vivos en cada lista
    uint64_t hover_us = 0;                    // tick del hover central de GUI
    // Frame al que corresponden las cifras de arriba: se SUMAN dentro del mismo
    // frame (puede haber varios RunService) y se ponen a cero al cambiar.
    uint64_t frame_proc = 0, frame_phys = 0;
};
inline GLPasoStats& gl_paso_stats() { static GLPasoStats s; return s; }

struct GLAsmStats {
    uint64_t ultima_us = 0, peor_us = 0, veces = 0;
    // Reparto de la ultima pasada, por fases, para no tener que adivinar donde
    // se va el tiempo.
    uint64_t f1_union = 0, f2_grupos = 0, f3_huellas = 0, f4_quitar = 0, f5_montar = 0;
    uint64_t n_juntas = 0, n_grupos = 0, n_intactos = 0, n_rehechos = 0;
};
inline GLAsmStats& gl_asm_stats() { static GLAsmStats s; return s; }

// id de pieza -> id de la raiz de SU conjunto. Solo aparecen las piezas que
// pertenecen a un conjunto de dos o mas; una pieza suelta no esta aqui.
inline HashMap<uint64_t, uint64_t>& gl_raiz_de_pieza() {
    static HashMap<uint64_t, uint64_t> m;
    return m;
}
// id de la raiz -> todos sus miembros (la raiz incluida, siempre la primera).
inline HashMap<uint64_t, LocalVector<uint64_t>>& gl_miembros_de_raiz() {
    static HashMap<uint64_t, LocalVector<uint64_t>> m;
    return m;
}

// La raiz del conjunto de esta pieza. Si no esta en ninguno, es ella misma:
// en Roblox una pieza suelta TAMBIEN es un assembly, uno de un solo miembro, y
// su AssemblyRootPart es ella.
inline uint64_t gl_raiz_ensamblaje(uint64_t pieza) {
    if (const uint64_t* r = gl_raiz_de_pieza().getptr(pieza)) return *r;
    return pieza;
}

inline void gl_ensamblajes_limpiar() {
    gl_raiz_de_pieza().clear();
    gl_miembros_de_raiz().clear();
}

#endif // GL_ASSEMBLY_H

#ifndef ROBLOX_CONSTRAINTS_H
#define ROBLOX_CONSTRAINTS_H

// ════════════════════════════════════════════════════════════════════
//  Constraints — equivalents to Roblox's Constraints
////
//  Constraints — equivalentes a los Constraints de Roblox
//
//  WeldConstraint       — Welds two parts together (no movement) / Suelda dos partes juntas (sin movimiento)
//  HingeConstraint      — Hinge between two parts (one free axis) / Bisagra entre dos partes (un eje libre)
//  BallSocketConstraint — Ball joint (multiple axes) / Rótula (múltiples ejes)
//  RodConstraint        — Fixed distance between two parts / Distancia fija entre dos partes
//  SpringConstraint     — Spring between two parts / Resorte entre dos partes
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/classes/hinge_joint3d.hpp>
#include <godot_cpp/classes/generic6_dof_joint3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <vector>

#include <godot_cpp/classes/pin_joint3d.hpp>
#include "gl_runtime.h"   // gl_gen_anclajes: dormir los constraints sin trabajo
#include "gl_assembly.h"  // conjuntos rigidos estilo Roblox
#include "roblox_bodymovers.h"   // gl_cuerpo_que_mueve: la raiz de un conjunto
#include "roblox_attachment.h"   // el eje de una bisagra es el de su Attachment0

using namespace godot;

// ────────────────────────────────────────────────────────────────────
//  Referencias SEGURAS a Part0/Part1 (1.16.13)
//
//  Estas clases guardaban `Node*` CRUDOS. En cuanto el juego destruye una parte
//  unida (un objeto que se agarra, un NPC que muere, un modelo que se borra) el
//  puntero queda COLGANDO — y el `if (!part0 || !part1) return;` no protege de
//  nada, porque el puntero no es nulo: es basura. El siguiente _physics_process
//  hacia Object::cast_to sobre memoria ya liberada y el proceso se caia.
//
//  Era el segfault INTERMITENTE del place real (1 de cada 3 partidas), con la
//  traza acabando en WeldConstraint::_physics_process.
//
//  Se guarda el ObjectID y se resuelve por ObjectDB en cada uso, que es lo que
//  ya hace el resto del motor (ver gl_runtime.h). Si el nodo murio, la
//  resolucion da nullptr y las guardas de siempre funcionan de verdad.
// ────────────────────────────────────────────────────────────────────
inline uint64_t gl_part_id(Node* n) { return n ? (uint64_t)n->get_instance_id() : 0; }
// ¿Estan las dos partes DENTRO del arbol? Un modelo soldado recien clonado (o
// guardado en ReplicatedStorage) tiene sus partes fuera, y pedirles el
// transform global solo imprime "Condition !is_inside_tree()" por consola. Con
// 3781 WeldConstraint en un place normal, eso es un chorro de ruido.
inline bool gl_partes_en_arbol(Node* a, Node* b) {
    Node3D* n0 = gl_cast<Node3D>(a);
    Node3D* n1 = gl_cast<Node3D>(b);
    return n0 && n1 && n0->is_inside_tree() && n1->is_inside_tree();
}
inline Node* gl_part_node(uint64_t id) {
    if (id == 0) return nullptr;
    Object* o = ObjectDB::get_instance(id);
    return o ? gl_cast<Node>(o) : nullptr;
}

// ¿Las dos partes estan QUIETAS? Un weld entre dos piezas ancladas no tiene
// nada que hacer: la relativa ya se cumple y se cumplira siempre. Es el caso
// mayoritario en un mapa importado (la geometria del escenario va soldada y
// anclada), asi que saltarselo quita casi todo el trabajo.
inline bool gl_partes_quietas(Node* a, Node* b) {
    RigidBody3D* r0 = gl_cast<RigidBody3D>(a);
    RigidBody3D* r1 = gl_cast<RigidBody3D>(b);
    // Si alguna no es un cuerpo rigido, puede moverla un script: no se salta.
    if (!r0 || !r1) return false;
    return r0->is_freeze_enabled() && r1->is_freeze_enabled();
}

// Suelta la pieza que congelo una soldadura al romperse o apagarse. Si esta en
// un conjunto, la congelacion es del conjunto y la deshace su reconstruccion
// (uno o dos frames despues): soltarla aqui la dejaba cayendo sin forma hasta
// entonces, o tiraba al conjunto entero si era su raiz.
inline void gl_soltar_soldada(RobloxPart* p) {
    if (!p || !p->is_inside_tree()) return;
    if (gl_raiz_de_pieza().has((uint64_t)p->get_instance_id())) return;
    p->set_freeze_enabled(p->get_anchored());
}

// ────────────────────────────────────────────────────────────────────
//  Los dos extremos de un constraint de Roblox: sus Attachment0/1. La pieza es
//  el padre del attachment y el punto es el propio attachment. Antes todos los
//  constraints iban de centro a centro de Part0/Part1 y no sabian nada de
//  attachments, asi que una bisagra de puerta giraba sobre el punto medio de
//  las dos piezas. Sin attachments sigue valiendo Part0/Part1 (la API vieja).
// ────────────────────────────────────────────────────────────────────
struct GLExtremo {
    Node3D*  pieza = nullptr;   // la que se mueve (el padre del attachment)
    Node3D*  att   = nullptr;   // el attachment, si lo hay
    Vector3  punto;             // donde actua, en mundo
};
inline GLExtremo gl_extremo(const Node* c, int cual, uint64_t part_id_viejo) {
    static const StringName k0s("_glo_Attachment0"), k0i("Attachment0");
    static const StringName k1s("_glo_Attachment1"), k1i("Attachment1");
    GLExtremo e;
    Node* a = (cual == 0) ? gl_ref_de(c, k0s, k0i) : gl_ref_de(c, k1s, k1i);
    if (Node3D* a3 = gl_cast<Node3D>(a)) {
        e.att = a3;
        e.pieza = gl_cast<Node3D>(a3->get_parent());
        if (a3->is_inside_tree()) e.punto = a3->get_global_position();
        return e;
    }
    e.pieza = gl_cast<Node3D>(gl_part_node(part_id_viejo));
    if (e.pieza && e.pieza->is_inside_tree()) e.punto = e.pieza->get_global_position();
    return e;
}
// Part0/Part1 de un place importado: el importador no tiene donde ponerlas y
// las deja como metas NodePath, que nadie leia. Medido en el place de prueba:
// 682 de 936 WeldConstraint no soldaban nada.
inline void gl_resolver_importadas(const Node* c, uint64_t& part0_id, uint64_t& part1_id) {
    static const StringName k0s("_glo_Part0"), k0i("Part0"), k1s("_glo_Part1"), k1i("Part1");
    if (!part0_id) if (Node* p = gl_ref_de(c, k0s, k0i)) part0_id = gl_part_id(p);
    if (!part1_id) if (Node* p = gl_ref_de(c, k1s, k1i)) part1_id = gl_part_id(p);
}

inline bool gl_extremos_listos(const GLExtremo& a, const GLExtremo& b) {
    return a.pieza && b.pieza && a.pieza != b.pieza && a.pieza->is_inside_tree() && b.pieza->is_inside_tree();
}

// Distancia entre dos puntos de dos cuerpos: fija (Rod) o con tope por arriba
// (Rope). Se corrige la velocidad relativa a lo largo de la linea con un impulso
// en cada punto, como un solver de restricciones: el giro sale solo (un
// pendulo oscila). Antes el Rod TELETRANSPORTABA las piezas: atravesaban cosas
// y no tenian inercia.
inline void gl_restriccion_distancia(const GLExtremo& e0, const GLExtremo& e1, float largo, bool solo_tensa, double delta) {
    RigidBody3D* b0 = gl_cuerpo_que_mueve(e0.pieza);
    RigidBody3D* b1 = gl_cuerpo_que_mueve(e1.pieza);
    const bool m0 = b0 && !b0->is_freeze_enabled();
    const bool m1 = b1 && !b1->is_freeze_enabled();
    if (!m0 && !m1) return;
    const Vector3 d = e1.punto - e0.punto;
    const float dist = d.length();
    if (dist < 1e-5f) return;
    const Vector3 n = d / dist;
    const float error = dist - largo;
    if (solo_tensa && error <= 0.0f) return;   // cuerda floja: no hace nada
    const float dt = (float)Math::max(delta, 0.0001);
    // Con la gravedad de este paso ya sumada: el solver la añade DESPUES de esta
    // correccion, y sin contarla la cuerda cedia g*dt²/0,2 en reposo (0,27 studs
    // a 60 Hz).
    auto vel_en = [dt](RigidBody3D* b, const Vector3& p) {
        return b->get_linear_velocity() + b->get_gravity() * b->get_gravity_scale() * dt
             + b->get_angular_velocity().cross(p - b->get_global_position());
    };
    const Vector3 v0 = m0 ? vel_en(b0, e0.punto) : Vector3();
    const Vector3 v1 = m1 ? vel_en(b1, e1.punto) : Vector3();
    const float w0 = m0 ? 1.0f / Math::max(b0->get_mass(), 0.001f) : 0.0f;
    const float w1 = m1 ? 1.0f / Math::max(b1->get_mass(), 0.001f) : 0.0f;
    float lambda = -((v1 - v0).dot(n) + 0.2f * error / dt) / (w0 + w1);
    if (solo_tensa && lambda > 0.0f) return;   // una cuerda solo tira
    const Vector3 j = n * lambda;
    if (m0) b0->apply_impulse(-j, e0.punto - b0->get_global_position());
    if (m1) b1->apply_impulse(j, e1.punto - b1->get_global_position());
}

// ════════════════════════════════════════════════════════════════════
//  TICK CENTRAL DE CONSTRAINTS (v1.17) — rendimiento
//
//  El problema, MEDIDO en el place de prueba: 2332 WeldConstraint, cada uno con
//  su propio _physics_process. En una GDExtension cada una de esas llamadas
//  cruza la frontera C++/Godot (lookup + marshalling), o sea ~140.000 cruces
//  por segundo a 60 Hz solo para mover unas matrices. Apagarlos daba +35% de
//  FPS, y era el mayor bocado del frame despues de la fisica en si.
//
//  Aqui se registran todos y el Workspace los recorre desde UN SOLO
//  _physics_process, llamando a un metodo C++ normal. Mismo comportamiento,
//  una llamada al motor en vez de miles.
// ════════════════════════════════════════════════════════════════════
class GLConstraintBase : public Node {
    GDCLASS(GLConstraintBase, Node);
protected:
    static void _bind_methods() {}
public:
    // Lo implementa cada constraint. Se llama desde el tick central.
    virtual void gl_tick(double delta) { (void)delta; }

    // ── DORMIR ────────────────────────────────────────────────────────
    // Un weld entre dos piezas ancladas no tiene nada que hacer nunca mas: la
    // relativa ya se cumple y no puede dejar de cumplirse. Pero AVERIGUARLO
    // costaba 6 Object::cast_to y 4 llamadas de propiedad en cada paso de
    // fisica (ver gl_gen_anclajes en gl_runtime.h). Cuando un constraint
    // descubre que no tiene trabajo se duerme, y solo se despierta si alguna
    // pieza del mapa cambia de Anchored o entra/sale del arbol.
    bool     gl_dormido = false;
    uint32_t gl_gen_visto = 0;
    void gl_dormir() { gl_dormido = true; gl_gen_visto = gl_gen_anclajes(); }
    bool gl_sigue_dormido() const { return gl_dormido && gl_gen_visto == gl_gen_anclajes(); }

    // ── Juntas RIGIDAS: las que forman conjuntos (assemblies) ─────────
    // En Roblox son Weld, WeldConstraint, Motor6D y Snap: las piezas que unen
    // pasan a ser UN solo cuerpo. Una bisagra o un muelle NO (dejan libertad de
    // movimiento, cada lado sigue siendo su propio cuerpo).
    virtual bool gl_es_rigida() const { return false; }
    virtual uint64_t gl_id_part0() const { return 0; }
    virtual uint64_t gl_id_part1() const { return 0; }
};

inline std::vector<uint64_t>& gl_constraint_ids() {
    static std::vector<uint64_t> v;
    return v;
}
// Indice de pertenencia. Antes esta alta recorria la lista ENTERA buscando
// duplicados: dar de alta N constraints era N²/2 comparaciones, y al importar el
// place son 5.336 — 14 millones de vueltas, todas en el mismo frame.
// Cuantos constraints trabajaron de verdad en el ultimo paso (los dormidos no
// cuentan). Es la mitad de la respuesta a "¿el tiron es nuestro o del solver?".
inline uint64_t& gl_constraints_despiertos() { static uint64_t n = 0; return n; }
inline HashMap<uint64_t, bool>& gl_constraint_dentro() {
    static HashMap<uint64_t, bool> m;
    return m;
}
inline void gl_constraint_register(Node* n) {
    if (!n) return;
    const uint64_t id = (uint64_t)n->get_instance_id();
    if (gl_constraint_dentro().has(id)) return;
    gl_constraint_dentro()[id] = true;
    gl_constraint_ids().push_back(id);
}
inline void gl_constraint_unregister(Node* n) {
    if (!n) return;
    const uint64_t id = (uint64_t)n->get_instance_id();
    if (!gl_constraint_dentro().has(id)) return;
    gl_constraint_dentro().erase(id);
    std::vector<uint64_t>& v = gl_constraint_ids();
    for (size_t i = 0; i < v.size(); i++)
        if (v[i] == id) { v[i] = v.back(); v.pop_back(); return; }
}
// ── Las piezas soldadas no chocan entre si ────────────────────────────
// En Roblox dos piezas unidas por una junta rigida son UN cuerpo desde el primer
// paso. Aqui lo son cuando se monta el conjunto (_reconstruir_conjuntos, como
// mucho cada 0,2 s); hasta entonces eran dos cuerpos y, si nacian solapadas (lo
// normal al soldar por script: Instance.new en el mismo sitio y C0 despues), Jolt
// las separaba de un golpe y el conjunto caia girado. Cada par rigido lleva una
// excepcion de colision desde el paso en que aparece en el espejo, y la pierde
// cuando la junta desaparece o se apaga.
inline uint64_t gl_par_clave(uint64_t a, uint64_t b) {
    return (a * 0x9E3779B97F4A7C15ULL) ^ (b + 0x632BE59BD9B4E019ULL + (a << 6) + (a >> 2));
}
inline void gl_excepcion_par(uint64_t a, uint64_t b, bool poner) {
    PhysicsBody3D* x = gl_cast<PhysicsBody3D>(ObjectDB::get_instance(ObjectID(a)));
    PhysicsBody3D* y = gl_cast<PhysicsBody3D>(ObjectDB::get_instance(ObjectID(b)));
    if (!x || !y || x == y) return;
    if (poner) x->add_collision_exception_with(y);
    else x->remove_collision_exception_with(y);
}
inline void gl_excepciones_soldadas(const LocalVector<GLJuntaRigida>& jr) {
    static HashMap<uint64_t, GLJuntaRigida> puestas;
    HashMap<uint64_t, GLJuntaRigida> ahora;
    for (const GLJuntaRigida& j : jr) {
        const uint64_t a = MIN(j.p0, j.p1), b = MAX(j.p0, j.p1);
        if (a == b) continue;
        const uint64_t k = gl_par_clave(a, b);
        if (ahora.has(k)) continue;
        GLJuntaRigida p;
        p.p0 = a;
        p.p1 = b;
        ahora.insert(k, p);
        if (!puestas.has(k)) gl_excepcion_par(a, b, true);
    }
    for (const KeyValue<uint64_t, GLJuntaRigida>& e : puestas)
        if (!ahora.has(e.key)) gl_excepcion_par(e.value.p0, e.value.p1, false);
    puestas = ahora;
}

// Recorre la lista una vez por paso de fisica. Los que ya no existen se caen de
// la lista aqui mismo (compactado in situ, sin recorrerla dos veces).
inline void gl_constraints_tick(double delta) {
    // Una sola consulta por paso en vez de una por constraint: con 2.077 welds y
    // 1.420 Motor6D eran ~3.500 cruces de la frontera GDExtension por paso, solo
    // para preguntar lo mismo. Cada gl_tick ya no la repite.
    if (Engine::get_singleton()->is_editor_hint()) return;
    std::vector<uint64_t>& v = gl_constraint_ids();
    // Aprovechando que aqui ya se resuelve y se castea CADA constraint, se
    // rellena de paso el espejo de juntas rigidas (ver gl_assembly.h). Solo
    // cuando esta pendiente: no en cada paso.
    const bool refrescar = !gl_juntas_al_dia();
    LocalVector<GLJuntaRigida>& jr = gl_juntas_rigidas();
    if (refrescar) jr.clear();
    size_t escribe = 0;
    uint64_t despiertos = 0;
    for (size_t i = 0; i < v.size(); i++) {
        Object* o = ObjectDB::get_instance(v[i]);
        GLConstraintBase* c = o ? gl_cast<GLConstraintBase>(o) : nullptr;
        if (!c) { gl_constraint_dentro().erase(v[i]); continue; }   // murio
        v[escribe++] = v[i];
        if (refrescar && c->gl_es_rigida()) {
            const uint64_t a = c->gl_id_part0(), b = c->gl_id_part1();
            if (a && b) { GLJuntaRigida j; j.p0 = a; j.p1 = b; jr.push_back(j); }
        }
        // Dos comparaciones en vez de 6 casts con RTTI y 4 llamadas de propiedad.
        if (c->gl_sigue_dormido()) continue;
        despiertos++;
        c->gl_tick(delta);
    }
    v.resize(escribe);
    gl_constraints_despiertos() = despiertos;
    if (refrescar) {
        gl_juntas_al_dia() = true;
        gl_excepciones_soldadas(jr);
    }
}
inline void gl_constraints_clear() {
    gl_constraint_ids().clear();
    gl_constraint_dentro().clear();
    gl_juntas_rigidas().clear();
    gl_juntas_al_dia() = false;
}

// ────────────────────────────────────────────────────────────────────
//  WeldConstraint — Keeps Part1 glued to Part0
////
//  WeldConstraint — Mantiene Part1 pegado a Part0
// ────────────────────────────────────────────────────────────────────
class WeldConstraint : public GLConstraintBase {
    GDCLASS(WeldConstraint, GLConstraintBase);

    // Alta y baja en el tick central (ver arriba): un solo _physics_process para
    // todos, en vez de uno por constraint.
    void _notification(int p_what) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (p_what == NOTIFICATION_ENTER_TREE) {
            gl_resolver_importadas(this, part0_id, part1_id);
            gl_constraint_register(this);
            gl_ensamblajes_cambiaron();   // una soldadura nueva rehace el conjunto
        } else if (p_what == NOTIFICATION_EXIT_TREE) {
            gl_constraint_unregister(this);
            gl_ensamblajes_cambiaron();   // al romperla, las piezas se separan
            if (enabled) gl_soltar_soldada(gl_cast<RobloxPart>(gl_part_node(part1_id)));
        }
    }

    uint64_t     part0_id              = 0;
    uint64_t     part1_id              = 0;
    bool        enabled            = true;
    Transform3D relative_transform;   // Computed in _ready / Calculado en _ready
    bool        transform_captured = false;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_part0","n"),   &WeldConstraint::set_part0);
        ClassDB::bind_method(D_METHOD("get_part0"),        &WeldConstraint::get_part0);
        ClassDB::bind_method(D_METHOD("set_part1","n"),   &WeldConstraint::set_part1);
        ClassDB::bind_method(D_METHOD("get_part1"),        &WeldConstraint::get_part1);
        ClassDB::bind_method(D_METHOD("set_enabled","b"), &WeldConstraint::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),      &WeldConstraint::get_enabled);

        ADD_GROUP("WeldConstraint","");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Enabled"), "set_enabled","get_enabled");
    }

public:
    bool gl_es_rigida() const override { return enabled; }
    uint64_t gl_id_part0() const override { return part0_id; }
    uint64_t gl_id_part1() const override { return part1_id; }

    // gl_ensamblajes_cambiaron() tira `gl_juntas_al_dia` y obliga a REHACER el
    // espejo de juntas entero (recorrer las miles de constraints del place).
    // Fuera del arbol eso no vale para nada: la constraint todavia no esta
    // registrada y su ENTER_TREE ya avisa. Y es justo el caso comun — el idioma
    // de Roblox es Instance.new -> Part0/Part1 -> Parent, asi que un WeldAll de
    // 15 piezas invalidaba el espejo 30 veces por brainrot, en la oleada de
    // spawn de cada 3 s.
    void  _gl_avisar_si_montada() { if (is_inside_tree()) gl_ensamblajes_cambiaron(); }
    void  set_part0(Node* n)   { part0_id = gl_part_id(n); transform_captured = false; gl_dormido = false; _gl_avisar_si_montada(); }
    Node* get_part0() const    { return gl_part_node(part0_id); }
    void  set_part1(Node* n)   { part1_id = gl_part_id(n); transform_captured = false; gl_dormido = false; _gl_avisar_si_montada(); }
    Node* get_part1() const    { return gl_part_node(part1_id); }
    // Deshabilitar un weld tiene que SOLTAR la pieza: gl_es_rigida() devuelve
    // `enabled`, asi que sin avisar el espejo se quedaba desfasado y Part1
    // congelada (FREEZE STATIC) para siempre.
    void  set_enabled(bool b)  {
        if (enabled == b) return;
        enabled = b;
        transform_captured = false;
        gl_dormido = false;
        if (!b) gl_soltar_soldada(gl_cast<RobloxPart>(gl_part_node(part1_id)));
        _gl_avisar_si_montada();
    }
    bool  get_enabled() const  { return enabled; }

    void _ready() override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        _capture_relative_transform();
    }

    void _capture_relative_transform() {
        // Resolver por ObjectID: si la parte fue destruida, esto da nullptr
        // y la guarda de abajo SI corta (con el Node* crudo no cortaba nunca).
        Node* part0 = gl_part_node(part0_id);
        Node* part1 = gl_part_node(part1_id);
        if (!gl_partes_en_arbol(part0, part1)) return;
        if (!part0 || !part1) return;
        Node3D* n0 = gl_cast<Node3D>(part0);
        Node3D* n1 = gl_cast<Node3D>(part1);
        if (!n0 || !n1) return;
        // relative = part0_inv * part1
        relative_transform  = n0->get_global_transform().inverse() * n1->get_global_transform();
        transform_captured  = true;

        // Freeze Part1 so it has no independent dynamics
        //// Congelar Part1 para que no tenga dinámica propia
        RigidBody3D* rb1 = gl_cast<RigidBody3D>(part1);
        if (rb1) {
            // STATIC y no KINEMATIC: una pieza soldada NO tiene dinamica propia
            // (esa es la definicion de soldar), y en kinematico el simulador la
            // sigue procesando entera. Es el mismo motivo por el que las piezas
            // ancladas van en STATIC (ver roblox_part.h): con miles de piezas
            // soldadas, la diferencia entre un modo y otro es el juego entero.
            // El tick del weld le sigue escribiendo el transform igual.
            rb1->set_freeze_mode(RigidBody3D::FREEZE_MODE_STATIC);
            rb1->set_freeze_enabled(true);
        }
    }

    void gl_tick(double delta) override {
        if (Engine::get_singleton()->is_editor_hint() || !enabled) return;
        // Resolver por ObjectID: si la parte fue destruida, esto da nullptr
        // y la guarda de abajo SI corta (con el Node* crudo no cortaba nunca).
        Node* part0 = gl_part_node(part0_id);
        Node* part1 = gl_part_node(part1_id);
        if (!gl_partes_en_arbol(part0, part1)) return;
        if (!part0 || !part1) return;
        if (!transform_captured) { _capture_relative_transform(); return; }
        // Las dos ancladas: la relativa ya se cumple y no puede dejar de
        // cumplirse. Es el caso de casi toda la geometria de un mapa importado.
        // Se DUERME: el tick central ya no lo volvera a mirar hasta que alguna
        // pieza cambie de Anchored o entre/salga del arbol.
        if (gl_partes_quietas(part0, part1)) { gl_dormir(); return; }

        Node3D* n0 = gl_cast<Node3D>(part0);
        Node3D* n1 = gl_cast<Node3D>(part1);
        if (!n0 || !n1) return;

        // Apply the stored relative transform
        //// Aplicar la transformación relativa guardada
        //
        // OJO al early-out: escribir el transform NO es barato aunque el valor
        // sea el mismo. set_global_transform mueve el cuerpo en el broadphase,
        // lo DESPIERTA y propaga NOTIFICATION_TRANSFORM_CHANGED por todo el
        // subarbol. El place de prueba trae 2.077 welds, asi que eran ~2.000
        // escrituras de transform en CADA paso de fisica, casi todas para dejar
        // la pieza exactamente donde ya estaba. Comparar 12 floats sale gratis
        // al lado de eso. (Encaja con lo medido: el 92% de los frames que
        // duran el doble son frames con paso de fisica, y el paso EN SI mide
        // igual que en los normales — el coste esta a su alrededor.)
        const Transform3D objetivo = n0->get_global_transform() * relative_transform;
        if (!objetivo.is_equal_approx(n1->get_global_transform()))
            n1->set_global_transform(objetivo);

        // Cancel residual velocities
        //// Anular velocidades residuales
        RigidBody3D* rb1 = gl_cast<RigidBody3D>(part1);
        if (rb1 && !rb1->is_freeze_enabled()) {
            // Mismo motivo: poner la velocidad a cero en un cuerpo que ya esta
            // a cero es una llamada al servidor de fisica que ademas lo despierta.
            if (rb1->get_linear_velocity() != Vector3())  rb1->set_linear_velocity(Vector3());
            if (rb1->get_angular_velocity() != Vector3()) rb1->set_angular_velocity(Vector3());
        }
    }
};

// ────────────────────────────────────────────────────────────────────
//  Weld (y ManualWeld / Snap) — la soldadura clasica de Roblox: Part1 queda en
//  Part0.CFrame * C0 * C1:Inverse(). No existia: Instance.new("Weld") daba nil
//  (el patron de casi toda herramienta y accesorio hecho por script), y un
//  WeldConstraint no sirve de sustituto porque se queda con la posicion de ese
//  momento y no sabe nada de C0/C1. Es rigida: entra en los conjuntos.
// ────────────────────────────────────────────────────────────────────
class Weld : public GLConstraintBase {
    GDCLASS(Weld, GLConstraintBase);

    void _notification(int p_what) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (p_what == NOTIFICATION_ENTER_TREE) {
            gl_resolver_importadas(this, part0_id, part1_id);
            gl_constraint_register(this);
            gl_ensamblajes_cambiaron();
        } else if (p_what == NOTIFICATION_EXIT_TREE) {
            gl_constraint_unregister(this);
            gl_ensamblajes_cambiaron();
            _soltar_part1();
        }
    }

    uint64_t    part0_id = 0;
    uint64_t    part1_id = 0;
    Transform3D c0;
    Transform3D c1;
    bool        enabled  = true;
    bool        colocada = false;   // Part1 ya se puso en su sitio

    // Al romperse, Part1 vuelve a tener su propia fisica (si no esta anclada).
    void _soltar_part1() {
        gl_soltar_soldada(gl_cast<RobloxPart>(gl_part_node(part1_id)));
        colocada = false;
    }
    void _cambio() {
        colocada = false;
        gl_dormido = false;
        if (is_inside_tree()) gl_ensamblajes_cambiaron();
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_part0","n"),   &Weld::set_part0);
        ClassDB::bind_method(D_METHOD("get_part0"),        &Weld::get_part0);
        ClassDB::bind_method(D_METHOD("set_part1","n"),   &Weld::set_part1);
        ClassDB::bind_method(D_METHOD("get_part1"),        &Weld::get_part1);
        ClassDB::bind_method(D_METHOD("set_c0","t"),      &Weld::set_c0);
        ClassDB::bind_method(D_METHOD("get_c0"),           &Weld::get_c0);
        ClassDB::bind_method(D_METHOD("set_c1","t"),      &Weld::set_c1);
        ClassDB::bind_method(D_METHOD("get_c1"),           &Weld::get_c1);
        ClassDB::bind_method(D_METHOD("set_enabled","b"), &Weld::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),      &Weld::get_enabled);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,        "Enabled"), "set_enabled", "get_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "C0"),      "set_c0", "get_c0");
        ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "C1"),      "set_c1", "get_c1");
    }

public:
    bool gl_es_rigida() const override { return enabled; }
    uint64_t gl_id_part0() const override { return part0_id; }
    uint64_t gl_id_part1() const override { return part1_id; }

    void  set_part0(Node* n)               { part0_id = gl_part_id(n); _cambio(); }
    Node* get_part0() const                { return gl_part_node(part0_id); }
    void  set_part1(Node* n)               { if (part1_id && n != gl_part_node(part1_id)) _soltar_part1(); part1_id = gl_part_id(n); _cambio(); }
    Node* get_part1() const                { return gl_part_node(part1_id); }
    void  set_c0(const Transform3D& t)     { c0 = t; _cambio(); }
    Transform3D get_c0() const             { return c0; }
    void  set_c1(const Transform3D& t)     { c1 = t; _cambio(); }
    Transform3D get_c1() const             { return c1; }
    void  set_enabled(bool b) {
        if (enabled == b) return;
        enabled = b;
        if (!b) _soltar_part1();
        _cambio();
    }
    bool  get_enabled() const              { return enabled; }

    void gl_tick(double delta) override {
        (void)delta;
        if (Engine::get_singleton()->is_editor_hint() || !enabled) return;
        Node3D* n0 = gl_cast<Node3D>(gl_part_node(part0_id));
        Node3D* n1 = gl_cast<Node3D>(gl_part_node(part1_id));
        if (!gl_partes_en_arbol(n0, n1)) return;
        const Transform3D objetivo = n0->get_global_transform() * c0 * c1.affine_inverse();
        if (!colocada) {
            // Part1 salta a su sitio, sin dinamica propia (la lleva el conjunto),
            // y el conjunto se rehace con la posicion buena.
            colocada = true;
            n1->set_global_transform(objetivo);
            if (RigidBody3D* rb1 = gl_cast<RigidBody3D>(n1)) {
                rb1->set_freeze_mode(RigidBody3D::FREEZE_MODE_STATIC);
                rb1->set_freeze_enabled(true);
            }
            gl_ensamblajes_cambiaron();
            return;
        }
        if (gl_partes_quietas(n0, n1)) { gl_dormir(); return; }
        if (!objetivo.is_equal_approx(n1->get_global_transform()))
            n1->set_global_transform(objetivo);
    }
};

// Base de orientacion de una junta: Z = el eje (asi lo lee Godot en la bisagra).
inline Basis gl_base_con_eje_z(const Vector3& eje) {
    const Vector3 z = eje.length_squared() > 1e-8f ? eje.normalized() : Vector3(0, 0, 1);
    const Vector3 ref = Math::abs(z.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
    const Vector3 x = ref.cross(z).normalized();
    return Basis(x, z.cross(x), z);
}
// El eje principal de un extremo: el de su attachment (Attachment.Axis, en el
// espacio de su pieza; por defecto su eje X), o el X de la pieza.
inline Vector3 gl_eje_extremo(const GLExtremo& e) {
    if (Attachment* a = gl_cast<Attachment>(e.att)) {
        if (a->get_axis() != Vector3(1, 0, 0) && e.pieza)
            return (e.pieza->get_global_transform().basis.xform(a->get_axis())).normalized();
        return a->get_global_transform().basis.get_column(0).normalized();
    }
    return e.pieza ? e.pieza->get_global_transform().basis.get_column(0).normalized() : Vector3(1, 0, 0);
}
// Lo mismo con el SecondaryAxis (su eje Y).
inline Vector3 gl_eje2_extremo(const GLExtremo& e) {
    if (Attachment* a = gl_cast<Attachment>(e.att)) {
        if (a->get_secondary_axis() != Vector3(0, 1, 0) && e.pieza)
            return (e.pieza->get_global_transform().basis.xform(a->get_secondary_axis())).normalized();
        return a->get_global_transform().basis.get_column(1).normalized();
    }
    return e.pieza ? e.pieza->get_global_transform().basis.get_column(1).normalized() : Vector3(0, 1, 0);
}
// El angulo de Roblox entre dos SecondaryAxis, alrededor del eje (mano
// derecha). 0 si alguno va a lo largo del eje y no hay plano en que medirlo.
inline float gl_angulo_entre(const Vector3& eje, const Vector3& s0, const Vector3& s1) {
    const Vector3 p0 = s0 - eje * s0.dot(eje), p1 = s1 - eje * s1.dot(eje);
    if (p0.length_squared() < 1e-6f || p1.length_squared() < 1e-6f) return 0.0f;
    return Math::atan2(eje.dot(p0.cross(p1)), p0.dot(p1));
}

// ────────────────────────────────────────────────────────────────────
//  HingeConstraint — Bisagra: gira solo alrededor del eje de su Attachment0.
//  Se monta en el punto de Attachment0 (antes en el punto medio de las dos
//  piezas, asi que una puerta giraba por su centro) y en el tick central, asi
//  que funciona aunque los attachments se pongan despues de meterla al mundo.
//  ActuatorType de Roblox: Motor (AngularVelocity + MotorMaxTorque) o Servo
//  (TargetAngle + AngularSpeed + ServoMaxTorque).
// ────────────────────────────────────────────────────────────────────
class HingeConstraint : public GLConstraintBase {
    GDCLASS(HingeConstraint, GLConstraintBase);

    void _notification(int p_what) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (p_what == NOTIFICATION_ENTER_TREE)      gl_constraint_register(this);
        else if (p_what == NOTIFICATION_EXIT_TREE)  { gl_constraint_unregister(this); _soltar_junta(); }
    }

    uint64_t part0_id = 0;
    uint64_t part1_id = 0;
    bool   enabled        = true;
    bool   limits_enabled = false;
    float  upper_angle    = 45.0f;
    float  lower_angle    = -45.0f;
    int    actuator       = 0;        // Enum.ActuatorType: 0 None, 1 Motor, 2 Servo
    float  angular_velocity = 0.0f;   // Motor, rad/s
    float  motor_max_torque = 0.0f;
    float  target_angle   = 0.0f;     // Servo, grados
    float  angular_speed  = 0.0f;     // Servo, rad/s
    float  servo_max_torque = 0.0f;
    float  current_angle  = 0.0f;     // grados

    HingeJoint3D* godot_joint = nullptr;
    uint64_t hecho_a = 0;             // con que cuerpos se armo la junta
    uint64_t hecho_b = 0;
    Basis    rel_inicial;             // giro relativo de B respecto de A al armarla
    Vector3  eje_local_a;             // el eje en el espacio de A
    float    angulo_inicial = 0.0f;   // CurrentAngle al armarla, en radianes

    void _soltar_junta() {
        if (godot_joint) { godot_joint->queue_free(); godot_joint = nullptr; }
        hecho_a = hecho_b = 0;
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_part0","n"),          &HingeConstraint::set_part0);
        ClassDB::bind_method(D_METHOD("get_part0"),               &HingeConstraint::get_part0);
        ClassDB::bind_method(D_METHOD("set_part1","n"),          &HingeConstraint::set_part1);
        ClassDB::bind_method(D_METHOD("get_part1"),               &HingeConstraint::get_part1);
        ClassDB::bind_method(D_METHOD("set_enabled","b"),        &HingeConstraint::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),             &HingeConstraint::get_enabled);
        ClassDB::bind_method(D_METHOD("set_limits_enabled","b"), &HingeConstraint::set_limits_enabled);
        ClassDB::bind_method(D_METHOD("get_limits_enabled"),      &HingeConstraint::get_limits_enabled);
        ClassDB::bind_method(D_METHOD("set_upper_angle","a"),    &HingeConstraint::set_upper_angle);
        ClassDB::bind_method(D_METHOD("get_upper_angle"),         &HingeConstraint::get_upper_angle);
        ClassDB::bind_method(D_METHOD("set_lower_angle","a"),    &HingeConstraint::set_lower_angle);
        ClassDB::bind_method(D_METHOD("get_lower_angle"),         &HingeConstraint::get_lower_angle);
        ClassDB::bind_method(D_METHOD("set_actuator_type","t"),  &HingeConstraint::set_actuator_type);
        ClassDB::bind_method(D_METHOD("get_actuator_type"),       &HingeConstraint::get_actuator_type);
        ClassDB::bind_method(D_METHOD("set_angular_velocity","v"), &HingeConstraint::set_angular_velocity);
        ClassDB::bind_method(D_METHOD("get_angular_velocity"),     &HingeConstraint::get_angular_velocity);
        ClassDB::bind_method(D_METHOD("set_motor_max_torque","t"), &HingeConstraint::set_motor_max_torque);
        ClassDB::bind_method(D_METHOD("get_motor_max_torque"),     &HingeConstraint::get_motor_max_torque);
        ClassDB::bind_method(D_METHOD("set_target_angle","a"),   &HingeConstraint::set_target_angle);
        ClassDB::bind_method(D_METHOD("get_target_angle"),        &HingeConstraint::get_target_angle);
        ClassDB::bind_method(D_METHOD("set_angular_speed","v"),  &HingeConstraint::set_angular_speed);
        ClassDB::bind_method(D_METHOD("get_angular_speed"),       &HingeConstraint::get_angular_speed);
        ClassDB::bind_method(D_METHOD("set_servo_max_torque","t"), &HingeConstraint::set_servo_max_torque);
        ClassDB::bind_method(D_METHOD("get_servo_max_torque"),     &HingeConstraint::get_servo_max_torque);
        ClassDB::bind_method(D_METHOD("get_current_angle"),       &HingeConstraint::get_current_angle);
        // Nombres viejos de GodotLuau (antes de los de Roblox): se siguen aceptando.
        ClassDB::bind_method(D_METHOD("set_motor_enabled","b"),  &HingeConstraint::set_motor_enabled);
        ClassDB::bind_method(D_METHOD("get_motor_enabled"),       &HingeConstraint::get_motor_enabled);

        ADD_GROUP("HingeConstraint","");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Enabled"),        "set_enabled","get_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "LimitsEnabled"),  "set_limits_enabled","get_limits_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "UpperAngle", PROPERTY_HINT_RANGE,"-180,180,0.1"), "set_upper_angle","get_upper_angle");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "LowerAngle", PROPERTY_HINT_RANGE,"-180,180,0.1"), "set_lower_angle","get_lower_angle");
        ADD_PROPERTY(PropertyInfo(Variant::INT,   "ActuatorType", PROPERTY_HINT_ENUM, "None,Motor,Servo"), "set_actuator_type","get_actuator_type");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "AngularVelocity"), "set_angular_velocity","get_angular_velocity");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "MotorMaxTorque"),  "set_motor_max_torque","get_motor_max_torque");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "TargetAngle"),     "set_target_angle","get_target_angle");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "AngularSpeed"),    "set_angular_speed","get_angular_speed");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ServoMaxTorque"),  "set_servo_max_torque","get_servo_max_torque");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "MotorEnabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_motor_enabled","get_motor_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "CurrentAngle", PROPERTY_HINT_NONE, "",
                                  PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "","get_current_angle");
    }

public:
    void  set_part0(Node* n)            { part0_id = gl_part_id(n); _soltar_junta(); }
    Node* get_part0() const             { return gl_part_node(part0_id); }
    void  set_part1(Node* n)            { part1_id = gl_part_id(n); _soltar_junta(); }
    Node* get_part1() const             { return gl_part_node(part1_id); }
    void  set_enabled(bool b)           { enabled = b; if (!b) _soltar_junta(); }
    bool  get_enabled() const           { return enabled; }
    void  set_limits_enabled(bool b)    { limits_enabled = b; _actualizar(); }
    bool  get_limits_enabled() const    { return limits_enabled; }
    void  set_upper_angle(float a)      { upper_angle = a; _actualizar(); }
    float get_upper_angle() const       { return upper_angle; }
    void  set_lower_angle(float a)      { lower_angle = a; _actualizar(); }
    float get_lower_angle() const       { return lower_angle; }
    void  set_actuator_type(int t)      { actuator = CLAMP(t, 0, 2); _actualizar(); }
    int   get_actuator_type() const     { return actuator; }
    void  set_angular_velocity(float v) { angular_velocity = v; }
    float get_angular_velocity() const  { return angular_velocity; }
    void  set_motor_max_torque(float t) { motor_max_torque = Math::max(t, 0.0f); }
    float get_motor_max_torque() const  { return motor_max_torque; }
    void  set_target_angle(float a)     { target_angle = a; }
    float get_target_angle() const      { return target_angle; }
    void  set_angular_speed(float v)    { angular_speed = Math::max(v, 0.0f); }
    float get_angular_speed() const     { return angular_speed; }
    void  set_servo_max_torque(float t) { servo_max_torque = Math::max(t, 0.0f); }
    float get_servo_max_torque() const  { return servo_max_torque; }
    float get_current_angle() const     { return current_angle; }
    void  set_motor_enabled(bool b)     { actuator = b ? 1 : 0; _actualizar(); }
    bool  get_motor_enabled() const     { return actuator == 1; }

    void _actualizar() {
        if (!godot_joint) return;
        godot_joint->set_flag(HingeJoint3D::FLAG_USE_LIMIT, limits_enabled);
        if (limits_enabled) {
            // El angulo de la junta de Godot es el de Roblox con el signo
            // cambiado (ver el motor en gl_tick) y contado desde que se armo:
            // CurrentAngle en [Lower, Upper] es la junta en [a0-Upper, a0-Lower].
            const double a0 = (double)angulo_inicial;
            godot_joint->set_param(HingeJoint3D::PARAM_LIMIT_UPPER, a0 - Math::deg_to_rad((double)lower_angle));
            godot_joint->set_param(HingeJoint3D::PARAM_LIMIT_LOWER, a0 - Math::deg_to_rad((double)upper_angle));
        }
        godot_joint->set_flag(HingeJoint3D::FLAG_ENABLE_MOTOR, actuator != 0);
    }

    void gl_tick(double delta) override {
        if (Engine::get_singleton()->is_editor_hint() || !enabled) return;
        const GLExtremo e0 = gl_extremo(this, 0, part0_id);
        const GLExtremo e1 = gl_extremo(this, 1, part1_id);
        if (!gl_extremos_listos(e0, e1)) { _soltar_junta(); return; }
        RigidBody3D* b0 = gl_cuerpo_que_mueve(e0.pieza);
        RigidBody3D* b1 = gl_cuerpo_que_mueve(e1.pieza);
        if (!b0 && !b1) return;
        const uint64_t ia = b0 ? (uint64_t)b0->get_instance_id() : 0;
        const uint64_t ib = b1 ? (uint64_t)b1->get_instance_id() : 0;
        if (!godot_joint || ia != hecho_a || ib != hecho_b) {
            _soltar_junta();
            const Vector3 eje = gl_eje_extremo(e0);
            godot_joint = memnew(HingeJoint3D);
            godot_joint->set_global_transform(Transform3D(gl_base_con_eje_z(eje), e0.punto));
            if (b0) godot_joint->set_node_a(b0->get_path());
            if (b1) godot_joint->set_node_b(b1->get_path());
            add_child(godot_joint);
            hecho_a = ia;
            hecho_b = ib;
            const Basis ba = b0 ? b0->get_global_transform().basis.orthonormalized() : Basis();
            const Basis bb = b1 ? b1->get_global_transform().basis.orthonormalized() : Basis();
            rel_inicial = ba.inverse() * bb;
            eje_local_a = ba.inverse().xform(eje).normalized();
            // Roblox mide CurrentAngle entre los SecondaryAxis de los dos
            // attachments: si al montarla no coinciden, no arranca en 0.
            angulo_inicial = gl_angulo_entre(eje, gl_eje2_extremo(e0), gl_eje2_extremo(e1));
            _actualizar();
        }
        // Angulo actual: el de partida mas lo que B giro respecto de A desde
        // que se armo, sobre el eje.
        const Basis ba = b0 ? b0->get_global_transform().basis.orthonormalized() : Basis();
        const Basis bb = b1 ? b1->get_global_transform().basis.orthonormalized() : Basis();
        const Quaternion q((ba.inverse() * bb) * rel_inicial.inverse());
        const float ang = angulo_inicial + 2.0f * Math::atan2(Vector3(q.x, q.y, q.z).dot(eje_local_a), q.w);
        current_angle = Math::rad_to_deg(Math::wrapf(ang, -(float)Math_PI, (float)Math_PI));
        // Motor y servo: Godot da un motor de velocidad; el servo lo persigue.
        // Ese motor gira AL REVES del eje (medido: velocidad 3 daba wY = -3
        // con el eje en +Y), y en Roblox AngularVelocity positiva hace crecer
        // CurrentAngle. De ahi el signo; sin el, el servo huia del objetivo.
        // Con limites, el motor no empuja contra ellos: el de Jolt los
        // atravesaba (15 grados con MotorMaxTorque 1e6).
        const float dt = (float)Math::max(delta, 0.0001);
        if (actuator == 1) {
            float vel = angular_velocity;
            if (limits_enabled && ((vel > 0.0f && current_angle >= upper_angle) ||
                                   (vel < 0.0f && current_angle <= lower_angle))) vel = 0.0f;
            godot_joint->set_param(HingeJoint3D::PARAM_MOTOR_TARGET_VELOCITY, -vel);
            godot_joint->set_param(HingeJoint3D::PARAM_MOTOR_MAX_IMPULSE, motor_max_torque * dt);
        } else if (actuator == 2) {
            const float objetivo = limits_enabled ? CLAMP(target_angle, lower_angle, upper_angle) : target_angle;
            const float falta = Math::deg_to_rad(Math::wrapf(objetivo - current_angle, -180.0f, 180.0f));
            float vel = falta / dt;
            if (angular_speed > 0.0f) vel = CLAMP(vel, -angular_speed, angular_speed);
            godot_joint->set_param(HingeJoint3D::PARAM_MOTOR_TARGET_VELOCITY, -vel);
            godot_joint->set_param(HingeJoint3D::PARAM_MOTOR_MAX_IMPULSE, servo_max_torque * dt);
        }
    }
    HingeConstraint() { set_meta("_gl_bridge", true); }
};

// ────────────────────────────────────────────────────────────────────
//  BallSocketConstraint — Rotula: el punto de Attachment0 queda pegado al de
//  Attachment1 y todo lo demas gira libre. Mismo cambio que la bisagra: en el
//  punto del attachment y armada en el tick central.
// ────────────────────────────────────────────────────────────────────
class BallSocketConstraint : public GLConstraintBase {
    GDCLASS(BallSocketConstraint, GLConstraintBase);

    void _notification(int p_what) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (p_what == NOTIFICATION_ENTER_TREE)      gl_constraint_register(this);
        else if (p_what == NOTIFICATION_EXIT_TREE)  { gl_constraint_unregister(this); _soltar_junta(); }
    }

    uint64_t part0_id = 0;
    uint64_t part1_id = 0;
    bool  enabled        = true;
    bool  limits_enabled = false;
    float upper_angle    = 45.0f;

    Generic6DOFJoint3D* junta = nullptr;
    uint64_t hecho_a = 0;
    uint64_t hecho_b = 0;

    void _soltar_junta() {
        if (junta) { junta->queue_free(); junta = nullptr; }
        hecho_a = hecho_b = 0;
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_part0","n"),          &BallSocketConstraint::set_part0);
        ClassDB::bind_method(D_METHOD("get_part0"),               &BallSocketConstraint::get_part0);
        ClassDB::bind_method(D_METHOD("set_part1","n"),          &BallSocketConstraint::set_part1);
        ClassDB::bind_method(D_METHOD("get_part1"),               &BallSocketConstraint::get_part1);
        ClassDB::bind_method(D_METHOD("set_enabled","b"),        &BallSocketConstraint::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),             &BallSocketConstraint::get_enabled);
        ClassDB::bind_method(D_METHOD("set_limits_enabled","b"), &BallSocketConstraint::set_limits_enabled);
        ClassDB::bind_method(D_METHOD("get_limits_enabled"),      &BallSocketConstraint::get_limits_enabled);
        ClassDB::bind_method(D_METHOD("set_upper_angle","a"),    &BallSocketConstraint::set_upper_angle);
        ClassDB::bind_method(D_METHOD("get_upper_angle"),         &BallSocketConstraint::get_upper_angle);

        ADD_GROUP("BallSocketConstraint","");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Enabled"),       "set_enabled","get_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "LimitsEnabled"), "set_limits_enabled","get_limits_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "UpperAngle",     PROPERTY_HINT_RANGE,"0,180,0.1"), "set_upper_angle","get_upper_angle");
    }

public:
    void  set_part0(Node* n)           { part0_id = gl_part_id(n); _soltar_junta(); }
    Node* get_part0() const            { return gl_part_node(part0_id); }
    void  set_part1(Node* n)           { part1_id = gl_part_id(n); _soltar_junta(); }
    Node* get_part1() const            { return gl_part_node(part1_id); }
    void  set_enabled(bool b)          { enabled = b; if (!b) _soltar_junta(); }
    bool  get_enabled() const          { return enabled; }
    void  set_limits_enabled(bool b)   { limits_enabled = b; _soltar_junta(); }
    bool  get_limits_enabled() const   { return limits_enabled; }
    void  set_upper_angle(float a)     { upper_angle = a; _soltar_junta(); }
    float get_upper_angle() const      { return upper_angle; }

    void gl_tick(double delta) override {
        (void)delta;
        if (Engine::get_singleton()->is_editor_hint() || !enabled) return;
        const GLExtremo e0 = gl_extremo(this, 0, part0_id);
        const GLExtremo e1 = gl_extremo(this, 1, part1_id);
        if (!gl_extremos_listos(e0, e1)) { _soltar_junta(); return; }
        RigidBody3D* b0 = gl_cuerpo_que_mueve(e0.pieza);
        RigidBody3D* b1 = gl_cuerpo_que_mueve(e1.pieza);
        if (!b0 && !b1) return;
        const uint64_t ia = b0 ? (uint64_t)b0->get_instance_id() : 0;
        const uint64_t ib = b1 ? (uint64_t)b1->get_instance_id() : 0;
        if (junta && ia == hecho_a && ib == hecho_b) return;
        _soltar_junta();
        junta = memnew(Generic6DOFJoint3D);
        junta->set_global_transform(Transform3D(gl_base_con_eje_z(gl_eje_extremo(e0)), e0.punto));
        if (b0) junta->set_node_a(b0->get_path());
        if (b1) junta->set_node_b(b1->get_path());
        // Traslacion bloqueada, giro libre (o con tope de UpperAngle).
        junta->set_flag_x(Generic6DOFJoint3D::FLAG_ENABLE_LINEAR_LIMIT, true);
        junta->set_flag_y(Generic6DOFJoint3D::FLAG_ENABLE_LINEAR_LIMIT, true);
        junta->set_flag_z(Generic6DOFJoint3D::FLAG_ENABLE_LINEAR_LIMIT, true);
        junta->set_flag_x(Generic6DOFJoint3D::FLAG_ENABLE_ANGULAR_LIMIT, limits_enabled);
        junta->set_flag_y(Generic6DOFJoint3D::FLAG_ENABLE_ANGULAR_LIMIT, limits_enabled);
        junta->set_flag_z(Generic6DOFJoint3D::FLAG_ENABLE_ANGULAR_LIMIT, limits_enabled);
        if (limits_enabled) {
            const float rad = Math::deg_to_rad((double)upper_angle);
            junta->set_param_x(Generic6DOFJoint3D::PARAM_ANGULAR_UPPER_LIMIT,  rad);
            junta->set_param_x(Generic6DOFJoint3D::PARAM_ANGULAR_LOWER_LIMIT, -rad);
            junta->set_param_y(Generic6DOFJoint3D::PARAM_ANGULAR_UPPER_LIMIT,  rad);
            junta->set_param_y(Generic6DOFJoint3D::PARAM_ANGULAR_LOWER_LIMIT, -rad);
            junta->set_param_z(Generic6DOFJoint3D::PARAM_ANGULAR_UPPER_LIMIT,  rad);
            junta->set_param_z(Generic6DOFJoint3D::PARAM_ANGULAR_LOWER_LIMIT, -rad);
        }
        add_child(junta);
        hecho_a = ia;
        hecho_b = ib;
    }
    BallSocketConstraint() { set_meta("_gl_bridge", true); }
};

// ────────────────────────────────────────────────────────────────────
//  RodConstraint — Keeps a fixed distance between two parts
////
//  RodConstraint — Mantiene distancia fija entre dos partes
// ────────────────────────────────────────────────────────────────────
class RodConstraint : public GLConstraintBase {
    GDCLASS(RodConstraint, GLConstraintBase);

    // Alta y baja en el tick central (ver arriba): un solo _physics_process para
    // todos, en vez de uno por constraint.
    void _notification(int p_what) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (p_what == NOTIFICATION_ENTER_TREE)      gl_constraint_register(this);
        else if (p_what == NOTIFICATION_EXIT_TREE)  gl_constraint_unregister(this);
    }

    uint64_t  part0_id   = 0;
    uint64_t  part1_id   = 0;
    bool  enabled = true;
    float length  = 5.0f;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_part0","n"),   &RodConstraint::set_part0);
        ClassDB::bind_method(D_METHOD("get_part0"),        &RodConstraint::get_part0);
        ClassDB::bind_method(D_METHOD("set_part1","n"),   &RodConstraint::set_part1);
        ClassDB::bind_method(D_METHOD("get_part1"),        &RodConstraint::get_part1);
        ClassDB::bind_method(D_METHOD("set_enabled","b"), &RodConstraint::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),      &RodConstraint::get_enabled);
        ClassDB::bind_method(D_METHOD("set_length","l"),  &RodConstraint::set_length);
        ClassDB::bind_method(D_METHOD("get_length"),       &RodConstraint::get_length);
        ClassDB::bind_method(D_METHOD("get_current_distance"), &RodConstraint::get_current_distance);

        ADD_GROUP("RodConstraint","");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Enabled"), "set_enabled","get_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Length", PROPERTY_HINT_RANGE,"0,1000,0.01"), "set_length","get_length");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "CurrentDistance", PROPERTY_HINT_NONE, "",
                                  PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_current_distance");
    }

public:
    void  set_part0(Node* n)    { part0_id = gl_part_id(n);  }
    Node* get_part0() const     { return gl_part_node(part0_id); }
    void  set_part1(Node* n)    { part1_id = gl_part_id(n);  }
    Node* get_part1() const     { return gl_part_node(part1_id); }
    void  set_enabled(bool b)   { enabled = b; }
    bool  get_enabled() const   { return enabled; }
    void  set_length(float l)   { length = Math::max(l, 0.01f); }
    float get_length() const    { return length; }

    // Longitud 0 = la que haya entre los extremos al empezar.
    void gl_tick(double delta) override {
        if (Engine::get_singleton()->is_editor_hint() || !enabled) return;
        const GLExtremo e0 = gl_extremo(this, 0, part0_id);
        const GLExtremo e1 = gl_extremo(this, 1, part1_id);
        if (!gl_extremos_listos(e0, e1)) return;
        if (length <= 0.01f) length = Math::max(e0.punto.distance_to(e1.punto), 0.01f);
        gl_restriccion_distancia(e0, e1, length, false, delta);
    }
    float get_current_distance() const {
        const GLExtremo e0 = gl_extremo(this, 0, part0_id);
        const GLExtremo e1 = gl_extremo(this, 1, part1_id);
        return gl_extremos_listos(e0, e1) ? e0.punto.distance_to(e1.punto) : 0.0f;
    }
    RodConstraint() { set_meta("_gl_bridge", true); }   // Attachment0/1 por el puente generico
};

// ────────────────────────────────────────────────────────────────────
//  RopeConstraint — cuerda: los extremos no pueden separarse mas de Length,
//  pero si acercarse (floja). No existia: Instance.new("RopeConstraint")
//  devolvia nil y cualquier columpio, grua o cadena moria al montarse.
// ────────────────────────────────────────────────────────────────────
class RopeConstraint : public GLConstraintBase {
    GDCLASS(RopeConstraint, GLConstraintBase);

    void _notification(int p_what) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (p_what == NOTIFICATION_ENTER_TREE)      gl_constraint_register(this);
        else if (p_what == NOTIFICATION_EXIT_TREE)  gl_constraint_unregister(this);
    }

    bool  enabled = true;
    float length  = 5.0f;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_enabled","b"), &RopeConstraint::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),      &RopeConstraint::get_enabled);
        ClassDB::bind_method(D_METHOD("set_length","l"),  &RopeConstraint::set_length);
        ClassDB::bind_method(D_METHOD("get_length"),       &RopeConstraint::get_length);
        ClassDB::bind_method(D_METHOD("get_current_distance"), &RopeConstraint::get_current_distance);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Enabled"), "set_enabled","get_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Length", PROPERTY_HINT_RANGE,"0,1000,0.01"), "set_length","get_length");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "CurrentDistance", PROPERTY_HINT_NONE, "",
                                  PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_current_distance");
    }

public:
    void  set_enabled(bool b) { enabled = b; }
    bool  get_enabled() const { return enabled; }
    void  set_length(float l) { length = Math::max(l, 0.0f); }
    float get_length() const  { return length; }
    float get_current_distance() const {
        const GLExtremo e0 = gl_extremo(this, 0, 0);
        const GLExtremo e1 = gl_extremo(this, 1, 0);
        return gl_extremos_listos(e0, e1) ? e0.punto.distance_to(e1.punto) : 0.0f;
    }
    void gl_tick(double delta) override {
        if (Engine::get_singleton()->is_editor_hint() || !enabled) return;
        const GLExtremo e0 = gl_extremo(this, 0, 0);
        const GLExtremo e1 = gl_extremo(this, 1, 0);
        if (!gl_extremos_listos(e0, e1)) return;
        gl_restriccion_distancia(e0, e1, length, true, delta);
    }
    RopeConstraint() { set_meta("_gl_bridge", true); }
};

// ────────────────────────────────────────────────────────────────────
//  SpringConstraint — Spring between two parts
////
//  SpringConstraint — Resorte entre dos partes
// ────────────────────────────────────────────────────────────────────
class SpringConstraint : public GLConstraintBase {
    GDCLASS(SpringConstraint, GLConstraintBase);

    // Alta y baja en el tick central (ver arriba): un solo _physics_process para
    // todos, en vez de uno por constraint.
    void _notification(int p_what) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        if (p_what == NOTIFICATION_ENTER_TREE)      gl_constraint_register(this);
        else if (p_what == NOTIFICATION_EXIT_TREE)  gl_constraint_unregister(this);
    }

    uint64_t  part0_id       = 0;
    uint64_t  part1_id       = 0;
    bool  enabled     = true;
    float free_length = 5.0f;
    float stiffness   = 200.0f;
    float damping     = 20.0f;
    float max_length  = 10.0f;
    float min_length  = 0.0f;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_part0","n"),       &SpringConstraint::set_part0);
        ClassDB::bind_method(D_METHOD("get_part0"),            &SpringConstraint::get_part0);
        ClassDB::bind_method(D_METHOD("set_part1","n"),       &SpringConstraint::set_part1);
        ClassDB::bind_method(D_METHOD("get_part1"),            &SpringConstraint::get_part1);
        ClassDB::bind_method(D_METHOD("set_enabled","b"),     &SpringConstraint::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),          &SpringConstraint::get_enabled);
        ClassDB::bind_method(D_METHOD("set_free_length","l"), &SpringConstraint::set_free_length);
        ClassDB::bind_method(D_METHOD("get_free_length"),      &SpringConstraint::get_free_length);
        ClassDB::bind_method(D_METHOD("set_stiffness","s"),   &SpringConstraint::set_stiffness);
        ClassDB::bind_method(D_METHOD("get_stiffness"),        &SpringConstraint::get_stiffness);
        ClassDB::bind_method(D_METHOD("set_damping","d"),     &SpringConstraint::set_damping);
        ClassDB::bind_method(D_METHOD("get_damping"),          &SpringConstraint::get_damping);

        ADD_GROUP("SpringConstraint","");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Enabled"),    "set_enabled","get_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "FreeLength",  PROPERTY_HINT_RANGE,"0,100,0.01"),    "set_free_length","get_free_length");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Stiffness",   PROPERTY_HINT_RANGE,"0,100000,1"),    "set_stiffness","get_stiffness");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Damping",     PROPERTY_HINT_RANGE,"0,10000,0.1"),   "set_damping","get_damping");
    }

public:
    void  set_part0(Node* n)         { part0_id = gl_part_id(n);  }
    Node* get_part0() const          { return gl_part_node(part0_id); }
    void  set_part1(Node* n)         { part1_id = gl_part_id(n);  }
    Node* get_part1() const          { return gl_part_node(part1_id); }
    void  set_enabled(bool b)        { enabled = b; }
    bool  get_enabled() const        { return enabled; }
    void  set_free_length(float l)   { free_length = Math::max(l, 0.0f); }
    float get_free_length() const    { return free_length; }
    void  set_stiffness(float s)     { stiffness = Math::max(s, 0.0f); }
    float get_stiffness() const      { return stiffness; }
    void  set_damping(float d)       { damping = Math::max(d, 0.0f); }
    float get_damping() const        { return damping; }

    // Fuerza de muelle en los puntos de los attachments (antes en los centros,
    // asi que un muelle descentrado no hacia girar nada).
    void gl_tick(double delta) override {
        (void)delta;
        if (Engine::get_singleton()->is_editor_hint() || !enabled) return;
        const GLExtremo e0 = gl_extremo(this, 0, part0_id);
        const GLExtremo e1 = gl_extremo(this, 1, part1_id);
        if (!gl_extremos_listos(e0, e1)) return;
        const Vector3 d = e1.punto - e0.punto;
        const float dist = d.length();
        if (dist < 0.001f) return;
        const Vector3 dir = d / dist;

        RigidBody3D* rb0 = gl_cuerpo_que_mueve(e0.pieza);
        RigidBody3D* rb1 = gl_cuerpo_que_mueve(e1.pieza);
        Vector3 rel_vel;
        if (rb1) rel_vel += rb1->get_linear_velocity();
        if (rb0) rel_vel -= rb0->get_linear_velocity();
        const Vector3 total = dir * ((dist - free_length) * stiffness + rel_vel.dot(dir) * damping);

        if (rb0 && !rb0->is_freeze_enabled()) rb0->apply_force( total, e0.punto - rb0->get_global_position());
        if (rb1 && !rb1->is_freeze_enabled()) rb1->apply_force(-total, e1.punto - rb1->get_global_position());
    }
    SpringConstraint() { set_meta("_gl_bridge", true); }
};

#endif // ROBLOX_CONSTRAINTS_H

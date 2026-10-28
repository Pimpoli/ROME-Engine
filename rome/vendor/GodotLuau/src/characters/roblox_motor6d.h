#ifndef ROBLOX_MOTOR6D_H
#define ROBLOX_MOTOR6D_H

// ════════════════════════════════════════════════════════════════════
//  Motor6D — la junta de los rigs de Roblox.
//
//      Part1 = Part0 * C0 * giro(CurrentAngle, eje Z) * Transform * C1^-1
//
//  Transform lo escribe el animador cada frame (gl_animador.h) o un script
//  (animacion procedural). CurrentAngle va hacia DesiredAngle a MaxVelocity
//  radianes por paso de 1/60 s, girando sobre el eje Z de C0 como en Roblox
//  (los Animate viejos del R6 mueven asi los hombros).
//
//  Part0/Part1 se guardan por ObjectID (una pieza la puede destruir cualquier
//  script) y por ruta relativa, que es lo que viaja en la escena y en Clone:
//  el clon de un rig resuelve sus juntas contra SUS piezas.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include "gl_runtime.h"

using namespace godot;

class Motor6D : public Node {
    GDCLASS(Motor6D, Node);

    mutable ObjectID part0_id;   // cache de la pieza (se resuelve al pedirla)
    mutable ObjectID part1_id;
    mutable String part0_path;
    mutable String part1_path;
    // Con que padres se calculo cada ruta: si no han cambiado, sigue valiendo.
    mutable ObjectID ruta0_padre, ruta0_padre_pieza;
    mutable ObjectID ruta1_padre, ruta1_padre_pieza;
    Transform3D c0;
    Transform3D c1;
    Transform3D transform;
    float       max_velocity  = 9999.0f;
    float       current_angle = 0.0f;
    float       desired_angle = 0.0f;
    bool        enabled       = true;

    // La inversa de C1 se usa en CADA paso de fisica y C1 casi nunca cambia.
    Transform3D c1_inv;
    bool        c1_inv_valida = false;
    int         reintento = 0;

    static bool _misma_raiz(const Node* a, const Node* b) {
        while (a->get_parent()) a = a->get_parent();
        while (b->get_parent()) b = b->get_parent();
        return a == b;
    }
    Node* _parte(ObjectID id) const {
        return id.is_valid() ? gl_cast<Node>(ObjectDB::get_instance(id)) : nullptr;
    }
    // La pieza por su ruta. Una ruta relativa se sigue tambien fuera del arbol
    // (el Clone de un rig no tiene padre todavia y sus juntas ya deben ver SUS piezas).
    Node* _buscar(ObjectID& id, const String& ruta) const {
        if (Node* p = _parte(id)) return p;
        if (ruta.is_empty() || (ruta.begins_with("/") && !is_inside_tree())) return nullptr;
        Node* p = get_node_or_null(NodePath(ruta));
        if (p) id = ObjectID(p->get_instance_id());
        return p;
    }
    // Si la pieza ya se conoce, no hay nada que hacer (la ruta se rehace al
    // pedirla, ver _ruta_actual); si no, se busca por la ruta.
    void _resolver(ObjectID& id, String& ruta) {
        if (_parte(id)) return;
        id = ObjectID();
        if (!ruta.is_empty() && is_inside_tree())
            if (Node* p = get_node_or_null(NodePath(ruta))) id = p->get_instance_id();
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_part0_path", "p"), &Motor6D::set_part0_path);
        ClassDB::bind_method(D_METHOD("get_part0_path"),       &Motor6D::get_part0_path);
        ClassDB::bind_method(D_METHOD("set_part1_path", "p"), &Motor6D::set_part1_path);
        ClassDB::bind_method(D_METHOD("get_part1_path"),       &Motor6D::get_part1_path);
        ClassDB::bind_method(D_METHOD("set_enabled", "v"),    &Motor6D::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),          &Motor6D::get_enabled);
        ClassDB::bind_method(D_METHOD("set_max_velocity", "v"), &Motor6D::set_max_velocity);
        ClassDB::bind_method(D_METHOD("get_max_velocity"),      &Motor6D::get_max_velocity);
        ClassDB::bind_method(D_METHOD("set_desired_angle", "v"), &Motor6D::set_desired_angle);
        ClassDB::bind_method(D_METHOD("get_desired_angle"),      &Motor6D::get_desired_angle);
        ClassDB::bind_method(D_METHOD("set_current_angle", "v"), &Motor6D::set_current_angle);
        ClassDB::bind_method(D_METHOD("get_current_angle"),      &Motor6D::get_current_angle);
        ClassDB::bind_method(D_METHOD("set_c0", "t"),          &Motor6D::set_c0);
        ClassDB::bind_method(D_METHOD("get_c0"),               &Motor6D::get_c0);
        ClassDB::bind_method(D_METHOD("set_c1", "t"),          &Motor6D::set_c1);
        ClassDB::bind_method(D_METHOD("get_c1"),               &Motor6D::get_c1);
        ClassDB::bind_method(D_METHOD("set_transform", "t"),   &Motor6D::set_transform);
        ClassDB::bind_method(D_METHOD("get_transform"),        &Motor6D::get_transform);

        ADD_PROPERTY(PropertyInfo(Variant::STRING, "Part0"), "set_part0_path", "get_part0_path");
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "Part1"), "set_part1_path", "get_part1_path");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,   "Enabled"), "set_enabled", "get_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "MaxVelocity", PROPERTY_HINT_RANGE, "0,100,0.001"), "set_max_velocity", "get_max_velocity");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "DesiredAngle"), "set_desired_angle", "get_desired_angle");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "CurrentAngle"), "set_current_angle", "get_current_angle");
        ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "C0"), "set_c0", "get_c0");
        ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "C1"), "set_c1", "get_c1");
        // Transform no se guarda: es lo que pone el animador en cada frame.
        ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "Transform", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                     "set_transform", "get_transform");
    }

    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) {
            _resolver(part0_id, part0_path);
            _resolver(part1_id, part1_path);
            set_physics_process(enabled && !Engine::get_singleton()->is_editor_hint());
        }
    }

    // La ruta se saca de la pieza al pedirla (la usan Clone, duplicate y el
    // guardado): si las piezas se mueven despues (al aplanar el personaje), la
    // guardada se queda vieja y el clon no encontraria sus piezas. Solo se
    // rehace si cambio el padre de la junta o el de la pieza: un Clone de un
    // modelo con decenas de juntas la pedia cientos de veces por oleada.
    String _ruta_actual(ObjectID id, String& guardada, ObjectID& padre, ObjectID& padre_pieza) const {
        Node* p = _parte(id);
        if (!p) return guardada;
        const Node* mp = get_parent();
        const Node* pp = p->get_parent();
        const ObjectID a = mp ? ObjectID(mp->get_instance_id()) : ObjectID();
        const ObjectID b = pp ? ObjectID(pp->get_instance_id()) : ObjectID();
        if (!guardada.is_empty() && a == padre && b == padre_pieza) return guardada;
        if (_misma_raiz(this, p)) {
            guardada = String(get_path_to(p));
            padre = a;
            padre_pieza = b;
        }
        return guardada;
    }

public:
    void set_part0_path(const String& p) { part0_path = p; part0_id = ObjectID(); ruta0_padre = ObjectID(); _resolver(part0_id, part0_path); }
    String get_part0_path() const        { return _ruta_actual(part0_id, part0_path, ruta0_padre, ruta0_padre_pieza); }
    void set_part1_path(const String& p) { part1_path = p; part1_id = ObjectID(); ruta1_padre = ObjectID(); _resolver(part1_id, part1_path); }
    String get_part1_path() const        { return _ruta_actual(part1_id, part1_path, ruta1_padre, ruta1_padre_pieza); }

    // Desde un script (Part0 = pieza). nil la quita.
    void set_part0(Node* n) {
        part0_id = n ? ObjectID(n->get_instance_id()) : ObjectID();
        part0_path = String();
        ruta0_padre = ObjectID();
        if (n) get_part0_path();
    }
    void set_part1(Node* n) {
        part1_id = n ? ObjectID(n->get_instance_id()) : ObjectID();
        part1_path = String();
        ruta1_padre = ObjectID();
        if (n) get_part1_path();
    }
    Node* get_part0() const { return _buscar(part0_id, part0_path); }
    Node* get_part1() const { return _buscar(part1_id, part1_path); }

    void set_enabled(bool v) {
        enabled = v;
        if (is_inside_tree()) set_physics_process(enabled && !Engine::get_singleton()->is_editor_hint());
    }
    bool get_enabled() const             { return enabled; }
    void set_max_velocity(float v)       { max_velocity = v; }
    float get_max_velocity() const       { return max_velocity; }
    void set_desired_angle(float v)      { desired_angle = v; }
    float get_desired_angle() const      { return desired_angle; }
    void set_current_angle(float v)      { current_angle = v; }
    float get_current_angle() const      { return current_angle; }
    void set_c0(const Transform3D& t)    { c0 = t; }
    Transform3D get_c0() const           { return c0; }
    void set_c1(const Transform3D& t)    { c1 = t; c1_inv_valida = false; }
    Transform3D get_c1() const           { return c1; }
    void set_transform(const Transform3D& t) { transform = t; }
    Transform3D get_transform() const    { return transform; }

    // Active de Roblox: la junta esta haciendo algo (encendida y con sus dos piezas).
    bool gl_activa() const { return enabled && get_part0() && get_part1(); }

    // Donde tiene que estar Part1 segun Part0 (lo usa tambien el animador para
    // colocar una cadena de juntas en el mismo frame).
    Transform3D gl_mundo_part1(const Transform3D& mundo_part0) {
        if (!c1_inv_valida) { c1_inv = c1.inverse(); c1_inv_valida = true; }
        Transform3D giro;
        if (current_angle != 0.0f) giro.basis = Basis(Vector3(0, 0, 1), current_angle);
        return mundo_part0 * c0 * giro * transform * c1_inv;
    }

    void _physics_process(double delta) override {
        if (!enabled) return;
        // Una pieza que todavia no existia al entrar (la crea un script despues):
        // se vuelve a buscar cada medio segundo, no en cada paso.
        if ((!part0_id.is_valid() && !part0_path.is_empty()) || (!part1_id.is_valid() && !part1_path.is_empty())) {
            if (++reintento >= 30) {
                reintento = 0;
                _resolver(part0_id, part0_path);
                _resolver(part1_id, part1_path);
            }
        }
        Node3D* n0 = gl_cast<Node3D>(_parte(part0_id));
        Node3D* n1 = gl_cast<Node3D>(_parte(part1_id));
        if (!n0 || !n1 || !n1->is_inside_tree()) return;
        // MaxVelocity va en radianes por paso de 1/60 s, como en Roblox.
        const float diff = desired_angle - current_angle;
        const float paso = max_velocity * (float)delta * 60.0f;
        current_angle += Math::clamp(diff, -paso, paso);
        // El early-out vale lo mismo que en el WeldConstraint: el place trae
        // 1.420 Motor6D y casi todos estan quietos.
        const Transform3D mundo = gl_mundo_part1(gl_xf(n0));
        if (!mundo.is_equal_approx(gl_xf(n1))) n1->set_global_transform(mundo);
    }
};

#endif // ROBLOX_MOTOR6D_H

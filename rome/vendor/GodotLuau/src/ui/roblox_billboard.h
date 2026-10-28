#ifndef ROBLOX_BILLBOARD_H
#define ROBLOX_BILLBOARD_H

#include <godot_cpp/classes/node3d.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/classes/node.hpp>
#include <vector>

#include "lua.h"
#include "lualib.h"
#include "gl_errors.h"
#include "gl_debug.h"
#include "gl_runtime.h"
#include "gl_world_gui.h"
#include "roblox_interactive.h"   // ClickDetector: reparto del clic en el mundo
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector4.hpp>
#include "../core/gl_autopsia.h"

using namespace godot;

// ────────────────────────────────────────────────────────────────────
//  BillboardGui — 3D GUI that always faces the camera (above a Part)
//  Drawn by the world GUI reflection (gl_world_gui.h) in billboard mode
////
//  BillboardGui — GUI 3D que siempre mira a la cámara (sobre un Part)
//  La dibuja el reflejo de gl_world_gui.h (malla de letras y Sprite3D)
// ────────────────────────────────────────────────────────────────────
class BillboardGui : public Node3D {
    GDCLASS(BillboardGui, Node3D);

    bool    enabled           = true;
    bool    always_on_top     = false;
    // ── EL SIZE DE UN BillboardGui SON DOS COSAS DISTINTAS ────────────────
    // En Roblox, Size es un UDim2 y cada mitad significa algo diferente:
    //   · Scale  -> STUDS del mundo. Se aleja contigo, como cualquier objeto.
    //   · Offset -> PIXELES DE PANTALLA. El cartel se ve del MISMO tamaño
    //               estes a 5 o a 500 studs.
    // Aqui se sumaban las dos dividiendo el offset por 50 px/stud, y el
    // resultado eran carteles monstruosos: medido en el place del usuario,
    // "PimpoliDev [Upgrades]" salia de 22,6 x 11,3 studs y el NPCSpeedBillboard
    // de 120 x 30, con la letra de "V.I.P!" a 34,8 studs de alto. En Roblox esos
    // mismos carteles se ven de uno o dos studs.
    // No hay ningun px/stud que valga, porque depende de la distancia: lo que
    // toca es dibujar la parte de Offset con tamaño FIJO en pantalla.
    // 0x0 como en Roblox (Instance.new); lo importado trae su Size.
    float   size_x            = 0.0f;   // parte Scale, en studs
    float   size_y            = 0.0f;
    float   px_x              = 0.0f;   // parte Offset, en pixeles de pantalla
    float   px_y              = 0.0f;
    Vector3 studies_offset    = Vector3();
    // El resto de propiedades de Roblox. Leerlas reventaba el script ("is not
    // a valid member"); las de colocacion se aplican, las demas se guardan.
    Vector3 studs_offset_mundo;            // StudsOffsetWorldSpace
    Vector3 extents_offset;                // ExtentsOffset (en medios tamaños del soporte)
    Vector3 extents_offset_mundo;          // ExtentsOffsetWorldSpace
    Vector2 size_offset;                   // SizeOffset (fraccion del tamaño)
    float   light_influence   = 0.0f;
    float   brightness        = 1.0f;
    bool    clips_descendants = false;
    bool    active            = false;
    bool    reset_on_spawn    = true;
    int     zindex_behavior   = 0;         // Global, el de Instance.new
    float   distance_upper    = -1.0f;     // DistanceUpperLimit
    float   distance_step     = 0.0f;      // DistanceStep
    // INFINITO, como en Roblox. Estaba en 100 studs, asi que todo cartel que no
    // dijera lo contrario se esfumaba a 100 studs mientras en Roblox se ve
    // siempre. El importador solo pisa esto si el .rbxl trae MaxDistance.
    float   max_distance      = GL_WGUI_SIN_LIMITE;
    float   min_distance      = 0.0f;
    // Adornee: el objeto al que se pega el cartel, que en Roblox NO tiene por
    // que ser su padre. No existia: escribirlo desde Luau reventaba el script.
    ObjectID adornee_id;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_enabled", "v"),       &BillboardGui::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),             &BillboardGui::get_enabled);
        ClassDB::bind_method(D_METHOD("set_size_x", "v"),        &BillboardGui::set_size_x);
        ClassDB::bind_method(D_METHOD("get_size_x"),              &BillboardGui::get_size_x);
        ClassDB::bind_method(D_METHOD("set_size_y", "v"),        &BillboardGui::set_size_y);
        ClassDB::bind_method(D_METHOD("get_size_y"),              &BillboardGui::get_size_y);
        ClassDB::bind_method(D_METHOD("set_always_on_top", "v"), &BillboardGui::set_always_on_top);
        ClassDB::bind_method(D_METHOD("get_always_on_top"),       &BillboardGui::get_always_on_top);
        ClassDB::bind_method(D_METHOD("set_max_distance", "v"),  &BillboardGui::set_max_distance);
        ClassDB::bind_method(D_METHOD("get_max_distance"),        &BillboardGui::get_max_distance);
        ClassDB::bind_method(D_METHOD("set_StudsOffset", "v"),   &BillboardGui::set_studs_offset);
        ClassDB::bind_method(D_METHOD("get_StudsOffset"),         &BillboardGui::get_studs_offset);
        ClassDB::bind_method(D_METHOD("set_DistanceLowerLimit", "v"), &BillboardGui::set_min_distance);
        ClassDB::bind_method(D_METHOD("get_DistanceLowerLimit"),      &BillboardGui::get_min_distance);
        ClassDB::bind_method(D_METHOD("set_udim2_size", "sx", "ox", "sy", "oy"), &BillboardGui::set_udim2_size);
        ClassDB::bind_method(D_METHOD("_gl_rebuild_world"),       &BillboardGui::_gl_rebuild_world);
        ClassDB::bind_method(D_METHOD("_gl_lienzo_px"),           &BillboardGui::_gl_lienzo_px);
        // El UDim2 COMPLETO como propiedad de guardado. SizeX/SizeY solo
        // llevan la parte Scale: la de Offset (px_x/px_y) no era propiedad de
        // nadie, asi que Clone() no la copiaba (gl_clone copia propiedades
        // registradas) y TODO clon de un cartel con Offset nacia a 0 -> el
        // minimo de 0.2x0.2 studs: un puntito ilegible. La plantilla original
        // se veia bien y los clones no, que es justo lo que se reportaba.
        ClassDB::bind_method(D_METHOD("set_SizeUDim2", "v"), &BillboardGui::set_SizeUDim2);
        ClassDB::bind_method(D_METHOD("get_SizeUDim2"),      &BillboardGui::get_SizeUDim2);
        ClassDB::bind_method(D_METHOD("set_Adornee", "o"),       &BillboardGui::set_adornee);
        ClassDB::bind_method(D_METHOD("get_Adornee"),             &BillboardGui::get_adornee);
        ClassDB::bind_method(D_METHOD("set_StudsOffsetWorldSpace", "v"), &BillboardGui::set_StudsOffsetWorldSpace);
        ClassDB::bind_method(D_METHOD("get_StudsOffsetWorldSpace"),      &BillboardGui::get_StudsOffsetWorldSpace);
        ClassDB::bind_method(D_METHOD("set_ExtentsOffset", "v"),         &BillboardGui::set_ExtentsOffset);
        ClassDB::bind_method(D_METHOD("get_ExtentsOffset"),              &BillboardGui::get_ExtentsOffset);
        ClassDB::bind_method(D_METHOD("set_ExtentsOffsetWorldSpace", "v"), &BillboardGui::set_ExtentsOffsetWorldSpace);
        ClassDB::bind_method(D_METHOD("get_ExtentsOffsetWorldSpace"),      &BillboardGui::get_ExtentsOffsetWorldSpace);
        ClassDB::bind_method(D_METHOD("set_SizeOffset", "v"),            &BillboardGui::set_SizeOffset);
        ClassDB::bind_method(D_METHOD("get_SizeOffset"),                 &BillboardGui::get_SizeOffset);
        ClassDB::bind_method(D_METHOD("set_LightInfluence", "v"),        &BillboardGui::set_LightInfluence);
        ClassDB::bind_method(D_METHOD("get_LightInfluence"),             &BillboardGui::get_LightInfluence);
        ClassDB::bind_method(D_METHOD("set_Brightness", "v"),            &BillboardGui::set_Brightness);
        ClassDB::bind_method(D_METHOD("get_Brightness"),                 &BillboardGui::get_Brightness);
        ClassDB::bind_method(D_METHOD("set_ClipsDescendants", "v"),      &BillboardGui::set_ClipsDescendants);
        ClassDB::bind_method(D_METHOD("get_ClipsDescendants"),           &BillboardGui::get_ClipsDescendants);
        ClassDB::bind_method(D_METHOD("set_Active", "v"),                &BillboardGui::set_Active);
        ClassDB::bind_method(D_METHOD("get_Active"),                     &BillboardGui::get_Active);
        ClassDB::bind_method(D_METHOD("set_ResetOnSpawn", "v"),          &BillboardGui::set_ResetOnSpawn);
        ClassDB::bind_method(D_METHOD("get_ResetOnSpawn"),               &BillboardGui::get_ResetOnSpawn);
        ClassDB::bind_method(D_METHOD("set_ZIndexBehavior", "v"),        &BillboardGui::set_ZIndexBehavior);
        ClassDB::bind_method(D_METHOD("get_ZIndexBehavior"),             &BillboardGui::get_ZIndexBehavior);
        ClassDB::bind_method(D_METHOD("set_DistanceUpperLimit", "v"),    &BillboardGui::set_DistanceUpperLimit);
        ClassDB::bind_method(D_METHOD("get_DistanceUpperLimit"),         &BillboardGui::get_DistanceUpperLimit);
        ClassDB::bind_method(D_METHOD("set_DistanceStep", "v"),          &BillboardGui::set_DistanceStep);
        ClassDB::bind_method(D_METHOD("get_DistanceStep"),               &BillboardGui::get_DistanceStep);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "StudsOffsetWorldSpace"),   "set_StudsOffsetWorldSpace",   "get_StudsOffsetWorldSpace");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "ExtentsOffset"),           "set_ExtentsOffset",           "get_ExtentsOffset");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "ExtentsOffsetWorldSpace"), "set_ExtentsOffsetWorldSpace", "get_ExtentsOffsetWorldSpace");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "SizeOffset"),              "set_SizeOffset",              "get_SizeOffset");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,   "LightInfluence"),          "set_LightInfluence",          "get_LightInfluence");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,   "Brightness"),              "set_Brightness",              "get_Brightness");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,    "ClipsDescendants"),        "set_ClipsDescendants",        "get_ClipsDescendants");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,    "Active"),                  "set_Active",                  "get_Active");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,    "ResetOnSpawn"),            "set_ResetOnSpawn",            "get_ResetOnSpawn");
        ADD_PROPERTY(PropertyInfo(Variant::INT,     "ZIndexBehavior"),          "set_ZIndexBehavior",          "get_ZIndexBehavior");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,   "DistanceUpperLimit"),      "set_DistanceUpperLimit",      "get_DistanceUpperLimit");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,   "DistanceStep"),            "set_DistanceStep",            "get_DistanceStep");

        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Enabled"),      "set_enabled",       "get_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "AlwaysOnTop"),  "set_always_on_top", "get_always_on_top");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "SizeX",  PROPERTY_HINT_RANGE, "0.1,50,0.1"), "set_size_x", "get_size_x");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "SizeY",  PROPERTY_HINT_RANGE, "0.1,50,0.1"), "set_size_y", "get_size_y");
        // Sin PROPERTY_HINT_RANGE "0,500": el hint RECORTABA el valor en el
        // inspector y no dejaba expresar el infinito de Roblox.
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "MaxDistance"), "set_max_distance", "get_max_distance");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3,"StudsOffset"),        "set_StudsOffset", "get_StudsOffset");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "DistanceLowerLimit"), "set_DistanceLowerLimit", "get_DistanceLowerLimit");
        ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "Adornee", PROPERTY_HINT_NODE_TYPE, "Node3D"), "set_Adornee", "get_Adornee");
        // Va DESPUES de SizeX/SizeY para que gane al restaurar un clon.
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR4, "SizeUDim2", PROPERTY_HINT_NONE, "",
                                  PROPERTY_USAGE_STORAGE), "set_SizeUDim2", "get_SizeUDim2");
    }

public:
    void set_enabled(bool v)        { enabled = v; set_visible(v); }
    bool get_enabled() const        { return enabled; }
    void set_size_x(float v)        { size_x = v; }
    float get_size_x() const        { return size_x; }
    float get_px_x() const          { return px_x; }
    float get_px_y() const          { return px_y; }
    void set_size_y(float v)        { size_y = v; }
    float get_size_y() const        { return size_y; }
    // Los tres repintan. Antes solo GUARDABAN el valor: escribir MaxDistance o
    // AlwaysOnTop desde Luau no se notaba hasta que otra cosa reconstruia el
    // cartel, y normalmente eso no pasaba nunca.
    void set_always_on_top(bool v)  { always_on_top = v; _repintar(); }
    bool get_always_on_top() const  { return always_on_top; }
    // El "sin limite" de dentro es 1e9; hacia Luau sale math.huge, como Roblox.
    void set_max_distance(float v)  { max_distance = MIN(v, GL_WGUI_SIN_LIMITE); _repintar(); }
    float get_max_distance() const  { return max_distance >= GL_WGUI_SIN_LIMITE ? (float)Math_INF : max_distance; }

    void set_StudsOffsetWorldSpace(Vector3 v)   { studs_offset_mundo = v; _recolocar(); }
    Vector3 get_StudsOffsetWorldSpace() const   { return studs_offset_mundo; }
    void set_ExtentsOffset(Vector3 v)           { extents_offset = v; _recolocar(); }
    Vector3 get_ExtentsOffset() const           { return extents_offset; }
    void set_ExtentsOffsetWorldSpace(Vector3 v) { extents_offset_mundo = v; _recolocar(); }
    Vector3 get_ExtentsOffsetWorldSpace() const { return extents_offset_mundo; }
    void set_SizeOffset(Vector2 v)              { size_offset = v; _repintar(); }
    Vector2 get_SizeOffset() const              { return size_offset; }
    void set_LightInfluence(float v)            { light_influence = v; }
    float get_LightInfluence() const            { return light_influence; }
    void set_Brightness(float v)                { brightness = v; _repintar(); }
    float get_Brightness() const                { return brightness; }
    void set_ClipsDescendants(bool v)           { clips_descendants = v; }
    bool get_ClipsDescendants() const           { return clips_descendants; }
    void set_Active(bool v)                     { active = v; }
    bool get_Active() const                     { return active; }
    void set_ResetOnSpawn(bool v)               { reset_on_spawn = v; }
    bool get_ResetOnSpawn() const               { return reset_on_spawn; }
    void set_ZIndexBehavior(int v)              { zindex_behavior = v; }
    int  get_ZIndexBehavior() const             { return zindex_behavior; }
    void set_DistanceUpperLimit(float v)        { distance_upper = v; }
    float get_DistanceUpperLimit() const        { return distance_upper; }
    void set_DistanceStep(float v)              { distance_step = v; }
    float get_DistanceStep() const              { return distance_step; }

    // Donde va el cartel respecto de su soporte: StudsOffset (aqui como mundo,
    // como siempre), StudsOffsetWorldSpace, y los Extents en medios tamaños de
    // la pieza (ExtentsOffset (0,1,0) = encima justo de la pieza).
    Vector3 _desplazamiento() const {
        Vector3 d = studies_offset + studs_offset_mundo;
        const Vector3 ext = extents_offset + extents_offset_mundo;
        if (!ext.is_zero_approx()) {
            Node* soporte = gl_cast<Node>(get_adornee());
            if (!soporte) soporte = get_parent();
            const Variant tam = soporte ? soporte->get(StringName("Size")) : Variant();
            if (tam.get_type() == Variant::VECTOR3) d += (Vector3)tam * 0.5f * ext;
        }
        return d;
    }
    void _recolocar() {
        if (get_adornee()) { set_process(true); return; }   // lo coloca _process
        set_position(_desplazamiento());
    }

    // Adornee (Roblox): el cartel se coloca sobre ESE objeto en vez de sobre su
    // padre. Es como se pega una UI a algo sin tener que colgarla de ello.
    void set_adornee(Object* o) {
        Node3D* n = gl_cast<Node3D>(o);
        adornee_id = n ? n->get_instance_id() : ObjectID();
        // Con adornee hay que seguirlo cada frame; sin el, el cartel ya viaja
        // con su padre y no hace falta gastar un _process.
        set_process(true);
        _repintar();   // sin esto, poner el Adornee despues no dibujaba nada
    }
    Object* get_adornee() const {
        return adornee_id.is_null() ? nullptr : ObjectDB::get_instance(adornee_id);
    }

    // Size de Roblox (UDim2). En un BillboardGui la Scale va en STUDS y el
    // Offset en PIXELES; se convierten con la referencia del motor, 50 px por
    // stud (la misma que usa SurfaceGui por defecto). Antes esto no existia:
    // el importador no encontraba donde poner el Size y TODOS los carteles se
    // quedaban con el 4x2 de fabrica, sin importar lo que dijera el juego.
    //
    // Comprobado con el place de prueba: los carteles GRANDES del juego usan
    // Scale puro (22.6 x 11.3 el rotulo de la tienda, 120 x 30 el de los NPC),
    // no offsets mal convertidos. O sea que en Roblox tambien son asi de
    // grandes; lo que estaba mal era el reflejo, no el tamaño.
    static constexpr float PX_POR_STUD = 50.0f;
    // Referencia de la parte de Offset: cuantos pixeles de Roblox equivalen a
    // una unidad del cartel cuando se dibuja a tamaño fijo. Es una CALIBRACION,
    // no una conversion a studs: el nodo se marca fixed_size y Godot ya se
    // encarga de que no cambie con la distancia.
    static constexpr float PX_FIJO = 260.0f;

    // (sx, ox, sy, oy) empaquetado, para clonar/guardar el Size entero.
    void set_SizeUDim2(Vector4 v) { set_udim2_size(v.x, v.y, v.z, v.w); }
    Vector4 get_SizeUDim2() const { return Vector4(size_x, px_x, size_y, px_y); }

    void set_udim2_size(float sx, float ox, float sy, float oy) {
        size_x = MAX(0.0f, sx);
        size_y = MAX(0.0f, sy);
        px_x   = MAX(0.0f, ox);
        px_y   = MAX(0.0f, oy);
        if (is_inside_tree()) { _relayout_hijos(); call_deferred("_gl_rebuild_world"); }
    }

    // Cambiar el Size cambia el LIENZO, y los hijos se colocaron contra el
    // anterior. Sin esto, un cartel al que un script le cambia el tamaño se
    // queda con la colocacion vieja.
    void _relayout_hijos() {
        for (int i = 0; i < get_child_count(); i++) {
            Node* c = get_child(i);
            if (c && !c->has_meta("__gl_world_gui") && c->has_method("_on_parent_resized"))
                c->call_deferred("_on_parent_resized");
        }
    }
    // ¿Este cartel es de los de tamaño fijo en pantalla? Lo es cuando su Size no
    // tiene practicamente Scale y si tiene Offset — que es el caso de casi
    // todos los carteles de cualquier juego.
    bool _es_tam_fijo() const {
        return (size_x < 0.01f && size_y < 0.01f) && (px_x > 0.5f || px_y > 0.5f);
    }

    // ── EL LIENZO DEL CARTEL, EN PIXELES ─────────────────────────────
    // Los hijos de un BillboardGui son Control colgando de un Node3D, asi que
    // Godot no tiene un padre Control del que sacar el area y usaba LA VENTANA.
    // Eso rompe todo lo que dependa de la PROPORCION, que es justo como se
    // construye un cartel en Roblox: un Frame a tamaño completo con un
    // UIAspectRatioConstraint de la proporcion del cartel.
    //
    // Medido en el place: un cartel de 21,9 x 5,47 (4:1) con su MainFrame a
    // Size (1,1) y constraint 4.0 se resolvia contra la ventana 1280x720 y
    // salia de 1280x320 — o sea offset inferior -400. Luego el reflejo 3D
    // recalcula la caja con las ANCLAS contra el lienzo del cartel (273 px de
    // alto) pero SUMANDO ese offset de -400, que estaba en pixeles de ventana:
    //   1.0 * 273,7 - 400 = -126  ->  alto NEGATIVO
    // Caja negativa = el elemento no se dibuja Y sus hijos heredan una caja
    // vacia, asi que el cartel entero desaparecia. 24 de los 239 del place.
    //
    // Devolviendo aqui el lienzo del cartel, las dos cuentas viven en el MISMO
    // sistema: el constraint 4:1 sobre un lienzo que ya es 4:1 no cambia nada,
    // los offsets se quedan en 0 y el reflejo sale exacto.
    Vector2 _gl_lienzo_px() const {
        if (_es_tam_fijo()) return Vector2(MAX(1.0f, px_x), MAX(1.0f, px_y));
        return Vector2(MAX(10.0f, (size_x + px_x / PX_POR_STUD) * PX_POR_STUD),
                       MAX(10.0f, (size_y + px_y / PX_POR_STUD) * PX_POR_STUD));
    }

    void set_studs_offset(Vector3 v) { studies_offset = v; _recolocar(); }
    Vector3 get_studs_offset() const { return studies_offset; }
    void set_min_distance(float v)   { min_distance = v; _repintar(); }
    float get_min_distance() const   { return min_distance; }

    void _repintar() {
        if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint())
            call_deferred("_gl_rebuild_world");
    }

    // Reconstruye el reflejo 3D del contenido. Se llama en diferido: los
    // Control hijos necesitan un frame para tener su rect resuelto.
    void _gl_rebuild_world() {
        remove_meta("_gl_wgui_sucio");   // se admite el siguiente aviso de cambio
        if (Engine::get_singleton()->is_editor_hint()) return;
        gl_world_gui_cull(this);
        gl_world_gui_mark(this);
        // Plantilla guardada (no cuelga de Workspace): fuera el reflejo. Sin
        // el borrado explicito, un cartel construido en el mundo y guardado
        // despues en ReplicatedStorage se seguia VIENDO.
        if (!gl_world_gui_in_world(this)) { gl_world_gui_borrar_reflejo(this); return; }
        // Sin Adornee y sin una BasePart/Attachment de padre no hay SITIO donde
        // ponerlo: Roblox no dibuja nada. Nosotros lo dibujabamos en el ORIGEN
        // del mundo, porque una carpeta no tiene transform.
        if (!gl_world_gui_tiene_soporte(this, get_adornee())) {
            gl_world_gui_borrar_reflejo(this); return;
        }
        // Dos modos, como Roblox: Scale = studs del mundo, Offset = tamaño fijo
        // en pantalla (ver el comentario de size_x).
        const bool fijo = _es_tam_fijo();
        float w, h;
        if (fijo) {
            w = MAX(0.05f, px_x / PX_FIJO);
            h = MAX(0.05f, px_y / PX_FIJO);
        } else {
            w = MAX(0.2f, size_x + px_x / PX_POR_STUD);
            h = MAX(0.2f, size_y + px_y / PX_POR_STUD);
        }
        // Caja de referencia en pixeles: el cartel entero. TIENE que ser la
        // misma que ve gl_gui_parent_area, o las anclas y los offsets acaban
        // en sistemas distintos (ver _gl_lienzo_px).
        const Vector2 raiz = _gl_lienzo_px();
        // Con Size en studs, Roblox mide la UI del cartel en pixeles de PANTALLA
        // y el tope de 100 de TextScaled casi nunca se alcanza: la letra llena
        // su caja (ver gl_wgui_texto_de). Con Size en pixeles el lienzo es ese
        // y el tope va como en un SurfaceGui.
        LocalVector<GLWorldGuiItem> items;
        for (int i = 0; i < get_child_count(); i++) {
            Node* c = get_child(i);
            if (c && !c->has_meta("__gl_world_gui"))
                gl_world_gui_collect(c, Vector2(), raiz, raiz, items, GL_WGUI_TOPE_ITEMS_BILLBOARD, 1.0f, !fijo);
        }
        gl_world_gui_build(this, items, w, h, true, max_distance, min_distance, always_on_top, fijo,
                           size_offset, brightness);
        // Con reflejo hay que vigilar cada frame: seguir al Adornee (si lo hay)
        // y sobre todo NEUTRALIZAR la rotacion heredada (ver _process).
        set_process(true);
    }

    // Con Adornee puesto, el cartel se pega a ESE objeto y no a su padre.
    void _process(double) override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        GL_AUT_SCOPE(AUT_P_CARTEL);
        if (!is_inside_tree()) return;
        // ── LA ROTACION DEL SOPORTE NO SE HEREDA ─────────────────────────
        // Las piezas del reflejo se colocan en el espacio LOCAL de este nodo y
        // solo cada Label3D/Sprite3D se gira a camara por separado. Si el
        // soporte esta girado (un NPC que gira, o una Part soldada a el), las
        // filas del cartel salen desparramadas por un eje al azar — y con la X
        // local mirando atras, en espejo. En Roblox un BillboardGui NUNCA rota
        // con su soporte.
        Transform3D t = get_global_transform();
        if (!t.basis.is_equal_approx(Basis())) {
            t.basis = Basis();
            set_global_transform(t);
        }
        Node3D* a = gl_cast<Node3D>(get_adornee());
        if (!a || !a->is_inside_tree()) return;
        const Vector3 destino = a->get_global_position() + _desplazamiento();
        if (!get_global_position().is_equal_approx(destino)) set_global_position(destino);
    }

    void _ready() override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        // Los Label3D que un script haya puesto a mano siguen en modo billboard.
        for (int i = 0; i < get_child_count(); i++) {
            Label3D* lbl = gl_cast<Label3D>(get_child(i));
            if (lbl) lbl->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
        }
        set_position(_desplazamiento());
        call_deferred("_gl_rebuild_world");
    }

    // ── AL CAMBIAR DE PADRE HAY QUE VOLVER A DECIDIR ─────────────────
    // _ready() corre UNA vez. Si el cartel se construye dentro del Workspace y
    // luego un script lo guarda en ReplicatedStorage, sus Label3D se quedan
    // puestos y SE SIGUEN VIENDO en el mundo — medido en el place: cinco
    // carteles "Overhead" de ReplicatedStorage/Personajes dibujando en el
    // origen. Y al reves igual de roto: lo que se crea guardado y luego se mete
    // en el Workspace no se dibujaba nunca.
    // ENTER_TREE salta en CADA reparentado, asi que aqui se vuelve a decidir:
    // _gl_rebuild_world borra el reflejo viejo y solo lo rehace si toca.
    void _notification(int what) {
        if (what == NOTIFICATION_ENTER_TREE && !Engine::get_singleton()->is_editor_hint())
            call_deferred("_gl_rebuild_world");
    }
};

// ────────────────────────────────────────────────────────────────────
//  SurfaceGui — GUI rendered on the surface of a Part
//  Uses a SubViewport projected onto a quad of the Part
////
//  SurfaceGui — GUI renderizada en la superficie de un Part
//  Usa SubViewport proyectado sobre un cuadrilátero del Part
// ────────────────────────────────────────────────────────────────────
class SurfaceGui : public Node3D {
    GDCLASS(SurfaceGui, Node3D);

    bool    enabled        = true;
    bool    always_on_top  = false;
    int     pixels_per_stud = 50;    // los de Instance.new en Roblox: 50 px/stud,
    String  adornee        = "";
    // Enum.NormalId de Roblox: Right=0, Top=1, Back=2, Left=3, Bottom=4, Front=5.
    // Es la numeracion que usa _gl_face_geom mas abajo. El valor por defecto era
    // 1, o sea TOP: cualquier SurfaceGui creado por script (que no fija Face,
    // porque en Roblox el defecto es Front) se pegaba al TECHO de la pieza, y la
    // deteccion de clics buscaba en la cara equivocada. El comentario que habia
    // aqui daba otra numeracion distinta y no coincidia con el codigo.
    int     face           = 5;  // Front, como en Roblox
    float   brightness     = 1.0f;
    float   light_influence = 0.0f;  // lienzo de 800x600 y sin luz de la escena
    bool    active         = true;
    Vector2 canvas_size    = Vector2(800, 600);
    int     sizing_mode    = 0;      // FixedSize=0, PixelsPerStud=1
    float   max_distance   = 0.0f;
    float   z_offset       = 0.0f;
    bool    clips_descendants = false;
    bool    reset_on_spawn = true;
    int     zindex_behavior = 0;     // Global, el de Instance.new

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_enabled", "v"),           &SurfaceGui::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"),                 &SurfaceGui::get_enabled);
        ClassDB::bind_method(D_METHOD("set_pixels_per_stud", "v"),   &SurfaceGui::set_pixels_per_stud);
        ClassDB::bind_method(D_METHOD("get_pixels_per_stud"),         &SurfaceGui::get_pixels_per_stud);
        ClassDB::bind_method(D_METHOD("set_face", "v"),              &SurfaceGui::set_face);
        ClassDB::bind_method(D_METHOD("get_face"),                    &SurfaceGui::get_face);
        ClassDB::bind_method(D_METHOD("set_brightness", "v"),        &SurfaceGui::set_brightness);
        ClassDB::bind_method(D_METHOD("get_brightness"),              &SurfaceGui::get_brightness);
        ClassDB::bind_method(D_METHOD("set_CanvasSize", "v"),        &SurfaceGui::set_CanvasSizeV);
        ClassDB::bind_method(D_METHOD("get_CanvasSize"),              &SurfaceGui::get_CanvasSizeV);
        ClassDB::bind_method(D_METHOD("set_SizingMode", "v"),        &SurfaceGui::set_SizingMode);
        ClassDB::bind_method(D_METHOD("get_SizingMode"),              &SurfaceGui::get_SizingMode);
        ClassDB::bind_method(D_METHOD("set_LightInfluence", "v"),    &SurfaceGui::set_LightInfluence);
        ClassDB::bind_method(D_METHOD("get_LightInfluence"),          &SurfaceGui::get_LightInfluence);
        ClassDB::bind_method(D_METHOD("set_MaxDistance", "v"),       &SurfaceGui::set_MaxDistanceS);
        ClassDB::bind_method(D_METHOD("get_MaxDistance"),             &SurfaceGui::get_MaxDistanceS);
        ClassDB::bind_method(D_METHOD("set_AlwaysOnTop", "v"),       &SurfaceGui::set_AlwaysOnTop);
        ClassDB::bind_method(D_METHOD("get_AlwaysOnTop"),             &SurfaceGui::get_AlwaysOnTop);
        ClassDB::bind_method(D_METHOD("set_ZOffset", "v"),           &SurfaceGui::set_ZOffset);
        ClassDB::bind_method(D_METHOD("get_ZOffset"),                 &SurfaceGui::get_ZOffset);
        ClassDB::bind_method(D_METHOD("_gl_rebuild_world"),           &SurfaceGui::_gl_rebuild_world);
        ClassDB::bind_method(D_METHOD("_gl_lienzo_px"),               &SurfaceGui::_gl_lienzo_px);
        ClassDB::bind_method(D_METHOD("gl_click_en", "punto_mundo"),  &SurfaceGui::gl_click_en);
        ClassDB::bind_method(D_METHOD("set_ClipsDescendants", "v"),  &SurfaceGui::set_ClipsDescendants);
        ClassDB::bind_method(D_METHOD("get_ClipsDescendants"),        &SurfaceGui::get_ClipsDescendants);
        ClassDB::bind_method(D_METHOD("set_Active", "v"),            &SurfaceGui::set_Active);
        ClassDB::bind_method(D_METHOD("get_Active"),                  &SurfaceGui::get_Active);
        ClassDB::bind_method(D_METHOD("set_ResetOnSpawn", "v"),      &SurfaceGui::set_ResetOnSpawn);
        ClassDB::bind_method(D_METHOD("get_ResetOnSpawn"),            &SurfaceGui::get_ResetOnSpawn);
        ClassDB::bind_method(D_METHOD("set_ZIndexBehavior", "v"),    &SurfaceGui::set_ZIndexBehavior);
        ClassDB::bind_method(D_METHOD("get_ZIndexBehavior"),          &SurfaceGui::get_ZIndexBehavior);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ClipsDescendants"), "set_ClipsDescendants", "get_ClipsDescendants");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Active"),           "set_Active",           "get_Active");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ResetOnSpawn"),     "set_ResetOnSpawn",     "get_ResetOnSpawn");
        ADD_PROPERTY(PropertyInfo(Variant::INT,  "ZIndexBehavior"),   "set_ZIndexBehavior",   "get_ZIndexBehavior");

        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Enabled"),       "set_enabled",         "get_enabled");
        // "PixelsPerStud" EXACTO (singular): el importador aplica las props del
        // .rbxl por nombre Roblox y con el typo "PixelsPerStuds" nunca la
        // encontraba — todo SurfaceGui con SizingMode=PixelsPerStud quedaba
        // con el default 20 y el lienzo salia con proporcion equivocada.
        ADD_PROPERTY(PropertyInfo(Variant::INT,   "PixelsPerStud", PROPERTY_HINT_RANGE, "1,200,1"), "set_pixels_per_stud", "get_pixels_per_stud");
        ADD_PROPERTY(PropertyInfo(Variant::INT,   "Face",           PROPERTY_HINT_RANGE, "0,5,1"),   "set_face",            "get_face");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Brightness",     PROPERTY_HINT_RANGE, "0,2,0.01"), "set_brightness",     "get_brightness");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR2,"CanvasSize"),      "set_CanvasSize",      "get_CanvasSize");
        ADD_PROPERTY(PropertyInfo(Variant::INT,   "SizingMode"),      "set_SizingMode",      "get_SizingMode");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "LightInfluence"),  "set_LightInfluence",  "get_LightInfluence");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "MaxDistance"),     "set_MaxDistance",     "get_MaxDistance");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "AlwaysOnTop"),     "set_AlwaysOnTop",     "get_AlwaysOnTop");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ZOffset"),         "set_ZOffset",         "get_ZOffset");
    }

public:
    void set_enabled(bool v)         { enabled = v; set_visible(v); }
    bool get_enabled() const         { return enabled; }
    void set_pixels_per_stud(int v)  { pixels_per_stud = v; }
    int  get_pixels_per_stud() const { return pixels_per_stud; }
    void set_face(int v)             { face = v; }
    int  get_face() const            { return face; }
    void set_brightness(float v)     { brightness = v; }
    float get_brightness() const     { return brightness; }
    void set_CanvasSizeV(Vector2 v)  { canvas_size = v; }
    Vector2 get_CanvasSizeV() const  { return canvas_size; }
    void set_SizingMode(int v)       { sizing_mode = v; }
    int  get_SizingMode() const      { return sizing_mode; }
    void set_LightInfluence(float v) { light_influence = v; }
    float get_LightInfluence() const { return light_influence; }
    void set_MaxDistanceS(float v)   { max_distance = v; }
    float get_MaxDistanceS() const   { return max_distance; }
    void set_AlwaysOnTop(bool v)     { always_on_top = v; }
    bool get_AlwaysOnTop() const     { return always_on_top; }
    void set_ZOffset(float v)        { z_offset = v; }
    float get_ZOffset() const        { return z_offset; }
    void set_ClipsDescendants(bool v) { clips_descendants = v; }
    bool get_ClipsDescendants() const { return clips_descendants; }
    // Active = false: el SurfaceGui no recibe clics (gl_click_en ya lo mira).
    void set_Active(bool v)          { active = v; }
    bool get_Active() const          { return active; }
    void set_ResetOnSpawn(bool v)    { reset_on_spawn = v; }
    bool get_ResetOnSpawn() const    { return reset_on_spawn; }
    void set_ZIndexBehavior(int v)   { zindex_behavior = v; }
    int  get_ZIndexBehavior() const  { return zindex_behavior; }

    // Geometria de la cara donde vive el cartel: tamaño en studs (w,h), giro y
    // desplazamiento respecto del Part, y el lienzo en pixeles. La comparten el
    // dibujado y la deteccion de pulsaciones, para que no puedan discrepar.
    // Face de Roblox: Right=0, Top=1, Back=2, Left=3, Bottom=4, Front=5.
    void _gl_face_geom(float& w, float& h, Vector3& rot, Vector3& off, Vector2& raiz) const {
        Vector3 psz(4, 4, 1);
        Node* par = get_parent();
        if (par) {
            Variant v = par->get(StringName("Size"));
            if (v.get_type() == Variant::VECTOR3) psz = v;
        }
        const float eps = 0.02f + z_offset * 0.05f;
        switch (face) {
            case 0: w = psz.z; h = psz.y; rot = Vector3(0,  90, 0); off = Vector3( psz.x*0.5f+eps, 0, 0); break;  // Right
            case 1: w = psz.x; h = psz.z; rot = Vector3(-90, 0, 0); off = Vector3(0,  psz.y*0.5f+eps, 0); break;  // Top
            // Front y Back estaban INTERCAMBIADAS. Un Label3D de Godot mira a
            // +Z, y la cara Front de Roblox es la de -Z: con rot 0 el cartel
            // quedaba mirando hacia DENTRO de la pieza, y lo que se veia desde
            // fuera era su espalda en espejo (el texto al reves de los carteles
            // de mejoras). Front necesita giro de 180 y Back quedarse en 0.
            case 2: w = psz.x; h = psz.y; rot = Vector3(0,   0, 0); off = Vector3(0, 0,  psz.z*0.5f+eps); break;  // Back
            case 3: w = psz.z; h = psz.y; rot = Vector3(0, -90, 0); off = Vector3(-psz.x*0.5f-eps, 0, 0); break;  // Left
            case 4: w = psz.x; h = psz.z; rot = Vector3( 90, 0, 0); off = Vector3(0, -psz.y*0.5f-eps, 0); break;  // Bottom
            default:w = psz.x; h = psz.y; rot = Vector3(0, 180, 0); off = Vector3(0, 0, -psz.z*0.5f-eps); break;  // Front
        }
        // ── EL LIENZO LO DECIDE SizingMode, NO "si hay CanvasSize" ───────
        // Enum.SurfaceGuiSizingMode: FixedSize=0, PixelsPerStud=1.
        //   FixedSize      -> manda CanvasSize.
        //   PixelsPerStud  -> Roblox IGNORA CanvasSize y saca el lienzo de la
        //                     cara del Part x PixelsPerStud.
        // Aqui se usaba CanvasSize siempre que viniera puesto, sin mirar el
        // modo. Medido en el cartel "V.I.P!" de la montaña (Part de
        // 89,4 x 45,0 x 54,7 con SizingMode=1 y PixelsPerStud=50):
        //   lo que usabamos:  800 x 600   -> proporcion 1,33
        //   lo correcto:     4471 x 2250  -> proporcion 1,99
        // Proporcion equivocada = todo el layout estirado, que es de donde
        // salia una letra ocupando el 77% del cartel cuando declara el 48%.
        const bool manda_canvas = (sizing_mode == 0) &&
                                  canvas_size.x > 1.0f && canvas_size.y > 1.0f;
        raiz = manda_canvas
             ? canvas_size
             : Vector2(w * (float)pixels_per_stud, h * (float)pixels_per_stud);
        if (raiz.x < 1.0f || raiz.y < 1.0f) raiz = Vector2(100, 100);
    }

    // El lienzo del cartel en pixeles, para que sus hijos Control midan contra
    // EL, y no contra la ventana. Mismo problema y misma cura que en
    // BillboardGui::_gl_lienzo_px: los hijos de un SurfaceGui son Control
    // colgando de un Node3D, Godot no encuentra un padre Control y usaba el
    // viewport, asi que cualquier UIAspectRatioConstraint se resolvia con la
    // proporcion de la PANTALLA en vez de la de la cara del Part.
    Vector2 _gl_lienzo_px() const {
        float w = 4.0f, h = 4.0f;
        Vector3 rot, off;
        Vector2 raiz;
        _gl_face_geom(w, h, rot, off, raiz);
        return raiz;
    }

    // Un SurfaceGui va PEGADO a una cara del Part padre. Se orienta a esa cara
    // y se refleja el contenido con Label3D/Sprite3D.
    void _gl_rebuild_world() {
        remove_meta("_gl_wgui_sucio");   // se admite el siguiente aviso de cambio
        if (Engine::get_singleton()->is_editor_hint()) return;
        gl_world_gui_cull(this);
        gl_world_gui_mark(this);
        // Plantilla guardada (no cuelga de Workspace): fuera el reflejo. Sin
        // el borrado explicito, un cartel construido en el mundo y guardado
        // despues en ReplicatedStorage se seguia VIENDO.
        if (!gl_world_gui_in_world(this)) { gl_world_gui_borrar_reflejo(this); return; }

        float w = 4.0f, h = 4.0f;
        Vector3 rot, off;
        Vector2 raiz;
        _gl_face_geom(w, h, rot, off, raiz);
        set_position(off);
        set_rotation_degrees(rot);

        LocalVector<GLWorldGuiItem> items;
        for (int i = 0; i < get_child_count(); i++) {
            Node* c = get_child(i);
            if (c && !c->has_meta("__gl_world_gui"))
                gl_world_gui_collect(c, Vector2(), raiz, raiz, items, GL_WGUI_TOPE_ITEMS_SURFACE);
        }
        // Distancia de corte: si el juego no la fija, un valor generoso pero
        // finito para que 1185 carteles no se dibujen todos a la vez.
        const float md = (max_distance > 0.5f && max_distance < 4000.0f) ? max_distance : 220.0f;
        // AlwaysOnTop y Brightness se guardaban y no los leia nadie.
        gl_world_gui_build(this, items, MAX(0.2f, w), MAX(0.2f, h), false, md, 0.0f, always_on_top, false,
                           Vector2(), brightness);
    }

    // Traduce el punto del mundo donde pego el rayo del raton a coordenadas del
    // lienzo y pulsa el boton que haya debajo. Devuelve true si algo se pulso.
    //
    // Sin esto los botones de un SurfaceGui no existian para la entrada: en el
    // place de prueba el unico camino para subir el nivel del tycoon es
    // `btn.MouseButton1Click` sobre el cartel de "MejoraDelTycoon", asi que la
    // base se quedaba en el nivel 0 para siempre.
    bool gl_click_en(const Vector3& punto_mundo) {
        if (!enabled || !active || !is_inside_tree()) return false;
        float w = 4.0f, h = 4.0f;
        Vector3 rot, off;
        Vector2 raiz;
        _gl_face_geom(w, h, rot, off, raiz);
        if (w < 0.01f || h < 0.01f) return false;

        // A local del cartel. affine_inverse deshace tambien la escala, asi que
        // loc queda en las mismas unidades (studs) que w/h, igual que el reflejo.
        const Vector3 loc = get_global_transform().affine_inverse().xform(punto_mundo);
        const float cx = loc.x / w + 0.5f;
        const float cy = 0.5f - loc.y / h;    // Roblox: Y crece hacia ABAJO
        if (cx < 0.0f || cx > 1.0f || cy < 0.0f || cy > 1.0f) return false;

        const Vector2 punto(cx * raiz.x, cy * raiz.y);
        Control* mejor = nullptr;
        int z = 0;
        for (int i = 0; i < get_child_count(); i++) {
            Node* c = get_child(i);
            if (c && !c->has_meta("__gl_world_gui"))
                gl_world_gui_pick(c, Vector2(), raiz, punto, mejor, z);
        }
        if (!mejor) return false;
        mejor->call("_on_pressed");
        return true;
    }

    void _ready() override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        call_deferred("_gl_rebuild_world");
    }
};

// ────────────────────────────────────────────────────────────────────
//  Pulsar en el mundo: reparte un clic entre los SurfaceGui pegados a la
//  pieza que toco el rayo y los ClickDetector que cuelguen de ella.
//
//  Antes NADIE llamaba a ClickDetector::fire_click: el detector se creaba,
//  aceptaba conexiones y no disparaba nunca.
// ────────────────────────────────────────────────────────────────────
inline bool gl_repartir_clic_mundo(Node* golpeado, const Vector3& punto, Node* jugador) {
    if (!golpeado) return false;
    bool algo = false;
    for (int i = 0; i < golpeado->get_child_count(); i++) {
        Node* c = golpeado->get_child(i);
        if (SurfaceGui* sg = gl_cast<SurfaceGui>(c)) {
            if (sg->gl_click_en(punto)) algo = true;
        }
    }
    if (!algo) {
        for (int i = 0; i < golpeado->get_child_count(); i++) {
            if (ClickDetector* cd = gl_cast<ClickDetector>(golpeado->get_child(i))) {
                // MaxActivationDistance de Roblox: se mide contra el jugador.
                bool cerca = true;
                Node3D* j3 = gl_cast<Node3D>(jugador);
                Node3D* g3 = gl_cast<Node3D>(golpeado);
                if (j3 && g3 && cd->get_max_distance() > 0.01f)
                    cerca = gl_pos(j3).distance_to(gl_pos(g3)) <= cd->get_max_distance();
                if (cerca) { cd->fire_click(jugador); algo = true; }
            }
        }
    }
    return algo;
}

// Motor6D: en su propia cabecera (F7, animacion por capas).
#include "roblox_motor6d.h"

// ────────────────────────────────────────────────────────────────────
//  Tool — equippable inventory item
////
//  Tool — herramienta equippable del inventario
// ────────────────────────────────────────────────────────────────────
class RobloxTool : public Node3D {
    GDCLASS(RobloxTool, Node3D);

public:
    struct LuaCallback { lua_State* main_L; int ref; bool active = true; };

    std::vector<LuaCallback> equipped_cbs;
    std::vector<LuaCallback> unequipped_cbs;
    std::vector<LuaCallback> activated_cbs;
    std::vector<LuaCallback> deactivated_cbs;

    void _gl_disconnect(int ref, int64_t vm) {
        for (auto& cb : equipped_cbs)    if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
        for (auto& cb : unequipped_cbs)  if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
        for (auto& cb : activated_cbs)   if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
        for (auto& cb : deactivated_cbs) if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
    }

private:
    String  tool_tip         = "";
    bool    requires_handle  = true;
    bool    can_be_dropped   = true;
    bool    manually_activated = false;
    bool    grip_forward     = true;
    // Tool.Enabled de Roblox (true de fabrica): a false la Tool no se puede
    // usar (Activated no dispara). Faltaba entero y los inventarios custom de
    // los places reales mueren con "Enabled is not a valid member of Tool" al
    // anadir la primera herramienta — sin inventario no se equipa NADA.
    bool    tool_enabled     = true;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"),      &RobloxTool::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("set_tool_tip", "v"),           &RobloxTool::set_tool_tip);
        ClassDB::bind_method(D_METHOD("get_tool_tip"),                  &RobloxTool::get_tool_tip);
        ClassDB::bind_method(D_METHOD("set_can_be_dropped", "v"),     &RobloxTool::set_can_be_dropped);
        ClassDB::bind_method(D_METHOD("get_can_be_dropped"),           &RobloxTool::get_can_be_dropped);
        ClassDB::bind_method(D_METHOD("set_requires_handle", "v"),    &RobloxTool::set_requires_handle);
        ClassDB::bind_method(D_METHOD("get_requires_handle"),          &RobloxTool::get_requires_handle);
        ClassDB::bind_method(D_METHOD("set_tool_enabled", "v"),       &RobloxTool::set_tool_enabled);
        ClassDB::bind_method(D_METHOD("get_tool_enabled"),             &RobloxTool::get_tool_enabled);
        ClassDB::bind_method(D_METHOD("equip"),   &RobloxTool::equip);
        ClassDB::bind_method(D_METHOD("unequip"), &RobloxTool::unequip);
        ClassDB::bind_method(D_METHOD("activate"), &RobloxTool::activate);

        ADD_PROPERTY(PropertyInfo(Variant::STRING, "ToolTip"),        "set_tool_tip",       "get_tool_tip");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,   "CanBeDropped"),   "set_can_be_dropped", "get_can_be_dropped");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,   "RequiresHandle"), "set_requires_handle","get_requires_handle");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,   "Enabled"),        "set_tool_enabled",   "get_tool_enabled");

        ADD_SIGNAL(MethodInfo("Equipped",     PropertyInfo(Variant::OBJECT, "mouse")));
        ADD_SIGNAL(MethodInfo("Unequipped"));
        ADD_SIGNAL(MethodInfo("Activated"));
        ADD_SIGNAL(MethodInfo("Deactivated"));
    }

public:
    void set_tool_tip(const String& v)      { tool_tip = v; }
    String get_tool_tip() const             { return tool_tip; }
    void set_can_be_dropped(bool v)         { can_be_dropped = v; }
    bool get_can_be_dropped() const         { return can_be_dropped; }
    void set_requires_handle(bool v)        { requires_handle = v; }
    bool get_requires_handle() const        { return requires_handle; }
    void set_tool_enabled(bool v)           { tool_enabled = v; }
    bool get_tool_enabled() const           { return tool_enabled; }

    void add_equipped_cb(lua_State* L, int ref)     { equipped_cbs.push_back({L,ref,true}); }
    void add_unequipped_cb(lua_State* L, int ref)   { unequipped_cbs.push_back({L,ref,true}); }
    void add_activated_cb(lua_State* L, int ref)    { activated_cbs.push_back({L,ref,true}); }
    void add_deactivated_cb(lua_State* L, int ref)  { deactivated_cbs.push_back({L,ref,true}); }

    void equip(Node* player = nullptr) {
        emit_signal("Equipped", player);
        _fire(equipped_cbs, player);
    }
    void unequip() {
        emit_signal("Unequipped");
        _fire_noarg(unequipped_cbs);
    }
    void activate() {
        if (!tool_enabled) return;   // Roblox: con Enabled=false la Tool no dispara
        emit_signal("Activated");
        _fire_noarg(activated_cbs);
    }
    void deactivate() {
        emit_signal("Deactivated");
        _fire_noarg(deactivated_cbs);
    }

private:
    void _fire(std::vector<LuaCallback>& list, Node* arg) {
        if (list.empty()) return;
        // Copia defensiva: el handler puede conectar/desconectar (UAF si no).
        std::vector<LuaCallback> snap = list;
        for (size_t i = 0; i < snap.size(); i++) {
            auto& cb = snap[i];
            if (!cb.active || !gl_state_alive(cb.main_L)) continue;
            lua_State* th=lua_newthread(cb.main_L);
            lua_rawgeti(cb.main_L,LUA_REGISTRYINDEX,cb.ref);
            if(lua_isfunction(cb.main_L,-1)){
                lua_xmove(cb.main_L,th,1);
                if(arg){
                    GodotObjectWrapper* w=(GodotObjectWrapper*)lua_newuserdata(th,sizeof(GodotObjectWrapper));
                    gow_set(w,arg);
                    luaL_getmetatable(th,"GodotObject"); lua_setmetatable(th,-2);
                } else lua_pushnil(th);
                gl_resume_handler(th, 1);
            } else lua_pop(cb.main_L,1);
            lua_pop(cb.main_L,1);
        }
        for (int i=(int)list.size()-1; i>=0; --i)
            if (!list[i].active || !gl_state_alive(list[i].main_L)) list.erase(list.begin()+i);
    }
    void _fire_noarg(std::vector<LuaCallback>& list) {
        if (list.empty()) return;
        std::vector<LuaCallback> snap = list;   // copia defensiva (ver _fire)
        for (size_t i = 0; i < snap.size(); i++) {
            auto& cb = snap[i];
            if (!cb.active || !gl_state_alive(cb.main_L)) continue;
            lua_State* th=lua_newthread(cb.main_L);
            lua_rawgeti(cb.main_L,LUA_REGISTRYINDEX,cb.ref);
            if(lua_isfunction(cb.main_L,-1)){lua_xmove(cb.main_L,th,1);gl_resume_handler(th, 0);}
            else lua_pop(cb.main_L,1);
            lua_pop(cb.main_L,1);
        }
        for (int i=(int)list.size()-1; i>=0; --i)
            if (!list[i].active || !gl_state_alive(list[i].main_L)) list.erase(list.begin()+i);
    }
};

// ────────────────────────────────────────────────────────────────────
//  Backpack — player inventory container
////
//  Backpack — inventario del jugador
// ────────────────────────────────────────────────────────────────────
class Backpack : public Node {
    GDCLASS(Backpack, Node);

protected:
    static void _bind_methods() {}
};

#endif // ROBLOX_BILLBOARD_H

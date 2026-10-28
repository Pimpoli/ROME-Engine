#ifndef GL_CLONE_H
#define GL_CLONE_H

// ════════════════════════════════════════════════════════════════════
//  Instance:Clone() — copia rápida, sin el duplicate() genérico de Godot
//
//  El problema, medido en el place de prueba: cada brainrot son 103 nodos y
//  clonarlo costaba 4,8 ms. El juego saca tandas de ~17 de golpe, o sea ~1.750
//  nodos en UN frame: de ahí el parón.
//
//  Node::duplicate() de Godot construye la lista COMPLETA de propiedades del
//  nodo (un diccionario por propiedad) y luego copia una a una con get()/set().
//  Y la mayoría de esas propiedades no son de Roblox, son de la clase de Godot
//  que heredamos y que nunca tocamos:
//
//      clase              copiadas   de ellas nuestras
//      RobloxTextLabel      139          35
//      RobloxImageLabel     119          32
//      RobloxFrame          104          22
//      RobloxPart            78          30
//
//  El coste sale casi exactamente a 0,6 us por propiedad copiada. O sea que
//  copiar SOLO lo que define al objeto en Roblox es 3-4x menos trabajo.
//
//  Además, una pieza se fabrica sola su MeshInstance3D y su CollisionShape3D
//  al entrar en el árbol. Copiarlos es tirar el tiempo dos veces: se marcan
//  como internos y el clon los vuelve a crear él solo.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/node3d.hpp>
// class_db_singleton.hpp ANTES que core/class_db.hpp: es la que define el macro
// que le da a ClassDB los metodos de consulta (class_get_property_list...).
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include "gl_autopsia.h"

using namespace godot;

// Marca de "este hijo lo fabrica la clase sola": no se copia, se recrea.
#define GL_META_INTERNO "_gl_interno"

// Las clases de Godot de las que colgamos. Al subir por la herencia buscando
// nuestras propiedades, aquí se para. Si alguna falta, el clon sigue siendo
// CORRECTO (copia de más y va más lento), nunca al revés.
inline bool gl_es_base_de_godot(const StringName& c) {
    static const char* kBases[] = {
        "Node", "Node2D", "Node3D", "CanvasItem", "Control",
        "CollisionObject3D", "PhysicsBody3D", "RigidBody3D", "StaticBody3D",
        "AnimatableBody3D", "CharacterBody3D", "Area3D", "GeometryInstance3D",
        "VisualInstance3D", "MeshInstance3D", "CollisionShape3D", "GPUParticles3D",
        "Label", "Label3D", "Button", "BaseButton", "TextureRect", "Panel",
        "PanelContainer", "LineEdit", "ScrollContainer", "Container",
        "Sprite3D", "SpriteBase3D", "Camera3D", "Light3D", "DirectionalLight3D",
        "AudioStreamPlayer", "AudioStreamPlayer3D", "Viewport", "SubViewport",
        nullptr
    };
    // OJO: `StringName(kBases[i])` CONSTRUYE una StringName desde texto, con su
    // hash y su mutex, y aqui se hacian las 36 EN CADA LLAMADA — o sea una vez
    // por nodo clonado. Para una clase nuestra nunca hay coincidencia, asi que
    // se recorrian las 36 enteras y se destruian. Con 2.000-4.300 nodos por
    // oleada son 72.000-155.000 construcciones. Ahora se construyen UNA vez en
    // la vida del programa y luego solo se comparan punteros.
    static LocalVector<StringName> bases;
    if (bases.is_empty())
        for (int i = 0; kBases[i]; i++) bases.push_back(StringName(kBases[i]));
    for (uint32_t i = 0; i < bases.size(); i++) if (c == bases[i]) return true;
    return false;
}

// Propiedades de Godot que SÍ hacen falta: el sitio en el mundo y si se ve.
// Todo lo demás de la clase base (amortiguaciones, filtros de ratón, anclas,
// atajos de teclado...) no define nada en Roblox y lo reconstruye la clase.
//
// EXCEPCION MEDIDA — el recorte de texto. `autowrap_mode` parecia cosa interna
// de Godot y no lo era: con autowrap APAGADO, Godot le pone al Label un alto
// MINIMO igual al de su texto, o sea que la caja ya no puede encoger a lo que
// pide el UDim2. Y entonces TextScaled hace lo suyo — agrandar la letra hasta
// llenar la caja — sobre una caja inflada. Pescadilla que se muerde la cola.
// Medido en la pantalla de carga del place: la plantilla mide 21,8 px de alto
// con letra de 14, y su clon 53 px con letra de 37, mismos anclajes y mismo
// padre. En Roblox el tamano de una UI NUNCA depende de su contenido.
// Se copian solo si el nodo original las tiene (get() da nil si no).
inline const LocalVector<StringName>& gl_props_de_godot_imprescindibles() {
    static LocalVector<StringName> v;
    if (v.is_empty()) {
        v.push_back(StringName("transform"));
        v.push_back(StringName("visible"));
        v.push_back(StringName("autowrap_mode"));
        v.push_back(StringName("clip_text"));
        v.push_back(StringName("text_overrun_behavior"));
        v.push_back(StringName("custom_minimum_size"));
    }
    return v;
}

// Lista de propiedades a copiar para una clase, calculada UNA vez por clase.
inline const LocalVector<StringName>& gl_props_a_copiar(const StringName& clase) {
    static HashMap<StringName, LocalVector<StringName>> cache;
    if (const LocalVector<StringName>* hay = cache.getptr(clase)) return *hay;

    LocalVector<StringName> lista;
    // Se sube por la herencia recogiendo lo que declara CADA clase nuestra,
    // y se para al llegar a la de Godot.
    StringName c = clase;
    int guarda = 0;
    while (c != StringName() && !gl_es_base_de_godot(c) && guarda++ < 16) {
        TypedArray<Dictionary> propias = ClassDB::class_get_property_list(c, true);
        for (int i = 0; i < propias.size(); i++) {
            Dictionary d = propias[i];
            const String nombre = d.get("name", String());
            if (nombre.is_empty()) continue;
            const int uso = (int)d.get("usage", 0);
            if (!(uso & PROPERTY_USAGE_STORAGE)) continue;
            if (uso & PROPERTY_USAGE_READ_ONLY) continue;
            lista.push_back(StringName(nombre));
        }
        c = ClassDB::get_parent_class(c);
    }
    for (uint32_t i = 0; i < gl_props_de_godot_imprescindibles().size(); i++)
        lista.push_back(gl_props_de_godot_imprescindibles()[i]);

    cache[clase] = lista;
    return cache[clase];
}

// ¿Es una clase nuestra? Si no lo es (un nodo de Godot suelto que el juego haya
// metido), se clona con el duplicate() de siempre para no cambiar su semántica.
inline bool gl_clase_nuestra(const StringName& c) {
    // Memoizado por clase: la herencia de una clase no cambia en marcha, asi que
    // esto es constante. Antes se recalculaba por NODO. Mismo razonamiento que
    // gl_props_a_copiar, que ya cachea por clase unas lineas mas arriba.
    static HashMap<StringName, bool> cache;
    if (const bool* hay = cache.getptr(c)) return *hay;
    const bool r = !gl_es_base_de_godot(c) && !gl_props_a_copiar(c).is_empty();
    cache.insert(c, r);
    return r;
}

inline Node* gl_clonar_impl(Node* orig, int prof);

inline Node* gl_clonar(Node* orig, int prof = 0) {
    if (prof == 0 && gl_aut_on()) {
        GLAutTiempo t(AUT_CLONE);
        return gl_clonar_impl(orig, 0);
    }
    return gl_clonar_impl(orig, prof);
}

inline Node* gl_clonar_impl(Node* orig, int prof) {
    if (!orig || prof > 64) return nullptr;
    const StringName clase = orig->get_class();

    // Lo que no es nuestro se copia como siempre: no merece la pena arriesgar
    // la semántica de un nodo de Godot por unos microsegundos.
    if (!gl_clase_nuestra(clase)) return orig->duplicate();

    Object* obj = ClassDB::instantiate(clase);
    Node* copia = gl_cast<Node>(obj);
    if (!copia) {
        if (obj) memdelete(obj);
        return orig->duplicate();   // respaldo
    }
    copia->set_name(orig->get_name());

    const LocalVector<StringName>& props = gl_props_a_copiar(clase);
    for (uint32_t i = 0; i < props.size(); i++) {
        // Si el nodo no tiene esa propiedad, get() da nil: escribirla seria un
        // error en la consola. La lista es comun a todas las clases y no todas
        // tienen las de texto (un Frame no tiene autowrap_mode).
        const Variant valor = orig->get(props[i]);
        if (valor.get_type() == Variant::NIL) continue;
        copia->set(props[i], valor);
    }

    // Los atributos de Roblox, los tags y la clase original viven en metadatos.
    TypedArray<StringName> metas = orig->get_meta_list();
    // El netId NO se copia: un clon es una instancia nueva y no puede heredar la
    // identidad de red del original. Antes se copiaba aqui y luego se quitaba
    // con gl_strip_netid_recursive, que recorre el subarbol clonado ENTERO una
    // segunda vez (dos llamadas al motor por nodo, ~2.000-4.300 nodos por
    // oleada). Filtrandolo en el sitio, esa pasada sobra.
    // Los shadow "_glrep_*" SI se conservan: los usa el snapshot cuando el clon
    // se replique.
    static const StringName SN_NETID("_gl_netid");
    for (int i = 0; i < metas.size(); i++) {
        const StringName m = metas[i];
        if (m == SN_NETID) continue;
        copia->set_meta(m, orig->get_meta(m));
    }
    // EL NOMBRE DE ROBLOX TIENE QUE SOBREVIVIR AL CLONADO. En Roblox dos
    // hermanos pueden llamarse igual; Godot no lo permite y renombra el clon a
    // "@RBXModel@1234" en cuanto choca con uno existente. Si no dejamos aqui el
    // nombre bueno, el juego guarda el clon en su cache, lo busca por nombre, no
    // lo encuentra y vuelve a clonar — la fuga que se comia 3.400 nodos cada 40
    // segundos. Ver gl_rbx_name en luau_api.h.
    if (!copia->has_meta("__rbx_name"))
        copia->set_meta("__rbx_name", String(orig->get_name()));

    for (int i = 0; i < orig->get_child_count(); i++) {
        Node* h = orig->get_child(i);
        if (!h) continue;
        if (h->has_meta(GL_META_INTERNO)) continue;   // lo recrea la clase sola
        static const StringName k_no_arch("_gl_no_archivable");
        if (h->has_meta(k_no_arch)) continue;         // Archivable = false: no se clona
        if (Node* ch = gl_clonar_impl(h, prof + 1)) copia->add_child(ch);
    }
    return copia;
}

#endif // GL_CLONE_H

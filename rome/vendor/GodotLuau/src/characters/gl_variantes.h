#ifndef GL_VARIANTES_H
#define GL_VARIANTES_H

// ════════════════════════════════════════════════════════════════════
//  MaterialVariant de Roblox: las texturas PROPIAS de un juego.
//
//  Como en Roblox: una pieza con Material == BaseMaterial y MaterialVariant ==
//  Name de una MaterialVariant de MaterialService dibuja el ColorMap de esa
//  variante MULTIPLICADO por su Color, en mosaico de StudsPerTile studs que
//  empieza en una esquina de cada cara (MaterialPattern Regular). Si la
//  variante no existe (todavia), la pieza se dibuja con su material base.
//
//  Antes MaterialVariant era un nodo vacio y el nombre solo se guardaba: el
//  tema Minecraft de Build a Mountain (adoquin, tablones, cesped, montañas) se
//  veia de colores lisos y en Roblox con sus texturas.
//
//  Se dibuja con el shader de caras de gl_superficies.h, que ya proyecta UNA
//  vez por cara desde la esquina (sin triplanar) y multiplica el color por
//  gl_tex. Por ahora solo en bloques (la caja unitaria del shader).
//
//  Se incluye al final de roblox_part.h (usa RobloxPart y gl_asset.h).
// ════════════════════════════════════════════════════════════════════

#include <algorithm>
#include "gl_cast.h"
#include <vector>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include "../importer/rbx_classmap.h"   // rbx::map_material

// Una propiedad de la variante: la que puso un script (_glp_) o la que trajo el
// archivo (meta con su nombre, como deja el importador lo que no es de Godot).
inline Variant gl_var_prop(Node* n, const char* k) {
    const String g = String("_glp_") + k;
    if (n->has_meta(g)) return n->get_meta(g);
    if (n->has_meta(k)) return n->get_meta(k);
    return Variant();
}

// Numero de un enum guardado como numero o como EnumItem convertido a tabla.
inline int gl_var_enum(const Variant& v, int defecto) {
    if (v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT) return (int)(int64_t)v;
    if (v.get_type() == Variant::DICTIONARY) {
        const Dictionary d = v;
        if (d.has("Value")) return (int)(int64_t)d["Value"];
    }
    return defecto;
}

inline HashMap<String, Ref<Texture2D>>& gl_var_cache_tex() {
    static HashMap<String, Ref<Texture2D>> c;
    return c;
}

// La textura de un ColorMap CON mipmaps: va en mosaico por todo el mapa y, sin
// ellos, a distancia sale moire (lo mismo que paso con los studs).
inline Ref<Texture2D> gl_var_textura(const String& id) {
    if (id.is_empty()) return Ref<Texture2D>();
    HashMap<String, Ref<Texture2D>>& cache = gl_var_cache_tex();
    if (Ref<Texture2D>* t = cache.getptr(id)) return *t;
    Ref<Texture2D> res;
    const String fichero = gl_is_roblox_asset(id) ? gl_local_asset_file(id) : String();
    if (!fichero.is_empty()) {
        Ref<Image> img = Image::load_from_file(fichero);
        if (img.is_valid() && !img->is_empty()) {
            img->generate_mipmaps();
            res = ImageTexture::create_from_image(img);
        }
    }
    // Solo lo encontrado: un .gdlu puede escribir la imagen despues.
    if (res.is_valid()) cache.insert(id, res);
    return res;
}

// Las piezas que han pedido una variante alguna vez (por id). Cuando entra o
// cambia una variante solo se miran estas, no el juego entero.
inline HashSet<uint64_t>& gl_var_piezas() { static HashSet<uint64_t> s; return s; }
inline std::vector<uint64_t>& gl_var_pendientes() { static std::vector<uint64_t> v; return v; }

inline void gl_variantes_liberar() {
    gl_var_cache_tex().clear();
    gl_var_piezas().clear();
    gl_var_pendientes().clear();
}

// MaterialService del juego de `desde`: el hijo con ese nombre de algun
// antepasado (el DataModel).
inline Node* gl_var_servicio(Node* desde) {
    static const NodePath k_ms("MaterialService");
    for (Node* n = desde; n; n = n->get_parent())
        if (Node* ms = n->get_node_or_null(k_ms)) return ms;
    return nullptr;
}

inline String gl_var_nombre(const Node* n) {
    if (n->has_meta("__rbx_name")) return n->get_meta("__rbx_name");
    return String(n->get_name());
}

// La variante que le toca a una pieza: textura y StudsPerTile. false si no
// tiene, no existe o es de otro material base (entonces va el material base).
inline bool gl_variante_de(Node* pieza, int material, Ref<Texture2D>& tex, float& studs) {
    static const StringName k_mv("MaterialVariant");
    // Interruptor de medida (A/B): GL_SIN_VARIANTES=1 dibuja las piezas sin la
    // textura de su MaterialVariant, para saber cuanto cuesta en la GPU.
    static const bool sin_variantes = OS::get_singleton() &&
        OS::get_singleton()->get_environment("GL_SIN_VARIANTES") == "1";
    if (sin_variantes) return false;
    if (!pieza || !pieza->has_meta(k_mv)) return false;
    gl_var_piezas().insert((uint64_t)pieza->get_instance_id());
    if (!pieza->is_inside_tree()) return false;
    const String nombre = pieza->get_meta(k_mv);
    if (nombre.is_empty()) return false;
    Node* ms = gl_var_servicio(pieza);
    if (!ms) return false;
    // MaterialService trae una docena como mucho: se recorre entero (las
    // variantes pueden ir dentro de carpetas).
    std::vector<Node*> pila;
    pila.push_back(ms);
    while (!pila.empty()) {
        Node* n = pila.back();
        pila.pop_back();
        for (int i = 0; i < n->get_child_count(); i++) pila.push_back(n->get_child(i));
        if (n == ms || !n->is_class("MaterialVariant") || gl_var_nombre(n) != nombre) continue;
        const int base = rbx::map_material(gl_var_enum(gl_var_prop(n, "BaseMaterial"), 256));
        if (base != material) continue;
        Variant cm = gl_var_prop(n, "ColorMap");
        if (cm.get_type() != Variant::STRING) cm = gl_var_prop(n, "ColorMapContent");
        tex = gl_var_textura(cm.get_type() == Variant::STRING ? String(cm) : String());
        if (!tex.is_valid()) return false;
        const Variant spt = gl_var_prop(n, "StudsPerTile");
        studs = (spt.get_type() == Variant::FLOAT || spt.get_type() == Variant::INT) ? (float)spt : 10.0f;
        return true;
    }
    return false;
}

// Una variante entro en el arbol: se apunta y su repintado va diferido.
inline void gl_variantes_apuntar(Node* variante) {
    gl_var_pendientes().push_back((uint64_t)variante->get_instance_id());
}

// Las variantes apuntadas entraron o cambiaron: se repintan las piezas que las
// usan. Antes cada variante recorria el juego entero (20.000 nodos, ~12 ms):
// las 8 de Build a Mountain eran 100 ms del primer frame. Ahora las que
// entran juntas se atienden en UNA pasada, y solo sobre las piezas apuntadas.
inline void gl_variantes_refrescar(Node* variante) {
    std::vector<uint64_t> ids;
    ids.swap(gl_var_pendientes());
    if (variante) ids.push_back((uint64_t)variante->get_instance_id());
    std::vector<String> nombres;
    for (const uint64_t id : ids) {
        Node* v = gl_cast<Node>(ObjectDB::get_instance(ObjectID(id)));
        if (!v || !v->is_inside_tree()) continue;
        const String nombre = gl_var_nombre(v);
        if (std::find(nombres.begin(), nombres.end(), nombre) == nombres.end()) nombres.push_back(nombre);
    }
    if (nombres.empty()) return;
    static const StringName k_mv("MaterialVariant");
    // Primero se juntan y luego se repintan: repintar vuelve a apuntar la pieza
    // en gl_var_piezas, y no se puede tocar el conjunto mientras se recorre.
    std::vector<RobloxPart*> tocan;
    std::vector<uint64_t> muertas;
    for (const uint64_t id : gl_var_piezas()) {
        RobloxPart* p = gl_cast<RobloxPart>(ObjectDB::get_instance(ObjectID(id)));
        if (!p) { muertas.push_back(id); continue; }
        if (!p->is_inside_tree() || !p->has_meta(k_mv)) continue;
        const String nombre = p->get_meta(k_mv);
        if (std::find(nombres.begin(), nombres.end(), nombre) != nombres.end()) tocan.push_back(p);
    }
    for (const uint64_t id : muertas) gl_var_piezas().erase(id);
    for (RobloxPart* p : tocan) p->gl_refrescar_material();
}

#endif // GL_VARIANTES_H

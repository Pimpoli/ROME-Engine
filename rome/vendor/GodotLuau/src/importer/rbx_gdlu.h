#ifndef RBX_GDLU_H
#define RBX_GDLU_H
#include "gl_cast.h"

// Orden de los trozos "1","2",...: por numero, no por como llegaron.
struct GDLUOrdenTrozos {
    bool operator()(const Node *a, const Node *b) const {
        return String(a->get_name()).to_int() < String(b->get_name()).to_int();
    }
};

// Metodos de RBXImporter: el paquete .gdlu del GodotLuau Exporter (el place
// con sus imagenes, mallas y animaciones) y su Lighting.
// Va aparte de rbx_importer.h (pasaba de 2.000 lineas) y se incluye desde
// alli, justo despues de la clase.

// Los trozos "1","2",... de una carpeta del paquete, en orden y juntos.
inline String gdlu_base64_de(Node *caja) {
    Vector<Node *> trozos;
    for (int i = 0; i < caja->get_child_count(); i++) trozos.push_back(caja->get_child(i));
    trozos.sort_custom<GDLUOrdenTrozos>();
    String b64;
    for (int i = 0; i < trozos.size(); i++) {
        const Variant v = trozos[i]->get(StringName("Value"));
        if (v.get_type() == Variant::STRING) b64 += String(v);
    }
    return b64;
}

// ── Mallas (GDLU_Mallas/<id>) ──────────────────────────────────────
// El exportador lee cada MeshId con EditableMesh y la manda en binario
// (little-endian) dentro de base64:
//     "GLM1"  u32 vertices  u32 triangulos  u32 banderas (1 normales, 2 uv)
//     vertices x 3 f32 posicion  [x 3 f32 normal]  [x 2 f32 uv]
//     triangulos x 3 u32 indices
// Roblox y Godot comparten ejes (Y arriba, -Z adelante) y studs. El sentido
// de cada triangulo se corrige con su normal: asi da igual cual use Roblox.
// La malla queda en assets/rbx/<id>.res y la recogen solas las MeshPart y
// los FileMesh con ese id (gl_malla_propia.h).
inline Ref<ArrayMesh> gdlu_malla_de_bytes(const PackedByteArray &b) {
    if (b.size() < 16 || b[0] != 'G' || b[1] != 'L' || b[2] != 'M' || b[3] != '1') return Ref<ArrayMesh>();
    const uint8_t *p = b.ptr();
    auto u32 = [p](int64_t o) { uint32_t v; memcpy(&v, p + o, 4); return v; };
    auto f32 = [p](int64_t o) { float v; memcpy(&v, p + o, 4); return v; };
    const uint32_t nv = u32(4), nt = u32(8), flags = u32(12);
    const bool con_n = flags & 1, con_uv = flags & 2;
    const int64_t necesario = 16 + (int64_t)nv * (12 + (con_n ? 12 : 0) + (con_uv ? 8 : 0)) + (int64_t)nt * 12;
    if (nv == 0 || nt == 0 || nv > 2000000 || nt > 2000000 || b.size() < necesario) return Ref<ArrayMesh>();
    PackedVector3Array vs, ns;
    PackedVector2Array uvs;
    PackedInt32Array idx;
    vs.resize(nv);
    int64_t o = 16;
    for (uint32_t i = 0; i < nv; i++, o += 12) vs.set(i, Vector3(f32(o), f32(o + 4), f32(o + 8)));
    if (con_n) { ns.resize(nv); for (uint32_t i = 0; i < nv; i++, o += 12) ns.set(i, Vector3(f32(o), f32(o + 4), f32(o + 8))); }
    if (con_uv) { uvs.resize(nv); for (uint32_t i = 0; i < nv; i++, o += 8) uvs.set(i, Vector2(f32(o), f32(o + 4))); }
    idx.resize(nt * 3);
    for (uint32_t t = 0; t < nt; t++, o += 12) {
        uint32_t a = u32(o), c = u32(o + 4), d = u32(o + 8);
        if (a >= nv || c >= nv || d >= nv) return Ref<ArrayMesh>();
        // Sentido: el que deja la normal geometrica del lado de las normales.
        if (con_n) {
            const Vector3 g = (vs[c] - vs[a]).cross(vs[d] - vs[a]);
            if (g.dot(ns[a] + ns[c] + ns[d]) < 0.0f) { const uint32_t x = c; c = d; d = x; }
        }
        idx.set(t * 3, (int32_t)a); idx.set(t * 3 + 1, (int32_t)c); idx.set(t * 3 + 2, (int32_t)d);
    }
    Array arr;
    arr.resize(Mesh::ARRAY_MAX);
    arr[Mesh::ARRAY_VERTEX] = vs;
    if (con_n) arr[Mesh::ARRAY_NORMAL] = ns;
    if (con_uv) arr[Mesh::ARRAY_TEX_UV] = uvs;
    arr[Mesh::ARRAY_INDEX] = idx;
    Ref<ArrayMesh> m;
    m.instantiate();
    m->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
    // Las caras para la colision (gl_puntos_malla), en la propia malla: una malla
    // cargada no guarda copia de sus vertices y leerlos despues es leerlos de la
    // GPU, esperandola (1,4 s de la carga de Build a Mountain).
    PackedVector3Array caras;
    caras.resize((int64_t)nt * 3);
    for (int64_t i = 0; i < (int64_t)nt * 3; i++) caras.set(i, vs[idx[i]]);
    m->set_meta("_gl_caras", caras);
    return m;
}

inline bool RBXImporter::_gdlu_malla(Node *caja) {
    const String id = String(caja->get_name()).strip_edges();
    if (id.is_empty() || !id.is_valid_int()) return false;
    const PackedByteArray crudo = Marshalls::get_singleton()->base64_to_raw(gdlu_base64_de(caja));
    const Ref<ArrayMesh> m = gdlu_malla_de_bytes(crudo);
    if (m.is_null()) return false;
    DirAccess::make_dir_recursive_absolute(_gdlu_dir_assets());
    const String ruta = _gdlu_dir_assets() + id + ".res";
    if (ResourceSaver::get_singleton()->save(m, ruta) != OK) return false;
    // Al indice, y lo que ya se busco de este id (y dio "no esta") se olvida;
    // la malla queda a mano aunque el archivo aun no este importado.
    gl_asset_indice_anotar(id, ".res");
    gl_mallas_locales()[String("rbxassetid://") + id] = m;
    return true;
}

// Junta los trozos base64 de una imagen, la decodifica y la deja en PNG.
inline bool RBXImporter::_gdlu_imagen(Node *caja) {
    const String nom = caja->get_name();
    PackedStringArray partes = nom.split("_");
    if (partes.size() < 3) return false;
    const String id = partes[0];
    const int w = partes[1].to_int();
    const int h = partes[2].to_int();
    if (w <= 0 || h <= 0) return false;

    // Los trozos van numerados ("1", "2", ...): se ordenan por numero, no
    // por el orden del arbol, para no depender de como llegaron.
    Vector<Node *> trozos;
    for (int i = 0; i < caja->get_child_count(); i++) trozos.push_back(caja->get_child(i));
    for (int i = 1; i < trozos.size(); i++) {
        Node *t = trozos[i];
        int j = i - 1;
        while (j >= 0 && String(trozos[j]->get_name()).to_int() > String(t->get_name()).to_int()) {
            trozos.write[j + 1] = trozos[j];
            j--;
        }
        trozos.write[j + 1] = t;
    }
    String b64;
    for (int i = 0; i < trozos.size(); i++) {
        const Variant v = trozos[i]->get(StringName("Value"));
        if (v.get_type() == Variant::STRING) b64 += String(v);
    }
    if (b64.is_empty()) return false;

    PackedByteArray crudo = Marshalls::get_singleton()->base64_to_raw(b64);
    const int64_t esperado = (int64_t)w * h * 4;
    if (crudo.size() < esperado) return false;
    if (crudo.size() > esperado) crudo.resize(esperado);

    Ref<Image> img = Image::create_from_data(w, h, false, Image::FORMAT_RGBA8, crudo);
    if (img.is_null()) return false;
    DirAccess::make_dir_recursive_absolute(_gdlu_dir_assets());
    if (img->save_png(_gdlu_dir_assets() + id + ".png") != OK) return false;
    gl_asset_indice_anotar(id, ".png");
    return true;
}

// El plugin manda las props de Lighting como texto (tostring de Luau):
// "12.5", "true", "0.27451, 0.27451, 0.27451". Aqui se devuelven al tipo
// que toca antes de aplicarlas.
inline Variant RBXImporter::_gdlu_valor(const String &s) {
    const String t = s.strip_edges();
    if (t == "true")  return true;
    if (t == "false") return false;
    if (t.find(",") >= 0) {
        PackedStringArray p = t.split(",");
        if (p.size() == 3)
            return Color(p[0].to_float(), p[1].to_float(), p[2].to_float());
        if (p.size() == 2)
            return Vector2(p[0].to_float(), p[1].to_float());
    }
    if (t.is_valid_float()) return t.to_float();
    return t;
}

inline void RBXImporter::_gdlu_lighting(Node *sv) {
    const Variant v = sv->get(StringName("Value"));
    if (v.get_type() != Variant::STRING) return;
    Ref<JSON> js; js.instantiate();
    if (js->parse(String(v)) != OK) return;
    const Variant datos = js->get_data();
    if (datos.get_type() != Variant::DICTIONARY) return;
    luz_props = datos;
    report["gdlu_lighting"] = luz_props;
}

// Se aplica al FINAL, cuando el servicio Lighting ya existe.
inline void RBXImporter::_gdlu_aplicar_lighting() {
    if (!luz_destino || luz_props.is_empty()) return;
    int puestas = 0;
    Array claves = luz_props.keys();
    for (int i = 0; i < claves.size(); i++) {
        const String k = claves[i];
        const Variant val = luz_props[claves[i]];
        if (val.get_type() != Variant::STRING) continue;
        if (!_props_of(luz_destino).has(k)) continue;
        luz_destino->set(StringName(k), _gdlu_valor(String(val)));
        puestas++;
    }
    report["gdlu_lighting_aplicadas"] = puestas;
}

// Las props de StarterPlayer, Players y Workspace. El plugin solo clona los
// HIJOS de cada servicio, asi que sin esto se perdian la velocidad de andar,
// el R6/R15 del juego, la gravedad o el tiempo de reaparicion. Igual que el
// .rbxl: propiedad si la clase de Godot la tiene; si no, meta con su nombre.
inline void RBXImporter::_gdlu_aplicar_servicios(const Dictionary &por_servicio) {
    int puestas = 0;
    Array svcs = por_servicio.keys();
    for (int i = 0; i < svcs.size(); i++) {
        Node *svc = root->get_node_or_null(NodePath(String(svcs[i])));
        const Variant props = por_servicio[svcs[i]];
        if (!svc || props.get_type() != Variant::DICTIONARY) continue;
        const Dictionary d = props;
        Array claves = d.keys();
        for (int k = 0; k < claves.size(); k++) {
            const String nombre = claves[k];
            Variant v = d[claves[k]];
            if (v.get_type() == Variant::ARRAY && Array(v).size() == 3) {
                const Array a = v;
                v = Vector3(a[0], a[1], a[2]);
            } else if (v.get_type() == Variant::FLOAT && (double)v == Math::floor((double)v)) {
                v = (int64_t)(double)v;   // los EnumItem llegan como su Value
            }
            if (_props_of(svc).has(nombre)) svc->set(StringName(nombre), v);
            else _set_meta_safe(svc, nombre, v);
            puestas++;
        }
    }
    report["gdlu_servicios_aplicadas"] = puestas;
}

// Algunos contenedores no se dejan clonar en Studio (StarterPlayerScripts,
// StarterCharacterScripts): el exportador los manda como Folder con el
// atributo GDLU_Clase y sus hijos dentro. Aqui vuelven a ser de su clase: si
// no, sus LocalScripts no se copiaban al jugador y corrian sueltos donde
// estaban. Devuelve el nodo que queda en su sitio.
static Node *_gdlu_clase_real(Node *n) {
    static const StringName k_clase("GDLU_Clase");
    for (int i = 0; i < n->get_child_count(); i++) _gdlu_clase_real(n->get_child(i));
    if (!n->has_meta(k_clase)) return n;
    const String clase = n->get_meta(k_clase);
    const String gclass = rbx::map_class(clase);
    Node *real = gclass.is_empty() ? nullptr : gl_cast<Node>(ClassDB::instantiate(StringName(gclass)));
    if (!real) return n;
    real->set_name(n->get_name());
    real->set_meta("__rbx_class", clase);
    Vector<Node *> hijos;
    for (int i = 0; i < n->get_child_count(); i++) hijos.push_back(n->get_child(i));
    for (int i = 0; i < hijos.size(); i++) {
        n->remove_child(hijos[i]);
        real->add_child(hijos[i]);
        hijos[i]->set_owner(nullptr);
    }
    if (Node *padre = n->get_parent()) {
        const int pos = n->get_index();
        padre->remove_child(n);
        padre->add_child(real);
        padre->move_child(real, pos);
    }
    memdelete(n);
    return real;
}

// Devuelve true si el arbol traia un paquete GDLU (y ya quedo desenvuelto).
inline bool RBXImporter::_gdlu_desenvolver() {
    if (!root) return false;
    // El paquete puede ser la propia raiz o — lo normal — colgar de ella:
    // el importador fabrica un "Game" sintetico (el DataModel no existe
    // como instancia) y la carpeta GDLU del archivo entra como su hija.
    Node *paq = nullptr;
    if (root->get_node_or_null(NodePath("GDLU_Manifest"))) {
        paq = root;
    } else {
        for (int i = 0; i < root->get_child_count() && !paq; i++) {
            Node *c = root->get_child(i);
            if (c->get_node_or_null(NodePath("GDLU_Manifest"))) paq = c;
        }
    }
    if (!paq) return false;

    int imgs_ok = 0, imgs_mal = 0, mallas_ok = 0, mallas_mal = 0, animaciones = 0;
    Dictionary props_servicios;   // se aplican al final, con Players ya creado
    Vector<Node *> a_borrar;
    Vector<Node *> hijos;
    for (int i = 0; i < paq->get_child_count(); i++) hijos.push_back(paq->get_child(i));

    for (int i = 0; i < hijos.size(); i++) {
        Node *h = hijos[i];
        const String nom = h->get_name();
        if (nom == "GDLU_Manifest") {
            const Variant v = h->get(StringName("Value"));
            if (v.get_type() == Variant::STRING) {
                Ref<JSON> js; js.instantiate();
                if (js->parse(String(v)) == OK) report["gdlu_manifiesto"] = js->get_data();
            }
            a_borrar.push_back(h);
        } else if (nom == "GDLU_Lighting") {
            _gdlu_lighting(h);
            a_borrar.push_back(h);
        } else if (nom == "GDLU_Servicios") {
            const Variant v = h->get(StringName("Value"));
            Ref<JSON> js; js.instantiate();
            if (v.get_type() == Variant::STRING && js->parse(String(v)) == OK &&
                js->get_data().get_type() == Variant::DICTIONARY)
                props_servicios = js->get_data();
            a_borrar.push_back(h);
        } else if (nom == "GDLU_Mallas") {
            for (int k = 0; k < h->get_child_count(); k++) {
                if (_gdlu_malla(h->get_child(k))) mallas_ok++; else mallas_mal++;
            }
            a_borrar.push_back(h);
        } else if (nom == "GDLU_Animaciones") {
            // Las animaciones (KeyframeSequence por id) se quedan tal cual en una
            // carpeta oculta de Game: las usa la animacion por capas (F7).
            animaciones = h->get_child_count();
            paq->remove_child(h);
            root->add_child(h);
        } else if (nom == "GDLU_Assets") {
            for (int k = 0; k < h->get_child_count(); k++) {
                if (_gdlu_imagen(h->get_child(k))) imgs_ok++; else imgs_mal++;
            }
            a_borrar.push_back(h);
        } else if (nom.begins_with("SVC_")) {
            // Se crea el SERVICIO DE VERDAD y se le mudan los hijos.
            //
            // No basta con renombrar la carpeta: un "Workspace" que por
            // dentro es un Folder no trae camara, ni entorno, ni espacio
            // fisico — el juego importado salia entero GRIS y sin
            // responder. Se instancia igual que en un .rbxl (map_class +
            // ClassDB) para que las dos rutas den el mismo arbol.
            const String svc = nom.substr(4);
            const String gclass = rbx::map_class(svc);
            Node *real = gclass.is_empty() ? nullptr
                : gl_cast<Node>(ClassDB::instantiate(StringName(gclass)));
            if (!real) {
                RBXInstance *gi = memnew(RBXInstance);
                gi->set_rbx_class_name(svc);
                real = gi;
            }
            real->set_name(svc);
            real->set_meta("__rbx_class", svc);
            _gdlu_clase_real(h);   // contenedores que llegaron como Folder (ver arriba)
            Vector<Node *> nietos;
            for (int k = 0; k < h->get_child_count(); k++) nietos.push_back(h->get_child(k));
            for (int k = 0; k < nietos.size(); k++) {
                h->remove_child(nietos[k]);
                real->add_child(nietos[k]);
                nietos[k]->set_owner(nullptr);
            }
            root->add_child(real);
            if (svc == "Lighting") luz_destino = real;
            a_borrar.push_back(h);
        }
    }
    for (int i = 0; i < a_borrar.size(); i++) {
        a_borrar[i]->get_parent()->remove_child(a_borrar[i]);
        memdelete(a_borrar[i]);
    }
    // La carpeta GDLU ya esta vacia: fuera del arbol.
    if (paq != root && paq->get_child_count() == 0) {
        root->remove_child(paq);
        memdelete(paq);
    }
    _gdlu_aplicar_lighting();   // el servicio ya existe: ahora si

    // RE-APLICAR las imagenes que se pidieron ANTES de existir.
    //
    // Orden del importador: primero se leen las propiedades (ahi cada
    // ImageLabel hace set_image y, como el PNG todavia no estaba escrito,
    // se queda con la textura BLANCA y la marca __rbx_missing_asset), y
    // solo despues se desempaquetan los assets. Sin este repaso el blanco
    // se horneaba en la escena y ganaba al Image al cargar: 835 imagenes
    // en blanco con su PNG ya en disco (medido en el place de prueba).
    if (imgs_ok > 0) {
        int repuestas = 0;
        Vector<Node *> pila;
        pila.push_back(root);
        while (!pila.is_empty()) {
            Node *n = pila[pila.size() - 1];
            pila.remove_at(pila.size() - 1);
            for (int k = 0; k < n->get_child_count(); k++) pila.push_back(n->get_child(k));
            if (!n->has_meta("__rbx_missing_asset")) continue;
            if (!n->has_method("set_image")) continue;
            const String id = n->get_meta("__rbx_missing_asset");
            if (gl_local_asset_file(id).is_empty()) continue;
            n->call("set_image", id);          // ahora si encuentra el PNG
            repuestas++;
        }
        report["gdlu_imagenes_repuestas"] = repuestas;
        if (verbose)
            UtilityFunctions::print("[GDLU] imagenes repuestas tras escribirlas: ", repuestas);
    }

    // Servicios que el paquete NO trae porque en Studio estan VACIOS en
    // tiempo de edicion (Players no tiene hijos hasta que entra alguien),
    // pero sin los cuales el juego no arranca: sin Players no hay
    // LocalPlayer, ni PlayerGui donde clonar el StarterGui, ni el tick de
    // reaparicion — el jugador caia al vacio y no volvia, y ni un
    // LocalScript llegaba a correr.
    static const char *kImprescindibles[] = {
        "Players", "MaterialService", "Lighting", "ReplicatedFirst",
        "ReplicatedStorage", "ServerScriptService", "ServerStorage",
        "StarterGui", "StarterPack", "StarterPlayer", "Workspace", nullptr
    };
    int creados = 0;
    for (int i = 0; kImprescindibles[i]; i++) {
        const String svc = kImprescindibles[i];
        if (root->get_node_or_null(NodePath(svc))) continue;
        const String gclass = rbx::map_class(svc);
        Node *n = gclass.is_empty() ? nullptr
            : gl_cast<Node>(ClassDB::instantiate(StringName(gclass)));
        if (!n) continue;
        n->set_name(svc);
        n->set_meta("__rbx_class", svc);
        root->add_child(n);
        creados++;
    }
    report["gdlu_servicios_creados"] = creados;
    _gdlu_aplicar_servicios(props_servicios);

    // Las piezas que pidieron su malla antes de que existiera la vuelven a pedir.
    if (mallas_ok > 0) {
        int repuestas = 0;
        Vector<Node *> pila;
        pila.push_back(root);
        while (!pila.is_empty()) {
            Node *n = pila[pila.size() - 1];
            pila.remove_at(pila.size() - 1);
            for (int k = 0; k < n->get_child_count(); k++) pila.push_back(n->get_child(k));
            if (!n->has_meta("__rbx_missing_asset")) continue;
            const String id = n->get_meta("__rbx_missing_asset");
            if (gl_malla_local(id).is_null()) continue;
            if (RBXMeshPart *mp = gl_cast<RBXMeshPart>(n)) { mp->set_mesh_id(id); repuestas++; }
            else if (RBXMesh *sm = gl_cast<RBXMesh>(n)) { sm->set_mesh_id(id); repuestas++; }
        }
        report["gdlu_mallas_repuestas"] = repuestas;
    }

    report["gdlu"] = true;
    report["gdlu_imagenes"] = imgs_ok;
    report["gdlu_imagenes_fallidas"] = imgs_mal;
    report["gdlu_mallas"] = mallas_ok;
    report["gdlu_mallas_fallidas"] = mallas_mal;
    report["gdlu_animaciones"] = animaciones;
    if (verbose)
        UtilityFunctions::print("[GDLU] paquete desenvuelto: ", imgs_ok,
                                " imagenes a ", _gdlu_dir_assets(),
                                (imgs_mal > 0 ? String(" (") + itos(imgs_mal) + " fallidas)" : String()));
    return true;
}

#endif // RBX_GDLU_H

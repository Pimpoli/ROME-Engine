#ifndef LUAU_API_METODOS_H
#define LUAU_API_METODOS_H

// Metodos de Instance para Luau (FindFirstChild, SetAttribute, GetService...),
// CoreGui, el perfilado del puente, pivotes, zonas y rayos, y las respuestas
// de una instancia ya destruida.
// Va aparte de luau_api.h (pasaba de 2.000 lineas) y se incluye desde alli:
// usa sus tipos y helpers, y la lectura de propiedades (luau_api_indice_*.h)
// usa lo que hay aqui.

// ════════════════════════════════════════════════════════════════════
//  Instance methods (accessible from Lua)
////
//  Métodos de Instance (accesibles desde Lua)
// ════════════════════════════════════════════════════════════════════

// ── EL NOMBRE DE ROBLOX NO ES EL NOMBRE DEL NODO DE GODOT ────────────────────
// En Roblox DOS HERMANOS PUEDEN LLAMARSE IGUAL. Godot lo prohibe: si le pides
// un nombre que ya tiene un hermano, lo cambia por su cuenta a "@RBXModel@1234"
// sin avisar. Y eso rompe cualquier juego que guarde varias copias con el mismo
// nombre — un inventario, un cache de modelos, mobs generados…
//
// Medido en el place del usuario: el cache ReplicatedStorage/HiddenBrainrotsCache
// tenia 178 hijos, 87 de ellos renombrados por Godot. El juego guardaba un
// brainrot, lo buscaba por su nombre, NO lo encontraba (Godot se lo habia
// renombrado) y volvia a clonar. Cada 40 segundos entraban +3.405 nodos y +26 MB,
// y los FPS caian de 13,3 a 9,2 solos. Esa era la "degradacion con el tiempo".
//
// Solucion: el nombre que pide Roblox se guarda aparte, en una meta, y es ese el
// que ve y busca todo el mundo. El nombre de Godot queda como un detalle interno
// para que el arbol siga siendo valido.
#define GL_META_RBX_NAME "__rbx_name"

inline String gl_rbx_name(const Node* n) {
    if (!n) return String();
    if (n->has_meta(GL_META_RBX_NAME)) return n->get_meta(GL_META_RBX_NAME);
    return String(n->get_name());
}

inline void gl_set_rbx_name(Node* n, const String& nombre) {
    if (!n) return;
    if (gl_rbx_name(n) == nombre) return;
    n->set_name(nombre);                       // Godot puede uniquificarlo…
    if (String(n->get_name()) == nombre) {
        if (n->has_meta(GL_META_RBX_NAME)) n->remove_meta(GL_META_RBX_NAME);
    } else {
        n->set_meta(GL_META_RBX_NAME, nombre); // …y aqui queda el de verdad
    }
}

// ¿Se llama asi para Roblox? Compara contra el nombre real, no el de Godot.
inline bool gl_rbx_llamado(const Node* n, const StringName& nombre) {
    if (!n) return false;
    if (n->get_name() == nombre) return true;
    return n->has_meta(GL_META_RBX_NAME) &&
           String(n->get_meta(GL_META_RBX_NAME)) == String(nombre);
}

// add_child que conserva el nombre de Roblox. Escribir Name ya lo hacia, pero
// meter un hijo en un padre que tenia otro con su nombre no: Godot renombraba
// al que llegaba ("@Part@96") y el segundo Part de un modelo dejaba de
// llamarse "Part" (.Name, FindFirstChild y padre.Part).
inline void gl_add_child_rbx(Node* padre, Node* hijo) {
    const String nombre = gl_rbx_name(hijo);
    padre->add_child(hijo);
    if (String(hijo->get_name()) != nombre) hijo->set_meta(GL_META_RBX_NAME, nombre);
}

// ── AGARRE DE UNA TOOL (RightGrip) ───────────────────────────────────────────
//  En Roblox, equipar una Tool suelda su Handle a la mano derecha con una junta
//  llamada "RightGrip". Aqui la Tool solo se reparentaba al personaje: su Handle
//  no esta anclado, asi que se CAIA al suelo desde donde estuviera. Sin esto,
//  equipar "funciona" (la Tool esta en el personaje) pero no se ve nada en la
//  mano — que es justo el sintoma de "selecciono la tool y no pasa nada".
inline void gl_tool_soltar(Node* tool) {
    if (!tool) return;
    // El RightGrip cuelga de la mano (como en Roblox); el de versiones viejas,
    // de la propia Tool.
    Node* g = nullptr;
    if (tool->has_meta("_gl_grip")) {
        g = gl_cast<Node>(ObjectDB::get_instance(ObjectID((uint64_t)tool->get_meta("_gl_grip"))));
        tool->remove_meta("_gl_grip");
    }
    if (!g) g = tool->get_node_or_null(NodePath("RightGrip"));
    if (g) {
        g->queue_free();
        if (Node* p = g->get_parent()) p->remove_child(g);
    }
    // El Handle vuelve a ser un cuerpo normal.
    if (Node* h = tool->get_node_or_null(NodePath("Handle")))
        if (h->has_method("set_anchored")) h->call("set_anchored", false);
}

inline void gl_tool_agarrar(Node* pers, Node* tool) {
    if (!pers || !tool) return;
    Node3D* handle = gl_cast<Node3D>(tool->get_node_or_null(NodePath("Handle")));
    if (!handle) return;
    // La mano: RightHand (R15) o Right Arm (R6). Si no hay rig, no se agarra nada.
    Node3D* mano = gl_cast<Node3D>(pers->get_node_or_null(NodePath("RightHand")));
    if (!mano) mano = gl_cast<Node3D>(pers->get_node_or_null(NodePath("Right Arm")));
    if (!mano) return;
    gl_tool_soltar(tool);   // por si venia agarrada
    // Como Roblox: Handle = Mano * C0 * C1^-1, con C0 = el RightGripAttachment
    // de la mano (donde agarra) y C1 = Tool.Grip (por donde se coge el Handle).
    Transform3D c0(Basis(), Vector3(0.0f, -1.0f, 0.0f));   // sin attachment: la punta de un brazo R6
    if (Node3D* att = gl_cast<Node3D>(mano->get_node_or_null(NodePath("RightGripAttachment"))))
        c0 = att->get_transform();
    Transform3D c1;
    for (const char* k : { "_glp_Grip", "Grip" }) {
        if (!tool->has_meta(k)) continue;
        const Variant g = tool->get_meta(k);
        if (g.get_type() == Variant::TRANSFORM3D) { c1 = g; break; }
    }
    handle->set_global_transform(mano->get_global_transform() * c0 * c1.affine_inverse());
    // Un Weld con C0/C1, no un WeldConstraint: este captura la relativa en su
    // primer paso, cuando el Motor6D ya ha movido el brazo, y el Handle se
    // quedaba 0,3 studs detras de la mano mientras sonaba la animacion.
    Weld* w = memnew(Weld);
    w->set_name("RightGrip");
    w->set_c0(c0);
    w->set_c1(c1);
    mano->add_child(w);
    w->set_part0(mano);
    w->set_part1(handle);
    tool->set_meta("_gl_grip", (int64_t)w->get_instance_id());
}

inline int method_destroy(lua_State* L) {
    GLAutTiempo _aut_destroy(AUT_DESTROY);
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    Node* n = w ? gow_node(w) : nullptr;
    if (n) {
        // Replicación (1.14.5): si somos servidor y la instancia está replicada,
        // avisar a los clientes ANTES de liberarla aquí.
        if (gl_net_role() == 1 && n->has_meta("_gl_netid")) {
            if (NetworkService* ns = gl_cast<NetworkService>(ObjectDB::get_instance(gl_net_service_id())))
                ns->rep_unreplicate(n);   // avisa a los clientes + olvida el subárbol (recursivo)
        }
        // Como Roblox: la instancia desaparece del árbol de inmediato y se
        // libera la memoria. Sacarla del padre primero garantiza que deje de
        // verse/colisionar al instante (queue_free por sí solo difiere al final
        // del frame).
        // queue_free ANTES de sacarlo del padre: remove_child emite
        // tree_exiting, que es donde se dispara Instance.Destroying, y el relay
        // necesita ver el nodo ya marcado para distinguir un Destroy de un
        // simple cambio de Parent.
        // MEDIDO (no suponer): liberar los nodos al final del frame NO es lo que
        // alarga el frame de la oleada. Probado con un interruptor que se
        // saltaba la liberacion — frames de 243/188/252/241/301/255/241/261 ms
        // sin liberar contra 220/192/250/236/427/288/221/243 ms liberando. La
        // misma cifra. El coste esta en CREAR, no en destruir.
        n->queue_free();
        Node* parent = n->get_parent();
        if (parent) {
            if (gl_en_callback_fisica()) {
                // Flush de física en curso (handler de Touched): sacarlo del
                // árbol AHORA es ilegal en Godot. Se apaga al instante
                // (invisible y sin colisión, que es lo observable) y el árbol
                // lo limpia queue_free al final del frame.
                if (Node3D* n3 = gl_cast<Node3D>(n)) n3->set_visible(false);
                if (CollisionObject3D* co = gl_cast<CollisionObject3D>(n)) {
                    co->set_collision_layer(0);
                    co->set_collision_mask(0);
                }
                parent->call_deferred("remove_child", n);
            } else {
                parent->remove_child(n);
            }
        }
    }
    return 0;
}

// Quita el netId de replicación de un nodo y todo su subárbol. Node::duplicate()
// copia la metadata (props STORAGE), así que un :Clone() en el servidor heredaría
// el "_gl_netid" del original y le secuestraría la identidad de red. Los shadow
// "_glrep_*" SÍ se conservan: sirven al snapshot cuando el clon se replique.
inline void gl_strip_netid_recursive(Node* n) {
    if (!n) return;
    if (n->has_meta("_gl_netid")) n->remove_meta("_gl_netid");
    for (int i = 0; i < n->get_child_count(); i++) gl_strip_netid_recursive(n->get_child(i));
}

inline int method_clone(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    // Archivable = false: Clone devuelve nil, como en Roblox (el personaje de
    // un jugador nace asi; hay que poner Archivable = true para clonarlo).
    static const StringName k_no_arch("_gl_no_archivable");
    if (w && gow_node(w) && gow_node(w)->has_meta(k_no_arch)) { lua_pushnil(L); return 1; }
    if (w && gow_node(w)) {
        // Clone rapido: copia SOLO lo que define al objeto en Roblox, en vez del
        // duplicate() generico de Godot (que recorre las 78-139 propiedades de la
        // clase base). Ver gl_clone.h para los numeros medidos.
        Node* cloned = gl_clonar(gow_node(w));
        if (cloned) {
            // Ya NO hace falta barrer el subarbol: gl_clonar filtra el netId al copiar
            // los metadatos (ver gl_clone.h). Esto se queda solo para el camino de
            // respaldo, el duplicate() de Godot, que si copia la meta tal cual.
            if (!gl_clase_nuestra(cloned->get_class())) gl_strip_netid_recursive(cloned);   // el clon nace SIN netId (se le asigna uno nuevo al replicarse)
            wrap_node(L, cloned); return 1;
        }
    }
    lua_pushnil(L); return 1;
}

inline int method_clearallchildren(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    if (w && gow_node(w)) {
        // Replicación (1.14.5): en el servidor, avisar a los clientes que destruyan
        // cada hijo replicado (ClearAllChildren no pasa por :Destroy()).
        NetworkService* rns = (gl_net_role() == 1)
            ? gl_cast<NetworkService>(ObjectDB::get_instance(gl_net_service_id())) : nullptr;
        TypedArray<Node> ch = gow_node(w)->get_children();
        for (int i = 0; i < ch.size(); i++) {
            Node* n = gl_cast<Node>(ch[i]);
            if (!n) continue;
            if (rns && n->has_meta("_gl_netid")) rns->rep_unreplicate(n);
            n->queue_free();
            // queue_free difiere la liberacion al FINAL del frame, asi que sin
            // esto los hijos seguian en el arbol justo despues del
            // ClearAllChildren: GetChildren los devolvia, FindFirstChild los
            // encontraba y seguian dibujandose y colisionando el resto del frame.
            // En Roblox desaparecen al instante. Es el clasico "la lista de la UI
            // se duplica al refrescar". Es seguro porque `ch` ya es una
            // instantanea, no se esta iterando el arbol vivo (mismo patron que
            // usa method_destroy — incluida la puerta del flush de física).
            if (Node* p = n->get_parent()) {
                if (gl_en_callback_fisica()) {
                    if (Node3D* n3 = gl_cast<Node3D>(n)) n3->set_visible(false);
                    if (CollisionObject3D* co = gl_cast<CollisionObject3D>(n)) {
                        co->set_collision_layer(0);
                        co->set_collision_mask(0);
                    }
                    p->call_deferred("remove_child", n);
                } else {
                    p->remove_child(n);
                }
            }
        }
    }
    return 0;
}

inline int method_findfirstchild(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    const char* name = gl_str(L, 2);
    bool recursive  = lua_isboolean(L, 3) && lua_toboolean(L, 3);
    if (!w || !gow_node(w)) { lua_pushnil(L); return 1; }
    if (!recursive) {
        // Direct children only — Roblox default
        for (int i = 0; i < gow_node(w)->get_child_count(); i++) {
            Node* ch = gow_node(w)->get_child(i);
            if (ch && gl_rbx_llamado(ch, StringName(name))) { wrap_node(L, ch); return 1; }
        }
    } else {
        // Recursive — BFS through all descendants
        std::vector<Node*> queue;
        for (int i = 0; i < gow_node(w)->get_child_count(); i++) {
            Node* ch = gow_node(w)->get_child(i);
            if (ch) queue.push_back(ch);
        }
        while (!queue.empty()) {
            Node* cur = queue.back(); queue.pop_back();
            if (gl_rbx_llamado(cur, StringName(name))) { wrap_node(L, cur); return 1; }
            for (int i = 0; i < cur->get_child_count(); i++) {
                Node* ch = cur->get_child(i);
                if (ch) queue.push_back(ch);
            }
        }
    }
    lua_pushnil(L); return 1;
}

inline int method_findfirstancestor(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    const char* name = gl_str(L, 2);
    if (!w || !gow_node(w)) { lua_pushnil(L); return 1; }
    Node* cur = gow_node(w)->get_parent();
    while (cur) {
        if (gl_rbx_llamado(cur, StringName(name))) { wrap_node(L, cur); return 1; }
        cur = cur->get_parent();
    }
    lua_pushnil(L); return 1;
}

inline int method_findfirstancestorofclass(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    const char* cls = gl_str(L, 2);
    if (!w || !gow_node(w)) { lua_pushnil(L); return 1; }
    Node* cur = gow_node(w)->get_parent();
    while (cur) {
        if (gl_isa(cur, cls)) { wrap_node(L, cur); return 1; }
        cur = cur->get_parent();
    }
    lua_pushnil(L); return 1;
}

inline int method_isancestorof(lua_State* L) {
    GodotObjectWrapper* w  = (GodotObjectWrapper*)lua_touserdata(L, 1);
    GodotObjectWrapper* w2 = (GodotObjectWrapper*)lua_touserdata(L, 2);
    if (!w || !gow_node(w) || !w2 || !gow_node(w2)) { lua_pushboolean(L, false); return 1; }
    Node* cur = gow_node(w2)->get_parent();
    while (cur) {
        if (cur == gow_node(w)) { lua_pushboolean(L, true); return 1; }
        cur = cur->get_parent();
    }
    lua_pushboolean(L, false); return 1;
}

inline int method_isdescendantof(lua_State* L) {
    GodotObjectWrapper* w  = (GodotObjectWrapper*)lua_touserdata(L, 1);
    GodotObjectWrapper* w2 = (GodotObjectWrapper*)lua_touserdata(L, 2);
    if (!w || !gow_node(w) || !w2 || !gow_node(w2)) { lua_pushboolean(L, false); return 1; }
    Node* cur = gow_node(w)->get_parent();
    while (cur) {
        if (cur == gow_node(w2)) { lua_pushboolean(L, true); return 1; }
        cur = cur->get_parent();
    }
    lua_pushboolean(L, false); return 1;
}

inline int method_getfullname(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    if (!w || !gow_node(w)) { lua_pushstring(L, ""); return 1; }
    lua_pushstring(L, gl_instance_fullname(gow_node(w)).utf8().get_data());
    return 1;
}

inline int method_findfirstchild_whichisa(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    const char* cls = gl_str(L, 2);
    if (!w || !gow_node(w)) { lua_pushnil(L); return 1; }
    for (int i = 0; i < gow_node(w)->get_child_count(); i++) {
        Node* ch = gow_node(w)->get_child(i);
        if (ch && gl_isa(ch, cls)) { wrap_node(L, ch); return 1; }
    }
    lua_pushnil(L); return 1;
}

inline int method_findfirstchildofclass(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    const char* cls = gl_str(L, 2);
    Node* p = w ? gow_node(w) : nullptr;
    if (p) {
        // Punteros directos: get_children() fabricaria un array de Variants
        // solo para mirar los hijos uno a uno (ver method_getchildren).
        const int cnt = p->get_child_count();
        for (int i = 0; i < cnt; i++) {
            Node* n = p->get_child(i);
            if (n && gl_isa(n, cls)) { wrap_node(L, n); return 1; }
        }
    }
    lua_pushnil(L); return 1;
}

// get_child_count() + get_child(i) en vez de get_children().
//
// get_children() FABRICA un TypedArray de Variants: reserva memoria, mete un
// Variant por hijo (con su recuento de referencias) y cruza la frontera de la
// GDExtension por cada indexacion. get_child(i) devuelve el puntero directo.
// Mismo resultado, sin asignar nada.
inline int method_getchildren(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    lua_newtable(L);
    Node* n = w ? gow_node(w) : nullptr;
    if (n) {
        const int cnt = n->get_child_count();
        for (int i = 0; i < cnt; i++) {
            wrap_node(L, n->get_child(i));
            lua_rawseti(L, -2, i + 1);
        }
    }
    return 1;
}

// El mismo cambio, y aqui pesa MUCHO mas: antes se fabricaba un TypedArray por
// CADA nodo visitado, mas otro con el resultado entero.
//
// Por que importa: el spawn de un place real llama a GetDescendants VARIAS
// veces sobre el mismo modelo (WeldAll, OptimizarFisicas, poner Anchored...).
// Medido en el place de prueba: 32.000 lecturas del puente en el frame de la
// oleada, y este metodo es de los que mas aporta. Ahora se recorre con
// punteros y una pila propia — sin Variants, sin std::function y sin recursion
// (un modelo muy anidado ya no puede reventar la pila de C++).
inline int method_getdescendants(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    lua_newtable(L);
    Node* raiz = w ? gow_node(w) : nullptr;
    if (raiz) {
        std::vector<Node*> pila;
        std::vector<Node*> orden;
        const int n0 = raiz->get_child_count();
        // En orden inverso en la pila para que salgan en el orden del arbol.
        for (int i = n0 - 1; i >= 0; i--) pila.push_back(raiz->get_child(i));
        while (!pila.empty()) {
            Node* n = pila.back();
            pila.pop_back();
            if (!n) continue;
            orden.push_back(n);
            const int cnt = n->get_child_count();
            for (int i = cnt - 1; i >= 0; i--) pila.push_back(n->get_child(i));
        }
        for (size_t i = 0; i < orden.size(); i++) {
            wrap_node(L, orden[i]);
            lua_rawseti(L, -2, (int)i + 1);
        }
    }
    return 1;
}

inline int method_isa(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    const char* cls = gl_str(L, 2);
    lua_pushboolean(L, w && gl_isa(gow_node(w), cls));
    return 1;
}

// Instance:HasTag(tag), directo. Antes cada lectura creaba una closure, saltaba
// al shim Luau de CollectionService, este volvia al C++ del servicio (otra
// closure) y alli se buscaba en lineal entre todas las instancias con ese tag:
// 193.000 en 10 s de la carga de Build a Mountain.
inline int method_hastag(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    const char* tag = gl_str(L, 2);
    lua_pushboolean(L, w && tag && gl_tiene_tag(gow_node(w), String::utf8(tag)));
    return 1;
}

// ── Vigilantes de propiedades y atributos ─────────────────────────────
// Godot no avisa cuando cambia un metadato (los atributos) ni cuando un script
// escribe una propiedad, asi que se lleva un registro propio. Son cuatro
// señales distintas en Roblox, y antes compartian una sola lista: un atributo
// "Text" disparaba GetPropertyChangedSignal("Text") y Changed saltaba con los
// atributos (dos veces, la segunda con el nombre "__any__").
enum : uint8_t {
    GL_VIGILA_PROP     = 0,   // GetPropertyChangedSignal(nombre): sin argumentos
    GL_VIGILA_CHANGED  = 1,   // Changed: recibe el nombre de la propiedad
    GL_VIGILA_ATTR     = 2,   // GetAttributeChangedSignal(nombre): sin argumentos
    GL_VIGILA_ATTR_ANY = 3,   // AttributeChanged: recibe el nombre del atributo
};
struct GLAttrWatch { uint64_t node_id; String attr; lua_State* main_L; int ref; bool active; uint8_t kind; int id; };
inline std::vector<GLAttrWatch>& gl_attr_watchers() {
    static std::vector<GLAttrWatch> v;
    return v;
}
// Apunta un vigilante y devuelve su id, que es lo que usa Disconnect.
inline int gl_watch(Node* n, uint8_t kind, const String& nombre, lua_State* L, int fn_idx) {
    if (!n) return 0;
    lua_State* mL = gl_main_of(L);
    if (!mL) mL = L;
    lua_pushvalue(L, fn_idx);
    const int ref = gl_ref(L, -1);
    lua_pop(L, 1);
    static int siguiente = 0;
    const int id = ++siguiente;
    gl_attr_watchers().push_back({ (uint64_t)n->get_instance_id(), nombre, mL, ref, true, kind, id });
    return id;
}
inline void gl_unwatch(int id) {
    std::vector<GLAttrWatch>& v = gl_attr_watchers();
    for (int i = (int)v.size() - 1; i >= 0; i--) {
        if (v[i].id != id) continue;
        if (gl_state_alive(v[i].main_L)) gl_unref(v[i].main_L, v[i].ref);
        v.erase(v.begin() + i);
        return;
    }
}
// Dispara en `n` los vigilantes de la clase `k_nombre` con ese `nombre` (sin
// argumentos) y los de `k_todos` (con el nombre como argumento). Se apuntan
// primero en una lista aparte y se dispara despues: un manejador puede conectar
// o desconectar otros y realojar el registro. Uno que un manejador anterior
// desconecto ya no salta, como en Roblox.
inline void gl_fire_watchers(Node* n, uint8_t k_nombre, uint8_t k_todos, const String& nombre) {
    std::vector<GLAttrWatch>& v = gl_attr_watchers();
    if (!n || v.empty()) return;    // nadie escucha: ni se recorre
    const uint64_t node_id = (uint64_t)n->get_instance_id();
    struct Disparo { int id; lua_State* mL; int ref; bool con_nombre; };
    std::vector<Disparo> lista;
    for (int i = (int)v.size() - 1; i >= 0; i--) {
        const GLAttrWatch& w = v[i];
        if (!w.active || !gl_state_alive(w.main_L)) { v.erase(v.begin() + i); continue; }
        if (w.node_id != node_id) continue;
        if (w.kind == k_todos) lista.push_back({ w.id, w.main_L, w.ref, true });
        else if (w.kind == k_nombre && w.attr == nombre) lista.push_back({ w.id, w.main_L, w.ref, false });
    }
    if (lista.empty()) return;
    const CharString nombre8 = nombre.utf8();
    for (const Disparo& d : lista) {
        bool sigue = false;
        for (const GLAttrWatch& w : v) if (w.id == d.id) { sigue = w.active; break; }
        if (!sigue || !gl_state_alive(d.mL)) continue;
        lua_State* th = lua_newthread(d.mL);
        lua_rawgeti(d.mL, LUA_REGISTRYINDEX, d.ref);
        if (lua_isfunction(d.mL, -1)) {
            lua_xmove(d.mL, th, 1);
            int nargs = 0;
            if (d.con_nombre) { lua_pushstring(th, nombre8.get_data()); nargs = 1; }
            gl_paso_stats().cambios_disparados++;
            const uint64_t _t_cambio = gl_aut_on() ? Time::get_singleton()->get_ticks_usec() : 0;
            gl_resume_handler(th, nargs);
            // Con GL_AUTOPSIA=1: que señal de cambio salta y cuantas veces.
            if (_t_cambio)
                gl_aut_cb_anota(String("[cambio] ") + gl_roblox_classname(n) + "." + nombre,
                                Time::get_singleton()->get_ticks_usec() - _t_cambio + 1);
        } else lua_pop(d.mL, 1);
        lua_pop(d.mL, 1);
    }
}
inline void gl_fire_attribute_changed(Node* n, const String& attr) {
    gl_fire_watchers(n, GL_VIGILA_ATTR, GL_VIGILA_ATTR_ANY, attr);
}
inline void gl_fire_prop_changed(Node* n, const String& prop) {
    gl_fire_watchers(n, GL_VIGILA_PROP, GL_VIGILA_CHANGED, prop);
}

// Conexion de un vigilante: Disconnect lo quita del registro por su id. Antes se
// pasaba ref 0, no desconectaba nada y el vigilante seguia saltando hasta que
// moria su VM (un :Once() de estas señales saltaba siempre).
inline void _gl_push_watch_connection(lua_State* L, int id) {
    lua_newtable(L);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "_gl_conn");
    lua_pushinteger(L, id);
    lua_pushcclosure(L, [](lua_State* dL) -> int {
        gl_unwatch((int)lua_tointeger(dL, lua_upvalueindex(1)));
        if (lua_istable(dL, 1)) { lua_pushboolean(dL, 0); lua_setfield(dL, 1, "_gl_conn"); }
        return 0;
    }, "Disconnect", 1);
    lua_setfield(L, -2, "Disconnect");
    lua_getfield(L, -1, "Disconnect");   // alias legacy :disconnect()
    lua_setfield(L, -2, "disconnect");
    gl_conn_connected_mt(L);
}

// Señal de instancia repartida desde el script dueño (ScriptNodeBase::relays):
// ChildAdded, ChildRemoved, DescendantAdded, DescendantRemoving, Destroying y
// AncestryChanged. `kind` es un GLRelayKind (gl_runtime.h).
inline void _gl_push_relay_signal(lua_State* L, Node* n, int kind) {
    lua_newtable(L);
    lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)n->get_instance_id());
    lua_pushinteger(L, kind);
    lua_pushcclosure(L, [](lua_State* pL) -> int {
        const uint64_t tid = (uint64_t)(uintptr_t)lua_touserdata(pL, lua_upvalueindex(1));
        const int k = (int)lua_tointeger(pL, lua_upvalueindex(2));
        if (!lua_isfunction(pL, 2)) { lua_pushnil(pL); return 1; }
        Node* sn = gl_nodo_script_de(pL);
        if (!sn) { lua_pushnil(pL); return 1; }
        lua_pushvalue(pL, 2); const int ref = gl_ref(pL, -1); lua_pop(pL, 1);
        const int id = (int)sn->call("_gl_relay_connect", (int64_t)tid, k, ref);
        lua_newtable(pL);
        lua_pushlightuserdata(pL, (void*)(uintptr_t)(uint64_t)sn->get_instance_id());
        lua_pushinteger(pL, id);
        lua_pushcclosure(pL, [](lua_State* dL) -> int {
            const uint64_t sid = (uint64_t)(uintptr_t)lua_touserdata(dL, lua_upvalueindex(1));
            const int i = (int)lua_tointeger(dL, lua_upvalueindex(2));
            Node* snn = gl_cast<Node>(ObjectDB::get_instance(sid));
            if (snn) snn->call("_gl_relay_disconnect", i);
            return 0;
        }, "Disconnect", 2);
        lua_setfield(pL, -2, "Disconnect");
        gl_conn_add_connected(pL);
        return 1;
    }, "Connect", 2);
    lua_setfield(L, -2, "Connect");
    gl_signal_lowercase_aliases(L);
}

// Señal de un vigilante (Changed, GetPropertyChangedSignal...): una tabla con
// Connect; Once y Wait los pone gl_signal_lowercase_aliases.
inline void _gl_push_watch_signal(lua_State* L, Node* n, uint8_t kind, const char* nombre) {
    lua_newtable(L);
    lua_pushlightuserdata(L, (void*)(uintptr_t)(uint64_t)n->get_instance_id());
    lua_pushinteger(L, kind);
    lua_pushstring(L, nombre);
    lua_pushcclosure(L, [](lua_State* cL) -> int {
        const uint64_t nid = (uint64_t)(uintptr_t)lua_touserdata(cL, lua_upvalueindex(1));
        const uint8_t k = (uint8_t)lua_tointeger(cL, lua_upvalueindex(2));
        const char* nom = lua_tostring(cL, lua_upvalueindex(3));
        Node* node = gl_cast<Node>(ObjectDB::get_instance(nid));
        int fn = -1;
        for (int i = 1; i <= lua_gettop(cL); i++) if (lua_isfunction(cL, i)) { fn = i; break; }
        if (fn == -1 || !node) return 0;
        _gl_push_watch_connection(cL, gl_watch(node, k, String::utf8(nom), cL, fn));
        return 1;
    }, "Connect", 3);
    lua_setfield(L, -2, "Connect");
    gl_signal_lowercase_aliases(L);
}

// gl_replica.h (incluido mas abajo): SetAttribute en el servidor se replica.
inline void gl_rep_on_attr(Node* n, const String& key);
// gl_atributos.h (incluido mas abajo): todos los tipos de atributo de Roblox.
inline bool gl_attr_desde_lua(lua_State* L, int idx, Variant& out);
inline void gl_push_attr_value(lua_State* L, const Variant& val);

inline int method_setattribute(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    if (w && gow_node(w)) {
        const char* key = gl_str(L, 2);
        // El valor de antes, para no avisar si no cambia (como Roblox).
        const bool habia = gow_node(w)->has_meta(key);
        const Variant antes = habia ? gow_node(w)->get_meta(key) : Variant();
        const int t = lua_type(L, 3);
        if (t == LUA_TNIL || t == LUA_TNONE) {
            if (habia) gow_node(w)->remove_meta(key);   // como Roblox: nil borra
        } else {
            // Se mira el TIPO real, no lua_isnumber: "0010" es un string y se
            // queda string (una mascara de bits guardada como numero moria al
            // hacerle :sub). Un tipo que Roblox no admite como atributo (una
            // tabla cualquiera, una Instance) es un error, como alli: antes se
            // guardaba su texto y GetAttribute devolvia un string.
            Variant v;
            if (!gl_attr_desde_lua(L, 3, v)) {
                luaL_error(L, "Unable to set attribute \"%s\": %s is not a supported attribute type",
                           key, luaL_typename(L, 3));
                return 0;
            }
            gow_node(w)->set_meta(key, v);
        }
        // Solo los vigilantes de atributos (GetAttributeChangedSignal y
        // AttributeChanged): Changed es de propiedades y no salta por esto.
        const bool hay = gow_node(w)->has_meta(key);
        if (hay != habia || (hay && gow_node(w)->get_meta(key) != antes)) {
            gl_fire_attribute_changed(gow_node(w), String(key));
            if (gl_net_role() == 1) gl_rep_on_attr(gow_node(w), String(key));
        }
    }
    return 0;
}

inline int method_getattribute(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    const char* key = gl_str(L, 2);
    if (w && gow_node(w) && gow_node(w)->has_meta(key)) {
        gl_push_attr_value(L, gow_node(w)->get_meta(key));
        return 1;
    }
    lua_pushnil(L); return 1;
}

// GetAttributes() → tabla {nombre = valor} con TODOS los atributos, como en
// Roblox. No existia: un script que quisiera recorrerlos moria con
// "GetAttributes is not a valid member".
inline int method_getattributes(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    lua_newtable(L);
    Node* nd = w ? gow_node(w) : nullptr;
    if (!nd) return 1;
    const TypedArray<StringName> metas = nd->get_meta_list();
    for (int i = 0; i < metas.size(); i++) {
        const String k = String(metas[i]);
        if (!gl_meta_es_atributo(k)) continue;
        gl_push_attr_value(L, nd->get_meta(StringName(k)));
        lua_setfield(L, -2, k.utf8().get_data());
    }
    return 1;
}

// BFS helper: find a child node by name in the subtree, down to `prof_max`
// levels. Los servicios cuelgan del DataModel: GetService busca 3 niveles por
// debajo de game y 4 desde la raiz de la escena, no el juego entero (~40.000
// nodos, 33 ms por busqueda fallida).
inline Node* _find_node_by_name(Node* root, const char* name, int prof_max = 1 << 30) {
    if (!root) return nullptr;
    const StringName sn(name);
    std::vector<std::pair<Node*, int>> queue;
    queue.emplace_back(root, 0);
    while (!queue.empty()) {
        const std::pair<Node*, int> cur = queue.back(); queue.pop_back();
        const int n = cur.first->get_child_count();
        for (int i = 0; i < n; i++) {
            Node* ch = cur.first->get_child(i);
            if (!ch) continue;
            if (gl_rbx_llamado(ch, sn)) return ch;
            if (cur.second + 1 < prof_max) queue.emplace_back(ch, cur.second + 1);
        }
    }
    return nullptr;
}

// ── GetService sin recorrer el arbol ─────────────────────────────────────
// Medido al arrancar Build a Mountain: 382 ms de un frame de 3,8 s eran
// GetService. Cada llamada que no encontraba el servicio entre los hijos
// directos de `game` (por nombre de Godot) recorria TODO el juego, unos 20.000
// nodos, a 1 µs por nodo. Los servicios no se mueven: lo encontrado se apunta
// aqui (por nombre, sin StringName ni String, que piden memoria y un cerrojo)
// y solo se vuelve a buscar si el nodo ya no existe o lo sacaron del arbol.
// Lo que NO esta (un servicio que el juego pide y aqui no existe) se busca a
// fondo como mucho una vez cada 10 s (ver method_getservice).
struct GLServEntrada {
    std::string nombre;
    uint64_t id = 0;
    uint64_t ms_ausente = UINT64_MAX;
};
struct GLServCache {
    uint64_t raiz = 0;
    std::vector<GLServEntrada> e;
};
inline GLServCache& gl_getservice_cache() { static GLServCache c; return c; }

// La entrada de ese nombre en el juego `raiz`; con `crear`, la abre si falta.
// OJO: crear una puede mover las demas (vector): no guardar el puntero.
inline GLServEntrada* gl_serv_entrada(Node* raiz, const char* nombre, bool crear) {
    GLServCache& c = gl_getservice_cache();
    const uint64_t id = (uint64_t)raiz->get_instance_id();
    if (c.raiz != id) {
        if (!crear) return nullptr;
        c.raiz = id;
        c.e.clear();
    }
    for (GLServEntrada& x : c.e)
        if (strcmp(x.nombre.c_str(), nombre) == 0) return &x;
    if (!crear) return nullptr;
    c.e.emplace_back();
    c.e.back().nombre = nombre;
    return &c.e.back();
}

inline void gl_serv_cache_apuntar(Node* raiz, const char* nombre, Node* n) {
    GLServEntrada* x = gl_serv_entrada(raiz, nombre, true);
    x->id = (uint64_t)n->get_instance_id();
    x->ms_ausente = UINT64_MAX;
}

// ── Virtual service containers ────────────────────────────────────────
// Returns (or creates if missing) a Node child of 'game' with a given name.
// Used for ReplicatedStorage, ServerStorage, StarterPack, etc.
inline Node* _get_or_create_container(Node* game, const char* name) {
    if (!game) return nullptr;
    Node* existing = game->get_node_or_null(NodePath(String(name)));
    if (existing) return existing;
    Node* container = memnew(Node);
    container->set_name(StringName(name));
    game->add_child(container);
    return container;
}

static const char* _VIRTUAL_CONTAINERS[] = {
    "ReplicatedStorage", "ServerStorage", "StarterPack",
    "StarterGui", "StarterPlayerScripts", "StarterCharacterScripts",
    "Teams", "ServerScriptService", nullptr
};

inline int method_getservice(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    const char* svc_name = gl_str(L, 2);
    if (!w || !gow_node(w)) { lua_pushnil(L); return 1; }

    // ── Client/server separation ──────────────────────────────────────────
    //// ── Separacion cliente/servidor ──────────────────────────────────────
    // ServerStorage and ServerScriptService are invisible to LocalScript,
    // just like in Roblox: the client can never access the server's storage.
    //// ServerStorage y ServerScriptService son invisibles para LocalScript,
    //// igual que en Roblox: el cliente nunca puede acceder al almacén del servidor.
    lua_getglobal(L, "__IS_CLIENT");
    const bool is_client = lua_isboolean(L, -1) && (lua_toboolean(L, -1) != 0);
    lua_pop(L, 1);
    lua_getglobal(L, "__IS_SERVER");
    const bool is_server = lua_isboolean(L, -1) && (lua_toboolean(L, -1) != 0);
    lua_pop(L, 1);
    if (is_client && (strcmp(svc_name, "ServerStorage") == 0 ||
                      strcmp(svc_name, "ServerScriptService") == 0)) {
        UtilityFunctions::push_error("[GodotLuau] LocalScript no puede acceder a '",
            svc_name, "' — este servicio es exclusivo del servidor.");
        lua_pushnil(L);
        return 1;
    }
    // Los servicios de ENTRADA son exclusivos del cliente: un ServerScript no
    // puede leer teclado/ratón/pantalla (igual que en Roblox).
    if (is_server && (strcmp(svc_name, "UserInputService")   == 0 ||
                      strcmp(svc_name, "ContextActionService") == 0)) {
        UtilityFunctions::push_error("[GodotLuau] ServerScript no puede acceder a '",
            svc_name, "' — la entrada de hardware solo existe en el cliente (LocalScript).");
        lua_pushnil(L);
        return 1;
    }

    // 1. Lua pure services stored as _G["__SVC_ServiceName"]
    std::string global_key = std::string("__SVC_") + svc_name;
    lua_getglobal(L, global_key.c_str());
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);

    Node* game = gow_node(w);
    // Un fallo se recuerda 10 s: cada busqueda a fondo recorre el juego y la
    // escena enteros (~40.000 nodos, 33 ms medidos en Build a Mountain) y un
    // juego que pide cada frame un servicio que aqui no hay (AdService) la
    // repetia cada frame.
    const uint64_t ahora_ms = Time::get_singleton()->get_ticks_msec();
    bool ausente_ya = false;
    if (GLServEntrada* ya = gl_serv_entrada(game, svc_name, false)) {
        if (ya->id) {
            Node* n = gl_cast<Node>(ObjectDB::get_instance(ObjectID(ya->id)));
            if (n && n->get_parent()) { wrap_node(L, n); return 1; }
            ya->id = 0;
        }
        ausente_ya = ya->ms_ausente != UINT64_MAX && ahora_ms - ya->ms_ausente < 10000;
    }

    // 2. Direct child of game node by exact name
    const StringName sn(svc_name);
    const int hijos = game->get_child_count();
    for (int i = 0; i < hijos; i++) {
        Node* ch = game->get_child(i);
        if (ch && ch->get_name() == sn) { gl_serv_cache_apuntar(game, svc_name, ch); wrap_node(L, ch); return 1; }
    }

    // Los que fabricamos nosotros (pasos 5 y 6) van siempre colgados de game:
    // si no estan entre sus hijos, no estan en ninguna parte y se crean ya.
    static const char* kAutoServices[] = {
        "Players", "UserInputService", "ContextActionService", "RunService",
        "TweenService", "CollectionService", "SoundService", "TextChatService",
        "Lighting", "MaterialService", "PlayerScripts", "TextService", NULL
    };
    bool lo_creamos = strcmp(svc_name, "NetworkService") == 0;
    for (int i = 0; !lo_creamos && _VIRTUAL_CONTAINERS[i]; i++) lo_creamos = strcmp(_VIRTUAL_CONTAINERS[i], svc_name) == 0;
    for (int i = 0; !lo_creamos && kAutoServices[i]; i++) lo_creamos = strcmp(kAutoServices[i], svc_name) == 0;

    if (!ausente_ya && !lo_creamos) {
        // 3. Anywhere in subtree
        Node* svc = _find_node_by_name(game, svc_name, 3);
        if (svc) { gl_serv_cache_apuntar(game, svc_name, svc); wrap_node(L, svc); return 1; }

        // 4. Search whole scene tree from root
        if (game->is_inside_tree()) {
            svc = _find_node_by_name((Node*)game->get_tree()->get_root(), svc_name, 4);
            if (svc) { gl_serv_cache_apuntar(game, svc_name, svc); wrap_node(L, svc); return 1; }
        }
        GLServEntrada* x = gl_serv_entrada(game, svc_name, true);
        if (x->ms_ausente == UINT64_MAX && OS::get_singleton()->get_environment("GL_PERFIL") == "1")
            UtilityFunctions::print("[PERFIL] GetService: no hay servicio '", svc_name, "'");
        x->ms_ausente = ahora_ms;
    }

    // 5. Auto-create well-known virtual containers
    for (int i = 0; _VIRTUAL_CONTAINERS[i]; i++) {
        if (strcmp(_VIRTUAL_CONTAINERS[i], svc_name) == 0) {
            Node* c = _get_or_create_container(game, svc_name);
            gl_serv_cache_apuntar(game, svc_name, c);
            wrap_node(L, c); return 1;
        }
    }

    // 6. Servicios singleton C++ que se auto-crean bajo game la primera vez
    if (strcmp(svc_name, "NetworkService") == 0) {
        NetworkService* ns = memnew(NetworkService);
        ns->set_name("NetworkService");
        game->add_child(ns);
        gl_serv_cache_apuntar(game, svc_name, ns);
        wrap_node(L, ns); return 1;
    }
    // Los servicios SIEMPRE existen, igual que en Roblox: GetService los crea al
    // pedirlos si no estan. Sin esto, un place importado (que no serializa los
    // servicios de entrada ni de tiempo) devolvia nil y se llevaba por delante a
    // todos los scripts de interfaz: UserInputService.InputBegan, RunService,
    // ContextActionService... eran la mitad de los errores del place de prueba.
    for (int i = 0; kAutoServices[i]; i++) {
        if (strcmp(svc_name, kAutoServices[i]) != 0) continue;
        if (!ClassDB::class_exists(sn)) break;
        Node* ps = gl_cast<Node>(ClassDB::instantiate(sn));
        if (!ps) break;
        ps->set_name(svc_name);
        game->add_child(ps);
        gl_serv_cache_apuntar(game, svc_name, ps);
        wrap_node(L, ps); return 1;
    }

    lua_pushnil(L); return 1;
}

// ════════════════════════════════════════════════════════════════════
//  CoreGui — estado REAL de StarterGui:SetCoreGuiEnabled (1.17)
////
//  Antes SetCoreGuiEnabled solo guardaba un meta y no ocultaba NADA: el
//  juego apagaba el chat de Roblox y el chat del motor seguia en pantalla.
//  El estado vive como meta del nodo StarterGui (fuente de verdad) y ademas
//  se EMPUJA a la UI viva; las UIs, al nacer, CONSULTAN el meta, porque el
//  caso normal es que un LocalScript llame a esto en su primera linea,
//  ANTES de que la UI exista.
// ════════════════════════════════════════════════════════════════════

// Nombres por VALOR del enum, verificados en la doc oficial:
// https://create.roblox.com/docs/reference/engine/enums/CoreGuiType
// PlayerList=0, Health=1, Backpack=2, Chat=3, All=4, EmotesMenu=5,
// SelfView=6, Captures=7, AvatarSwitcher=8, ExperienceShop=9.
static const char* kGLCoreGuiNames[] = {
    "PlayerList", "Health", "Backpack", "Chat", "All",
    "EmotesMenu", "SelfView", "Captures", "AvatarSwitcher", "ExperienceShop"
};
static const int kGLCoreGuiCount = (int)(sizeof(kGLCoreGuiNames) / sizeof(kGLCoreGuiNames[0]));

// Normaliza el argumento `which` al NOMBRE del CoreGuiType. Los scripts pasan
// Enum.CoreGuiType.X (aqui un numero, ver luau_stdlib.h) o directamente el
// string; guardar por nombre evita que "3" y "Chat" sean dos estados distintos.
inline String gl_coregui_which(lua_State* pL, int idx) {
    // Enum.CoreGuiType.X ya NO es un numero: es un EnumItem (tabla con
    // .Name/.Value, ver luau_stdlib.h). Sin esta rama caia al "All" final y
    // SetCoreGuiEnabled(Enum.CoreGuiType.Chat, false) apagaba TODAS las UIs.
    if (lua_istable(pL, idx)) {
        lua_getfield(pL, idx, "Name");
        if (lua_isstring(pL, -1)) {
            String s = String(lua_tostring(pL, -1));
            lua_pop(pL, 1);
            return s;
        }
        lua_pop(pL, 1);
        lua_getfield(pL, idx, "Value");
        if (lua_isnumber(pL, -1)) {
            const int v = (int)lua_tonumber(pL, -1);
            lua_pop(pL, 1);
            if (v >= 0 && v < kGLCoreGuiCount) return String(kGLCoreGuiNames[v]);
            return String::num_int64(v);
        }
        lua_pop(pL, 1);
    }
    if (lua_isnumber(pL, idx)) {
        const int v = (int)lua_tonumber(pL, idx);
        if (v >= 0 && v < kGLCoreGuiCount) return String(kGLCoreGuiNames[v]);
        return String::num_int64(v);   // valor desconocido: se guarda tal cual
    }
    if (lua_isstring(pL, idx)) return String(lua_tostring(pL, idx));
    return String("All");
}

// Estado guardado de un CoreGuiType. Sin meta = encendido (como Roblox).
inline bool gl_coregui_enabled_meta(Node* sg, const String& name) {
    if (!sg) return true;
    const String mk = String("_gl_coregui_") + name;
    return sg->has_meta(mk) ? (bool)sg->get_meta(mk) : true;
}

inline Node* gl_find_class_in(Node* n, const char* cls) {
    if (!n) return nullptr;
    if (n->is_class(cls)) return n;
    for (int i = 0; i < n->get_child_count(); i++)
        if (Node* r = gl_find_class_in(n->get_child(i), cls)) return r;
    return nullptr;
}

// Empuja el estado de UN CoreGuiType a la UI del motor que le corresponde.
// Va por has_method/call para no acoplar aqui los headers de UI.
//   Chat -> la ventana de chat (RobloxChat) Y su boton de la topbar
//           (la barra del menu de Luau): en Roblox, apagar el CoreGui del chat quita
//           tambien el icono de chat de la topbar.
//   PlayerList / Health / Backpack / EmotesMenu / SelfView / Captures:
//           el motor no dibuja (todavia) esas UIs core, asi que no hay nada
//           que ocultar; el estado queda guardado para GetCoreGuiEnabled.
inline void gl_coregui_push(Node* sg, const String& name, bool on) {
    if (!sg || !sg->is_inside_tree()) return;
    Node* root = (Node*)sg->get_tree()->get_root();
    if (name == "Chat") {
        if (Node* chat = gl_find_class_in(root, "RobloxChat"))
            if (chat->has_method("gl_set_chat_enabled")) chat->call("gl_set_chat_enabled", on);
        // (El boton de chat de la barra superior lo dibuja ahora el menu de
        //  Luau, que lee este mismo estado; ya no hay menu de C++ al que avisar.)
    }
}

// ════════════════════════════════════════════════════════════════════
//  PERFILADO DEL PUENTE (solo con GL_PERFIL=1 en el entorno)
//
//  godot_object_index es una cadena de cientos de strcmp y TODA lectura de
//  propiedad la recorre desde el principio. Para saber que claves añadir al
//  camino rapido hay que CONTARLAS, no adivinarlas. Esto cuenta cuantas veces
//  entra cada clave y cuanto tiempo total se va, y lo vuelca cada 10 s.
//
//  Apagado no cuesta nada: el flag se lee UNA vez y el `if` se predice solo.
// ════════════════════════════════════════════════════════════════════
// GetPivot de un Model. Igual que la version de mas abajo pero tomando el
// objeto del argumento 1 (que es como se llama siempre: model:GetPivot()), para
// poder compartirla desde el camino rapido sin duplicar el cuerpo.
inline int method_getpivot_model(lua_State* L) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    Node* m = w ? gow_node(w) : nullptr;
    if (!m) { lua_pushnil(L); return 1; }
    _gl_push_cframe_full(L, (Transform3D)m->call("GetPivot"));
    return 1;
}

struct GLPerfilPuente {
    HashMap<String, uint64_t> veces_lee, veces_escribe;
    HashMap<String, uint64_t> us_por_clave;   // tiempo de ESCRITURA por propiedad
    uint64_t us_lee = 0, us_escribe = 0, n_lee = 0, n_escribe = 0;
    uint64_t us_impl = 0, us_rep = 0;   // escritura: trabajo real vs hook de replicacion
    uint64_t ultimo_volcado = 0;
};
inline GLPerfilPuente& gl_perfil() { static GLPerfilPuente p; return p; }
inline bool gl_perfil_on() {
    static const bool on = OS::get_singleton()
        && OS::get_singleton()->get_environment("GL_PERFIL") == "1";
    return on;
}
inline void gl_perfil_volcar_si_toca() {
    GLPerfilPuente& p = gl_perfil();
    const uint64_t ahora = Time::get_singleton()->get_ticks_msec();
    if (p.ultimo_volcado == 0) { p.ultimo_volcado = ahora; return; }
    if (ahora - p.ultimo_volcado < 10000) return;
    p.ultimo_volcado = ahora;
    UtilityFunctions::print("[PERFIL] ===== puente Luau<->C++ (10 s) =====");
    UtilityFunctions::print("[PERFIL] LECTURAS  ", (int64_t)p.n_lee,
                            "  total ", (double)p.us_lee / 1000.0, " ms");
    UtilityFunctions::print("[PERFIL] ESCRITURAS", (int64_t)p.n_escribe,
                            "  total ", (double)p.us_escribe / 1000.0, " ms");
    UtilityFunctions::print("[PERFIL]   de eso: propiedad ", (double)p.us_impl / 1000.0,
                            " ms  |  hook de RED ", (double)p.us_rep / 1000.0, " ms");
    // top 25 de cada lado
    for (int lado = 0; lado < 2; lado++) {
        HashMap<String, uint64_t>& m = lado == 0 ? p.veces_lee : p.veces_escribe;
        UtilityFunctions::print("[PERFIL] --- ", lado == 0 ? "leidas" : "escritas", " mas frecuentes ---");
        // seleccion directa: son pocas claves distintas, no hace falta ordenar
        HashMap<String, uint64_t> resto = m;
        for (int k = 0; k < 25 && resto.size() > 0; k++) {
            String mejor;
            uint64_t maxv = 0;
            for (const KeyValue<String, uint64_t>& kv : resto)
                if (kv.value > maxv) { maxv = kv.value; mejor = kv.key; }
            if (maxv == 0) break;
            UtilityFunctions::print("[PERFIL]   ", (int64_t)maxv, "  ", mejor);
            resto.erase(mejor);
        }
    }
    UtilityFunctions::print("[PERFIL] --- escrituras que MAS TIEMPO se llevan ---");
    {
        HashMap<String, uint64_t> resto = p.us_por_clave;
        for (int k = 0; k < 15 && resto.size() > 0; k++) {
            String mejor; uint64_t maxv = 0;
            for (const KeyValue<String, uint64_t>& kv : resto)
                if (kv.value > maxv) { maxv = kv.value; mejor = kv.key; }
            if (maxv == 0) break;
            const uint64_t veces = p.veces_escribe.has(mejor) ? p.veces_escribe[mejor] : 1;
            UtilityFunctions::print("[PERFIL]   ", (double)maxv / 1000.0, " ms  x", (int64_t)veces,
                                    "  = ", (double)maxv / (double)(veces ? veces : 1), " us c/u   ", mejor);
            resto.erase(mejor);
        }
    }
    p.us_por_clave.clear();
    p.veces_lee.clear(); p.veces_escribe.clear();
    p.us_lee = p.us_escribe = p.n_lee = p.n_escribe = 0;
    p.us_impl = p.us_rep = 0;
}

// Lo que ve un rayo o una consulta: la pieza tocada (el miembro del conjunto
// soldado, no su raiz) o, si es un personaje, su HumanoidRootPart, como en
// Touched. Antes un rayo contra un personaje devolvia el personaje entero y
// `resultado.Instance.Parent:FindFirstChild("Humanoid")` no lo encontraba.
inline Node* gl_golpe_a_pieza(Node* collider, int shape_idx) {
    if (!collider) return nullptr;
    if (RobloxPart* rp = gl_cast<RobloxPart>(collider)) return rp->gl_pieza_de_forma(shape_idx);
    // El terreno choca con cuerpos internos colgados de el: cuenta el Terrain.
    if (Node* padre = collider->get_parent())
        if (gl_cast<RobloxTerrain>(padre)) return padre;
    return gl_touch_reported_node(collider);
}

// ── Consultas de zona (GetPartBoundsInBox / InRadius / GetPartsInPart) ───────
struct GLZona { Ref<Shape3D> forma; Transform3D xf; Node* excluir = nullptr; };
inline void gl_leer_filtro(lua_State* L, int idx, Vector<Node*>& filtro);   // mas abajo

// Caja o esfera de consulta, del mismo cache que las formas de las piezas: una
// hitbox que pregunta cada frame con el mismo tamaño no crea nada, y se suelta
// al cerrar con todo lo demas.
inline Ref<Shape3D> gl_forma_consulta(bool esfera, const Vector3& tam) {
    const Vector3 t(MAX(Math::abs(tam.x), 0.01f), MAX(Math::abs(tam.y), 0.01f), MAX(Math::abs(tam.z), 0.01f));
    const GLClaveForma key = gl_shape_key(esfera ? 1 : 0, t);
    HashMap<uint64_t, Ref<Shape3D>>& sc = gl_shared_shapes();
    if (Ref<Shape3D>* hay = sc.getptr(key)) return *hay;
    Ref<Shape3D> forma;
    if (esfera) {
        Ref<SphereShape3D> s; s.instantiate();
        s->set_radius(MIN(t.x, t.z) * 0.5f);
        forma = s;
    } else {
        Ref<BoxShape3D> b; b.instantiate();
        b->set_size(t);
        forma = b;
    }
    sc[key] = forma;
    return forma;
}

// Resuelve la zona con los OverlapParams (en idx_params) y deja en la pila el
// array de piezas, como Roblox: FilterDescendantsInstances + FilterType,
// MaxParts (0 = sin limite), RespectCanCollide y CanQuery.
inline void gl_zona_consultar(lua_State* L, Node* ws, const GLZona& z, int idx_params) {
    Vector<Node*> filtro;
    bool incluir = false;
    bool respetar = false;
    int tope = 0;
    if (lua_istable(L, idx_params)) {
        lua_getfield(L, idx_params, "MaxParts");          tope = (int)gl_num_or(L, -1, 0);           lua_pop(L, 1);
        lua_getfield(L, idx_params, "FilterType");        incluir = ((int)gl_num_or(L, -1, 1) == 0); lua_pop(L, 1);
        lua_getfield(L, idx_params, "RespectCanCollide"); respetar = lua_toboolean(L, -1) != 0;     lua_pop(L, 1);
        lua_getfield(L, idx_params, "FilterDescendantsInstances");
        gl_leer_filtro(L, -1, filtro);
        lua_pop(L, 1);
    }
    lua_newtable(L);
    if (incluir && filtro.is_empty()) return;   // Include con la lista vacia: nada
    Node3D* ws3d = gl_cast<Node3D>(ws);
    if (!ws3d || !ws3d->is_inside_tree() || z.forma.is_null()) return;
    PhysicsDirectSpaceState3D* space = ws3d->get_world_3d()->get_direct_space_state();
    if (!space) return;
    Ref<PhysicsShapeQueryParameters3D> q;
    q.instantiate();
    q->set_shape(z.forma);
    q->set_transform(z.xf);
    q->set_collision_mask(0xFFFFFFFF);   // con la capa de consultas: CanCollide=false tambien cuenta
    if (CollisionObject3D* co = gl_cast<CollisionObject3D>(z.excluir)) {
        TypedArray<RID> ex;
        ex.push_back(co->get_rid());
        q->set_exclude(ex);
    }
    const int max_res = tope > 0 ? MAX(64, tope * 4) : 4096;
    TypedArray<Dictionary> res = space->intersect_shape(q, max_res);
    Vector<Node*> vistos;
    int n = 0;
    for (int i = 0; i < res.size(); i++) {
        Dictionary d = res[i];
        Node* c = gl_cast<Node>(d.get("collider", Variant()).operator Object*());
        Node* pieza = gl_golpe_a_pieza(c, (int)d.get("shape", -1));
        if (!pieza || pieza == z.excluir) continue;
        if (RobloxPart* rp = gl_cast<RobloxPart>(pieza)) {
            if (!rp->get_can_query()) continue;
            if (respetar && !rp->get_can_collide()) continue;
        } else if (pieza == c) {
            continue;   // ni pieza ni personaje (cuerpos internos): no es una BasePart
        }
        if (!filtro.is_empty() && gl_en_filtro(pieza, filtro) != incluir) continue;
        if (vistos.find(pieza) >= 0) continue;
        vistos.push_back(pieza);
        wrap_node(L, pieza);
        lua_rawseti(L, -2, ++n);
        if (tope > 0 && n >= tope) break;
    }
}

// ── Rayos de workspace (Raycast y FindPartOnRay*) ────────────────────────────
struct GLRayo { Node* hit = nullptr; Vector3 pos, normal; float dist = 0.0f; bool ok = false; };

// El rayo va de `from` a `from + dir`: el largo es el de la direccion.
inline GLRayo gl_ws_rayo(Node* ws, const Vector3& from, const Vector3& dir,
                         const Vector<Node*>* filtro, bool incluir, bool respetar_colision) {
    GLRayo r;
    Node3D* ws3d = gl_cast<Node3D>(ws);
    if (!ws3d || !ws3d->is_inside_tree() || dir.length_squared() < 1e-12f) return r;
    PhysicsDirectSpaceState3D* space = ws3d->get_world_3d()->get_direct_space_state();
    if (!space) return r;
    Ref<PhysicsRayQueryParameters3D> params;
    params.instantiate();
    params->set_from(from);
    params->set_to(from + dir);
    Dictionary res = gl_raycast_roblox(space, params, 16, filtro, incluir, respetar_colision);
    if (res.is_empty()) return r;
    Variant cv = res.get("collider", Variant());
    if (cv.get_type() == Variant::OBJECT)
        r.hit = gl_golpe_a_pieza(gl_cast<Node>(cv.operator Object*()), (int)res.get("shape", -1));
    r.pos = res.get("position", Vector3());
    r.normal = res.get("normal", Vector3());
    r.ok = true;
    return r;
}

// Un array de instancias de Luau (FilterDescendantsInstances, ignoreList...).
inline void gl_leer_filtro(lua_State* L, int idx, Vector<Node*>& filtro) {
    if (!lua_istable(L, idx)) return;
    if (idx < 0) idx = lua_gettop(L) + 1 + idx;
    const int cuantos = (int)lua_objlen(L, idx);
    for (int i = 1; i <= cuantos; i++) {
        lua_rawgeti(L, idx, i);
        if (_gl_has_metatable(L, -1, "GodotObject")) {
            GodotObjectWrapper* fw = (GodotObjectWrapper*)lua_touserdata(L, -1);
            if (Node* fn = fw ? gow_node(fw) : nullptr) filtro.push_back(fn);
        }
        lua_pop(L, 1);
    }
}

// Material de lo golpeado como EnumItem (Enum.Material.X), como Roblox. Lo que
// no es una pieza (personajes, terreno) cuenta como Plastic.
inline void gl_push_material_de(lua_State* L, Node* hit) {
    RobloxPart* p = gl_cast<RobloxPart>(hit);
    uis_push_enum(L, "Material", p ? p->get_roblox_material() : 0);
}

// ── Spherecast / Blockcast / Shapecast ────────────────────────────────────────
// Una forma que se mueve por `dir` y se para en lo primero que toca. Como en
// Roblox, lo que ya esta tocando la forma al salir no cuenta (se aparta y se
// vuelve a lanzar), y se respetan el filtro, CanQuery y RespectCanCollide.
inline GLRayo gl_ws_barrido(Node* ws, const Ref<Shape3D>& forma, const Transform3D& xf, const Vector3& dir,
                            const Vector<Node*>& filtro, bool incluir, bool respetar) {
    GLRayo r;
    Node3D* ws3d = gl_cast<Node3D>(ws);
    if (!ws3d || !ws3d->is_inside_tree() || forma.is_null() || dir.length_squared() < 1e-12f) return r;
    PhysicsDirectSpaceState3D* space = ws3d->get_world_3d()->get_direct_space_state();
    if (!space) return r;
    Ref<PhysicsShapeQueryParameters3D> q;
    q.instantiate();
    q->set_shape(forma);
    q->set_collision_mask(0xFFFFFFFF);
    TypedArray<RID> excluir;
    for (int vuelta = 0; vuelta < 32; vuelta++) {
        q->set_transform(xf);
        q->set_motion(dir);
        q->set_exclude(excluir);
        const PackedFloat32Array f = space->cast_motion(q);
        if (f.size() < 2 || f[1] >= 1.0f) return r;   // no toca nada en todo el camino
        Transform3D en = xf;
        en.origin += dir * f[1];
        q->set_transform(en);
        q->set_motion(Vector3());
        const Dictionary info = space->get_rest_info(q);
        if (info.is_empty()) return r;
        const RID rid = info.get("rid", RID());
        Node* c = gl_cast<Node>(ObjectDB::get_instance(ObjectID((uint64_t)(int64_t)info.get("collider_id", 0))));
        Node* pieza = gl_golpe_a_pieza(c, (int)info.get("shape", -1));
        bool saltar = (f[1] <= 0.0f) || !pieza;    // ya la tocaba al salir
        if (!saltar) {
            if (RobloxPart* rp = gl_cast<RobloxPart>(pieza)) {
                if (!rp->get_can_query() || (respetar && !rp->get_can_collide())) saltar = true;
            }
            if (!filtro.is_empty() && gl_en_filtro(pieza, filtro) != incluir) saltar = true;
        }
        if (!saltar) {
            r.ok = true;
            r.hit = pieza;
            r.pos = info.get("point", Vector3());
            r.normal = info.get("normal", Vector3());
            r.dist = dir.length() * f[1];
            return r;
        }
        if (!rid.is_valid()) return r;
        excluir.push_back(rid);
    }
    return r;
}

// Los RaycastParams de un barrido (filtro, tipo, RespectCanCollide).
inline void gl_leer_raycastparams(lua_State* L, int idx, Vector<Node*>& filtro, bool& incluir, bool& respetar) {
    incluir = false;
    respetar = false;
    if (!lua_istable(L, idx)) return;
    lua_getfield(L, idx, "FilterType");        incluir = ((int)gl_num_or(L, -1, 1) == 0); lua_pop(L, 1);
    lua_getfield(L, idx, "RespectCanCollide"); respetar = lua_toboolean(L, -1) != 0;     lua_pop(L, 1);
    lua_getfield(L, idx, "FilterDescendantsInstances");
    gl_leer_filtro(L, -1, filtro);
    lua_pop(L, 1);
}

// RaycastResult de Roblox en la pila (tabla con Instance/Position/Normal/
// Distance/Material), o nil.
inline void gl_push_resultado_rayo(lua_State* L, const GLRayo& r, float distancia) {
    if (!r.ok) { lua_pushnil(L); return; }
    lua_newtable(L);
    if (r.hit) wrap_node(L, r.hit); else lua_pushnil(L);
    lua_setfield(L, -2, "Instance");
    push_vector3(L, r.pos.x, r.pos.y, r.pos.z);          lua_setfield(L, -2, "Position");
    push_vector3(L, r.normal.x, r.normal.y, r.normal.z); lua_setfield(L, -2, "Normal");
    lua_pushnumber(L, distancia);                         lua_setfield(L, -2, "Distance");
    gl_push_material_de(L, r.hit);                        lua_setfield(L, -2, "Material");
}

#include "gl_puente_camara.h"   // Camera: CameraType, CFrame, rayos de pantalla...
#include "gl_cast.h"
#include "gl_puente_animacion.h" // AnimationTrack, LoadAnimation, AnimationPlayed (F7)
#include "gl_puente_sonido.h"   // Sound, SoundGroup, SoundService:PlayLocalSound
#include "gl_puente_efectos.h"  // particulas, Beam/Trail, Highlight, luces, Decal
#include "gl_atributos.h"       // atributos con todos los tipos de Roblox
#include "gl_puente_chat.h"     // TextChatService moderno: canales, comandos, OnIncomingMessage
#include "gl_navegacion.h"      // PathfindingService con NavigationServer3D
#include "gl_puente_editables.h" // EditableImage/EditableMesh, Content y AssetService (F9)
#include "gl_puente_texturas.h"  // streaming de texturas para ContentProvider:PreloadAsync

inline int godot_object_index_real(lua_State* L);

inline int godot_object_index(lua_State* L) {
    if (gl_aut_on()) { GLAutTiempo _a(AUT_LEE); return godot_object_index_real(L); }
    if (!gl_perfil_on()) return godot_object_index_real(L);
    const char* k = lua_tostring(L, 2);
    const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
    const int r = godot_object_index_real(L);
    GLPerfilPuente& p = gl_perfil();
    p.us_lee += Time::get_singleton()->get_ticks_usec() - t0;
    p.n_lee++;
    if (k) p.veces_lee[String(k)]++;
    gl_perfil_volcar_si_toca();
    return r;
}

// ── Instancia YA DESTRUIDA: en Roblox NO deja de responder ────────────────
// Destruir una instancia en Roblox no invalida el objeto: sigue teniendo sus
// metodos (GetAttribute devuelve nil, GetChildren {}, IsDescendantOf false…) y
// solo su Parent pasa a nil. Aqui, en cambio, se devolvia nil para TODA clave,
// asi que un patron tan comun como
//
//     task.delay(4, function()
//         if character and not character:GetAttribute("IsStunned") then …
//
// explotaba con "attempt to call missing method 'GetAttribute' of userdata" y se
// llevaba por delante lo que quedara del script. Visto en el log del place, en
// ServerScriptService.Server.CallModules.SistemaEscachar:170. Por eso aqui se
// devuelven metodos inertes que retornan lo MISMO que retornaria Roblox.
//
// Lo que no se puede imitar: IsA y Name necesitan la clase y el nombre reales,
// y el nodo ya no existe. Se devuelve false y "" (mejor que nil: no rompe una
// concatenacion ni una comparacion).
inline int gl_muerto_nil(lua_State* L)   { lua_pushnil(L);        return 1; }
inline int gl_muerto_false(lua_State* L) { lua_pushboolean(L, 0); return 1; }
inline int gl_muerto_tabla(lua_State* L) { lua_newtable(L);       return 1; }
inline int gl_muerto_nada(lua_State* L)  { (void)L;               return 0; }
inline int gl_muerto_str(lua_State* L)   { lua_pushstring(L, ""); return 1; }
inline int gl_muerto_senal(lua_State* L) { _gl_push_inert_signal(L); return 1; }

inline bool gl_index_instancia_muerta(lua_State* L, const char* key) {
    if (!key) return false;
    struct Entrada { const char* nombre; lua_CFunction fn; };
    static const Entrada tabla[] = {
        // Metodos que devuelven nil
        {"GetAttribute", gl_muerto_nil}, {"FindFirstChild", gl_muerto_nil},
        {"FindFirstChildOfClass", gl_muerto_nil}, {"FindFirstChildWhichIsA", gl_muerto_nil},
        {"FindFirstDescendant", gl_muerto_nil}, {"FindFirstAncestor", gl_muerto_nil},
        {"FindFirstAncestorOfClass", gl_muerto_nil}, {"FindFirstAncestorWhichIsA", gl_muerto_nil},
        {"WaitForChild", gl_muerto_nil}, {"Clone", gl_muerto_nil}, {"GetPivot", gl_muerto_nil},
        // …false
        {"IsA", gl_muerto_false}, {"IsAncestorOf", gl_muerto_false},
        {"IsDescendantOf", gl_muerto_false}, {"HasTag", gl_muerto_false},
        // …tabla vacia
        {"GetChildren", gl_muerto_tabla}, {"GetDescendants", gl_muerto_tabla},
        {"GetAttributes", gl_muerto_tabla}, {"GetTags", gl_muerto_tabla},
        // …nada (no-op)
        {"Destroy", gl_muerto_nada}, {"Remove", gl_muerto_nada},
        {"SetAttribute", gl_muerto_nada}, {"ClearAllChildren", gl_muerto_nada},
        {"AddTag", gl_muerto_nada}, {"RemoveTag", gl_muerto_nada},
        {"PivotTo", gl_muerto_nada}, {"SetPrimaryPartCFrame", gl_muerto_nada},
        // …cadena vacia
        {"GetFullName", gl_muerto_str},
        // …señal inerte (conectarse a nil reventaria igual que antes)
        {"GetPropertyChangedSignal", gl_muerto_senal},
        {"GetAttributeChangedSignal", gl_muerto_senal},
    };
    for (const Entrada& e : tabla)
        if (strcmp(key, e.nombre) == 0) { lua_pushcfunction(L, e.fn, key); return true; }
    // Propiedades: Parent nil es CORRECTO (es como se comprueba "esta
    // destruido"). Name y ClassName como cadena vacia, no nil.
    if (strcmp(key, "Name") == 0 || strcmp(key, "ClassName") == 0) {
        lua_pushstring(L, ""); return true;
    }
    return false;
}

#endif // LUAU_API_METODOS_H

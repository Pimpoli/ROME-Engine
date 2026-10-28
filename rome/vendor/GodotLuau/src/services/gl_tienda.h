#ifndef GL_TIENDA_H
#define GL_TIENDA_H

// MarketplaceService con el estado COMPARTIDO entre scripts.
//
// Cada script tiene su propia VM, y la tienda era una tabla de Luau por VM: el
// ProcessReceipt que pone un Script del servidor no se enteraba de una compra
// hecha desde un LocalScript, PromptProductPurchaseFinished solo saltaba en el
// script que pidio la compra y un pase comprado solo existia para ese script.
// En el place de prueba los productos no daban nada y la recompensa de los
// pases del servidor no llegaba nunca.
//
// Ahora el estado vive aqui:
//  · las compras son de PRUEBA (no se cobra nada), como las de Studio: una
//    ventana pregunta y el jugador acepta o cancela;
//  · ProcessReceipt es uno por proceso (el del servidor) y los recibos que no
//    devuelven PurchaseGranted se reintentan al reasignarlo o cuando el jugador
//    vuelve a entrar, como Roblox; quedan en disco entre partidas;
//  · la propiedad de pases, objetos y paquetes se guarda en user:// y la leen
//    todas las VMs (y las ventanas del multijugador local);
//  · las señales *Finished saltan en todas las VMs que las escuchan.
// En red, el servidor pide la ventana al cliente del jugador y el cliente le
// devuelve el resultado (NetworkService::_mps_red).
//
// El lado Luau esta en luau_stdlib.h (MarketplaceService) y usa __gl_mps.

#include <godot_cpp/classes/canvas_layer.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/center_container.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <string>
#include <vector>
#include "gl_runtime.h"
#include "roblox_services.h"   // PlayerObject
#include "roblox_remote.h"     // _gl_push_node, _gl_node_from_lua, _gl_find_local_player

using namespace godot;

struct GLTiendaCB { lua_State* main_L = nullptr; int ref = -1; bool active = true; };
struct GLRecibo { std::string id; int64_t uid = 0; int64_t producto = 0; bool en_curso = false; };
struct GLTiendaReintento { double t = 0.0; int64_t uid = 0; };

struct GLTienda {
    std::vector<GLTiendaCB> oyentes;           // un despachador por VM que escucha las señales
    GLTiendaCB recibo{ nullptr, -1, false };   // ProcessReceipt (envuelto por el stdlib)
    std::vector<GLRecibo> recibos;             // pendientes de PurchaseGranted
    bool recibos_cargados = false;
    std::vector<GLTiendaReintento> reintentos; // ProcessReceipt diferido (uid 0 = todos)
    uint64_t nodo = 0;                         // GLTiendaNodo: la ventana y los temporizadores
    int64_t contador = 0;
};
inline GLTienda& gl_tienda() { static GLTienda t; return t; }

// ── Disco: propiedad y recibos pendientes ──────────────────────────────
inline String gl_tienda_ruta() { return String("user://gl_marketplace.json"); }

inline Dictionary gl_tienda_leer() {
    Dictionary d;
    Ref<FileAccess> f = FileAccess::open(gl_tienda_ruta(), FileAccess::READ);
    if (f.is_valid()) {
        Variant v = JSON::parse_string(f->get_as_text());
        if (v.get_type() == Variant::DICTIONARY) d = v;
    }
    if (d.get("owned", Variant()).get_type() != Variant::ARRAY) d["owned"] = Array();
    if (d.get("receipts", Variant()).get_type() != Variant::ARRAY) d["receipts"] = Array();
    return d;
}
inline void gl_tienda_escribir(const Dictionary& d) {
    Ref<FileAccess> f = FileAccess::open(gl_tienda_ruta(), FileAccess::WRITE);
    if (f.is_valid()) f->store_string(JSON::stringify(d, "\t"));
}
inline String gl_tienda_clave(const String& tipo, int64_t uid, int64_t id) {
    return tipo + String(":") + String::num_int64(uid) + String(":") + String::num_int64(id);
}
// Se lee del disco en cada consulta: otra VM u otra ventana puede haber comprado.
inline bool gl_tienda_tiene(const String& tipo, int64_t uid, int64_t id) {
    Array o = gl_tienda_leer()["owned"];
    return o.has(gl_tienda_clave(tipo, uid, id));
}
inline void gl_tienda_dar(const String& tipo, int64_t uid, int64_t id) {
    Dictionary d = gl_tienda_leer();
    Array o = d["owned"];
    const String k = gl_tienda_clave(tipo, uid, id);
    if (o.has(k)) return;
    o.push_back(k);
    d["owned"] = o;
    gl_tienda_escribir(d);
}
inline void gl_tienda_guardar_recibos() {
    Dictionary d = gl_tienda_leer();
    Array a;
    for (const GLRecibo& r : gl_tienda().recibos) {
        Dictionary x;
        x["id"] = String::utf8(r.id.c_str());
        x["uid"] = String::num_int64(r.uid);
        x["product"] = String::num_int64(r.producto);
        a.push_back(x);
    }
    d["receipts"] = a;
    gl_tienda_escribir(d);
}
// Los recibos que quedaron sin conceder en una partida anterior.
inline void gl_tienda_cargar_recibos() {
    GLTienda& t = gl_tienda();
    if (t.recibos_cargados) return;
    t.recibos_cargados = true;
    Array a = gl_tienda_leer()["receipts"];
    for (int i = 0; i < a.size(); i++) {
        if (a[i].get_type() != Variant::DICTIONARY) continue;
        Dictionary x = a[i];
        GLRecibo r;
        r.id = String(x.get("id", String())).utf8().get_data();
        r.uid = String(x.get("uid", String("0"))).to_int();
        r.producto = String(x.get("product", String("0"))).to_int();
        if (r.id.empty()) continue;
        bool ya = false;
        for (const GLRecibo& e : t.recibos) if (e.id == r.id) { ya = true; break; }
        if (!ya) t.recibos.push_back(r);
    }
}

// ── Señales en todas las VMs ───────────────────────────────────────────
// Cada VM que escucha registro un despachador; aqui se le pasa el tipo y los
// argumentos de su señal *Finished, que dispara su copia local.
inline void gl_tienda_avisar(const String& tipo, Node* jugador, int64_t uid, int64_t id, bool hecho) {
    const CharString t8 = tipo.utf8();
    gl_fire_cbs(gl_tienda().oyentes, [&](lua_State* th) {
        lua_pushstring(th, t8.get_data());
        if (tipo == "product") {           // (userId, productId, isPurchased)
            lua_pushnumber(th, (double)uid);
        } else {                           // (player, id, wasPurchased)
            _gl_push_node(th, jugador);
        }
        lua_pushnumber(th, (double)id);
        lua_pushboolean(th, hecho ? 1 : 0);
        return 4;
    });
}

// ── ProcessReceipt ─────────────────────────────────────────────────────
// Llama al ProcessReceipt con cada recibo pendiente (de un jugador, o de todos
// con uid 0). El envoltorio del stdlib responde con __gl_mps.recibo_hecho.
inline void gl_tienda_procesar(int64_t solo_uid) {
    GLTienda& t = gl_tienda();
    if (!t.recibo.active || !gl_state_alive(t.recibo.main_L)) return;
    std::vector<std::string> ids;
    for (const GLRecibo& r : t.recibos)
        if (!r.en_curso && (solo_uid == 0 || r.uid == solo_uid)) ids.push_back(r.id);
    for (const std::string& id : ids) {
        // Se vuelve a buscar en cada vuelta: un ProcessReceipt que acaba sin
        // ceder borra su recibo de la lista mientras se recorre.
        GLRecibo* r = nullptr;
        for (GLRecibo& e : t.recibos) if (e.id == id) { r = &e; break; }
        if (!r || r->en_curso) continue;
        if (!t.recibo.active || !gl_state_alive(t.recibo.main_L)) return;
        r->en_curso = true;
        const int64_t uid = r->uid, prod = r->producto;
        lua_State* mL = t.recibo.main_L;
        lua_State* th = lua_newthread(mL);
        lua_rawgeti(mL, LUA_REGISTRYINDEX, t.recibo.ref);
        lua_xmove(mL, th, 1);
        lua_createtable(th, 0, 6);
        lua_pushstring(th, id.c_str());   lua_setfield(th, -2, "PurchaseId");
        lua_pushnumber(th, (double)uid);  lua_setfield(th, -2, "PlayerId");
        lua_pushnumber(th, (double)prod); lua_setfield(th, -2, "ProductId");
        lua_pushnumber(th, 0);            lua_setfield(th, -2, "CurrencySpent");
        uis_push_enum(th, "CurrencyType", 1);           lua_setfield(th, -2, "CurrencyType");            // Robux
        uis_push_enum(th, "ProductPurchaseChannel", 1); lua_setfield(th, -2, "ProductPurchaseChannel");  // InExperience
        gl_resume_handler(th, 1);
        lua_pop(mL, 1);
    }
}
inline void gl_tienda_recibo_hecho(const std::string& id, bool concedido) {
    GLTienda& t = gl_tienda();
    for (size_t i = 0; i < t.recibos.size(); i++) {
        if (t.recibos[i].id != id) continue;
        if (concedido) {
            t.recibos.erase(t.recibos.begin() + i);
            gl_tienda_guardar_recibos();
        } else {
            // NotProcessedYet o error: se reintenta al reasignar ProcessReceipt
            // o cuando ese jugador vuelve a entrar.
            t.recibos[i].en_curso = false;
        }
        return;
    }
}
inline void gl_tienda_nuevo_recibo(int64_t uid, int64_t producto) {
    GLTienda& t = gl_tienda();
    gl_tienda_cargar_recibos();
    GLRecibo r;
    r.id = (String("GL-") + String::num_uint64(Time::get_singleton()->get_ticks_usec(), 16) + "-" +
            String::num_int64(++t.contador)).utf8().get_data();
    r.uid = uid;
    r.producto = producto;
    t.recibos.push_back(r);
    gl_tienda_guardar_recibos();
    gl_tienda_procesar(uid);
}

// ── Red ────────────────────────────────────────────────────────────────
inline Node* gl_tienda_red() {
    return gl_net_service_id() ? gl_cast<Node>(ObjectDB::get_instance(gl_net_service_id())) : nullptr;
}
inline void gl_tienda_enviar(int peer, const String& tipo, int64_t id, int estado) {
    if (Node* ns = gl_tienda_red()) ns->call("net_mps_enviar", peer, tipo, id, estado);
}

// Lo que pasa al cerrarse la ventana (o al llegar el resultado de un cliente).
inline void gl_tienda_resultado(const String& tipo, int64_t id, Node* jugador, bool hecho, bool desde_red) {
    PlayerObject* po = gl_cast<PlayerObject>(jugador);
    const int64_t uid = po ? po->user_id : 0;
    if (hecho && tipo != "product") gl_tienda_dar(tipo, uid, id);   // pases, objetos y paquetes
    gl_tienda_avisar(tipo, jugador, uid, id, hecho);
    if (gl_net_role() == 2) {
        // Cliente: el servidor tiene sus propias señales y el ProcessReceipt.
        if (!desde_red) gl_tienda_enviar(1, tipo, id, hecho ? 1 : 0);
        return;
    }
    if (hecho && tipo == "product") gl_tienda_nuevo_recibo(uid, id);
}

// ── La ventana de compra ───────────────────────────────────────────────
// Va en una capa propia encima de todo; el texto, en ingles como el resto de la
// interfaz del motor.
class GLTiendaNodo : public CanvasLayer {
    GDCLASS(GLTiendaNodo, CanvasLayer);
protected:
    static void _bind_methods() {}
public:
    String tipo;
    int64_t item = 0;
    uint64_t jugador = 0;
    bool abierta = false;
    bool menu_antes = false;
    Control* raiz = nullptr;
    Label* titulo = nullptr;
    Label* nombre = nullptr;
    Label* nota = nullptr;
    Button* comprar = nullptr;
    Button* cancelar = nullptr;

    GLTiendaNodo() { set_layer(120); }

    void _ready() override {
        raiz = memnew(Control);
        raiz->set_name("GLPurchasePrompt");
        raiz->set_anchors_preset(Control::PRESET_FULL_RECT);
        raiz->set_mouse_filter(Control::MOUSE_FILTER_STOP);
        raiz->set_visible(false);
        add_child(raiz);
        ColorRect* fondo = memnew(ColorRect);
        fondo->set_color(Color(0, 0, 0, 0.45f));
        fondo->set_anchors_preset(Control::PRESET_FULL_RECT);
        fondo->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        raiz->add_child(fondo);
        CenterContainer* centro = memnew(CenterContainer);
        centro->set_anchors_preset(Control::PRESET_FULL_RECT);
        centro->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        raiz->add_child(centro);
        PanelContainer* panel = memnew(PanelContainer);
        Ref<StyleBoxFlat> sb;
        sb.instantiate();
        sb->set_bg_color(Color(0.12f, 0.12f, 0.14f, 0.98f));
        sb->set_corner_radius_all(10);
        sb->set_content_margin_all(20);
        panel->add_theme_stylebox_override("panel", sb);
        panel->set_custom_minimum_size(Vector2(340, 0));
        centro->add_child(panel);
        VBoxContainer* col = memnew(VBoxContainer);
        col->add_theme_constant_override("separation", 10);
        panel->add_child(col);
        titulo = memnew(Label);
        titulo->add_theme_font_size_override("font_size", 20);
        col->add_child(titulo);
        nombre = memnew(Label);
        nombre->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        col->add_child(nombre);
        nota = memnew(Label);
        nota->add_theme_font_size_override("font_size", 13);
        nota->add_theme_color_override("font_color", Color(0.7f, 0.7f, 0.74f));
        nota->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        col->add_child(nota);
        HBoxContainer* fila = memnew(HBoxContainer);
        fila->set_alignment(BoxContainer::ALIGNMENT_END);
        fila->add_theme_constant_override("separation", 8);
        col->add_child(fila);
        cancelar = memnew(Button);
        cancelar->set_name("Cancel");
        cancelar->set_custom_minimum_size(Vector2(90, 34));
        cancelar->connect("pressed", callable_mp(this, &GLTiendaNodo::_on_cancelar));
        fila->add_child(cancelar);
        comprar = memnew(Button);
        comprar->set_name("Buy");
        comprar->set_text("Buy");
        comprar->set_custom_minimum_size(Vector2(90, 34));
        comprar->connect("pressed", callable_mp(this, &GLTiendaNodo::_on_comprar));
        fila->add_child(comprar);
    }

    void mostrar(const String& t, int64_t id, Node* j, bool ya_lo_tiene) {
        if (abierta || !raiz) return;   // como Roblox: una compra a la vez
        tipo = t;
        item = id;
        jugador = j ? (uint64_t)j->get_instance_id() : 0;
        abierta = true;
        String que = "Item";
        if (t == "product") que = "Developer Product";
        else if (t == "gamepass") que = "Game Pass";
        else if (t == "bundle") que = "Bundle";
        nombre->set_text(que + " " + String::num_int64(id));
        if (ya_lo_tiene) {
            titulo->set_text("Already owned");
            nota->set_text("You already own this item.");
            comprar->set_visible(false);
            cancelar->set_text("OK");
        } else {
            titulo->set_text("Buy item");
            nota->set_text("This is a test purchase; your account will not be charged.");
            comprar->set_visible(true);
            cancelar->set_text("Cancel");
        }
        raiz->set_visible(true);
        // Con la ventana abierta el personaje no se mueve y el raton queda libre.
        menu_antes = gl_menu_open();
        gl_set_menu_open(true);
        if (Input* in = Input::get_singleton()) in->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
    }

    void cerrar(bool hecho) {
        if (!abierta) return;
        abierta = false;
        raiz->set_visible(false);
        gl_set_menu_open(menu_antes);
        gl_raton_aplicar();
        Node* j = jugador ? gl_cast<Node>(ObjectDB::get_instance(ObjectID(jugador))) : nullptr;
        gl_tienda_resultado(tipo, item, j, hecho, false);
    }
    void _on_comprar() { cerrar(true); }
    void _on_cancelar() { cerrar(false); }

    void _process(double delta) override {
        std::vector<GLTiendaReintento>& v = gl_tienda().reintentos;
        if (v.empty()) return;
        std::vector<int64_t> listos;
        for (int i = (int)v.size() - 1; i >= 0; i--) {
            v[i].t -= delta;
            if (v[i].t > 0.0) continue;
            listos.push_back(v[i].uid);
            v.erase(v.begin() + i);
        }
        for (int64_t uid : listos) gl_tienda_procesar(uid);
    }
};

// El nodo de la tienda, creado la primera vez que hace falta.
inline GLTiendaNodo* gl_tienda_nodo() {
    GLTienda& t = gl_tienda();
    if (t.nodo) {
        if (GLTiendaNodo* n = gl_cast<GLTiendaNodo>(ObjectDB::get_instance(ObjectID(t.nodo))))
            if (n->is_inside_tree()) return n;
    }
    SceneTree* st = gl_cast<SceneTree>(Engine::get_singleton()->get_main_loop());
    if (!st || !st->get_root()) return nullptr;
    GLTiendaNodo* n = memnew(GLTiendaNodo);
    n->set_name("GLMarketplace");
    n->set_meta("_gl_interno", true);
    st->get_root()->add_child(n, false, Node::INTERNAL_MODE_BACK);
    t.nodo = (uint64_t)n->get_instance_id();
    return n;
}
inline void gl_tienda_programar(int64_t uid, double segundos) {
    if (!gl_tienda_nodo()) return;
    gl_tienda().reintentos.push_back({ segundos, uid });
}

// Una compra pedida para `jugador`: la ventana sale en SU maquina.
inline void gl_tienda_pedir(const String& tipo, Node* jugador, int64_t id) {
    PlayerObject* po = gl_cast<PlayerObject>(jugador);
    if (!po) return;
    if (!po->is_local) {
        // Servidor pidiendo la compra a un cliente.
        if (gl_net_role() == 1 && po->peer_id > 0) gl_tienda_enviar(po->peer_id, tipo, id, -1);
        return;
    }
    GLTiendaNodo* n = gl_tienda_nodo();
    if (!n) return;
    const bool ya = tipo != "product" && gl_tienda_tiene(tipo, po->user_id, id);
    n->mostrar(tipo, id, po, ya);
}

// La red entrega aqui: estado -1 = el servidor pide la ventana (cliente);
// 0/1 = resultado de un cliente (servidor).
inline void gl_tienda_desde_red(int peer, const String& tipo, int64_t id, int estado) {
    Node* ns = gl_tienda_red();
    if (!ns) return;
    if (estado < 0) {
        gl_tienda_pedir(tipo, _gl_find_local_player(ns), id);
        return;
    }
    Node* j = gl_cast<Node>((Object*)ns->call("player_node_for_peer", peer));
    gl_tienda_resultado(tipo, id, j, estado != 0, true);
}

// Players avisa al entrar un jugador: sus recibos pendientes se reintentan un
// poco despues, cuando el juego ya ha cargado sus datos.
inline void gl_tienda_jugador_entra(Node* jugador) {
    PlayerObject* po = gl_cast<PlayerObject>(jugador);
    if (!po || gl_tienda().recibos.empty()) return;
    for (const GLRecibo& r : gl_tienda().recibos)
        if (r.uid == po->user_id) { gl_tienda_programar(po->user_id, 3.0); return; }
}

// ── __gl_mps en cada VM ────────────────────────────────────────────────
// Funciones con nombre y no lambdas: lua_pushcfunction es una macro, y una coma
// fuera de parentesis dentro de la lambda (un GLTiendaCB{a, b, c}) la rompe.

// escuchar(fn): fn(tipo, ...) dispara la señal *Finished de esta VM.
inline int gl_mps_escuchar(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    GLTiendaCB cb;
    cb.main_L = gl_main_of(L);
    cb.ref = gl_ref(L, -1);
    lua_pop(L, 1);
    gl_tienda().oyentes.push_back(cb);
    return 0;
}
// recibos(fn | nil): el ProcessReceipt envuelto. En un cliente no hace nada.
inline int gl_mps_recibos(lua_State* L) {
    if (gl_net_role() == 2) return 0;
    GLTienda& t = gl_tienda();
    if (t.recibo.active && t.recibo.ref >= 0 && gl_state_alive(t.recibo.main_L))
        gl_unref(t.recibo.main_L, t.recibo.ref);
    t.recibo = GLTiendaCB();
    t.recibo.active = false;
    for (GLRecibo& r : t.recibos) r.en_curso = false;
    if (!lua_isfunction(L, 1)) return 0;
    lua_pushvalue(L, 1);
    t.recibo.main_L = gl_main_of(L);
    t.recibo.ref = gl_ref(L, -1);
    t.recibo.active = true;
    lua_pop(L, 1);
    // Los pendientes, un poco despues: Roblox no llama en mitad de la
    // asignacion, y el juego suele terminar de prepararse justo detras.
    gl_tienda_cargar_recibos();
    if (!t.recibos.empty()) gl_tienda_programar(0, 0.5);
    return 0;
}
// recibo_hecho(purchaseId, concedido)
inline int gl_mps_recibo_hecho(lua_State* L) {
    gl_tienda_recibo_hecho(luaL_checkstring(L, 1), lua_toboolean(L, 2) != 0);
    return 0;
}
// pedir(tipo, jugador, id)
inline int gl_mps_pedir(lua_State* L) {
    const String tipo = String::utf8(luaL_checkstring(L, 1));
    gl_tienda_pedir(tipo, _gl_node_from_lua(L, 2), (int64_t)luaL_optnumber(L, 3, 0));
    return 0;
}
// tiene(tipo, userId, id)
inline int gl_mps_tiene(lua_State* L) {
    const String tipo = String::utf8(luaL_checkstring(L, 1));
    const int64_t uid = (int64_t)luaL_optnumber(L, 2, 0);
    const int64_t id = (int64_t)luaL_optnumber(L, 3, 0);
    lua_pushboolean(L, gl_tienda_tiene(tipo, uid, id) ? 1 : 0);
    return 1;
}

inline void gl_tienda_registrar(lua_State* L) {
    gl_jugador_entra_hook() = gl_tienda_jugador_entra;
    gl_tienda_red_hook() = gl_tienda_desde_red;
    lua_newtable(L);
    lua_pushcfunction(L, gl_mps_escuchar, "escuchar");
    lua_setfield(L, -2, "escuchar");
    lua_pushcfunction(L, gl_mps_recibos, "recibos");
    lua_setfield(L, -2, "recibos");
    lua_pushcfunction(L, gl_mps_recibo_hecho, "recibo_hecho");
    lua_setfield(L, -2, "recibo_hecho");
    lua_pushcfunction(L, gl_mps_pedir, "pedir");
    lua_setfield(L, -2, "pedir");
    lua_pushcfunction(L, gl_mps_tiene, "tiene");
    lua_setfield(L, -2, "tiene");
    lua_setglobal(L, "__gl_mps");
}

#endif // GL_TIENDA_H

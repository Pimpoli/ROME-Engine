#ifndef GL_PUENTE_CHAT_H
#define GL_PUENTE_CHAT_H

// ════════════════════════════════════════════════════════════════════
//  TextChatService moderno (F5 de la v1.17): el puente y el recorrido de un
//  mensaje. Se incluye desde luau_api.h, despues de sus ayudantes.
//
//  Un mensaje del jugador local (la barra del chat o TextChannel:SendAsync):
//    Player.Chatted → ¿es un TextChatCommand? → Triggered y no se envia
//    → SendingMessage → OnIncomingMessage (servicio y canal) → la ventana
//    → MessageReceived (canal y servicio) → la burbuja → la red.
//  Uno que llega por red recorre lo mismo desde OnIncomingMessage.
//  DisplaySystemMessage, igual pero sin TextSource y sin burbuja.
//
//  Antes TextChatService tenia SendMessage y un MessageReceived que pasaba el
//  texto "Nombre: mensaje": no habia TextChannels, ni TextSource, ni
//  OnIncomingMessage, ni comandos. Las etiquetas de chat ([VIP]), los mensajes
//  de sistema y los comandos /algo de cualquier juego no hacian nada.
//
//  El texto de un jugador se escapa como en Roblox (&lt;...): en sus mensajes
//  no vale el RichText. PrefixText y los mensajes de sistema si lo admiten.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/label3d.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/classes/file_access.hpp>

struct GLChatMensaje {
    String texto, prefijo, id, metadata;
    uint64_t fuente = 0, canal = 0;
    int estado = 2;   // Enum.TextChatMessageStatus.Success (Sending = 3)
};

inline Node* gl_chat_nodo(uint64_t id) {
    return id ? gl_cast<Node>(ObjectDB::get_instance(ObjectID(id))) : nullptr;
}
inline uint64_t gl_chat_id(Node* n) { return n ? (uint64_t)n->get_instance_id() : 0; }

inline String gl_chat_escapar(const String& s) {
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            .replace("\"", "&quot;").replace("'", "&apos;");
}

// El TextChatMessage: una tabla con los campos de Roblox (uno por VM).
inline void gl_chat_push_mensaje(lua_State* L, const GLChatMensaje& m) {
    lua_createtable(L, 0, 14);
    lua_pushstring(L, m.texto.utf8().get_data());    lua_setfield(L, -2, "Text");
    lua_pushstring(L, m.prefijo.utf8().get_data());  lua_setfield(L, -2, "PrefixText");
    lua_pushstring(L, m.id.utf8().get_data());       lua_setfield(L, -2, "MessageId");
    lua_pushstring(L, m.metadata.utf8().get_data()); lua_setfield(L, -2, "Metadata");
    lua_pushstring(L, "");                           lua_setfield(L, -2, "Translation");
    uis_push_enum(L, "TextChatMessageStatus", m.estado); lua_setfield(L, -2, "Status");
    wrap_node(L, gl_chat_nodo(m.fuente));            lua_setfield(L, -2, "TextSource");
    wrap_node(L, gl_chat_nodo(m.canal));             lua_setfield(L, -2, "TextChannel");
    lua_pushstring(L, "TextChatMessage");            lua_setfield(L, -2, "ClassName");
    lua_pushstring(L, "TextChatMessage");            lua_setfield(L, -2, "Name");
    lua_getglobal(L, "DateTime");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "now");
        lua_remove(L, -2);
        if (lua_isfunction(L, -1) && lua_pcall(L, 0, 1, 0) == LUA_OK) lua_setfield(L, -2, "Timestamp");
        else lua_pop(L, 1);
    } else lua_pop(L, 1);
    lua_pushcfunction(L, [](lua_State* LL) -> int {
        const char* c = luaL_optstring(LL, 2, "");
        lua_pushboolean(LL, strcmp(c, "TextChatMessage") == 0 || strcmp(c, "Instance") == 0);
        return 1;
    }, "IsA");
    lua_setfield(L, -2, "IsA");
}

// OnIncomingMessage: se llama en el acto (no puede ceder) y lo que devuelve
// (TextChatMessageProperties) cambia como se ve el mensaje.
inline void gl_chat_aplicar_entrante(const GLChatCB& cb, GLChatMensaje& m) {
    if (!gl_chat_cb_vivo(cb)) return;
    lua_State* mL = cb.main_L;
    lua_State* th = lua_newthread(mL);
    lua_rawgeti(th, LUA_REGISTRYINDEX, cb.ref);
    gl_chat_push_mensaje(th, m);
    if (lua_pcall(th, 1, 1, 0) != LUA_OK) {
        const char* e = lua_tostring(th, -1);
        UtilityFunctions::push_error(String("OnIncomingMessage: ") + String::utf8(e ? e : "error"));
    } else if (TextChatMessageProperties* p = gl_cast<TextChatMessageProperties>(_gl_node_from_lua(th, -1))) {
        if (p->tiene_text) m.texto = p->text;
        if (p->tiene_prefix) m.prefijo = p->prefix;
    }
    lua_pop(mL, 1);
}

// ── La ventana y las burbujas ──────────────────────────────────────────
inline Color gl_chat_color_nombre(const String& nombre) {
    static const Color colores[] = {
        Color(0.40f, 0.76f, 0.98f), Color(0.98f, 0.73f, 0.40f), Color(0.67f, 0.95f, 0.60f),
        Color(0.95f, 0.60f, 0.80f), Color(0.80f, 0.65f, 0.98f), Color(0.98f, 0.90f, 0.45f),
    };
    int h = 0;
    for (int i = 0; i < nombre.length(); i++) h += (int)nombre[i];
    return colores[h % 6];
}
inline String gl_chat_linea(const GLChatMensaje& m, bool sistema, const String& nombre) {
    if (sistema) {
        const Color c(0.79f, 0.82f, 0.88f);
        return "[color=#" + c.to_html(false) + "]" + gl_rich_a_bbcode(m.texto, c) + "[/color]";
    }
    const Color nc = gl_chat_color_nombre(nombre);
    const Color tc(0.90f, 0.90f, 0.92f);
    String linea;
    if (!m.prefijo.is_empty())
        linea += "[color=#" + nc.to_html(false) + "][b]" + gl_rich_a_bbcode(m.prefijo, nc) + ":[/b][/color] ";
    return linea + "[color=#" + tc.to_html(false) + "]" + gl_rich_a_bbcode(m.texto, tc) + "[/color]";
}

inline bool gl_chat_config_activa(Node* tcs, const char* nombre, bool por_defecto) {
    GLChatConfig* c = tcs ? gl_cast<GLChatConfig>(tcs->get_node_or_null(NodePath(nombre))) : nullptr;
    return c ? c->enabled : por_defecto;
}

// Burbuja sobre una pieza o un personaje (su Head). Una por hablante: la nueva
// sustituye a la anterior, y se va sola a los 15 s.
inline void gl_chat_burbuja(Node* objetivo, const String& texto) {
    if (!objetivo || texto.is_empty()) return;
    Node3D* ancla = gl_cast<Node3D>(objetivo->get_node_or_null(NodePath("Head")));
    if (!ancla) ancla = gl_cast<Node3D>(objetivo);
    if (!ancla || !ancla->is_inside_tree()) return;
    if (Node* vieja = ancla->get_node_or_null(NodePath("GLChatBubble"))) {
        ancla->remove_child(vieja);
        vieja->queue_free();
    }
    Label3D* l = memnew(Label3D);
    l->set_name("GLChatBubble");
    l->set_text(texto);
    l->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
    l->set_font_size(40);
    l->set_pixel_size(0.01f);
    l->set_outline_size(12);
    l->set_modulate(Color(1, 1, 1));
    l->set_outline_modulate(Color(0.08f, 0.08f, 0.1f));
    l->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    l->set_width(520);
    l->set_vertical_alignment(VERTICAL_ALIGNMENT_BOTTOM);
    l->set_position(Vector3(0, 1.8f, 0));
    l->set_meta("_gl_interno", true);
    ancla->add_child(l, false, Node::INTERNAL_MODE_BACK);
    if (SceneTree* st = ancla->get_tree()) st->create_timer(15.0)->connect("timeout", Callable(l, "queue_free"));
}
inline void gl_chat_burbuja_de(Node* tcs, Node* jugador, const String& texto) {
    PlayerObject* po = gl_cast<PlayerObject>(jugador);
    if (!po || !gl_chat_config_activa(tcs, "BubbleChatConfiguration", true)) return;
    gl_chat_burbuja(po->get_character(), texto);
}

// ── Canales y TextSource ───────────────────────────────────────────────
inline TextChannel* gl_chat_canal(Node* tcs, const char* nombre) {
    return tcs ? gl_cast<TextChannel>(tcs->get_node_or_null(NodePath(String("TextChannels/") + nombre))) : nullptr;
}
// Donde escribe la barra: ChatInputBarConfiguration.TargetTextChannel o RBXGeneral.
inline TextChannel* gl_chat_canal_barra(Node* tcs) {
    Node* cfg = tcs ? tcs->get_node_or_null(NodePath("ChatInputBarConfiguration")) : nullptr;
    if (cfg && cfg->has_meta("_glo_TargetTextChannel"))
        if (TextChannel* c = gl_cast<TextChannel>(gl_chat_nodo((uint64_t)(int64_t)cfg->get_meta("_glo_TargetTextChannel"))))
            return c;
    return gl_chat_canal(tcs, "RBXGeneral");
}
// El TextSource del jugador en ese canal; se crea la primera vez que habla.
inline TextSource* gl_chat_fuente(TextChannel* canal, int64_t uid, const String& nombre) {
    if (!canal) return nullptr;
    for (int i = 0; i < canal->get_child_count(); i++)
        if (TextSource* ts = gl_cast<TextSource>(canal->get_child(i)))
            if (ts->user_id == uid) return ts;
    TextSource* ts = memnew(TextSource);
    ts->set_name(nombre.is_empty() ? String("User") + String::num_int64(uid) : nombre);
    ts->user_id = uid;
    canal->add_child(ts);
    return ts;
}
inline TextSource* gl_chat_fuente_de(TextChannel* canal, Node* jugador) {
    PlayerObject* po = gl_cast<PlayerObject>(jugador);
    return po ? gl_chat_fuente(canal, po->user_id, String(po->get_name())) : nullptr;
}
inline String gl_chat_nombre_visible(Node* jugador) {
    PlayerObject* po = gl_cast<PlayerObject>(jugador);
    if (!po) return String("Player");
    return po->display_name.is_empty() ? String(po->get_name()) : po->display_name;
}

// ── Entregar un mensaje en esta maquina ────────────────────────────────
inline void gl_chat_entregar(TextChatService* tcs, TextChannel* canal, GLChatMensaje m, bool sistema,
                             const String& nombre, Node* hablante, const String& texto_burbuja) {
    if (!tcs) return;
    const GLChatMensaje original = m;   // MessageReceived recibe el mensaje, no lo que pinto el callback
    gl_chat_aplicar_entrante(tcs->entrante, m);
    if (canal) gl_chat_aplicar_entrante(canal->entrante, m);
    if (RobloxChat* w = tcs->get_chat()) w->gl_mostrar(gl_chat_linea(m, sistema, nombre));
    auto empujar = [&](lua_State* th) { gl_chat_push_mensaje(th, original); return 1; };
    if (canal) gl_fire_cbs(canal->recibido_cbs, empujar);
    gl_fire_cbs(tcs->recibido_cbs, empujar);
    if (!sistema && hablante) gl_chat_burbuja_de(tcs, hablante, texto_burbuja);
}
inline String gl_chat_nuevo_id() {
    static int64_t n = 0;
    return String("GLMSG-") + String::num_int64(++n);
}

inline GLChatMensaje gl_chat_sistema(TextChatService* tcs, TextChannel* canal, const String& texto, const String& meta) {
    GLChatMensaje m;
    m.texto = texto;
    m.metadata = meta;
    m.canal = gl_chat_id(canal);
    m.id = gl_chat_nuevo_id();
    gl_chat_entregar(tcs, canal, m, true, String(), nullptr, String());
    return m;
}

inline Node* gl_chat_red() {
    return gl_net_service_id() ? gl_cast<Node>(ObjectDB::get_instance(gl_net_service_id())) : nullptr;
}

// Un mensaje del jugador local por `canal` (la barra o SendAsync).
inline GLChatMensaje gl_chat_enviar_local(TextChatService* tcs, TextChannel* canal, const String& texto, const String& meta) {
    Node* yo = _gl_find_local_player(tcs);
    const String nombre = gl_chat_nombre_visible(yo);
    GLChatMensaje m;
    m.texto = gl_chat_escapar(texto);
    m.prefijo = nombre;
    m.metadata = meta;
    m.fuente = gl_chat_id(gl_chat_fuente_de(canal, yo));
    m.canal = gl_chat_id(canal);
    m.id = gl_chat_nuevo_id();
    m.estado = 3;   // Sending
    gl_fire_cbs(tcs->enviando_cbs, [&](lua_State* th) { gl_chat_push_mensaje(th, m); return 1; });
    m.estado = 2;
    if (PlayerObject* po = gl_cast<PlayerObject>(yo)) po->gl_fire_chatted(texto);
    gl_chat_entregar(tcs, canal, m, false, nombre, yo, texto);
    if (Node* ns = gl_chat_red()) ns->call("gl_send_chat", nombre, texto);
    return m;
}

// ── Comandos ───────────────────────────────────────────────────────────
inline TextChatCommand* gl_chat_buscar_comando(Node* n, const String& texto, int prof = 0) {
    if (!n || prof > 4) return nullptr;
    for (int i = 0; i < n->get_child_count(); i++) {
        Node* c = n->get_child(i);
        if (TextChatCommand* k = gl_cast<TextChatCommand>(c))
            if (k->enabled && k->coincide(texto)) return k;
        if (TextChatCommand* k = gl_chat_buscar_comando(c, texto, prof + 1)) return k;
    }
    return nullptr;
}
inline bool gl_chat_es_comando(TextChatService* tcs, const String& texto) {
    return gl_chat_buscar_comando(tcs, texto.strip_edges()) != nullptr;
}
// Lo que hacen los comandos de Roblox que aqui tienen sentido.
inline void gl_chat_comando_propio(TextChatService* tcs, TextChatCommand* cmd) {
    const String nombre = cmd->get_name();
    TextChannel* sis = gl_chat_canal(tcs, "RBXSystem");
    if (nombre == "RBXClearCommand") {
        if (RobloxChat* w = tcs->get_chat()) w->gl_limpiar();
    } else if (nombre == "RBXHelpCommand") {
        String lista;
        if (Node* cmds = tcs->get_node_or_null(NodePath("TextChatCommands")))
            for (int i = 0; i < cmds->get_child_count(); i++)
                if (TextChatCommand* k = gl_cast<TextChatCommand>(cmds->get_child(i)))
                    if (k->enabled && !k->primary.is_empty() && k->autocomplete)
                        lista += (lista.is_empty() ? "" : ", ") + k->primary;
        gl_chat_sistema(tcs, sis, String("Chat commands: ") + lista, String());
    } else if (nombre == "RBXVersionCommand") {
        String v = FileAccess::get_file_as_string("res://Version").strip_edges();
        gl_chat_sistema(tcs, sis, String("GodotLuau ") + (v.is_empty() ? String("(unknown version)") : v), String());
    } else if (nombre != "RBXEmoteCommand") {
        // /e lo hace el Animate de cada juego por Player.Chatted.
        gl_chat_sistema(tcs, sis, String("This command is not available here."), String());
    }
}

// ── Los tres caminos de entrada ────────────────────────────────────────
// La barra del chat del jugador local.
inline void gl_chat_desde_barra(TextChatService* tcs, const String& crudo) {
    const String texto = crudo.strip_edges();
    if (!tcs || texto.is_empty()) return;
    if (TextChatCommand* cmd = gl_chat_buscar_comando(tcs, texto)) {
        Node* yo = _gl_find_local_player(tcs);
        if (PlayerObject* po = gl_cast<PlayerObject>(yo)) po->gl_fire_chatted(texto);
        TextSource* fuente = gl_chat_fuente_de(gl_chat_canal_barra(tcs), yo);
        gl_fire_cbs(cmd->disparado_cbs, [&](lua_State* th) {
            wrap_node(th, fuente);
            lua_pushstring(th, texto.utf8().get_data());
            return 2;
        });
        if (cmd->has_meta("_gl_rbx_cmd")) gl_chat_comando_propio(tcs, cmd);
        // El servidor tiene sus propios comandos (y su Chatted).
        if (gl_net_role() == 2) if (Node* ns = gl_chat_red()) ns->call("gl_send_cmd", texto);
        return;
    }
    gl_chat_enviar_local(tcs, gl_chat_canal_barra(tcs), texto, String());
}
// Lo que escribio otro jugador (o, en el servidor, cualquiera).
inline void gl_chat_desde_red(TextChatService* tcs, Node* jugador, const String& nombre, const String& texto) {
    if (!tcs) return;
    TextChannel* canal = gl_chat_canal(tcs, "RBXGeneral");
    GLChatMensaje m;
    m.texto = gl_chat_escapar(texto);
    m.prefijo = nombre;
    m.fuente = gl_chat_id(gl_chat_fuente_de(canal, jugador));
    m.canal = gl_chat_id(canal);
    m.id = gl_chat_nuevo_id();
    gl_chat_entregar(tcs, canal, m, false, nombre, jugador, texto);
}
// Un comando de un cliente, en el servidor: solo Triggered.
inline void gl_chat_comando_de_red(TextChatService* tcs, Node* jugador, const String& texto) {
    TextChatCommand* cmd = gl_chat_buscar_comando(tcs, texto);
    if (!cmd) return;
    TextSource* fuente = gl_chat_fuente_de(gl_chat_canal(tcs, "RBXGeneral"), jugador);
    gl_fire_cbs(cmd->disparado_cbs, [&](lua_State* th) {
        wrap_node(th, fuente);
        lua_pushstring(th, texto.utf8().get_data());
        return 2;
    });
}
// TextChatService:SendMessage(nombre, texto), la API vieja de GodotLuau.
inline void gl_chat_mostrar_de(TextChatService* tcs, const String& nombre, const String& texto) {
    TextChannel* canal = gl_chat_canal(tcs, "RBXGeneral");
    GLChatMensaje m;
    m.texto = gl_chat_escapar(texto);
    m.prefijo = nombre;
    m.canal = gl_chat_id(canal);
    m.id = gl_chat_nuevo_id();
    gl_chat_entregar(tcs, canal, m, false, nombre, nullptr, String());
}

// ── El puente ──────────────────────────────────────────────────────────
// 0 TextChatService.MessageReceived, 1 SendingMessage, 2 TextChannel.MessageReceived,
// 3 TextChatCommand.Triggered.
inline std::vector<GLChatCB>* gl_chat_lista(Node* x, int cual) {
    if (TextChatService* t = gl_cast<TextChatService>(x)) return cual == 0 ? &t->recibido_cbs : cual == 1 ? &t->enviando_cbs : nullptr;
    if (TextChannel* c = gl_cast<TextChannel>(x)) return cual == 2 ? &c->recibido_cbs : nullptr;
    if (TextChatCommand* k = gl_cast<TextChatCommand>(x)) return cual == 3 ? &k->disparado_cbs : nullptr;
    return nullptr;
}
inline int gl_chat_push_senal(lua_State* L, Node* n, int cual) {
    lua_newtable(L);
    lua_pushlightuserdata(L, (void*)(uintptr_t)gl_chat_id(n));
    lua_pushinteger(L, cual);
    lua_pushcclosure(L, [](lua_State* LL) -> int {
        Node* x = gl_chat_nodo((uint64_t)(uintptr_t)lua_touserdata(LL, lua_upvalueindex(1)));
        std::vector<GLChatCB>* v = x ? gl_chat_lista(x, (int)lua_tointeger(LL, lua_upvalueindex(2))) : nullptr;
        if (!v || !lua_isfunction(LL, 2)) { lua_pushnil(LL); return 1; }
        lua_pushvalue(LL, 2);
        GLChatCB cb;
        cb.ref = gl_ref(LL, -1);
        lua_pop(LL, 1);
        cb.main_L = gl_main_of(LL);
        v->push_back(cb);
        _gl_push_connection(LL, x, cb.ref);
        return 1;
    }, "Connect", 2);
    lua_setfield(L, -2, "Connect");
    gl_signal_lowercase_aliases(L);
    return 1;
}
// Un callback de un solo dueño se lee como la funcion si es de esta VM; si es
// de otra, true (una funcion no cruza de VM).
inline int gl_chat_push_cb(lua_State* L, const GLChatCB& cb) {
    if (!gl_chat_cb_vivo(cb)) lua_pushnil(L);
    else if (cb.main_L == gl_main_of(L)) lua_rawgeti(L, LUA_REGISTRYINDEX, cb.ref);
    else lua_pushboolean(L, 1);
    return 1;
}
inline TextChatService* gl_chat_tcs_de_canal(Node* canal) {
    Node* p = canal ? canal->get_parent() : nullptr;
    if (TextChatService* t = gl_cast<TextChatService>(p)) return t;
    return p ? gl_cast<TextChatService>(p->get_parent()) : nullptr;
}

inline int gl_chat_index(lua_State* L, Node* n, const char* key) {
    if (TextChatService* t = gl_cast<TextChatService>(n)) {
        // Enum.ChatVersion.TextChatService: los juegos lo miran para elegir API.
        // El EnumItem, no el numero: `== Enum.ChatVersion.TextChatService` con un
        // numero da false siempre (antes pasaba, y el juego usaba el chat viejo).
        if (strcmp(key, "ChatVersion") == 0)       { uis_push_enum(L, "ChatVersion", 1); return 1; }
        if (strcmp(key, "MessageReceived") == 0)   return gl_chat_push_senal(L, t, 0);
        if (strcmp(key, "SendingMessage") == 0)    return gl_chat_push_senal(L, t, 1);
        if (strcmp(key, "OnIncomingMessage") == 0) return gl_chat_push_cb(L, t->entrante);
        if (strcmp(key, "OnBubbleAdded") == 0)     return gl_chat_push_cb(L, t->burbuja_cb);
        if (strcmp(key, "OnChatWindowAdded") == 0) return gl_chat_push_cb(L, t->ventana_cb);
        if (strcmp(key, "CreateDefaultCommands") == 0 || strcmp(key, "CreateDefaultTextChannels") == 0) { lua_pushboolean(L, 1); return 1; }
        if (strcmp(key, "ChatTranslationEnabled") == 0) { lua_pushboolean(L, 0); return 1; }
        if (strcmp(key, "CanUserChatAsync") == 0 || strcmp(key, "CanUsersChatAsync") == 0 ||
            strcmp(key, "CanUsersDirectChatAsync") == 0) {
            lua_pushcfunction(L, [](lua_State* LL) -> int { lua_pushboolean(LL, 1); return 1; }, "CanUserChatAsync");
            return 1;
        }
        if (strcmp(key, "DisplayBubble") == 0) {
            lua_pushcfunction(L, [](lua_State* LL) -> int {
                Node* obj = _gl_node_from_lua(LL, 2);
                const char* s = lua_tostring(LL, 3);
                if (obj && s) gl_chat_burbuja(obj, gl_rich_entidades(_gl_strip_rich_tags(String::utf8(s))));
                return 0;
            }, "DisplayBubble");
            return 1;
        }
        // SendMessage(texto) y SendMessage(nombre, texto): la API vieja de GodotLuau.
        if (strcmp(key, "SendMessage") == 0) {
            lua_pushlightuserdata(L, (void*)(uintptr_t)gl_chat_id(t));
            lua_pushcclosure(L, [](lua_State* LL) -> int {
                TextChatService* x = gl_cast<TextChatService>(gl_chat_nodo((uint64_t)(uintptr_t)lua_touserdata(LL, lua_upvalueindex(1))));
                if (!x) return 0;
                if (lua_gettop(LL) >= 3 && !lua_isnil(LL, 3)) gl_chat_mostrar_de(x, gl_str(LL, 2), gl_str(LL, 3));
                else gl_chat_enviar_local(x, gl_chat_canal(x, "RBXGeneral"), gl_str(LL, 2), String());
                return 0;
            }, "SendMessage", 1);
            return 1;
        }
        if (strcmp(key, "SetPlayerName") == 0) {
            lua_pushlightuserdata(L, (void*)(uintptr_t)gl_chat_id(t));
            lua_pushcclosure(L, [](lua_State* LL) -> int {
                TextChatService* x = gl_cast<TextChatService>(gl_chat_nodo((uint64_t)(uintptr_t)lua_touserdata(LL, lua_upvalueindex(1))));
                if (x) x->set_player_name(gl_str(LL, 2));
                return 0;
            }, "SetPlayerName", 1);
            return 1;
        }
        return -1;
    }
    if (TextChannel* c = gl_cast<TextChannel>(n)) {
        if (strcmp(key, "MessageReceived") == 0)       return gl_chat_push_senal(L, c, 2);
        if (strcmp(key, "OnIncomingMessage") == 0)     return gl_chat_push_cb(L, c->entrante);
        if (strcmp(key, "ShouldDeliverCallback") == 0) return gl_chat_push_cb(L, c->entregar);
        if (strcmp(key, "DirectChatRequester") == 0)   { lua_pushnil(L); return 1; }
        const bool enviar = strcmp(key, "SendAsync") == 0;
        if (enviar || strcmp(key, "DisplaySystemMessage") == 0) {
            lua_pushlightuserdata(L, (void*)(uintptr_t)gl_chat_id(c));
            lua_pushboolean(L, enviar ? 1 : 0);
            lua_pushcclosure(L, [](lua_State* LL) -> int {
                TextChannel* x = gl_cast<TextChannel>(gl_chat_nodo((uint64_t)(uintptr_t)lua_touserdata(LL, lua_upvalueindex(1))));
                TextChatService* tcs = gl_chat_tcs_de_canal(x);
                if (!x || !tcs) return 0;
                const String texto = String::utf8(luaL_checkstring(LL, 2));
                const String meta = String::utf8(luaL_optstring(LL, 3, ""));
                const GLChatMensaje m = lua_toboolean(LL, lua_upvalueindex(2))
                    ? gl_chat_enviar_local(tcs, x, texto, meta)
                    : gl_chat_sistema(tcs, x, texto, meta);
                gl_chat_push_mensaje(LL, m);
                return 1;
            }, enviar ? "SendAsync" : "DisplaySystemMessage", 2);
            return 1;
        }
        // AddUserAsync(userId) -> (TextSource, true)
        if (strcmp(key, "AddUserAsync") == 0) {
            lua_pushlightuserdata(L, (void*)(uintptr_t)gl_chat_id(c));
            lua_pushcclosure(L, [](lua_State* LL) -> int {
                TextChannel* x = gl_cast<TextChannel>(gl_chat_nodo((uint64_t)(uintptr_t)lua_touserdata(LL, lua_upvalueindex(1))));
                if (!x) { lua_pushnil(LL); lua_pushboolean(LL, 0); return 2; }
                const int64_t uid = (int64_t)luaL_checknumber(LL, 2);
                String nombre;
                if (Node* ps = gl_servicio(x, "Players"))
                    for (int i = 0; i < ps->get_child_count(); i++)
                        if (PlayerObject* po = gl_cast<PlayerObject>(ps->get_child(i)))
                            if (po->user_id == uid) nombre = po->get_name();
                wrap_node(LL, gl_chat_fuente(x, uid, nombre));
                lua_pushboolean(LL, 1);
                return 2;
            }, "AddUserAsync", 1);
            return 1;
        }
        if (strcmp(key, "SetDirectChatRequester") == 0) {
            lua_pushcfunction(L, [](lua_State*) -> int { return 0; }, "SetDirectChatRequester");
            return 1;
        }
        return -1;
    }
    if (TextChatCommand* k = gl_cast<TextChatCommand>(n)) {
        if (strcmp(key, "Triggered") == 0) return gl_chat_push_senal(L, k, 3);
        return -1;
    }
    return -1;
}

// Escritura de los callbacks. true si era suya.
inline bool gl_chat_newindex(lua_State* L, Node* n, const char* key) {
    if (TextChatService* t = gl_cast<TextChatService>(n)) {
        if (strcmp(key, "OnIncomingMessage") == 0) { gl_chat_cb_poner(t->entrante, L, 3); return true; }
        if (strcmp(key, "OnBubbleAdded") == 0)     { gl_chat_cb_poner(t->burbuja_cb, L, 3); return true; }
        if (strcmp(key, "OnChatWindowAdded") == 0) { gl_chat_cb_poner(t->ventana_cb, L, 3); return true; }
        return false;
    }
    if (TextChannel* c = gl_cast<TextChannel>(n)) {
        if (strcmp(key, "OnIncomingMessage") == 0)     { gl_chat_cb_poner(c->entrante, L, 3); return true; }
        if (strcmp(key, "ShouldDeliverCallback") == 0) { gl_chat_cb_poner(c->entregar, L, 3); return true; }
    }
    return false;
}

#endif // GL_PUENTE_CHAT_H

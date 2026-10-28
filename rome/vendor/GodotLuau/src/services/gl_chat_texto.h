#ifndef GL_CHAT_TEXTO_H
#define GL_CHAT_TEXTO_H

// TextChatService moderno (F5 de la v1.17): las instancias que cuelgan de él.
//
//   TextChatService
//   ├─ TextChannels / RBXGeneral, RBXSystem   (TextChannel, con un TextSource por jugador)
//   ├─ TextChatCommands / RBXHelpCommand...   (TextChatCommand)
//   ├─ ChatWindowConfiguration, ChatInputBarConfiguration,
//   │  BubbleChatConfiguration, ChannelTabsConfiguration
//   └─ ChatWindow                              (la ventana del chat, interna)
//
// Las propiedades van ligadas con el nombre de Roblox: las lee y escribe el
// puente generico (_gl_bridge). Señales, metodos y el recorrido de un mensaje
// estan en core/gl_puente_chat.h.

#include <godot_cpp/classes/node.hpp>
#include <vector>
#include "gl_runtime.h"

using namespace godot;

struct GLChatCB { lua_State* main_L = nullptr; int ref = -1; bool active = true; };

// Pone (o quita, si no es funcion) un callback de un solo dueño, como
// OnIncomingMessage: el ultimo que se asigna es el que vale.
inline void gl_chat_cb_poner(GLChatCB& cb, lua_State* L, int idx) {
    if (cb.ref >= 0 && cb.main_L && gl_state_alive(cb.main_L)) gl_unref(cb.main_L, cb.ref);
    cb = GLChatCB();
    cb.active = false;
    if (!lua_isfunction(L, idx)) return;
    lua_pushvalue(L, idx);
    cb.ref = gl_ref(L, -1);
    lua_pop(L, 1);
    cb.main_L = gl_main_of(L);
    cb.active = true;
}
inline bool gl_chat_cb_vivo(const GLChatCB& cb) {
    return cb.active && cb.ref >= 0 && gl_state_alive(cb.main_L);
}

class TextChannel : public Node {
    GDCLASS(TextChannel, Node);
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect", "ref", "vm"), &TextChannel::_gl_disconnect);
    }
public:
    std::vector<GLChatCB> recibido_cbs;   // MessageReceived
    GLChatCB entrante;                    // OnIncomingMessage
    GLChatCB entregar;                    // ShouldDeliverCallback
    TextChannel() { set_meta("_gl_bridge", true); }
    void _gl_disconnect(int ref, int64_t vm) {
        for (auto& c : recibido_cbs) if (c.ref == ref && GL_MISMA_VM(c)) c.active = false;
    }
};

class TextSource : public Node {
    GDCLASS(TextSource, Node);
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_user_id", "v"), &TextSource::set_user_id);
        ClassDB::bind_method(D_METHOD("get_user_id"), &TextSource::get_user_id);
        ClassDB::bind_method(D_METHOD("set_can_send", "v"), &TextSource::set_can_send);
        ClassDB::bind_method(D_METHOD("get_can_send"), &TextSource::get_can_send);
        ADD_PROPERTY(PropertyInfo(Variant::INT, "UserId"), "set_user_id", "get_user_id");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "CanSend"), "set_can_send", "get_can_send");
    }
public:
    int64_t user_id = 0;
    bool can_send = true;
    TextSource() { set_meta("_gl_bridge", true); }
    void set_user_id(int64_t v) { user_id = v; }
    int64_t get_user_id() const { return user_id; }
    void set_can_send(bool v) { can_send = v; }
    bool get_can_send() const { return can_send; }
};

class TextChatCommand : public Node {
    GDCLASS(TextChatCommand, Node);
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect", "ref", "vm"), &TextChatCommand::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("set_primary", "v"), &TextChatCommand::set_primary);
        ClassDB::bind_method(D_METHOD("get_primary"), &TextChatCommand::get_primary);
        ClassDB::bind_method(D_METHOD("set_secondary", "v"), &TextChatCommand::set_secondary);
        ClassDB::bind_method(D_METHOD("get_secondary"), &TextChatCommand::get_secondary);
        ClassDB::bind_method(D_METHOD("set_enabled", "v"), &TextChatCommand::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"), &TextChatCommand::get_enabled);
        ClassDB::bind_method(D_METHOD("set_autocomplete", "v"), &TextChatCommand::set_autocomplete);
        ClassDB::bind_method(D_METHOD("get_autocomplete"), &TextChatCommand::get_autocomplete);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "PrimaryAlias"), "set_primary", "get_primary");
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "SecondaryAlias"), "set_secondary", "get_secondary");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Enabled"), "set_enabled", "get_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "AutocompleteVisible"), "set_autocomplete", "get_autocomplete");
    }
public:
    String primary, secondary;
    bool enabled = true;
    bool autocomplete = true;
    std::vector<GLChatCB> disparado_cbs;   // Triggered(originTextSource, unfilteredText)
    TextChatCommand() { set_meta("_gl_bridge", true); }
    void set_primary(const String& v) { primary = v; }
    String get_primary() const { return primary; }
    void set_secondary(const String& v) { secondary = v; }
    String get_secondary() const { return secondary; }
    void set_enabled(bool v) { enabled = v; }
    bool get_enabled() const { return enabled; }
    void set_autocomplete(bool v) { autocomplete = v; }
    bool get_autocomplete() const { return autocomplete; }
    void _gl_disconnect(int ref, int64_t vm) {
        for (auto& c : disparado_cbs) if (c.ref == ref && GL_MISMA_VM(c)) c.active = false;
    }
    // ¿El mensaje empieza por uno de sus alias? (sin mayusculas, como Roblox)
    bool coincide(const String& texto) const {
        const String primera = texto.strip_edges().get_slicec(' ', 0).to_lower();
        if (primera.is_empty()) return false;
        return (!primary.is_empty() && primera == primary.strip_edges().to_lower()) ||
               (!secondary.is_empty() && primera == secondary.strip_edges().to_lower());
    }
};

// Lo que devuelve OnIncomingMessage para cambiar como se ve un mensaje.
class TextChatMessageProperties : public Node {
    GDCLASS(TextChatMessageProperties, Node);
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_text", "v"), &TextChatMessageProperties::set_text);
        ClassDB::bind_method(D_METHOD("get_text"), &TextChatMessageProperties::get_text);
        ClassDB::bind_method(D_METHOD("set_prefix", "v"), &TextChatMessageProperties::set_prefix);
        ClassDB::bind_method(D_METHOD("get_prefix"), &TextChatMessageProperties::get_prefix);
        ClassDB::bind_method(D_METHOD("set_translation", "v"), &TextChatMessageProperties::set_translation);
        ClassDB::bind_method(D_METHOD("get_translation"), &TextChatMessageProperties::get_translation);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "Text"), "set_text", "get_text");
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "PrefixText"), "set_prefix", "get_prefix");
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "Translation"), "set_translation", "get_translation");
    }
public:
    String text, prefix, translation;
    bool tiene_text = false, tiene_prefix = false;   // solo cuenta lo que el script puso
    TextChatMessageProperties() { set_meta("_gl_bridge", true); }
    void set_text(const String& v) { text = v; tiene_text = true; }
    String get_text() const { return text; }
    void set_prefix(const String& v) { prefix = v; tiene_prefix = true; }
    String get_prefix() const { return prefix; }
    void set_translation(const String& v) { translation = v; }
    String get_translation() const { return translation; }
};

// Las cuatro configuraciones. Enabled es lo unico que cambia algo aqui; el resto
// de propiedades (colores, tamaños...) se guardan y se leen tal cual.
class GLChatConfig : public Node {
    GDCLASS(GLChatConfig, Node);
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_enabled", "v"), &GLChatConfig::set_enabled);
        ClassDB::bind_method(D_METHOD("get_enabled"), &GLChatConfig::get_enabled);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Enabled"), "set_enabled", "get_enabled");
    }
    virtual void _al_cambiar() {}
public:
    bool enabled = true;
    GLChatConfig() { set_meta("_gl_bridge", true); }
    void set_enabled(bool v) {
        if (enabled == v) return;
        enabled = v;
        _al_cambiar();
    }
    bool get_enabled() const { return enabled; }
    // La ventana del chat (RobloxChat, hija interna del TextChatService).
    Node* ventana() const {
        Node* tcs = get_parent();
        return tcs ? tcs->get_node_or_null(NodePath("ChatWindow")) : nullptr;
    }
};
class ChatWindowConfiguration : public GLChatConfig {
    GDCLASS(ChatWindowConfiguration, GLChatConfig);
protected:
    static void _bind_methods() {}
    void _al_cambiar() override { if (Node* v = ventana()) v->call("gl_set_ventana_visible", enabled); }
};
class ChatInputBarConfiguration : public GLChatConfig {
    GDCLASS(ChatInputBarConfiguration, GLChatConfig);
protected:
    static void _bind_methods() {}
    void _al_cambiar() override { if (Node* v = ventana()) v->call("gl_set_barra_visible", enabled); }
};
class BubbleChatConfiguration : public GLChatConfig {
    GDCLASS(BubbleChatConfiguration, GLChatConfig);
protected:
    static void _bind_methods() {}
};
class ChannelTabsConfiguration : public GLChatConfig {
    GDCLASS(ChannelTabsConfiguration, GLChatConfig);
protected:
    static void _bind_methods() {}
public:
    ChannelTabsConfiguration() { enabled = false; }
};

#endif // GL_CHAT_TEXTO_H

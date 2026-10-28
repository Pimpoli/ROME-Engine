#ifndef ROBLOX_TEXTCHAT_H
#define ROBLOX_TEXTCHAT_H
#include "gl_cast.h"

// TextChatService. Va aparte de roblox_services.h (que pasaba de 2.000
// lineas) y se incluye desde alli, en el mismo sitio en que estaba: usa
// _enforce_game_parent y Chat, definidos antes.

// TextChatService moderno (F5 de la v1.17). Al arrancar crea lo que Roblox crea
// con CreateDefaultTextChannels y CreateDefaultCommands, las cuatro
// configuraciones y la ventana del chat (interna). El recorrido de un mensaje,
// las señales y los metodos estan en core/gl_puente_chat.h.
class TextChatService;
inline void gl_chat_desde_barra(TextChatService* tcs, const String& texto);
inline void gl_chat_desde_red(TextChatService* tcs, Node* jugador, const String& nombre, const String& texto);
inline void gl_chat_comando_de_red(TextChatService* tcs, Node* jugador, const String& texto);
inline void gl_chat_mostrar_de(TextChatService* tcs, const String& nombre, const String& texto);
inline bool gl_chat_es_comando(TextChatService* tcs, const String& texto);

class TextChatService : public Node {
    GDCLASS(TextChatService, Node);

private:
    RobloxChat* chat_window = nullptr;

    Node* _hijo(Node* padre, const char* clase, const char* nombre) {
        if (Node* ya = padre->get_node_or_null(NodePath(nombre))) return ya;
        Node* n = gl_cast<Node>(ClassDB::instantiate(StringName(clase)));
        if (!n) return nullptr;
        n->set_name(nombre);
        padre->add_child(n);
        return n;
    }
    void _crear_hijos() {
        if (Node* canales = _hijo(this, "Folder", "TextChannels")) {
            _hijo(canales, "TextChannel", "RBXGeneral");
            _hijo(canales, "TextChannel", "RBXSystem");
        }
        if (Node* cmds = _hijo(this, "Folder", "TextChatCommands")) {
            static const char* k[][3] = {
                { "RBXClearCommand", "/clear", "/cls" },   { "RBXConsoleCommand", "/console", "" },
                { "RBXEmoteCommand", "/emote", "/e" },     { "RBXHelpCommand", "/help", "/?" },
                { "RBXMuteCommand", "/mute", "/m" },       { "RBXTeamCommand", "/team", "/t" },
                { "RBXUnmuteCommand", "/unmute", "/um" },  { "RBXVersionCommand", "/version", "/v" },
                { "RBXWhisperCommand", "/whisper", "/w" },
            };
            for (auto& e : k) {
                if (cmds->get_node_or_null(NodePath(e[0]))) continue;
                TextChatCommand* c = memnew(TextChatCommand);
                c->set_name(e[0]);
                c->primary = e[1];
                c->secondary = e[2];
                c->set_meta("_gl_rbx_cmd", true);
                cmds->add_child(c);
            }
        }
        _hijo(this, "ChatWindowConfiguration", "ChatWindowConfiguration");
        _hijo(this, "ChatInputBarConfiguration", "ChatInputBarConfiguration");
        _hijo(this, "BubbleChatConfiguration", "BubbleChatConfiguration");
        _hijo(this, "ChannelTabsConfiguration", "ChannelTabsConfiguration");
    }

protected:
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_ENTER_TREE) _enforce_game_parent(this);
    }
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("send_message", "player", "message"), &TextChatService::send_message_from_luau);
        ClassDB::bind_method(D_METHOD("set_player_name", "name"), &TextChatService::set_player_name);
        ClassDB::bind_method(D_METHOD("gl_enviar_barra", "text"), &TextChatService::gl_enviar_barra);
        ClassDB::bind_method(D_METHOD("gl_recibir_red", "player", "name", "text"), &TextChatService::gl_recibir_red);
        ClassDB::bind_method(D_METHOD("gl_comando_red", "player", "text"), &TextChatService::gl_comando_red);
        ClassDB::bind_method(D_METHOD("gl_es_comando", "text"), &TextChatService::gl_es_comando);
        ClassDB::bind_method(D_METHOD("_gl_disconnect", "ref", "vm"), &TextChatService::_gl_disconnect);
    }

public:
    std::vector<GLChatCB> recibido_cbs;   // MessageReceived(TextChatMessage)
    std::vector<GLChatCB> enviando_cbs;   // SendingMessage(TextChatMessage)
    GLChatCB entrante;                    // OnIncomingMessage
    GLChatCB burbuja_cb, ventana_cb;      // OnBubbleAdded / OnChatWindowAdded (se guardan)

    void _ready() override {
        if (Engine::get_singleton()->is_editor_hint()) return;
        // La ventana es interna: GetChildren del servicio no la ve.
        chat_window = memnew(Chat);
        chat_window->set_name("ChatWindow");
        add_child(chat_window, false, Node::INTERNAL_MODE_BACK);
        _crear_hijos();
        GL_DEBUG_PRINT("[GodotLuau] TextChatService ready. Press '/' or 'Enter' to open the chat.");
    }

    // SendMessage(nombre, texto) de la API vieja de GodotLuau.
    void send_message_from_luau(String player, String message) { gl_chat_mostrar_de(this, player, message); }
    void set_player_name(String name) { if (chat_window) chat_window->set_player_name(name); }
    RobloxChat* get_chat() const { return chat_window; }

    // La barra de la ventana (RobloxChat) y la red entran por aqui.
    void gl_enviar_barra(String texto) { gl_chat_desde_barra(this, texto); }
    void gl_recibir_red(Node* jugador, String nombre, String texto) { gl_chat_desde_red(this, jugador, nombre, texto); }
    void gl_comando_red(Node* jugador, String texto) { gl_chat_comando_de_red(this, jugador, texto); }
    bool gl_es_comando(String texto) { return gl_chat_es_comando(this, texto); }

    void _gl_disconnect(int ref, int64_t vm) {
        for (auto& c : recibido_cbs) if (c.ref == ref && GL_MISMA_VM(c)) c.active = false;
        for (auto& c : enviando_cbs) if (c.ref == ref && GL_MISMA_VM(c)) c.active = false;
    }
};

#endif // ROBLOX_TEXTCHAT_H

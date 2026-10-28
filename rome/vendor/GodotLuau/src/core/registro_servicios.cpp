#include "gl_cabeceras.h"

// Registro de clases (4/11): servicios y valores. Ver el final de gl_cabeceras.h.
void gl_registro_servicios() {
    // Services equivalent to Roblox
    //// Servicios equivalentes a Roblox
    ClassDB::register_class<ReplicatedStorage>();
    ClassDB::register_class<ServerStorage>();
    ClassDB::register_class<ReplicatedFirst>();
    ClassDB::register_class<ServerScriptService>();
    ClassDB::register_class<StarterCharacterScripts>();
    ClassDB::register_class<StarterPlayerScripts>();
    ClassDB::register_class<StarterPlayer>();
    ClassDB::register_class<StarterGui>();
    ClassDB::register_class<StarterPack>();
    ClassDB::register_class<Players>();
    ClassDB::register_class<PlayerObject>();   // objeto "Player" (Player ≠ Character)
    ClassDB::register_class<PlayerGui>();      // contenedores por jugador (1.14.10)
    ClassDB::register_class<PlayerScripts>();
    ClassDB::register_internal_class<RobloxMouse>();  ClassDB::register_class<Mouse>();    // Player:GetMouse() (1.14.10 Finish)
    ClassDB::register_internal_class<RobloxValue>();
    ClassDB::register_class<NumberValue>();
    ClassDB::register_class<IntValue>();
    ClassDB::register_class<StringValue>();
    ClassDB::register_class<BoolValue>();
    ClassDB::register_class<ObjectValue>();
    ClassDB::register_class<Vector3Value>();
    ClassDB::register_class<CFrameValue>();
    ClassDB::register_class<Color3Value>();
    ClassDB::register_class<BrickColorValue>();
    ClassDB::register_class<RayValue>();   // NumberValue/IntValue/StringValue/…
    ClassDB::register_class<Lighting>();
    ClassDB::register_class<MaterialService>();
    ClassDB::register_class<NetworkClient>();
    ClassDB::register_class<Teams>();
    ClassDB::register_class<Team>();   // 1.14.10 (el cliente lo instancia por nombre al replicar)
    ClassDB::register_class<Folder>();
    ClassDB::register_class<RemoteEventNode>();
    ClassDB::register_class<UnreliableRemoteEventNode>();
    ClassDB::register_class<RemoteFunctionNode>();
    ClassDB::register_class<BindableEventNode>();
    ClassDB::register_class<SoundService>();
    ClassDB::register_class<RunService>();
    ClassDB::register_class<TextChatService>();
    // TextChatService moderno (gl_chat_texto.h)
    ClassDB::register_class<TextChannel>();
    ClassDB::register_class<TextSource>();
    ClassDB::register_class<TextChatCommand>();
    ClassDB::register_class<TextChatMessageProperties>();
    ClassDB::register_internal_class<GLChatConfig>();
    ClassDB::register_class<ChatWindowConfiguration>();
    ClassDB::register_class<ChatInputBarConfiguration>();
    ClassDB::register_class<BubbleChatConfiguration>();
    ClassDB::register_class<ChannelTabsConfiguration>();
    ClassDB::register_internal_class<GLTiendaNodo>();   // ventana de compra (gl_tienda.h)
}

// Registro de clases (5/11): red y vistas del editor. Ver el final de gl_cabeceras.h.
void gl_registro_red() {
    ClassDB::register_class<NetworkService>();
    ClassDB::register_class<GLFreeCamera>();    // cámara libre de la Vista de Servidor
    ClassDB::register_class<GLVRPreview>();     // vista previa VR (dos ojos)
}

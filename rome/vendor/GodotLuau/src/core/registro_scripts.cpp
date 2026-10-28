// Registro de clases (1/11): scripts. Ver el final de gl_cabeceras.h.
#include "gl_cabeceras.h"

void gl_registro_scripts() {
    // GLAssetFetcher hereda de Node: registrarlo en SERVERS (donde Node aun
    // no existe) dejaba la clase sin registrar y cada pedir() fabricaba un
    // objeto corrupto — el juego moria con SIGSEGV al vaciar los deferred.
    ClassDB::register_class<GLAssetFetcher>();
    // Analizador de compatibilidad (GodotLuau IA, Capa 0). Es RefCounted y
    // no toca la escena: se registra aqui para que el panel del editor
    // pueda instanciarlo con ClassDB.instantiate("GLCompat").
    ClassDB::register_class<GLCompat>();

    // Scripts (color in editor: Blue / Naranja / Purple)
    //// Scripts (color en el editor: Azul / Naranja / Morado)
    ClassDB::register_class<ScriptNodeBase>();
    ClassDB::register_class<LocalScript>();   // Blue / Azul
    ClassDB::register_class<ServerScript>();  // Orange / Naranja
    ClassDB::register_class<ModuleScript>();  // Purple / Morado
}

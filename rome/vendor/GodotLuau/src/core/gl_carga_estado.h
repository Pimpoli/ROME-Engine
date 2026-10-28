#ifndef GL_CARGA_ESTADO_H
#define GL_CARGA_ESTADO_H

// Lo que comparten la pantalla de carga por defecto (ui/gl_carga_defecto.h), el
// Workspace que la crea y ReplicatedFirst:RemoveDefaultLoadingScreen() del
// puente. Va aparte porque esos tres se incluyen antes que la pantalla.

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/object.hpp>

using namespace godot;

// La pantalla viva (0 si no hay) y si el juego ya pidio quitarla.
inline uint64_t& gl_carga_defecto_id() { static uint64_t id = 0; return id; }
inline bool& gl_carga_quitada() { static bool q = false; return q; }

// La crea el Workspace al entrar en juego; definida en ui/gl_carga_defecto.h.
inline void gl_carga_defecto_crear(Node* workspace);

// RemoveDefaultLoadingScreen: fuera en el acto, como en Roblox. Si el juego lo
// pide antes de que salga, ya no sale.
inline void gl_carga_defecto_quitar() {
    gl_carga_quitada() = true;
    if (CanvasLayer* c = Object::cast_to<CanvasLayer>(ObjectDB::get_instance(ObjectID(gl_carga_defecto_id())))) {
        c->set_visible(false);
        c->queue_free();
    }
    gl_carga_defecto_id() = 0;
}

#endif // GL_CARGA_ESTADO_H

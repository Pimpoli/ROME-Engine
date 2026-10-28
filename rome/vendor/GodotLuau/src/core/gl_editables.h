#ifndef GL_EDITABLES_H
#define GL_EDITABLES_H

// ════════════════════════════════════════════════════════════════════
//  El gestor de EditableImage / EditableMesh: sube a la GPU lo que se
//  pinto o edito, UNA vez por frame como mucho.
//
//  Pintar o editar solo apunta el objeto aqui y programa un vaciado en
//  diferido (la cola de mensajes de Godot se vacia al final del frame,
//  antes de dibujar). Asi mil DrawRectangle seguidos son una sola subida de
//  la textura, y mil SetPosition una sola reconstruccion de la malla.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>

#include "gl_editable_imagen.h"
#include "gl_editable_malla.h"

#include <vector>

class GLEditablesGestor : public Object {
    GDCLASS(GLEditablesGestor, Object);

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("gl_vaciar"), &GLEditablesGestor::gl_vaciar);
    }

public:
    std::vector<Ref<GLEditableImage>> imagenes;
    std::vector<Ref<GLEditableMesh>> mallas;
    bool pendiente = false;

    void programar() {
        if (pendiente) return;
        pendiente = true;
        call_deferred("gl_vaciar");
    }
    void gl_vaciar() {
        pendiente = false;
        std::vector<Ref<GLEditableImage>> im;
        std::vector<Ref<GLEditableMesh>> ma;
        im.swap(imagenes);
        ma.swap(mallas);
        for (const Ref<GLEditableImage>& e : im) { e->apuntada = false; e->subir(); }
        for (const Ref<GLEditableMesh>& e : ma) { e->apuntada = false; e->subir(); }
    }
};

inline GLEditablesGestor*& gl_editables_gestor_ptr() {
    static GLEditablesGestor* g = nullptr;
    return g;
}
inline GLEditablesGestor* gl_editables_gestor() {
    GLEditablesGestor*& g = gl_editables_gestor_ptr();
    if (!g) g = memnew(GLEditablesGestor);
    return g;
}
inline void gl_editables_apuntar_imagen(GLEditableImage* ei) {
    GLEditablesGestor* g = gl_editables_gestor();
    g->imagenes.push_back(Ref<GLEditableImage>(ei));
    ei->apuntada = true;
    g->programar();
}
inline void gl_editables_apuntar_malla(GLEditableMesh* em) {
    GLEditablesGestor* g = gl_editables_gestor();
    g->mallas.push_back(Ref<GLEditableMesh>(em));
    em->apuntada = true;
    g->programar();
}
// Al descargar la extension, mientras el RenderingServer sigue vivo.
inline void gl_editables_liberar() {
    GLEditablesGestor*& g = gl_editables_gestor_ptr();
    if (!g) return;
    g->imagenes.clear();
    g->mallas.clear();
    memdelete(g);
    g = nullptr;
}

#endif // GL_EDITABLES_H

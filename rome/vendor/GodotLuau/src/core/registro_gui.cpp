#include "gl_cabeceras.h"

// Registro de clases (6/11): BodyMovers y constraints. Ver el final de gl_cabeceras.h.
void gl_registro_fisica() {
    // BodyMovers — equivalent to Roblox's BodyMovers
    //// BodyMovers — equivalentes a los BodyMovers de Roblox
    ClassDB::register_class<BodyVelocity>();
    ClassDB::register_class<BodyPosition>();
    ClassDB::register_class<BodyForce>();
    ClassDB::register_class<BodyAngularVelocity>();
    ClassDB::register_class<BodyGyro>();

    // Constraints — joints and physical restrictions
    //// Constraints — juntas y restricciones físicas
    // Base comun de los constraints que tienen tick. Se registra como
    // interna: no aparece en el menu de crear nodos, pero ClassDB la
    // necesita para que cast_to funcione en el tick central.
    ClassDB::register_abstract_class<GLConstraintBase>();
    ClassDB::register_class<WeldConstraint>();
    ClassDB::register_class<Weld>();
    ClassDB::register_class<HingeConstraint>();
    ClassDB::register_class<BallSocketConstraint>();
    ClassDB::register_class<RodConstraint>();
    ClassDB::register_class<RopeConstraint>();
    ClassDB::register_class<SpringConstraint>();
    ClassDB::register_class<Attachment>();
}

// Registro de clases (7/11): GUI con texto. Ver el final de gl_cabeceras.h.
void gl_registro_gui_texto() {
    // Roblox-style GUI — ScreenGui, Frame, TextLabel, etc.
    //// GUI tipo Roblox — ScreenGui, Frame, TextLabel, etc.
    ClassDB::register_class<ScreenGui>();
    ClassDB::register_internal_class<RobloxFrame>();  ClassDB::register_class<Frame>();
    ClassDB::register_internal_class<RobloxTextLabel>();  ClassDB::register_class<TextLabel>();
    ClassDB::register_internal_class<RobloxTextButton>();  ClassDB::register_class<TextButton>();
    ClassDB::register_internal_class<RobloxTextBox>();  ClassDB::register_class<TextBox>();
}

// Registro de clases (8/11): GUI con imagen y scroll. Ver el final de gl_cabeceras.h.
void gl_registro_gui_imagen() {
    ClassDB::register_internal_class<RobloxImageLabel>();  ClassDB::register_class<ImageLabel>();
    ClassDB::register_internal_class<RobloxImageButton>();  ClassDB::register_class<ImageButton>();
    ClassDB::register_internal_class<RobloxScrollingFrame>();  ClassDB::register_class<ScrollingFrame>();
}

// Registro de clases (9/11): efectos de iluminación y nodos raíz. Ver el final
// de gl_cabeceras.h. Vive aqui y no con el resto del juego para repartir el
// peso: registro_juego.cpp era el TU mas lento.
void gl_registro_efectos() {
    // Lighting FX nodes
    //// Nodos de efectos de iluminación
    ClassDB::register_class<AtmosphereNode>();
    ClassDB::register_class<LightingSkyNode>();
    ClassDB::register_class<SunRaysNode>();
    // El bloom propio de BloomEffect (CompositorEffect, solo en Forward+).
    ClassDB::register_internal_class<GLPostFX>();
    // La sonda de luz (gl_sonda.h) y las sombras del sol propias (gl_sombras.h).
    ClassDB::register_internal_class<GLSonda>();
    ClassDB::register_internal_class<GLSombraCopia>();
    ClassDB::register_internal_class<GLSombraPantalla>();
    ClassDB::register_internal_class<GLSombras>();
    // La pantalla de carga por defecto (gl_carga_defecto.h).
    ClassDB::register_internal_class<GLCargaFondo>();
    ClassDB::register_internal_class<GLCargaAtras>();
    ClassDB::register_internal_class<GLCargaHalos>();
    ClassDB::register_internal_class<GLCargaDibujo>();
    ClassDB::register_internal_class<GLCargaAtlas>();
    ClassDB::register_internal_class<GLCargaTexto>();
    ClassDB::register_internal_class<GLCargaPantalla>();
    ClassDB::register_internal_class<GLCargaFrente>();
    ClassDB::register_internal_class<GLCargaDefecto>();
    ClassDB::register_class<BloomEffect>();
    ClassDB::register_class<BlurEffect>();
    ClassDB::register_class<ColorCorrectionEffect>();
    ClassDB::register_class<DepthOfFieldEffect>();

    // RobloxTemplate (nodo raíz generador de la estructura Roblox)
    ClassDB::register_class<RobloxTemplate>();

    // New root nodes: choose 3D or 2D when creating the scene
    //// Nuevos nodos raíz: selecciona 3D o 2D al crear la escena
    ClassDB::register_internal_class<RobloxGame3D>();  ClassDB::register_class<Game3D>();
    ClassDB::register_internal_class<RobloxGame2D>();  ClassDB::register_class<Game2D>();
}

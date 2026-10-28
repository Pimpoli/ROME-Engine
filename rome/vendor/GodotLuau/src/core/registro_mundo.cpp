#include "gl_cabeceras.h"

// Registro de clases (2/11): personaje y mundo 3D. Ver el final de gl_cabeceras.h.
void gl_registro_mundo() {
    // 3D characters and physics
    //// Personajes y físicas 3D
    ClassDB::register_class<Humanoid>();
    ClassDB::register_internal_class<RobloxWorkspace>();  ClassDB::register_class<Workspace>();
    ClassDB::register_internal_class<RobloxPlayer>();  ClassDB::register_class<Character>();
    // RobloxPart INTERNA (oculta del "Add Node", pero cargable: las escenas
    // viejas type="RobloxPart" siguen abriendo). El usuario usa "Part" (1.15).
    ClassDB::register_internal_class<RobloxPart>();
    ClassDB::register_class<Part>();
    ClassDB::register_internal_class<RobloxTerrain>();  ClassDB::register_class<Terrain>();   // Workspace.Terrain

    // Efectos del mundo (1.17): luces, cintas y Highlight de Roblox.
    ClassDB::register_class<PointLight>();
    ClassDB::register_class<SpotLight>();
    ClassDB::register_class<SurfaceLight>();
    ClassDB::register_internal_class<GLCinta>();
    ClassDB::register_class<Beam>();
    ClassDB::register_class<Trail>();
    ClassDB::register_class<Highlight>();
}

// Registro de clases (3/11): importador .rbxl, mundo 2D y chat. Ver el final
// de gl_cabeceras.h.
void gl_registro_importador() {
    // Importador de .rbxl (1.16): clases que existen en Roblox pero no
    // tenian equivalente aqui. RBXInstance es el comodin que evita perder
    // instancias de clases todavia no soportadas.
    ClassDB::register_class<RBXInstance>();
    ClassDB::register_class<RBXModel>();
    ClassDB::register_class<RBXMeshPart>();
    ClassDB::register_class<RBXTexture>();
    ClassDB::register_class<RBXDecal>();
    ClassDB::register_class<RBXMesh>();
    ClassDB::register_class<RBXBone>();
    ClassDB::register_class<RBXImporter>();

    // 2D characters and physics
    //// Personajes y físicas 2D
    ClassDB::register_class<Humanoid2D>();
    ClassDB::register_internal_class<RobloxWorkspace2D>();  ClassDB::register_class<Workspace2D>();
    ClassDB::register_internal_class<RobloxPlayer2D>();  ClassDB::register_class<Character2D>();

    // Chat
    //// Chat
    ClassDB::register_internal_class<RobloxChat>();  ClassDB::register_class<Chat>();
}

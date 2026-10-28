#ifndef GL_CABECERAS_H
#define GL_CABECERAS_H

// Todos los headers de la extension, en un orden fijo. Lo incluye cada .cpp de
// src/core/ (register_types.cpp y los registro_*.cpp).
//
// Que TODOS los TU incluyan lo mismo y en el mismo orden importa: el codigo
// vive en headers con funciones `inline`, y C++ exige que una funcion inline
// sea identica en todos los TU. Un header incluido en otro orden puede cambiar
// que sobrecarga ve una funcion (roblox_input.h va antes de push_vector3, por
// ejemplo) y el enlazador se quedaria en silencio con una de las dos.

#include "register_types.h"
#include "gl_asset_fetch.h"
#include "gl_debug.h"
#include "luau_script.h"
#include "luau_language.h"
#include "roblox_workspace.h"
#include "roblox_workspace2d.h"
#include "roblox_player.h"
#include "roblox_player2d.h"
#include "roblox_part.h"
#include "roblox_terrain.h"
#include "humanoid.h"
#include "humanoid2d.h"
#include "folder.h"
#include "roblox_chat.h"
#include "roblox_services.h"
#include "roblox_network.h"
#include "gl_avatar.h"
#include "roblox_datamodel.h"
#include "roblox_game3d.h"
#include "roblox_game2d.h"
#include "roblox_remote.h"
#include "roblox_lighting_fx.h"
#include "gl_sonda.h"            // la sonda de luz (luz propia de las piezas y sombras)
#include "gl_sombras.h"          // sombras del sol propias (mapa guardado + pantalla)
#include "roblox_bodymovers.h"
#include "roblox_constraints.h"
#include "roblox_attachment.h"
#include "roblox_extra.h"
#include "roblox_behavior.h"
#include "roblox_effects.h"
#include "roblox_luces.h"      // PointLight / SpotLight / SurfaceLight
#include "roblox_cintas.h"     // Beam / Trail
#include "roblox_highlight.h"
#include "roblox_gui.h"
#include "roblox_tween.h"
#include "roblox_sound.h"
#include "roblox_interactive.h"
#include "roblox_billboard.h"
#include "roblox_input.h"
#include "roblox_animation.h"
#include "rbx_instances.h"
#include "rbx_importer.h"
#include "gl_compat.h"   // Capa 0 de GodotLuau IA: analizador de compatibilidad
#include "gl_carga_defecto.h"   // pantalla de carga por defecto (usa la cola de arranque y las fuentes)

// El plugin del importador NO va tras #ifdef TOOLS_ENABLED: el proyecto se
// compila con target=template_debug y esa es la libreria que carga el editor,
// asi que el guard lo dejaria fuera y el menu nunca apareceria. Registrarlo es
// inofensivo en runtime porque el nivel EDITOR solo se alcanza en el editor.
#include "rbx_import_plugin.h"
#include <godot_cpp/classes/editor_plugin_registration.hpp>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
// Los nombres que ve el usuario, sin "Roblox" delante (Frame, TextButton,
// Sound...). Va el ULTIMO: son subclases finas de todo lo de arriba.
#include "gl_nombres_limpios.h"

using namespace godot;

// El registro de clases esta repartido en varios .cpp (registro_*.cpp) para
// que se compilen a la vez: en un solo archivo eran ~60 s de un solo hilo.
// Ojo, cada .cpp de mas cuesta ~8 s fijos (parsear todo esto y los ~200
// registros de clases de Godot que godot-cpp mete en cada TU): partir mas no
// siempre baja el total. Medido con debug + release a la vez: 1 TU 73 s,
// 13 TU 55 s, 6 TU 39 s.
// register_types.cpp los llama EN ESTE ORDEN, que es el orden de registro de
// siempre (una clase hija necesita a su padre ya registrado).
void gl_registro_scripts();
void gl_registro_mundo();
void gl_registro_importador();
void gl_registro_servicios();
void gl_registro_red();
void gl_registro_fisica();
void gl_registro_gui_texto();
void gl_registro_gui_imagen();
void gl_registro_efectos();
void gl_registro_juego();
void gl_registro_extra();

#endif

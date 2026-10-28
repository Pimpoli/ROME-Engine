#ifndef LUAU_STDLIB_H
#define LUAU_STDLIB_H

// ════════════════════════════════════════════════════════════════════
//  GodotLuau stdlib — services implemented in pure Lua.
//  Runs at the start of each ScriptNodeBase before the user's script.
//  Services are stored in _G["__SVC_ServiceName"] so that
//  game:GetService() can find them without a C++ node.
////
//  GodotLuau stdlib — servicios implementados en Lua puro.
//  Se ejecuta al inicio de cada ScriptNodeBase antes del script del usuario.
//  Los servicios se guardan en _G["__SVC_NombreServicio"] para que
//  game:GetService() los encuentre sin necesidad de un nodo C++.
// ════════════════════════════════════════════════════════════════════

// Array inline y no `static const char*`: con varios TU, static metia una copia
// del texto en cada TU que lo usara. Asi queda una sola en la DLL.
inline constexpr char LUAU_STDLIB_CODE[] =
#include "luau_stdlib_base.h"        // senales, Enum, consultas, TweenService
#include "luau_stdlib_servicios.h"   // servicios en Luau (__SVC_*)
#include "luau_stdlib_tipos.h"       // Vector2, UDim2, CFrame, BrickColor...
#include "luau_stdlib_ola1.h"        // Ola 1: lo que faltaba
#include "luau_stdlib_editables.h"   // Content y AssetService (EditableImage/Mesh)
;

#endif // LUAU_STDLIB_H

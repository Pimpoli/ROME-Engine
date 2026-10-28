"""
GodotLuau — Crear nuevo proyecto
=================================
Crea un proyecto de Godot 4 listo para usar con la arquitectura
completa de Roblox: servicios, movimiento y chat ya configurados.

Uso:  python nuevo_proyecto.py
Autor: PimpoliDev

FUNCIONES:
  - Elige entre juego 2D o 3D
  - Crea toda la estructura de carpetas
  - Copia la DLL compilada y los iconos
  - Genera la escena principal con todos los servicios
  - Incluye el script de movimiento correspondiente (Luau)
"""

import os
import sys
import shutil
import random
import string

# Este script vive en tools/. RAIZ es la raiz del repo.
RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ORIGEN es de donde se copian bin/, DefaultScripts/, icons/ y assets/. En su
# dia estaban en la raiz del repo, pero se movieron todos a GodotLuau/ (la
# carpeta del addon) y esto se quedo apuntando a la raiz: el creador de
# proyectos fallaba en seco con "No se encontro ninguna libreria compilada en
# bin/" y no se podia crear NI UN proyecto nuevo. Se detecta la disposicion en
# vez de fijarla, asi que las dos siguen valiendo.
ORIGEN = os.path.join(RAIZ, "GodotLuau")
if not os.path.isdir(os.path.join(ORIGEN, "bin")):
    ORIGEN = RAIZ

# addons/ (el panel de config y el actualizador) SI vive en la raiz del repo.
ORIGEN_ADDONS = RAIZ if os.path.isdir(os.path.join(RAIZ, "addons")) else ORIGEN

# ─── Banner ────────────────────────────────────────────────────────
BANNER = """
══════════════════════════════════════════════════════
  GodotLuau — by PimpoliDev
  Sistema Roblox para Godot
══════════════════════════════════════════════════════
"""

# ─── Generador de UID válido para Godot 4 ──────────────────────────
def uid_godot():
    chars = string.digits + string.ascii_lowercase
    return "uid://" + "".join(random.choices(chars, k=13))

# ─── Plantillas de archivos ────────────────────────────────────────

def project_godot(nombre):
    return f"""; Configuración del proyecto Godot 4
; Generado por GodotLuau — PimpoliDev

config_version=5

[application]

config/name="{nombre}"
config/description="Proyecto creado con GodotLuau"
run/main_scene="res://Escenas/RobloxGame.tscn"
config/features=PackedStringArray("4.6", "Forward Plus")
; El icono de la VENTANA (y el de la barra de tareas, y el de la ficha del
; proyecto en el gestor de Godot): el de GodotLuau mientras el logo sea el de
; fabrica. Al elegir un logo propio en "GodotLuau Config" > Marca, pasa a ser
; ese logo.
config/icon="res://GodotLuau/icons/GodotLuauIcono.svg"

; Pantalla de arranque de Godot: el logo de GodotLuau sobre negro (la pantalla
; de carga de GodotLuau empieza en negro justo despues). Es un PNG porque el
; arranque de Godot solo lee PNG.
boot_splash/image="res://GodotLuau/icons/GodotLuauSplash.png"
boot_splash/fullsize=false
boot_splash/use_filter=true
boot_splash/bg_color=Color(0, 0, 0, 1)

[editor_plugins]

enabled=PackedStringArray("res://addons/GodotLuauUpdater/plugin.cfg")
"""

def gdextension(libs):
    """Genera el .gdextension solo con las librerías que realmente existen"""
    plataformas = {".dll": "windows", ".so": "linux", ".dylib": "macos"}
    lineas = [
        "[configuration]",
        'entry_symbol = "luau_extension_init"',
        'compatibility_minimum = "4.3"',
        "",
        "[libraries]",
    ]
    for lib in libs:
        ext = os.path.splitext(lib)[1]
        plat = plataformas.get(ext)
        if not plat:
            continue
        target = "debug" if "template_debug" in lib else "release"
        lineas.append(f'{plat}.{target}.x86_64 = "{lib}"')
    return "\n".join(lineas) + "\n"

def scene_3d(uid):
    # Game3D crea AUTOMÁTICAMENTE toda la jerarquía al abrir en el editor
    return f"""[gd_scene format=3 uid="{uid}"]

[node name="Game" type="Game3D"]
"""

def scene_2d(uid):
    # Game2D crea AUTOMÁTICAMENTE toda la jerarquía al abrir en el editor
    return f"""[gd_scene format=3 uid="{uid}"]

[node name="Game" type="Game2D"]
"""

def icon_svg():
    """Icono minimalista de GodotLuau"""
    return """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <rect width="64" height="64" rx="12" fill="#1a1a2e"/>
  <text x="32" y="44" font-family="monospace" font-size="28" font-weight="bold"
        text-anchor="middle" fill="#4fc3f7">GL</text>
  <text x="32" y="56" font-family="monospace" font-size="8"
        text-anchor="middle" fill="#546e7a">GodotLuau</text>
</svg>"""

def script_controller_3d():
    """Contenido del script de movimiento 3D para StarterPlayerScripts"""
    script_path = os.path.join(ORIGEN, "DefaultScripts", "PlayerController3D.lua")
    if os.path.exists(script_path):
        with open(script_path, "r", encoding="utf-8") as f:
            return f.read()
    # Fallback básico si no existe el archivo
    return """-- > GodotLuau — PimpoliDev
-- PlayerController3D — Sistema de movimiento 3D

-- MODO DE CAMARA: 1=Fija, 2=Suave, 3=Combinada
local CAMERA_MODE = 1

local Players = game:GetService("Players")
local player  = Players.LocalPlayer
if not player then return end

local humanoid = player:FindFirstChild("Humanoid")
if humanoid then
    humanoid.WalkSpeed = 16
    humanoid.JumpPower = 20
end

player.CameraMode = CAMERA_MODE
print("[GodotLuau] PlayerController3D listo.")
"""

def script_controller_2d():
    """Contenido del script de movimiento 2D para StarterPlayerScripts"""
    script_path = os.path.join(ORIGEN, "DefaultScripts", "PlayerController2D.lua")
    if os.path.exists(script_path):
        with open(script_path, "r", encoding="utf-8") as f:
            return f.read()
    return """-- > GodotLuau — PimpoliDev
-- PlayerController2D — Sistema de movimiento 2D

-- TIPO DE MOVIMIENTO: 0=Platformer, 1=TopDown
local MOVEMENT_TYPE = 0

-- MODO DE CAMARA: 1=Fija, 2=Suave, 3=Combinada
local CAMERA_MODE = 1

local Players = game:GetService("Players")
local player  = Players.LocalPlayer
if not player then return end

player.MovementType = MOVEMENT_TYPE
player.CameraMode   = CAMERA_MODE
print("[GodotLuau] PlayerController2D listo.")
"""

# ─── Lógica principal ───────────────────────────────────────────────

def elegir_opcion(pregunta, opciones):
    """Muestra un menú numerado y devuelve el índice elegido (0-based)"""
    while True:
        print(f"\n{pregunta}")
        for i, opt in enumerate(opciones, 1):
            print(f"  {i}. {opt}")
        resp = input("  Tu elección: ").strip()
        if resp.isdigit() and 1 <= int(resp) <= len(opciones):
            return int(resp) - 1
        print("  ⚠  Opción no válida. Intenta de nuevo.")

def crear_proyecto(destino, nombre, modo):
    is_2d = (modo == 1)
    modo_str = "2D" if is_2d else "3D"
    print(f"\n  Creando proyecto '{nombre}' ({modo_str}) en:")
    print(f"  {destino}\n")

    # 1. Estructura de carpetas
    #
    # TODO lo de la extension cuelga de GodotLuau/. Lo unico que queda fuera es
    # lo que es DEL USUARIO (Escenas/, Assets/) y addons/, que Godot obliga a
    # tener en res://addons/ para descubrir el plugin de editor.
    #
    # Esto estaba desincronizado: se creaban bin/ e icons/ en la raiz, que es la
    # disposicion de la 1.16 — precisamente las carpetas que _migrate_old_layout
    # del updater BORRA al migrar. O sea que el creador de proyectos fabricaba
    # proyectos con la estructura vieja.
    #
    # GodotLuau/LocalScripts, /ServerScripts y /ModuleScripts es donde la
    # extension guarda los .lua del editor (ver luau_script.h).
    carpetas = [
        "GodotLuau/bin", "GodotLuau/icons",
        "GodotLuau/LocalScripts", "GodotLuau/ServerScripts", "GodotLuau/ModuleScripts",
        "Escenas",
        "Assets/Sounds", "Assets/Textures",
    ]
    for carpeta in carpetas:
        os.makedirs(os.path.join(destino, carpeta), exist_ok=True)
        print(f"  📁 {carpeta}/")

    # 2. Copiar librería(s) compiladas
    bin_origen = os.path.join(ORIGEN, "bin")
    libs_copiadas = []
    if os.path.isdir(bin_origen):
        for f in os.listdir(bin_origen):
            if f.endswith((".dll", ".so", ".dylib")):
                shutil.copy2(os.path.join(bin_origen, f),
                             os.path.join(destino, "GodotLuau", "bin", f))
                print(f"  ✔ GodotLuau/bin/{f}")
                libs_copiadas.append(f"res://GodotLuau/bin/{f}")

    if not libs_copiadas:
        print("\n  ERROR: No se encontró ninguna librería compilada en bin/")
        print("  Por favor compila el proyecto con: scons")
        sys.exit(1)

    # 3. Copiar iconos — TODOS, no solo los .svg
    # Antes se copiaban solo los .svg y un logo de fabrica en PNG se quedaba
    # fuera: el menu lo pedia por ruta y no existia, asi que en un proyecto
    # nuevo salia sin logo. Los iconos van a res://GodotLuau/icons/ porque esa
    # es la ruta que usan las plantillas .luau.
    icons_origen = os.path.join(ORIGEN, "icons")
    icons_destino = os.path.join(destino, "GodotLuau", "icons")
    count_icons = 0
    if os.path.isdir(icons_origen):
        os.makedirs(icons_destino, exist_ok=True)
        for f in os.listdir(icons_origen):
            if f.lower().endswith((".svg", ".png", ".jpg", ".jpeg", ".webp")):
                shutil.copy2(os.path.join(icons_origen, f),
                             os.path.join(icons_destino, f))
                count_icons += 1
        # MenuLogo.svg es la ruta FIJA que usa el menu. Nace como copia de la
        # imagen de fabrica (el logo de Rocket Meteor, vectorial, 2 KB); el
        # panel "GodotLuau Config" la sobreescribe cuando el usuario elige otra
        # (y la envuelve tambien en SVG). RocketMeteor.svg se queda intacta al
        # lado para poder volver a la de fabrica.
        factory = os.path.join(icons_origen, "RocketMeteor.svg")
        if os.path.exists(factory):
            shutil.copy2(factory, os.path.join(icons_destino, "MenuLogo.svg"))
            print("  ✔ GodotLuau/icons/MenuLogo.svg (logo por defecto)")
        if count_icons > 0:
            print(f"  ✔ GodotLuau/icons/ ({count_icons} iconos, logo incluido)")

    # 4. Crear icono del proyecto
    with open(os.path.join(destino, "GodotLuau", "icons", "icon.svg"), "w", encoding="utf-8") as f:
        f.write(icon_svg())
    print("  ✔ GodotLuau/icons/icon.svg")

    # 5. Copiar el addon (panel de config + updater) y el archivo Version
    addon_origen = os.path.join(ORIGEN_ADDONS, "addons", "GodotLuauUpdater")
    if os.path.isdir(addon_origen):
        shutil.copytree(addon_origen,
                        os.path.join(destino, "addons", "GodotLuauUpdater"),
                        dirs_exist_ok=True)
        print("  ✔ addons/GodotLuauUpdater/")
    # Avatares por defecto (personaje R15/R6)
    # Los .obj del rig R6 se piden por res://GodotLuau/assets/avatars/r6/..., que
    # es donde el codigo los busca. Aqui se copiaban a res://assets/avatars/, o
    # sea a OTRA ruta: no se encontraban, el personaje por defecto no se podia
    # armar y salia un muñeco de una sola malla en vez del R6 de seis piezas.
    assets_origen = os.path.join(ORIGEN, "assets")
    if os.path.isdir(assets_origen):
        shutil.copytree(assets_origen,
                        os.path.join(destino, "GodotLuau", "assets"),
                        dirs_exist_ok=True)
        sub = sorted(d for d in os.listdir(assets_origen)
                     if os.path.isdir(os.path.join(assets_origen, d)))
        print("  ✔ GodotLuau/assets/ (%s)" % ", ".join(sub))
    # DefaultScripts: el menú de Escape, PlayerModule, chat, cámara, salud…
    # TODO ESO ES LUAU y tiene que venir EN EL PROYECTO, no solo empotrado en
    # la DLL. Es la diferencia entre "hay un menú" y "aquí está el menú, ábrelo
    # y cámbialo": el diseño se lee, se entiende y se edita como cualquier otro
    # script del juego, que es lo que lo hace tuyo y no del motor.
    # (La copia empotrada sigue existiendo como respaldo para un juego
    # exportado o una instalación a medias; en ejecución gana el archivo.)
    defs_origen = os.path.join(ORIGEN, "DefaultScripts")
    if os.path.isdir(defs_origen):
        shutil.copytree(defs_origen,
                        os.path.join(destino, "GodotLuau", "DefaultScripts"),
                        dirs_exist_ok=True)
        n_luau = sum(len([f for f in fs if f.endswith(".luau")])
                     for _, _, fs in os.walk(defs_origen))
        print(f"  ✔ DefaultScripts/ ({n_luau} scripts Luau, menú incluido)")

    # El archivo Version vive en la RAIZ DEL REPO (no dentro de GodotLuau/), asi
    # que buscarlo en ORIGEN hacia que no se copiara nunca desde que la
    # extension se movio de sitio: el proyecto nacia sin Version y el updater
    # reportaba "version desconocida".
    version_origen = os.path.join(RAIZ, "Version")
    if not os.path.exists(version_origen):
        version_origen = os.path.join(ORIGEN, "Version")
    if os.path.exists(version_origen):
        shutil.copy2(version_origen, os.path.join(destino, "GodotLuau", "Version"))
        print("  ✔ GodotLuau/Version")
    else:
        print("  ⚠  No se encontro el archivo Version")

    # 6. Generar archivos de configuración
    uid_escena = uid_godot()

    # project.godot
    with open(os.path.join(destino, "project.godot"), "w", encoding="utf-8") as f:
        f.write(project_godot(nombre))
    print("  ✔ project.godot")

    # godot_luau.gdextension — DENTRO de GodotLuau/. Godot busca *.gdextension
    # por todo el proyecto, no solo en la raiz, asi que desde aqui carga igual y
    # el usuario ve una sola carpeta de la extension en el sistema de archivos.
    with open(os.path.join(destino, "GodotLuau", "godot_luau.gdextension"), "w", encoding="utf-8") as f:
        f.write(gdextension(libs_copiadas))
    print("  ✔ GodotLuau/godot_luau.gdextension")

    # Escena principal
    escena_contenido = scene_2d(uid_escena) if is_2d else scene_3d(uid_escena)
    with open(os.path.join(destino, "Escenas", "RobloxGame.tscn"), "w", encoding="utf-8") as f:
        f.write(escena_contenido)
    print("  ✔ Escenas/RobloxGame.tscn")

    # NOTA: Los scripts de jugador (Health, PlayerModule, ControlModule, CameraModule, GameManager)
    # son creados AUTOMÁTICAMENTE por la extensión C++ al abrir el proyecto en Godot.
    # Solo creamos archivos de ejemplo adicionales aquí.

    # 7. ModuleScript de ejemplo adicional (Utils)
    module_script = """-- > GodotLuau — PimpoliDev
local Module = {}

return Module
"""
    with open(os.path.join(destino, "GodotLuau", "ModuleScripts", "Utils.lua"), "w", encoding="utf-8") as f:
        f.write(module_script)
    print("  ✔ GodotLuau/ModuleScripts/Utils.lua")

    # ─── Mensaje final ───────────────────────────────────────────
    raiz = "Game2D" if is_2d else "Game3D"
    print(f"""
══════════════════════════════════════════════════════
  ✅  Proyecto "{nombre}" listo!  (modo {modo_str})

  CÓMO ABRIRLO EN GODOT:
  1. Abre Godot 4
  2. Clic en "Import" en el Project Manager
  3. Navega a esta carpeta → selecciona project.godot
  4. Clic en "Importar y Editar"
  5. Abre Escenas/RobloxGame.tscn
  6. ¡La estructura Roblox se crea automáticamente!

  ESTRUCTURA AUTO-GENERADA:
  Game ({raiz})
    ├── Workspace (con CurrentCamera)
    ├── Players / Lighting / Teams...
    ├── ServerScriptService
    │   └── GameManager (ServerScript) ← editable
    └── StarterPlayer
        ├── StarterCharacterScripts
        │   ├── Health.lua  ← regen vida
        │   └── Animate.lua ← animaciones
        └── StarterPlayerScripts
            ├── PlayerModule.lua ← loader
            └── Modules/
                ├── ControlModule.lua ← velocidad
                └── CameraModule.lua ← cámara
══════════════════════════════════════════════════════
  Ruta: {destino}
""")


# ─── Punto de entrada ───────────────────────────────────────────────
if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    print(BANNER)

    # Nombre del proyecto
    nombre = input("  Nombre del proyecto (ej: MiJuegoRoblox): ").strip()
    if not nombre:
        nombre = "MiJuegoRoblox"

    # Tipo de juego
    modo = elegir_opcion(
        "¿Qué tipo de juego vas a crear?",
        [
            "3D — Mundo tridimensional (como la mayoría de juegos de Roblox)",
            "2D — Juego en dos dimensiones (plataformas, RPG top-down, etc.)",
        ]
    )

    # Si es 2D, preguntar subtipo para informar al usuario
    if modo == 1:
        subtipo = elegir_opcion(
            "¿Qué estilo de movimiento 2D prefieres? (podrás cambiarlo después en el script)",
            [
                "Platformer — Con gravedad y salto (tipo Mario, Celeste)",
                "TopDown    — Sin gravedad, 8 direcciones (tipo Zelda, RPG)",
            ]
        )
        print(f"\n  Nota: El tipo se configura en StarterPlayerScripts/Modules/ControlModule.lua")
        print(f"  Variable MOVEMENT_TYPE = {subtipo}  ({['Platformer', 'TopDown'][subtipo]})")

    # Directorio de destino
    print("\n  ¿Dónde crear el proyecto?")
    print("  (Deja en blanco para usar el Escritorio)")
    destino_base = input("  Ruta: ").strip()

    if not destino_base:
        destino_base = os.path.join(os.path.expanduser("~"), "Desktop")

    destino = os.path.join(destino_base, nombre.replace(" ", "_"))

    # Verificar si ya existe
    if os.path.isfile(destino):
        print(f"\n  ERROR: '{destino}' existe y es un archivo, no una carpeta.")
        sys.exit(1)
    if os.path.isdir(destino) and os.listdir(destino):
        resp = input(f"\n  La carpeta ya existe y tiene archivos. ¿Sobreescribir? (s/n): ").strip().lower()
        if resp != "s":
            print("  Cancelado.")
            sys.exit(0)
        shutil.rmtree(destino)

    os.makedirs(destino, exist_ok=True)
    crear_proyecto(destino, nombre, modo)

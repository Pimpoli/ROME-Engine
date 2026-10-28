"""
GodotLuau — Generador del paquete de release
=============================================
Regenera GodotLuau.zip (el paquete que descarga el auto-updater)
y su hash GodotLuau.zip.sha256 para la verificación de integridad.

Ejecutar SIEMPRE después de recompilar las DLLs y antes de hacer push:
    python generar_release.py
"""
import os
import sys
import hashlib
import zipfile

NOMBRE_ZIP = "GodotLuau.zip"

# Contenido del paquete de release (lo que instala el updater en res://)
#
# Desde la 1.17 el proyecto del usuario solo ve DOS cosas de GodotLuau:
#   res://GodotLuau/   → todo (bin, icons, assets, DefaultScripts, el
#                        .gdextension, la licencia y el contador de ids)
#   res://addons/GodotLuauUpdater/ → obligado: Godot SOLO descubre plugins de
#                        editor en res://addons/<nombre>/plugin.cfg. No se puede
#                        mover sin perder el panel "GodotLuau Config".
#
# El .gdextension ya NO va suelto en la raiz: Godot busca *.gdextension por todo
# el proyecto, asi que funciona igual desde dentro de la carpeta (es lo que hace
# cualquier extension distribuida como addon).
#
# (origen en el repo, destino dentro del zip)
ARCHIVOS_SUELTOS = [
    ("LICENSE", "GodotLuau/LICENSE"),   # en el repo se queda en la raiz (GitHub)
    # Version se queda en la raiz del repo porque el updater lo lee por URL raw
    # (raw.githubusercontent.com/.../main/Version). Dentro del zip tiene que ir
    # a GodotLuau/Version: es de donde lo leen el plugin y el banner de C++.
    # Sin esto una instalacion limpia no tiene version y el banner dice "?".
    ("Version", "GodotLuau/Version"),
]
CARPETAS = [
    "addons/GodotLuauUpdater",
    "GodotLuau",   # TODO lo de la extension vive aqui (bin, icons, assets, shaders, DefaultScripts...)
]
# Archivos que NUNCA deben viajar en el zip aunque esten dentro de GodotLuau/.
# .luau_ids.cfg es el contador de ids DEL PROYECTO: si se empaquetara, cada
# actualizacion le pisaria los contadores al usuario y los ids de script
# empezarian a chocar. El repo es tambien un proyecto de Godot, asi que este
# archivo aparece aqui en cuanto se abre el editor.
# Archivos del proyecto que viven en GodotLuau/ pero no son del paquete: si el
# repo los tiene (porque se ha usado como proyecto), no deben pisar los del usuario.
# El logo del juego (MenuLogo.svg) y su .ico son DEL USUARIO: el actualizador
# descomprime el zip entero encima del proyecto y cada actualizacion le volvia a
# poner el logo de fabrica. Si faltan, el plugin los crea desde RocketMeteor.svg.
# BootSplash.png ya no se usa (el arranque va en negro).
EXCLUIR = {".luau_ids.cfg", ".gl_mp_session", ".gl_view_cmd", ".gl_host", "gl_match.cfg",
           "MenuLogo.svg", "MenuLogo.svg.import", "GodotLuau.ico",
           "BootSplash.png", "BootSplash.png.import"}
# Carpetas que NO viajan en el paquete base. GodotLuau/ia es la extension
# OPCIONAL del chat local (llama.cpp, ~7 MB): quien no use la IA no tiene por
# que descargarla, asi que va en su propio zip (--solo-ia).
EXCLUIR_DIRS = {"ia", ".luau_trash", ".luau_backup"}
EXTENSIONES_BIN = (".dll", ".so", ".dylib", ".lib", ".exp")
# ".luau" = plantillas de GodotLuau/DefaultScripts (el motor las lee de disco)
EXTENSIONES_TEXTO = (".cfg", ".gd", ".gdextension", ".txt", ".md", ".json", ".lua", ".luau", ".uid")

# Binarios por plataforma (para armar zips por plataforma en el release).
BIN_POR_PLATAFORMA = {
    "windows": (".dll", ".lib", ".exp"),
    "linux":   (".so",),
    "macos":   (".dylib",),
}


def agregar_archivo(zf, ruta, arcname):
    """Agrega un archivo al zip quitando el BOM UTF-8 de los archivos de texto.
    Godot no tolera BOM en .cfg/.gdextension (el plugin deja de cargar)."""
    if ruta.lower().endswith(EXTENSIONES_TEXTO):
        with open(ruta, "rb") as f:
            data = f.read()
        if data.startswith(b"\xef\xbb\xbf"):
            data = data[3:]
            # tambien arreglar el archivo original en disco
            with open(ruta, "wb") as f:
                f.write(data)
            print(f"⚠  BOM eliminado: {arcname}")
        zf.writestr(arcname, data)
    else:
        zf.write(ruta, arcname=arcname)


def empaquetar(nombre_zip=NOMBRE_ZIP, only=None):
    print("=" * 54)
    print(f"  GodotLuau — Empaquetando {nombre_zip}"
          + (f" (solo {only})" if only else " (todas las plataformas)"))
    print("=" * 54)

    # Qué binarios incluir en /bin: todos, o solo los de una plataforma.
    bin_ok = BIN_POR_PLATAFORMA.get(only, EXTENSIONES_BIN) if only else EXTENSIONES_BIN

    with zipfile.ZipFile(nombre_zip, "w", zipfile.ZIP_DEFLATED) as zf:
        for origen, destino in ARCHIVOS_SUELTOS:
            if os.path.exists(origen):
                agregar_archivo(zf, origen, destino)
                print(f"✓  {origen} → {destino}")

        for carpeta in CARPETAS:
            if not os.path.isdir(carpeta):
                print(f"⚠  No existe: {carpeta}/ (omitida)")
                continue
            for raiz, subdirs, archivos in os.walk(carpeta):
                # Podar las carpetas excluidas (la IA opcional).
                subdirs[:] = [d for d in subdirs
                              if not (carpeta == "GodotLuau" and d in EXCLUIR_DIRS)]
                for archivo in archivos:
                    if archivo in EXCLUIR:
                        print(f"·  omitido (es del proyecto, no del paquete): {archivo}")
                        continue
                    raiz_norm = raiz.replace("\\", "/")
                    if raiz_norm.endswith("/bin"):
                        if not archivo.endswith(EXTENSIONES_BIN):
                            continue
                        if only and not archivo.endswith(bin_ok):
                            continue   # binario de otra plataforma → fuera de este zip
                    ruta = os.path.join(raiz, archivo)
                    agregar_archivo(zf, ruta, ruta.replace("\\", "/"))
            print(f"✓  {carpeta}/")

    sha = hashlib.sha256(open(nombre_zip, "rb").read()).hexdigest()
    with open(nombre_zip + ".sha256", "w") as f:
        f.write(sha + "\n")

    tam = os.path.getsize(nombre_zip) / 1024.0
    print()
    print(f"  ¡Listo! {nombre_zip} ({tam:.0f} KB)")
    print(f"  SHA-256: {sha}")
    print("=" * 54)


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except AttributeError:
        pass
    # Uso:
    #   python generar_release.py                 → GodotLuau.zip (todas las plataformas)
    #   python generar_release.py --only windows --out "Windows GodotLuau.zip"
    #   python generar_release.py --only linux   --out "Linux GodotLuau.zip"
    argv = sys.argv[1:]
    # Paquete de la IA opcional: solo GodotLuau/ia (el motor de chat).
    if "--solo-ia" in argv:
        salida = "GodotLuau IA.zip"
        with zipfile.ZipFile(salida, "w", zipfile.ZIP_DEFLATED) as zf:
            n = 0
            for raiz, _, archivos in os.walk(os.path.join("GodotLuau", "ia")):
                for a in archivos:
                    if a.endswith((".a", ".exp", ".lib")):
                        continue
                    ruta = os.path.join(raiz, a)
                    zf.write(ruta, arcname=ruta.replace("\\", "/"))
                    n += 1
        sha = hashlib.sha256(open(salida, "rb").read()).hexdigest()
        with open(salida + ".sha256", "w") as f:
            f.write(sha + chr(10))
        print("  %s listo (%d archivos, %.0f KB)"
              % (salida, n, os.path.getsize(salida) / 1024.0))
        sys.exit(0)
    only = None
    out = NOMBRE_ZIP
    i = 0
    while i < len(argv):
        if argv[i] == "--only" and i + 1 < len(argv):
            only = argv[i + 1]; i += 2
        elif argv[i] == "--out" and i + 1 < len(argv):
            out = argv[i + 1]; i += 2
        else:
            i += 1
    empaquetar(out, only)

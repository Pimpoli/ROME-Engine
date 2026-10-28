"""Empotra los avatares R6/R15 en la DLL: escribe src/characters/gl_avatares_gen.h.

    python tools/generar_avatares.py [--origen CARPETA] [--copiar]

Lee de GodotLuau/assets/avatars/ (o de --origen) para cada rig (r6, r15):
rig.json, mallas/*.obj y anims/*.json, mas cara.png. Con --copiar, ademas los
deja en GodotLuau/assets/avatars/ (lo que sale de Blender va a --origen).

En ejecucion manda la copia en disco (res://GodotLuau/assets/avatars/...), asi
un avatar se edita sin recompilar; la empotrada es para juegos exportados y
proyectos sin esos archivos (ver gl_avatar_rig.h).
"""
import json
import os
import shutil
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DESTINO_ASSETS = os.path.join(RAIZ, "GodotLuau", "assets", "avatars")
SALIDA = os.path.join(RAIZ, "src", "characters", "gl_avatares_gen.h")
TROZO = 30000   # literales de menos de 64 KB cada uno, contando los escapes

args = sys.argv[1:]
origen = DESTINO_ASSETS
if "--origen" in args:
    origen = args[args.index("--origen") + 1]
copiar = "--copiar" in args

archivos = []   # (ruta relativa, bytes)
for rig in ("r6", "r15"):
    base = os.path.join(origen, rig)
    if not os.path.isdir(base):
        print("falta", base)
        sys.exit(1)
    rel = rig + "/rig.json"
    datos = json.load(open(os.path.join(base, "rig.json"), encoding="utf-8"))
    archivos.append((rel, json.dumps(datos, separators=(",", ":")).encode("utf-8")))
    for sub, ext in (("mallas", ".obj"), ("anims", ".json")):
        carpeta = os.path.join(base, sub)
        for nombre in sorted(os.listdir(carpeta)):
            if not nombre.endswith(ext):
                continue
            ruta = os.path.join(carpeta, nombre)
            if ext == ".json":
                contenido = json.dumps(json.load(open(ruta, encoding="utf-8")), separators=(",", ":")).encode("utf-8")
            else:
                lineas = [l for l in open(ruta, encoding="utf-8").read().splitlines() if l and not l.startswith("#")]
                contenido = ("\n".join(lineas) + "\n").encode("utf-8")
            archivos.append((f"{rig}/{sub}/{nombre}", contenido))
cara = open(os.path.join(origen, "cara.png"), "rb").read()

if copiar and os.path.abspath(origen) != os.path.abspath(DESTINO_ASSETS):
    for rig in ("r6", "r15"):
        for sub in ("mallas", "anims"):
            os.makedirs(os.path.join(DESTINO_ASSETS, rig, sub), exist_ok=True)
    for rel, _ in archivos:
        shutil.copy2(os.path.join(origen, rel), os.path.join(DESTINO_ASSETS, rel))
    shutil.copy2(os.path.join(origen, "cara.png"), os.path.join(DESTINO_ASSETS, "cara.png"))
    for rig in ("r6", "r15"):
        glb = os.path.join(origen, rig, f"Avatar{rig.upper()}.glb")
        if os.path.exists(glb):
            shutil.copy2(glb, os.path.join(DESTINO_ASSETS, rig, os.path.basename(glb)))


def literal(b):
    # Texto UTF-8 como literales normales con escapes: sin depender de un
    # delimitador de raw string que el contenido pudiera traer.
    trozos = []
    texto = b.decode("utf-8")
    for i in range(0, len(texto), TROZO):
        parte = texto[i:i + TROZO]
        esc = (parte.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
               .replace("\r", "").replace("\t", "\\t").replace("?", "\\?"))   # ? por los trigrafos
        trozos.append('"' + esc + '"')
    return "\n    ".join(trozos)


# Las declaraciones van en el header y los datos en UN .cpp: con los datos en
# el header, cada unidad de compilacion que lo incluyera tendria su copia.
DATOS = os.path.join(RAIZ, "src", "core", "gl_avatares_gen.cpp")
with open(SALIDA, "w", encoding="utf-8", newline="\n") as f:
    f.write("// GENERADO por tools/generar_avatares.py: los avatares R6/R15 empotrados.\n")
    f.write("// No editar a mano: volver a generar desde GodotLuau/assets/avatars/.\n")
    f.write("// Los datos estan en src/core/gl_avatares_gen.cpp (una sola copia en la DLL).\n")
    f.write("#ifndef GL_AVATARES_GEN_H\n#define GL_AVATARES_GEN_H\n\n")
    f.write("struct GLArchivoAvatar { const char* ruta; const char* datos; int largo; };\n")
    f.write("extern const GLArchivoAvatar GL_ARCHIVOS_AVATAR[];\n")
    f.write("extern const int GL_ARCHIVOS_AVATAR_N;\n")
    f.write("extern const unsigned char GL_AVA_CARA_PNG[];\n")
    f.write("extern const int GL_AVA_CARA_PNG_N;\n\n#endif // GL_AVATARES_GEN_H\n")
with open(DATOS, "w", encoding="utf-8", newline="\n") as f:
    f.write("// GENERADO por tools/generar_avatares.py. No editar a mano.\n")
    f.write('#include "../characters/gl_avatares_gen.h"\n\n')
    for i, (rel, b) in enumerate(archivos):
        f.write(f"static const char GL_AVA_{i}[] =\n    {literal(b)};\n")
    f.write("\nextern const GLArchivoAvatar GL_ARCHIVOS_AVATAR[] = {\n")
    for i, (rel, b) in enumerate(archivos):
        f.write(f'    {{ "{rel}", GL_AVA_{i}, {len(b)} }},\n')
    f.write("};\n")
    f.write(f"extern const int GL_ARCHIVOS_AVATAR_N = {len(archivos)};\n\n")
    f.write("extern const unsigned char GL_AVA_CARA_PNG[] = {\n")
    for i in range(0, len(cara), 24):
        f.write("    " + ", ".join(str(x) for x in cara[i:i + 24]) + ",\n")
    f.write("};\n")
    f.write(f"extern const int GL_AVA_CARA_PNG_N = {len(cara)};\n")

total = sum(len(b) for _, b in archivos)
print(f"{len(archivos)} archivos ({total // 1024} KB) + cara.png ({len(cara) // 1024} KB) -> {SALIDA} y {DATOS}")

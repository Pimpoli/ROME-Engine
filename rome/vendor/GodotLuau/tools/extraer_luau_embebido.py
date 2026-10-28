"""
GodotLuau — Extractor del Luau embebido en los headers C++
===========================================================
Saca cada bloque R"LUAU( ... )LUAU" de src/**.h a un archivo .luau suelto
para que tools/check_luau.cpp pueda comprobarles la sintaxis.

Existe porque el stdlib (src/core/luau_stdlib.h) y las plantillas embebidas
son codigo Luau que el compilador de C++ ve como una cadena cualquiera: un
error de sintaxis ahi compila perfectamente y revienta en tiempo de ejecucion.

    python tools/extraer_luau_embebido.py <carpeta_destino>
"""
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Solo LUAU: R"SHADER(...)" y R"JSON(...)" no son codigo Luau.
PATRON = re.compile(r'R"LUAU\((.*?)\)LUAU"', re.S)


def extraer(destino):
    os.makedirs(destino, exist_ok=True)
    escritos = []
    for base, _, archivos in os.walk(os.path.join(RAIZ, "src")):
        for nombre in sorted(archivos):
            if not nombre.endswith(".h"):
                continue
            ruta = os.path.join(base, nombre)
            with open(ruta, encoding="utf-8") as f:
                texto = f.read()
            for i, bloque in enumerate(PATRON.findall(texto)):
                salida = os.path.join(destino, "%s__%02d.luau" % (nombre[:-2], i))
                with open(salida, "w", encoding="utf-8", newline="\n") as f:
                    f.write(bloque)
                escritos.append(salida)
    return escritos


if __name__ == "__main__":
    destino = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, ".luau_extraido")
    escritos = extraer(destino)
    for r in escritos:
        print(os.path.relpath(r, RAIZ).replace("\\", "/"))
    print("\n%d bloque(s) Luau extraidos a %s" % (len(escritos), destino), file=sys.stderr)

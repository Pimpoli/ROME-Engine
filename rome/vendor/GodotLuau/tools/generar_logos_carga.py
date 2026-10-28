"""Mete en la DLL lo que usa la pantalla de carga por defecto.

    python tools/generar_logos_carga.py

Lee dev/carga/ (los logos de Rocket Meteor para fondo oscuro y de GodotLuau,
los dos en SVG; el borde ondulado, el humo, la estrella y los sonidos de la
intro de Just Press it) y escribe src/core/gl_carga_logos.cpp. Va dentro de la
DLL para que la pantalla salga aunque el proyecto no tenga los archivos. Los
logos se rasterizan al tamaño de la ventana (nitidos a cualquier resolucion).
El logo de GodotLuau sale de C:\\Claude\\GodotLuauLogo (build.js): es el
godotluau-logo-ubuntu.svg.
"""
import os
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CARGA = os.path.join(RAIZ, "dev", "carga")
SALIDA = os.path.join(RAIZ, "src", "core", "gl_carga_logos.cpp")

# En el mismo orden que GLCargaRecurso (src/ui/gl_carga_piezas.h).
RECURSOS = [
    "rocket-meteor-fondo-oscuro.svg",
    "godotluau-logo.svg",
    "splash_transition.png",
    "big_smoke.png",
    "star.png",
    "fall_impact.ogg",
    # La nota de cada letra suena hasta 34 veces: va ya decodificada (PCM de 16
    # bits, estereo, 44100 Hz, sin cabecera) para que cada play() no tenga que
    # montar un decodificador Ogg (~0,6 ms). Sale de item_collect.ogg con:
    # ffmpeg -i item_collect.ogg -f s16le -acodec pcm_s16le -ar 44100 -ac 2 item_collect.pcm
    "item_collect.pcm",
    "rainbow_open.ogg",
    "button_break.ogg",
]


def bytes_c(nombre, datos):
    lineas = []
    # 150 por linea: el archivo se queda por debajo de 2.000 lineas.
    for i in range(0, len(datos), 150):
        lineas.append("    " + ",".join(str(b) for b in datos[i:i + 150]) + ",")
    return "static const unsigned char %s[] = {\n%s\n};\n" % (nombre, "\n".join(lineas))


def main():
    partes = [
        "// GENERADO por tools/generar_logos_carga.py. No editar a mano.\n"
        "// Lo que usa la pantalla de carga por defecto (src/ui/gl_carga_defecto.h).\n"
        "#include <cstddef>\n\n"
    ]
    total = 0
    for i, nombre in enumerate(RECURSOS):
        with open(os.path.join(CARGA, nombre), "rb") as f:
            datos = f.read()
        total += len(datos)
        partes.append("// %s\n" % nombre)
        partes.append(bytes_c("GL_CARGA_%d" % i, datos) + "\n")
    partes.append("const unsigned char* gl_carga_recurso(int i, size_t& n) {\n    switch (i) {\n")
    for i in range(len(RECURSOS)):
        partes.append("        case %d: n = sizeof(GL_CARGA_%d); return GL_CARGA_%d;\n" % (i, i, i))
    partes.append("        default: n = 0; return nullptr;\n    }\n}\n")
    with open(SALIDA, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(partes))
    print(len(RECURSOS), "recursos,", total, "bytes ->", SALIDA)
    return 0


if __name__ == "__main__":
    sys.exit(main())

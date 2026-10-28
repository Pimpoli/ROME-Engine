"""
GodotLuau — Generador de la tabla de compatibilidad (Capa 0 de GodotLuau IA)
============================================================================
Produce src/core/gl_compat_gen.h con DOS lados:

  · lo que ESTE motor implementa de verdad — se saca del propio codigo
    (los strcmp del puente, el mapa de Instance.new, los servicios del
    stdlib y la tabla de Enums). No es una lista escrita a mano que se
    quede vieja: es el motor describiendose a si mismo.

  · lo que ROBLOX tiene — se saca de dev/api/globalTypes.d.luau, el volcado
    oficial de la API.

Con los dos lados, el analizador puede distinguir tres casos MUY distintos:
    1. lo usas y lo tenemos            -> nada que decir
    2. lo usas, Roblox lo tiene, no lo tenemos -> ESTO es lo que rompe el juego
    3. lo usas y no existe ni en Roblox -> es tuyo (atributo, campo de tabla)

Ejecutar tras tocar el puente:  python tools/generar_compat.py
"""
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(RAIZ, "src")
SALIDA = os.path.join(SRC, "core", "gl_compat_gen.h")


def leer(*partes):
    ruta = os.path.join(RAIZ, *partes)
    if not os.path.exists(ruta):
        return ""
    with open(ruta, encoding="utf-8", errors="replace") as f:
        texto = f.read()
    # Los headers grandes estan partidos (luau_stdlib.h, luau_api.h...): sus
    # partes se pegan donde va su #include, y el texto queda como antes.
    base = os.path.splitext(os.path.basename(ruta))[0]
    def pegar(m):
        return leer(*partes[:-1], m.group(1))
    return re.sub(r'^#include "(%s_[^"]+)".*$' % re.escape(base), pegar, texto, flags=re.M)


def leer_registro():
    """register_types.cpp y los registro_*.cpp: el registro de clases esta
    repartido entre ellos para compilar en paralelo."""
    core = os.path.join(SRC, "core")
    partes = [leer("src", "core", "register_types.cpp")]
    for nombre in sorted(os.listdir(core)):
        if nombre.startswith("registro_") and nombre.endswith(".cpp"):
            partes.append(leer("src", "core", nombre))
    return "\n".join(partes)


# ── LADO MOTOR ──────────────────────────────────────────────────────────────

def _todos_los_headers():
    """Todos los .h propios (src/), menos el generado."""
    for base, _, archivos in os.walk(SRC):
        for n in sorted(archivos):
            if n.endswith(".h") and n != "gl_compat_gen.h":
                yield os.path.join(base, n)


def miembros_del_puente():
    """Todo nombre al que el motor responde. Se barre TODO src/, no un par de
    archivos: el puente esta repartido (luau_api.h resuelve la mayoria, pero
    WaitForChild lo hace el __index de luau_script.h, los UI* son clases con
    sus propias ADD_PROPERTY, etc.). Quedarse corto aqui es peor que pasarse:
    produce falsos positivos, que es lo unico que puede hacer inutil a un
    analizador."""
    nombres = set()
    for ruta in _todos_los_headers():
        with open(ruta, encoding="utf-8", errors="replace") as f:
            txt = f.read()
        # strcmp(key, "X") / strcmp(canon, "X") / strcmp(metodo, "X")
        for m in re.finditer(r'strcmp\(\s*(?:key|canon|metodo|nombre|prop)\s*,\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*\)', txt):
            nombres.add(m.group(1))
        # comparaciones con String / StringName
        for m in re.finditer(r'(?:key|prop|nombre)\s*==\s*"([A-Za-z_][A-Za-z0-9_]*)"', txt):
            nombres.add(m.group(1))
        for m in re.finditer(r'StringName\("([A-Za-z_][A-Za-z0-9_]*)"\)', txt):
            nombres.add(m.group(1))
        # Publicados por ClassDB: los alcanza el respaldo generico
        for m in re.finditer(r'PropertyInfo\([^,]+,\s*"([A-Za-z_][A-Za-z0-9_]*)"', txt):
            nombres.add(m.group(1))
        for m in re.finditer(r'D_METHOD\("([A-Za-z_][A-Za-z0-9_]*)"', txt):
            nombres.add(m.group(1))
        for m in re.finditer(r'ADD_SIGNAL\(\s*MethodInfo\("([A-Za-z_][A-Za-z0-9_]*)"', txt):
            nombres.add(m.group(1))
        # TABLAS DE NOMBRES: el puente resuelve familias enteras con arrays de
        # literales (kMoveSig[], MOUSE_EVENTS[], kAutoServices[]...). Sin leer
        # estas, senales que SI funcionan (Humanoid.Jumping, Mouse.Button1Down)
        # salian como no soportadas: falsos positivos, que es justo lo que hace
        # inutil a un analizador.
        for m in re.finditer(r'const char\s*\*\s*(?:const\s+)?\w+\[\]\s*=\s*\{(.*?)\}\s*;', txt, re.S):
            bloque = m.group(1)
            if len(bloque) > 4000:
                continue   # tablas gigantes generadas: no son nombres de API
            for k in re.finditer(r'"([A-Za-z_][A-Za-z0-9_]*)"', bloque):
                nombres.add(k.group(1))
    # El stdlib es Lua dentro de una cadena: sus metodos y claves de tabla.
    std = leer("src", "core", "luau_stdlib.h")
    for m in re.finditer(r'function\s+[A-Za-z_][A-Za-z0-9_]*\s*[:.]([A-Za-z_][A-Za-z0-9_]*)\s*\(', std):
        nombres.add(m.group(1))
    for m in re.finditer(r'^\s*([A-Z][A-Za-z0-9_]*)\s*=\s*(?:function|_mkSignal|\{)', std, re.M):
        nombres.add(m.group(1))
    # Accesores calculados del stdlib: _cf_get.LookVector = function(cf) ...
    for m in re.finditer(r'^\s*_\w+\.([A-Za-z_][A-Za-z0-9_]*)\s*=', std, re.M):
        nombres.add(m.group(1))
    # Comparaciones por nombre dentro del Lua: k == "LookVector"
    for m in re.finditer(r'==\s*"([A-Za-z_][A-Za-z0-9_]*)"', std):
        nombres.add(m.group(1))
    return nombres


def clases_instanciables():
    """Lo que Instance.new() puede crear: el mapa del stdlib + lo registrado en
    ClassDB con nombre de Roblox."""
    nombres = set()
    std = leer("src", "core", "luau_stdlib.h")
    m = re.search(r"local\s+_cls_map\s*=\s*\{(.*?)\n\}", std, re.S)
    if m:
        for k in re.finditer(r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"', m.group(1)):
            nombres.add(k.group(1))
    # Toda clase declarada o registrada en src/: los UI* (UICorner, UIGradient…)
    # se registran dentro de sus propios modulos, no en los registro_*.cpp.
    reg = leer_registro()
    for k in re.finditer(r'register_class<([A-Za-z_][A-Za-z0-9_]*)>', reg):
        nombres.add(k.group(1))
    for ruta in _todos_los_headers():
        with open(ruta, encoding="utf-8", errors="replace") as f:
            txt = f.read()
        for k in re.finditer(r'GDCLASS\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,', txt):
            nombres.add(k.group(1))
        for k in re.finditer(r'register_class<([A-Za-z_][A-Za-z0-9_]*)>', txt):
            nombres.add(k.group(1))
        # El comodin del importador acepta clases por nombre en tablas.
        for k in re.finditer(r'GL_CLASE_PUENTE\(\s*([A-Za-z_][A-Za-z0-9_]*)', txt):
            nombres.add(k.group(1))
    # El mapa del importador: Roblox -> nuestra clase. La izquierda tambien
    # "existe" para Instance.new porque el puente la traduce.
    cm = leer("src", "importer", "rbx_classmap.h")
    for k in re.finditer(r'm\["([A-Za-z_][A-Za-z0-9_]*)"\]', cm):
        nombres.add(k.group(1))
    return nombres


def servicios():
    """game:GetService("X") que resuelven."""
    nombres = set()
    std = leer("src", "core", "luau_stdlib.h")
    for m in re.finditer(r'_G\["__SVC_([A-Za-z_][A-Za-z0-9_]*)"\]', std):
        nombres.add(m.group(1))
    api = leer("src", "core", "luau_api.h")
    m = re.search(r"kAutoServices\[\]\s*=\s*\{(.*?)\};", api, re.S)
    if m:
        for s in re.finditer(r'"([A-Za-z_][A-Za-z0-9_]*)"', m.group(1)):
            nombres.add(s.group(1))
    # Los servicios que son NODOS del arbol (Workspace, Lighting, Players...)
    reg = leer_registro()
    for k in re.finditer(r'register_class<([A-Za-z_][A-Za-z0-9_]*)>', reg):
        nombres.add(k.group(1))
    return nombres


def enums_motor():
    """Enum.X.Y que existen en el stdlib, aplanados a "X.Y"."""
    pares = set()
    std = leer("src", "core", "luau_stdlib.h")
    m = re.search(r"\nEnum\s*=\s*\{(.*?)\n\}\s*\n", std, re.S)
    if not m:
        return pares
    bloque = m.group(1)
    for em in re.finditer(r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*\{([^{}]*)\}', bloque):
        enum_name = em.group(1)
        for im in re.finditer(r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*-?\d+', em.group(2)):
            pares.add("%s.%s" % (enum_name, im.group(1)))
    return pares


# ── LADO ROBLOX (globalTypes.d.luau) ────────────────────────────────────────

def api_roblox():
    """Del volcado oficial: nombres de clase, union de nombres de miembro,
    los Enum.X.Y reales y de quien hereda cada clase."""
    txt = leer("dev", "api", "globalTypes.d.luau")
    clases, miembros, enums, herencia = set(), set(), set(), {}
    if not txt:
        return clases, miembros, enums, herencia

    clase_actual = None
    enum_actual = None
    for linea in txt.split("\n"):
        m = re.match(r'declare class (\w+)(?: extends (\w+))?', linea)
        if m:
            nombre = m.group(1)
            if nombre.startswith("Enum") and nombre.endswith("_INTERNAL"):
                enum_actual = nombre[4:-9]
                clase_actual = None
            else:
                clase_actual = nombre
                enum_actual = None
                if not nombre.startswith("Enum"):
                    clases.add(nombre)
                    padre = m.group(2)
                    if padre and padre not in ("Object", "Instance"):
                        herencia[nombre] = padre
            continue
        if linea.startswith("end"):
            clase_actual = None
            enum_actual = None
            continue
        # Miembro: "\tNombre: Tipo"  o  "\tfunction Nombre(self, ...)"
        mm = re.match(r'\t(?:function )?([A-Za-z_][A-Za-z0-9_]*)\s*[:(]', linea)
        if not mm:
            continue
        nombre = mm.group(1)
        if enum_actual:
            if nombre not in ("GetEnumItems", "FromName", "FromValue"):
                enums.add("%s.%s" % (enum_actual, nombre))
        elif clase_actual:
            miembros.add(nombre)
    return clases, miembros, enums, herencia


def escribir_herencia(herencia):
    """src/core/gl_herencia_gen.h: pares (clase, padre) ordenados por clase,
    para que IsA suba la cadena como Roblox (ScreenGui -> LayerCollector ->
    GuiBase2d -> GuiBase). Va aparte de gl_compat_gen.h porque lo usa el
    puente en tiempo de juego y aquel es grande."""
    ruta = os.path.join(SRC, "core", "gl_herencia_gen.h")
    lineas = [
        "// GENERADO POR tools/generar_compat.py — NO EDITAR A MANO",
        "// De quien hereda cada clase de Roblox (dev/api/globalTypes.d.luau),",
        "// sin Instance: pares {clase, padre} ordenados por clase (strcmp).",
        "#ifndef GL_HERENCIA_GEN_H",
        "#define GL_HERENCIA_GEN_H",
        "",
        "static const char* const GL_RBX_HERENCIA[][2] = {",
    ]
    for clase in sorted(herencia):
        lineas.append('    {"%s", "%s"},' % (clase, herencia[clase]))
    lineas += [
        "};",
        "static const int GL_RBX_HERENCIA_N = %d;" % len(herencia),
        "",
        "#endif // GL_HERENCIA_GEN_H",
        "",
    ]
    with open(ruta, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lineas))
    return len(herencia)


# ── ESCRITURA DEL HEADER ────────────────────────────────────────────────────

def tabla_c(nombre, valores):
    ls = sorted(valores)
    out = ["static const char* const %s[] = {" % nombre]
    fila = "   "
    for v in ls:
        trozo = ' "%s",' % v
        if len(fila) + len(trozo) > 100:
            out.append(fila)
            fila = "   "
        fila += trozo
    if fila.strip():
        out.append(fila)
    out.append("    nullptr")
    out.append("};")
    return "\n".join(out), len(ls)


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except AttributeError:
        pass

    gl_miembros = miembros_del_puente()
    gl_clases = clases_instanciables()
    gl_serv = servicios()
    gl_enums = enums_motor()
    rbx_clases, rbx_miembros, rbx_enums, rbx_herencia = api_roblox()

    partes = ["""// ════════════════════════════════════════════════════════════════════
//  GENERADO POR tools/generar_compat.py — NO EDITAR A MANO
//
//  Tablas de compatibilidad para el analizador (Capa 0 de GodotLuau IA).
//  Un lado es lo que ESTE motor implementa (sacado del propio codigo), el
//  otro lo que Roblox tiene (del volcado oficial de su API). Cruzandolos se
//  puede decir, sin ejecutar nada y sin equivocarse, que linea de un script
//  no va a funcionar aqui — y si es porque falta en el motor o porque el
//  nombre no existe ni en Roblox.
// ════════════════════════════════════════════════════════════════════
#ifndef GL_COMPAT_GEN_H
#define GL_COMPAT_GEN_H
"""]
    for nombre, valores, comentario in [
        ("GL_COMPAT_MIEMBROS", gl_miembros, "Nombres a los que el puente responde"),
        ("GL_COMPAT_CLASES", gl_clases, "Lo que Instance.new() sabe crear"),
        ("GL_COMPAT_SERVICIOS", gl_serv, "game:GetService(...) que resuelven"),
        ("GL_COMPAT_ENUMS", gl_enums, "Enum.X.Y que existen aqui"),
        ("GL_RBX_CLASES", rbx_clases, "Clases de Roblox (referencia)"),
        ("GL_RBX_MIEMBROS", rbx_miembros, "Union de miembros de Roblox (referencia)"),
        ("GL_RBX_ENUMS", rbx_enums, "Enum.X.Y reales de Roblox (referencia)"),
    ]:
        txt, n = tabla_c(nombre, valores)
        partes.append("\n// %s (%d)\n%s\n" % (comentario, n, txt))
    partes.append("\n#endif // GL_COMPAT_GEN_H\n")

    with open(SALIDA, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(partes))

    print("gl_compat_gen.h generado:")
    print("  motor  -> %5d miembros | %4d clases | %4d servicios | %5d enums"
          % (len(gl_miembros), len(gl_clases), len(gl_serv), len(gl_enums)))
    print("  roblox -> %5d miembros | %4d clases |                   %5d enums"
          % (len(rbx_miembros), len(rbx_clases), len(rbx_enums)))
    print("  %.0f KB" % (os.path.getsize(SALIDA) / 1024.0))
    print("gl_herencia_gen.h generado: %d clases con padre" % escribir_herencia(rbx_herencia))


if __name__ == "__main__":
    main()

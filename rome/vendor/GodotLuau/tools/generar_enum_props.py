"""Genera src/core/gl_enum_props_gen.h: que propiedades de Roblox son un enum.

    python tools/generar_enum_props.py

Lee dev/api/globalTypes.d.luau (las declaraciones `declare class X extends Y`
con propiedades `Nombre: EnumAlgo`), resuelve la herencia y escribe, por clase
concreta, cada propiedad cuyo tipo es un enum. El puente lo usa para entregar
EnumItems en vez de numeros (F8 de la v1.17).
"""
import os
import re

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
API = os.path.join(RAIZ, "dev", "api", "globalTypes.d.luau")
SALIDA = os.path.join(RAIZ, "src", "core", "gl_enum_props_gen.h")

lineas = open(API, encoding="utf-8").read().splitlines()

re_enum = re.compile(r"^declare class Enum(\w+) extends EnumItem end\s*$")
re_cls = re.compile(r"^declare class (\w+)(?: extends (\w+))?\s*$")
re_prop = re.compile(r"^\s+(\w+)\s*:\s*(.+?)\s*$")

enums = set()
for ln in lineas:
    m = re_enum.match(ln)
    if m:
        enums.add(m.group(1))

clases = {}   # nombre -> (padre, {prop: enum})
actual = None
for ln in lineas:
    m = re_cls.match(ln)
    if m:
        nombre, padre = m.group(1), m.group(2)
        if nombre.startswith("Enum") and (padre in ("EnumItem", "Enum") or nombre in ("Enum", "EnumItem", "Enums")):
            actual = None
            continue
        actual = (padre, {})
        clases[nombre] = actual
        continue
    if actual is None:
        continue
    if ln.strip() == "end" and not ln.startswith("\t\t"):
        actual = None
        continue
    if re.match(r"^\s+function ", ln):
        continue
    mp = re_prop.match(ln)
    if not mp:
        continue
    prop, tipo = mp.group(1), mp.group(2)
    if tipo.startswith("RBXScriptSignal"):
        continue
    t = tipo[:-1] if tipo.endswith("?") else tipo
    if t.startswith("Enum") and t[4:] in enums:
        actual[1][prop] = t[4:]


def props_de(clase, visto=None):
    visto = visto or set()
    if clase in visto or clase not in clases:
        return {}
    visto.add(clase)
    padre, propias = clases[clase]
    r = dict(props_de(padre, visto)) if padre else {}
    r.update(propias)
    return r


filas = []
for clase in sorted(clases):
    for prop, enum in sorted(props_de(clase).items()):
        filas.append((prop, clase, enum))
filas.sort()

with open(SALIDA, "w", encoding="utf-8", newline="\n") as f:
    f.write("// GENERADO por tools/generar_enum_props.py desde dev/api/globalTypes.d.luau.\n")
    f.write("// No editar a mano: volver a generar. (F8 de la v1.17)\n")
    f.write("#ifndef GL_ENUM_PROPS_GEN_H\n#define GL_ENUM_PROPS_GEN_H\n\n")
    f.write("// Ordenado por propiedad: cada propiedad de Roblox cuyo tipo es un enum,\n")
    f.write("// por clase concreta (las heredadas ya resueltas).\n")
    f.write("struct GLEnumPropGen { const char* prop; const char* clase; const char* enumerado; };\n")
    f.write("static const GLEnumPropGen GL_ENUM_PROPS_GEN[] = {\n")
    for prop, clase, enum in filas:
        f.write(f'    {{ "{prop}", "{clase}", "{enum}" }},\n')
    f.write("};\n")
    f.write(f"static const int GL_ENUM_PROPS_GEN_N = {len(filas)};\n\n#endif // GL_ENUM_PROPS_GEN_H\n")

nombres = sorted({p for p, _, _ in filas})
print(f"{len(enums)} enums, {len(clases)} clases, {len(filas)} pares clase.propiedad, {len(nombres)} nombres -> {SALIDA}")

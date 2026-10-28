"""Ejecutables de prueba del actualizador, generados a partir de bin/rome_agente_B.exe.

- real: el autónomo tal cual (consulta GitHub de verdad).
- local: el mismo con las URL del actualizador apuntando a 127.0.0.1 (servidor falso).
- nuevo: como local, pero con ROME_VERSION subida a una versión mayor de igual longitud.
Las cadenas se cambian por otras de la misma longitud, así que no hace falta compilar.
"""
import json
from pathlib import Path
import re

import comun

PUERTO = 18765


def _constante(nombre):
    texto = (comun.ROOT / "editor/rome_update_manager.cpp").read_text(encoding="utf-8")
    return re.search(r'const char \*' + nombre + r' = "([^"]+)";', texto).group(1)


def url_local(url, sufijo):
    base = f"http://127.0.0.1:{PUERTO}/"
    relleno = len(url) - len(base) - len(sufijo)
    if relleno < 1:
        raise ValueError("URL demasiado corta para sustituirla: " + url)
    return base + "q" * relleno + sufijo


def urls():
    api_rome, api_godot, descargas = _constante("ROME_RELEASE_API"), _constante("GODOT_STABLE_API"), _constante("ROME_DOWNLOAD_ROOT")
    return {
        "api_rome": (api_rome, url_local(api_rome, "/rome-api")),
        "api_godot": (api_godot, url_local(api_godot, "/godot-api")),
        "descargas": (descargas, url_local(descargas, "/d/")),
    }


def _partes(version):
    nucleo, _, pre = version.partition("-")
    return [int(x) for x in nucleo.split(".")], pre.split(".") if pre else []


def comparar(a, b):
    (na, pa), (nb, pb) = _partes(a), _partes(b)
    if na != nb:
        return 1 if na > nb else -1
    if not pa or not pb:
        return 0 if not pa and not pb else (1 if not pa else -1)
    for x, y in zip(pa, pb):
        if x == y:
            continue
        if x.isdigit() and y.isdigit():
            return 1 if int(x) > int(y) else -1
        if x.isdigit() != y.isdigit():
            return -1 if x.isdigit() else 1
        return 1 if x > y else -1
    return (len(pa) > len(pb)) - (len(pa) < len(pb))


def version_superior(version):
    """Una versión mayor con la misma longitud, cambiando un dígito por 9."""
    posiciones = [i for i, c in enumerate(version) if c.isdigit() and c != "9"]
    for i in reversed(posiciones):
        candidata = version[:i] + "9" + version[i + 1:]
        if comparar(candidata, version) > 0:
            return candidata
    raise ValueError("No hay una versión mayor de la misma longitud que " + version)


def _clave(editor, extra):
    estado = Path(editor).stat()
    manifiesto = comun.RUNTIME / "manifest.json"
    script = comun.ROOT / "rome/package_standalone.py"
    return {"editor": [estado.st_size, estado.st_mtime_ns], "manifiesto": comun.sha256(manifiesto),
            "empaquetador": comun.sha256(script), "extra": extra}


def _construir(nombre, cambios, salida):
    claves_ruta = comun.TRABAJO / "paquete/claves.json"
    claves = json.loads(claves_ruta.read_text(encoding="utf-8")) if claves_ruta.is_file() else {}
    clave = _clave(comun.EDITOR, [list(c) for c in cambios])
    if salida.is_file() and claves.get(nombre) == clave:
        return salida
    editor = comun.EDITOR
    if cambios:
        editor = comun.TRABAJO / "paquete/editores" / (nombre + ".exe")
        comun.parchear(comun.EDITOR, editor, cambios)
    resultado = comun.empaquetar(editor, salida)
    if resultado.returncode != 0 or not salida.is_file():
        raise RuntimeError(f"Empaquetado de {nombre} falló:\n{resultado.stdout}\n{resultado.stderr}")
    claves[nombre] = clave
    claves_ruta.parent.mkdir(parents=True, exist_ok=True)
    claves_ruta.write_text(json.dumps(claves, indent=2), encoding="utf-8")
    return salida


def preparar():
    version = comun.version_rome()
    superior = version_superior(version)
    locales = [v for v in urls().values()]
    carpeta = comun.TRABAJO / "paquete"
    real = _construir("real", [], carpeta / comun.nombre_asset(version))
    local = _construir("local", locales, carpeta / "local" / comun.nombre_asset(version))
    nuevo = _construir("nuevo", locales + [(version, superior)], carpeta / "local" / comun.nombre_asset(superior))
    return {"version": version, "superior": superior, "real": real, "local": local, "nuevo": nuevo,
            "sha_local": comun.sha256(local), "sha_nuevo": comun.sha256(nuevo)}


if __name__ == "__main__":
    datos = preparar()
    for clave, valor in datos.items():
        print(f"{clave}: {valor}")

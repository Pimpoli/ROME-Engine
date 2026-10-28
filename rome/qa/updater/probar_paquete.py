"""1a. Ejecutable autónomo: primera apertura, segunda apertura y caché versionada.

python rome/qa/updater/probar_paquete.py
Abre el autónomo sin argumentos desde una carpeta que solo lo contiene a él, con
APPDATA y LOCALAPPDATA propios, y comprueba que sale el inicio de ROME y que el
runtime se extrae una sola vez a ROMEEngine/runtime/<versión>-<identidad>.
"""
import json
from pathlib import Path
import re
import shutil
import time
import zlib

import comun
import paquetes


def _cache_runtime(base):
    raiz = base / "local/ROMEEngine/runtime"
    return sorted(p for p in raiz.iterdir() if p.is_dir()) if raiz.is_dir() else []


def _abrir(exe, carpeta, entorno, log, informe, etiqueta, esperar_marca=None):
    inicio = time.monotonic()
    proceso = comun.lanzar([exe], carpeta, entorno, log)
    marca = None
    if esperar_marca:
        marca = comun.esperar(esperar_marca, 180, 0.2)
        informe.nota(etiqueta + " segundos hasta runtime.complete", round(time.monotonic() - inicio, 1) if marca else None)
    ventana = comun.esperar(lambda: [v for v in comun.ventanas([proceso.pid]) if v["titulo"]], 120, 0.5)
    informe.nota(etiqueta + " segundos hasta la ventana", round(time.monotonic() - inicio, 1))
    # Margen para que termine la consulta de actualizaciones del arranque.
    time.sleep(8)
    finales = comun.ventanas([proceso.pid])
    return proceso, marca, ventana, finales


def probar(autonomo=None):
    informe = comun.Informe("paquete")
    if autonomo:
        # Comprobar un ejecutable ya empaquetado (el que se va a publicar).
        real = Path(autonomo).resolve()
        version = comun.version_de(real)
        datos = {"version": version, "real": real}
        informe.nota("autónomo", str(real))
    else:
        datos = paquetes.preparar()
        version, real = datos["version"], datos["real"]
        informe.check(comun.version_de(comun.EDITOR) == version, "La copia del editor es la versión del árbol", version)
    asset = re.match(r"^ROME-Studio-(.+)-windows-x86_64\.exe$", real.name)
    informe.check(asset is None or asset.group(1) == version, "El nombre del archivo coincide con su versión", f"{real.name} / {version}")
    sha_archivo = real.with_name(real.name + ".sha256")
    informe.check(sha_archivo.is_file() and sha_archivo.read_text(encoding="utf-8").split()[0] == comun.sha256(real), "El .sha256 corresponde al autónomo")
    informe.check(comun.version_de(real) == version, "El autónomo arranca y dice su versión", version)
    entradas = comun.entradas_runtime(real)
    archivos = [e for e in entradas if not e.is_dir()]
    informe.check(any(e.filename == "rome_runtime/manifest.json" for e in entradas), "El autónomo lleva el runtime dentro", f"{len(archivos)} archivos")
    informe.nota("tamaño del autónomo (MB)", round(real.stat().st_size / 1048576, 1))

    experiencias_antes = comun.documentos_experiencias()
    descarga = comun.carpeta_limpia(comun.TRABAJO / "descarga")
    exe = descarga / real.name
    shutil.copy2(real, exe)
    entorno, base = comun.entorno_aislado("paquete")
    marca = lambda: next((p / "runtime.complete" for p in _cache_runtime(base) if (p / "runtime.complete").is_file()), None)

    proceso, completo, ventana, finales = _abrir(exe, descarga, entorno, comun.TRABAJO / "logs/paquete-1.log", informe, "1ª apertura", marca)
    try:
        informe.check(completo is not None, "1ª apertura extrae el runtime a la caché del usuario", str(completo.parent if completo else ""))
        titulos = [v["titulo"] for v in finales]
        informe.nota("ventanas tras abrir", titulos)
        informe.check(bool(ventana) and any("ROME" in t for t in titulos), "Sale una ventana de ROME")
        informe.check(len(finales) == 1, "Sin diálogos al abrir (solo la ventana del inicio)", titulos)
        if ventana:
            foto = comun.TRABAJO / "fotos/inicio.png"
            foto.parent.mkdir(parents=True, exist_ok=True)
            informe.nota("foto", comun.capturar(finales[0]["hwnd"], foto))
        informe.check(sorted(p.name for p in descarga.iterdir()) == [exe.name], "No aparece nada junto al ejecutable", sorted(p.name for p in descarga.iterdir()))
    finally:
        comun.cerrar(proceso)

    if completo:
        cache = completo.parent
        informe.check(cache.name.startswith(version + "-"), "La caché lleva la versión en el nombre", cache.name)
        faltan, distintos = [], []
        for entrada in archivos:
            destino = cache / entrada.filename.removeprefix("rome_runtime/")
            if not destino.is_file():
                faltan.append(entrada.filename)
            elif destino.stat().st_size != entrada.file_size or zlib.crc32(destino.read_bytes()) != entrada.CRC:
                distintos.append(entrada.filename)
        informe.check(not faltan and not distintos, "Todos los archivos del runtime extraídos íntegros", f"faltan {len(faltan)}, distintos {len(distintos)}")
        manifiesto = json.loads((cache / "manifest.json").read_text(encoding="utf-8"))
        malos = [r for r, h in manifiesto.get("files", {}).items() if not (cache / "project" / r).is_file() or comun.sha256(cache / "project" / r) != h]
        informe.check(not malos, "Los archivos del proyecto coinciden con el manifiesto", f"{len(malos)} distintos")
        temporales = [str(p) for p in cache.rglob("*.tmp")]
        informe.check(not temporales, "Sin temporales en la caché", len(temporales))
        sello = (completo.stat().st_mtime_ns, (cache / "manifest.json").stat().st_mtime_ns)

        proceso, _, ventana, finales = _abrir(exe, descarga, entorno, comun.TRABAJO / "logs/paquete-2.log", informe, "2ª apertura")
        comun.cerrar(proceso)
        informe.check(bool(ventana), "2ª apertura abre el inicio")
        informe.check((completo.stat().st_mtime_ns, (cache / "manifest.json").stat().st_mtime_ns) == sello and len(_cache_runtime(base)) == 1, "2ª apertura no vuelve a extraer")

        if autonomo:
            informe.check(comun.documentos_experiencias() == experiencias_antes, "No se crearon experiencias en Documentos")
            return informe.guardar()
        # Un archivo distinto con el mismo manifest.json (p. ej. otra imagen del inicio).
        variante = comun.carpeta_limpia(comun.TRABAJO / "runtime_variante")
        shutil.copytree(comun.RUNTIME, variante, dirs_exist_ok=True)
        (variante / "hub" / "qa_variante.txt").write_text("B", encoding="utf-8")
        exe_variante = descarga / "ROME Variante.exe"
        salida = comun.empaquetar(comun.EDITOR, exe_variante, variante)
        informe.check(salida.returncode == 0, "Empaqueta una variante con un archivo distinto", salida.stderr.strip()[-300:])
        proceso, _, _, _ = _abrir(exe_variante, descarga, entorno, comun.TRABAJO / "logs/paquete-3.log", informe, "variante", lambda: len(_cache_runtime(base)) == 2 and all((p / "runtime.complete").is_file() for p in _cache_runtime(base)))
        comun.cerrar(proceso)
        nombres = [p.name for p in _cache_runtime(base)]
        informe.check(len(nombres) == 2 and cache.name in nombres, "Cambiar un archivo del runtime (sin tocar manifest.json) usa otra carpeta y conserva la anterior", nombres)

        exe_nuevo = descarga / datos["nuevo"].name
        shutil.copy2(datos["nuevo"], exe_nuevo)
        informe.check(comun.version_de(exe_nuevo) == datos["superior"], "El ejecutable con la versión subida dice " + datos["superior"])
        proceso, _, _, _ = _abrir(exe_nuevo, descarga, entorno, comun.TRABAJO / "logs/paquete-4.log", informe, "versión nueva", lambda: any(p.name.startswith(datos["superior"] + "-") and (p / "runtime.complete").is_file() for p in _cache_runtime(base)))
        comun.cerrar(proceso)
        nombres = [p.name for p in _cache_runtime(base)]
        informe.check(any(n.startswith(datos["superior"] + "-") for n in nombres), "Una versión nueva usa su propia carpeta de runtime", nombres)

        # La versión nueva limpia los runtimes de versiones anteriores a los 30 s.
        proceso = comun.lanzar([exe_nuevo], descarga, entorno, comun.TRABAJO / "logs/paquete-5.log")
        limpio = comun.esperar(lambda: all(p.name.startswith(datos["superior"] + "-") for p in _cache_runtime(base)), 75, 1)
        comun.cerrar(proceso)
        nombres = [p.name for p in _cache_runtime(base)]
        informe.check(limpio, "La versión nueva borra los runtimes de versiones anteriores", nombres)
        informe.nota("carpetas de runtime tras la prueba", nombres)

    informe.check(comun.documentos_experiencias() == experiencias_antes, "No se crearon experiencias en Documentos")
    return informe.guardar()


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--autonomo", help="Ejecutable ya empaquetado que se quiere comprobar (por defecto se empaqueta bin/rome_agente_B.exe)")
    raise SystemExit(probar(parser.parse_args().autonomo))

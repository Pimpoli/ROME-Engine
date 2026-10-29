"""Prueba la release publicada en GitHub con el actualizador de verdad.

Una copia del editor recién compilado que dice ser la versión anterior abre una
experiencia con APPDATA y LOCALAPPDATA propios. Tiene que encontrar en GitHub
la versión de core/version.h, descargarla, comprobar su SHA-256 e instalarla al
cerrar. Al volver a abrir ya es la versión nueva y no pide actualizar otra vez.
Correr solo tras publicar.

Por defecto el ejecutable publicado (unos 180 MB) se baja antes, sin ventanas, y
se deja en la caché de la copia: el actualizador lo da por bueno solo si su
tamaño y su SHA-256 coinciden con los que publica GitHub. Con --descarga-propia
lo descarga el propio actualizador. Ojo: hacia GitHub se han visto 160 KB/s
sostenidos y cortes a los ~15 min, así que puede no terminar.

    python rome/qa/updater/probar_release.py [--anterior 0.1.0-alpha.1] [--descarga-propia]
"""
import argparse
import http.client
import json
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import comun  # noqa: E402

TRABAJO = comun.TRABAJO / "release"
COMPILADO = comun.BIN / "godot.windows.editor.x86_64.exe"
INSTALADO = TRABAJO / "instalado/ROME Studio.exe"
PROYECTO = TRABAJO / "proyecto"
API = "https://api.github.com/repos/Pimpoli/ROME-Engine/releases?per_page=20"


def anterior_de(version):
    cabeza, _, numero = version.rpartition(".")
    if not numero.isdigit() or int(numero) == 0 or len(str(int(numero) - 1)) != len(numero):
        raise SystemExit("Indica --anterior: una versión menor con la misma longitud")
    return f"{cabeza}.{int(numero) - 1}"


def asset_publicado(version):
    peticion = urllib.request.Request(API, headers={"Accept": "application/vnd.github+json", "User-Agent": "ROME-QA"})
    with urllib.request.urlopen(peticion, timeout=60) as respuesta:
        for release in json.load(respuesta):
            if release.get("tag_name") == "v" + version and not release.get("draft"):
                for asset in release.get("assets", []):
                    if asset.get("name") == comun.nombre_asset(version):
                        return asset
    return None


def bajar(url, ruta, total):
    """GitHub corta las descargas lentas a los ~15 min: se sigue desde donde quedó."""
    ruta.parent.mkdir(parents=True, exist_ok=True)
    temporal = ruta.with_name(ruta.name + ".part")
    for _ in range(10):
        hecho = temporal.stat().st_size if temporal.is_file() else 0
        if hecho >= total:
            break
        cabeceras = {"User-Agent": "ROME-QA"}
        if hecho:
            cabeceras["Range"] = f"bytes={hecho}-"
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=cabeceras), timeout=120) as respuesta:
                with open(temporal, "ab" if hecho and respuesta.status == 206 else "wb") as archivo:
                    shutil.copyfileobj(respuesta, archivo, 1 << 20)
        except (OSError, http.client.HTTPException):
            continue
    temporal.replace(ruta)


def preparar_proyecto():
    comun.borrar(PROYECTO)
    subprocess.run(["robocopy", str(comun.BIN / "rome_update_qa"), str(PROYECTO), "/E", "/NFL", "/NDL", "/NJH", "/NJS", "/NP",
                    "/XF", "integration-result.json"], capture_output=True)
    ajustes = PROYECTO / "project.godot"
    ajustes.write_text(ajustes.read_text(encoding="utf-8").replace("res://addons/ROMESmoke/plugin.cfg", "res://addons/ROMEUpdaterQA/plugin.cfg"), encoding="utf-8")
    shutil.copytree(Path(__file__).with_name("ROMEUpdaterQA"), PROYECTO / "addons/ROMEUpdaterQA", dirs_exist_ok=True)


def resultado():
    ruta = PROYECTO / "updater_qa_result.json"
    try:
        return json.loads(ruta.read_text(encoding="utf-8")) if ruta.is_file() else {}
    except ValueError:
        return {}


def abrir(entorno, nombre, segundos):
    (PROYECTO / "updater_qa_result.json").unlink(missing_ok=True)
    proceso = comun.lanzar([INSTALADO, "--editor", "--path", PROYECTO], PROYECTO, entorno, TRABAJO / f"logs/{nombre}.log")
    comun.esperar(lambda: proceso.poll() is not None, segundos, 1)
    return comun.cerrar(proceso)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--anterior", help="Versión que finge la copia (misma longitud que la publicada)")
    parser.add_argument("--descarga-propia", action="store_true", help="Que el actualizador baje el ejecutable él mismo")
    args = parser.parse_args()
    version = comun.version_rome()
    anterior = args.anterior or anterior_de(version)
    informe = comun.Informe("release")

    asset = asset_publicado(version)
    if not informe.check(asset is not None and str(asset.get("digest", "")).startswith("sha256:"), f"GitHub publica {comun.nombre_asset(version)} con su huella"):
        return informe.guardar()
    huella = asset["digest"].removeprefix("sha256:")

    comun.matar_ejecutable(INSTALADO)
    comun.carpeta_limpia(INSTALADO.parent)
    copia = TRABAJO / "editor-anterior.exe"
    comun.parchear(COMPILADO, copia, [(version, anterior)])
    salida = comun.empaquetar(copia, INSTALADO)
    INSTALADO.with_name(INSTALADO.name + ".sha256").unlink(missing_ok=True)
    if not informe.check(salida.returncode == 0 and comun.version_de(INSTALADO) == anterior, f"Se arma una copia que dice ser la {anterior}", salida.stderr.strip()[-300:]):
        return informe.guardar()
    preparar_proyecto()
    espera = 1500 if args.descarga_propia else 120
    (PROYECTO / "updater_qa.json").write_text(json.dumps({"etapa": "al_cerrar", "version_nueva": version, "espera_descarga": espera}), encoding="utf-8")
    entorno, base = comun.entorno_aislado("release")
    descarga = base / "local/ROMEEngine/updates" / version / comun.nombre_asset(version)
    if not args.descarga_propia:
        guardada = TRABAJO / "descarga" / comun.nombre_asset(version)
        if comun.sha256_o_nada(guardada) != huella:
            bajar(asset["browser_download_url"], guardada, int(asset["size"]))
        if not informe.check(comun.sha256_o_nada(guardada) == huella, "Lo que sirve GitHub coincide con su huella publicada"):
            return informe.guardar()
        descarga.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(guardada, descarga)

    codigo = abrir(entorno, "anterior", espera + 300)
    datos = resultado()
    estado = datos.get("descarga", {}).get("estado", {})
    informe.nota("estado tras descargar", {k: estado.get(k) for k in ("rome_status", "verified", "action_error")})
    informe.check(datos.get("inicio", {}).get("version") == anterior, f"La copia arranca como {anterior}")
    informe.check(estado.get("verified") is True, f"Encuentra la {version} en GitHub y su descarga cuadra con el tamaño y la huella publicados")
    informe.check(comun.sha256_o_nada(descarga) == huella, "Lo descargado es el ejecutable publicado", descarga.name)
    informe.check(codigo == 0, "El editor se cierra solo, sin tener que matarlo", codigo)
    instalado = comun.esperar(lambda: comun.sha256_o_nada(INSTALADO) == huella, 120, 1)
    informe.check(instalado, "Al cerrar, se instala la versión publicada en lugar de la copia")
    informe.check(comun.version_de(INSTALADO, entorno) == version, f"El ejecutable instalado dice {version}")
    informe.check(sorted(p.name for p in INSTALADO.parent.iterdir()) == [INSTALADO.name], "No quedan copias ni temporales junto al ejecutable", sorted(p.name for p in INSTALADO.parent.iterdir()))

    abrir(entorno, "nueva", 240)
    datos = resultado()
    final = datos.get("reinicio_final", {})
    informe.check(datos.get("reinicio", {}).get("version") == version, f"Al volver a abrir ya es la {version}")
    informe.check(final.get("rome_status") == "current", "Con la versión publicada no pide actualizar otra vez", final.get("rome_status"))
    informe.check(not descarga.exists(), "La versión nueva borra la descarga")
    return informe.guardar()


if __name__ == "__main__":
    sys.exit(main())

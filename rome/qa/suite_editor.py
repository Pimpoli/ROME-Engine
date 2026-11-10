"""Suite del editor: deja lista la experiencia desechable, lanza smoke.gd y dice si pasó.

python rome/qa/suite_editor.py [editor.exe] [--sin-ventana]
La experiencia y sus ajustes viven en bin/rome_fallos_qa, con APPDATA y
LOCALAPPDATA propios. Abre una ventana de ROME unos minutos: no hay que tocarla.
Con --sin-ventana corre en headless, que sirve de avance pero no dibuja.
Antes hay que compilar y pasar rome/package_runtime.py --rome-only, porque la
experiencia toma ROMEStudio de bin/rome_runtime.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parents[2]
RUTAS = [argumento for argumento in sys.argv[1:] if not argumento.startswith("--")]
EDITOR = Path(RUTAS[0]).resolve() if RUTAS else ROOT / "bin/godot.windows.editor.x86_64.exe"
TRABAJO = ROOT / "bin/rome_fallos_qa"
# La experiencia con la que pasó la suite el 07/11; solo se copia la primera vez.
ORIGEN = ROOT / "bin/rome_studio_layout_qa"


def preparar():
    experiencia = TRABAJO / "integration"
    if not experiencia.is_dir():
        if not (ORIGEN / "integration/project.godot").is_file():
            sys.exit(f"Falta la experiencia de pruebas: crea una en {experiencia} como dice CONTRIBUTING.md.")
        shutil.copytree(ORIGEN / "integration", experiencia)
        shutil.copytree(ORIGEN / "integration_settings", TRABAJO / "integration_settings")
    pruebas = experiencia / "addons/ROMESmoke"
    pruebas.mkdir(parents=True, exist_ok=True)
    for archivo in (ROOT / "rome/qa").iterdir():
        if archivo.suffix in (".gd", ".cfg"):
            shutil.copy2(archivo, pruebas / archivo.name)
    return experiencia


def main():
    experiencia = preparar()
    resultado = experiencia / "integration-result.json"
    resultado.unlink(missing_ok=True)
    log = TRABAJO / "integration.log"
    log.unlink(missing_ok=True)
    entorno = os.environ.copy()
    entorno["APPDATA"] = str(TRABAJO / "integration_settings/roaming")
    entorno["LOCALAPPDATA"] = str(TRABAJO / "integration_settings/local")
    comando = [str(EDITOR), "--editor", "--path", str(experiencia), "--max-fps", "60", "--quit-after", "40000", "--log-file", str(log)]
    if "--sin-ventana" in sys.argv:
        comando.insert(1, "--headless")
    try:
        codigo = subprocess.run(comando, env=entorno, timeout=1500).returncode
    except subprocess.TimeoutExpired:
        codigo = "más de 25 minutos"

    texto = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
    datos = json.loads(resultado.read_text(encoding="utf-8")) if resultado.is_file() else None
    fallos = []
    if codigo != 0:
        fallos.append(f"el editor salió con {codigo}")
    if datos is None:
        fallos.append("no hay integration-result.json")
    elif not datos.get("passed"):
        fallos.append("falló: " + datos["failed"] if datos.get("failed") else "la suite se cortó antes de terminar")
    if "SCRIPT ERROR" in texto:
        fallos.append("hay SCRIPT ERROR en el log")

    secciones = [linea[len("QA section: "):] for linea in texto.splitlines() if linea.startswith("QA section: ")]
    print("secciones: " + ", ".join(secciones))
    errores = sorted({linea.strip()[:220] for linea in texto.splitlines() if linea.startswith(("ERROR:", "SCRIPT ERROR:"))})
    for linea in errores[:12]:
        print("  " + linea)
    print(f"errores distintos en el log: {len(errores)} ({log})")
    hechas = len(datos.get("checks", [])) if datos else 0
    if fallos:
        print(f"FALLO tras {hechas} comprobaciones: " + "; ".join(fallos))
        return 1
    print(f"PASÓ: {hechas} comprobaciones")
    return 0


if __name__ == "__main__":
    sys.exit(main())

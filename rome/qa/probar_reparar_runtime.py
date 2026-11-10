"""Caché del runtime: un archivo que falta se repone aunque siga la marca de completo.

python rome/qa/probar_reparar_runtime.py [editor.exe]
Empaqueta un autónomo con ese editor y lo abre sin ventana, con APPDATA y
LOCALAPPDATA propios, para que extraiga el runtime. Luego borra un archivo de
la caché, como haría un antivirus, y lo abre otra vez. Todo queda en
bin/rome_fallos_qa/runtime.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parents[2]
EDITOR = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "bin/godot.windows.editor.x86_64.exe"
TRABAJO = ROOT / "bin/rome_fallos_qa/runtime"
PERDIDO = "GodotLuau/godot_luau.gdextension"


def abrir(exe, entorno, nombre):
    log = TRABAJO / (nombre + ".log")
    comando = [str(exe), "--headless", "--project-manager", "--quit-after", "120", "--log-file", str(log)]
    subprocess.run(comando, cwd=exe.parent, env=entorno, timeout=600)
    return log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""


def check(valor, texto):
    print(("OK    " if valor else "FALLO ") + texto, flush=True)
    return bool(valor)


def main():
    shutil.rmtree(TRABAJO, ignore_errors=True)
    exe = TRABAJO / "descarga/ROME-prueba.exe"
    exe.parent.mkdir(parents=True)
    empaquetado = subprocess.run([sys.executable, str(ROOT / "rome/package_standalone.py"), "--editor", str(EDITOR), "--runtime", str(ROOT / "bin/rome_runtime"), "--output", str(exe)], capture_output=True, text=True, timeout=900)
    if not check(empaquetado.returncode == 0 and exe.is_file(), "Se empaqueta un autónomo con el editor"):
        print(empaquetado.stdout[-600:] + empaquetado.stderr[-600:])
        return 1
    entorno = os.environ.copy()
    for clave, carpeta in (("APPDATA", "roaming"), ("LOCALAPPDATA", "local")):
        (TRABAJO / carpeta).mkdir()
        entorno[clave] = str(TRABAJO / carpeta)

    abrir(exe, entorno, "primera")
    marcas = list((TRABAJO / "local/ROMEEngine/runtime").glob("*/runtime.complete"))
    if not check(len(marcas) == 1, "La primera apertura extrae el runtime a la caché"):
        return 1
    cache = marcas[0].parent
    esperado = json.loads((cache / "manifest.json").read_text(encoding="utf-8"))["files"][PERDIDO]
    perdido = cache / "project" / PERDIDO
    perdido.unlink()
    abrir(exe, entorno, "segunda")
    repuesto = perdido.is_file() and hashlib.sha256(perdido.read_bytes()).hexdigest() == esperado
    bien = check(repuesto, "Un archivo que falta en la caché se repone al abrir, aunque siga la marca")
    bien = check(marcas[0].is_file(), "La caché vuelve a quedar marcada como completa") and bien
    return 0 if bien else 1


if __name__ == "__main__":
    sys.exit(main())

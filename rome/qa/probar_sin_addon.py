"""Experiencia con ROMEStudio desactivado: el editor avisa en vez de callar.

python rome/qa/probar_sin_addon.py [editor.exe]
Copia la experiencia de pruebas sin ningún plugin activo y la abre unos
segundos, con ventana porque el aviso no sale en headless. Mira el log; en la
ventana tiene que verse el diálogo «ROME Studio» con el botón Reactivar.
Todo queda en bin/rome_fallos_qa/sin_addon.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parents[2]
EDITOR = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "bin/godot.windows.editor.x86_64.exe"
ORIGEN = ROOT / "bin/rome_studio_layout_qa"
TRABAJO = ROOT / "bin/rome_fallos_qa/sin_addon"


def main():
    shutil.rmtree(TRABAJO, ignore_errors=True)
    shutil.copytree(ORIGEN / "integration", TRABAJO / "proyecto")
    shutil.copytree(ORIGEN / "integration_settings", TRABAJO / "ajustes")
    proyecto = TRABAJO / "proyecto/project.godot"
    texto = proyecto.read_text(encoding="utf-8")
    proyecto.write_text(re.sub(r"enabled=PackedStringArray\([^)]*\)", "enabled=PackedStringArray()", texto), encoding="utf-8")
    entorno = os.environ.copy()
    entorno["APPDATA"] = str(TRABAJO / "ajustes/roaming")
    entorno["LOCALAPPDATA"] = str(TRABAJO / "ajustes/local")
    log = TRABAJO / "editor.log"
    comando = [str(EDITOR), "--editor", "--path", str(TRABAJO / "proyecto"), "--max-fps", "60", "--quit-after", "900", "--log-file", str(log)]
    subprocess.run(comando, env=entorno, timeout=600)
    salida = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
    avisa = "ROMEStudio está desactivado" in salida
    print(("OK    " if avisa else "FALLO ") + "Sin ROMEStudio activo, el editor lo dice al abrir la experiencia")
    return 0 if avisa else 1


if __name__ == "__main__":
    sys.exit(main())

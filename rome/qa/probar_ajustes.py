"""Primer arranque: lo que se elige en la primera sesión sigue en la segunda.

python rome/qa/probar_ajustes.py [editor.exe]
Abre el editor dos veces sin ventana, con APPDATA y LOCALAPPDATA propios y un
proyecto vacío. Su plugin pone el espaciado en «Spacious» la primera vez y dice
cuál encuentra la segunda. Todo queda en bin/rome_fallos_qa/ajustes.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parents[2]
EDITOR = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "bin/godot.windows.editor.x86_64.exe"
TRABAJO = ROOT / "bin/rome_fallos_qa/ajustes"

PROYECTO = """config_version=5

[application]

config/name="QA Ajustes"

[editor_plugins]

enabled=PackedStringArray("res://addons/qa_ajustes/plugin.cfg")
"""

PLUGIN_CFG = """[plugin]

name="QA Ajustes"
description=""
author=""
version="1"
script="plugin.gd"
"""

PLUGIN = """@tool
extends EditorPlugin

func _enter_tree() -> void:
\tvar ajustes := EditorInterface.get_editor_settings()
\tprint("QA espaciado=", ajustes.get_setting("interface/theme/spacing_preset"))
\tif OS.get_environment("QA_PRIMERA") == "1":
\t\tajustes.set_setting("interface/theme/spacing_preset", "Spacious")
\tget_tree().quit.call_deferred()
"""


def abrir(entorno, nombre):
    log = TRABAJO / (nombre + ".log")
    comando = [str(EDITOR), "--headless", "--editor", "--path", str(TRABAJO / "proyecto"), "--quit-after", "3000", "--log-file", str(log)]
    subprocess.run(comando, env=entorno, timeout=300)
    return log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""


def check(valor, texto):
    print(("OK    " if valor else "FALLO ") + texto, flush=True)
    return bool(valor)


def main():
    shutil.rmtree(TRABAJO, ignore_errors=True)
    addon = TRABAJO / "proyecto/addons/qa_ajustes"
    addon.mkdir(parents=True)
    (TRABAJO / "proyecto/project.godot").write_text(PROYECTO, encoding="utf-8")
    (addon / "plugin.cfg").write_text(PLUGIN_CFG, encoding="utf-8")
    (addon / "plugin.gd").write_text(PLUGIN, encoding="utf-8")
    entorno = os.environ.copy()
    for clave, carpeta in (("APPDATA", "roaming"), ("LOCALAPPDATA", "local")):
        (TRABAJO / carpeta).mkdir()
        entorno[clave] = str(TRABAJO / carpeta)

    entorno["QA_PRIMERA"] = "1"
    primera = abrir(entorno, "primera")
    guardados = list((TRABAJO / "roaming").rglob("editor_settings-*.tres"))
    ajustes = guardados[0].read_text(encoding="utf-8") if guardados else ""
    entorno["QA_PRIMERA"] = "0"
    segunda = abrir(entorno, "segunda")

    bien = check("QA espaciado=Compact" in primera, "La instalación nueva arranca con el espaciado compacto")
    bien = check('spacing_preset = "Spacious"' in ajustes, "El cambio de la primera sesión se guarda") and bien
    bien = check("rome/studio/appearance_revision = 3" in ajustes, "La instalación nueva nace con la apariencia ya migrada") and bien
    bien = check("QA espaciado=Spacious" in segunda, "La segunda sesión respeta el espaciado elegido en la primera") and bien
    return 0 if bien else 1


if __name__ == "__main__":
    sys.exit(main())

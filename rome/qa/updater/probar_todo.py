"""Arnés completo del actualizador y del ejecutable autónomo (unos 20-25 min).

python rome/qa/updater/probar_todo.py
Abre ventanas del editor (no hace falta tocarlas) y no publica nada: un servidor
local hace de GitHub. Con otro editor: set ROME_QA_EDITOR=<ruta del editor.exe>
(por defecto bin/rome_agente_B.exe, con su .console.exe al lado).
Resultados: bin/rome_agente_B_updater/<parte>-resultado.json y logs/.
"""
import json
from pathlib import Path
import subprocess
import sys

import comun

PARTES = ["probar_scripts.py", "probar_paquete.py", "probar_ayudante.py", "probar_editor.py", "probar_experiencias.py"]


def main():
    carpeta = Path(__file__).parent
    resumen = []
    for parte in PARTES:
        print(f"===== {parte}", flush=True)
        codigo = subprocess.run([sys.executable, str(carpeta / parte)]).returncode
        nombre = parte.removeprefix("probar_").removesuffix(".py")
        resultado = comun.TRABAJO / f"{nombre}-resultado.json"
        datos = json.loads(resultado.read_text(encoding="utf-8")) if resultado.is_file() else {"checks": [], "errores": ["sin resultado"]}
        resumen.append((nombre, len(datos["checks"]), datos["errores"], codigo))
    print("===== resumen", flush=True)
    for nombre, bien, errores, codigo in resumen:
        print(f"{nombre}: {bien} OK, {len(errores)} fallos (salida {codigo})")
        for error in errores:
            print(f"   - {error}")
    return 1 if any(errores for _, _, errores, _ in resumen) else 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Scripts de empaquetado: nombre y versión, salida en uso y GodotLuau sin auto-actualizarse.

python rome/qa/updater/probar_scripts.py
No hace falta compilar: usa bin/rome_agente_B.exe y una copia de bin/rome_update_qa.
"""
import importlib.util
import subprocess

import comun
import paquetes

INFORME = comun.Informe("scripts")
CARPETA = comun.TRABAJO / "scripts"


def _modulo(nombre):
    especificacion = importlib.util.spec_from_file_location(nombre, comun.ROOT / "rome" / (nombre + ".py"))
    modulo = importlib.util.module_from_spec(especificacion)
    especificacion.loader.exec_module(modulo)
    return modulo


def nombre_y_version():
    version = comun.version_rome()
    otra = paquetes.version_superior(version)
    salida = CARPETA / comun.nombre_asset(otra)
    resultado = comun.empaquetar(comun.EDITOR, salida)
    INFORME.check(resultado.returncode != 0 and "ROME_VERSION" in (resultado.stdout + resultado.stderr), "Un nombre de versión que no es la del editor se rechaza", (resultado.stdout + resultado.stderr).strip()[-160:])
    INFORME.check(not salida.exists() and not salida.with_name(salida.name + ".partial").exists(), "Al rechazarlo no deja archivos")


def salida_en_uso():
    salida = CARPETA / "ROME Ocupado.exe"
    salida.write_bytes(b"anterior")
    candado = comun.bloquear(salida)
    try:
        resultado = comun.empaquetar(comun.EDITOR, salida)
    finally:
        comun.soltar(candado)
    INFORME.check(resultado.returncode != 0 and "open" in (resultado.stdout + resultado.stderr), "Si el ejecutable de salida está abierto, avisa", (resultado.stdout + resultado.stderr).strip()[-120:])
    INFORME.check(salida.read_bytes() == b"anterior" and not salida.with_name(salida.name + ".partial").exists(), "El ejecutable anterior queda intacto y sin temporales")


def luau_sin_auto_actualizar():
    proyecto = CARPETA / "proyecto_luau"
    comun.borrar(proyecto)
    subprocess.run(["robocopy", str(comun.BIN / "rome_update_qa"), str(proyecto), "/E", "/NFL", "/NDL", "/NJH", "/NJS", "/NP", "/XF", "integration-result.json"], capture_output=True)
    ajustes = proyecto / "project.godot"
    ajustes.write_text(ajustes.read_text(encoding="utf-8").replace('"res://addons/ROMESmoke/plugin.cfg", ', ""), encoding="utf-8")
    runtime = _modulo("package_runtime")
    runtime.keep_luau_with_rome(proyecto)
    runtime.keep_luau_with_rome(proyecto)
    texto = (proyecto / "addons/GodotLuauUpdater/plugin_actualizador.gd").read_text(encoding="utf-8")
    INFORME.check(texto.count(runtime.ROME_GUARD) == 2, "El actualizador propio de GodotLuau queda bloqueado en ROME (y aplicarlo dos veces no lo duplica)")
    entorno, _ = comun.entorno_aislado("scripts-luau")
    salida = subprocess.run([str(comun.CONSOLA),"--headless", "--editor", "--path", str(proyecto), "--quit-after", "600"], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=600, env=entorno)
    log = (CARPETA / "luau.log")
    log.write_text(salida.stdout + salida.stderr, encoding="utf-8")
    lineas = (salida.stdout + salida.stderr).splitlines()
    errores = [lineas[i - 1].strip() + " | " + l.strip() for i, l in enumerate(lineas) if "plugin_actualizador.gd" in l]
    INFORME.check(not errores, "El editor carga el actualizador de GodotLuau modificado sin errores", errores[:3])
    INFORME.check(any("GodotLuau" in l for l in lineas), "La prueba llegó a cargar GodotLuau", f"{len(lineas)} líneas en {log}")


def probar():
    CARPETA.mkdir(parents=True, exist_ok=True)
    for prueba in (nombre_y_version, salida_en_uso, luau_sin_auto_actualizar):
        print(f"--- {prueba.__name__}", flush=True)
        try:
            prueba()
        except Exception as error:
            INFORME.check(False, f"{prueba.__name__}: terminó con una excepción", repr(error))
    return INFORME.guardar()


if __name__ == "__main__":
    raise SystemExit(probar())

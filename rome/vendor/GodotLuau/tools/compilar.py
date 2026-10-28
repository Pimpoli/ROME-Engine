#!/usr/bin/env python3
"""Compila la extension: debug y release A LA VEZ, con el toolchain ya puesto.

Uso (desde cualquier sitio):
    python tools/compilar.py                  debug + release en paralelo
    python tools/compilar.py debug            solo debug (la que carga el editor)
    python tools/compilar.py release
    python tools/compilar.py build_ia=yes     lo que lleve "=" va tal cual a SCons

Por que en paralelo: un build normal son 6 TU grandes por target (ver
src/core/gl_cabeceras.h), y 6 solos no llenan los 20 hilos. Con los dos
targets a la vez se compilan 12 y el total baja (medido: 36 s los dos, 28 s
solo debug, tras tocar un header). Se puede porque cada target tiene sus
objetos y su .sconsign (ver SConstruct).

La salida completa de cada target queda en %TEMP%/godotluau_compilar_<target>.log.
"""
import os
import subprocess
import sys
import tempfile
import time

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MSYS = r"C:\msys64"
TARGETS = {"debug": "template_debug", "release": "template_release"}


def python_con_scons():
    # El de MSYS2 trae SCons por pacman; si no esta, el que ejecuta este script.
    msys_py = os.path.join(MSYS, "ucrt64", "bin", "python.exe")
    return msys_py if os.path.exists(msys_py) else sys.executable


def entorno():
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([os.path.join(MSYS, "ucrt64", "bin"),
                                   os.path.join(MSYS, "usr", "bin"), env.get("PATH", "")])
    env["MSYSTEM"] = "UCRT64"
    # Con MINGW_PREFIX puesto, godot-cpp arma CXX como "<prefijo>/bin/-w64-mingw32-g++",
    # que no existe.
    env.pop("MINGW_PREFIX", None)
    env["PYTHONIOENCODING"] = "utf-8"
    return env


def resumen_error(log):
    with open(log, encoding="utf-8", errors="replace") as f:
        lineas = f.read().splitlines()
    errores = [l for l in lineas if "error" in l.lower() and "error_macros" not in l]
    return (errores or lineas)[-30:]


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except AttributeError:
        pass
    pedidos = [a for a in sys.argv[1:] if a in TARGETS]
    extra = [a for a in sys.argv[1:] if "=" in a]
    raros = [a for a in sys.argv[1:] if a not in TARGETS and "=" not in a]
    if raros:
        print("No entiendo: " + " ".join(raros))
        print(__doc__)
        return 2
    pedidos = pedidos or list(TARGETS)
    # Cada SCons se queda con su parte de los nucleos (godot-cpp cogeria todos).
    jobs = max(2, (os.cpu_count() or 4) // len(pedidos))

    procesos = {}
    for nombre in pedidos:
        log = os.path.join(tempfile.gettempdir(), "godotluau_compilar_%s.log" % nombre)
        cmd = [python_con_scons(), "-m", "SCons", "platform=windows", "use_mingw=yes",
               "target=" + TARGETS[nombre], "-j%d" % jobs] + extra
        salida = open(log, "w", encoding="utf-8")
        p = subprocess.Popen(cmd, cwd=RAIZ, env=entorno(), stdout=salida, stderr=subprocess.STDOUT)
        procesos[nombre] = (p, salida, log, time.time())
    print("Compilando %s (-j%d cada uno)..." % (" + ".join(pedidos), jobs))

    fallos = 0
    pendientes = dict(procesos)
    while pendientes:
        for nombre, (p, salida, log, t0) in list(pendientes.items()):
            if p.poll() is None:
                continue
            salida.close()
            del pendientes[nombre]
            segundos = time.time() - t0
            if p.returncode == 0:
                with open(log, encoding="utf-8", errors="replace") as f:
                    al_dia = not any(l.startswith(("Compiling", "Linking")) for l in f)
                print("✓ %-7s %4.0f s%s" % (nombre, segundos, "  (ya estaba al dia)" if al_dia else ""))
            else:
                fallos += 1
                print("✗ %-7s %4.0f s  FALLO. Log: %s" % (nombre, segundos, log))
                for linea in resumen_error(log):
                    print("    " + linea)
        time.sleep(0.5)
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())

"""1b. Ayudante de actualización con dos ejecutables reales, sin publicar nada.

python rome/qa/updater/probar_ayudante.py [caso ...]
El "instalado" es el autónomo local y el "nuevo" el mismo con la versión subida.
El ayudante se lanza igual que lo hace el editor:
  nuevo.exe --headless --path <staging> -- --rome-update-helper --target ... --wait-pid ...
Cada caso usa su carpeta y sus APPDATA/LOCALAPPDATA dentro de bin/rome_agente_B_updater.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from types import SimpleNamespace

import comun
import paquetes

DATOS = None
INFORME = comun.Informe("ayudante")


def caso(nombre, datos, destino_nombre="ROME Studio.exe"):
    carpeta = comun.carpeta_limpia(comun.TRABAJO / "ayudante" / nombre)
    instalado = carpeta / "instalado"
    instalado.mkdir()
    target = instalado / destino_nombre
    shutil.copy2(datos["local"], target)
    entorno, base = comun.entorno_aislado("ayudante-" + nombre)
    descarga = base / "local/ROMEEngine/updates" / datos["superior"]
    descarga.mkdir(parents=True)
    nuevo = descarga / datos["nuevo"].name
    shutil.copy2(datos["nuevo"], nuevo)
    staging = descarga / "helper-qa"
    staging.mkdir()
    ticket = staging / "apply.ticket"
    ticket.write_text(datos["sha_nuevo"], encoding="utf-8")
    soporta_estado = b"--status-file" in Path(nuevo).read_bytes()
    return SimpleNamespace(nombre=nombre, carpeta=carpeta, instalado=instalado, target=target, nuevo=nuevo, staging=staging,
                           ticket=ticket, entorno=entorno, base=base, estado=staging / "status.json", soporta_estado=soporta_estado)


def ayudante(c, pid, extra=(), sha_nuevo=None, sha_original=None, target=None):
    comando = [c.nuevo, "--headless", "--path", c.staging, "--", "--rome-update-helper", "--target", target or c.target,
               "--wait-pid", pid, "--sha256", sha_nuevo or DATOS["sha_nuevo"], "--original-sha256", sha_original or DATOS["sha_local"],
               "--cancel-file", c.ticket, *extra]
    if c.soporta_estado:
        comando += ["--status-file", c.estado]
    c.inicio = time.monotonic()
    return comun.lanzar(comando, c.carpeta, c.entorno, c.carpeta / "ayudante.log")


def durmiente():
    return subprocess.Popen([sys.executable, "-c", "import time; time.sleep(900)"])


def terminar(proceso, segundos):
    try:
        proceso.wait(segundos)
    except subprocess.TimeoutExpired:
        comun.cerrar(proceso)
        return None
    comun.cerrar(proceso)
    return proceso.returncode


def restos(c):
    return sorted(p.name for p in c.instalado.iterdir() if p.name.endswith(".tmp") or ".rome-update-" in p.name or ".rome-rollback-" in p.name)


def copias(c):
    candidatas = list(c.instalado.glob("*.bak")) + list((c.base / "local/ROMEEngine").rglob("*.bak"))
    return [p for p in candidatas if comun.sha256(p) == DATOS["sha_local"]]


def estado(c):
    if not c.estado.is_file():
        return {}
    try:
        return json.loads(c.estado.read_text(encoding="utf-8"))
    except ValueError:
        return {"ilegible": True}


def reiniciados(c, target=None):
    return comun.esperar(lambda: comun.procesos_de(target or c.target), 30, 1) or []


def comprobar_intacto(c, etiqueta, codigo, esperado):
    INFORME.check(codigo == esperado, f"{etiqueta}: código {esperado}", f"salió {codigo}")
    INFORME.check(comun.sha256(c.target) == DATOS["sha_local"], f"{etiqueta}: el instalado sigue intacto")
    INFORME.check(not restos(c), f"{etiqueta}: sin temporales junto al ejecutable", restos(c))
    if c.soporta_estado:
        datos = estado(c)
        INFORME.check(datos.get("code") == esperado and bool(datos.get("message")), f"{etiqueta}: el estado explica el motivo", datos.get("message", ""))


def comprobar_instalado(c, etiqueta, codigo, argumentos_reinicio):
    INFORME.check(codigo == 0, f"{etiqueta}: código 0", f"salió {codigo}; log {c.carpeta / 'ayudante.log'}")
    INFORME.check(comun.sha256(c.target) == DATOS["sha_nuevo"], f"{etiqueta}: el ejecutable quedó reemplazado por el nuevo")
    INFORME.check(bool(copias(c)), f"{etiqueta}: queda una copia verificada del ejecutable anterior")
    INFORME.check(not restos(c), f"{etiqueta}: sin temporales junto al ejecutable", restos(c))
    INFORME.check(not c.ticket.exists(), f"{etiqueta}: el ayudante borra su ticket")
    if argumentos_reinicio is not None:
        procesos = reiniciados(c)
        lineas = [p.get("CommandLine") or "" for p in procesos]
        INFORME.check(any(all(a in linea for a in argumentos_reinicio) for linea in lineas), f"{etiqueta}: reinicia el nuevo ejecutable", lineas)
        for p in procesos:
            comun.matar_arbol(p["ProcessId"])
        time.sleep(1)
        INFORME.check(comun.version_de(c.target, c.entorno) == DATOS["superior"], f"{etiqueta}: el reiniciado es la versión nueva")
    if c.soporta_estado:
        INFORME.check(estado(c).get("code") == 0, f"{etiqueta}: el estado final es correcto", estado(c))


def c_espera(datos):
    c = caso("espera", datos)
    viejo = durmiente()
    proceso = ayudante(c, viejo.pid, ["--restart"])
    time.sleep(6)
    sigue = proceso.poll() is None
    INFORME.check(sigue and comun.sha256(c.target) == DATOS["sha_local"], "espera: no toca nada mientras el editor sigue abierto", f"ayudante vivo={sigue}, código={proceso.returncode}")
    if c.soporta_estado:
        comun.esperar(lambda: estado(c).get("stage") == "waiting", 30, 0.5)
        INFORME.check(estado(c).get("stage") == "waiting", "espera: el estado avisa de que todo está preparado y espera", estado(c).get("stage"))
        INFORME.check(any(".rome-update-" in p.name for p in c.instalado.iterdir()), "espera: la versión nueva ya está copiada junto al ejecutable antes de cerrar el editor")
    viejo.kill()
    comprobar_instalado(c, "espera", terminar(proceso, 90), ["--project-manager"])


def c_editor_vivo(datos):
    c = caso("editor_vivo", datos)
    vacia = comun.carpeta_limpia(c.carpeta / "vacia")
    editor = comun.lanzar([c.target, "--headless", "--project-manager"], vacia, c.entorno, c.carpeta / "editor.log")
    time.sleep(3)
    proceso = ayudante(c, editor.pid, ["--restart"])
    time.sleep(8)
    sigue = proceso.poll() is None
    INFORME.check(sigue and comun.sha256(c.target) == DATOS["sha_local"], "editor_vivo: espera a que se cierre el ejecutable en uso", f"ayudante vivo={sigue}, código={proceso.returncode}")
    comun.cerrar(editor)
    comprobar_instalado(c, "editor_vivo", terminar(proceso, 90), ["--project-manager"])


def c_ticket(datos):
    c = caso("ticket_borrado", datos)
    viejo = durmiente()
    proceso = ayudante(c, viejo.pid)
    time.sleep(4)
    c.ticket.unlink(missing_ok=True)
    codigo = terminar(proceso, 15)
    INFORME.check(time.monotonic() - c.inicio < 25, "ticket_borrado: cancela enseguida", round(time.monotonic() - c.inicio, 1))
    comprobar_intacto(c, "ticket_borrado", codigo, 9)
    viejo.kill()


def c_timeout(datos):
    c = caso("tiempo_agotado", datos)
    viejo = durmiente()
    proceso = ayudante(c, viejo.pid, ["--timeout-seconds", "3"])
    codigo = terminar(proceso, 40)
    comprobar_intacto(c, "tiempo_agotado", codigo, 4)
    viejo.kill()


def c_hashes(datos):
    c = caso("hash_nuevo", datos)
    comprobar_intacto(c, "hash_nuevo", terminar(ayudante(c, 0, sha_nuevo=DATOS["sha_local"]), 60), 3)
    c = caso("hash_instalado", datos)
    comprobar_intacto(c, "hash_instalado", terminar(ayudante(c, 0, sha_original=DATOS["sha_nuevo"]), 60), 3)


def c_bloqueo_temporal(datos):
    c = caso("bloqueo_temporal", datos)
    candado = comun.bloquear(c.target)
    proceso = ayudante(c, 0)
    time.sleep(5)
    comun.soltar(candado)
    comprobar_instalado(c, "bloqueo_temporal", terminar(proceso, 90), None)


def c_bloqueo_permanente(datos):
    c = caso("bloqueo_permanente", datos)
    candado = comun.bloquear(c.target)
    try:
        proceso = ayudante(c, 0, ["--restart"])
        codigo = terminar(proceso, 150)
    finally:
        comun.soltar(candado)
    INFORME.nota("bloqueo_permanente segundos", round(time.monotonic() - c.inicio, 1))
    comprobar_intacto(c, "bloqueo_permanente", codigo, 7)
    procesos = reiniciados(c)
    INFORME.check(bool(procesos), "bloqueo_permanente: vuelve a abrir la versión instalada para no dejarte sin editor", [p.get("CommandLine") for p in procesos])
    for p in procesos:
        comun.matar_arbol(p["ProcessId"])


def c_solo_lectura(datos):
    c = caso("solo_lectura", datos)
    os.chmod(c.target, 0o444)
    try:
        codigo = terminar(ayudante(c, 0), 150)
    finally:
        os.chmod(c.target, 0o666)
    INFORME.nota("solo_lectura código", codigo)
    INFORME.check(comun.sha256(c.target) in (DATOS["sha_local"], DATOS["sha_nuevo"]), "solo_lectura: el ejecutable nunca queda a medias")
    INFORME.check(not restos(c), "solo_lectura: sin temporales", restos(c))


def c_sin_permiso(datos):
    c = caso("sin_permiso", datos)
    usuario = subprocess.run(["whoami"], capture_output=True, text=True).stdout.strip()
    subprocess.run(["icacls", str(c.instalado), "/deny", f"{usuario}:(WD,AD,DC)"], capture_output=True)
    try:
        codigo = terminar(ayudante(c, 0), 60)
    finally:
        subprocess.run(["icacls", str(c.instalado), "/remove:d", usuario], capture_output=True)
    INFORME.check(codigo not in (0, None), "sin_permiso: falla sin instalar", codigo)
    INFORME.check(comun.sha256(c.target) == DATOS["sha_local"], "sin_permiso: el instalado sigue intacto")
    INFORME.check(not restos(c), "sin_permiso: sin temporales", restos(c))
    if c.soporta_estado:
        INFORME.check(bool(estado(c).get("message")), "sin_permiso: el estado explica el motivo", estado(c).get("message", ""))


def c_enlace(datos):
    c = caso("enlace", datos)
    enlace = c.carpeta / "enlace"
    subprocess.run(["cmd", "/c", "mklink", "/J", str(enlace), str(c.instalado)], capture_output=True)
    if not enlace.is_junction():
        INFORME.check(False, "enlace: no se pudo crear la unión de prueba")
        return
    codigo = terminar(ayudante(c, 0, target=enlace / c.target.name), 90)
    INFORME.check(codigo == 0, "enlace: instala aunque la ruta pase por una unión (como OneDrive)", f"salió {codigo}")
    INFORME.check(comun.sha256(c.target) == (DATOS["sha_nuevo"] if codigo == 0 else DATOS["sha_local"]), "enlace: el ejecutable real queda coherente")
    os.rmdir(enlace)


def c_no_rome(datos):
    c = caso("destino_no_rome", datos, "ROME Falso.exe")
    falso = comun.CONSOLA if comun.CONSOLA.is_file() else Path(os.environ["SystemRoot"]) / "System32/whoami.exe"
    shutil.copy2(falso, c.target)
    proceso = ayudante(c, 0, sha_original=comun.sha256(c.target))
    codigo = terminar(proceso, 60)
    INFORME.check(codigo == 3, "destino_no_rome: rechaza un destino que no es ROME", f"salió {codigo}")
    INFORME.check(comun.sha256(c.target) == comun.sha256(falso), "destino_no_rome: no lo toca")


def c_proyecto(datos):
    c = caso("proyecto", datos)
    proyecto = c.carpeta / "proyecto con espacios"
    proyecto.mkdir()
    (proyecto / "project.godot").write_text('config_version=5\n\n[application]\n\nconfig/name="QA reinicio"\n', encoding="utf-8")
    comprobar_instalado(c, "proyecto", terminar(ayudante(c, 0, ["--restart", "--project", proyecto]), 90), ["--editor", "--path", str(proyecto).replace("\\", "/")])
    c = caso("proyecto_invalido", datos)
    comprobar_intacto(c, "proyecto_invalido", terminar(ayudante(c, 0, ["--restart", "--project", c.carpeta / "no_existe"]), 60), 3)


def c_reabre_proyecto(datos):
    # El editor reiniciado abre de verdad la experiencia (el plugin ROMEUpdaterQA lo apunta).
    import probar_editor
    probar_editor.preparar_proyecto()
    proyecto = probar_editor.PROYECTO
    (proyecto / "updater_qa.json").write_text(json.dumps({"etapa": "reapertura", "version_nueva": DATOS["superior"]}), encoding="utf-8")
    (proyecto / "updater_qa_result.json").unlink(missing_ok=True)
    c = caso("reabre_proyecto", datos)
    codigo = terminar(ayudante(c, 0, ["--restart", "--project", proyecto]), 90)
    INFORME.check(codigo == 0, "reabre_proyecto: instala", f"salió {codigo}")
    comun.esperar(lambda: "reinicio" in probar_editor.resultado(), 240, 1)
    reinicio = probar_editor.resultado().get("reinicio", {})
    INFORME.check(reinicio.get("version") == DATOS["superior"] and Path(reinicio.get("proyecto", "-")).resolve() == proyecto.resolve(),
                  "reabre_proyecto: el editor reiniciado abre la misma experiencia con la versión nueva", f"{reinicio.get('version')} {reinicio.get('proyecto')}")
    comun.esperar(lambda: not comun.pids_de(c.target), 90, 1)


CASOS = {"espera": c_espera, "editor_vivo": c_editor_vivo, "ticket": c_ticket, "timeout": c_timeout, "hashes": c_hashes,
         "bloqueo_temporal": c_bloqueo_temporal, "bloqueo_permanente": c_bloqueo_permanente, "solo_lectura": c_solo_lectura,
         "sin_permiso": c_sin_permiso, "enlace": c_enlace, "no_rome": c_no_rome, "proyecto": c_proyecto, "reabre_proyecto": c_reabre_proyecto}


def probar(nombres):
    global DATOS
    DATOS = paquetes.preparar()
    INFORME.nota("ayudante con archivo de estado", b"--status-file" in Path(DATOS["nuevo"]).read_bytes())
    for nombre in nombres or CASOS:
        print(f"--- {nombre}", flush=True)
        try:
            CASOS[nombre](DATOS)
        except Exception as error:
            INFORME.check(False, f"{nombre}: el caso terminó con una excepción", repr(error))
        finally:
            comun.matar_ejecutable(comun.TRABAJO / "ayudante" / nombre / "instalado" / "ROME Studio.exe")
    return INFORME.guardar()


if __name__ == "__main__":
    raise SystemExit(probar(sys.argv[1:]))

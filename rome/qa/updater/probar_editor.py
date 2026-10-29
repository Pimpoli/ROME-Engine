"""1c. El actualizador de punta a punta con el editor abierto (con ventana).

python rome/qa/updater/probar_editor.py [sin_novedad] [con_novedad] [cancelar]
Un servidor local hace de GitHub Releases: los ejecutables "local" y "nuevo"
llevan las URL del actualizador cambiadas a 127.0.0.1 (ver paquetes.py), así que
no se publica nada ni se consulta GitHub. El plugin ROMEUpdaterQA conduce el
editor desde dentro: consulta del arranque, Ayuda > Actualizaciones de ROME,
descarga, «Actualizar y reiniciar», cancelación en el aviso de guardar y la
reapertura del proyecto con la versión nueva.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import threading
import time

import comun
import paquetes

INFORME = comun.Informe("editor")
E2E = comun.TRABAJO / "e2e"
PROYECTO = E2E / "proyecto"
INSTALADO = E2E / "instalado" / "ROME Studio.exe"


class Manejador(BaseHTTPRequestHandler):
    def do_GET(self):
        estado = self.server.estado
        estado["peticiones"].append((round(time.time(), 1), self.path.rsplit("/", 1)[-1]))
        if self.path.endswith("/rome-api"):
            self._json(estado["releases"])
        elif self.path.endswith("/godot-api"):
            self._json({"tag_name": "4.7.2-stable", "draft": False, "prerelease": False})
        elif "/d/" in self.path:
            nombre = self.path.split("/d/", 1)[1].split("/", 1)[-1]
            archivo = estado["archivos"].get(nombre)
            if not archivo:
                self.send_error(404)
                return
            total = archivo.stat().st_size
            rango = self.headers.get("Range", "")
            inicio = int(rango[6:-1]) if rango.startswith("bytes=") and rango.endswith("-") else 0
            estado["rangos"].append(inicio)
            if inicio >= total:
                self.send_error(416)
                return
            # Como GitHub con una conexión lenta: corta a mitad, sin avisar.
            corte = estado["cortes"].pop(0) if estado["cortes"] else None
            self.send_response(206 if inicio else 200)
            if inicio:
                self.send_header("Content-Range", f"bytes {inicio}-{total - 1}/{total}")
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(total - inicio))
            self.end_headers()
            restante = total - inicio if corte is None else min(corte, total - inicio)
            with open(archivo, "rb") as origen:
                origen.seek(inicio)
                while restante > 0:
                    bloque = origen.read(min(1 << 20, restante))
                    self.wfile.write(bloque)
                    restante -= len(bloque)
            if corte is not None:
                estado["cortados"].append(inicio + corte)
                self.close_connection = True
                try:
                    self.connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        else:
            self.send_error(404)

    def _json(self, datos):
        cuerpo = json.dumps(datos).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(cuerpo)))
        self.end_headers()
        self.wfile.write(cuerpo)

    def log_message(self, *_):
        pass


def release(version, archivo=None):
    tag = "v" + version
    assets = []
    if archivo:
        nombre = comun.nombre_asset(version)
        assets.append({"name": nombre, "browser_download_url": paquetes.urls()["descargas"][1] + tag + "/" + nombre,
                       "digest": "sha256:" + comun.sha256(archivo), "size": archivo.stat().st_size, "state": "uploaded"})
    return {"tag_name": tag, "name": "ROME " + version, "draft": False, "prerelease": "-" in version,
            "body": f"Notas de prueba de {version}.\n\n- Arreglos del actualizador.", "published_at": "2026-10-28T12:00:00Z", "assets": assets}


def preparar_proyecto():
    if not (PROYECTO / "project.godot").is_file():
        PROYECTO.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["robocopy", str(comun.BIN / "rome_update_qa"), str(PROYECTO), "/E", "/NFL", "/NDL", "/NJH", "/NJS", "/NP",
                        "/XF", "integration-result.json"], capture_output=True)
    ajustes = PROYECTO / "project.godot"
    texto = ajustes.read_text(encoding="utf-8").replace("res://addons/ROMESmoke/plugin.cfg", "res://addons/ROMEUpdaterQA/plugin.cfg")
    ajustes.write_text(texto, encoding="utf-8")
    shutil.copytree(Path(__file__).with_name("ROMEUpdaterQA"), PROYECTO / "addons/ROMEUpdaterQA", dirs_exist_ok=True)


def resultado():
    ruta = PROYECTO / "updater_qa_result.json"
    try:
        return json.loads(ruta.read_text(encoding="utf-8")) if ruta.is_file() else {}
    except ValueError:
        return {}


def abrir_editor(nombre, datos, servidor, releases, extra=None):
    entorno, base = comun.entorno_aislado("e2e-" + nombre)
    INSTALADO.parent.mkdir(parents=True, exist_ok=True)
    comun.matar_ejecutable(INSTALADO)
    for viejo in INSTALADO.parent.iterdir():
        comun.borrar(viejo)
    comun.copiar_cuando_libre(datos["local"], INSTALADO)
    escenario = {"etapa": nombre, "version_nueva": datos["superior"], **(extra or {})}
    (PROYECTO / "updater_qa.json").write_text(json.dumps(escenario), encoding="utf-8")
    (PROYECTO / "updater_qa_result.json").unlink(missing_ok=True)
    servidor.estado["releases"] = releases
    servidor.estado["peticiones"].clear()
    proceso = comun.lanzar([INSTALADO, "--editor", "--path", PROYECTO], PROYECTO, entorno, comun.TRABAJO / f"logs/editor-{nombre}.log")
    return proceso, base


def descarga_de(base, datos):
    return base / "local/ROMEEngine/updates" / datos["superior"] / comun.nombre_asset(datos["superior"])


def e_sin_novedad(datos, servidor):
    proceso, base = abrir_editor("sin_novedad", datos, servidor, [release(datos["version"], datos["local"])])
    comun.esperar(lambda: "menu" in resultado() or proceso.poll() is not None, 240, 1)
    comun.esperar(lambda: proceso.poll() is not None, 30, 1)
    comun.cerrar(proceso)
    r = resultado()
    inicio, menu = r.get("inicio", {}), r.get("menu", {})
    INFORME.nota("sin_novedad peticiones", servidor.estado["peticiones"])
    INFORME.check(inicio.get("estado", {}).get("rome_status") == "current", "sin_novedad: la consulta del arranque dice que está al día", inicio.get("estado", {}).get("rome_status"))
    INFORME.check(inicio and not inicio.get("dialogo_visible") and not inicio.get("ventanas"), "sin_novedad: al arrancar no se abre ningún diálogo", inicio.get("ventanas"))
    INFORME.check(menu.get("encontrado") and menu.get("dialogo_visible"), "sin_novedad: Ayuda > Actualizaciones de ROME abre el diálogo")
    INFORME.check(any("0.1.0" in t and "actual" in t.lower() for t in menu.get("textos", [])), "sin_novedad: el diálogo muestra la versión actual", menu.get("textos", [])[:2])


def e_con_novedad(datos, servidor):
    proceso, base = abrir_editor("con_novedad", datos, servidor, [release(datos["version"], datos["local"]), release(datos["superior"], datos["nuevo"])])
    comun.esperar(lambda: "instalar" in resultado() or proceso.poll() is not None, 300, 1)
    r = resultado()
    inicio = r.get("inicio", {})
    INFORME.check(inicio.get("estado", {}).get("rome_status") == "update_available", "con_novedad: la consulta del arranque encuentra " + datos["superior"], inicio.get("estado", {}).get("rome_status"))
    INFORME.check(inicio and not inicio.get("ventanas"), "con_novedad: en el editor avisa sin abrir diálogos", inicio.get("ventanas"))
    INFORME.check(r.get("descarga", {}).get("estado", {}).get("verified"), "con_novedad: descarga y verifica el SHA-256", r.get("descarga", {}).get("estado", {}).get("action_error", ""))
    instante = time.time()
    cerrado = comun.esperar(lambda: proceso.poll() is not None, 90, 0.5)
    INFORME.check(cerrado, "con_novedad: «Actualizar y reiniciar» cierra el editor", round(time.time() - instante, 1))
    comun.cerrar(proceso)
    reemplazado = comun.esperar(lambda: comun.sha256_o_nada(INSTALADO) == datos["sha_nuevo"], 120, 1)
    INFORME.check(reemplazado, "con_novedad: el ejecutable instalado pasa a ser el nuevo")
    comun.esperar(lambda: "reinicio" in resultado(), 240, 1)
    reinicio = resultado().get("reinicio", {})
    INFORME.check(reinicio.get("version") == datos["superior"], "con_novedad: se reabre con la versión nueva", reinicio.get("version"))
    INFORME.check(Path(reinicio.get("proyecto", "-")).resolve() == PROYECTO.resolve(), "con_novedad: --project reabre el mismo proyecto", reinicio.get("proyecto"))
    comprobar_limpieza("con_novedad", base)
    INFORME.nota("con_novedad estado tras reiniciar", resultado().get("reinicio_final", {}).get("rome_message", ""))
    INFORME.nota("con_novedad peticiones", servidor.estado["peticiones"])


def restos_de_descargas(base):
    carpeta = base / "local/ROMEEngine/updates"
    return sorted(str(p.relative_to(carpeta)) for p in carpeta.rglob("*") if p.name != "releases.json") if carpeta.is_dir() else []


def comprobar_limpieza(nombre, base):
    comun.esperar(lambda: not comun.pids_de(INSTALADO), 90, 1)
    comun.matar_ejecutable(INSTALADO)
    junto = sorted(p.name for p in INSTALADO.parent.iterdir())
    INFORME.check(junto == [INSTALADO.name], f"{nombre}: no deja copias ni temporales junto al ejecutable", junto)
    INFORME.check(not restos_de_descargas(base), f"{nombre}: la versión nueva limpia la descarga y la carpeta del ayudante", restos_de_descargas(base)[:6])
    copias = list((base / "local/ROMEEngine/backups").glob("*.bak"))
    INFORME.check(len(copias) == 1, f"{nombre}: guarda una sola copia de la versión anterior, en la caché", [p.name for p in copias])


def e_al_cerrar(datos, servidor):
    proceso, base = abrir_editor("al_cerrar", datos, servidor, [release(datos["version"], datos["local"]), release(datos["superior"], datos["nuevo"])])
    comun.esperar(lambda: "descarga" in resultado() or proceso.poll() is not None, 300, 1)
    INFORME.check(resultado().get("descarga", {}).get("estado", {}).get("verified"), "al_cerrar: descarga sola en segundo plano y la verifica")
    INFORME.check(comun.esperar(lambda: proceso.poll() is not None, 60, 0.5), "al_cerrar: el editor se cierra con normalidad")
    comun.cerrar(proceso)
    reemplazado = comun.esperar(lambda: comun.sha256_o_nada(INSTALADO) == datos["sha_nuevo"], 120, 1)
    INFORME.check(reemplazado, "al_cerrar: al cerrar el editor se instala la versión descargada")
    INFORME.check(not comun.pids_de(INSTALADO), "al_cerrar: no reabre ROME por su cuenta")
    otra =comun.lanzar([INSTALADO, "--editor", "--path", PROYECTO], PROYECTO, comun.entorno_aislado("e2e-al_cerrar", limpio=False)[0], comun.TRABAJO / "logs/editor-al_cerrar-2.log")
    comun.esperar(lambda: "reinicio" in resultado() or otra.poll() is not None, 240, 1)
    INFORME.check(resultado().get("reinicio", {}).get("version") == datos["superior"], "al_cerrar: la siguiente vez que se abre ya es la versión nueva", resultado().get("reinicio", {}).get("version"))
    comun.esperar(lambda: otra.poll() is not None, 90, 1)
    comun.cerrar(otra)
    comprobar_limpieza("al_cerrar", base)


def e_cancelar(datos, servidor):
    # Con save_each_scene_on_quit = true (lo normal) el editor de ROME se cuelga al cerrar con la
    # escena principal sin guardar (ver cierre_con_cambios); aquí se aísla el actualizador.
    proceso, base = abrir_editor("cancelar", datos, servidor, [release(datos["version"], datos["local"]), release(datos["superior"], datos["nuevo"])],
                                 {"guardar_cada_escena": False})
    descargado = descarga_de(base, datos)
    vistos = []
    fin = time.monotonic() + 360
    while time.monotonic() < fin and proceso.poll() is None and "cancelado" not in resultado():
        if comun.pids_de(descargado):
            vistos.append(time.time())
        time.sleep(0.5)
    comun.esperar(lambda: proceso.poll() is not None, 40, 1)
    # Si algo fue mal y el ayudante sigue, se le deja terminar (no matarlo a medias).
    vivo_al_final = not comun.esperar(lambda: not comun.pids_de(descargado), 60, 1)
    comun.cerrar(proceso)
    r = resultado()
    INFORME.nota("cancelar: había escena abierta al pulsar instalar", r.get("instalar", {}).get("escena"))
    aviso, cancelado = r.get("aviso_guardar", {}), r.get("cancelado", {})
    INFORME.check(aviso.get("visible"), "cancelar: con cambios sin guardar sale el aviso de guardar", aviso.get("texto", "")[:80])
    INFORME.check(bool(vistos) and max(vistos) >= aviso.get("hora", 1e12) - 1, "cancelar: el ayudante sigue esperando mientras está el aviso",
                  f"visto hasta {round(max(vistos) - aviso.get('hora', 0), 1) if vistos else None} s respecto al aviso")
    INFORME.check(not cancelado.get("estado", {}).get("helper_started", True), "cancelar: al cancelar, el editor deja de esperar la instalación")
    INFORME.check(not vivo_al_final, "cancelar: el ayudante termina al cancelar")
    INFORME.check(comun.sha256_o_nada(INSTALADO) == datos["sha_local"], "cancelar: el ejecutable instalado no cambia")
    junto = sorted(p.name for p in INSTALADO.parent.iterdir())
    INFORME.check(junto == [INSTALADO.name], "cancelar: no deja temporales junto al ejecutable", junto)
    INFORME.nota("cancelar mensaje", cancelado.get("estado", {}).get("action_error", ""))
    comun.matar_ejecutable(descargado)


def e_reanudar(datos, servidor):
    """El servidor corta la descarga como GitHub cuando va lento; el editor se cierra a medias y al volver sigue."""
    corte = 40 * 1024 * 1024
    releases = [release(datos["version"], datos["local"]), release(datos["superior"], datos["nuevo"])]
    estado = servidor.estado
    estado["cortes"], estado["rangos"], estado["cortados"] = [corte], [], []
    proceso, base = abrir_editor("reanudar", datos, servidor, releases, {"etapa": "al_cerrar"})
    parcial = descarga_de(base, datos).with_name(comun.nombre_asset(datos["superior"]) + ".partial")
    comun.esperar(lambda: estado["cortados"] and parcial.is_file() and parcial.stat().st_size >= corte, 120, 0.2)
    # Cerrado de golpe mientras espera para reintentar.
    comun.cerrar(proceso)
    tamano = parcial.stat().st_size if parcial.is_file() else 0
    INFORME.check(tamano == corte, "reanudar: tras el corte se conserva lo descargado", tamano)
    estado["cortes"], estado["rangos"] = [corte], []
    (PROYECTO / "updater_qa_result.json").unlink(missing_ok=True)
    otra = comun.lanzar([INSTALADO, "--editor", "--path", PROYECTO], PROYECTO, comun.entorno_aislado("e2e-reanudar", limpio=False)[0], comun.TRABAJO / "logs/editor-reanudar-2.log")
    comun.esperar(lambda: "descarga" in resultado() or otra.poll() is not None, 300, 1)
    descarga = resultado().get("descarga", {}).get("estado", {})
    INFORME.check(estado["rangos"][:2] == [corte, 2 * corte], "reanudar: al volver a abrir sigue desde lo guardado, y tras otro corte, desde donde quedó", estado["rangos"])
    INFORME.check(descarga.get("verified"), "reanudar: termina y verifica el SHA-256", descarga.get("action_error", ""))
    comun.esperar(lambda: otra.poll() is not None, 90, 1)
    comun.cerrar(otra)
    INFORME.check(comun.esperar(lambda: comun.sha256_o_nada(INSTALADO) == datos["sha_nuevo"], 120, 1), "reanudar: al cerrar se instala la versión descargada")
    INFORME.check(not parcial.exists() and not parcial.with_name(parcial.name + ".json").exists(), "reanudar: no quedan restos de la descarga a medias")


def e_cierre_con_cambios(datos, servidor):
    # No es del actualizador, pero «Actualizar y reiniciar» y el cierre normal usan este camino.
    proceso, base = abrir_editor("cierre_con_cambios", datos, servidor, [release(datos["version"], datos["local"])])
    comun.esperar(lambda: proceso.poll() is not None, 240, 1)
    codigo = proceso.poll()
    comun.cerrar(proceso)
    r = resultado()
    INFORME.check(r.get("marcado", {}).get("escena"), "cierre_con_cambios: la escena principal está abierta y con cambios")
    INFORME.check("aviso_al_cerrar" in r and r.get("sigue_abierto_tras_cancelar"), "cierre_con_cambios: cerrar con cambios sin guardar pregunta y se puede cancelar",
                  f"código de salida {codigo}; {'sin aviso' if 'sin_aviso' in r else 'el editor terminó sin avisar' if 'aviso_al_cerrar' not in r else ''}")


ESCENARIOS = {"sin_novedad": e_sin_novedad, "con_novedad": e_con_novedad, "cancelar": e_cancelar, "al_cerrar": e_al_cerrar, "reanudar": e_reanudar,
              "cierre_con_cambios": e_cierre_con_cambios}


def probar(nombres):
    datos = paquetes.preparar()
    preparar_proyecto()
    servidor = ThreadingHTTPServer(("127.0.0.1", paquetes.PUERTO), Manejador)
    servidor.estado = {"releases": [], "peticiones": [], "rangos": [], "cortes": [], "cortados": [],
                       "archivos": {comun.nombre_asset(datos["superior"]): datos["nuevo"], comun.nombre_asset(datos["version"]): datos["local"]}}
    threading.Thread(target=servidor.serve_forever, daemon=True).start()
    try:
        for nombre in nombres or ESCENARIOS:
            print(f"--- {nombre}", flush=True)
            try:
                ESCENARIOS[nombre](datos, servidor)
            except Exception as error:
                INFORME.check(False, f"{nombre}: el escenario terminó con una excepción", repr(error))
            finally:
                comun.matar_ejecutable(INSTALADO)
    finally:
        servidor.shutdown()
    return INFORME.guardar()


if __name__ == "__main__":
    raise SystemExit(probar(sys.argv[1:]))

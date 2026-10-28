"""Prueba que las experiencias se ponen al día solas al abrirlas.

Abre el editor de verdad (con ventana, --quit) sobre experiencias de prueba en
bin/rome_agente_B_updater/experiencias, cada una con APPDATA y LOCALAPPDATA
propios, y comprueba en disco qué se cambió, qué se conservó y qué quedó en
la copia de seguridad. Usa el editor del arnés (ROME_QA_EDITOR, por defecto
bin/rome_agente_B.exe, que necesita bin/rome_runtime al lado) y un autónomo
hecho con él: copia ahí el editor recién compilado y pasa
`package_runtime.py --rome-only` antes.

    python rome/qa/updater/probar_experiencias.py
"""
import ctypes
import hashlib
import json
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import comun  # noqa: E402

TRABAJO = comun.TRABAJO / "experiencias"
EDITOR = comun.EDITOR
CONSOLA_DEV = comun.CONSOLA
AUTONOMO = TRABAJO / "programa/rome_autonomo.exe"
CONSOLA_AUTONOMO = AUTONOMO.with_name("rome_autonomo.console.exe")
MANIFIESTO_DEV = comun.RUNTIME / "manifest.json"
FUERA = ("GodotLuau/LocalScripts/", "GodotLuau/ModuleScripts/", "GodotLuau/ServerScripts/")
LOGO = "GodotLuau/icons/MenuLogo.svg"
ICO = "GodotLuau/icons/GodotLuau.ico"
SALUD = "GodotLuau/DefaultScripts/StarterPlayer/StarterCharacterScripts/Health.luau"
ANIMAR = "GodotLuau/DefaultScripts/StarterPlayer/StarterCharacterScripts/Animate.luau"
CLIENTE = "addons/ROMEStudio/templates/ObbyClient.luau"
SERVIDOR = "addons/ROMEStudio/templates/ObbyServer.luau"
ESTUDIO = "addons/ROMEStudio/studio.gd"
ICONO = "GodotLuau/icons/Folder.svg"
VERSION = "GodotLuau/Version"

AJUSTES = """config_version=5

[application]

config/name="{nombre}"
run/main_scene="res://Scenes/Place.tscn"
config/features=PackedStringArray("4.8")

[editor_plugins]

enabled=PackedStringArray({plugins})

[physics]

3d/physics_engine="Jolt Physics"
{rome}"""


def gestionados(manifiesto):
    archivos = json.loads(manifiesto)["files"]
    return {r: h for r, h in archivos.items()
            if r.startswith(("GodotLuau/", "addons/ROMEStudio/", "addons/GodotLuauUpdater/")) and not r.startswith(FUERA) and r not in (LOGO, ICO)}


def identidad(manifiesto):
    return hashlib.sha256(manifiesto).hexdigest()[:12]


def sha_texto(texto):
    return hashlib.sha256(texto.encode("utf-8")).hexdigest()


def crear(nombre, rome=True):
    destino = comun.carpeta_limpia(TRABAJO / nombre)
    shutil.copytree(comun.RUNTIME / "project", destino, dirs_exist_ok=True)
    plugins = '"res://addons/GodotLuauUpdater/plugin.cfg", "res://addons/ROMEStudio/plugin.cfg"' if rome else ""
    (destino / "project.godot").write_text(AJUSTES.format(nombre=nombre, plugins=plugins, rome="\n[rome]\n\nstudio/enabled=true\n" if rome else ""), encoding="utf-8")
    (destino / "Scenes").mkdir(exist_ok=True)
    (destino / "Scenes/Place.tscn").write_text('[gd_scene format=3]\n\n[node name="Game" type="Node3D"]\n', encoding="utf-8")
    return destino


def escribir(proyecto, relativa, texto):
    ruta = proyecto / relativa
    ruta.parent.mkdir(parents=True, exist_ok=True)
    ruta.write_bytes(texto.encode("utf-8"))
    return sha_texto(texto)


def agregar(proyecto, relativa, texto):
    with open(proyecto / relativa, "ab") as archivo:
        archivo.write(texto.encode("utf-8"))


def leer(proyecto, relativa):
    ruta = proyecto / relativa
    return ruta.read_bytes().decode("utf-8", errors="replace") if ruta.is_file() else None


def sellar(proyecto, ident, archivos):
    sello = proyecto / ".rome/runtime.json"
    sello.parent.mkdir(parents=True, exist_ok=True)
    sello.write_text(json.dumps({"identity": ident, "rome_version": "0.0.1", "files": archivos}, indent=1), encoding="utf-8")


def sello(proyecto):
    ruta = proyecto / ".rome/runtime.json"
    return json.loads(ruta.read_text(encoding="utf-8")) if ruta.is_file() else {}


def copias(proyecto):
    carpeta = proyecto / ".rome"
    return sorted(p.name for p in carpeta.iterdir() if p.is_dir() and p.name.startswith("backup-")) if carpeta.is_dir() else []


def temporales(proyecto):
    return [str(p.relative_to(proyecto)) for p in proyecto.rglob("*") if p.name.endswith((".rome-tmp", ".tmp")) and ".godot" not in p.parts]


def distintos(proyecto, esperados):
    return sorted(r for r, h in esperados.items() if not (proyecto / r).is_file() or comun.sha256(proyecto / r) != h)


def abrir(consola, proyecto, entorno, nombre, segundos=240):
    log = TRABAJO / "logs" / f"{nombre}.log"
    inicio = time.monotonic()
    proceso = comun.lanzar([consola, "--editor", "--path", proyecto, "--quit"], proyecto, entorno, log)
    try:
        proceso.wait(segundos)
    except subprocess.TimeoutExpired:
        pass
    codigo = comun.cerrar(proceso)
    return codigo, log.read_text(encoding="utf-8", errors="replace"), round(time.monotonic() - inicio, 1)


def bloquear_sin_borrar(ruta):
    """Deja leer y escribir pero no borrar ni reemplazar, como un programa que tiene el archivo abierto."""
    manejador = comun.kernel32.CreateFileW(str(ruta), comun.GENERIC_READ, comun.FILE_SHARE_READ | 0x2, None, comun.OPEN_EXISTING, 0, None)
    if manejador in (None, comun.INVALID_HANDLE):
        raise ctypes.WinError(ctypes.get_last_error())
    return manejador


def preparar_autonomo(informe):
    comun.matar_ejecutable(AUTONOMO)
    comun.carpeta_limpia(AUTONOMO.parent)
    salida = comun.empaquetar(EDITOR, AUTONOMO)
    if not informe.check(salida.returncode == 0 and AUTONOMO.is_file(), "Se empaqueta un autónomo con el editor recién compilado", salida.stderr.strip()[-300:]):
        return None
    shutil.copy2(CONSOLA_DEV, CONSOLA_AUTONOMO)
    with zipfile.ZipFile(AUTONOMO) as archivo:
        return archivo.read("rome_runtime/manifest.json")


def e_antigua(informe, manifiesto):
    """Experiencia sin sello (creada antes de este cambio) abierta con un autónomo que nunca se ha abierto."""
    esperados = gestionados(manifiesto)
    proyecto = crear("antigua")
    agregar(proyecto, ESTUDIO, "\n# cambio viejo\n")
    salud = escribir(proyecto, SALUD, leer(proyecto, SALUD) + "\n-- mi cambio\n")
    logo = escribir(proyecto, LOGO, '<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><circle cx="32" cy="32" r="30" fill="#e5534b"/></svg>\n')
    (proyecto / ICONO).unlink()
    propios = {"Scenes/Mio.tscn": escribir(proyecto, "Scenes/Mio.tscn", '[gd_scene format=3]\n\n[node name="Mio" type="Node3D"]\n'),
               "GodotLuau/LocalScripts/Mio.lua": escribir(proyecto, "GodotLuau/LocalScripts/Mio.lua", "print('mio')\n"),
               "addons/ROMEStudio/viejo.txt": escribir(proyecto, "addons/ROMEStudio/viejo.txt", "sin registro\n")}
    entorno, base = comun.entorno_aislado("experiencias-antigua")
    codigo, log, segundos = abrir(CONSOLA_AUTONOMO, proyecto, entorno, "antigua")
    informe.nota("antigua segundos (extraer + actualizar + abrir)", segundos)
    informe.check(codigo == 0, "antigua: el editor abre y se cierra bien", codigo)
    caches = list((base / "local/ROMEEngine/runtime").glob("*/runtime.complete"))
    informe.check(len(caches) == 1 and caches[0].parent.name.endswith(identidad(manifiesto)), "antigua: el autónomo extrae su runtime antes de cargar GodotLuau", [c.parent.name for c in caches])
    informe.check(distintos(proyecto, esperados) == [SALUD], "antigua: todos los archivos de ROME quedan como los del runtime salvo el script editable", distintos(proyecto, esperados)[:5])
    informe.check((leer(proyecto, ".rome/backup-anterior/" + ESTUDIO) or "").endswith("# cambio viejo\n"), "antigua: la versión vieja de studio.gd queda en .rome/backup-anterior")
    informe.check(comun.sha256(proyecto / SALUD) == salud, "antigua: Health.luau cambiado se conserva (sin sello no se sabe si es del creador)")
    informe.check(not (proyecto / ".rome/backup-anterior" / SALUD).exists(), "antigua: lo que se conserva no va a la copia")
    informe.check(comun.sha256(proyecto / LOGO) == logo and not (proyecto / ".rome/backup-anterior" / LOGO).exists() and not (proyecto / ".rome/backup-anterior" / ICO).exists(),
                  "antigua: el logo propio del juego y su icono de ventana no se tocan")
    informe.check((proyecto / ICONO).is_file(), "antigua: un archivo de ROME borrado vuelve")
    informe.check(all((proyecto / r).is_file() and comun.sha256(proyecto / r) == h for r, h in propios.items()), "antigua: escenas, scripts propios y archivos sin registro no se tocan")
    s = sello(proyecto)
    informe.check(s.get("identity") == identidad(manifiesto) and s.get("files") == esperados, "antigua: el sello guarda la identidad del runtime y sus archivos", s.get("identity"))
    informe.check((proyecto / ".rome/.gdignore").is_file() and "runtime.json" in (leer(proyecto, ".rome/.gitignore") or ""), "antigua: .rome lleva .gdignore y .gitignore")
    informe.check(not temporales(proyecto), "antigua: sin temporales", temporales(proyecto)[:3])
    informe.check(log.count("ROME actualizó los componentes de esta experiencia") >= 2, "antigua: se avisa al abrir y otra vez en el editor", log.count("ROME actualizó"))
    informe.check("Se mantuvo tu versión de Health.luau" in log, "antigua: el aviso nombra el script que se conservó")
    return proyecto, entorno


def e_igual(informe, proyecto, entorno):
    """Segunda apertura con el mismo ROME: no se toca nada."""
    sello_antes = (proyecto / ".rome/runtime.json").stat().st_mtime_ns
    copia = proyecto / ".rome/backup-anterior" / ESTUDIO
    copia_antes = copia.stat().st_mtime_ns if copia.is_file() else 0
    codigo, log, segundos = abrir(CONSOLA_AUTONOMO, proyecto, entorno, "igual")
    informe.nota("igual segundos (abrir sin cambios)", segundos)
    informe.check(codigo == 0, "igual: el editor abre y se cierra bien", codigo)
    informe.check((proyecto / ".rome/runtime.json").stat().st_mtime_ns == sello_antes, "igual: el sello no se reescribe")
    informe.check(copia.is_file() and copia.stat().st_mtime_ns == copia_antes, "igual: la copia anterior sigue igual")
    informe.check("ROME actualizó" not in log and "no pudo actualizar" not in log, "igual: no hay aviso")


def e_anterior(informe, manifiesto):
    """Experiencia sellada por un ROME anterior: se distingue lo de ROME de lo del creador."""
    esperados = gestionados(manifiesto)
    proyecto = crear("anterior")
    base = dict(esperados)
    base[CLIENTE] = escribir(proyecto, CLIENTE, "-- ObbyClient de un ROME anterior\n")
    base[ANIMAR] = escribir(proyecto, ANIMAR, "-- Animate de un ROME anterior\n")
    escribir(proyecto, SALUD, "-- Health editado por el creador\n")
    base[SALUD] = sha_texto("-- Health de un ROME anterior\n")
    base["addons/ROMEStudio/obsoleto.txt"] = escribir(proyecto, "addons/ROMEStudio/obsoleto.txt", "ya no viene\n")
    escribir(proyecto, "addons/ROMEStudio/obsoleto2.txt", "ya no viene, pero lo cambié\n")
    base["addons/ROMEStudio/obsoleto2.txt"] = sha_texto("ya no viene\n")
    escribir(proyecto, SERVIDOR, "-- truco del creador en un archivo de ROME\n")
    escribir(proyecto, ".rome/backup-viejo/nota.txt", "copia de otra actualización\n")
    sellar(proyecto, "000000000000", base)
    entorno, _ = comun.entorno_aislado("experiencias-dev")
    codigo, log, _ = abrir(CONSOLA_DEV, proyecto, entorno, "anterior")
    copia = ".rome/backup-000000000000/"
    informe.check(codigo == 0, "anterior: el editor abre y se cierra bien", codigo)
    informe.check(distintos(proyecto, esperados) == [SALUD], "anterior: todo queda como el runtime salvo el script que editó el creador", distintos(proyecto, esperados)[:5])
    informe.check(leer(proyecto, copia + CLIENTE) == "-- ObbyClient de un ROME anterior\n", "anterior: un archivo de ROME sin tocar se actualiza y su versión vieja va a la copia")
    informe.check(leer(proyecto, copia + ANIMAR) == "-- Animate de un ROME anterior\n", "anterior: un script por defecto sin tocar también se actualiza")
    informe.check(leer(proyecto, SALUD) == "-- Health editado por el creador\n", "anterior: un script por defecto editado se conserva")
    informe.check(not (proyecto / "addons/ROMEStudio/obsoleto.txt").exists() and leer(proyecto, copia + "addons/ROMEStudio/obsoleto.txt") == "ya no viene\n", "anterior: un archivo que ROME ya no trae se retira a la copia")
    informe.check(leer(proyecto, "addons/ROMEStudio/obsoleto2.txt") == "ya no viene, pero lo cambié\n", "anterior: si el creador lo cambió, se queda")
    informe.check(leer(proyecto, copia + SERVIDOR) == "-- truco del creador en un archivo de ROME\n", "anterior: un archivo de ROME editado se reemplaza, con copia")
    informe.check(copias(proyecto) == ["backup-000000000000"], "anterior: solo queda la copia de esta actualización", copias(proyecto))
    s = sello(proyecto)
    informe.check(s.get("identity") == identidad(manifiesto) and s.get("files") == esperados, "anterior: el sello pasa al runtime nuevo", s.get("identity"))
    informe.check("(4 archivos de ROMEStudio y GodotLuau)" in log and "Se mantuvo tu versión de Health.luau" in log, "anterior: el aviso cuenta 4 archivos y nombra el conservado")
    informe.check(not temporales(proyecto), "anterior: sin temporales", temporales(proyecto)[:3])


def e_bloqueada(informe, manifiesto):
    """Un archivo en uso: no se cambia nada y se reintenta la próxima vez."""
    esperados = gestionados(manifiesto)
    proyecto = crear("bloqueada")
    base = dict(esperados)
    base[SERVIDOR] = escribir(proyecto, SERVIDOR, "-- ObbyServer de un ROME anterior\n")
    (proyecto / ICONO).unlink()
    sellar(proyecto, "111111111111", base)
    entorno, _ = comun.entorno_aislado("experiencias-dev", limpio=False)
    manejador = comun.bloquear(proyecto / SERVIDOR)
    try:
        codigo, log, _ = abrir(CONSOLA_DEV, proyecto, entorno, "bloqueada")
    finally:
        comun.soltar(manejador)
    informe.check(codigo == 0, "bloqueada: el editor abre igual", codigo)
    informe.check("está en uso" in log and SERVIDOR in log, "bloqueada: avisa de qué archivo está en uso")
    informe.check(leer(proyecto, SERVIDOR) == "-- ObbyServer de un ROME anterior\n" and not (proyecto / ICONO).exists(), "bloqueada: no cambia nada a medias")
    informe.check(sello(proyecto).get("identity") == "111111111111" and not copias(proyecto), "bloqueada: el sello sigue siendo el viejo y no hay copia")
    codigo, log, _ = abrir(CONSOLA_DEV, proyecto, entorno, "bloqueada-reintento")
    informe.check(codigo == 0 and not distintos(proyecto, esperados), "bloqueada: al volver a abrirla sin el bloqueo se actualiza entera", distintos(proyecto, esperados)[:5])
    informe.check(leer(proyecto, ".rome/backup-111111111111/" + SERVIDOR) == "-- ObbyServer de un ROME anterior\n" and sello(proyecto).get("identity") == identidad(manifiesto), "bloqueada: con su copia y el sello nuevo")


def e_vuelta_atras(informe, manifiesto):
    """Falla a mitad (un archivo no se deja reemplazar): lo ya cambiado vuelve a como estaba."""
    esperados = gestionados(manifiesto)
    orden = list(esperados)
    informe.check(orden.index(CLIENTE) < orden.index(VERSION), "vuelta_atras: ObbyClient.luau se actualiza antes que Version", (orden.index(CLIENTE), orden.index(VERSION)))
    proyecto = crear("vuelta_atras")
    base = dict(esperados)
    base[CLIENTE] = escribir(proyecto, CLIENTE, "-- ObbyClient de un ROME anterior\n")
    base[VERSION] = escribir(proyecto, VERSION, "v0.0.1\n")
    sellar(proyecto, "222222222222", base)
    entorno, _ = comun.entorno_aislado("experiencias-dev", limpio=False)
    manejador = bloquear_sin_borrar(proyecto / VERSION)
    try:
        codigo, log, _ = abrir(CONSOLA_DEV, proyecto, entorno, "vuelta_atras")
    finally:
        comun.soltar(manejador)
    informe.check(codigo == 0, "vuelta_atras: el editor abre igual", codigo)
    informe.check("la dejó como estaba" in log, "vuelta_atras: avisa de que la dejó como estaba")
    informe.check(leer(proyecto, CLIENTE) == "-- ObbyClient de un ROME anterior\n", "vuelta_atras: el archivo que ya se había reemplazado vuelve a su versión")
    informe.check(leer(proyecto, VERSION) == "v0.0.1\n", "vuelta_atras: el que falló sigue intacto")
    informe.check(sello(proyecto).get("identity") == "222222222222" and not copias(proyecto) and not temporales(proyecto), "vuelta_atras: sello viejo, sin copia ni temporales", (copias(proyecto), temporales(proyecto)[:3]))


def e_ajena(informe):
    """Un proyecto de Godot que no es de ROME no se toca."""
    proyecto = crear("ajena", rome=False)
    agregar(proyecto, ESTUDIO, "\n# cambio en un proyecto ajeno\n")
    antes = comun.sha256(proyecto / ESTUDIO)
    entorno, _ = comun.entorno_aislado("experiencias-dev", limpio=False)
    codigo, _, _ = abrir(CONSOLA_DEV, proyecto, entorno, "ajena")
    informe.check(codigo == 0 and comun.sha256(proyecto / ESTUDIO) == antes and not (proyecto / ".rome").exists(), "ajena: un proyecto sin rome/studio/enabled no se toca", codigo)


def main():
    informe = comun.Informe("experiencias")
    comun.carpeta_limpia(TRABAJO / "logs")
    manifiesto_dev = MANIFIESTO_DEV.read_bytes()
    manifiesto_autonomo = preparar_autonomo(informe)
    if manifiesto_autonomo:
        proyecto, entorno = e_antigua(informe, manifiesto_autonomo)
        e_igual(informe, proyecto, entorno)
    e_anterior(informe, manifiesto_dev)
    e_bloqueada(informe, manifiesto_dev)
    e_vuelta_atras(informe, manifiesto_dev)
    e_ajena(informe)
    return informe.guardar()


if __name__ == "__main__":
    sys.exit(main())

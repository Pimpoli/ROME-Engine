"""Utilidades del arnés del actualizador de ROME.

Todo lo que generan las pruebas va a bin/rome_agente_B_updater. Ninguna prueba
abre los ejecutables públicos ni usa la caché o los ajustes reales del usuario:
cada proceso corre con APPDATA y LOCALAPPDATA propios.
"""
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import zipfile

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parents[3]
BIN = ROOT / "bin"
EDITOR = Path(os.environ.get("ROME_QA_EDITOR", BIN / "rome_agente_B.exe"))
CONSOLA = EDITOR.with_name(EDITOR.stem + ".console.exe")
RUNTIME = BIN / "rome_runtime"
TRABAJO = BIN / "rome_agente_B_updater"
VERSION_FALSA = "0.1.0-alpha.9"


def version_rome():
    texto = (ROOT / "core/version.h").read_text(encoding="utf-8")
    return re.search(r'#define ROME_VERSION "([^"]+)"', texto).group(1)


def nombre_asset(version):
    return f"ROME-Studio-{version}-windows-x86_64.exe"


def sha256(ruta):
    h = hashlib.sha256()
    with open(ruta, "rb") as archivo:
        for bloque in iter(lambda: archivo.read(1 << 20), b""):
            h.update(bloque)
    return h.hexdigest()


def sha256_o_nada(ruta):
    """Para sondear: mientras se reemplaza, el exe puede estar un instante sin poderse abrir."""
    try:
        return sha256(ruta)
    except OSError:
        return None


class Informe:
    def __init__(self, nombre):
        self.nombre = nombre
        self.checks = []
        self.errores = []
        self.datos = {}

    def check(self, valor, texto, detalle=""):
        linea = texto + (f" ({detalle})" if detalle != "" else "")
        (self.checks if valor else self.errores).append(linea)
        print(("OK    " if valor else "FALLO ") + linea, flush=True)
        return bool(valor)

    def nota(self, clave, valor):
        self.datos[clave] = valor
        print(f"      {clave}: {valor}", flush=True)

    def guardar(self):
        TRABAJO.mkdir(parents=True, exist_ok=True)
        ruta = TRABAJO / f"{self.nombre}-resultado.json"
        datos = {"checks": self.checks, "errores": self.errores, "datos": self.datos}
        ruta.write_text(json.dumps(datos, indent=2, ensure_ascii=False, default=str), encoding="utf-8")
        print(f"{self.nombre}: {len(self.checks)} OK, {len(self.errores)} fallos -> {ruta}", flush=True)
        return 1 if self.errores else 0


def borrar(ruta, intentos=20):
    ruta = Path(ruta)

    def quitar_solo_lectura(funcion, camino, _error):
        os.chmod(camino, 0o666)
        funcion(camino)

    for _ in range(intentos):
        if not os.path.lexists(ruta):
            return True
        try:
            if ruta.is_junction() or ruta.is_symlink():
                os.rmdir(ruta) if ruta.is_dir() else ruta.unlink()
            elif ruta.is_dir():
                for hijo in ruta.rglob("*"):
                    if hijo.is_junction():
                        os.rmdir(hijo)
                shutil.rmtree(ruta, onexc=quitar_solo_lectura)
            else:
                os.chmod(ruta, 0o666)
                ruta.unlink()
        except OSError:
            time.sleep(0.5)
    return not os.path.lexists(ruta)


def carpeta_limpia(ruta):
    borrar(ruta)
    Path(ruta).mkdir(parents=True, exist_ok=True)
    return Path(ruta)


def entorno_aislado(nombre, limpio=True):
    base = TRABAJO / "entornos" / nombre
    if limpio:
        borrar(base)
    for sub in ("roaming", "local"):
        (base / sub).mkdir(parents=True, exist_ok=True)
    entorno = os.environ.copy()
    entorno["APPDATA"] = str(base / "roaming")
    entorno["LOCALAPPDATA"] = str(base / "local")
    return entorno, base


def esperar(condicion, segundos, paso=0.25):
    fin = time.monotonic() + segundos
    while time.monotonic() < fin:
        valor = condicion()
        if valor:
            return valor
        time.sleep(paso)
    return condicion()


def version_de(exe, entorno=None):
    salida = subprocess.run([str(exe), "--version"], capture_output=True, text=True, timeout=120, env=entorno)
    return salida.stdout.strip().splitlines()[-1] if salida.stdout.strip() else ""


def parchear(origen, destino, cambios):
    """Reemplaza cadenas de igual longitud en una copia del binario."""
    datos = bytearray(Path(origen).read_bytes())
    cuentas = {}
    for viejo, nuevo in cambios:
        viejo, nuevo = viejo.encode(), nuevo.encode()
        if len(viejo) != len(nuevo):
            raise ValueError(f"Longitudes distintas: {viejo!r} / {nuevo!r}")
        cuentas[viejo.decode()] = datos.count(viejo)
        if cuentas[viejo.decode()] == 0:
            raise ValueError(f"No aparece en el binario: {viejo!r}")
        datos = datos.replace(viejo, nuevo)
    Path(destino).parent.mkdir(parents=True, exist_ok=True)
    Path(destino).write_bytes(datos)
    return cuentas


def empaquetar(editor, salida, runtime=RUNTIME, extra=()):
    comando = [sys.executable, str(ROOT / "rome/package_standalone.py"), "--editor", str(editor), "--runtime", str(runtime), "--output", str(salida), *extra]
    return subprocess.run(comando, capture_output=True, text=True, timeout=900)


def entradas_runtime(exe):
    with zipfile.ZipFile(exe) as archivo:
        return [info for info in archivo.infolist() if info.filename.startswith("rome_runtime/")]


def lanzar(comando, cwd, entorno, log):
    Path(log).parent.mkdir(parents=True, exist_ok=True)
    salida = open(log, "wb")
    proceso = subprocess.Popen([str(parte) for parte in comando], cwd=str(cwd), env=entorno, stdout=salida, stderr=subprocess.STDOUT)
    proceso.log_handle = salida
    return proceso


def cerrar(proceso, segundos=20):
    if proceso is None:
        return None
    if proceso.poll() is None:
        matar_arbol(proceso.pid)
    try:
        proceso.wait(segundos)
    except subprocess.TimeoutExpired:
        pass
    if getattr(proceso, "log_handle", None):
        proceso.log_handle.close()
    return proceso.returncode


def matar_arbol(pid):
    subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True)


def procesos_de(exe):
    """Procesos cuyo ejecutable es exactamente esa ruta (pid y línea de órdenes)."""
    # El Python de MSYS2 escribe las rutas con "/", y WMI las da con "\".
    ruta = str(Path(exe).resolve()).replace("/", "\\").replace("'", "''")
    script = ("$p = @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq '" + ruta + "' } | "
              "Select-Object ProcessId, CommandLine); ConvertTo-Json -InputObject $p -Compress")
    salida = subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True, timeout=90).stdout.strip()
    if not salida:
        return []
    datos = json.loads(salida)
    return datos if isinstance(datos, list) else [datos]


def matar_ejecutable(exe):
    for proceso in procesos_de(exe):
        matar_arbol(proceso["ProcessId"])
    # taskkill vuelve antes de que el sistema suelte el archivo del exe.
    esperar(lambda: not pids_de(exe), 20, 0.25)


# Ventanas y bloqueos de archivos (Win32).
user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
ENUM = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = [ENUM, wintypes.LPARAM]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
GENERIC_READ = 0x80000000
FILE_SHARE_READ = 0x1
OPEN_EXISTING = 3
INVALID_HANDLE = ctypes.c_void_p(-1).value


def ventanas(pids):
    pids = set(pids)
    resultado = []

    def visitar(hwnd, _parametro):
        if not user32.IsWindowVisible(hwnd):
            return True
        dueno = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(dueno))
        if dueno.value not in pids:
            return True
        largo = user32.GetWindowTextLengthW(hwnd)
        texto = ctypes.create_unicode_buffer(largo + 1)
        user32.GetWindowTextW(hwnd, texto, largo + 1)
        caja = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(caja))
        resultado.append({"hwnd": hwnd, "pid": dueno.value, "titulo": texto.value, "ancho": caja.right - caja.left, "alto": caja.bottom - caja.top})
        return True

    user32.EnumWindows(ENUM(visitar), 0)
    return resultado


class _Entrada(ctypes.Structure):
    _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD), ("th32ProcessID", wintypes.DWORD),
                ("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", wintypes.LONG), ("dwFlags", wintypes.DWORD),
                ("szExeFile", wintypes.WCHAR * 260)]


kernel32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
kernel32.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(_Entrada)]
kernel32.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(_Entrada)]
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]


def copiar_cuando_libre(origen, destino, segundos=30):
    fin = time.monotonic() + segundos
    while True:
        try:
            return shutil.copy2(origen, destino)
        except PermissionError:
            if time.monotonic() > fin:
                raise
            time.sleep(0.5)


def pids_de(exe):
    """Rápido (sin WMI): pids cuyo ejecutable es esa ruta."""
    objetivo = os.path.normcase(str(Path(exe).resolve()))
    nombre = os.path.normcase(Path(exe).name)
    instantanea = kernel32.CreateToolhelp32Snapshot(2, 0)
    entrada = _Entrada()
    entrada.dwSize = ctypes.sizeof(_Entrada)
    pids = []
    try:
        valido = kernel32.Process32FirstW(instantanea, ctypes.byref(entrada))
        while valido:
            if os.path.normcase(entrada.szExeFile) == nombre:
                manejador = kernel32.OpenProcess(0x1000, False, entrada.th32ProcessID)
                if manejador:
                    texto = ctypes.create_unicode_buffer(1024)
                    largo = wintypes.DWORD(1024)
                    if kernel32.QueryFullProcessImageNameW(manejador, 0, texto, ctypes.byref(largo)) and os.path.normcase(texto.value) == objetivo:
                        pids.append(entrada.th32ProcessID)
                    kernel32.CloseHandle(manejador)
            valido = kernel32.Process32NextW(instantanea, ctypes.byref(entrada))
    finally:
        kernel32.CloseHandle(instantanea)
    return pids


def pids_arbol(pid):
    """El pid y sus descendientes (el editor no abre hijos, pero el envoltorio de consola sí)."""
    script = "Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId | ConvertTo-Json -Compress"
    salida = subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True, timeout=90).stdout
    filas = json.loads(salida) if salida.strip() else []
    hijos = {}
    for fila in filas:
        hijos.setdefault(fila["ParentProcessId"], []).append(fila["ProcessId"])
    pendientes, vistos = [pid], set()
    while pendientes:
        actual = pendientes.pop()
        if actual not in vistos:
            vistos.add(actual)
            pendientes.extend(hijos.get(actual, []))
    return vistos


def bloquear(ruta):
    """Abre el archivo dejando solo lectura compartida, como un antivirus que lo examina."""
    manejador = kernel32.CreateFileW(str(ruta), GENERIC_READ, FILE_SHARE_READ, None, OPEN_EXISTING, 0, None)
    if manejador in (None, INVALID_HANDLE):
        raise ctypes.WinError(ctypes.get_last_error())
    return manejador


def soltar(manejador):
    if manejador:
        kernel32.CloseHandle(manejador)


def capturar(hwnd, salida):
    script = Path(__file__).with_name("capturar.ps1")
    return subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script), "-Ventana", str(hwnd), "-Salida", str(salida)],
                          capture_output=True, text=True, timeout=60).stdout.strip()


def documentos_experiencias():
    carpeta = Path(os.path.expanduser("~")) / "Documents/ROME/Experiences"
    return sorted(p.name for p in carpeta.iterdir()) if carpeta.is_dir() else []

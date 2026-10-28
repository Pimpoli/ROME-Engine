// Muestreador de pila del hilo principal (solo con GL_MUESTREO=1 en el entorno).
//
// Para saber A DONDE se va un frame largo, tambien la parte que pasa dentro de
// Godot, sin perfilador externo: cada ~1 ms suspende el hilo principal, apunta
// su pila (hasta 40 direcciones de retorno, desenrolladas con las tablas .pdata
// que lleva todo ejecutable x64 de Windows) y lo suelta. Al cerrar Godot vuelca
// las muestras a GL_MUESTREO_SALIDA (por defecto muestreo.txt). Los nombres se
// ponen despues con la tabla de simbolos (scons ... simbolos=yes).
//
// CON EL HILO PARADO NO SE PUEDE COGER NINGUN CERROJO: si se paro con ese
// cerrojo cogido, nos quedamos esperandolo para siempre (paso: Godot colgado al
// arrancar, con el cerrojo del cargador de DLL). Por eso las tablas .pdata se
// copian aqui (por modulo, ordenadas) y se buscan a mano: RtlLookupFunctionEntry
// coge cerrojos. La lista de modulos se refresca con el hilo principal en marcha.
//
// Va en su propio TU porque <windows.h> choca con nombres de godot-cpp.
// Apagado no cuesta nada: ni hilo ni memoria.

#include "gl_muestreo.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int kProf = 40;
constexpr LONG kMax = 90000;   // 90 s a 1 kHz, ~30 MB
constexpr int kModulos = 512;

struct Muestra {
    int64_t qpc;
    uint32_t n;
    uint64_t pc[kProf];
};

// Un modulo cargado y su tabla de funciones (la del propio modulo en memoria:
// es de solo lectura y vive mientras el modulo este cargado).
struct Modulo {
    uintptr_t base;
    uintptr_t fin;
    const RUNTIME_FUNCTION* funcs;
    DWORD n;
};

Muestra* g_muestras = nullptr;
volatile LONG g_n = 0;
volatile LONG g_activo = 0;
HANDLE g_principal = nullptr;
HANDLE g_hilo = nullptr;
ULONG_PTR g_pila_baja = 0;
ULONG_PTR g_pila_alta = 0;
int64_t g_qpc0 = 0;
uint64_t g_ticks0 = 0;
Modulo g_modulos[kModulos];
int g_n_modulos = 0;

// Lista de modulos con su .pdata, ordenada por base. Coge el cerrojo del
// cargador (EnumProcessModules): SOLO con el hilo principal en marcha.
void refrescar_modulos() {
    HMODULE mods[kModulos];
    DWORD bytes = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &bytes)) return;
    int n = 0;
    const DWORD total = bytes / sizeof(HMODULE);
    for (DWORD i = 0; i < total && i < kModulos; i++) {
        const uintptr_t base = (uintptr_t)mods[i];
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) continue;
        const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) continue;
        const IMAGE_DATA_DIRECTORY& ex = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        Modulo m;
        m.base = base;
        m.fin = base + nt->OptionalHeader.SizeOfImage;
        m.funcs = ex.VirtualAddress ? (const RUNTIME_FUNCTION*)(base + ex.VirtualAddress) : nullptr;
        m.n = ex.VirtualAddress ? ex.Size / sizeof(RUNTIME_FUNCTION) : 0;
        // Insercion ordenada (son pocos y casi siempre ya vienen en orden).
        int j = n++;
        while (j > 0 && g_modulos[j - 1].base > base) { g_modulos[j] = g_modulos[j - 1]; j--; }
        g_modulos[j] = m;
    }
    g_n_modulos = n;
}

// La entrada de .pdata de pc, sin cerrojos: busqueda binaria en la copia.
const RUNTIME_FUNCTION* buscar_funcion(uintptr_t pc, uintptr_t& base) {
    int lo = 0, hi = g_n_modulos - 1, m = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (g_modulos[mid].base <= pc) { m = mid; lo = mid + 1; } else hi = mid - 1;
    }
    if (m < 0 || pc >= g_modulos[m].fin || !g_modulos[m].funcs) return nullptr;
    const Modulo& md = g_modulos[m];
    base = md.base;
    const DWORD rva = (DWORD)(pc - md.base);
    DWORD a = 0, b = md.n;
    while (a < b) {
        const DWORD mid = (a + b) / 2;
        const RUNTIME_FUNCTION& f = md.funcs[mid];
        if (rva < f.BeginAddress) b = mid;
        else if (rva >= f.EndAddress) a = mid + 1;
        else return &md.funcs[mid];
    }
    return nullptr;
}

// Solo la funcion mas interna puede ser una hoja (sin entrada en .pdata): las
// de mas arriba han llamado a otra y tienen que tenerla. Si falta, o la pila se
// sale de la del hilo, se corta ahi: lo que siguiera seria basura.
int desenrollar(CONTEXT& c, uint64_t* pc) {
    int n = 0;
    while (n < kProf && c.Rip != 0) {
        pc[n++] = c.Rip;
        if (c.Rsp < g_pila_baja || c.Rsp + 8 > g_pila_alta) break;
        uintptr_t base = 0;
        const RUNTIME_FUNCTION* f = buscar_funcion((uintptr_t)c.Rip, base);
        if (!f) {
            if (n > 1) break;
            c.Rip = *(DWORD64*)c.Rsp;
            c.Rsp += 8;
            continue;
        }
        const DWORD64 rsp = c.Rsp;
        PVOID datos = nullptr;
        DWORD64 marco = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, (DWORD64)base, c.Rip, (PRUNTIME_FUNCTION)f, &c, &datos, &marco, nullptr);
        if (c.Rsp <= rsp) break;
    }
    return n;
}

DWORD WINAPI hilo_muestreo(LPVOID) {
    // Temporizador de alta resolucion (Windows 10 1803+): Sleep(1) puede dormir
    // 15,6 ms si nadie ha subido la resolucion del reloj.
    HANDLE t = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
    if (!t) t = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    if (!t) return 0;
    LARGE_INTEGER plazo;
    plazo.QuadPart = -10000;   // 1 ms, relativo
    SetWaitableTimer(t, &plazo, 1, nullptr, nullptr, FALSE);
    uint32_t vueltas = 0;
    while (g_activo && g_n < kMax) {
        WaitForSingleObject(t, INFINITE);
        // Modulos nuevos (drivers, DLL que Godot carga tarde): con el hilo
        // principal EN MARCHA, cada medio segundo.
        if ((vueltas++ & 511) == 0) refrescar_modulos();
        if (SuspendThread(g_principal) == (DWORD)-1) break;
        // Con el hilo parado: nada de memoria dinamica, ni llamadas a Godot, ni
        // nada del sistema que coja un cerrojo (ver arriba).
        Muestra& m = g_muestras[g_n];
        CONTEXT c;
        memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_FULL;
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        if (GetThreadContext(g_principal, &c)) {
            m.qpc = q.QuadPart;
            m.n = (uint32_t)desenrollar(c, m.pc);
            g_n = g_n + 1;
        }
        ResumeThread(g_principal);
    }
    CancelWaitableTimer(t);
    CloseHandle(t);
    return 0;
}

} // namespace

void gl_muestreo_iniciar(uint64_t ticks_godot_us) {
    const char* e = getenv("GL_MUESTREO");
    if (!e || strcmp(e, "1") != 0 || g_muestras) return;
    g_muestras = (Muestra*)VirtualAlloc(nullptr, sizeof(Muestra) * kMax, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_muestras) return;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_principal,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) {
        VirtualFree(g_muestras, 0, MEM_RELEASE);
        g_muestras = nullptr;
        return;
    }
    GetCurrentThreadStackLimits(&g_pila_baja, &g_pila_alta);
    refrescar_modulos();
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    g_qpc0 = q.QuadPart;
    g_ticks0 = ticks_godot_us;
    g_activo = 1;
    g_hilo = CreateThread(nullptr, 0, hilo_muestreo, nullptr, 0, nullptr);
    if (g_hilo) SetThreadPriority(g_hilo, THREAD_PRIORITY_TIME_CRITICAL);
}

void gl_muestreo_volcar() {
    if (!g_muestras) return;
    g_activo = 0;
    if (g_hilo) {
        WaitForSingleObject(g_hilo, 3000);
        CloseHandle(g_hilo);
        g_hilo = nullptr;
    }
    const char* ruta = getenv("GL_MUESTREO_SALIDA");
    if (!ruta || !*ruta) ruta = "muestreo.txt";
    if (FILE* f = fopen(ruta, "w")) {
        LARGE_INTEGER fr;
        QueryPerformanceFrequency(&fr);
        fprintf(f, "frecuencia %lld\ninicio %lld %llu\n", (long long)fr.QuadPart, (long long)g_qpc0,
                (unsigned long long)g_ticks0);
        HMODULE mods[1024];
        DWORD bytes = 0;
        if (EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &bytes)) {
            const DWORD total = bytes / sizeof(HMODULE);
            for (DWORD i = 0; i < total && i < 1024; i++) {
                MODULEINFO mi;
                char nombre[MAX_PATH];
                if (GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof mi) &&
                    GetModuleFileNameA(mods[i], nombre, MAX_PATH))
                    fprintf(f, "modulo %llx %lx %s\n", (unsigned long long)(uintptr_t)mi.lpBaseOfDll,
                            (unsigned long)mi.SizeOfImage, nombre);
            }
        }
        const LONG n = g_n;
        for (LONG i = 0; i < n; i++) {
            const Muestra& m = g_muestras[i];
            fprintf(f, "m %lld", (long long)m.qpc);
            for (uint32_t k = 0; k < m.n; k++) fprintf(f, " %llx", (unsigned long long)m.pc[k]);
            fputc('\n', f);
        }
        fclose(f);
    }
    if (g_principal) {
        CloseHandle(g_principal);
        g_principal = nullptr;
    }
    VirtualFree(g_muestras, 0, MEM_RELEASE);
    g_muestras = nullptr;
}

#else
void gl_muestreo_iniciar(uint64_t) {}
void gl_muestreo_volcar() {}
#endif

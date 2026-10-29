// Depurador mínimo para cazar caídas sin cdb ni WinDbg: lanza el programa con la
// API de depuración de Windows y, si se cae, escribe la pila de todos sus hilos
// (módulo+offset, y función y línea cuando hay PDB) y lo que el programa mandó
// con OutputDebugString (ahí cuenta el montón de Windows qué encontró roto).
//
//   depurador.exe [--informe pila.txt] -- programa.exe args...
//
// Devuelve el código de salida del programa.
#include <windows.h>
#include <dbghelp.h>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>

#pragma comment(lib, "dbghelp.lib")

static HANDLE proceso;
static FILE* informe;
static std::map<DWORD, HANDLE> hilos;
static int volcados;
static int avisos;
static int lineas_ods;

static void escribir(const wchar_t* formato, ...) {
	va_list argumentos;
	va_start(argumentos, formato);
	vfwprintf(informe, formato, argumentos);
	va_end(argumentos);
	fflush(informe);
}

// La dirección como módulo+offset y, si hay símbolos, función y línea. En las
// direcciones de retorno se busca el símbolo un byte antes (la llamada).
static std::wstring describir(DWORD64 direccion, bool retorno) {
	wchar_t texto[1600];
	IMAGEHLP_MODULEW64 modulo = { sizeof(modulo) };
	bool hay_modulo = SymGetModuleInfoW64(proceso, direccion, &modulo);
	if (hay_modulo) {
		swprintf(texto, 1600, L"%ls+0x%llx", modulo.ModuleName, direccion - modulo.BaseOfImage);
	} else {
		swprintf(texto, 1600, L"0x%llx", direccion);
	}
	std::wstring resultado = texto;
	if (!hay_modulo) {
		return resultado;
	}
	DWORD64 busqueda = direccion - (retorno ? 1 : 0);
	alignas(SYMBOL_INFOW) char memoria[sizeof(SYMBOL_INFOW) + 512 * sizeof(wchar_t)] = {};
	SYMBOL_INFOW* simbolo = reinterpret_cast<SYMBOL_INFOW*>(memoria);
	simbolo->SizeOfStruct = sizeof(SYMBOL_INFOW);
	simbolo->MaxNameLen = 511;
	DWORD64 desplazamiento = 0;
	if (SymFromAddrW(proceso, busqueda, &desplazamiento, simbolo)) {
		// Sin PDB solo hay exportaciones: el nombre es la exportación anterior más cercana.
		swprintf(texto, 1600, L"  %ls%ls+0x%llx", modulo.SymType == SymExport ? L"~" : L"", simbolo->Name, desplazamiento + (retorno ? 1 : 0));
		resultado += texto;
		IMAGEHLP_LINEW64 linea = { sizeof(linea) };
		DWORD columna = 0;
		if (SymGetLineFromAddrW64(proceso, busqueda, &columna, &linea)) {
			swprintf(texto, 1600, L"  [%ls:%lu]", linea.FileName, linea.LineNumber);
			resultado += texto;
		}
	}
	return resultado;
}

static void pila(HANDLE hilo, int maximo) {
	CONTEXT contexto = {};
	contexto.ContextFlags = CONTEXT_FULL;
	if (!GetThreadContext(hilo, &contexto)) {
		escribir(L"  (sin contexto: error %lu)\n", GetLastError());
		return;
	}
	STACKFRAME64 marco = {};
	marco.AddrPC.Offset = contexto.Rip;
	marco.AddrPC.Mode = AddrModeFlat;
	marco.AddrFrame.Offset = contexto.Rbp;
	marco.AddrFrame.Mode = AddrModeFlat;
	marco.AddrStack.Offset = contexto.Rsp;
	marco.AddrStack.Mode = AddrModeFlat;
	for (int i = 0; i < maximo; i++) {
		if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proceso, hilo, &marco, &contexto, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !marco.AddrPC.Offset) {
			break;
		}
		escribir(L"  %2d %ls\n", i, describir(marco.AddrPC.Offset, i > 0).c_str());
	}
}

static std::wstring nombre_hilo(DWORD id) {
	std::wstring nombre;
	HANDLE consulta = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
	PWSTR descripcion = nullptr;
	if (consulta && SUCCEEDED(GetThreadDescription(consulta, &descripcion)) && descripcion) {
		nombre = descripcion;
		LocalFree(descripcion);
	}
	if (consulta) {
		CloseHandle(consulta);
	}
	return nombre;
}

static void volcar(DWORD hilo_fallo, const EXCEPTION_RECORD& registro, bool primera) {
	volcados++;
	escribir(L"\n=== excepción 0x%08lx (%ls oportunidad) en %ls, hilo %lu ===\n", registro.ExceptionCode, primera ? L"primera" : L"segunda",
			describir((DWORD64)registro.ExceptionAddress, false).c_str(), hilo_fallo);
	for (DWORD i = 0; i < registro.NumberParameters; i++) {
		escribir(L"  parámetro %lu: 0x%llx\n", i, (unsigned long long)registro.ExceptionInformation[i]);
	}
	auto un_hilo = [](DWORD id, HANDLE hilo, int maximo) {
		escribir(L"\n-- hilo %lu %ls\n", id, nombre_hilo(id).c_str());
		pila(hilo, maximo);
	};
	if (hilos.count(hilo_fallo)) {
		un_hilo(hilo_fallo, hilos[hilo_fallo], 100);
	}
	for (auto& [id, hilo] : hilos) {
		if (id != hilo_fallo) {
			un_hilo(id, hilo, 40);
		}
	}
}

static void cargar_modulo(HANDLE archivo, void* base) {
	wchar_t ruta[1024] = L"";
	if (archivo) {
		GetFinalPathNameByHandleW(archivo, ruta, 1024, FILE_NAME_NORMALIZED);
	}
	const wchar_t* limpia = wcsncmp(ruta, L"\\\\?\\", 4) ? ruta : ruta + 4;
	SymLoadModuleExW(proceso, archivo, limpia[0] ? limpia : nullptr, nullptr, (DWORD64)base, 0, nullptr, 0);
	// Sin cerrarlo, el ejecutable quedaría bloqueado y no se podría instalar encima al cerrar.
	if (archivo) {
		CloseHandle(archivo);
	}
}

static void cadena_depuracion(const OUTPUT_DEBUG_STRING_INFO& info) {
	if (lineas_ods >= 3000 || !info.nDebugStringLength) {
		return;
	}
	lineas_ods++;
	SIZE_T leidos = 0;
	if (info.fUnicode) {
		std::wstring texto(info.nDebugStringLength, L'\0');
		ReadProcessMemory(proceso, info.lpDebugStringData, texto.data(), info.nDebugStringLength * sizeof(wchar_t), &leidos);
		texto.resize(wcsnlen(texto.c_str(), leidos / sizeof(wchar_t)));
		escribir(L"ODS: %ls%ls", texto.c_str(), !texto.empty() && texto.back() == L'\n' ? L"" : L"\n");
	} else {
		std::string texto(info.nDebugStringLength, '\0');
		ReadProcessMemory(proceso, info.lpDebugStringData, texto.data(), info.nDebugStringLength, &leidos);
		texto.resize(strnlen(texto.c_str(), leidos));
		escribir(L"ODS: %hs%ls", texto.c_str(), !texto.empty() && texto.back() == '\n' ? L"" : L"\n");
	}
}

static bool es_grave(DWORD codigo) {
	switch (codigo) {
		case EXCEPTION_ACCESS_VIOLATION:
		case EXCEPTION_ILLEGAL_INSTRUCTION:
		case EXCEPTION_STACK_OVERFLOW:
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
		case 0xC0000374: // STATUS_HEAP_CORRUPTION
		case 0xC0000409: // STATUS_STACK_BUFFER_OVERRUN (__fastfail)
		case 0xC0000602: // STATUS_FAIL_FAST_EXCEPTION
			return true;
	}
	return false;
}

static std::wstring citar(const wchar_t* argumento) {
	if (*argumento && !wcspbrk(argumento, L" \t\"")) {
		return argumento;
	}
	std::wstring resultado = L"\"";
	for (const wchar_t* c = argumento; *c; c++) {
		if (*c == L'"') {
			resultado += L'\\';
		}
		resultado += *c;
	}
	return resultado + L"\"";
}

int wmain(int argc, wchar_t** argv) {
	const wchar_t* ruta_informe = L"pila.txt";
	int primero = 1;
	while (primero < argc) {
		if (!wcscmp(argv[primero], L"--informe") && primero + 1 < argc) {
			ruta_informe = argv[primero + 1];
			primero += 2;
		} else if (!wcscmp(argv[primero], L"--")) {
			primero++;
			break;
		} else {
			break;
		}
	}
	if (primero >= argc) {
		fwprintf(stderr, L"uso: depurador [--informe pila.txt] -- programa args...\n");
		return 2;
	}
	std::wstring linea;
	for (int i = primero; i < argc; i++) {
		linea += (i > primero ? L" " : L"") + citar(argv[i]);
	}
	if (_wfopen_s(&informe, ruta_informe, L"w, ccs=UTF-8") || !informe) {
		fwprintf(stderr, L"no se puede escribir %ls\n", ruta_informe);
		return 2;
	}
	escribir(L"%ls\n", linea.c_str());

	STARTUPINFOW inicio = { sizeof(inicio) };
	inicio.dwFlags = STARTF_USESTDHANDLES;
	inicio.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	inicio.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	inicio.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	PROCESS_INFORMATION info = {};
	if (!CreateProcessW(nullptr, linea.data(), nullptr, nullptr, TRUE, DEBUG_ONLY_THIS_PROCESS, nullptr, nullptr, &inicio, &info)) {
		escribir(L"CreateProcess falló: %lu\n", GetLastError());
		return 3;
	}
	proceso = info.hProcess;
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
	SymInitializeW(proceso, nullptr, FALSE);

	DWORD codigo_salida = 0;
	bool break_inicial = false;
	DEBUG_EVENT evento;
	while (WaitForDebugEvent(&evento, INFINITE)) {
		DWORD continuar = DBG_CONTINUE;
		switch (evento.dwDebugEventCode) {
			case CREATE_PROCESS_DEBUG_EVENT:
				hilos[evento.dwThreadId] = evento.u.CreateProcessInfo.hThread;
				cargar_modulo(evento.u.CreateProcessInfo.hFile, evento.u.CreateProcessInfo.lpBaseOfImage);
				break;
			case CREATE_THREAD_DEBUG_EVENT:
				hilos[evento.dwThreadId] = evento.u.CreateThread.hThread;
				break;
			case EXIT_THREAD_DEBUG_EVENT:
				hilos.erase(evento.dwThreadId);
				break;
			case LOAD_DLL_DEBUG_EVENT:
				cargar_modulo(evento.u.LoadDll.hFile, evento.u.LoadDll.lpBaseOfDll);
				break;
			case UNLOAD_DLL_DEBUG_EVENT:
				SymUnloadModule64(proceso, (DWORD64)evento.u.UnloadDll.lpBaseOfDll);
				break;
			case OUTPUT_DEBUG_STRING_EVENT:
				cadena_depuracion(evento.u.DebugString);
				break;
			case EXCEPTION_DEBUG_EVENT: {
				const EXCEPTION_RECORD& registro = evento.u.Exception.ExceptionRecord;
				bool primera = evento.u.Exception.dwFirstChance != 0;
				if (registro.ExceptionCode == EXCEPTION_BREAKPOINT && !break_inicial) {
					break_inicial = true;
					break;
				}
				bool parada = registro.ExceptionCode == EXCEPTION_BREAKPOINT;
				if ((!primera || parada || es_grave(registro.ExceptionCode)) && volcados < 6) {
					volcar(evento.dwThreadId, registro, primera);
				} else if (avisos < 200) {
					avisos++;
					escribir(L"excepción 0x%08lx (%ls) en %ls\n", registro.ExceptionCode, primera ? L"primera" : L"segunda",
							describir((DWORD64)registro.ExceptionAddress, false).c_str());
				}
				continuar = parada && volcados < 6 ? DBG_CONTINUE : DBG_EXCEPTION_NOT_HANDLED;
				break;
			}
			case EXIT_PROCESS_DEBUG_EVENT:
				codigo_salida = evento.u.ExitProcess.dwExitCode;
				break;
		}
		ContinueDebugEvent(evento.dwProcessId, evento.dwThreadId, continuar);
		if (evento.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
			break;
		}
	}
	escribir(L"\nsalida 0x%08lx\n", codigo_salida);
	SymCleanup(proceso);
	fclose(informe);
	return (int)codigo_salida;
}

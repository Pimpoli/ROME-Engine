// ════════════════════════════════════════════════════════════════════
//  GodotLuau — comprobador de sintaxis Luau (para la CI)
//
//  Compila con el MISMO compilador de Luau que lleva la extension cada
//  archivo que se le pase por linea de comandos. No ejecuta nada: solo
//  contesta si el codigo compila o en que linea esta el error.
//
//  Existe porque el Luau del proyecto vive en dos sitios que NADIE compila
//  hasta que el juego arranca:
//    - los .luau de GodotLuau/DefaultScripts/ (plantillas de fabrica),
//    - las cadenas R"LUAU(...)LUAU" embebidas en los headers (el stdlib).
//  Un parentesis de menos ahi no rompe la build de C++: rompe el juego del
//  usuario en tiempo de ejecucion, con un "[GodotLuau Stdlib Syntax]" y la
//  mitad de los servicios sin existir.
//
//  Compilar (lo hace la CI):
//    g++ -std=c++17 -O1 -I luau/Ast/include -I luau/Compiler/include \
//        -I luau/Common/include -I luau/Compiler/src \
//        tools/check_luau.cpp luau/Ast/src/*.cpp luau/Compiler/src/*.cpp \
//        luau/Common/src/*.cpp -o check_luau
// ════════════════════════════════════════════════════════════════════
#include "Luau/Compiler.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static bool leer(const std::string& ruta, std::string& out) {
    std::ifstream f(ruta, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    // Quitar el BOM UTF-8: el lexer de Luau lo trata como caracter invalido.
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF
                        && (unsigned char)out[1] == 0xBB
                        && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: check_luau <archivo.luau> [...]\n");
        return 2;
    }

    int fallos = 0;
    for (int i = 1; i < argc; i++) {
        const std::string ruta = argv[i];
        std::string src;
        if (!leer(ruta, src)) {
            std::fprintf(stderr, "ERROR  no se pudo leer %s\n", ruta.c_str());
            fallos++;
            continue;
        }

        // luau_load acepta tanto bytecode valido como bytecode de error; el de
        // error empieza por un 0 seguido del mensaje. Es el mismo criterio que
        // usa la extension en _validate.
        std::string bc = Luau::compile(src);
        if (!bc.empty() && bc[0] == '\0') {
            // El mensaje de Luau ya empieza por ":<linea>: ...".
            std::fprintf(stderr, "ERROR  %s%s\n", ruta.c_str(), bc.c_str() + 1);
            fallos++;
        } else {
            std::printf("ok     %s (%zu bytes -> %zu de bytecode)\n", ruta.c_str(), src.size(), bc.size());
        }
    }

    if (fallos) {
        std::fprintf(stderr, "\n%d archivo(s) con errores de sintaxis Luau.\n", fallos);
        return 1;
    }
    std::printf("\n%d archivo(s) Luau comprobados, todo correcto.\n", argc - 1);
    return 0;
}

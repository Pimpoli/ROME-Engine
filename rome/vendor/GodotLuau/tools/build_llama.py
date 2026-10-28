"""
GodotLuau — Compila llama.cpp para la extension opcional "GodotLuau IA".
=======================================================================
Deja las librerias estaticas en llama.cpp/build/ para que despues

    scons platform=windows target=template_debug use_mingw=yes build_ia=yes

pueda enlazarlas. Se separa del SConstruct porque llama.cpp trae su propio
CMake con cientos de archivos y banderas por backend: reimplementar eso en
SCons seria fragil y habria que rehacerlo en cada actualizacion.

CPU por ahora. El backend Vulkan (que aprovecharia la GPU que Godot ya usa)
se enciende con --vulkan, pero hay que MEDIRLO antes de darlo por bueno.
"""
import os
import subprocess
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LC = os.path.join(RAIZ, "llama.cpp")


def main():
    if not os.path.isdir(LC):
        print("No existe llama.cpp/. Trae el submodulo con:")
        print("   git submodule update --init --recursive llama.cpp")
        return 1
    vulkan = "--vulkan" in sys.argv
    build = os.path.join(LC, "build")
    conf = [
        "cmake", "-B", build, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_SHARED_LIBS=OFF",
        "-DLLAMA_CURL=OFF",
        "-DLLAMA_BUILD_TESTS=OFF", "-DLLAMA_BUILD_EXAMPLES=OFF",
        "-DLLAMA_BUILD_TOOLS=OFF", "-DLLAMA_BUILD_SERVER=OFF",
        # NATIVE=OFF: el binario se reparte a otras maquinas, no puede
        # compilarse para las instrucciones EXACTAS de esta CPU.
        "-DGGML_NATIVE=OFF", "-DGGML_OPENMP=OFF",
        "-DCMAKE_C_COMPILER=gcc", "-DCMAKE_CXX_COMPILER=g++",
    ]
    if vulkan:
        conf.append("-DGGML_VULKAN=ON")
    print(" ".join(conf))
    if subprocess.call(conf, cwd=LC) != 0:
        return 1
    return subprocess.call(["cmake", "--build", build, "--target", "llama", "-j8"], cwd=LC)


if __name__ == "__main__":
    sys.exit(main())

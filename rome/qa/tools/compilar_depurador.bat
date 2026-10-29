@echo off
rem Compila depurador.cpp en bin\depurador.exe. Sin cl en el PATH, usa el MSVC de Visual Studio 18.
where cl >nul 2>nul || call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /O2 /W3 /std:c++17 /utf-8 "%~dp0depurador.cpp" /Fo"%TEMP%\\" /Fe"%~dp0..\..\..\bin\depurador.exe" /link dbghelp.lib

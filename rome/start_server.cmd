@echo off
title ROME - Servidor propio
python "%~dp0rome_runtime\project\GodotLuau\DedicatedServer\manager\server.py" --engine "%~dp0ROME Studio.exe" --data "%USERPROFILE%\ROME Servers"
if errorlevel 1 (
  echo.
  echo Revisa el mensaje anterior. Se necesita Python 3.10 o posterior.
  pause
)

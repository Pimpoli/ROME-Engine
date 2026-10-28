# Procedencia y actualizaciones de la base

ROME Engine 0.1.0-alpha.1 parte de Godot Engine 4.8 beta 1, revisión
`4d10419cacd6d3009976e8f9fd3abd970fd58018` del repositorio
https://github.com/godotengine/godot.

El repositorio público de ROME contiene una instantánea completa del código y
conserva su commit inicial de documentación. No incorpora todo el historial
histórico de Godot ni pertenece automáticamente a su red de forks en GitHub.
Las licencias, los créditos y las notas de procedencia se conservan.

Los cambios posteriores de Godot se integran manualmente y se prueban. Para
preparar una actualización, compara la revisión indicada con la versión elegida
en un checkout aparte de Godot, aplica ese delta a una rama de ROME, resuelve
los conflictos y repite la compilación y las pruebas del editor y de GodotLuau.
No reemplaces el árbol de ROME por una descarga nueva: perderías sus cambios.

`rome/vendor/GodotLuau/` contiene una copia autorizada del checkout local usado
para esta versión, incluidos sus cambios todavía no publicados por separado.
Su licencia MIT y su documentación original están en ese directorio.
`dependencies.json` registra la revisión base y las revisiones públicas exactas
de godot-cpp y Luau. `python rome/setup_dependencies.py` descarga esas dos
dependencias. No se incluyen modelos opcionales de IA, cachés, binarios ni los
archivos ZIP locales del checkout original.

La copia de compilación en `bin/rome_extension_build/` recibe las adaptaciones
de compatibilidad descritas en `rome/build_runtime.py`; el checkout original
del equipo de GodotLuau permanece independiente.

# Contribuir a ROME Engine

ROME desarrolla una interfaz y un flujo de creación propios sobre Godot, con
Luau integrado mediante GodotLuau. Agradecemos correcciones, mejoras de uso,
documentación y pruebas reproducibles.

La versión pública actual es **0.1.0-alpha.1**. Las descargas y su estado por
plataforma se publican en la
[pre-release correspondiente](https://github.com/Pimpoli/ROME-Engine/releases/tag/v0.1.0-alpha.1).

## Errores y propuestas

Abre un [issue en ROME-Engine](https://github.com/Pimpoli/ROME-Engine/issues)
con el resultado esperado, los pasos para reproducirlo y el resultado obtenido.
Para errores, añade la revisión de ROME, Windows y renderizador utilizados, y
un proyecto mínimo cuando sea necesario.

Los cambios específicos del editor de ROME se revisan aquí. GodotLuau se mantiene
en [su propio repositorio](https://github.com/Pimpoli/GodotLuau): identifica qué
componente produce el problema antes de cambiar su API o sus scripts por defecto.
ROME incorpora los fuentes utilizados por esta integración en
`rome/vendor/GodotLuau/`. Cambiar esa copia es una modificación de la dependencia
y debe revisarse como tal; no hace falta editar un checkout externo para
construir ROME.
Para un error del núcleo, comprobar si también ocurre en Godot sin las
modificaciones de ROME ayuda a delimitarlo.

## Dónde está cada parte

| Área | Fuentes principales |
| --- | --- |
| Barra y pestañas de herramientas | `editor/rome_studio.cpp`, `editor/rome_ribbon_customization.cpp` |
| Pestañas de escenas y scripts | `editor/scene/editor_scene_tabs.*`, `editor/script/` |
| Inicio y creación de experiencias | `editor/project_manager/rome_project_hub.cpp`, `rome_project_template.cpp` |
| Recursos, servicios y scripts del editor | `rome/addons/ROMEStudio/` |
| Classic Obby y su código Luau | `studio_obby.gd` y `templates/` dentro de ROMEStudio |
| Marca, iconos y miniaturas | `rome/assets/`, `rome/make_studio_icons.py`, `rome/render_previews.gd` |
| Construcción y empaquetado | `rome/build_runtime.py`, `rome/package_runtime.py` |
| Paquete autónomo | `rome/package_standalone.py` |
| Fuentes de GodotLuau incluidos | `rome/vendor/GodotLuau/` |
| Preparación de dependencias públicas | `rome/setup_dependencies.py` |
| Comprobaciones de integración | `rome/qa/` |

El resto del árbol conserva la estructura de Godot. La
[guía de contribución original](CONTRIBUTING.godot.md) y la
[documentación técnica de Godot](https://contributing.godotengine.org/)
sirven de referencia para el núcleo y el estilo de C++.
El origen del código y las actualizaciones del núcleo se describen en
[UPSTREAM.md](UPSTREAM.md). El repositorio publica una instantánea de un derivado
de Godot; no es un fork registrado en la red de repositorios de GitHub.

## Preparar un cambio

1. Ejecuta `python rome/setup_dependencies.py` para preparar las revisiones
   públicas fijadas de `godot-cpp` y `luau`. Compila y empaqueta ROME siguiendo
   el [README](README.md#compilar-en-windows), con `rome/vendor/GodotLuau` como
   origen de la extensión.
2. Acota la modificación a un problema o una función que pueda revisarse.
3. Conserva los identificadores y formatos internos cuando un cambio de
   presentación sea suficiente. Traduce las etiquetas visibles sin renombrar
   rutas persistentes o referencias de escenas.
4. Mantén las licencias y atribuciones existentes. No incluyas `bin/`, cachés,
   ejecutables, DLL ni proyectos de prueba personales en el cambio.
   Tampoco añadas modelos de IA, archivos de una instalación personal ni clones
   completos de las dependencias que prepara `setup_dependencies.py`.
5. Describe el comportamiento resultante y cómo lo comprobaste en la propuesta.

Respeta el estilo del archivo que edites. Los cambios de apariencia deben
revisarse también en una ventana estrecha y con otra escala del editor cuando
afecten al tamaño o a la disposición de los controles.

## Comprobaciones

Como mínimo, compila las áreas nativas modificadas y comprueba el diff:

```powershell
scons platform=windows target=editor -j6
git diff --check
```

Para cambios de ROMEStudio, actualiza el paquete antes de crear la experiencia
de prueba. Si sus componentes de GodotLuau ya están preparados:

```powershell
python rome/package_runtime.py --rome-only
```

### Integración del editor

`rome/qa/smoke.gd` es un **EditorPlugin de pruebas**. Crea objetos y archivos,
cambia escenas y pestañas, ejecuta Deshacer y cierra el editor al terminar.
Úsalo solamente en una experiencia desechable, nunca en tu juego de trabajo.

1. Crea una experiencia Baseplate nueva con la compilación actual de ROME y
   cierra su editor.
2. Copia el contenido de `rome/qa/` a `addons/ROMESmoke/` dentro de esa experiencia.
3. En el `project.godot` de la experiencia, añade ROMESmoke a los plugins activos:

   ```ini
   [editor_plugins]
   enabled=PackedStringArray("res://addons/GodotLuauUpdater/plugin.cfg", "res://addons/ROMEStudio/plugin.cfg", "res://addons/ROMESmoke/plugin.cfg")
   ```

4. Desde la raíz del repositorio, ejecuta el editor de consola. Sustituye las
   rutas del ejemplo por las de tu experiencia y un directorio de logs existente:

   ```powershell
   & './bin/godot.windows.editor.x86_64.console.exe' --headless --editor --path 'C:/ROME-QA/Baseplate' --max-fps 60 --quit-after 3000 --log-file 'C:/ROME-QA/editor-tests.log'
   ```

El resultado se escribe en `integration-result.json` dentro de la experiencia.
Revisa también los errores del log. El límite de frames es una salida de
seguridad; que el proceso termine no sustituye un resultado satisfactorio.
Desactiva ROMESmoke después de las pruebas si vas a abrir el proyecto manualmente.

La suite incluye comprobaciones específicas de pestañas compartidas,
personalización de la barra, organización de scripts, servicios, Recursos y
escenas reutilizables. Los detalles están en `document_tabs_checks.gd`,
`custom_tabs_checks.gd` y `script_files_checks.gd`.

### Classic Obby en ejecución

Crea otra experiencia desechable desde **Classic Obby**. Copia
`rome/qa/obby_runtime.gd` a la raíz de esa experiencia y ejecuta:

```powershell
& './bin/godot.windows.editor.x86_64.console.exe' --headless --path 'C:/ROME-QA/ClassicObby' --script res://obby_runtime.gd
```

La prueba utiliza la escena principal configurada en el proyecto y escribe
`runtime-result.json`. Comprueba movimiento local, transporte del personaje,
contactos letales, checkpoints, caídas y respawn. Estas pruebas locales no
demuestran por sí solas el comportamiento multijugador entre procesos separados.

### Revisión visual

Después de los checks automáticos, abre la experiencia y comprueba la función
que cambiaste. En pestañas, alterna entre dos scripts y dos escenas, cierra un
script y verifica que vuelves al documento correcto. En Recursos, crea carpetas,
busca archivos y prueba una escena reutilizable con Deshacer.

Para cambios en la galería, el generador de miniaturas requiere el renderizador
Forward+. Conserva el diseño compartido con la plantilla cuando cambies el Obby;
la imagen debe representar el recorrido que se crea.

## Preparar un paquete de la alpha

La versión pública de ROME se identifica mediante `ROME_VERSION` en
`core/version.h`. La versión técnica heredada de Godot se conserva para la
compatibilidad del núcleo y de las extensiones.

Después de construir y empaquetar el editor según el README, genera el ejecutable
autónomo con `rome/package_standalone.py`. Pruébalo desde una ubicación que no
tenga una carpeta `rome_runtime` vecina: debe abrir el inicio de ROME y extraer
sus componentes a la caché del usuario. Comprueba también que puede crear una
experiencia y cargar la extensión. La herramienta escribe un checksum SHA-256
junto al archivo generado.

Publica solo los paquetes construidos y comprobados. En esta alpha, Linux x86_64
depende de que su workflow termine correctamente y de adjuntar su resultado a
la versión. Android, macOS y otras plataformas no deben anunciarse como
descargables mientras no exista un binario de ROME validado para ellas.

Si actualizas la copia de GodotLuau incluida, conserva sus licencias y documenta
la revisión y los cambios incorporados. Sus dependencias públicas se fijan en
`rome/vendor/GodotLuau/dependencies.json`; los checkouts que descarga el script
de preparación no forman parte de la instantánea versionada.

## Enviar una propuesta

Explica el problema concreto, el resultado del cambio y las pruebas realizadas.
Incluye capturas para cambios visuales, usando una experiencia de ejemplo.
Señala las limitaciones conocidas, especialmente si dependen de GodotLuau o de
una plataforma que no hayas probado.

Los cambios que afectan a recursos persistentes deben explicar cómo conservan
las escenas existentes, las referencias de scripts y los datos del usuario.

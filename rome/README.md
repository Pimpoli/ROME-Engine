# ROME Engine

Local fork of Godot 4.8 beta 1, based on commit
`4d10419cacd6d3009976e8f9fd3abd970fd58018`.

## Build and run

From the repository root in PowerShell:

```powershell
scons platform=windows target=editor -j6
python rome/build_runtime.py --source 'C:/Users/pimpo/Desktop/GodotLuau' --jobs 6 --target both
python rome/package_runtime.py --source 'C:/Users/pimpo/Desktop/GodotLuau' --binary-source './bin/rome_extension_build/GodotLuau/bin'
./bin/godot.windows.editor.x86_64.exe --project-manager
```

Keep `bin/rome_runtime` beside the executable. It contains the default project
payload, GodotLuau extension, ROMEStudio plugin, and gallery artwork. The original
GodotLuau checkout is read only; compatibility changes are made to a build copy.
They address the inherited gravity method and TextDirection property collisions
in Godot 4.8 while preserving the Luau property API.

New experiences are stored in Documents/ROME/Experiences. If that location is
unavailable, the hub uses the persistent application data directory instead
(on this computer: AppData/Roaming/ROME/Experiences).

## Branding

`assets/branding/rome.svg` and `rome.png` are the supplied symbol used for small
icons. `rome-engine.svg` and `rome-engine.png` are the supplied full wordmark used
for startup artwork and full logos. The original user files are unchanged.

Regenerate the desktop, Android, Apple, web, and project defaults with:

```powershell
python rome/apply_branding.py --svg 'C:/Users/pimpo/Downloads/Rocket-Meteor-logos/rocket-meteor-roblox.svg' --png 'C:/Users/pimpo/Downloads/Rocket-Meteor-logos/png/rocket-meteor-icono-grupo-roblox-1024.png' --wordmark-svg 'C:/Users/pimpo/Downloads/ROME-Engine.svg' --wordmark-png 'C:/Users/pimpo/Downloads/Logo ROME Engine limpio y transparente.png'
```

Then rebuild the editor and repackage the runtime. Existing projects retain
their own artwork unless explicitly updated. Copyright, contributor credits,
and dependency licenses remain in place.

## Studio interface

The fork adds a project hub, local template gallery, ribbon actions, an Explorer,
and a property inspector backed by GodotLuau objects. This is an ongoing editor
conversion; complete Roblox Studio behavior and cloud services are not implemented.

The hub hero artwork and its generation prompt are documented in
`assets/hub/hero.prompt.md`.

The latest continuation notes, toolbar behavior, and remaining work are in
`PROGRESS.md`. Public launchers are `bin/ROME Studio.exe` and, while that version
is already running, `bin/ROME Studio Preview.exe`.


## Refresh only ROME

When GodotLuau is maintained by a separate team, preserve the existing bundle:

```powershell
python rome/package_runtime.py --rome-only
```

This updates ROMEStudio, its template artwork, the gallery and the editor copy.
It leaves the extension's code, updater addon and binaries intact. Gameplay
sources for the new Classic Obby live in `addons/ROMEStudio/templates/` and are
copied into each new game's `Scripts/ClassicObby/` directory for editing.

## 2D foundation

The hub offers `2d` (Game2D) and `mixed` (Game3D with an ordinary World2D branch).
`studio_2d.gd` builds new-experience scaffolding after the extension creates its
services. It keeps unfinished 3D factory startup scripts disabled in pure 2D;
it does not rewrite their source. Game2D's existing native Character2D supplies
movement, gravity and jump. Its current respawn/network limitations remain.

The ribbon's 2D/3D buttons remember the active world per scene during the
session. Resource scenes and native 2D insertion share routing, placement and
Undo. Physics shapes require a body/area and followers require a path, including
compatible reusable-scene roots. Composition uses explicit SubViewport targets.

Editor QA: `qa/twod_checks.gd` in the normal disposable integration fixture.
Template QA: enable `qa/twod_template_bake.gd` only as an EditorPlugin in a new
disposable 2d/mixed project. After it saves and quits, run
`--headless --path <fixture> --script <copied twod_runtime_checks.gd> --fixed-fps 60`
to check actual movement, jumping and collision. Never enable the bake plugin
in a creator's real experience.

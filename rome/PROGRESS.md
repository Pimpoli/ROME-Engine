# ROME Studio: continuation point

## Current interface

- Six standard ribbon tabs: Inicio, Avatar, UI, Script, Modelo, Plugins.
- Extra is not a standard tab. The + button creates a named, empty custom tab.
- Agregar herramientas opens a searchable checklist of ROME commands. Selected
  shortcuts retain their order and are saved in the editor preferences under
  `rome/studio/custom_tabs`. Right-click a custom tab to rename, duplicate, reorder, or remove it. Tools support ordering, removal, and persistent dividers.
- Toolbar selection changes the ribbon, leaving the current viewport intact.
- Avatar and Modelo share transform tools and snapping; UI and Script have
  distinct tool groups. Some advanced tools are visibly disabled because their
  ROME implementation is not available yet.
- Recursos opens the ROME scene and asset browser; Archivos exposes the complete filesystem. Nueva escena is also
  available as a custom shortcut.

## Main scene and reusable scenes

The canonical main scene is `application/run/main_scene`, resolved through its
resource UID when necessary. Its filename is independent of the game's name.
The editor reopens it if a ROME project starts without an active scene. Closing
its tab uses the existing return-to-project-manager flow, including save prompts.

New reusable scenes start with an ordinary Node3D named Model. They do not
receive the service hierarchy. Their files remain regular native PackedScenes.

The bundled `studio_services.gd` maintains the main Game3D root's named service
containers and order. It recreates removed containers and handles restoration
of an original container after a temporary replacement. RunService stays
available internally while the default Explorer list omits it.

The current GodotLuau baseline already hides its Parts in storage services, disables their physics, and copies StarterGui into the local PlayerGui. Arbitrary native renderable nodes outside Workspace are not universally isolated; that remains a compatibility boundary. No new runtime override was added.

## Source and validation

Native ribbon: `editor/rome_studio.cpp` and
`editor/rome_ribbon_customization.cpp`. Editor layer:
`rome/addons/ROMEStudio/`. Integration fixture: `rome/qa/`.

Build and packaging instructions remain in `rome/README.md`. The package script
updates `bin/ROME Studio.exe`; if that executable is running, it writes the
updated executable to `bin/ROME Studio Preview.exe` instead of interrupting it.
Both use the adjacent `bin/rome_runtime` bundle.

This iteration's disposable project is `bin/rome_ribbon_qa/`. Results are written
to its `integration-result.json`; the engine log is `bin/rome_ribbon_tests.log`.
The user's original GodotLuau source checkout remains unchanged. Working changes
have not been committed. Do not reset or discard the working tree.

## References and remaining visual work

Reference: the user's nine new Studio captures, particularly the six tab layouts.
Official behavior references:

- https://create.roblox.com/docs/studio/overview#custom-tabs
- https://create.roblox.com/docs/projects/data-model

Further visual comparison is needed for the panel action
buttons, dropdown details, and script diagnostics. ROME is not yet a complete
1:1 recreation of Roblox Studio. Installed Roblox plugins in the screenshots
are not bundled or simulated as working ROME plugins.

## Last validation

Native Windows editor build succeeded. The disposable editor integration fixture
passed 26 checks without ERROR or SCRIPT ERROR entries. The updated default
ribbon layout was visually reviewed in the preview window. Full 1:1 parity
remains unfinished, as listed above.

## Template and respawn pass

The hub now offers only Baseplate, En blanco, and Classic Obby, with larger
cards, descriptions, and square 1024 px rendered thumbnails. Generate previews
with rome/render_previews.gd using --rendering-method forward_plus.
Respawn artwork is an original eight-point curved sun in
rome/assets/templates/spawn_marker.svg and its transparent 1024 px PNG.
SpawnSymbol/Artwork is nested under a separate Node3D, preventing GodotLuau
from adopting the decal as the Part mesh on reload. Existing and newly added
SpawnLocations receive this artwork through the bundled editor plugin.
The previous integration run passed 27 checks including the separate decal mesh.


## Parkour and workflow pass — 2026-10-28

Classic Obby is now a six-stage route: jumps, red contact hazards, three moving
platforms, balance steps, zigzag, and a finish ascent. Six checkpoint islands,
a progress HUD, signs, and a newly rendered gallery image accompany it.
`studio_obby.gd` shares its geometry layout with `render_previews.gd`.

Movement and grounded-passenger transport run in ObbyClient (LocalScript under
StarterPlayerScripts). Static hazard Touched handlers, fall deaths, checkpoint
progress and checkpoint respawns run in ObbyServer (ServerScriptService), with
server/client context guards. Editable copies are created in
Scripts/ClassicObby/*.lua; the template sources use Luau syntax. The bundled
loader currently registers .lua resources. Two-process multiplayer was not
verified; local play and actual physical contacts were verified.

The Resources dock filters user scenes/models, images, audio, and other assets.
It opens reusable scenes and inserts native PackedScene instances with Undo.
Bundled implementation directories are excluded from this view. Archivos opens
the complete native browser. Main and reusable scenes retain their prior flow.

Services and StarterPlayer script folders repair their canonical names/order
and preserve content created in replacement containers when Undo restores an
original. Native Explorer blocks moving fixed services before the extension
could discard a service at an invalid parent. Initial service rows are folded.
The initial camera now sets the orbit-controller state rather than overriding
only its preview Camera3D transform.

The orientation control retains Godot's original circular colored axes at the
user's explicit request. The experimental cube was removed from the source
and rebuilt; node_3d_editor_viewport.cpp has no ROME diff.

Validation: Windows native editor build passed; 35 editor integration checks
passed without ERROR/SCRIPT ERROR/WARNING entries; 12 gameplay checks passed,
including actual laser contact, checkpoint contact, checkpoint respawn, falls,
and grounded passenger transport. Tests are in rome/qa/smoke.gd,
custom_tabs_checks.gd, and obby_runtime.gd. The final fresh experience is
bin/rome_studio_review (main Scenes/Place.tscn). Source checkout and 221
protected bundled GodotLuau runtime files were preserved.

For this pass packaging uses `python rome/package_runtime.py --rome-only`.
That mode updates only ROME's addon/assets, gallery and editor executable; it
does not rewrite GodotLuau code, addon components or binaries. Existing user
experiences are not regenerated; create Classic Obby to get the new template.
Full Roblox Studio parity and cloud services remain outside the implemented
features; advanced disabled tools remain visible as unavailable.

Additional references:
- https://create.roblox.com/docs/studio/overview#custom-tabs
- https://create.roblox.com/docs/scripting/locations
- https://create.roblox.com/docs/tutorials/use-case-tutorials/scripting/basic-scripting/deadly-lava

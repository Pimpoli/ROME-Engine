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


## Public alpha and document/resource workflow — 2026-10-28

Public version: 0.1.0-alpha.1, independent of the inherited Godot ABI number.
The public footer, About, CLI version and startup header use ROME's version.
Numeric Godot engine/extension API compatibility remains unchanged.

Recursos is now a hierarchical folder tree with Nueva carpeta and no Nueva
escena button. Empty folders are retained, Scenes uses the editor's translated
label, and Scripts has exact LocalScript/ServerScript/ModuleScript folders.
Existing autogenerated game scripts are copied into their typed folders while
preserving the Script object's identity, its in-memory source and the old file.
Logical node renames update document labels without breaking file references.

Native EditorSceneTabs now presents one ROME document bar containing scenes
and scripts. Scene selection restores the correct scene tree and viewport;
script selection restores its code editor and caret. Dirty code gets the native
Save/Discard/Cancel prompt. Closing an active script returns to the scene.
All 64 integrated editor checks passed without ERROR/SCRIPT ERROR/WARNING.

Standalone desktop packaging appends the runtime ZIP to the native executable.
The project hub extracts it to a versioned user cache on first launch. Empty
directories are preserved; template installation also ensures Scenes exists.
The Windows standalone executable generated a fresh Obby from its extracted
runtime and passed all 12 gameplay checks. Source code and DLL dependency
checks found no required MSYS2 runtime DLLs in the distributed Windows binaries.

The welcome hero uses the existing original AI artwork hero-art.png, with a
left-to-right readability gradient instead of the obsolete village screenshot.
The original circular viewport orientation control remains unchanged.

User explicitly authorized publishing the local GodotLuau source snapshot.
It is included under rome/vendor/GodotLuau without binaries, caches or optional
AI models. Public godot-cpp and Luau dependencies are pinned in dependencies.json;
setup_dependencies.py retrieves them. The separate Desktop/GodotLuau checkout
was not edited. The original 221 bundled runtime files retain their hashes.

GitHub source repository: https://github.com/Pimpoli/ROME-Engine
Source publication checkout: bin/rome_publish, branch main. It preserves the
repository's initial commit and publishes a full source snapshot, avoiding a
transfer of Godot's entire historical Git object store. The main workspace still
retains its original Godot-based local history; use the publication checkout to
push public main changes. Upstream provenance is in rome/UPSTREAM.md.

Release: v0.1.0-alpha.1 (pre-release). Windows is locally built and verified.
Linux workflow run 40674086143 builds the vendored source in GitHub Actions and
uploads an executable only after compilation and gameplay checks pass. Android
and macOS do not yet have verified ROME artifacts. Android requires additional
extension binaries and editor APK packaging. No ROME startup updater has been
implemented; the GodotLuau updater is separate from ROME engine updates.


## Studio parity pass — 2026-10-28 (afternoon)

Owner request: closer 1:1 with Roblox Studio in look and behavior while keeping
Godot's reusable scenes; push improvements to GitHub as PimpoliDev without
co-author lines; publish no new platform releases until the updater is polished;
ship a single double-click executable.

Native editor (C++):
- Ribbon split tools: Pieza (5 shapes), GUI, Script (Script/LocalScript/ModuleScript),
  Material, Color (palette + picker), Grupo (model/folder/ungroup), Bloquear and
  Anclar have a dropdown arrow (`rome_tool_menus` in `editor/rome_studio.cpp`,
  arrow built in `_rome_make_tool`). Selection tools grey out with no selection.
- Snap: Studio checkboxes before the move/rotate spin boxes; one shared state on
  every page, stored in project metadata `rome/snap_*`; a zero step frees an axis.
- Test group: "Probar" dropdown (play, current scene, stop), blue play, red stop,
  Godot's extra run buttons hidden; Shift+F5 stops; stopping returns to the world.
- Studio shortcuts (`rome/*` editor shortcuts): Ctrl+1..5 tools, Ctrl+L local/world,
  Ctrl+G group, Ctrl+Alt+G folder, Ctrl+U ungroup. `emulate_numpad` now defaults off
  so Ctrl+number never turns into camera views (editor settings revision 3).
- Explorer: hovered row shows a "+" that opens Insertar objeto for that parent.
- Document bar: script tabs show the object name with its class icon (Studio
  style), "(*)" when dirty, middle click closes, right click offers close /
  close others / close all. Godot's script list is hidden once per project
  (`rome/script_list_revision`), Ctrl+\ restores it.
- A place opens on its 3D world, never on an empty script screen.
- Locked parts cannot be clicked in the viewport (still selectable in Explorer).
- Inspector hides Godot metadata in ROME mode (Attributes replace it); selection
  boxes are Studio blue.

ROMEStudio addon (GDScript): new Insertar objeto window (`studio_insert_dialog.gd`,
`studio_catalog.gd`, 18 categories, recents), camera-centred placement
(`studio_placement.gd`), Properties by sections with Attributes
(`studio_properties.gd`), and handlers for `insert_part`, `group_folder`,
`ungroup`, `lock_set`, `anchor_set`, `insert_material` with Undo/Redo.

Validation: 180 integrated editor checks (`rome/qa/smoke.gd` with
`ribbon_checks.gd` and `studio_panels_checks.gd`) pass in a fresh project.
Known noise: GodotLuau's LuauLanguage misses `_get_public_*` virtuals, and Godot
4.8 beta sometimes reports "update_scripts_classes already exists"; one run
crashed right after that error while saving a scene and later runs did not.

Updater and standalone executable (`editor/rome_update_manager.*`,
`main/rome_update_helper.*`, packaging scripts):
- Two blockers fixed: Godot's JSON turns GitHub's `size` into a float (every real
  asset was rejected), and the helper did not wait for the editor because
  `OS::is_process_running` only sees child processes (now OpenProcess +
  WaitForSingleObject; Linux `kill(pid, 0)`).
- The helper stages the new executable and a backup before the editor closes,
  retries locked files for 30 s, never leaves a half-written executable, reopens
  the previous version on failure and reports through `--status-file`. One
  backup lives in `%LOCALAPPDATA%/ROMEEngine/backups`; OneDrive paths work.
- The manager closes the editor only after the helper reports `waiting`;
  "install on close" and automatic download are on by default; alpha/beta
  channel checkbox; release notes and a GitHub link in the dialog; startup check
  cached 10 min with ETag; old downloads, runtimes and backups are cleaned.
- Packaging rejects a release name whose version differs from the compiled one
  and hashes the whole runtime into the cache key. ROME experiences disable
  GodotLuau's own updater so it cannot replace the ROME-patched build.
- Fixed with the updater tests: closing the editor with an unsaved main scene
  recursed through the ROME main-tab hook and crashed (`!_is_closing_editor()`).
- Harness without publishing (local server plays GitHub, isolated APPDATA):
  `python rome/qa/updater/probar_todo.py` (~25 min, opens editor windows).

Release procedure (do not publish until the owner asks): raise `ROME_VERSION` in
`core/version.h`, build, `python rome/package_runtime.py --rome-only`, build the
standalone with `rome/package_standalone.py`, run
`python rome/qa/updater/probar_paquete.py --autonomo <exe>`, then publish a
non-draft prerelease whose asset is `ROME-Studio-<version>-windows-x86_64.exe`
with the notes in the body. The GitHub API currently lists no public release,
and 0.1.0-alpha.1 has no updater, so its users download the next one manually.

Experiences update themselves (`RomeProjectTemplate::update_project`, called in
`Main::setup` right before `register_core_extensions()`, the last moment the
experience's GodotLuau DLL can still be replaced; editor + `rome/studio/enabled`
only):
- Identity = first 12 hex of the SHA-256 of the runtime `manifest.json` (the
  standalone adds a `standalone` block, so it differs from the dev sidecar even
  with equal files; equal files are never copied). `install()` and every update
  write `.rome/runtime.json` (identity, version, managed files with hashes) plus
  `.rome/.gdignore` and a `.gitignore` that keeps only `runtime.json`.
- Managed = manifest files under `GodotLuau/` (minus `LocalScripts`,
  `ModuleScripts`, `ServerScripts`, and `icons/MenuLogo.svg` plus
  `icons/GodotLuau.ico`: the game's logo, and the window icon GodotLuau
  regenerates from it), `addons/ROMEStudio/` and `addons/GodotLuauUpdater/`.
  Scenes, assets and anything unlisted (`GodotLuau/.luau_trash`, `.luau_ids.cfg`,
  the editor's `~godot_luau…dll` copy) are never touched.
- Same identity and every managed file present: nothing is hashed. Otherwise a
  three-way decision per file against the stamp: missing or unmodified files are
  replaced. `DefaultScripts` and `DedicatedServer` keep a version the creator
  changed; without a stamp any different one counts as changed. Other files edited
  in the experience are replaced, since ROME owns them. Files ROME no longer ships
  are retired only if unmodified.
- Any target that cannot be opened for writing aborts before changing anything.
  Replaced and retired files go to `.rome/backup-<previous identity|anterior>`;
  only the newest backup is kept. A failure midway (or a copied file whose hash
  does not match, which also drops `runtime.complete` so the runtime is
  re-extracted) restores what had changed and leaves the old stamp, so the next
  open retries. The message is printed at startup and repeated by
  `ROMEUpdateManager` as a toast and in Output.
- `get_root()` now reads the appended runtime with minizip directly (the ZIP
  module is not registered that early), extracts sequentially, reports why an
  extraction failed and does not retry it in the same process.
- `package_runtime.py` mirrors `addons/ROMEStudio` (removes it before copying)
  so removed files also leave experiences.
- Tests: `python rome/qa/updater/probar_experiencias.py` (legacy experience
  opened by a never-run standalone, second open, stamped update with
  creator-edited scripts, locked file, failure midway with rollback, non-ROME
  project).

Next candidates: Studio camera mouse mapping (middle pan), a Luau command bar
(needs a GodotLuau API to run code in the editor), BindableFunction support in
GodotLuau, and porting the shared scene/script tab bar to GodotLuau itself.

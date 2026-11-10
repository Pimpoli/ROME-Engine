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
4.8 beta reports "update_scripts_classes already exists" when a nested main loop
re-enters script class registration (the progress dialog guards it). The crash
that sometimes followed that error while saving a scene was the SceneTree timer
double free fixed in alpha.4 (updater notes below).

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
`core/version.h` and the version in README.md, CONTRIBUTING.md, rome/UPSTREAM.md
and the `release_tag` default of `.github/workflows/rome-release.yml`, build,
`python rome/package_runtime.py --rome-only`, build the standalone with
`rome/package_standalone.py`, run
`python rome/qa/updater/probar_paquete.py --autonomo <exe>`, then publish a
non-draft prerelease whose asset is `ROME-Studio-<version>-windows-x86_64.exe`
with the notes in the body. The updater shows the body as plain text, so the notes
avoid Markdown markup. GitHub's API gives each asset a `digest` (`sha256:…`), which
the updater requires. 0.1.0-alpha.2 is the first release with the updater;
0.1.0-alpha.1 stayed a draft without one, so anyone holding it downloads
alpha.2 manually once. Linux assets come from the manual `rome-release.yml`
workflow (release tag input) once the release exists. After publishing,
`python rome/qa/updater/probar_release.py` checks the real thing: a copy of the
build that claims the previous version (same-length patch) finds the release
on GitHub, downloads and verifies it, installs it on close and, reopened, says
it is current. `--desde <exe>` starts from the previous published executable
instead of the patched copy; the editor stays open `--espera-cierre` seconds (60)
before closing, because alpha.3 and older crash when closed right after startup
(below); from alpha.4 on, `--espera-cierre 0` is the harder test.

Released: v0.1.0-alpha.2 on 2026-10-28, prerelease from commit e682de2, Windows
asset `ROME-Studio-0.1.0-alpha.2-windows-x86_64.exe` (186,879,068 bytes, SHA-256
face6fb5f4f70229e7b9b5667c5bb4f76448d1dba8a8989c8defb41aec4740e4) plus its
`.sha256`. No Linux asset yet; the alpha.1 draft was left untouched.
`probar_release.py` passed 13/13 against it: real API, size and digest check,
clean close, install on close, reopened as alpha.2 and "current".

Released: v0.1.0-alpha.3 on 2026-10-28, prerelease from commit c4f6158, Windows
asset `ROME-Studio-0.1.0-alpha.3-windows-x86_64.exe` (186,884,188 bytes, SHA-256
08913bf4fe2dae682b6f0672c965e2cacf933ab26ca6f05d6262d943a5559e73) plus its
`.sha256`; the first release whose updater resumes downloads. Checked from the
published alpha.2 itself (`probar_release.py --desde
bin/releases/0.1.0-alpha.2/ROME-Studio-0.1.0-alpha.2-windows-x86_64.exe`):
13/13 with the editor closed 60 s after start.

Found by that check:
- From this machine GitHub served the asset at ~160 KB/s sustained (16 KB/s at
  worst) while other hosts gave 29 MB/s, and cut the transfer after ~15 min
  (curl error 56). Range requests work (206). Fixed after alpha.2 (ships in the
  next release; alpha.2 itself downloads the next one the old way): the download
  keeps what arrived in `<asset>.partial` (HTTPRequest `keep_partial_download`),
  records the asset's SHA-256 in `<asset>.partial.json`, and asks only for the
  rest with `Range` + `append_to_download_file` (a 200 answer rewrites the file).
  Cuts and one-minute stalls retry by themselves after 5, 10, 20, 40, 60 s; only
  attempts that bring no data count, and after 8 the user can resume by hand.
  Closing ROME mid-download continues next time; "Cancelar" pauses and keeps
  the data. Test: `probar_editor.py reanudar` (local server cuts at 40 MB, editor
  killed, reopened: requests start at 40 MB and then 80 MB, verified, installed).
- Fixed in alpha.4 (alpha.2 and alpha.3 keep it):
  closing the editor within the first second after it opened ended in
  STATUS_HEAP_CORRUPTION (0xC0000374 at ntdll+0x118095, no Godot backtrace)
  before install on close ran, 4 of 4 with the published alpha.2 and clean when
  closed 60 s after start. Cause, in Godot's `SceneTree::process_timers`: a
  timeout callback that runs a nested main loop iteration (any EditorProgress:
  saving a scene, `ResourceSaver.save` registering script classes, mesh
  previews) walked the timer list again, fired and erased the timer the outer
  loop was holding, and the outer loop erased it a second time. As the only
  timer the list was already gone and nothing happened; with another pending
  (ROMEStudio waits 0.8 s in `studio.gd` before framing a fresh experience's
  camera) the element was freed twice. ROMEUpdaterQA resumes from a timer and
  calls `save_all_scenes()`; the smoke test's occasional crash after
  `ResourceSaver.save` was the same. Found with `rome/qa/tools/depurador.cpp`
  (`compilar_depurador.bat` builds `bin/depurador.exe`; there is no cdb or
  WinDbg here): it runs a program under the Win32 debug API and writes every
  thread's stack on a crash plus OutputDebugString, and under its debug heap the
  double erase printed `Condition "p_element->data != this"` in
  `List<Ref<SceneTreeTimer>>::_Data::erase` instead of crashing.
  `process_timers` and `process_tweens` (same pattern with tween callbacks) now
  ignore nested calls while they walk their list. Checks: smoke.gd fires a
  0.05 s timer with another pending and runs `make_mesh_previews` from its
  callback; it must fire once (182 checks; the build without the fix crashed at
  `ResourceSaver.save` before getting there). `probar_release.py --espera-cierre
  0` from the fixed build: 13/13, no list errors in the logs.
- ROMEUpdaterQA sent the close request from inside its own coroutine: with
  nothing unsaved the editor exits synchronously and `unload_editor_addons()`
  frees the plugin mid-function (`Nonexistent function '_cerrar_sin_preguntas'
  in base 'previously freed'` in every al_cerrar log). It now sends the close and
  its "discard" answer deferred. The crash above stayed, so that was not it.

Released: v0.1.0-alpha.4 on 2026-10-29, prerelease from commit 3fa7c91, Windows
asset `ROME-Studio-0.1.0-alpha.4-windows-x86_64.exe` (186,884,189 bytes, SHA-256
1682c59d7dc4ab35f418a7a9f189e026ce3bf6d804282bb714b1f5d7b13e5e54) plus its
`.sha256`; the first release without the early-close crash. Checked from the
published alpha.3 itself (`probar_release.py --desde
bin/releases/0.1.0-alpha.3/ROME-Studio-0.1.0-alpha.3-windows-x86_64.exe`):
13/13 with the editor closed 60 s after start. The whole run, download from
GitHub included, took under 3 minutes: the 160 KB/s of 2026-10-28 was not
permanent.

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

Next candidates: a Luau command bar
(needs a GodotLuau API to run code in the editor), BindableFunction support in
GodotLuau, and porting the shared scene/script tab bar to GodotLuau itself.


## Properties and Studio camera controls — 2026-11-02

Implemented on the current 0.1.0-alpha.4 development build; no new release or
version bump was made in this pass.

- `studio_properties.gd`: selected object's public class, icon and name in a
  compact header. Vector2/Vector3 properties expand into editable axes;
  GodotLuau's Vector4/UDim2 expands into X/Y and Scale/Offset. Full-value and
  component editing both use the existing EditorProperty change/Undo path.
  Orientation axes display degrees and convert back to native radians. Editing
  one component preserves the others. Fold state survives Inspector rebuilds
  and switching objects. Filtering reveals matching component descendants
  without overwriting saved fold state. Invalid/non-finite numeric input is
  rejected. The header follows object renames.
- `editor/rome_studio.cpp` and `InspectorDock`: Ctrl+Shift+P focuses/selects the
  property search from the viewport or from focused Luau code, opening the dock
  when needed. It runs before the text-editor shortcut guard.
- `node_3d_editor_viewport.cpp`: ROME projects default to MMB pan without Shift;
  RMB look/WASD and F focus retain the native implementation. Alt+MMB orbits;
  Alt+Shift+MMB adds angle snapping, keeping plain orbit unsnapped. Original
  circular orientation control remains. The profile can be disabled via
  `editors/3d/navigation/rome_studio_controls`. Other projects and the original
  navigation shortcuts/preferences are preserved.
- No changes to the original GodotLuau checkout, its vendored sources or DLLs.

Validation: native Windows build succeeded; `git diff --check` passed. Updated
editor integration suite passed 199 checks in `bin/rome_parity_checks_01`, with
isolated APPDATA/LOCALAPPDATA in `bin/rome_parity_settings`. Includes real
component edits, Undo/Redo, UDim2 axes, degree conversion, fold persistence,
filter focus with CodeEdit focused, MMB camera translation without rotation,
and native orbit restored when the profile is disabled. The fixture still
occasionally logs the pre-existing duplicate ProgressDialog task errors during
script resource registration; these are separate from the 199 assertions.
GUI review in `bin/rome_parity_qa_20261102` showed the new header/coordinate
editors and a clean startup console. Edited X from 0 to 3 with Y/Z retained and
restored it with Ctrl+Z. Also reviewed at the default smaller window size.
`python rome/package_runtime.py --rome-only` refreshed `bin/ROME Studio.exe` and
the bundled ROME addon. Existing experiences receive it through the existing
runtime update flow. No public release was uploaded.

Official behavior references:
- https://create.roblox.com/docs/studio/properties
- https://create.roblox.com/docs/studio/overview


## Toolbar appearance and tab overflow — 2026-11-02

Continues the alpha.4 development build; no public release/version bump.

- New native `editor/rome_toolbar_appearance.cpp`: toolbar options menu next to
  the tab strip, also available from empty ribbon space or a standard tab's
  context click. Compact icons/buttons, label visibility and toolbar collapse
  are independent preferences in `rome/studio/toolbar_compact`,
  `toolbar_labels`, `toolbar_collapsed`. Original defaults are retained.
- Collapsing keeps the mezzanine/testing controls and tabs visible. Hovering
  a tab temporarily reveals its page; it folds again after the pointer leaves.
  Ribbon popups, color pickers and custom-tab dialogs keep it open while in use.
- Shared styling applies to standard and regenerated custom pages, including
  Add Tools and separators. Dropdown arrows and tooltips remain available when
  labels are hidden. No extra raw-pointer registry for captions/icons.
- `ROMEToolbarTabs` wraps the existing row in a horizontal ScrollContainer.
  Large-screen gaps collapse on smaller windows. Many custom tabs no longer
  enlarge the window; the active tab is scrolled into view after the scrollbar
  range is updated, not before its maximum/page is ready. The helper only
  inspects live row buttons, avoiding queued old ButtonGroup members.
- No edits to GodotLuau source/runtime components. Packaging remains ROME-only.

Validation: Windows native build passed; full editor fixture passed 216 checks
with no SCRIPT ERROR entries. Checks cover options, persistence, custom-page
rebuilds, hover reveal, cleanup, five extra tabs without window growth and
scrolling to the active tab. The existing duplicate ProgressDialog task errors
from script registration still occasionally appear in the full fixture.
Manual review at 1152x680 verified compact mode, icon-only tools, dropdown
arrows and folding through the real menu; restored the original preferences.
Official reference: https://create.roblox.com/docs/studio/overview#appearance

Paths: `bin/rome_toolbar_build.log`, `bin/rome_toolbar_tests.log`,
`bin/rome_parity_checks_01/integration-result.json`. The original source history
and unrelated working changes remain intact; no release assets were published.


## Explorer search and organization — 2026-11-02

Continues the alpha.4 development build; no new release/version bump.

- Native Explorer supports `is:PublicClass` plus existing name terms (AND),
  case-insensitive, through GodotLuau's readonly generated public inheritance
  table. Metadata `__rbx_class` wins over physical implementation: WedgePart
  matches BasePart, not Part; Accessory matches Accoutrement, not Model.
  Empty/unknown class queries do not accidentally match every object.
  Native type:/t: and group:/g: remain available; OR, property comparisons,
  tag and ancestry query syntax are not implemented in this pass.
- Filtered results retain dimmed, non-selectable ancestors and temporarily open
  their branches. Clearing the filter restores fold state; no Node.display_folded
  writes while filtering. ROME omits the native flatten-parent checkbox, while
  non-ROME projects and node-selection dialogs keep their previous behavior.
- Ctrl+Shift+X opens/focuses Explorer search even from focused Luau code; Ctrl+I
  opens the real Insert Object dialog in the viewport/Explorer. In ROME only,
  native Run Editor Script uses Ctrl+Alt+Shift+X to avoid stealing Explorer search.
  Its menus remain available and non-ROME run shortcuts are unchanged.
- Ctrl+A in the object tree selects only visible/selectable query matches, or
  all instances in an unfiltered tree, including folded branches. Context ancestors
  and hidden DataModel root are excluded; text fields keep native text selection.
  Ctrl+A no longer activates Godot's old Add Child shortcut/button in ROME.
- Explorer context menu offers Model/Folder grouping and Ungroup via the existing
  addon Undo actions. A Hierarchy submenu selects direct editable children or
  unique parents; visible services can be navigated but not grouped/moved.
  Group guards revalidate ownership, inherited nodes, root and fixed services
  at dispatch as well as disabling invalid menu entries. Native advanced items
  remain available; the whole menu has not yet been replaced with Studio parity.
- No changes to GodotLuau source, DLLs or its generated inheritance table.

Validation: native Windows build and git diff --check passed. Full disposable
editor suite passed 244 checks with no SCRIPT ERROR entries, including public
class distinctions, fold restoration, result-only selection, shortcuts from
code, real menu dispatch, world-transform preservation, grouping Undo, and
fixed-service safeguards. Existing duplicate ProgressDialog task errors during
script registration remain in the fixture logs. Manual review at 1152x680
confirmed `Moving is:BasePart` returns the three moving platforms with context
parents dimmed and the organization entries present in the context menu.

Current local build: `bin/ROME Studio Preview.exe` (main launcher was open).
Logs: `bin/rome_explorer_build.log`, `bin/rome_explorer_tests.log`;
fixture result: `bin/rome_parity_checks_01/integration-result.json`.
No release assets or new version were published.
Reference: https://create.roblox.com/docs/studio/explorer

## Continuous transforms, resource layout and script activation — 2026-11-02

Continues the alpha.4 development build; no release/version bump.

- Normal held-left-button 3D transforms now accumulate virtual mouse motion
  through the native cursor-warp helper, within the viewport with an inset of
  3 scaled pixels. Relocation events do not introduce transform jumps. Ending
  or cancelling a wrapped drag restores the pointer to its starting position
  while the editor is focused. The option
  `editors/3d/navigation/continuous_transform_grab` defaults to true in ROME.
  Instant transforms retain their existing handling; custom gizmo handles
  and vertex snapping keep their native input behavior.
- Resources defaults to LEFT_BL beneath Properties and is visible initially.
  Workspace revision 6 migrates prior default bottom/right-bottom/closed
  placements, retaining customized other slots and floating windows. Output
  stays below the world. Reduced list minimum height and a one-line counter
  leave Properties usable at 1152x680; help is available in the counter tooltip.
- Resource icons distinguish scenes, models, images, audio and typed script
  folders. LocalScript is blue, ServerScript orange, ModuleScript purple.
  The existing folder creation, search, category filter and insertion remain.
- Native SceneTreeEditor handles script double-click before editable Tree
  cells start renaming. It selects the clicked selectable row, defers the
  existing open_script signal, and opens its exact Luau resource in the shared
  document bar. Inline row buttons retain their own actions.
- No changes to GodotLuau source or binaries. ROME-only packaging preserves
  its bundled payload; the 221-file baseline retains only the previously
  recorded standalone-updater customization.

Validation: native Windows build and git diff --check passed. The full
disposable editor suite passed 270 checks with no SCRIPT ERROR entries.
New GUI checks cover all three script types, selection of the clicked row,
absence of a rename popup, colored folders, visible dock migration, both
viewport edges, synthetic relocation motion and one-step Undo. Existing
duplicate ProgressDialog task errors during script registration still occur
in the fixture. Manual review at 1152x680 confirmed the compact left dock,
GameManager double-click opening code, and Place returning to the world.

Current local build: `bin/ROME Studio Preview.exe` (the original launcher
remains open). Build log: `bin/rome_comfort_build.log`; suite log:
`bin/rome_explorer_tests.log`; result:
`bin/rome_parity_checks_01/integration-result.json`. No release was published.

## 2D and mixed-game foundation — 2026-11-02

Continues alpha.4 locally; no public release/version bump.

- Hub and project-template installer accept `2d` and `mixed`, with inline SVG
  preview cards. Pure 2D starts with the existing Game2D/Workspace2D extension
  root, GL Compatibility, a 1280x720 canvas and a dark background. Mixed starts
  with one Game3D and ordinary `Workspace/World2D` content; it never starts a
  second Game2D/Workspace2D player system.
- New `studio_2d.gd` builds three native StaticBody2D platforms, collision and
  colored visuals, an origin marker and a guide. Native editor grouping lets
  a platform's body, artwork and collision move together. Ownership is assigned
  to authored 2D content so it survives packing; 3D private render meshes stay
  under the extension's existing lifecycle.
- Pure 2D keeps factory LocalScript/ServerScript source available but disables
  its automatic startup in the new experience: current PlayerModule/ControlModule
  factory code assumes 3D and missing modules. Character2D's existing native
  movement, gravity, floor collision and jump work. Passive 2D respawn, networking
  and complete Luau API parity remain pending; no extension source/DLL changes.
- Native 2D/3D view buttons, per-scene view preference for the editor session,
  correct return from scripts/playback, and dimension-aware reusable new scenes.
  Main Game2D opens the actual 2D editor after layout restoration. Toolbar Move,
  Rotate and Scale dispatch to CanvasItemEditor when it is active. Native 2D
  Inspector, collision, sprite/animation, tilemap and polygon editors remain.
- Resources has a small + menu for reusable 2D/3D scenes. Object insertion adds
  a Mundo 2D category and composition nodes. Shared routing handles both objects
  and PackedScene instances, including an automatic ordinary World2D wrapper
  with one Undo action. Collision/path attachments require compatible ancestors
  or roots; invalid parents are rejected before committing anything.
- SubViewportContainer/SubViewport are available for explicit composition.
  Inserted 2D content uses the destination viewport's canvas and dimensions;
  transformed Node2D parents are respected. A selected SubViewport retains 3D
  scene instances instead of rerouting them to the main Workspace. The default
  mixed template shares the viewport, with independent 2D/3D transforms/physics.
- Fixed-service repair and native Explorer service/root protections now cover
  Game2D as well. Empty reusable 2D scenes do not acquire game services. Saving
  a new template leaves the deferred message flush before its native save dialog.

Validation: final Windows native build and git diff --check passed. Full editor
suite: 337 checks, no SCRIPT ERROR entries. Dedicated new-template/editor checks:
40 pure 2D and 37 mixed; runtime checks: 13 pure 2D and 5 mixed. Actual D/Space
events exercise native Character2D motion, jump, landing and camera selection.
GUI checks verify the visible startup workspace, reusable scene serialization,
script round trips, attachment guards, composition routing and Undo/Redo.
Manual review at 1152x680 verified 2D editing, ribbon Move dragging a complete
platform, and Ctrl+Z restoring it. Existing ProgressDialog registration/save
warnings, Compatibility AA warnings and GL preview-texture cleanup errors can
still appear in the disposable fixture logs; they are not SCRIPT ERROR entries.

Artifacts: `bin/rome_twod_build_final.log`, `bin/rome_explorer_tests.log`,
`bin/rome_parity_checks_01/integration-result.json`, and the marked disposable
`bin/rome_twod_qa_20261102_2d` / `_mixed` fixture result JSON/logs. The 221-file
GodotLuau bundle baseline retains only the previously recorded standalone
updater customization. `bin/ROME Studio.exe` now matches the new native build;
the previous launcher had been closed, so packaging could update it directly.

## Explorer context, shared properties and resource thumbnails — 2026-11-02

Continues the local alpha.4 development build; no new release/version bump.

- Explorer now opens a compact `ROMEExplorerContext`: script opening when
  relevant, insertion, cut/copy/paste, duplicate/delete/rename, grouping and
  hierarchy. The original native menu, including plugin/resource actions,
  remains as its `ROMEExplorerAdvanced` submenu. Both dispatch through the
  existing native handlers. Fixed services cannot be cut, copied, duplicated,
  renamed, changed to another class or moved through those handlers. Deletion
  keeps the existing service-reset flow.
- In the Explorer, Ctrl+V targets Workspace (the reusable root if there is no
  Workspace); Ctrl+Shift+V targets selected parents. Native clipboard copying,
  resource remapping and ownership are retained. Multiple destination parents
  share one paste Undo action; every pasted object stays selected. Advanced
  sibling paste remains available with its conflicting shortcut removed.
  This does not implement Studio's camera placement/original-CFrame paste
  options or replace the native 2D canvas clipboard workflow.
- MultiNodeEdit exposes readonly node-count/path/class queries. StudioInspector
  resolves its exact target paths instead of inferring them from live selection.
  Compatible extension objects use the Studio common-property grid; native 2D
  objects retain their complete Inspector. The header shows the object count,
  mixed values use —, booleans use an indeterminate checkbox, and enum dropdowns
  include a no-op mixed entry. All edits go through the native proxy/Undo path.
- Individual vector/UDim2 component edits use native fieldwise assignment, so
  each node retains its own other axes/scales/offsets. Whole-value edits still
  explicitly replace the value on all targets. Multi-UDim2 pair rows are read-only
  while individual components remain editable. Adding/removing attributes is
  kept to single selection in this pass. A stale panel cannot commit after
  switching away from its source scene.
- Resources keeps its default folder tree and adds an optional narrow thumbnail
  grid. View/folder preferences are per project. Folder/up navigation, search,
  opening, dragging and scene insertion share the tree's actual resource paths.
  Native EditorResourcePreview supplies real previews, bounded to 32 eligible
  files per refreshed view, with type-icon fallback. Generation and path/index
  guards reject callbacks from old folders or closed views. No file moves or
  language-extension edits are performed.

Validation: final native Windows build and git diff --check passed. The full
GUI editor fixture passed 383 checks with no SCRIPT ERROR entries. New checks
cover common/mixed properties, per-object vector preservation, degree and UDim2
edits, Undo/Redo, native 2D Inspector fallback, compact/advanced menus, actual
Ctrl+V/Ctrl+Shift+V routing, multi-parent paste selection and Undo, service
guards, generated texture previews, stale callbacks, persisted navigation,
cross-folder search and insertion from a thumbnail tile. The test runner also
starts on its intended main scene and closes its own saved 2D document before
quitting, so layout restoration cannot switch the fixture mid-check.

Build log: `bin/rome_studio_workflow_build.log`; suite log/result:
`bin/rome_explorer_tests.log` and
`bin/rome_parity_checks_01/integration-result.json`. The primary
`bin/ROME Studio.exe` matches the new build. The GodotLuau 221-file baseline
retains only its previously recorded standalone-updater customization.

Manual review at 1152x680 confirmed folder cards and navigation in the narrow
Resources dock, three filtered Explorer objects sharing the Studio property
header and mixed Name value, and the complete compact context menu with its
Advanced submenu. The disposable review editor remains open for inspection.

References: https://create.roblox.com/docs/studio/explorer and
https://create.roblox.com/docs/studio/properties

## Studio design chrome — 2026-11-02

Continues the local alpha.4 development build, retaining the native circular
orientation gizmo and the existing 2D/mixed workspaces.

- Corrected flat ribbon buttons that skipped their state backgrounds. Standard
  and custom tabs, selected transform tools, split-button arrows and toolbar
  options now share transparent normal, charcoal hover and visible gray active
  styles. Mirrored button styles retain the same palette.
- Long command pages expose previous/next chevrons that scroll the real viewport
  by 70%, disable at either end and disappear when the complete page fits. Tab
  changes return to the first command. The whole tools row follows collapsed
  and temporary hover-reveal behavior; overflow does not widen the editor.
  Wide windows center the tab strip independently of the run controls. Dynamic
  stretch weights keep all three strip/spacer minima at zero, so adding many
  custom tabs cannot lock the editor to its previous wide window size.
- Side and bottom dock headers expose Float and Close alongside the original
  context menu. The buttons occupy native header-container width, preserving tab
  hit areas and drag/reorder. Activation resolves the current dock and checks
  native closability, supported layouts and multiwindow availability. Closing
  hides the dock; closing a floating window restores its saved slot. Main-screen
  workspace containers do not acquire these panel actions.
- ROME Dark uses neutral panel/header and Output styles, including the native
  Odd/BottomPanel variations; the shared document strip retains its active
  accent. Floating panel titles use ROME branding. Property tables reserve more
  width for names, clip long values and omit hierarchy guide lines while keeping
  expandable vector/UDim2 fields and native property editing. Hovering a clipped
  value reveals its full value, including readonly and component rows.

Reference: https://create.roblox.com/docs/studio/overview

Validation: final Windows native build passed (`bin/rome_design_build_final.log`).
The full disposable editor suite passed 409 checks with a fresh result and no
native or script ERROR entries (`bin/rome_explorer_tests.log` and
`bin/rome_parity_checks_01/integration-result.json`). New checks exercise actual
1152/1920 window layout, centered tabs independently of run controls, eight
additional custom tabs, wide-to-narrow resizing, overflow-end access and
collapsed toolbar behavior. Dock checks cover native capability guards,
hide/reopen and a real floating-window return to its original slot.

Manual review at 1152x680 and maximized 1920-wide verified the selected tab/tool
backgrounds, full Resources access through the chevrons, centered tab strip,
neutral dock/Output chrome, header quick actions and improved property labels.
The final preview editor remains open on the disposable review project. Both
public local executables were refreshed from the final native build. The
221-file GodotLuau baseline still differs only in the previously recorded
standalone-updater customization; no extension source or binaries changed.

## 2026-11-03 — resource organization, camera movement, templates and self-hosting

Local alpha.4 development build; no new version tag or public Release in this pass.

- Perspective wheel movement now dollies continuously with optional smoothing,
  leaving orbit radius/FOV/clipping unchanged. Native/orthographic behavior and
  the circular orientation gizmo remain available.
- Resources supports F2 rename, destination-folder moves and folder drop targets
  through FileSystemDock's dependency-preserving move pipeline. The native scan/
  import readiness guard prevents concurrent resource mutations.
- Unified document tabs reserve a close hit area and show X on active/hovered
  tabs without moving the text. Hub cards offer sorting by edit time, natural
  name and asynchronously calculated disk size, plus list-only/trash/both removal.
  The confirmation shows the complete folder path and defaults to list-only.
- Pure/mixed 2D templates now have authored geometric platforms, collectibles,
  checkpoint, fall recovery, a moving bridge and finish HUD. Mixed F2 switches
  between its separate 2D viewport and the original 3D world. These are local
  GDScript mechanics, not new network replication in GodotLuau.
- Added GodotLuau's portable owner-server manager and editor profiles to the
  local checkout, vendored sources and runtime bundle. Backups deduplicate by
  project content, run every five minutes/on Play, and keep credentials outside
  the archive. Publishing imports a staged project with editor plugins disabled
  and launches the real --glserver process; Server Real uses --glconnect.
  Preparar y probar publishes current changes before opening the client.
- The owner service supports a PC or VM, authenticated APIs and HTTPS remote
  administration. Windows has Iniciar servidor ROME.cmd beside the local editor.
  Python 3.10+ is required. No public server, firewall changes or external upload
  were performed; all network QA used disposable localhost projects.
- Exposed ROME's scene-save pipeline without Stop so periodic backups do not
  interrupt a running game. Stock Godot defers unsaved-scene backups during play.
  Ported the existing Character2D gravity and TextDirection compatibility fixes
  into the local extension, and implemented its empty public-symbol queries.

Validation:
- Native Windows build passed (bin/rome_hub_final_build.log).
- GodotLuau debug and release DLL builds passed through tools/compilar.py.
- Full editor integration: 434 checks passed with a fresh result and no ERROR
  entries (bin/rome_explorer_tests.log). Includes real dependency rewrites after
  resource/folder relocation and camera wheel input, not only property checks.
- 2D template/runtime: 40 + 17 checks; mixed: 37 + 12 checks. All assertions pass.
- Self-host service: 5 Python unit tests pass. Actual editor upload/import/start
  and separate remote client connection passed. Profile persistence metadata,
  private-file exclusion and backup-without-stopping-Play also passed.
- Manual Windows review verified Hub labels/card menus/removal path dialog and
  the real GodotLuau server panel in a narrow editor window; no project deleted.
- Packaged public EXE SHA-256 matches the final native build. Both packaged DLLs
  match the local GodotLuau build; latest edited sources are mirrored to vendor.

Known issues / limits: mixed editor QA reports two GLES3 texture allocations
(349524 bytes each) still live at shutdown; its gameplay assertions pass. The
headless remote client reports one ObjectDB instance at shutdown. Compatibility
renderer also reports the extension's screen-space-AA warning. These are not
claimed resolved. Server tests exercised Windows/localhost; Linux/VM deployment
requires the matching Linux extension and remains unverified. Servers do not
restart their games automatically after the management service restarts. Snapshot
retention is manual. The public alpha download has not been updated by this pass.

## 2026-11-07 — Studio workspace and toolbar

- New ROME experiences open with Explorer above Properties on the right and
  Output below the world. Existing saved layouts still load through the
  normal layout path. Apply the preset through the top-right Paneles menu
  or Ventana > Editor Layout > Estilo Studio. Resources can be opened on
  demand from Paneles and returns to its saved slot.
- The header separates testing, ribbon tabs and workspace controls. Modelo
  follows Inicio, the active tab has a blue accent, and the toolbar menu
  saves the optional left alignment. Panel and toolbar menus stay inside
  the editor edge. The command row measures labels and uses its overflow
  arrows without a second horizontal scrollbar.
- Native Windows build and all 448 editor integration checks passed.
  Reviewed the new default, layout persistence and both header menus in
  Classic Obby. The local ROME Studio executable matches the tested build.
  The 233 GodotLuau runtime files matched their pre-packaging hashes.
  Logs and the disposable fixtures are in `bin/rome_studio_layout_qa`.

Reference: https://create.roblox.com/docs/studio/overview

## Microsoft Store package — 2026-11-09

Windows SmartScreen warns on the unsigned GitHub exe. The free way out is the
Store, which signs MSIX packages itself. `rome/package_msix.py` puts the editor
next to `rome_runtime/` (the hub reads it in place), makes the logos and packs
with makeappx. Pass `--name`, `--publisher` and `--publisher-name` from
Partner Center for a real upload; the defaults are only for local tests.
`AppxManifest.xml` next to the editor turns the updater off (`from_store()`).

Validated from a registered layout (Developer Mode, `Add-AppxPackage -Register`):
opens from its Start menu entry, GodotLuau loads, Place.tscn builds and Play stays
alive. Not validated: the read-only WindowsApps install, which only a signed or
Store package reaches. Hiding the two remaining update checkboxes compiled but was
not linked into a test package yet.
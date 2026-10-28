"""Bundle an existing GodotLuau checkout beside the ROME editor.

Usage: python rome/package_runtime.py --source C:/path/to/GodotLuau
The source checkout is read only. Re-run after rebuilding/updating GodotLuau.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
ROME_GUARD = '\tif ProjectSettings.get_setting("rome/studio/enabled", false):\n'


def keep_luau_with_rome(project: Path):
    """The bundled GodotLuau is built for this engine and arrives with ROME updates.

    Its own updater would replace it with an upstream build inside the user's
    experience, so in ROME experiences it only shows the bundled version.
    """
    updater = project / "addons/GodotLuauUpdater/plugin_actualizador.gd"
    text = updater.read_text(encoding="utf-8")
    if ROME_GUARD in text:
        return
    notice = '\t\t_set_ver_status(_get_local_version() + " · incluido en ROME", "", _col_dim)\n'
    entries = {
        "func _check_for_update() -> void:\n": ROME_GUARD + notice + "\t\treturn\n",
        "func _start_download() -> void:\n": ROME_GUARD + "\t\t_downloading = false\n" + notice + "\t\treturn\n",
    }
    for entry, guard in entries.items():
        if text.count(entry) != 1:
            raise SystemExit(f"GodotLuauUpdater changed ({entry.strip()}): update keep_luau_with_rome in rome/package_runtime.py")
        text = text.replace(entry, entry + guard)
    updater.write_text(text, encoding="utf-8", newline="\n")


def refresh_rome(output: Path):
    """Update only ROME assets/editor code, leaving all GodotLuau components intact."""
    project = output / "project"
    if not (project / "GodotLuau/godot_luau.gdextension").is_file():
        raise SystemExit("An existing runtime bundle is required for --rome-only.")
    keep_luau_with_rome(project)
    addon = project / "addons/ROMEStudio"
    # Mirrored, so a file removed from ROME also leaves the experiences it updates.
    shutil.rmtree(addon, ignore_errors=True)
    shutil.copytree(ROOT / "rome/addons/ROMEStudio", addon, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("*.import", "*.uid", "__pycache__"))
    (addon / "assets").mkdir(exist_ok=True)
    for asset in (ROOT / "rome/assets/templates").iterdir():
        if asset.suffix in (".png", ".svg"):
            shutil.copy2(asset, addon / "assets" / asset.name)
    hub = output / "hub"
    hub.mkdir(parents=True, exist_ok=True)
    for asset in (ROOT / "rome/assets/hub").glob("*.png"):
        shutil.copy2(asset, hub / asset.name)
    if (hub / "hero-art.png").is_file():
        shutil.copy2(hub / "hero-art.png", hub / "hero.png")
    (output / "licenses").mkdir(exist_ok=True)
    for name in ("LICENSE", "LICENSE.txt", "COPYRIGHT.txt", "AUTHORS.md"):
        if (ROOT / name).is_file():
            shutil.copy2(ROOT / name, output / "licenses" / name)
    manifest_path = output / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.is_file() else {}
    manifest["runtime_layout_revision"] = 2
    manifest["files"] = {
        str(path.relative_to(project)).replace("\\", "/"): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(project.rglob("*")) if path.is_file() and ".godot" not in path.parts
    }
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    editor = ROOT / "bin/godot.windows.editor.x86_64.exe"
    public_editor = output.parent / "ROME Studio.exe"
    try:
        shutil.copy2(editor, public_editor)
    except PermissionError:
        public_editor = output.parent / "ROME Studio Preview.exe"
        shutil.copy2(editor, public_editor)
    print(f"ROME-only payload updated; GodotLuau preserved. Editor: {public_editor}")


def package(source: Path, output: Path, binary_source: Path | None = None):
    source = source.resolve()
    payload = source / "GodotLuau"
    selected_binaries = binary_source or payload / "bin"
    if not (selected_binaries / "godot_luau.windows.template_debug.x86_64.dll").is_file():
        raise SystemExit("GodotLuau debug DLL is missing. Build the source checkout first.")
    project = output / "project"
    project.mkdir(parents=True, exist_ok=True)
    ignore = shutil.ignore_patterns("*.import", "*.o", "*.obj", "*.a", "*.lib", "*.exp", "*.pdb", "__pycache__")
    for name in ("bin", "icons", "assets", "DefaultScripts", "shaders", "licenses", "DedicatedServer"):
        if (payload / name).is_dir():
            shutil.copytree(payload / name, project / "GodotLuau" / name, dirs_exist_ok=True, ignore=ignore)
    shutil.copy2(payload / "godot_luau.gdextension", project / "GodotLuau/godot_luau.gdextension")
    if binary_source:
        for binary in binary_source.glob("*.dll"):
            shutil.copy2(binary, project / "GodotLuau/bin" / binary.name)
    shutil.copy2(source / "Version", project / "GodotLuau/Version")
    shutil.copy2(source / "LICENSE", project / "GodotLuau/LICENSE")
    shutil.copytree(source / "addons/GodotLuauUpdater", project / "addons/GodotLuauUpdater", dirs_exist_ok=True, ignore=ignore)
    # Adapt the bundled editor presentation without modifying the user's checkout.
    settings_plugin = project / "addons/GodotLuauUpdater/plugin.gd"
    text = settings_plugin.read_text(encoding="utf-8")
    text = text.replace('"GodotLuau Config"', '"Luau"')
    text = text.replace('\t\tmake_bottom_panel_item_visible(scroll)', '\t\t# ROME keeps the viewport visible on startup.')
    text = text.replace('lbl.text = "GodotLuau Config"', 'lbl.text = "ROME Luau"')
    settings_plugin.write_text(text, encoding="utf-8", newline="\n")
    editor_plugin = project / "addons/GodotLuauUpdater/plugin_editor.gd"
    text = editor_plugin.read_text(encoding="utf-8")
    text = text.replace('lbl.text = "Players"', 'lbl.text = "Jugadores"')
    text = text.replace('_mp_device.add_item("PC")', '_mp_device.add_item("Dispositivo actual")')
    text = text.replace('\t_mp_bar.add_child(_mp_device)', '\tvar device_slot := EditorInterface.get_base_control().find_child("ROMEDeviceSlot", true, false)\n\tif device_slot != null:\n\t\tdevice_slot.add_child(_mp_device)\n\telse:\n\t\t_mp_bar.add_child(_mp_device)')
    text = text.replace('_mp_count.custom_minimum_size = Vector2(90, 0)', '_mp_count.custom_minimum_size = Vector2(54, 0)')
    editor_plugin.write_text(text, encoding="utf-8", newline="\n")
    keep_luau_with_rome(project)
    shutil.rmtree(project / "addons/ROMEStudio", ignore_errors=True)
    shutil.copytree(ROOT / "rome/addons/ROMEStudio", project / "addons/ROMEStudio", dirs_exist_ok=True, ignore=ignore)

    for asset in (ROOT / "rome/assets/templates").iterdir():
        if asset.suffix in (".png", ".svg"):
            shutil.copy2(asset, project / "GodotLuau/assets/textures" / asset.name)
    # Assets are created/imported in the destination project, never in the source checkout.
    for name in ("LocalScripts", "ServerScripts", "ModuleScripts"):
        (project / "GodotLuau" / name).mkdir(exist_ok=True)
    for name in ("Scenes", "Assets"):
        (project / name).mkdir(exist_ok=True)
    factory = project / "GodotLuau/icons/RocketMeteor.svg"
    if factory.is_file():
        shutil.copy2(factory, project / "GodotLuau/icons/MenuLogo.svg")
    branding = ROOT / "rome/assets/branding"
    if (branding / "rome.svg").is_file():
        for name in ("RocketMeteor.svg", "MenuLogo.svg", "GodotLuauIcono.svg"):
            shutil.copy2(branding / "rome.svg", project / "GodotLuau/icons" / name)
        splash = branding / "rome-engine.png" if (branding / "rome-engine.png").is_file() else branding / "rome.png"
        shutil.copy2(splash, project / "GodotLuau/icons/GodotLuauSplash.png")
        shutil.copy2(ROOT / "platform/windows/godot.ico", project / "GodotLuau/icons/GodotLuau.ico")
    hub = output / "hub"
    hub.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(("baseplate", "classic", "terrain", "empty", "obby", "starting")):
        sky = ("#86bdd5", "#8bc5df", "#9acbe0", "#83afce", "#9ccde3", "#8fc3db")[index]
        ground = ("#687789", "#8b8583", "#639650", "#4b667b", "#7b7299", "#70935c")[index]
        objects = ""
        if name == "obby":
            for i in range(7):
                x, y = 32 + i * 29, 164 - i * 13
                objects += f'<path d="M{x},{y}l25,-8 23,8 -25,9z" fill="{("#eeeef4", "#9864e5")[i%2]}"/>'
        elif name == "starting":
            objects = '<path d="M80 125l56-18 57 20-56 20z" fill="#a3835f"/><path d="M80 125v43l57 20v-41z" fill="#826548"/><path d="M137 147l56-20v43l-56 18z" fill="#65513d"/>'
        elif name != "empty":
            objects = '<path d="M105 157l42-15 44 16-43 16z" fill="#d3dce8"/><path d="M148 145v26m-33-12 66-1" stroke="#546176" stroke-width="2"/>'
        grid = "" if name == "empty" else ''.join(f'<path d="M{128+i*7} 92L{128+i*45} 220" stroke="#ffffff" stroke-opacity=".10"/>' for i in range(-8, 9)) + ''.join(f'<path d="M0 {y}H280" stroke="#ffffff" stroke-opacity=".12"/>' for y in (99,108,121,139,164,199))
        (hub / (name + ".svg")).write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="280" height="220"><defs><linearGradient id="s" x2="0" y2="1"><stop stop-color="{sky}"/><stop offset="1" stop-color="#dae6e9"/></linearGradient></defs><rect width="280" height="220" fill="url(#s)"/><path d="M0 92H280V220H0z" fill="{ground}"/>{grid}{objects}</svg>', encoding="utf-8")
    if (ROOT / "rome/assets/hub").is_dir():
        for asset in (ROOT / "rome/assets/hub").glob("*.png"):
            shutil.copy2(asset, hub / asset.name)
        if (hub / "hero-art.png").is_file():
            shutil.copy2(hub / "hero-art.png", hub / "hero.png")
    files = {}
    for path in sorted(project.rglob("*")):
        if path.is_file():
            files[str(path.relative_to(project)).replace("\\", "/")] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {"godot_luau_version": (source / "Version").read_text().strip(), "files": files}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    editor = ROOT / "bin/godot.windows.editor.x86_64.exe"
    if editor.is_file():
        public_editor = output.parent / "ROME Studio.exe"
        try:
            shutil.copy2(editor, public_editor)
        except PermissionError:
            # Leave a running editor and its unsaved work alone.
            public_editor = output.parent / "ROME Studio Preview.exe"
            shutil.copy2(editor, public_editor)
        print(f"Editor: {public_editor}")
    print(f"Bundled {len(files)} files: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path)
    parser.add_argument("--rome-only", action="store_true", help="Preserve the bundled GodotLuau code and binaries")
    parser.add_argument("--output", type=Path, default=ROOT / "bin/rome_runtime")
    parser.add_argument("--binary-source", type=Path)
    args = parser.parse_args()
    if args.rome_only:
        refresh_rome(args.output)
    elif args.source:
        package(args.source, args.output, args.binary_source)
    else:
        parser.error("--source is required unless --rome-only is used")

"""Build and verify a standalone ROME Linux editor from the vendored snapshot.

Run on Linux with the development packages listed in the release workflow.
GodotLuau and its dependencies are staged below bin/; input sources are read-only.
Android APKs require a separate native/runtime packaging pipeline and are not
represented as supported artifacts by this desktop builder.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from build_runtime import prepare
from package_runtime import keep_luau_with_rome

ROOT = Path(__file__).resolve().parents[1]


def rome_version():
    text = (ROOT / "core/version.h").read_text(encoding="utf-8")
    return re.search(r'#define ROME_VERSION "([^"]+)"', text).group(1)


def run(command, cwd=ROOT, **kwargs):
    print("+", " ".join(str(item) for item in command), flush=True)
    return subprocess.run([str(item) for item in command], cwd=cwd, check=True, **kwargs)


def stage_source(source: Path, destination: Path):
    if not (source / "SConstruct").is_file():
        raise ValueError(f"GodotLuau SConstruct is missing: {source}")
    ignore = shutil.ignore_patterns(".git", ".godot", "__pycache__", "bin", "models", "llama.cpp",
                                   "*.o", "*.obj", "*.a", "*.lib", "*.exp", "*.dll", "*.so", "*.pdb", "*.zip")
    shutil.copytree(source, destination, dirs_exist_ok=True, ignore=ignore)
    metadata_file = source / "dependencies.json"
    metadata = json.loads(metadata_file.read_text(encoding="utf-8")) if metadata_file.is_file() else {}
    for name in ("godot-cpp", "luau"):
        required = "SConstruct" if name == "godot-cpp" else "VM/src/lvmexecute.cpp"
        if (destination / name / required).is_file():
            continue
        dependency = metadata.get("dependencies", {}).get(name)
        if not dependency:
            raise ValueError(f"Missing {name} sources and dependency pin in {metadata_file}")
        revision = dependency["revision"]
        if not re.fullmatch(r"[0-9a-fA-F]{40}", revision):
            raise ValueError(f"{name} must use an immutable 40-character Git commit")
        checkout = destination / name
        checkout.mkdir(parents=True, exist_ok=True)
        if not (checkout / ".git").is_dir():
            run(["git", "init", checkout])
            run(["git", "-C", checkout, "remote", "add", "origin", dependency["url"]])
        else:
            run(["git", "-C", checkout, "remote", "set-url", "origin", dependency["url"]])
        run(["git", "-C", checkout, "fetch", "--depth", "1", "origin", revision])
        run(["git", "-C", checkout, "checkout", "--detach", "FETCH_HEAD"])
    return metadata


def adapt_presentation(project: Path):
    # These are the same bundled-copy presentation adapters used on Windows.
    # The vendored source and the user's upstream checkout remain untouched.
    settings = project / "addons/GodotLuauUpdater/plugin.gd"
    text = settings.read_text(encoding="utf-8")
    text = text.replace('"GodotLuau Config"', '"Luau"')
    text = text.replace('\t\tmake_bottom_panel_item_visible(scroll)', '\t\t# ROME keeps the viewport visible on startup.')
    settings.write_text(text, encoding="utf-8", newline="\n")
    editor = project / "addons/GodotLuauUpdater/plugin_editor.gd"
    text = editor.read_text(encoding="utf-8")
    text = text.replace('lbl.text = "Players"', 'lbl.text = "Jugadores"')
    text = text.replace('_mp_device.add_item("PC")', '_mp_device.add_item("Dispositivo actual")')
    text = text.replace('\t_mp_bar.add_child(_mp_device)', '\tvar device_slot := EditorInterface.get_base_control().find_child("ROMEDeviceSlot", true, false)\n\tif device_slot != null:\n\t\tdevice_slot.add_child(_mp_device)\n\telse:\n\t\t_mp_bar.add_child(_mp_device)')
    text = text.replace('_mp_count.custom_minimum_size = Vector2(90, 0)', '_mp_count.custom_minimum_size = Vector2(54, 0)')
    editor.write_text(text, encoding="utf-8", newline="\n")
    keep_luau_with_rome(project)


def bundle_runtime(source: Path, libraries: Path, output: Path, provenance: dict):
    project = output / "project"
    payload = project / "GodotLuau"
    payload.mkdir(parents=True, exist_ok=True)
    ignore = shutil.ignore_patterns("*.import", ".git", ".godot", "__pycache__", "*.o", "*.obj", "*.a", "*.lib", "*.exp", "*.pdb")
    for directory in ("icons", "assets", "DefaultScripts", "shaders", "licenses", "DedicatedServer"):
        if (source / "GodotLuau" / directory).is_dir():
            shutil.copytree(source / "GodotLuau" / directory, payload / directory, dirs_exist_ok=True, ignore=ignore)
    manifest = source / "GodotLuau/godot_luau.gdextension"
    if not manifest.is_file():
        manifest = source / "godot_luau.gdextension"
    shutil.copy2(manifest, payload / "godot_luau.gdextension")
    (payload / "bin").mkdir(exist_ok=True)
    for target in ("debug", "release"):
        name = f"godot_luau.linux.template_{target}.x86_64.so"
        if not (libraries / name).is_file():
            raise ValueError(f"Missing Linux extension: {libraries / name}")
        shutil.copy2(libraries / name, payload / "bin" / name)
    for name in ("LICENSE", "Version"):
        shutil.copy2(source / name, payload / name)
    shutil.copytree(source / "addons/GodotLuauUpdater", project / "addons/GodotLuauUpdater", dirs_exist_ok=True, ignore=ignore)
    adapt_presentation(project)
    addon = project / "addons/ROMEStudio"
    shutil.copytree(ROOT / "rome/addons/ROMEStudio", addon, dirs_exist_ok=True, ignore=ignore)
    (addon / "assets").mkdir(exist_ok=True)
    (payload / "assets/textures").mkdir(parents=True, exist_ok=True)
    for asset in (ROOT / "rome/assets/templates").iterdir():
        if asset.suffix in (".png", ".svg"):
            shutil.copy2(asset, addon / "assets" / asset.name)
            shutil.copy2(asset, payload / "assets/textures" / asset.name)
    for directory in ("Scenes", "Assets", "Scripts/LocalScript", "Scripts/ServerScript", "Scripts/ModuleScript"):
        (project / directory).mkdir(parents=True, exist_ok=True)
    branding = ROOT / "rome/assets/branding"
    for name in ("RocketMeteor.svg", "MenuLogo.svg", "GodotLuauIcono.svg"):
        shutil.copy2(branding / "rome.svg", payload / "icons" / name)
    shutil.copy2(branding / "rome-engine.png", payload / "icons/GodotLuauSplash.png")
    shutil.copy2(ROOT / "platform/windows/godot.ico", payload / "icons/GodotLuau.ico")
    hub = output / "hub"
    hub.mkdir(parents=True, exist_ok=True)
    for asset in (ROOT / "rome/assets/hub").glob("*.png"):
        shutil.copy2(asset, hub / asset.name)
    if (hub / "hero-art.png").is_file():
        shutil.copy2(hub / "hero-art.png", hub / "hero.png")
    (output / "licenses").mkdir(exist_ok=True)
    for name in ("LICENSE.txt", "COPYRIGHT.txt", "AUTHORS.md"):
        if (ROOT / name).is_file():
            shutil.copy2(ROOT / name, output / "licenses" / name)
    files = {path.relative_to(project).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
             for path in sorted(project.rglob("*")) if path.is_file()}
    engine_revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    data = {"platform": "linux", "arch": "x86_64", "rome_source_commit": engine_revision,
            "godot_luau_version": (source / "Version").read_text().strip(),
            "godot_luau_source": provenance, "files": files}
    (output / "manifest.json").write_text(json.dumps(data, indent=2), encoding="utf-8")


def smoke_test(editor: Path, runtime: Path, standalone: Path):
    project = ROOT / "bin/rome_linux_release_smoke"
    shutil.copytree(runtime / "project", project, dirs_exist_ok=True)
    (project / "project.godot").write_text('''config_version=5
[application]
config/name="ROME Linux release verification"
run/main_scene="res://Scenes/Place.tscn"
[editor_plugins]
enabled=PackedStringArray("res://addons/ROMEStudio/plugin.cfg")
[rome]
studio/enabled=true
[rendering]
renderer/rendering_method="gl_compatibility"
''', encoding="utf-8")
    (project / "Scenes/Place.tscn").write_text('[gd_scene format=3]\n\n[node name="Game" type="Game3D"]\nmetadata/rome_template = "obby"\n', encoding="utf-8")
    # Fresh-project generation exercises extension registration and the editor
    # addon. The game test then verifies real contacts, health and passenger carry.
    run([editor, "--headless", "--editor", "--path", project, "--max-fps", "60", "--quit-after", "300"], timeout=180)
    scene = (project / "Scenes/Place.tscn").read_text(encoding="utf-8")
    if 'metadata/rome_template_built = true' not in scene:
        raise ValueError("The Linux editor did not generate the Classic Obby scene")
    shutil.copy2(ROOT / "rome/qa/obby_runtime.gd", project / "runtime_qa.gd")
    run([editor, "--headless", "--path", project, "--script", "res://runtime_qa.gd"], timeout=90)
    result = json.loads((project / "runtime-result.json").read_text(encoding="utf-8"))
    if len(result.get("checks", [])) < 12:
        raise ValueError("Linux gameplay verification did not finish all 12 checks")
    # No adjacent rome_runtime folder exists in the release directory: this
    # invocation must open the archive appended to the ELF executable itself.
    run([standalone, "--headless", "--project-manager", "--quit-after", "3"], timeout=90)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "rome/vendor/GodotLuau")
    parser.add_argument("--version", default=rome_version(), help="Must match ROME_VERSION in core/version.h")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--output", type=Path, default=ROOT / "bin/release")
    parser.add_argument("--skip-editor-build", action="store_true")
    parser.add_argument("--skip-extension-build", action="store_true")
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        parser.error("This builder must run on Linux; use GitHub Actions from Windows")
    if not re.fullmatch(r"v?[0-9]+\.[0-9]+\.[0-9]+(?:[-.][A-Za-z0-9.]+)?", args.version):
        parser.error("Invalid release version")
    if not 1 <= args.jobs <= 8:
        parser.error("--jobs must be between 1 and 8")
    staged = ROOT / "bin/rome_release_luau_source"
    provenance = stage_source(args.source.resolve(), staged)
    extension = ROOT / "bin/rome_extension_build_linux"
    prepare(staged, extension)
    scons = [sys.executable, "-m", "SCons"]
    if not args.skip_editor_build:
        run(scons + ["platform=linuxbsd", "target=editor", "arch=x86_64", "debug_symbols=no", "lto=none", "accesskit=no", f"-j{args.jobs}"])
    if not args.skip_extension_build:
        for target in ("debug", "release"):
            run(scons + ["platform=linux", "arch=x86_64", f"target=template_{target}", "debug_symbols=no", "lto=none", f"-j{args.jobs}"], cwd=extension)
    runtime = ROOT / "bin/rome_runtime_linux"
    bundle_runtime(staged, extension / "GodotLuau/bin", runtime, provenance)
    editor = ROOT / "bin/godot.linuxbsd.editor.x86_64"
    output = args.output.resolve() / f"ROME-{args.version.removeprefix('v')}-linux-x86_64"
    run([sys.executable, ROOT / "rome/package_standalone.py", "--editor", editor, "--runtime", runtime, "--output", output])
    output.chmod(output.stat().st_mode | 0o111)
    smoke_test(editor, runtime, output)
    print(f"Verified Linux release asset: {output}", flush=True)


if __name__ == "__main__":
    main()

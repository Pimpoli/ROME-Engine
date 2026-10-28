"""Build a patched copy of GodotLuau for Godot 4.8 without editing its checkout."""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def prepare(source: Path, build: Path):
    ignore = shutil.ignore_patterns(".git", ".godot", "__pycache__", "*.o", "*.obj", "*.a", "*.lib", "*.exp", "*.dll", "*.so", "*.dylib", "*.pdb")
    build.mkdir(parents=True, exist_ok=True)
    for name in ("src", "luau", "godot-cpp"):
        active_ignore = shutil.ignore_patterns("gen", ".git", "__pycache__", "*.o", "*.obj", "*.a", "*.lib", "*.exp", "*.dll", "*.so", "*.pdb") if name == "godot-cpp" else ignore
        shutil.copytree(source / name, build / name, dirs_exist_ok=True, ignore=active_ignore)
    shutil.copy2(source / "SConstruct", build / "SConstruct")
    (build / "GodotLuau/bin").mkdir(parents=True, exist_ok=True)
    # Godot 4.8's GDType rejects members shadowing an inherited method/enum.
    player = build / "src/characters/roblox_player2d.h"
    text = player.read_text(encoding="utf-8")
    text = text.replace('D_METHOD("get_gravity")', 'D_METHOD("get_roblox_gravity")')
    text = text.replace('"set_gravity", "get_gravity"', '"set_gravity", "get_roblox_gravity"')
    player.write_text(text, encoding="utf-8")
    gui = build / "src/ui/roblox_gui_texto.h"
    text = gui.read_text(encoding="utf-8")
    text = text.replace('PropertyInfo(Variant::INT,  "TextDirection")', 'PropertyInfo(Variant::INT,  "RobloxTextDirection")')
    gui.write_text(text, encoding="utf-8")
    # Keep Luau's public TextDirection API while avoiding Control's enum name.
    reader = build / "src/core/luau_api_indice_gui.h"
    text = reader.read_text(encoding="utf-8")
    marker = "inline int gl_index_gui(lua_State* L, Node* n, const char* key) {"
    text = text.replace(marker, marker + '\n    if (strcmp(key, "TextDirection") == 0 && n->has_method("get_TextDirection")) { lua_pushinteger(L, (int)n->call("get_TextDirection")); return 1; }')
    reader.write_text(text, encoding="utf-8")
    writer = build / "src/core/luau_api_escritura.h"
    text = writer.read_text(encoding="utf-8")
    marker = "    if (!wrapper || !gow_node(wrapper)) return 0;"
    text = text.replace(marker, marker + '\n    if (strcmp(key, "TextDirection") == 0 && gow_node(wrapper)->has_method("set_TextDirection")) { gow_node(wrapper)->call("set_TextDirection", (int)gl_num(L, 3)); return 0; }', 1)
    writer.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--target", choices=("debug", "release", "both"), default="both")
    args = parser.parse_args()
    build = ROOT / "bin/rome_extension_build"
    prepare(args.source.resolve(), build)
    for target in (("debug", "release") if args.target == "both" else (args.target,)):
        subprocess.run(["scons", "platform=windows", "target=template_" + target, "use_mingw=yes", f"-j{args.jobs}"], cwd=build, check=True)
    print("ROME GodotLuau binaries:", build / "GodotLuau/bin")

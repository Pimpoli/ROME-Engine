"""Replace Godot's robot artwork with the supplied ROME artwork."""
import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def apply(svg: Path, png: Path, wordmark_svg: Path | None = None, wordmark_png: Path | None = None):
    assets = ROOT / "rome/assets/branding"
    assets.mkdir(parents=True, exist_ok=True)
    shutil.copy2(svg, assets / "rome.svg")
    shutil.copy2(png, assets / "rome.png")
    if wordmark_svg:
        shutil.copy2(wordmark_svg, assets / "rome-engine.svg")
    if wordmark_png:
        shutil.copy2(wordmark_png, assets / "rome-engine.png")
    source = svg.read_text(encoding="utf-8")
    targets = ["editor/icons/Godot.svg", "editor/icons/GodotMonochrome.svg", "editor/icons/GodotFile.svg", "editor/icons/Logo.svg", "editor/icons/TitleBarLogo.svg", "editor/icons/DefaultProjectIcon.svg", "editor/icons/ROME.svg", "misc/logo/icon.svg", "misc/logo/icon_outlined.svg", "misc/logo/logo.svg", "misc/logo/logo_outlined.svg", "misc/dist/html/logo.svg", "misc/dist/windows/icon_console.svg"]
    for relative in targets:
        target = ROOT / relative
        old = target.read_text(encoding="utf-8")
        dimensions = re.search(r'<svg\b[^>]*', old).group()
        content = source
        for key in ("width", "height"):
            match = re.search(rf'\b{key}="([0-9.]+)"', dimensions)
            value = match.group(1) if match else "64"
            content = re.sub(rf'\b{key}="1024"', f'{key}="{value}"', content, count=1)
        target.write_text(content, encoding="utf-8", newline="\n")
    if (assets / "rome-engine.svg").is_file():
        full_source = (assets / "rome-engine.svg").read_text(encoding="utf-8")
        for relative in ("editor/icons/Logo.svg", "misc/logo/logo.svg", "misc/logo/logo_outlined.svg", "misc/dist/html/logo.svg"):
            target = ROOT / relative
            dimensions = re.search(r'<svg\b[^>]*', target.read_text(encoding="utf-8")).group()
            content = full_source
            for key in ("width", "height"):
                match = re.search(rf'\b{key}="([0-9.]+)"', dimensions)
                value = match.group(1) if match else "128"
                if key == "height":
                    width = float(re.search(r'\bwidth="([0-9.]+)"', dimensions).group(1))
                    value = str(round(width * 1046 / 1504))
                content = re.sub(rf'\b{key}="[0-9.]+"', f'{key}="{value}"', content, count=1)
            target.write_text(content, encoding="utf-8", newline="\n")
    # Apple's layered icon source must also lose the old robot layers.
    apple = ROOT / "misc/dist/macos/GodotLG.icon"
    (apple / "Assets/layer_0.svg").write_text(source, encoding="utf-8", newline="\n")
    for layer in (1, 2):
        (apple / f"Assets/layer_{layer}.svg").write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024"/>\n', encoding="utf-8")
    icon_config = json.loads((apple / "icon.json").read_text(encoding="utf-8"))
    icon_config["groups"][0]["layers"] = [{"image-name": "layer_0.svg", "name": "ROME"}]
    (apple / "icon.json").write_text(json.dumps(icon_config, indent=2) + "\n", encoding="utf-8")
    subprocess.run([str(ROOT / "bin/godot.windows.editor.x86_64.console.exe"), "--headless", "--path", str(ROOT), "--script", "rome/convert_branding.gd"], check=True)
    rendered = assets / "rendered"
    sizes = [16, 24, 32, 48, 64, 128, 256]
    entries = [ (size, (rendered / f"icon{size}.png").read_bytes()) for size in sizes ]
    offset = 6 + 16 * len(entries)
    directory = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    images = bytearray()
    for size, data in entries:
        directory.extend(struct.pack("<BBBBHHII", size if size < 256 else 0, size if size < 256 else 0, 0, 0, 1, 32, len(data), offset))
        images.extend(data)
        offset += len(data)
    for relative in ("platform/windows/godot.ico", "platform/windows/godot_console.ico"):
        (ROOT / relative).write_bytes(directory + images)
    chunks = bytearray()
    for kind, size in ((b"ic07", 128), (b"ic08", 256), (b"ic09", 512), (b"ic10", 1024)):
        data = (rendered / f"icon{size}.png").read_bytes()
        chunks.extend(kind + struct.pack(">I", len(data) + 8) + data)
    icns = b"icns" + struct.pack(">I", len(chunks) + 8) + chunks
    for relative in ("misc/dist/macos_tools.app/Contents/Resources/GodotLG.icns", "misc/dist/macos_template.app/Contents/Resources/icon.icns"):
        (ROOT / relative).write_bytes(icns)
    print(f"Replaced {len(targets)} vector assets, Windows/macOS icons and startup artwork.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--svg", type=Path, required=True)
    parser.add_argument("--png", type=Path, required=True)
    parser.add_argument("--wordmark-svg", type=Path)
    parser.add_argument("--wordmark-png", type=Path)
    args = parser.parse_args()
    apply(args.svg, args.png, args.wordmark_svg, args.wordmark_png)

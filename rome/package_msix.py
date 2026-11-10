"""Build the Microsoft Store package (MSIX) of ROME Studio.

The Store signs what it publishes, so Windows shows no SmartScreen warning.
The editor sits next to rome_runtime/, which the hub reads in place instead of
extracting the copy appended to the standalone executable. AppxManifest.xml next
to the editor tells the updater to stay off: the Store installs new versions.

Identity values come from Partner Center (Product management > Product identity).
Without them the package is only good for local tests.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
from xml.sax.saxutils import escape

from package_standalone import embedded_version, runtime_files

ROOT = Path(__file__).resolve().parents[1]
ICON = ROOT / "rome/assets/branding/rendered/icon1024.png"
LOGOS = {
    "StoreLogo.png": 50,
    "Square44x44Logo.png": 44,
    "Square44x44Logo.targetsize-44_altform-unplated.png": 44,
    "Square150x150Logo.png": 150,
}
PRERELEASE = re.compile(r"^(alpha|beta|rc)\.(\d+)$")
MANIFEST = """<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
  xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
  xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
  IgnorableNamespaces="uap rescap">
  <Identity Name="{name}" Publisher="{publisher}" Version="{version}" ProcessorArchitecture="x64" />
  <Properties>
    <DisplayName>ROME Studio</DisplayName>
    <PublisherDisplayName>{publisher_name}</PublisherDisplayName>
    <Logo>Assets\\StoreLogo.png</Logo>
  </Properties>
  <Dependencies>
    <TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.17763.0" MaxVersionTested="10.0.26100.0" />
  </Dependencies>
  <Resources>
    <Resource Language="es-ES" />
  </Resources>
  <Applications>
    <Application Id="ROMEStudio" Executable="ROME Studio.exe" EntryPoint="Windows.FullTrustApplication">
      <uap:VisualElements DisplayName="ROME Studio" Description="Editor de experiencias con Luau sobre Godot"
        BackgroundColor="transparent" Square150x150Logo="Assets\\Square150x150Logo.png" Square44x44Logo="Assets\\Square44x44Logo.png" />
    </Application>
  </Applications>
  <Capabilities>
    <rescap:Capability Name="runFullTrust" />
  </Capabilities>
</Package>
"""


def store_version(version: str):
    """0.1.0-alpha.4 -> 0.1.4.0. The Store takes four numbers ending in 0, each upload
    higher than the last, so the third one holds the patch and the prerelease:
    alpha 0-99, beta 100-199, rc 200-299 and 999 for the stable release."""
    core, _, prerelease = version.partition("-")
    major, minor, patch = (int(part) for part in core.split("."))
    step = 999
    if prerelease:
        match = PRERELEASE.match(prerelease)
        if not match or int(match[2]) > 99:
            raise ValueError(f"There is no Store version for {version}")
        step = {"alpha": 0, "beta": 100, "rc": 200}[match[1]] + int(match[2])
    if patch > 64:
        raise ValueError(f"{version} does not fit the Store version scheme")
    return f"{major}.{minor}.{patch * 1000 + step}.0"


def make_logos(folder: Path):
    # System.Drawing comes with Windows, so no image library is needed
    folder.mkdir(parents=True, exist_ok=True)
    quoted = lambda path: str(path).replace("'", "''")
    lines = ["Add-Type -AssemblyName System.Drawing", f"$icon = [System.Drawing.Image]::FromFile('{quoted(ICON)}')"]
    for name, size in LOGOS.items():
        lines += [
            f"$bitmap = New-Object System.Drawing.Bitmap {size}, {size}",
            "$canvas = [System.Drawing.Graphics]::FromImage($bitmap)",
            "$canvas.InterpolationMode = 'HighQualityBicubic'",
            "$canvas.PixelOffsetMode = 'HighQuality'",
            f"$canvas.DrawImage($icon, 0, 0, {size}, {size})",
            f"$bitmap.Save('{quoted(folder / name)}', [System.Drawing.Imaging.ImageFormat]::Png)",
            "$canvas.Dispose()",
            "$bitmap.Dispose()",
        ]
    lines.append("$icon.Dispose()")
    subprocess.run(["powershell", "-NoProfile", "-Command", "; ".join(lines)], check=True)


def find_makeappx():
    kits = Path("C:/Program Files (x86)/Windows Kits/10/bin")
    found = sorted(kits.glob("10.*/x64/makeappx.exe"))
    if not found:
        raise ValueError("makeappx.exe is missing; install the Windows SDK")
    return found[-1]


def package(editor: Path, runtime: Path, output: Path, name: str, publisher: str, publisher_name: str):
    editor, runtime, output = editor.resolve(), runtime.resolve(), output.resolve()
    if not editor.is_file() or not (runtime / "manifest.json").is_file():
        raise ValueError("Use an existing editor and a runtime folder with its manifest.json")
    if not (runtime / "project/GodotLuau/godot_luau.gdextension").is_file():
        raise ValueError("The runtime has no GodotLuau extension")
    version = embedded_version(editor)
    if not version:
        raise ValueError("Could not read the ROME version compiled into the editor")
    layout = output.with_suffix("")
    if layout.exists():
        shutil.rmtree(layout)
    layout.mkdir(parents=True)
    shutil.copy2(editor, layout / "ROME Studio.exe")
    for path in runtime_files(runtime):
        target = layout / "rome_runtime" / path.relative_to(runtime)
        if path.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    make_logos(layout / "Assets")
    manifest = MANIFEST.format(
        name=escape(name, {'"': "&quot;"}),
        publisher=escape(publisher, {'"': "&quot;"}),
        version=store_version(version),
        publisher_name=escape(publisher_name),
    )
    (layout / "AppxManifest.xml").write_text(manifest, encoding="utf-8")
    result = subprocess.run([str(find_makeappx()), "pack", "/d", str(layout), "/p", str(output), "/o"], capture_output=True, text=True)
    if result.returncode != 0:
        raise ValueError("makeappx failed:\n" + (result.stdout + result.stderr).strip()[-3000:])
    print(f"Store package: {output}\nROME version: {version} (Store {store_version(version)})\nLayout: {layout}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--name", default="RocketMeteor.ROMEStudio", help="Package/Identity/Name from Partner Center")
    parser.add_argument("--publisher", default="CN=ROME Studio Pruebas", help="Package/Identity/Publisher from Partner Center")
    parser.add_argument("--publisher-name", default="Rocket Meteor", help="Package/Properties/PublisherDisplayName from Partner Center")
    args = parser.parse_args()
    try:
        package(args.editor, args.runtime, args.output, args.name, args.publisher, args.publisher_name)
    except ValueError as error:
        raise SystemExit(f"package_msix: {error}")

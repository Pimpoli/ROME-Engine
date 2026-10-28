"""Create a single-file desktop editor with its runtime embedded as a ZIP.

The native project hub extracts the archive into a versioned cache at first
launch. The original editor and runtime directories are read-only inputs.

Release names (ROME-Studio-<version>-windows-x86_64.exe, ROME-<version>-linux-x86_64)
must match the version compiled into the editor; otherwise the updater would
offer the same release again after installing it.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import zipfile

RELEASE_NAME = re.compile(r"^ROME-(?:Studio-)?(?P<version>\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)-(?:windows-x86_64\.exe|linux-x86_64)$")
SKIPPED_PARTS = {".godot", ".git", "__pycache__"}
SKIPPED_SUFFIXES = {".import", ".pyc"}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def embedded_version(editor: Path):
    """ROME_VERSION as compiled into the editor, from its "ROME Engine <version>" banner."""
    found = {m.decode().rstrip(".-") for m in re.findall(rb"ROME Engine (\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)", editor.read_bytes())}
    return found.pop() if len(found) == 1 else None


def runtime_files(runtime: Path):
    files = []
    for path in sorted(runtime.rglob("*")):
        relative = path.relative_to(runtime)
        if any(part in SKIPPED_PARTS for part in relative.parts) or path.suffix in SKIPPED_SUFFIXES:
            continue
        if ":" in relative.as_posix() or ".." in relative.parts:
            raise ValueError(f"The hub refuses to extract this runtime path: {relative.as_posix()}")
        if path.is_dir() or path.is_file():
            files.append(path)
    return files


def package(editor: Path, runtime: Path, output: Path):
    editor, runtime, output = editor.resolve(), runtime.resolve(), output.resolve()
    if editor == output or not editor.is_file():
        raise ValueError("Use an existing editor and a different output path")
    manifest_path = runtime / "manifest.json"
    if not manifest_path.is_file():
        raise ValueError("Runtime manifest is missing")
    if not (runtime / "project/GodotLuau/godot_luau.gdextension").is_file():
        raise ValueError("The runtime has no GodotLuau extension; the hub would never finish installing it")
    version = embedded_version(editor)
    release = RELEASE_NAME.match(output.name)
    if release and release["version"] != version:
        raise ValueError(f"The release name says {release['version']} but the editor was built as {version}. "
                         "Update ROME_VERSION in core/version.h and rebuild, or the updater would offer this release forever.")
    files = runtime_files(runtime)
    # The hub names its cache after the embedded manifest. Hashing the whole
    # payload into it makes any changed runtime file extract to a fresh folder.
    content = hashlib.sha256()
    for path in files:
        if path.is_file() and path != manifest_path:
            content.update(path.relative_to(runtime).as_posix().encode() + b"\0" + sha256(path).encode() + b"\n")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["standalone"] = {"rome_version": version, "content_sha256": content.hexdigest()}
    output.parent.mkdir(parents=True, exist_ok=True)
    partial = output.with_name(output.name + ".partial")
    shutil.copy2(editor, partial)
    count = 0
    try:
        with zipfile.ZipFile(partial, "a", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in files:
                name = "rome_runtime/" + path.relative_to(runtime).as_posix()
                if path.is_dir():
                    archive.write(path, name + "/")
                elif path == manifest_path:
                    archive.writestr(name, json.dumps(manifest, indent=2))
                    count += 1
                else:
                    archive.write(path, name)
                    count += 1
        with zipfile.ZipFile(partial) as archive:
            if archive.testzip() is not None:
                raise ValueError("Embedded archive failed its integrity check")
            if json.loads(archive.read("rome_runtime/manifest.json")).get("standalone") != manifest["standalone"]:
                raise ValueError("Embedded manifest does not match the packaged runtime")
        checksum = sha256(partial)
        try:
            os.replace(partial, output)
        except PermissionError:
            raise ValueError(f"{output.name} is open; close it and package again") from None
    finally:
        partial.unlink(missing_ok=True)
    checksum_file = output.with_name(output.name + ".sha256")
    checksum_file.with_name(checksum_file.name + ".partial").write_text(checksum + "  " + output.name + "\n", encoding="utf-8")
    os.replace(checksum_file.with_name(checksum_file.name + ".partial"), checksum_file)
    print(f"Standalone editor: {output}\nROME version: {version}\nEmbedded files: {count}\nSHA256: {checksum}")
    if not release:
        print(f"Release asset name for the updater: ROME-Studio-{version}-windows-x86_64.exe or ROME-{version}-linux-x86_64")
    return checksum


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        package(args.editor, args.runtime, args.output)
    except ValueError as error:
        raise SystemExit(f"package_standalone: {error}")

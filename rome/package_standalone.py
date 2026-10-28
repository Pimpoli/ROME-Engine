"""Create a single-file desktop editor with its runtime embedded as a ZIP.

The native project hub extracts the archive into a versioned cache at first
launch. The original editor and runtime directories are read-only inputs.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import zipfile


def package(editor: Path, runtime: Path, output: Path):
    editor, runtime, output = editor.resolve(), runtime.resolve(), output.resolve()
    if editor == output or not editor.is_file():
        raise ValueError("Use an existing editor and a different output path")
    if not (runtime / "manifest.json").is_file():
        raise ValueError("Runtime manifest is missing")
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(editor, output)
    files = []
    with zipfile.ZipFile(output, "a", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(runtime.rglob("*")):
            relative = path.relative_to(runtime)
            if any(part in {".godot", ".git", "__pycache__"} for part in relative.parts) or path.suffix in {".import", ".pyc"}:
                continue
            if path.is_dir():
                archive.write(path, "rome_runtime/" + relative.as_posix() + "/")
                continue
            if not path.is_file():
                continue
            archive.write(path, "rome_runtime/" + relative.as_posix())
            files.append(relative.as_posix())
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError("Embedded archive failed its integrity check")
    checksum = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_name(output.name + ".sha256").write_text(checksum + "  " + output.name + "\n", encoding="utf-8")
    print(f"Standalone editor: {output}\nEmbedded files: {len(files)}\nSHA256: {checksum}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    package(args.editor, args.runtime, args.output)

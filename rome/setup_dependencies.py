"""Fetch the exact public source dependencies used by the bundled GodotLuau."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def setup(source: Path):
    source = source.resolve()
    manifest = json.loads((source / "dependencies.json").read_text(encoding="utf-8"))
    for name, dependency in manifest["dependencies"].items():
        destination = source / name
        if destination.exists() and not (destination / ".git").exists():
            raise SystemExit(f"Refusing to overwrite a non-Git directory: {destination}")
        if not destination.exists():
            destination.mkdir()
            subprocess.run(["git", "init", str(destination)], check=True)
            subprocess.run(["git", "-C", str(destination), "remote", "add", "origin", dependency["url"]], check=True)
        status = subprocess.check_output(["git", "-C", str(destination), "status", "--porcelain"], text=True)
        if status.strip():
            raise SystemExit(f"Dependency has local changes; preserve them before updating: {destination}")
        subprocess.run(["git", "-C", str(destination), "fetch", "--depth", "1", "origin", dependency["revision"]], check=True)
        subprocess.run(["git", "-C", str(destination), "checkout", "--detach", dependency["revision"]], check=True)
        print(f"Ready: {name} at {dependency['revision']}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=ROOT / "rome/vendor/GodotLuau")
    setup(parser.parse_args().source)

"""Owner-operated GodotLuau project backup and dedicated game server.

Python 3.10+, standard library only. See README.md beside this file.
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
from pathlib import Path, PurePosixPath
import re
import secrets
import shutil
import ssl
import stat
import subprocess
import threading
import time
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAX_ARCHIVE = 2 * 1024**3
MAX_FILES = 50000


def validate_archive(path: Path) -> list[zipfile.ZipInfo]:
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        if len(entries) > MAX_FILES or sum(x.file_size for x in entries) > MAX_ARCHIVE:
            raise ValueError("Project exceeds the file or expanded-size limit")
        seen = set()
        for entry in entries:
            name = entry.orig_filename
            parts = PurePosixPath(name).parts
            if (not parts or name.startswith("/") or "\\" in name or ":" in name
                    or any(x in (".", "..") or x.startswith(".") for x in parts)
                    or stat.S_ISLNK(entry.external_attr >> 16)):
                raise ValueError("Archive contains an unsafe or hidden path")
            key = name.rstrip("/").casefold()
            if key in seen:
                raise ValueError("Archive has duplicate paths")
            seen.add(key)
        if "project.godot" not in seen:
            raise ValueError("Archive must contain project.godot at its root")
        bad = archive.testzip()
        if bad:
            raise ValueError("Archive checksum failed")
        return entries


class Store:
    def __init__(self, root: Path, engine: Path | None = None, base_port: int = 25565):
        self.root = root.resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        self.engine = engine.resolve() if engine else None
        self.base_port = base_port
        self.lock = threading.RLock()
        self.games: dict[str, subprocess.Popen] = {}

    def project(self, name: str) -> Path:
        if not re.fullmatch(r"[a-zA-Z0-9_-]{1,80}", name):
            raise ValueError("Invalid project ID")
        target = self.root / name
        if target.is_symlink() or target.resolve().parent != self.root:
            raise ValueError("Project directory cannot be a link")
        target.mkdir(exist_ok=True)
        return target

    def state(self, name: str) -> dict:
        path = self.project(name) / "state.json"
        data = json.loads(path.read_text()) if path.exists() else {}
        process = self.games.get(name)
        return {**data, "running": process is not None and process.poll() is None}

    def save_state(self, name: str, data: dict):
        path = self.project(name) / "state.json"
        temporary = path.with_suffix(".new")
        temporary.write_text(json.dumps(data, indent=2), encoding="utf-8")
        temporary.replace(path)

    def snapshot(self, name: str, incoming: Path) -> dict:
        validate_archive(incoming)
        digest = self.digest(incoming)
        with self.lock:
            project = self.project(name)
            archive = project / (digest + ".zip")
            if not archive.exists():
                incoming.replace(archive)
            state = self.state(name)
            state.update(revision=digest, saved_at=int(time.time()))
            self.save_state(name, state)
            return state

    @staticmethod
    def digest(path: Path) -> str:
        result = hashlib.sha256()
        with path.open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                result.update(block)
        return result.hexdigest()

    def publish(self, name: str) -> dict:
        with self.lock:
            if self.engine is None or not self.engine.is_file():
                raise ValueError("Configure --engine with the compatible GodotLuau/ROME editor executable")
            project = self.project(name)
            state = self.state(name)
            revision = state.get("revision", "")
            if not re.fullmatch(r"[0-9a-f]{64}", revision):
                raise ValueError("Upload a snapshot before publishing")
            archive = project / (revision + ".zip")
            entries = validate_archive(archive)
            release = project / ("release-" + revision)
            ready = release / ".import-ready"
            if not ready.exists():
                # Fresh, owned staging path, never a user project folder.
                staging = project / ("staging-" + secrets.token_hex(8))
                staging.mkdir()
                with zipfile.ZipFile(archive) as source:
                    for entry in entries:
                        target = staging.joinpath(*PurePosixPath(entry.filename).parts)
                        if not target.resolve().is_relative_to(staging.resolve()):
                            raise ValueError("Archive path escapes staging")
                        if entry.is_dir():
                            target.mkdir(parents=True, exist_ok=True)
                        else:
                            target.parent.mkdir(parents=True, exist_ok=True)
                            with source.open(entry) as content, target.open("wb") as output:
                                shutil.copyfileobj(content, output)
                # Editor plugins are authoring tools, not part of a dedicated
                # game. Do not run their updaters, QA hooks or UI during import.
                settings = staging / "project.godot"
                settings.write_text(re.sub(r"(?ms)^\[editor_plugins\]\s*.*?(?=^\[|\Z)", "", settings.read_text(encoding="utf-8")), encoding="utf-8")
                with (project / "import.log").open("wb") as log:
                    result = subprocess.run([str(self.engine), "--headless", "--editor", "--path", str(staging), "--import"],
                                            stdout=log, stderr=subprocess.STDOUT, timeout=180,
                                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                if result.returncode:
                    raise ValueError("Server import failed; inspect import.log. Previous game was preserved")
                (staging / ".import-ready").touch()
                staging.rename(release)
            if state.get("published_revision") == revision and state["running"]:
                return state
            used = {self.state(p.name).get("port") for p in self.root.iterdir() if p.is_dir() and re.fullmatch(r"[a-zA-Z0-9_-]{1,80}", p.name)}
            port = state.get("port", next((p for p in range(self.base_port, min(65535, self.base_port + 128)) if p not in used), None))
            if port is None:
                raise ValueError("No game ports available")
            old_revision = state.get("published_revision")
            self.stop(name)
            process = self._launch(project, release, port)
            time.sleep(1)
            if process.poll() is not None:
                if old_revision and old_revision != revision:
                    self.games[name] = self._launch(project, project / ("release-" + old_revision), port)
                raise ValueError("Game exited during startup; inspect game.log")
            self.games[name] = process
            state.update(published_revision=revision, port=port, running=True)
            self.save_state(name, state)
            return state

    def _launch(self, project: Path, release: Path, port: int):
        with (project / "game.log").open("ab") as log:
            return subprocess.Popen([str(self.engine), "--headless", "--path", str(release), "--", "--glserver", str(port)],
                                    stdout=log, stderr=subprocess.STDOUT,
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))

    def stop(self, name: str):
        process = self.games.pop(name, None)
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


class Handler(BaseHTTPRequestHandler):
    server_version = "GodotLuauServer/1"

    def log_message(self, fmt, *args):
        # Do not record authentication material or user-controlled URLs.
        pass

    def send_json(self, status, data):
        payload = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def handle_request(self):
        supplied = self.headers.get("Authorization", "")
        if not hmac.compare_digest(supplied, "Bearer " + self.server.token):
            self.send_json(401, {"error": "Authentication required"})
            return
        if self.command == "GET" and self.path == "/health":
            self.send_json(200, {"service": "GodotLuauServer", "protocol": 1})
            return
        match = re.fullmatch(r"/v1/projects/([a-zA-Z0-9_-]{1,80})/(snapshot|publish|status|stop)", self.path)
        if not match:
            self.send_json(404, {"error": "Unknown endpoint"})
            return
        name, action = match.groups()
        try:
            if action == "snapshot" and self.command == "PUT":
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= MAX_ARCHIVE:
                    raise ValueError("Invalid archive size")
                incoming = self.server.store.project(name) / ("upload-" + secrets.token_hex(8) + ".tmp")
                try:
                    remaining = size
                    self.connection.settimeout(90)
                    with incoming.open("wb") as output:
                        while remaining:
                            block = self.rfile.read(min(remaining, 1024 * 1024))
                            if not block:
                                raise ValueError("Incomplete upload")
                            output.write(block)
                            remaining -= len(block)
                    result = self.server.store.snapshot(name, incoming)
                finally:
                    incoming.unlink(missing_ok=True)
            elif action == "publish" and self.command == "POST":
                result = self.server.store.publish(name)
            elif action == "stop" and self.command == "POST":
                with self.server.store.lock:
                    self.server.store.stop(name)
                result = self.server.store.state(name)
            elif action == "status" and self.command == "GET":
                result = self.server.store.state(name)
            else:
                self.send_json(405, {"error": "Method not allowed"})
                return
            self.send_json(200, result)
        except (ValueError, OSError, zipfile.BadZipFile, subprocess.TimeoutExpired) as error:
            self.send_json(400, {"error": str(error)})

    do_GET = handle_request
    do_PUT = handle_request
    do_POST = handle_request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=Path("server-data"))
    parser.add_argument("--engine", type=Path)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8088)
    parser.add_argument("--game-port", type=int, default=25565)
    parser.add_argument("--cert", type=Path)
    parser.add_argument("--key", type=Path)
    args = parser.parse_args()
    if args.bind not in ("127.0.0.1", "localhost", "::1") and not (args.cert and args.key):
        parser.error("External management requires --cert and --key (HTTPS). Alternatively use a local TLS reverse proxy")
    store = Store(args.data, args.engine, args.game_port)
    token_file = store.root / "owner-token.txt"
    if not token_file.exists():
        token_file.write_text(secrets.token_urlsafe(32), encoding="utf-8")
        token_file.chmod(0o600)
    server = ThreadingHTTPServer((args.bind, args.port), Handler)
    server.daemon_threads = True
    server.store = store
    server.token = token_file.read_text().strip()
    if args.cert and args.key:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(str(args.cert), str(args.key))
        server.socket = context.wrap_socket(server.socket, server_side=True)
    print(f"GodotLuau server manager listening on {args.bind}:{args.port}", flush=True)
    print(f"Owner token file: {token_file}. Keep this private.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        with store.lock:
            for name in list(store.games):
                store.stop(name)
        server.server_close()


if __name__ == "__main__":
    main()

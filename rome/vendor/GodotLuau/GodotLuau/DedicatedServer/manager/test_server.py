import io
import json
from pathlib import Path
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
import zipfile

from server import Handler, Store, ThreadingHTTPServer, validate_archive


def project_zip(extra=None):
    result = io.BytesIO()
    with zipfile.ZipFile(result, "w") as archive:
        archive.writestr("project.godot", 'config_version=5\n[application]\nconfig/name="Fixture"\n')
        for name, content in (extra or {}).items():
            entry = zipfile.ZipInfo(name)
            entry.filename = name
            archive.writestr(entry, content)
    return result.getvalue()


class ServerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.store = Store(self.root / "data")

    def tearDown(self):
        self.temp.cleanup()

    def test_snapshot_preserves_history_and_deduplicates(self):
        source = self.root / "incoming.zip"
        source.write_bytes(project_zip({"Scenes/Main.tscn": "first"}))
        first = self.store.snapshot("fixture", source)
        source.write_bytes(project_zip({"Scenes/Main.tscn": "first"}))
        second = self.store.snapshot("fixture", source)
        self.assertEqual(first["revision"], second["revision"])
        source.write_bytes(project_zip({"Scenes/Main.tscn": "second"}))
        third = self.store.snapshot("fixture", source)
        self.assertNotEqual(first["revision"], third["revision"])
        self.assertEqual(len(list(self.store.project("fixture").glob("*.zip"))), 2)
        self.assertFalse(third["running"])

    def test_rejects_escaping_hidden_and_absolute_paths(self):
        source = self.root / "incoming.zip"
        for name in ["../outside", "/root/file", "C:/drive", "Scripts\\escape", ".git/config", "a/../../b"]:
            with self.subTest(name=name):
                source.write_bytes(project_zip({name: "bad"}))
                with self.assertRaises(ValueError):
                    validate_archive(source)

    def test_rejects_case_collision(self):
        source = self.root / "incoming.zip"
        source.write_bytes(project_zip({"Scripts/A.lua": "1", "scripts/a.lua": "2"}))
        with self.assertRaises(ValueError):
            validate_archive(source)

    def test_rejects_unsafe_project_id_and_missing_engine(self):
        for name in ["../outside", "a/b", ".", "C:", ""]:
            with self.assertRaises(ValueError):
                self.store.project(name)
        with self.assertRaises(ValueError):
            self.store.publish("fixture")

    def test_http_auth_upload_and_status(self):
        http = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        http.store = self.store
        http.token = "test-only-owner-token"
        thread = threading.Thread(target=http.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{http.server_port}"
        def request(path, method="GET", body=None, auth=True):
            headers = {"Authorization": "Bearer " + http.token} if auth else {}
            req = urllib.request.Request(base + path, data=body, method=method, headers=headers)
            with urllib.request.urlopen(req, timeout=3) as response:
                return json.load(response)
        try:
            with self.assertRaises(urllib.error.HTTPError) as error:
                request("/health", auth=False)
            self.assertEqual(error.exception.code, 401)
            error.exception.close()
            self.assertEqual(request("/health")["protocol"], 1)
            uploaded = request("/v1/projects/fixture/snapshot", "PUT", project_zip())
            state = request("/v1/projects/fixture/status")
            self.assertEqual(uploaded["revision"], state["revision"])
            self.assertFalse(state["running"])
            with self.assertRaises(urllib.error.HTTPError) as invalid:
                request("/v1/projects/fixture/snapshot", "PUT", project_zip({"../escape": "bad"}))
            invalid.exception.close()
            self.assertEqual(request("/v1/projects/fixture/status")["revision"], state["revision"])
        finally:
            http.shutdown()
            http.server_close()
            thread.join()


if __name__ == "__main__":
    unittest.main()

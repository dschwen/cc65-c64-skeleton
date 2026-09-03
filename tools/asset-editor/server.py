#!/usr/bin/env python3
"""Local development server for the C64 asset editor.

Serves the editor UI and exposes a small API limited to the repository's
assets/ directory.
"""

from __future__ import annotations

import argparse
import json
import mimetypes
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse


APP_DIR = Path(__file__).resolve().parent
REPO_ROOT = APP_DIR.parents[1]
ASSETS_DIR = REPO_ROOT / "assets"


def _is_resource_id(name: str) -> int | None:
    if len(name) != 2:
        return None
    try:
        value = int(name, 16)
    except ValueError:
        return None
    return value


def _asset_kinds(path: Path) -> list[str]:
    kinds: set[str] = set()
    suffix = path.suffix.lower()

    # assets/scripts/<ID>.script is plain-text DSL source (see
    # tools/compile_script.py) - identified by location/extension, not
    # content, same as portraits are identified by living under
    # assets/portraits/.
    if path.parent.name == "scripts" and suffix == ".script" and \
            _is_resource_id(path.stem) is not None:
        return ["script"]

    data = path.read_bytes()

    if data[:4] == b"CCHR":
        kinds.add("charset")
    if data[:4] == b"CTIL":
        kinds.add("tiles")
    if len(data) in (256 * 8, 2 * 256 * 8):
        kinds.add("charset")
    if len(data) in (224, 1001, 1248, 1253, 1257) and data[:2] == bytes((20, 11)):
        kinds.add("map")
    if len(data) == 256 * 64:
        kinds.add("objecttypes")
    if len(data) == 256 and path.parent.name == "portraits":
        kinds.add("portrait")

    if kinds:
        return sorted(kinds)

    if suffix in {".cchr", ".rom", ".chr"}:
        kinds.add("charset")
    elif suffix in {".ctil", ".til", ".tiles"}:
        kinds.add("tiles")
    elif suffix in {".map", ".cmap"}:
        kinds.add("map")
    elif suffix in {".cobj", ".objects"}:
        kinds.add("objecttypes")

    return sorted(kinds)


def _asset_path(raw_path: str) -> Path:
    rel = unquote(raw_path).lstrip("/")
    path = (ASSETS_DIR / rel).resolve()
    try:
        path.relative_to(ASSETS_DIR.resolve())
    except ValueError as exc:
        raise ValueError("asset path escapes assets directory") from exc
    if path == ASSETS_DIR.resolve():
        raise ValueError("asset path must name a file")
    return path


class AssetEditorHandler(SimpleHTTPRequestHandler):
    server_version = "C64AssetEditor/1.0"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(APP_DIR), **kwargs)

    def end_headers(self) -> None:
        # The editor is a development tool; stale UI assets are never useful.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def _send_json(self, status: HTTPStatus, payload: object) -> None:
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_error_json(self, status: HTTPStatus, message: str) -> None:
        self._send_json(status, {"error": message})

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/api/assets":
            self._list_assets()
            return
        if parsed.path.startswith("/api/assets/"):
            self._read_asset(parsed.path[len("/api/assets/") :])
            return
        super().do_GET()

    def do_PUT(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path.startswith("/api/assets/"):
            self._write_asset(parsed.path[len("/api/assets/") :])
            return
        self._send_error_json(HTTPStatus.NOT_FOUND, "unknown endpoint")

    def _list_assets(self) -> None:
        ASSETS_DIR.mkdir(parents=True, exist_ok=True)
        files = []
        for path in sorted(ASSETS_DIR.rglob("*")):
            if not path.is_file():
                continue
            rel = path.relative_to(ASSETS_DIR)
            if any(part.startswith(".") for part in rel.parts):
                continue
            stat = path.stat()
            files.append(
                {
                    "path": rel.as_posix(),
                    "size": stat.st_size,
                    "mtime": int(stat.st_mtime),
                    "kinds": _asset_kinds(path),
                }
            )
        self._send_json(HTTPStatus.OK, {"files": files})

    def _read_asset(self, raw_path: str) -> None:
        try:
            path = _asset_path(raw_path)
        except ValueError as exc:
            self._send_error_json(HTTPStatus.BAD_REQUEST, str(exc))
            return
        if not path.is_file():
            self._send_error_json(HTTPStatus.NOT_FOUND, "asset not found")
            return

        body = path.read_bytes()
        ctype = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _write_asset(self, raw_path: str) -> None:
        try:
            path = _asset_path(raw_path)
        except ValueError as exc:
            self._send_error_json(HTTPStatus.BAD_REQUEST, str(exc))
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._send_error_json(HTTPStatus.LENGTH_REQUIRED, "missing content length")
            return
        body = self.rfile.read(length)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(body)
        self._send_json(
            HTTPStatus.OK,
            {"path": path.relative_to(ASSETS_DIR).as_posix(), "size": len(body)},
        )


def main() -> None:
    parser = argparse.ArgumentParser(description="Serve the C64 asset editor.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()

    ASSETS_DIR.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer((args.host, args.port), AssetEditorHandler)
    print(f"Serving asset editor at http://{args.host}:{args.port}/")
    print(f"Asset API root: {ASSETS_DIR}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()

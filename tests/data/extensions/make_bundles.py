"""Writes the .gsbundle files that the gs_cli_validate_bundle* tests check: one good bundle and two broken ones."""

import io
import json
import sys
import zipfile
from pathlib import Path

MEMBER = Path(__file__).resolve().parent / "bundle-member"
MEMBER_ID = "gstest.bundle-tint"
MEMBER_VERSION = "1.0.0"
BUILTIN = {"id": "gamescrcpy.slate-theme", "version": "1.0.0", "builtin": True, "enabled": True}
STAMP = (1980, 1, 1, 0, 0, 0)


def add(archive: zipfile.ZipFile, name: str, data: bytes) -> None:
    info = zipfile.ZipInfo(name, date_time=STAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    archive.writestr(info, data)


def member_package(version: str) -> bytes:
    manifest = json.loads((MEMBER / "manifest.json").read_text(encoding="utf-8"))
    manifest["version"] = version
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        add(archive, "manifest.json", json.dumps(manifest, indent=2).encode("utf-8"))
        for shader in sorted((MEMBER / "shaders").iterdir()):
            add(archive, f"shaders/{shader.name}", shader.read_bytes())
    return buffer.getvalue()


def bundle_index() -> bytes:
    bundle = {
        "schemaVersion": 1,
        "name": "CLI test bundle",
        "description": "One packed filter and one built-in theme.",
        "extensions": [{"id": MEMBER_ID, "version": MEMBER_VERSION, "enabled": True}, BUILTIN],
    }
    return json.dumps(bundle, indent=2).encode("utf-8")


def write_bundle(path: Path, files: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in files.items():
            add(archive, name, data)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: make_bundles.py <output-folder>", file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    inner = f"extensions/{MEMBER_ID}-{MEMBER_VERSION}.gsext"
    write_bundle(output / "good.gsbundle", {"bundle.json": bundle_index(), inner: member_package(MEMBER_VERSION)})
    write_bundle(output / "missing-index.gsbundle", {inner: member_package(MEMBER_VERSION)})
    write_bundle(output / "mismatch.gsbundle", {"bundle.json": bundle_index(), inner: member_package("1.0.1")})
    return 0


if __name__ == "__main__":
    sys.exit(main())

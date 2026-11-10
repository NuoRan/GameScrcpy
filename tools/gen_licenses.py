#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

DESCRIPTION = (
    "Collects the license of every third-party component GameScrcpy ships (REL-002). The table in "
    "third_party/README.md gives each component's version and license, its license files give the text, and "
    "a BUILDINFO.md next to it must name the same version. Writes licenses.json for the About page and a "
    "licenses/ folder with one text file per component. Fails when a directory under third_party/ has no license."
)

SEPARATOR = "\n\n" + "=" * 72 + "\n\n"


@dataclass(frozen=True)
class Component:
    name: str
    directory: str
    license: str
    files: tuple[str, ...]


@dataclass(frozen=True)
class Extra:
    name: str
    license: str
    file: str


COMPONENTS = (
    Component("doctest", "doctest", "MIT", ("LICENSE.txt",)),
    Component("tl::expected", "tl-expected", "CC0-1.0", ("COPYING",)),
    Component("nlohmann/json", "nlohmann-json", "MIT", ("LICENSE.MIT",)),
    Component("QWindowKit", "qwindowkit", "Apache-2.0", ("LICENSE",)),
    Component("qmsetup", "qwindowkit/qmsetup", "MIT", ("LICENSE",)),
    Component("syscmdline", "qwindowkit/qmsetup/src/syscmdline", "MIT", ("LICENSE",)),
    Component("FFmpeg", "ffmpeg", "LGPL-2.1-or-later",
              ("licenses/FFmpeg-LICENSE.md", "licenses/FFmpeg-COPYING.LGPLv2.1")),
    Component("dav1d", "ffmpeg", "BSD-2-Clause", ("licenses/dav1d-COPYING",)),
    Component("Lucide", "lucide", "ISC AND MIT", ("LICENSE",)),
    Component("QR Code generator", "qrcodegen", "MIT", ("LICENSE",)),
    Component("miniz", "miniz", "MIT", ("LICENSE",)),
    Component("QuickJS-ng", "quickjs-ng", "MIT", ("LICENSE",)),
    Component("SDL3", "sdl3", "Zlib", ("licenses/SDL3-LICENSE.txt",)),
    Component("libusb", "libusb", "LGPL-2.1-or-later", ("licenses/libusb-COPYING", "licenses/libusb-AUTHORS")),
    Component("TypeScript", "typescript", "Apache-2.0", ("LICENSE", "NOTICE.txt")),
)

PLATFORM_TOOLS = "Android SDK Platform-Tools (adb)"

EXTRAS = (
    Extra("Qt", "LGPL-3.0-only", "Qt-LICENSE.txt"),
    Extra("Microsoft Visual C++ runtime", "Microsoft Software License Terms", "MSVC-runtime.txt"),
    Extra(PLATFORM_TOOLS, "Apache-2.0 AND others", "Android-SDK-Platform-Tools-NOTICE.txt"),
)


class LicenseError(Exception):
    pass


def readme_rows(readme: Path) -> list[list[str]]:
    rows = []
    for line in readme.read_text(encoding="utf-8").splitlines():
        if line.startswith("|") and not line.startswith("|---"):
            rows.append([cell.strip() for cell in line.strip().strip("|").split("|")])
    return rows


def row_of(rows: list[list[str]], name: str) -> list[str]:
    for cells in rows:
        label = cells[0].lstrip("└").strip()
        if label.startswith(name) and (len(label) == len(name) or not label[len(name)].isalnum()):
            return cells
    raise LicenseError(f"third_party/README.md has no row for {name}")


def version_of(cell: str, name: str) -> str:
    dotted = re.search(r"v?(\d+(?:\.\d+)+)", cell)
    if dotted:
        return dotted.group(1)
    commit = re.search(r"`([0-9a-f]{7,40})`", cell)
    if commit:
        return commit.group(1)[:12]
    raise LicenseError(f"third_party/README.md gives no version for {name}: {cell}")


def license_text(paths: list[Path]) -> str:
    texts = []
    for path in paths:
        text = path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n").strip()
        if not text:
            raise LicenseError(f"{path} is empty")
        texts.append(text)
    return SEPARATOR.join(texts) + "\n"


def file_name(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9.-]+", "-", name).strip("-") + ".txt"


def pinned_version(pin: Path) -> str:
    version = json.loads(pin.read_text(encoding="utf-8")).get("version", "")
    if not isinstance(version, str) or not re.fullmatch(r"\d+(?:\.\d+)+", version):
        raise LicenseError(f"{pin} gives no version")
    return version


def collect(third_party: Path, extras: Path, qt_version: str, msvc_version: str, platform_tools: Path,
            inputs: list[Path]) -> list[dict[str, str]]:
    inputs += [third_party, third_party / "README.md"]
    rows = readme_rows(third_party / "README.md")
    covered = {component.directory.split("/")[0] for component in COMPONENTS}
    for directory in sorted(p.name for p in third_party.iterdir() if p.is_dir()):
        if directory not in covered:
            raise LicenseError(f"third_party/{directory} has no license: add it to tools/gen_licenses.py "
                               "and third_party/README.md")
    entries = []
    for component in COMPONENTS:
        root = third_party / component.directory
        files = [root / name for name in component.files]
        missing = [str(path) for path in files if not path.is_file()]
        if not root.is_dir() or missing:
            raise LicenseError(f"{component.name}: license file missing: {', '.join(missing) or root}")
        cells = row_of(rows, component.name)
        if len(cells) < 3:
            raise LicenseError(f"third_party/README.md: the row for {component.name} is incomplete")
        version = version_of(cells[1], component.name)
        if not cells[2].startswith(component.license.split()[0]):
            raise LicenseError(f"{component.name}: third_party/README.md says {cells[2]}, "
                               f"tools/gen_licenses.py says {component.license}")
        buildinfo = third_party / component.directory.split("/")[0] / "BUILDINFO.md"
        if buildinfo.is_file():
            inputs.append(buildinfo)
            if version not in buildinfo.read_text(encoding="utf-8"):
                raise LicenseError(f"{component.name}: {buildinfo} does not name version {version}")
        inputs += files
        entries.append({
            "name": component.name,
            "version": version,
            "license": component.license,
            "directory": component.directory,
            "file": file_name(component.name),
            "text": license_text(files),
        })
    inputs.append(platform_tools)
    versions = {"Qt": qt_version, "Microsoft Visual C++ runtime": msvc_version,
                PLATFORM_TOOLS: pinned_version(platform_tools)}
    for extra in EXTRAS:
        path = extras / extra.file
        if not path.is_file():
            raise LicenseError(f"{extra.name}: license file missing: {path}")
        inputs.append(path)
        entries.append({
            "name": extra.name,
            "version": versions[extra.name],
            "license": extra.license,
            "directory": "",
            "file": file_name(extra.name),
            "text": license_text([path]),
        })
    return entries


def toolset_of(msvc_version: str) -> str:
    number = int(msvc_version)
    return f"14.{number - 1900}" if number >= 1900 else msvc_version


def write_folder(folder: Path, entries: list[dict[str, str]]) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    wanted = {entry["file"] for entry in entries} | {"README.txt"}
    for stale in folder.glob("*.txt"):
        if stale.name not in wanted:
            stale.unlink()
    for entry in entries:
        (folder / entry["file"]).write_text(entry["text"], encoding="utf-8", newline="\n")
    lines = ["Open-source components in GameScrcpy and their licenses.", ""]
    lines += [f"{entry['name']} {entry['version']} ({entry['license']}): {entry['file']}" for entry in entries]
    (folder / "README.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def write_depfile(depfile: Path, target: Path, inputs: list[Path]) -> None:
    def escaped(path: Path) -> str:
        return path.resolve().as_posix().replace(" ", "\\ ")

    unique = sorted({escaped(path) for path in inputs})
    depfile.parent.mkdir(parents=True, exist_ok=True)
    depfile.write_text(f"{escaped(target)}: " + " \\\n  ".join(unique) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=DESCRIPTION)
    parser.add_argument("--third-party", type=Path, required=True)
    parser.add_argument("--extras", type=Path, required=True)
    parser.add_argument("--qt-version", required=True)
    parser.add_argument("--msvc-version", required=True)
    parser.add_argument("--platform-tools", type=Path, required=True)
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--folder", type=Path, required=True)
    parser.add_argument("--depfile", type=Path)
    args = parser.parse_args()
    inputs: list[Path] = []
    try:
        entries = collect(args.third_party, args.extras, args.qt_version, toolset_of(args.msvc_version),
                          args.platform_tools, inputs)
    except (LicenseError, OSError, ValueError) as error:
        print(f"gen_licenses: {error}", file=sys.stderr)
        return 1
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(entries, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    write_folder(args.folder, entries)
    if args.depfile:
        write_depfile(args.depfile, args.json, inputs)
    print(f"gen_licenses: {len(entries)} components")
    return 0


if __name__ == "__main__":
    sys.exit(main())

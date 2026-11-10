"""A stand-in adb server for showing device states and wireless pairing in the app.

    python tools/fake_adb_server.py --port 5039 --device "R58M123ABC unauthorized usb:1-1 transport_id:1"
    python tools/fake_adb_server.py --port 5039 --mdns-pairing adb-R58M123ABC-AbCdEf@192.168.1.23:37123 --pair ok
    $env:ANDROID_ADB_SERVER_PORT = 5039; build/msvc-debug/bin/GameScrcpy.exe

It answers host:version, host:devices-l and host:track-devices-l with the given devices, in the
long format of adb server 41. With --mdns-pairing it lists phones that show their pairing dialog
under host:mdns:services, adding those that --mdns-file holds when asked (NAME@ADDRESS per line;
write the name of a QR code the app shows there, as a phone that scanned it would advertise).
With --pair it answers host:pair: "ok" as a successful pairing is taken to be answered (not seen
with a phone yet), "wrong-code" as adb's source says a rejected code is (OKAY, then FAIL and the
reason, as adb 36.0.2 answers failures). With --identify, devices in the "device" state answer the
app's identification (host:transport, then the getprop command over shell v2) with their serial,
and a model and manufacturer taken from model: and manufacturer: in their description; serialno:
gives the phone's own serial number instead, so a USB and a wireless entry can be the same phone. With
--files DIR (needs --identify), those devices also answer the sync service (LIST/LIS2, STAT/STA2,
RECV/RCV2, SEND) and the rm -rf, mkdir -p and mv shell commands of the Files page, with DIR standing for
/sdcard. Every other request is refused, so nothing reaches a real device.

    python tools/fake_adb_server.py --port 5051 --identify --files D:/tmp/phone --device "R58M123ABC device model:Pixel_8"
"""

import argparse
import hashlib
import os
import pathlib
import shlex
import shutil
import socket
import socketserver

SERVER_VERSION = 41


def prefixed(text: str) -> bytes:
    data = text.encode()
    return f"{len(data):04x}".encode() + data


def read_exactly(connection: socket.socket, size: int) -> bytes | None:
    data = b""
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            return None
        data += chunk
    return data


def pairing_services(phones: list[str]) -> str:
    lines = []
    for phone in phones:
        name, _, address = phone.strip().partition("@")
        if name and address:
            lines.append(f"{name}\t_adb-tls-pairing._tcp\t{address}\n")
    return "".join(lines)


def shell_packet(kind: int, data: bytes) -> bytes:
    return bytes([kind]) + len(data).to_bytes(4, "little") + data


def identity(serial: str, details: dict[str, str]) -> bytes:
    serialno = details.get("serialno", serial)
    android_id = hashlib.sha256(serialno.encode()).hexdigest()[:16]
    lines = [f"serialno={serialno}", f"bootserialno={serialno}", f"androidid={android_id}", "release=15",
             f"manufacturer={details.get('manufacturer', 'Google').replace('_', ' ')}",
             f"model={details.get('model', 'Pixel 8').replace('_', ' ')}", "marketname=", "booted=1"]
    return shell_packet(1, ("\n".join(lines) + "\n").encode()) + shell_packet(3, bytes([0]))


FILE_FEATURES = "shell_v2,cmd,stat_v2,ls_v2,sendrecv_v2"
SYNC_CHUNK = 64 * 1024


def le32(value: int) -> bytes:
    return (value & 0xFFFFFFFF).to_bytes(4, "little")


def le64(value: int) -> bytes:
    return (value & 0xFFFFFFFFFFFFFFFF).to_bytes(8, "little")


def phone_path(root: pathlib.Path, path: str) -> pathlib.Path | None:
    if path != "/sdcard" and not path.startswith("/sdcard/"):
        return None
    base = root.resolve()
    relative = path[len("/sdcard"):].strip("/")
    target = (base / relative).resolve() if relative else base
    if target != base and base not in target.parents:
        return None
    return target


def stat_v2(ident: bytes, target: pathlib.Path | None) -> bytes:
    if target is None or not target.exists():
        return ident + le32(2) + bytes(64)
    info = target.stat()
    mtime = int(info.st_mtime)
    return (ident + le32(0) + le64(0) + le64(0) + le32(info.st_mode) + le32(1) + le32(0) + le32(0) +
            le64(info.st_size) + le64(mtime) + le64(mtime) + le64(mtime))


def list_folder(connection: socket.socket, target: pathlib.Path | None, v2: bool) -> None:
    reply = b""
    if target is not None and target.is_dir():
        for child in sorted(target.iterdir()):
            info = child.stat()
            name = child.name.encode()
            mtime = int(info.st_mtime)
            if v2:
                reply += (b"DNT2" + le32(0) + le64(0) + le64(0) + le32(info.st_mode) + le32(1) + le32(0) + le32(0) +
                          le64(info.st_size) + le64(mtime) + le64(mtime) + le64(mtime))
            else:
                reply += b"DENT" + le32(info.st_mode) + le32(info.st_size) + le32(mtime)
            reply += le32(len(name)) + name
    connection.sendall(reply + b"DONE" + bytes(72 if v2 else 16))


def sync_fail(connection: socket.socket, reason: str) -> None:
    data = reason.encode()
    connection.sendall(b"FAIL" + le32(len(data)) + data)


def send_file(connection: socket.socket, target: pathlib.Path | None) -> None:
    if target is None or not target.exists():
        sync_fail(connection, "No such file or directory")
        return
    if target.is_dir():
        sync_fail(connection, "read failed: Is a directory")
        return
    with target.open("rb") as source:
        while chunk := source.read(SYNC_CHUNK):
            connection.sendall(b"DATA" + le32(len(chunk)) + chunk)
    connection.sendall(b"DONE" + le32(0))


def receive_file(connection: socket.socket, root: pathlib.Path, path_and_mode: str) -> bool:
    path = path_and_mode.rpartition(",")[0] or path_and_mode
    content = b""
    while True:
        header = read_exactly(connection, 8)
        if header is None:
            return False
        ident, value = header[:4], int.from_bytes(header[4:], "little")
        if ident == b"DONE":
            break
        if ident != b"DATA":
            return False
        chunk = read_exactly(connection, value)
        if chunk is None:
            return False
        content += chunk
    target = phone_path(root, path)
    if target is None:
        sync_fail(connection, "couldn't create file: Permission denied")
        return True
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(content)
    os.utime(target, (value, value))
    print(f"received {path} ({len(content)} bytes)", flush=True)
    connection.sendall(b"OKAY" + le32(0))
    return True


def serve_sync(connection: socket.socket, root: pathlib.Path) -> None:
    while True:
        header = read_exactly(connection, 8)
        if header is None:
            return
        ident, length = header[:4], int.from_bytes(header[4:], "little")
        if ident == b"QUIT":
            return
        data = read_exactly(connection, length) if length else b""
        if data is None:
            return
        path = data.decode("utf-8", "replace")
        target = phone_path(root, path)
        if ident in (b"LIST", b"LIS2"):
            list_folder(connection, target, ident == b"LIS2")
        elif ident == b"STAT":
            if target is None or not target.exists():
                connection.sendall(b"STAT" + bytes(12))
            else:
                info = target.stat()
                connection.sendall(b"STAT" + le32(info.st_mode) + le32(info.st_size) + le32(int(info.st_mtime)))
        elif ident in (b"STA2", b"LST2"):
            connection.sendall(stat_v2(ident, target))
        elif ident in (b"RECV", b"RCV2"):
            if ident == b"RCV2" and read_exactly(connection, 8) is None:
                return
            send_file(connection, target)
        elif ident == b"SEND":
            if not receive_file(connection, root, path):
                return
        else:
            sync_fail(connection, "unknown command")
            return


def run_files_shell(root: pathlib.Path, command: str) -> tuple[int, bytes]:
    try:
        words = shlex.split(command)
    except ValueError:
        return 127, b"bad quoting\n"
    print(f"shell {words}", flush=True)
    if words[:2] == ["rm", "-rf"]:
        for path in words[2:]:
            target = phone_path(root, path)
            if target is None or target == root.resolve():
                continue
            if target.is_dir():
                shutil.rmtree(target, ignore_errors=True)
            elif target.exists():
                target.unlink()
        return 0, b""
    if words[:2] == ["mkdir", "-p"]:
        for path in words[2:]:
            target = phone_path(root, path)
            if target is not None:
                target.mkdir(parents=True, exist_ok=True)
        return 0, b""
    if words[:1] == ["if"] and "mv" in words and len(words) >= words.index("mv") + 3:
        at = words.index("mv")
        source, target = phone_path(root, words[at + 1]), phone_path(root, words[at + 2])
        if source is None or target is None:
            return 1, b"mv: Permission denied\n"
        if target.exists():
            return 1, b"already exists\n"
        if not source.exists():
            return 1, b"mv: No such file or directory\n"
        source.rename(target)
        return 0, b""
    return 127, f"not supported by the fake adb server: {command}\n".encode()


def make_handler(device_list: str, phones: list[str], mdns_file: str | None, pair: str | None,
                 identifiable: dict[str, dict[str, str]], files: pathlib.Path | None = None):
    def mdns() -> str:
        listed = list(phones)
        if mdns_file and pathlib.Path(mdns_file).exists():
            listed += pathlib.Path(mdns_file).read_text(encoding="utf-8").splitlines()
        return pairing_services(listed)

    class Handler(socketserver.BaseRequestHandler):
        def handle(self):
            length = read_exactly(self.request, 4)
            request = read_exactly(self.request, int(length, 16)) if length else None
            if request is None:
                return
            request = request.decode()
            if request == "host:version":
                self.request.sendall(b"OKAY" + prefixed(f"{SERVER_VERSION:04x}"))
            elif request == "host:devices-l":
                self.request.sendall(b"OKAY" + prefixed(device_list))
            elif request == "host:track-devices-l":
                self.request.sendall(b"OKAY" + prefixed(device_list))
                while self.request.recv(1024):
                    pass
            elif request == "host:mdns:services" and (phones or mdns_file):
                self.request.sendall(b"OKAY" + prefixed(mdns()))
            elif request.startswith("host:pair:") and pair:
                address = request.removeprefix("host:pair:").partition(":")[2]
                print(f"pairing requested with {address}", flush=True)
                if pair == "ok":
                    self.request.sendall(
                        b"OKAY" + prefixed(f"Successfully paired to {address} [guid=adb-R58M123ABC-AbCdEf]"))
                else:
                    self.request.sendall(b"OKAYFAIL" + prefixed("Failed: Wrong password or connection was dropped."))
            elif (files and request.startswith("host-serial:") and request.endswith(":features") and
                  request.removeprefix("host-serial:").removesuffix(":features") in identifiable):
                self.request.sendall(b"OKAY" + prefixed(FILE_FEATURES))
            elif request.startswith("host:transport:") and request.removeprefix("host:transport:") in identifiable:
                serial = request.removeprefix("host:transport:")
                self.request.sendall(b"OKAY")
                length = read_exactly(self.request, 4)
                service = read_exactly(self.request, int(length, 16)).decode() if length else ""
                if service.startswith("shell,v2,raw:") and "getprop ro.serialno" in service:
                    print(f"identified {serial}", flush=True)
                    self.request.sendall(b"OKAY" + identity(serial, identifiable[serial]))
                elif files and service == "sync:":
                    self.request.sendall(b"OKAY")
                    serve_sync(self.request, files)
                elif files and service.startswith("shell,v2,raw:"):
                    status, error = run_files_shell(files, service.removeprefix("shell,v2,raw:"))
                    reply = b"OKAY" + (shell_packet(2, error) if error else b"") + shell_packet(3, bytes([status]))
                    self.request.sendall(reply)
                else:
                    self.request.sendall(b"FAIL" + prefixed(f"not supported by the fake adb server: {service}"))
            else:
                self.request.sendall(b"FAIL" + prefixed(f"not supported by the fake adb server: {request}"))

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--device", action="append", default=[], help='"serial state [key:value ...]"')
    parser.add_argument("--mdns-pairing", action="append", default=[], metavar="NAME@ADDRESS",
                        help="a phone showing its pairing dialog, such as adb-R58M123ABC-AbCdEf@192.168.1.23:37123")
    parser.add_argument("--mdns-file", help="more such phones, NAME@ADDRESS per line, read at every request")
    parser.add_argument("--pair", choices=["ok", "wrong-code"], help="how pairing requests are answered")
    parser.add_argument("--identify", action="store_true", help="answer the identification of devices in the device state")
    parser.add_argument("--files", type=pathlib.Path,
                        help="a folder that identified devices show as /sdcard to the sync service and file commands")
    args = parser.parse_args()
    if args.files:
        args.files.mkdir(parents=True, exist_ok=True)

    lines = []
    identifiable = {}
    for device in args.device:
        serial, state, *details = device.split()
        lines.append(" ".join([f"{serial:<22}", state, *details]) + "\n")
        if args.identify and state == "device":
            identifiable[serial] = dict(detail.partition(":")[::2] for detail in details)
    socketserver.ThreadingTCPServer.daemon_threads = True
    handler = make_handler("".join(lines), args.mdns_pairing, args.mdns_file, args.pair, identifiable, args.files)
    with socketserver.ThreadingTCPServer(("127.0.0.1", args.port), handler) as server:
        print(f"fake adb server on 127.0.0.1:{args.port} with {len(lines)} device(s)", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()

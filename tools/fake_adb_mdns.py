"""Advertises a phone's wireless debugging over mDNS, so that the adb server discovers it without one.

    python tools/fake_adb_mdns.py --type pairing --name adb-FAKE0001-AbCdEf --port 37123 --seconds 60

A phone showing "Pair device with pairing code" advertises _adb-tls-pairing._tcp; one with
wireless debugging on advertises _adb-tls-connect._tcp. The adb server lists what it discovers
under host:mdns:services. Nothing answers on the advertised port, so pairing with or connecting
to it fails: enough to see how the app lists such phones and reports the failure.

It announces every second and answers queries for its names. On leaving (after --seconds, or on
Ctrl+C) it sends a goodbye so the adb server forgets the service; if killed instead, the records
expire after two minutes.
"""

import argparse
import select
import socket
import struct
import time

GROUP = "224.0.0.251"
PORT = 5353
SERVICE_TYPES = {"pairing": "_adb-tls-pairing._tcp.local", "connect": "_adb-tls-connect._tcp.local"}
TYPE_A, TYPE_PTR, TYPE_TXT, TYPE_SRV = 1, 12, 16, 33
CLASS_IN = 1
CACHE_FLUSH = 0x8000
TTL = 120


def encode_name(name: str) -> bytes:
    data = b""
    for label in name.split("."):
        encoded = label.encode()
        data += bytes([len(encoded)]) + encoded
    return data + b"\x00"


def read_name(packet: bytes, offset: int) -> tuple[str, int]:
    labels = []
    end = None
    for _ in range(128):
        length = packet[offset]
        if length & 0xC0 == 0xC0:
            if end is None:
                end = offset + 2
            offset = ((length & 0x3F) << 8) | packet[offset + 1]
            continue
        offset += 1
        if length == 0:
            break
        labels.append(packet[offset : offset + length].decode(errors="replace"))
        offset += length
    return ".".join(labels), end if end is not None else offset


def questions(packet: bytes) -> list[str]:
    try:
        _, flags, count = struct.unpack_from("!HHH", packet)
        if flags & 0x8000:
            return []
        names = []
        offset = 12
        for _ in range(count):
            name, offset = read_name(packet, offset)
            offset += 4
            names.append(name.lower())
        return names
    except (IndexError, struct.error):
        return []


def record(name: str, rtype: int, rclass: int, ttl: int, rdata: bytes) -> bytes:
    return encode_name(name) + struct.pack("!HHIH", rtype, rclass, ttl, len(rdata)) + rdata


def announcement(service: str, instance: str, host: str, address: str, port: int, ttl: int) -> bytes:
    records = [
        record(service, TYPE_PTR, CLASS_IN, ttl, encode_name(instance)),
        record(instance, TYPE_SRV, CLASS_IN | CACHE_FLUSH, ttl, struct.pack("!HHH", 0, 0, port) + encode_name(host)),
        record(instance, TYPE_TXT, CLASS_IN | CACHE_FLUSH, ttl, b"\x00"),
        record(host, TYPE_A, CLASS_IN | CACHE_FLUSH, ttl, socket.inet_aton(address)),
    ]
    return struct.pack("!HHHHHH", 0, 0x8400, 0, len(records), 0, 0) + b"".join(records)


def local_address() -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.connect(("192.0.2.1", 9))
        return probe.getsockname()[0]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--type", choices=sorted(SERVICE_TYPES), default="pairing")
    parser.add_argument("--name", default="adb-FAKE0001-AbCdEf", help="the service's instance name")
    parser.add_argument("--port", type=int, default=37123)
    parser.add_argument("--address", help="the IPv4 address to advertise; by default this machine's")
    parser.add_argument("--seconds", type=float, help="leave after this long; by default on Ctrl+C")
    args = parser.parse_args()

    address = args.address or local_address()
    service = SERVICE_TYPES[args.type]
    instance = f"{args.name}.{service}"
    host = f"{args.name}.local"
    ours = {service.lower(), instance.lower(), host.lower()}

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", PORT))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton(GROUP) + socket.inet_aton(address))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(address))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)

    hello = announcement(service, instance, host, address, args.port, TTL)
    print(f"advertising {instance} at {address}:{args.port}", flush=True)
    leave_at = time.monotonic() + args.seconds if args.seconds is not None else None
    next_announcement = 0.0
    try:
        while leave_at is None or time.monotonic() < leave_at:
            if time.monotonic() >= next_announcement:
                sock.sendto(hello, (GROUP, PORT))
                next_announcement = time.monotonic() + 1.0
            wait = next_announcement - time.monotonic()
            if leave_at is not None:
                wait = min(wait, leave_at - time.monotonic())
            ready, _, _ = select.select([sock], [], [], max(0.0, wait))
            if ready:
                packet, _ = sock.recvfrom(9000)
                if ours.intersection(questions(packet)):
                    sock.sendto(hello, (GROUP, PORT))
    except KeyboardInterrupt:
        pass
    finally:
        sock.sendto(announcement(service, instance, host, address, args.port, 0), (GROUP, PORT))
        sock.close()
        print("goodbye sent", flush=True)


if __name__ == "__main__":
    main()

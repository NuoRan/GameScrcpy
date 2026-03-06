#!/usr/bin/env python3
"""Map GameScrcpy.exe offsets to code bytes and try to identify functions."""
import struct

exe_path = r"D:\QtScrcpy2\output\x64\Release\GameScrcpy.exe"

with open(exe_path, 'rb') as f:
    f.seek(0x3C)
    pe = struct.unpack('<I', f.read(4))[0]
    f.seek(pe + 6)
    nsec = struct.unpack('<H', f.read(2))[0]
    f.seek(pe + 20)
    ohsz = struct.unpack('<H', f.read(2))[0]
    st = pe + 24 + ohsz

    text_rva = text_raw = 0
    rdata_rva = rdata_raw = 0
    for i in range(nsec):
        f.seek(st + i * 40)
        nm = f.read(8).rstrip(b'\x00')
        vs, vr, rs, ro = struct.unpack('<IIII', f.read(16))
        if nm == b'.text':
            text_rva, text_raw = vr, ro
        if nm == b'.rdata':
            rdata_rva, rdata_raw = vr, ro

    def rva_to_file(rva):
        return text_raw + (rva - text_rva)

    offsets = [0xb22c3, 0x8b57f, 0x4d7dd]
    for off in offsets:
        fo = rva_to_file(off)
        # Read backward to find function start (CC padding before function)
        f.seek(fo - 512)
        chunk = f.read(512)
        func_start = None
        for j in range(len(chunk) - 1, 0, -1):
            if chunk[j] == 0xCC and chunk[j-1] == 0xCC:
                # Find first non-CC byte after the padding
                k = j + 1
                while k < len(chunk) and chunk[k] == 0xCC:
                    k += 1
                if k < len(chunk):
                    func_start = off - (len(chunk) - k)
                break

        print(f"\n=== GameScrcpy.exe+{off:#x} ===")
        if func_start:
            print(f"  Function starts at +{func_start:#x} (offset into func: {off-func_start} bytes)")

        # Read bytes around the offset
        f.seek(fo - 32)
        data = f.read(96)
        print(f"  Bytes at offset:")
        for row in range(0, 96, 16):
            addr = off - 32 + row
            hexb = ' '.join(f'{data[row+k]:02x}' for k in range(min(16, len(data)-row)))
            marker = " <--- HERE" if 32 <= row < 48 else ""
            print(f"    +{addr:#08x}: {hexb}{marker}")

    # Try to find strings near these offsets to identify the function
    # Look for references to string constants
    print("\n=== Searching for string references near crash offsets ===")
    for off in offsets:
        fo = rva_to_file(off)
        f.seek(fo - 256)
        chunk = f.read(512)

        # Look for LEA patterns: 48 8d 0d XX XX XX XX (lea rcx, [rip+XX])
        # or 48 8d 15 XX XX XX XX (lea rdx, [rip+XX])
        refs = []
        for j in range(len(chunk) - 7):
            if chunk[j] == 0x48 and chunk[j+1] == 0x8d and chunk[j+2] in (0x05, 0x0d, 0x15, 0x25, 0x2d, 0x35, 0x3d):
                disp = struct.unpack_from('<i', chunk, j+3)[0]
                rip_val = off - 256 + j + 7  # RIP after instruction
                target_rva = rip_val + disp
                refs.append((j, target_rva))

        if refs:
            print(f"\n  +{off:#x} nearby LEA references:")
            for j, target in refs:
                # Try to read string at target RVA (in .rdata section)
                if rdata_rva <= target < rdata_rva + 0x200000:
                    foff = rdata_raw + (target - rdata_rva)
                    f.seek(foff)
                    raw = f.read(128)
                    # Try ASCII
                    ascii_end = raw.find(b'\x00')
                    if ascii_end > 0 and all(32 <= b < 127 for b in raw[:ascii_end]):
                        print(f"    [{off-256+j:#x}] -> \"{raw[:ascii_end].decode('ascii')}\"")
                    else:
                        # Try UTF-16
                        try:
                            s = raw[:64].decode('utf-16-le').split('\x00')[0]
                            if len(s) > 2 and s.isprintable():
                                print(f"    [{off-256+j:#x}] -> L\"{s}\"")
                        except:
                            pass

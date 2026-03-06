#!/usr/bin/env python3
"""Parse minidump to extract crash info and module details."""
import sys
import struct
import os

def parse_minidump(path):
    with open(path, 'rb') as f:
        # Read header
        sig, ver, num_streams, stream_dir_rva = struct.unpack_from('<4sIII', f.read(32))
        print(f"Signature: {sig}, Version: {ver:#x}, Streams: {num_streams}")

        f.seek(stream_dir_rva)
        streams = {}
        for i in range(num_streams):
            stream_type, data_size, rva = struct.unpack('<III', f.read(12))
            streams[stream_type] = (rva, data_size)

        # Stream types
        THREAD_LIST = 3
        MODULE_LIST = 4
        EXCEPTION = 6
        SYSTEM_INFO = 7

        # Parse exception stream (type 6)
        if EXCEPTION in streams:
            rva, size = streams[EXCEPTION]
            f.seek(rva)
            thread_id = struct.unpack('<I', f.read(4))[0]
            f.read(4)  # alignment
            # MINIDUMP_EXCEPTION
            exc_code, exc_flags, exc_record, exc_address = struct.unpack('<IIQQ', f.read(24))
            num_params, = struct.unpack('<I', f.read(4))
            f.read(4)  # unused
            params = []
            for j in range(min(num_params, 15)):
                p, = struct.unpack('<Q', f.read(8))
                params.append(p)

            print(f"\n=== EXCEPTION ===")
            print(f"Thread ID: {thread_id:#x}")
            print(f"Exception Code: {exc_code:#010x}")
            print(f"Exception Address: {exc_address:#018x}")
            if exc_code == 0xC0000005 and num_params >= 2:
                rw = "READ" if params[0] == 0 else "WRITE" if params[0] == 1 else "EXEC"
                print(f"ACCESS_VIOLATION: {rw} at {params[1]:#018x}")
            elif exc_code == 0xE0000002:
                print(f"** FAKE EXCEPTION (from crash handler) **")

            # Thread context follows - read it
            ctx_rva, ctx_size = struct.unpack('<II', f.read(8))
            if ctx_rva > 0 and ctx_size >= 1232:
                f.seek(ctx_rva)
                ctx_data = f.read(ctx_size)
                # AMD64 CONTEXT: flags at offset 48, rip at offset 248
                if len(ctx_data) >= 256:
                    ctx_flags = struct.unpack_from('<I', ctx_data, 48)[0]
                    rax = struct.unpack_from('<Q', ctx_data, 120)[0]
                    rcx = struct.unpack_from('<Q', ctx_data, 128)[0]
                    rdx = struct.unpack_from('<Q', ctx_data, 136)[0]
                    rbx = struct.unpack_from('<Q', ctx_data, 144)[0]
                    rsp = struct.unpack_from('<Q', ctx_data, 152)[0]
                    rbp = struct.unpack_from('<Q', ctx_data, 160)[0]
                    rsi = struct.unpack_from('<Q', ctx_data, 168)[0]
                    rdi = struct.unpack_from('<Q', ctx_data, 176)[0]
                    r8  = struct.unpack_from('<Q', ctx_data, 184)[0]
                    r9  = struct.unpack_from('<Q', ctx_data, 192)[0]
                    r10 = struct.unpack_from('<Q', ctx_data, 200)[0]
                    r11 = struct.unpack_from('<Q', ctx_data, 208)[0]
                    rip = struct.unpack_from('<Q', ctx_data, 248)[0]

                    print(f"\nContext Registers:")
                    print(f"  RIP = {rip:#018x}")
                    print(f"  RSP = {rsp:#018x}")
                    print(f"  RAX = {rax:#018x}  RBX = {rbx:#018x}")
                    print(f"  RCX = {rcx:#018x}  RDX = {rdx:#018x}")
                    print(f"  RSI = {rsi:#018x}  RDI = {rdi:#018x}")
                    print(f"  RBP = {rbp:#018x}")
                    print(f"  R8  = {r8:#018x}  R9  = {r9:#018x}")
                    print(f"  R10 = {r10:#018x}  R11 = {r11:#018x}")

                    crash_rip = rip
                else:
                    crash_rip = exc_address
            else:
                crash_rip = exc_address
        else:
            print("No exception stream!")
            crash_rip = 0

        # Parse module list (type 4)
        if MODULE_LIST in streams:
            rva, size = streams[MODULE_LIST]
            f.seek(rva)
            num_modules, = struct.unpack('<I', f.read(4))

            modules = []
            for i in range(num_modules):
                mod_data = f.read(108)
                base_addr = struct.unpack_from('<Q', mod_data, 0)[0]
                size_of_image = struct.unpack_from('<I', mod_data, 8)[0]
                name_rva = struct.unpack_from('<I', mod_data, 96)[0]

                # Read module name
                pos = f.tell()
                f.seek(name_rva)
                name_len = struct.unpack('<I', f.read(4))[0]
                name_bytes = f.read(name_len)
                name = name_bytes.decode('utf-16-le', errors='replace').rstrip('\x00')
                f.seek(pos)

                modules.append((base_addr, size_of_image, name))

            # Find crash module
            crash_module = None
            for base, sz, name in modules:
                if base <= crash_rip < base + sz:
                    crash_module = (base, sz, name)
                    break

            print(f"\n=== CRASH MODULE ===")
            if crash_module:
                base, sz, name = crash_module
                offset = crash_rip - base
                print(f"Module: {os.path.basename(name)}")
                print(f"Base: {base:#018x}, Size: {sz:#x}")
                print(f"Crash offset: +{offset:#x}")
            else:
                print(f"No module found for RIP {crash_rip:#018x}")

            # Print all loaded modules near crash
            print(f"\n=== KEY MODULES ===")
            for base, sz, name in sorted(modules, key=lambda x: x[0]):
                bname = os.path.basename(name).lower()
                if any(kw in bname for kw in ['qt6', 'gamescrcpy', 'avcodec', 'avformat', 'avutil', 'd3d11', 'fluent']):
                    end = base + sz
                    marker = " <<<" if base <= crash_rip < end else ""
                    print(f"  {base:#018x} - {end:#018x} ({sz:#08x}) {os.path.basename(name)}{marker}")

        # Parse thread list to show crash thread's stack
        if THREAD_LIST in streams:
            rva, size = streams[THREAD_LIST]
            f.seek(rva)
            num_threads, = struct.unpack('<I', f.read(4))
            print(f"\n=== THREADS ({num_threads}) ===")
            for i in range(num_threads):
                tid = struct.unpack('<I', f.read(4))[0]
                suspend_count, priority_class, priority = struct.unpack('<III', f.read(12))
                teb = struct.unpack('<Q', f.read(8))[0]
                stack_rva, stack_size = struct.unpack('<QI', f.read(12))
                stack_mem_rva, stack_mem_size = struct.unpack('<II', f.read(8))
                ctx_rva, ctx_size = struct.unpack('<II', f.read(8))

                # Mark crash thread
                if EXCEPTION in streams:
                    f_save = f.tell()
                    ex_rva, _ = streams[EXCEPTION]
                    f.seek(ex_rva)
                    ex_tid = struct.unpack('<I', f.read(4))[0]
                    f.seek(f_save)

                    if tid == ex_tid:
                        # Read this thread's context
                        f_save2 = f.tell()
                        f.seek(ctx_rva)
                        ctx_data = f.read(ctx_size)
                        rip = struct.unpack_from('<Q', ctx_data, 248)[0] if len(ctx_data) >= 256 else 0
                        rsp = struct.unpack_from('<Q', ctx_data, 152)[0] if len(ctx_data) >= 160 else 0
                        f.seek(f_save2)

                        print(f"  >>> CRASH Thread {tid:#06x}: RIP={rip:#018x} RSP={rsp:#018x}")

                        # Try to read stack memory for return addresses
                        if stack_mem_rva > 0 and stack_mem_size > 0:
                            f_save3 = f.tell()
                            f.seek(stack_mem_rva)
                            stack_raw = f.read(stack_mem_size)

                            # Scan stack for potential return addresses in loaded modules
                            print(f"  Stack scan (potential return addresses):")
                            mod_ranges = [(b, b+s, n) for b, s, n in modules]
                            found = 0
                            for off in range(0, min(len(stack_raw), 4096), 8):
                                addr = struct.unpack_from('<Q', stack_raw, off)[0]
                                for mb, me, mn in mod_ranges:
                                    if mb <= addr < me:
                                        bname = os.path.basename(mn).lower()
                                        if any(kw in bname for kw in ['qt6', 'gamescrcpy', 'avcodec', 'd3d11']):
                                            stack_addr = rsp + off
                                            print(f"    [{stack_addr:#018x}] {addr:#018x} ({os.path.basename(mn)}+{addr-mb:#x})")
                                            found += 1
                                            if found >= 30:
                                                break
                                if found >= 30:
                                    break
                            f.seek(f_save3)
                    else:
                        pass  # not crash thread

if __name__ == '__main__':
    dump = sys.argv[1] if len(sys.argv) > 1 else r"D:\QtScrcpy2\output\x64\Release\logs\crash_20260305_024802.dmp"
    print(f"Parsing: {dump}")
    print(f"Size: {os.path.getsize(dump)} bytes")
    parse_minidump(dump)

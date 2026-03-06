#!/usr/bin/env python3
"""Parse minidump v2 - faster, more robust."""
import struct, os, sys

def parse(path):
    with open(path, 'rb') as f:
        data = f.read()

    sig, ver, num_streams, stream_dir_rva = struct.unpack_from('<4sIII', data, 0)
    print(f"Streams: {num_streams}")

    streams = {}
    for i in range(num_streams):
        off = stream_dir_rva + i * 12
        st, sz, rva = struct.unpack_from('<III', data, off)
        streams[st] = (rva, sz)

    # Parse exception (type 6)
    if 6 in streams:
        rva, _ = streams[6]
        tid = struct.unpack_from('<I', data, rva)[0]
        exc_code = struct.unpack_from('<I', data, rva + 8)[0]
        exc_addr = struct.unpack_from('<Q', data, rva + 24)[0]
        num_params = struct.unpack_from('<I', data, rva + 32)[0]
        params = [struct.unpack_from('<Q', data, rva + 40 + j*8)[0] for j in range(min(num_params, 15))]

        print(f"\n=== EXCEPTION ===")
        print(f"Thread: {tid:#x}, Code: {exc_code:#010x}, Addr: {exc_addr:#018x}")
        if exc_code == 0xC0000005 and num_params >= 2:
            rw = ["READ", "WRITE", "EXEC"][min(params[0], 2)]
            print(f"ACCESS_VIOLATION: {rw} at {params[1]:#018x}")

        # Thread context: at offset rva + 160 (after MINIDUMP_EXCEPTION = 152 bytes + 8 alignment)
        # Actually: ThreadId(4) + __alignment(4) + MINIDUMP_EXCEPTION(152) + ThreadContext(MINIDUMP_LOCATION_DESCRIPTOR=8)
        ctx_off = rva + 4 + 4 + 152  # = rva + 160
        # MINIDUMP_LOCATION_DESCRIPTOR: DataSize first, then Rva
        ctx_size, ctx_rva = struct.unpack_from('<II', data, ctx_off)
        print(f"  Context: rva={ctx_rva:#x}, size={ctx_size:#x}")

        if ctx_rva > 0 and ctx_size >= 1232:
            ctx = data[ctx_rva:ctx_rva + ctx_size]
            regs = {}
            for name, off in [('RAX',120),('RCX',128),('RDX',136),('RBX',144),('RSP',152),('RBP',160),('RSI',168),('RDI',176),('R8',184),('R9',192),('R10',200),('R11',208),('RIP',248)]:
                regs[name] = struct.unpack_from('<Q', ctx, off)[0]

            print(f"\nRegisters:")
            print(f"  RIP={regs['RIP']:#018x}  RSP={regs['RSP']:#018x}")
            print(f"  RAX={regs['RAX']:#018x}  RBX={regs['RBX']:#018x}")
            print(f"  RCX={regs['RCX']:#018x}  RDX={regs['RDX']:#018x}")
            print(f"  RSI={regs['RSI']:#018x}  RDI={regs['RDI']:#018x}")
            print(f"  R8 ={regs['R8']:#018x}  R9 ={regs['R9']:#018x}")
            crash_rip = regs['RIP']
            crash_rsp = regs['RSP']
        else:
            crash_rip = exc_addr
            crash_rsp = 0
    else:
        print("No exception stream")
        return

    # Parse modules (type 4)
    modules = []
    if 4 in streams:
        rva, _ = streams[4]
        num_mod = struct.unpack_from('<I', data, rva)[0]
        for i in range(num_mod):
            moff = rva + 4 + i * 108
            base = struct.unpack_from('<Q', data, moff)[0]
            sz = struct.unpack_from('<I', data, moff + 8)[0]
            name_rva = struct.unpack_from('<I', data, moff + 20)[0]  # ModuleNameRva at offset 20

            # Read name (MINIDUMP_STRING: 4-byte length + UTF-16)
            nlen = struct.unpack_from('<I', data, name_rva)[0]
            nlen = min(nlen, 500)  # safety cap
            name = data[name_rva+4:name_rva+4+nlen].decode('utf-16-le', errors='replace').rstrip('\x00')
            modules.append((base, sz, name))

    # Find crash module
    print(f"\n=== CRASH MODULE ===")
    for base, sz, name in modules:
        if base <= crash_rip < base + sz:
            print(f"  {os.path.basename(name)} base={base:#018x} offset=+{crash_rip-base:#x}")
            break
    else:
        print(f"  Not found for RIP={crash_rip:#018x}")

    # Key modules
    print(f"\n=== KEY MODULES ===")
    for base, sz, name in sorted(modules, key=lambda x: x[0]):
        bn = os.path.basename(name).lower()
        if any(k in bn for k in ['qt6', 'gamescrcpy', 'avcodec', 'd3d11', 'fluent', 'ucrtbase', 'vcruntime']):
            mark = " <<<" if base <= crash_rip < base + sz else ""
            print(f"  {base:#018x}+{sz:#08x} {os.path.basename(name)}{mark}")

    # Stack scan for crash thread
    if crash_rsp > 0:
        # Find which thread's stack memory has the crash RSP
        if 3 in streams:
            rva, _ = streams[3]
            num_threads = struct.unpack_from('<I', data, rva)[0]

            for i in range(num_threads):
                toff = rva + 4 + i * 48
                t_tid = struct.unpack_from('<I', data, toff)[0]
                if t_tid != tid:
                    continue

                # MINIDUMP_THREAD (48 bytes):
                # ThreadId(4) + SuspendCount(4) + PriorityClass(4) + Priority(4) + Teb(8)
                # + Stack.StartOfMemoryRange(8) + Stack.Memory.DataSize(4) + Stack.Memory.Rva(4)
                # + ThreadContext.DataSize(4) + ThreadContext.Rva(4)
                stack_addr = struct.unpack_from('<Q', data, toff + 24)[0]
                stack_mem_size = struct.unpack_from('<I', data, toff + 32)[0]
                stack_mem_rva = struct.unpack_from('<I', data, toff + 36)[0]

                print(f"\n=== STACK SCAN (Thread {tid:#x}) ===")
                print(f"  Stack base: {stack_addr:#018x}, size: {stack_mem_size:#x}")

                # Build module lookup
                mod_ranges = [(b, b+s, n) for b, s, n in modules]

                # Scan from RSP
                start = crash_rsp - stack_addr
                if start < 0: start = 0
                stack_data = data[stack_mem_rva:stack_mem_rva + stack_mem_size]

                count = 0
                for off in range(start, min(len(stack_data), start + 8192), 8):
                    val = struct.unpack_from('<Q', stack_data, off)[0]
                    for mb, me, mn in mod_ranges:
                        if mb <= val < me:
                            bn = os.path.basename(mn).lower()
                            if any(k in bn for k in ['qt6', 'gamescrcpy', 'avcodec', 'd3d11']):
                                addr = stack_addr + off
                                print(f"  [{addr:#018x}] -> {os.path.basename(mn)}+{val-mb:#x}")
                                count += 1
                                if count >= 40:
                                    break
                    if count >= 40:
                        break
                break

if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else r"D:\QtScrcpy2\output\x64\Release\logs\crash_20260305_024802.dmp"
    print(f"Parsing: {os.path.basename(path)} ({os.path.getsize(path)} bytes)")
    parse(path)

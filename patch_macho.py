#!/usr/bin/env python3
"""Add an LC_LOAD_DYLIB command to a thin arm64 Mach-O (like insert_dylib). Usage: patch_macho.py IN OUT [dylib-path]"""
import struct, sys
LC_LOAD_DYLIB = 0xC
inp, outp = sys.argv[1], sys.argv[2]
path = (sys.argv[3] if len(sys.argv) > 3 else "@executable_path/Frameworks/FWMods.dylib").encode()
b = bytearray(open(inp, "rb").read())
magic, cpu, sub, ftype, ncmds, sizeofcmds, flags, res = struct.unpack("<IiiIIIII", b[:32])
assert magic == 0xFEEDFACF, "need a thin 64-bit little-endian Mach-O"
# existing?
off = 32; first_sect = 1 << 62
for _ in range(ncmds):
    cmd, size = struct.unpack("<II", b[off:off+8])
    if cmd in (LC_LOAD_DYLIB, 0x80000018):
        no = struct.unpack("<I", b[off+8:off+12])[0]
        if b[off+no:off+size].split(b"\0")[0] == path:
            sys.exit("already contains " + path.decode())
    if cmd == 0x19:
        nsects = struct.unpack("<I", b[off+64:off+68])[0]; so = off + 72
        for _ in range(nsects):
            o = struct.unpack("<I", b[so+48:so+52])[0]
            if o: first_sect = min(first_sect, o)
            so += 80
    off += size
end = 32 + sizeofcmds
assert off == end
cmdsize = (24 + len(path) + 1 + 7) & ~7
assert end + cmdsize <= first_sect, "not enough header padding"
assert b[end:end+cmdsize] == b"\0" * cmdsize, "header padding is not empty"
cmd = struct.pack("<IIIIII", LC_LOAD_DYLIB, cmdsize, 24, 2, 0x10000, 0x10000) + path + b"\0" * (cmdsize - 24 - len(path))
b[end:end+cmdsize] = cmd
struct.pack_into("<II", b, 16, ncmds + 1, sizeofcmds + cmdsize)
open(outp, "wb").write(b)
print("added", path.decode(), "cmdsize", cmdsize, "ncmds", ncmds, "->", ncmds + 1)
print("NOTE: code signature is now invalid - re-sign the app (codesign / ldid) before installing.")

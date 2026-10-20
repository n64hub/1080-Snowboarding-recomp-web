#!/usr/bin/env python3
"""Reader for the 1080 Snowboarding ROM filesystem and its USO code modules.

ROM layout (big-endian):
  File entry : 0x12345678, index, 1, name[...]          (0x28 bytes)
  File data  : 0x12345678 (or 0), then records until type 11 (End).
               .bin files are raw data with no records.
  Record     : type, size, attr, payload[size]
               attr bit 0x00010000 means the payload is Yay0-compressed.
               The next record starts at the payload end rounded up to 2 bytes.
               Type 8 (Bss) has no payload; size is the bss size.

Record types: 0 Info, 1 Sym, 2 TextReloc, 3 DataReloc, 4 RoDataReloc, 5 Text,
6 Data, 7 RoData, 8 Bss, 9 EntrySym, 11 End, 12 BinaryData, 13 DiskInfo.
"""
import struct
import sys

MAGIC = 0x12345678
TYPE_NAMES = {0: "info", 1: "sym", 2: "text_reloc", 3: "data_reloc",
              4: "rodata_reloc", 5: "text", 6: "data", 7: "rodata", 8: "bss",
              9: "entry_sym", 10: "eof", 11: "end", 12: "bin", 13: "disk_info"}
COMPRESSED = 0x00010000


def u32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def yay0_decompress(src):
    """Plain Yay0 decoder (header: 'Yay0', out size, link offset, chunk offset)."""
    assert src[:4] == b"Yay0", "not a Yay0 block"
    out_size, link, chunk = struct.unpack_from(">III", src, 4)
    out = bytearray()
    mask_pos, mask_bits, mask = 16, 0, 0
    while len(out) < out_size:
        if mask_bits == 0:
            mask = u32(src, mask_pos)
            mask_pos += 4
            mask_bits = 32
        if mask & 0x80000000:
            out.append(src[chunk])
            chunk += 1
        else:
            code = struct.unpack_from(">H", src, link)[0]
            link += 2
            dist = (code & 0xFFF) + 1
            count = code >> 12
            if count == 0:
                count = src[chunk] + 0x12
                chunk += 1
            else:
                count += 2
            for _ in range(count):
                out.append(out[-dist])
        mask <<= 1
        mask_bits -= 1
    return bytes(out)


def list_files(rom):
    """Yield (index, name, entry_offset, data_offset) for every named file."""
    o = 0x1000
    while o < len(rom) - 0x28:
        if (u32(rom, o) == MAGIC and u32(rom, o + 8) == 1
                and u32(rom, o + 4) < 0x1000):
            name = rom[o + 12:o + 0x24].split(b"\0")[0]
            if name and all(32 <= c < 127 for c in name):
                yield u32(rom, o + 4), name.decode(), o, o + 0x28
                o += 0x28
                continue
        o += 2


def read_module(rom, data_off):
    """Return {type_name: bytes} for the file whose data starts at data_off.

    Compressed payloads are decompressed. 'bss' maps to its size (int).
    'attr_<name>' holds each record's attribute word."""
    # The data header word is normally MAGIC, but a few modules store 0.
    assert u32(rom, data_off) in (MAGIC, 0), hex(data_off)
    t = data_off + 4
    secs = {}
    for _ in range(64):
        typ, size, attr = u32(rom, t), u32(rom, t + 4), u32(rom, t + 8)
        name = TYPE_NAMES.get(typ)
        if name is None or name == "end":
            break
        if typ == 8:
            secs["bss"] = size
            t += 12
            continue
        raw = rom[t + 12:t + 12 + size]
        if attr & COMPRESSED and raw[:4] == b"Yay0":
            raw = yay0_decompress(raw)
        secs.setdefault(name, raw)
        secs.setdefault("attr_" + name, attr)
        t = (t + 12 + size + 1) & ~1
    return secs


def decode_relocs(blob):
    """TextReloc/DataReloc entries: (flags, symIdx<<4 | kind, offset) * n.

    kind: 1 = R_MIPS_26 (jal/j), 2 = LO16, 3 = HI16 (text); data relocs use
    other kinds for 32-bit words. Returns [(sym_idx, kind, offset)]."""
    out = []
    for i in range(0, len(blob) - 11, 12):
        f1, off, _ = struct.unpack_from(">III", blob, i)
        out.append((f1 >> 4, f1 & 0xF, off))
    return out


if __name__ == "__main__":
    rom = open(sys.argv[1], "rb").read()
    for idx, name, entry, data in list_files(rom):
        if not name.endswith(".uso"):
            continue
        secs = read_module(rom, data)
        desc = " ".join(f"{k}={v if isinstance(v, int) else len(v):#x}"
                        for k, v in secs.items() if not k.startswith("attr_"))
        print(f"{idx:3d} {name:20s} @{data:#08x} {desc}")

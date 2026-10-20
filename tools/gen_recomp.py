#!/usr/bin/env python3
"""Generate N64Recomp input for 1080 Snowboarding from a user-supplied ROM.

Outputs (in the output directory):
  image.bin   ROM copy with every USO module's code appended, with relocations
              pre-applied against unique placeholder addresses
  syms.toml   N64Recomp symbol file: sections, functions and relocations
  modules.json  Placeholder address layout per module, used by the runtime to
              map placeholder sections onto wherever the game loads them

How the game's code is organised:
  * The kernel is loaded once at 0x80000400 (ROM 0x1000) and never moves.
  * Everything else lives in USO modules that the kernel loads and links at
    runtime to whatever address it allocates. Each module has text, rodata,
    data and bss sections, a symbol table and relocation tables.

The recompiler needs a fixed address for every function, so each module gets
its own placeholder address range, packed upward from PLACEHOLDER_BASE. Code is
relocated against those placeholders here, and every relocated instruction is
also emitted as a relocation so the runtime can point it at the real address.
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import uso  # noqa: E402

KERNEL_ROM = 0x1000
KERNEL_VRAM = 0x80000400
KERNEL_TEXT_SIZE = 0x9E60

PLACEHOLDER_BASE = 0x81000000

# Parts of a module's text that are RSP microcode, not CPU code. bootup.uso
# carries rspboot, the F3DEX 1.23 family and the audio microcode here.
MICROCODE = {"bootup.uso": [(0x23300, 0x31040), (0x46470, 0x473F0)]}

SECTION_ORDER = ["text", "rodata", "data", "bss"]
SECTION_TYPE = {5: "text", 6: "data", 7: "rodata", 8: "bss"}
R_26, R_LO16, R_HI16 = 1, 2, 3
RELOC_NAMES = {R_26: "R_MIPS_26", R_LO16: "R_MIPS_LO16", R_HI16: "R_MIPS_HI16"}


def align(x, a):
    return (x + a - 1) & ~(a - 1)


class Module:
    def __init__(self, index, name, secs, base):
        self.index = index
        self.name = name
        self.secs = secs
        # Placeholder layout: text, rodata, data, bss packed in that order.
        self.base = base
        self.addr = {}
        cur = self.base
        for s in SECTION_ORDER:
            size = self.size(s)
            self.addr[s] = cur
            cur = align(cur + size, 0x10)
        self.end = align(cur, 0x1000)
        self.syms = self._parse_syms()

    def size(self, s):
        if s == "bss":
            return self.secs.get("bss", 0)
        return len(self.secs.get(s, b""))

    def _parse_syms(self):
        """List of (kind, a, b) per symbol index.

        kind 'def': a = section name, b = offset
        kind 'imp': a = module index, b = symbol index in that module
        kind 'abs': a = absolute address
        """
        blob = self.secs.get("sym", b"")
        out = []
        for i in range(len(blob) // 12):
            info, value, extra = struct.unpack_from(">III", blob, i * 12)
            info >>= 16
            low, high = info & 0xF, info >> 4
            if low == 1:
                out.append(("imp", high, extra >> 16))
            elif low == 3:
                out.append(("abs", value, None))
            elif low in (2, 4) and high in SECTION_TYPE:
                out.append(("def", SECTION_TYPE[high], value))
            else:
                out.append(("none", None, None))
        return out


def resolve(modules, mod, sym_idx, depth=0):
    """Placeholder address of symbol sym_idx in module mod."""
    if depth > 4 or sym_idx >= len(mod.syms):
        return None
    kind, a, b = mod.syms[sym_idx]
    if kind == "def":
        return mod.addr[a] + b
    if kind == "abs":
        return a
    if kind == "imp":
        target = modules.get(a)
        if target is None:
            return None
        return resolve(modules, target, b, depth + 1)
    return None


def text_functions(mod, extra_targets):
    """Sorted function start offsets within the module's text."""
    size = mod.size("text")
    starts = {0}
    for kind, sec, off in mod.syms:
        if kind == "def" and sec == "text" and off < size:
            starts.add(off)
    starts.update(o for o in extra_targets if 0 <= o < size)
    base = mod.addr["text"]
    starts.update(a - base for a in ADDRESS_TAKEN if base <= a < base + size)
    skip = MICROCODE.get(mod.name, [])
    for lo, hi in skip:
        starts.add(hi)
    return sorted(o for o in starts if o % 4 == 0
                  and not any(lo <= o < hi for lo, hi in skip))


def relocate_text(modules, mod, problems):
    """Return (patched text bytes, reloc list, internal jal targets)."""
    text = bytearray(mod.secs["text"])
    relocs = []
    jal_targets = set()
    entries = uso.decode_relocs(mod.secs.get("text_reloc", b""))
    # Pair HI16 relocs with the following LO16 against the same symbol so the
    # addend split across the two instructions is reassembled correctly.
    pending_hi = {}
    # A LO16 may also follow an already-paired HI16 (several loads and stores
    # off one lui); it reuses the last HI16 addend seen for its symbol.
    last_hi_addend = {}
    for sym_idx, kind, off in entries:
        if off + 4 > len(text):
            problems.append(f"{mod.name}: reloc past end of text at {off:#x}")
            continue
        word = struct.unpack_from(">I", text, off)[0]
        target = resolve(modules, mod, sym_idx)
        if target is None:
            problems.append(f"{mod.name}: unresolved symbol {sym_idx} at {off:#x}")
            continue
        vram = mod.addr["text"] + off
        if kind == R_26:
            addend = (word & 0x3FFFFFF) << 2
            dest = target + addend
            word = (word & 0xFC000000) | ((dest >> 2) & 0x3FFFFFF)
            if mod.addr["text"] <= dest < mod.addr["text"] + mod.size("text"):
                jal_targets.add(dest - mod.addr["text"])
        elif kind == R_HI16:
            pending_hi.setdefault(sym_idx, []).append((off, word))
            continue
        elif kind == R_LO16:
            lo = struct.unpack(">h", struct.pack(">H", word & 0xFFFF))[0]
            his = pending_hi.pop(sym_idx, [])
            if his:
                hi_addend = (his[0][1] & 0xFFFF) << 16
                last_hi_addend[sym_idx] = hi_addend
            else:
                hi_addend = last_hi_addend.get(sym_idx, 0)
            dest = target + hi_addend + lo
            for hoff, hword in his:
                hi = ((dest + 0x8000) >> 16) & 0xFFFF
                struct.pack_into(">I", text, hoff, (hword & 0xFFFF0000) | hi)
                relocs.append((mod.addr["text"] + hoff, dest, R_HI16))
            word = (word & 0xFFFF0000) | (dest & 0xFFFF)
            ADDRESS_TAKEN.add(dest)
        else:
            problems.append(f"{mod.name}: unknown text reloc kind {kind}")
            continue
        struct.pack_into(">I", text, off, word)
        if dest >= PLACEHOLDER_BASE:
            relocs.append((vram, dest, kind))
    for sym_idx, his in pending_hi.items():
        for hoff, hword in his:
            target = resolve(modules, mod, sym_idx)
            dest = target + ((hword & 0xFFFF) << 16)
            hi = ((dest + 0x8000) >> 16) & 0xFFFF
            struct.pack_into(">I", text, hoff, (hword & 0xFFFF0000) | hi)
            relocs.append((mod.addr["text"] + hoff, dest, R_HI16))
            problems.append(f"{mod.name}: HI16 without LO16 at {hoff:#x}")
    relocs = [r for r in relocs if r[1] >= PLACEHOLDER_BASE]
    relocs.sort()
    return bytes(text), relocs, jal_targets


# Placeholder addresses the code takes the address of (lui/addiu pairs and
# data pointers). Those that land in a module's text are function entry points,
# typically called through function pointers.
ADDRESS_TAKEN = set()


def relocate_data(modules, mod, sec, problems):
    """Section bytes with 32-bit pointer relocations applied (kind 4)."""
    blob = bytearray(mod.secs[sec])
    for sym_idx, kind, off in uso.decode_relocs(mod.secs.get(sec + "_reloc", b"")):
        target = resolve(modules, mod, sym_idx)
        if kind != 4 or target is None or off + 4 > len(blob):
            problems.append(f"{mod.name}: bad {sec} reloc {sym_idx}/{kind} at {off:#x}")
            continue
        word = struct.unpack_from(">I", blob, off)[0]
        dest = (word + target) & 0xFFFFFFFF
        struct.pack_into(">I", blob, off, dest)
        # Pointers in data (not rodata, which holds switch jump tables) that
        # land in code are function pointers.
        if sec == "data":
            ADDRESS_TAKEN.add(dest)
    return bytes(blob)


def kernel_functions(rom):
    """Function starts in the kernel.

    The kernel has no symbol table, so starts come from the entry point, every
    jal target, every address loaded with a lui/addiu pair (function pointers
    such as thread entry points), and the first instruction after a `jr $ra`
    that no earlier branch in the same function jumps past."""
    end = KERNEL_VRAM + KERNEL_TEXT_SIZE
    words = [struct.unpack_from(">I", rom, o)[0]
             for o in range(KERNEL_ROM, KERNEL_ROM + KERNEL_TEXT_SIZE, 4)]
    starts = {KERNEL_VRAM}
    lui = {}
    for i, w in enumerate(words):
        op = w >> 26
        if op == 3:
            t = 0x80000000 | ((w & 0x3FFFFFF) << 2)
            if KERNEL_VRAM <= t < end:
                starts.add(t)
        elif op == 0x0F:
            lui[(w >> 16) & 0x1F] = (w & 0xFFFF) << 16
        elif op == 0x09 and ((w >> 21) & 0x1F) in lui:
            t = (lui[(w >> 21) & 0x1F] + struct.unpack(">h", struct.pack(">H", w & 0xFFFF))[0]) & 0xFFFFFFFF
            if KERNEL_VRAM <= t < end and t % 4 == 0:
                starts.add(t)

    def branch_target(i, w):
        op = w >> 26
        rt = (w >> 16) & 0x1F
        if op in (4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17) or (op == 1 and rt in (0, 1, 2, 3, 0x10, 0x11)) \
                or (op == 0x11 and ((w >> 21) & 0x1F) == 8):
            off = struct.unpack(">h", struct.pack(">H", w & 0xFFFF))[0]
            return i + 1 + off
        if op == 2:  # j: usually a branch within the function
            return (((KERNEL_VRAM + i * 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2) - KERNEL_VRAM) // 4
        return None

    reach = 0
    for i, w in enumerate(words):
        if KERNEL_VRAM + i * 4 in starts:
            reach = i
        t = branch_target(i, w)
        if t is not None and 0 <= t < len(words):
            reach = max(reach, t)
        if w == 0x03E00008 and reach <= i + 1:
            j = i + 2
            while j < len(words) and words[j] == 0:
                j += 1
            if j < len(words):
                starts.add(KERNEL_VRAM + j * 4)
    return sorted(starts)


NAMES = {}


def load_names(path):
    """Known names from recomp/function_names.txt: '<generated name> <name>'."""
    for line in open(path):
        line = line.split("#")[0].split()
        if len(line) == 2:
            NAMES[line[0]] = line[1]


def toml_funcs(lines, starts, end, name_fn, cuts=()):
    """Emit functions; each runs to the next start, or to a cut (microcode)."""
    lines.append("functions = [")
    for i, s in enumerate(starts):
        e = starts[i + 1] if i + 1 < len(starts) else end
        e = min([e] + [c for c in cuts if c > s])
        if e > s:
            name = name_fn(s)
            name = NAMES.get(name, name)
            lines.append(f'    {{ name = "{name}", vram = 0x{s:08X}, size = 0x{e - s:X} }},')
    lines.append("]")


def kernel_hooks(rom, starts):
    """N64Recomp hooks that sync loaded modules before each kernel jalr.

    The kernel calls into modules through function pointers, so syncing just
    before those calls keeps the runtime's function map current. Library
    functions (named ones) are skipped since N64Recomp replaces them.
    """
    config = Path(__file__).resolve().parent.parent / "recomp" / "us.toml"
    stubs = set(re.findall(r'"(func_[0-9A-F]{8})"', config.read_text()))
    out = ["# Generated by tools/gen_recomp.py. Do not edit.", ""]
    for off in range(0, KERNEL_TEXT_SIZE, 4):
        w = struct.unpack_from(">I", rom, KERNEL_ROM + off)[0]
        if w >> 26 != 0 or w & 0x3F != 9:  # SPECIAL / JALR
            continue
        vram = KERNEL_VRAM + off
        func = max(st for st in starts if st <= vram)
        name = f"func_{func:08X}"
        if NAMES.get(name, name) != name or name in stubs:
            continue
        out += ["[[patches.hook]]", f'func = "{name}"', f"before_vram = 0x{vram:08X}",
                'text = "    { void game_sync_modules(uint8_t*); game_sync_modules(rdram); }"', ""]
    return "\n".join(out) + "\n"


# The audio microcode lives in bootup's text. Its task uses it as both the boot
# and main ucode, so it runs from IMEM 0x1000. The extra branch targets are its
# command jump table (DMEM 0x10 in its data, bootup data offset 0x1D350).
AUDIO_UCODE = (0x46470, 0xF80)
AUDIO_JUMP_TABLE = [
    0x1f38, 0x122c, 0x1148, 0x1ee8, 0x1dfc, 0x14f0, 0x1e6c, 0x1cac,
    0x1178, 0x17c8, 0x11e4, 0x1aa0, 0x1b30, 0x118c, 0x1c58, 0x121c,
    0x1780, 0x182c, 0x1a1c, 0x1880, 0x1a80, 0x1a90, 0x1a44, 0x1b9c,
]


def audio_ucode_config(layout):
    """RSPRecomp config for the audio microcode."""
    off, size = AUDIO_UCODE
    targets = ", ".join(f"0x{t:04X}" for t in sorted(set(AUDIO_JUMP_TABLE)))
    return "\n".join([
        "# Generated by tools/gen_recomp.py. Do not edit.",
        f"text_offset = 0x{layout['bootup.uso']['text_rom'] + off:X}",
        f"text_size = 0x{size:X}",
        "text_address = 0x04001000",
        'rom_file_path = "image.bin"',
        'output_file_path = "../rsp/aspMain.cpp"',
        'output_function_name = "aspMain"',
        f"extra_indirect_branch_targets = [{targets}]",
    ]) + "\n"


def module_table(layout):
    """C++ table the runtime uses to map placeholder sections to load addresses."""
    rows = ["// Generated by tools/gen_recomp.py. Do not edit.",
            "// file index, name, then {section index, placeholder vram, size} for text, rodata, data, bss.",
            "// A section index of -1 means the module has no such section."]
    for name, e in layout.items():
        cols = []
        for s in ("text", "rodata", "data", "bss"):
            sec = e["sections"][s]
            idx = sec.get("index", -1)
            cols.append(f"{{{idx}, 0x{sec['vram']:08X}u, 0x{sec['size']:X}u}}")
        rows.append(f'{{{e["index"]}, "{name}", {{{", ".join(cols)}}}}},')
    return "\n".join(rows) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom", help="1080 Snowboarding (USA) .z64 ROM")
    ap.add_argument("out", help="output directory")
    a = ap.parse_args()

    rom = open(a.rom, "rb").read()
    names_file = Path(__file__).resolve().parent.parent / "recomp" / "function_names.txt"
    if names_file.exists():
        load_names(names_file)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    modules = {}
    base = PLACEHOLDER_BASE
    for idx, name, _entry, data in sorted(uso.list_files(rom)):
        if not name.endswith(".uso"):
            continue
        modules[idx] = Module(idx, name, uso.read_module(rom, data), base)
        base = modules[idx].end

    problems = []
    image = bytearray(rom)
    image += bytes(align(len(image), 0x10) - len(image))
    lines = ["# Generated by tools/gen_recomp.py. Do not edit.", ""]

    kstarts = kernel_functions(rom)
    lines += ["[[section]]", 'name = "kernel"', f"rom = 0x{KERNEL_ROM:X}",
              f"vram = 0x{KERNEL_VRAM:08X}", f"size = 0x{KERNEL_TEXT_SIZE:X}"]
    toml_funcs(lines, kstarts, KERNEL_VRAM + KERNEL_TEXT_SIZE,
               lambda v: f"func_{v:08X}")
    lines.append("")

    # Relocate everything first so every module's address-taken functions are
    # known before any module's function list is written.
    relocated = {}
    for idx in sorted(modules):
        mod = modules[idx]
        if mod.size("text"):
            relocated[idx, "text"] = relocate_text(modules, mod, problems)
        for s in ("rodata", "data"):
            if mod.size(s):
                relocated[idx, s] = relocate_data(modules, mod, s, problems)

    layout = {}
    sec_index = 1  # the kernel is section 0; N64Recomp numbers sections in file order
    total_funcs = len(kstarts)
    total_relocs = 0
    for idx in sorted(modules):
        mod = modules[idx]
        stem = mod.name.removesuffix(".uso")
        entry = {"index": idx, "name": mod.name,
                 "sections": {s: {"vram": mod.addr[s], "size": mod.size(s)}
                              for s in SECTION_ORDER}}
        if mod.size("text"):
            text, relocs, jal_targets = relocated[idx, "text"]
            rom_off = len(image)
            image += text
            image += bytes(align(len(image), 0x10) - len(image))
            starts = text_functions(mod, jal_targets)
            base = mod.addr["text"]
            cuts = [lo for lo, _ in MICROCODE.get(mod.name, [])]
            lines += ["[[section]]", f'name = "{stem}_text"', f"rom = 0x{rom_off:X}",
                      f"vram = 0x{base:08X}", f"size = 0x{len(text):X}"]
            toml_funcs(lines, [base + s for s in starts], base + len(text),
                       lambda v, b=base, st=stem: f"{st}_{v - b:06X}",
                       cuts=[base + c for c in cuts])
            lines.append("relocs = [")
            for vram, target, kind in relocs:
                lines.append(f'    {{ type = "{RELOC_NAMES[kind]}", vram = 0x{vram:08X}, '
                             f"target_vram = 0x{target:08X} }},")
            lines.append("]")
            lines.append("")
            entry["text_rom"] = rom_off
            entry["sections"]["text"]["index"] = sec_index
            sec_index += 1
            entry["functions"] = len(starts)
            total_funcs += len(starts)
            total_relocs += len(relocs)
        layout[mod.name] = entry
        # Data sections are listed so relocations can target them. Their bytes
        # go in the image too (with pointers relocated) because the recompiler
        # reads jump tables out of rodata.
        for s in ("rodata", "data", "bss"):
            if not mod.size(s):
                continue
            rom_off = 0
            if s != "bss":
                blob = relocated[idx, s]
                rom_off = len(image)
                image += blob
                image += bytes(align(len(image), 0x10) - len(image))
            lines += ["[[section]]", f'name = "{stem}_{s}"', f"rom = 0x{rom_off:X}",
                      f"vram = 0x{mod.addr[s]:08X}", f"size = 0x{mod.size(s):X}",
                      "functions = []", "relocs = []", ""]
            entry["sections"][s]["index"] = sec_index
            sec_index += 1

    (out / "image.bin").write_bytes(image)
    (out / "syms.toml").write_text("\n".join(lines) + "\n")
    (out / "modules.json").write_text(json.dumps(layout, indent=1))
    (out / "module_table.inl").write_text(module_table(layout))
    (out / "hooks.toml").write_text(kernel_hooks(rom, kstarts))
    (out / "aspMain.toml").write_text(audio_ucode_config(layout))
    print(f"{len(modules)} modules, {total_funcs} functions, {total_relocs} relocations")
    if problems:
        print(f"{len(problems)} problems, first few:")
        for p in problems[:15]:
            print("  " + p)


if __name__ == "__main__":
    main()

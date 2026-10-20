# How 1080 Snowboarding stores its code

Almost all of the game's code lives in **USO modules**: relocatable code files
that the kernel loads and links at runtime. Everything below was worked out
from the ROM itself and is what `tools/uso.py` and `tools/gen_recomp.py` rely on.
All values are big-endian.

## Kernel

ROM `0x1000`, loaded at `0x80000400` (the boot chip is a CIC-6103, which adds
`0x100000` to the header's entry point `0x80100400`). Text is `0x9E60` bytes.
It never moves, contains the USO loader, and has no symbol table.

> The reference decomp names kernel functions as if it were loaded at
> `0x80000000`; add `0x400` to its addresses to get the real ones.

## Filesystem

A file entry is `0x28` bytes: `0x12345678`, file index, `1`, then the name.
The file's data follows immediately. `.bin` files are raw. `.uso` files start
with a header word (`0x12345678`, or `0` in a few modules) followed by records.

## Records

```
type   u32   0 info, 1 sym, 2 text reloc, 3 data reloc, 4 rodata reloc,
             5 text, 6 data, 7 rodata, 8 bss, 9 entry sym, 11 end
size   u32   payload size (for bss: the bss size, and there is no payload)
attr   u32   bit 0x00010000 = payload is Yay0-compressed
payload
```

The next record starts at the end of the payload, rounded up to 2 bytes.

## Symbols

12 bytes each: `info:u16 pad:u16`, `value:u32`, `extra:u32`.

| info low nibble | meaning |
|---|---|
| 1 | import: `info >> 4` is the file index of the module that defines it, `extra >> 16` is the symbol's index in that module |
| 2 | symbol defined in section `info >> 4` (5 text, 6 data, 7 rodata, 8 bss) at offset `value` |
| 3 | absolute address `value` (used for kernel and libultra globals) |
| 4 | section start symbol, same layout as 2 |

Padding fields hold leftover bytes from the original tools and can be ignored.

## Relocations

12 bytes each: `symIdx << 4 | kind`, `offset`, `flags`.

- Text kinds: `1` = 26-bit jump (`jal`/`j`), `2` = LO16, `3` = HI16.
  The instruction's own immediate is the addend; HI16 pairs with the next LO16
  against the same symbol.
- Data and rodata kind `4` = 32-bit pointer, the word in place is the addend.

## How the port uses this

`tools/gen_recomp.py` gives each module a placeholder address range starting at
`0x81000000`, relocates every module against those placeholders, and writes an
N64Recomp symbol file in which every relocated instruction is also a
relocation. At runtime the PC build maps each placeholder section onto wherever
the game's own loader put it. All 11,016 calls between functions land on a
function listed in the modules' symbol tables, which is a good check that the
decoding is right.

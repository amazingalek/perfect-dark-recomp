"""The relocation list: every word of the game's data that is a pointer into the code segments the relink moves.

Perfect Dark runs lib at 0x70001050, its inflate code at 0x70200000 and game (.text and .rodata) at 0x7f000000, all
through the TLB. tools/relink.ld.patch links them where they sit in physical memory instead (0x80001050, 0x80200000,
0x80220000). The player's ROM supplies every byte at run time, unpacked by the game's own inflate; the words this list
names are then shifted from the retail addresses to the relinked ones, and the data is the relinked build's.

    relocs.py make A.elf A.map B.elf relocs.txt
        A is the relink at the real bases, B at other bases (tools/build_decomp.sh links both): the words that differ
        between them, outside .text, are the list. Offsets only, no bytes of the game.
    relocs.py check pd.z64 A.elf A.map relocs.txt
        The local check (needs the ROM; never in CI): the retail segments, inflated, with the list applied, equal the
        relink's byte for byte outside .text, and the preamble is the ROM's. Prints what differs if not, and how many
        words differ in .text (instructions that carry addresses, which the recompiler reads from the relink and the
        game never reads as data).
    relocs.py scan A.elf A.map
        Words outside .text in the relink that still hold a retail address (0x70000000-0x702fffff,
        0x7f000000-0x7f1fffff): hard-coded addresses no link can move.

The ROM's layout (lib compressed from 0x3050, data and game zipped behind Rare's 0x1173 header, game page by page
behind an offset table) is read as the decomp's tools/extract reads it.
"""
import re
import struct
import sys
import zlib

SEGMENTS = (".lib", ".data", ".inflate", ".game")
# Retail link addresses (lib/perfect_dark/ld/pd.ld); .data doesn't move.
RETAIL_BASE = {".lib": 0x70001050, ".inflate": 0x70200000, ".game": 0x7f000000}
MOVED = (".lib", ".inflate", ".game")


def elf_sections(path):
    with open(path, "rb") as f:
        elf = f.read()
    assert elf[:4] == b"\x7fELF" and elf[4] == 1 and elf[5] == 2, f"{path}: not a 32-bit big-endian ELF"
    shoff, = struct.unpack_from(">I", elf, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from(">HHH", elf, 0x2E)
    headers = [struct.unpack_from(">IIIIII", elf, shoff + i * shentsize) for i in range(shnum)]
    strtab_off = headers[shstrndx][4]
    out = {}
    for name_off, sh_type, _flags, addr, offset, size in headers:
        name = elf[strtab_off + name_off:elf.index(b"\0", strtab_off + name_off)].decode()
        if name in SEGMENTS or name == ".preamble":
            out[name] = (addr, b"" if sh_type == 8 else elf[offset:offset + size])  # 8: NOBITS
    return out


def text_ends(map_path):
    """Where each segment's .text ends: the lowest address of a non-empty .rodata (or .data) input section in it."""
    ends = {}
    section = None
    pending = None
    out_re = re.compile(r"^(\.\w+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)")
    in_re = re.compile(r"^ (\.\w+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+\S")
    with open(map_path) as f:
        for line in f:
            m = out_re.match(line)
            if m:
                section = m.group(1)
                continue
            if pending and re.match(r"^\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+\S", line):
                line = " " + pending + line
            pending = None
            if re.match(r"^ \.\w+\s*$", line):
                pending = line.strip()
                continue
            m = in_re.match(line)
            if m and section in SEGMENTS and m.group(1) != ".text" and int(m.group(3), 16):
                addr = int(m.group(2), 16)
                ends[section] = min(ends.get(section, addr), addr)
    return ends


def text_size(seg, sections, ends):
    base = sections[seg][0]
    return ends.get(seg, base + len(sections[seg][1])) - base if seg != ".data" else 0


def words(data, start=0):
    for off in range(start & ~3, len(data) - 3, 4):
        yield off, struct.unpack_from(">I", data, off)[0]


def make(a_elf, a_map, b_elf, out_path):
    a, b, ends = elf_sections(a_elf), elf_sections(b_elf), text_ends(a_map)
    lines = []
    for seg in (".lib", ".data", ".inflate", ".game"):
        da, db = a[seg][1], b[seg][1]
        assert len(da) == len(db), f"{seg}: {len(da):#x} bytes at one base, {len(db):#x} at the other"
        start = text_size(seg, a, ends)
        for off, wa in words(da, start):
            if wa != struct.unpack_from(">I", db, off)[0]:
                lines.append(f"{seg[1:]} {off:06x}")
    with open(out_path, "w", newline="\n") as f:
        f.write("# tools/relocs.py: <segment> <offset in it, hex> of each word that points into lib, inflate or game.\n")
        f.write("\n".join(lines) + "\n")
    print(f"{out_path}: {len(lines)} words")


def read_list(path):
    out = {}
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            seg, off = line.split()
            out.setdefault("." + seg, []).append(int(off, 16))
    return out


def retail_segments(rom_path, a_map):
    """lib, data and game inflated from the ROM, and inflate as it lies in it, as the decomp's tools/extract does."""
    with open(rom_path, "rb") as f:
        rom = f.read()
    syms = {}
    with open(a_map) as f:
        for m in re.finditer(r"0x([0-9a-f]+)\s+(_\w+SegmentRom(?:Start|End)) = ", f.read()):
            syms[m.group(2)] = int(m.group(1), 16)

    def unzip(at):
        assert rom[at:at + 2] == b"\x11\x73", f"no Rare zip header at {at:#x}"
        return zlib.decompressobj(wbits=-15).decompress(rom[at + 5:])

    lib = rom[0x1050:0x3050] + unzip(0x3050)
    data = unzip(syms["_datazipSegmentRomStart"])
    inflate = rom[syms["_inflateSegmentRomStart"]:syms["_inflateSegmentRomEnd"]]
    game = b""
    start = i = syms["_gamezipSegmentRomStart"]
    while True:
        at = start + struct.unpack_from(">I", rom, i)[0] + 2
        if rom[at:at + 2] == b"\0\0":
            break
        page = zlib.decompressobj(wbits=-15).decompress(rom[at + 5:at + 0x1000])
        game += page
        if len(page) != 0x1000:
            break
        i += 4
    return {".lib": lib, ".data": data, ".inflate": inflate, ".game": game}


def check(rom_path, a_elf, a_map, list_path):
    a, ends = elf_sections(a_elf), text_ends(a_map)
    retail = retail_segments(rom_path, a_map)
    relocs = read_list(list_path)
    ranges = [(RETAIL_BASE[s], RETAIL_BASE[s] + len(a[s][1]), a[s][0]) for s in MOVED]
    failed = False
    for seg in SEGMENTS[:4]:
        mine = bytearray(retail[seg])
        theirs = a[seg][1]
        # The ROM's inflated segments run to their 0x10-aligned end at most; the link's may have padding beyond.
        if len(mine) < len(theirs):
            print(f"{seg}: the ROM gives {len(mine):#x} bytes, the relink {len(theirs):#x}")
            failed = True
            continue
        mine = mine[:len(theirs)]
        bad = []
        for off in relocs.get(seg, []):
            v = struct.unpack_from(">I", mine, off)[0]
            for lo, hi, new in ranges:
                if lo <= v <= hi:
                    struct.pack_into(">I", mine, off, v - lo + new)
                    break
            else:
                bad.append(f"  {seg}+{off:#x}: {v:#010x} in the ROM is no retail lib, inflate or game address")
        tsize = text_size(seg, a, ends)
        diffs = [off for off, w in words(theirs, tsize) if struct.unpack_from(">I", mine, off)[0] != w]
        tdiffs = sum(1 for off, w in words(theirs[:tsize]) if struct.unpack_from(">I", mine, off)[0] != w)
        n = len(relocs.get(seg, []))
        print(f"{seg}: {len(theirs):#x} bytes, .text {tsize:#x}; {n} words relocated; "
              f"{len(diffs)} words differ outside .text, {tdiffs} in .text (instructions with addresses)")
        for line in bad[:20]:
            print(line)
        for off in diffs[:40]:
            print(f"  {seg}+{off:#x}: ROM+list {struct.unpack_from('>I', mine, off)[0]:#010x}, "
                  f"relink {struct.unpack_from('>I', theirs, off)[0]:#010x}")
        failed |= bool(bad or diffs)
    # The preamble, linked where IPL3 runs it, is the ROM's own (its call to vm_boot is the same address either way).
    with open(rom_path, "rb") as f:
        f.seek(0x1000)
        preamble = f.read(0x50)
    if a[".preamble"][1] != preamble:
        print(".preamble: not the ROM's 0x1000-0x1050")
        failed = True
    print("FAIL: the ROM plus the list is not the relink" if failed else
          "OK: the retail segments, inflated, plus the list equal the relink outside .text")
    return 1 if failed else 0


def scan(a_elf, a_map):
    a, ends = elf_sections(a_elf), text_ends(a_map)
    for seg in SEGMENTS:
        for off, w in words(a[seg][1], text_size(seg, a, ends)):
            if 0x70000000 <= w < 0x70300000 or 0x7F000000 <= w < 0x7F200000:
                print(f"{seg}+{off:#x} ({a[seg][0] + off:#010x}): {w:#010x}")


if __name__ == "__main__":
    cmd, args = sys.argv[1], sys.argv[2:]
    sys.exit({"make": make, "check": check, "scan": scan}[cmd](*args) or 0)

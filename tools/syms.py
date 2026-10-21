"""Writes the N64Recomp config that dumps the relink's symbols (syms/dump.toml, syms/data_dump.toml), with a size for
every function the ELF gives none.

    syms.py build/relink/stage1.elf build/relink/pd.map build/dump_syms.toml

N64Recomp takes a symbol as a function only when it is STT_FUNC with a size. IDO gives its C functions both, but the
decomp's assembly (`glabel`, src/include/macros.inc) gives a global label no type and no size, and its few
`.type @function` labels no size: those are sized here, as the distance to the next function in the same code. A
function whose last instruction pair is no jump falls through into the next one (cosf adds pi/2 and runs into sinf, as
GoldenEye's does): its size takes the next one's in, so the recompiled function runs to a return. Local labels without
a type (rzip.s's `.pdheader`) are inside functions, not functions. Prints each function sized here and each fall
through, and every IDO size that disagrees with the distance to the next function.

The entry point is the preamble (ROM 0x1000, linked at 0x80001000 by tools/relink.ld.patch), which N64Recomp names
recomp_entrypoint.
"""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from relocs import text_ends  # noqa: E402

CODE = (".preamble", ".lib", ".inflate", ".game")


def elf(path):
    data = Path(path).read_bytes()
    shoff, = struct.unpack_from(">I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from(">HHH", data, 0x2E)
    shdrs = [struct.unpack_from(">IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]

    def cstr(off):
        return data[off:data.index(b"\0", off)].decode()

    names = [cstr(shdrs[shstrndx][4] + h[0]) for h in shdrs]
    sections = {n: (h[3], h[4], h[5]) for n, h in zip(names, shdrs)}  # addr, offset, size
    symtab = shdrs[names.index(".symtab")]
    strtab = shdrs[names.index(".strtab")]
    syms = []
    for off in range(symtab[4], symtab[4] + symtab[5], 16):
        name_off, value, size, info, _other, shndx = struct.unpack_from(">IIIBBH", data, off)
        if 0 < shndx < len(names):
            syms.append((cstr(strtab[4] + name_off), value, size, info >> 4, info & 0xF, names[shndx]))
    return data, sections, syms


def word(data, sections, sec, addr):
    base, off, _ = sections[sec]
    return struct.unpack_from(">I", data, off + addr - base)[0]


def is_jump(insn):
    op = insn >> 26
    if op == 0 and insn & 0x3F == 8:  # jr
        return True
    if op == 2:  # j
        return True
    return insn >> 16 == 0x1000 or insn == 0x42000018  # beq $zero, $zero (b), eret


def returns(data, sections, sec, start, end):
    """Whether the code from start to end ends in a jump (or a call that never returns, as __osCleanupThread's to
    osDestroyThread) and its delay slot, past any nops padding it out."""
    last = end - 4
    while last > start and word(data, sections, sec, last) == 0:
        last -= 4
    return any(is_jump(w) or w >> 26 == 3 for w in (word(data, sections, sec, a) for a in (last, last - 4) if a >= start))


def objects(map_path):
    """(start, end, object name) of each input .text section, from the map."""
    out = []
    pending = None
    with open(map_path) as f:
        for line in f:
            if pending and re.match(r"^\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+\S", line):
                line = " .text" + line
            pending = re.match(r"^ \.text\s*$", line)
            m = re.match(r"^ \.text\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", line)
            if m and int(m.group(2), 16):
                start = int(m.group(1), 16)
                out.append((start, start + int(m.group(2), 16), Path(m.group(3)).stem))
    return sorted(out)


def main(elf_path, map_path, relocs_path, out_path):
    data, sections, syms = elf(elf_path)
    ends = text_ends(map_path)
    text = {s: (sections[s][0], ends.get(s, sections[s][0] + sections[s][2])) for s in CODE}

    def code_section(addr):
        return next((s for s, (lo, hi) in text.items() if lo <= addr < hi), None)

    STT_NOTYPE, STT_FUNC, STB_GLOBAL = 0, 2, 1
    named = {}  # addr -> (name, ELF size or 0)
    names_seen = {}
    for name, value, size, bind, typ, sec in syms:
        names_seen[name] = names_seen.get(name, 0) + 1
        if code_section(value) == sec and (typ == STT_FUNC or (typ == STT_NOTYPE and bind == STB_GLOBAL)):
            if value not in named or (size and not named[value][1]):
                named[value] = (name, size if typ == STT_FUNC else 0)

    # Where code goes that no symbol names: jal targets, and pointers to code built with lui/addiu or held in .data.
    report = []
    targets = {}
    for sec, (lo, hi) in text.items():
        for pc in range(lo, hi, 4):
            w = word(data, sections, sec, pc)
            if w >> 26 == 3:
                targets.setdefault(((pc & 0xF0000000) | (w & 0x3FFFFFF) << 2), "jal")
            elif w >> 26 == 0xF:  # lui
                rt, hi16 = (w >> 16) & 0x1F, w & 0xFFFF
                for pc2 in range(pc + 4, min(pc + 0x40, hi), 4):
                    w2 = word(data, sections, sec, pc2)
                    if w2 >> 26 == 9 and (w2 >> 21) & 0x1F == rt:  # addiu rX, rt
                        addr = ((hi16 << 16) + (w2 & 0xFFFF) - (0x10000 if w2 & 0x8000 else 0)) & 0xFFFFFFFF
                        if code_section(addr):
                            targets.setdefault(addr, "lui/addiu")
                        break
    for line in Path(relocs_path).read_text().splitlines():
        if line.startswith("data "):
            off = int(line.split()[1], 16)
            v = word(data, sections, ".data", sections[".data"][0] + off)
            if code_section(v):
                targets.setdefault(v, ".data pointer")

    # Each named function covers its ELF size; code past that, up to the next named function, is split at the targets.
    starts = dict((a, n) for a, n in named.items())
    order = sorted(starts)
    objs = objects(map_path)
    covered = []
    for i, addr in enumerate(order):
        name, size = starts[addr]
        if size:
            covered.append((addr, addr + size))
    covered.sort()
    unnamed = []
    for addr, kind in sorted(targets.items()):
        if addr in starts:
            continue
        inside = next(((lo, hi) for lo, hi in covered if lo < addr < hi), None)
        if inside:
            report.append(f"  {kind} target {addr:#010x} is inside {starts[inside[0]][0]}: not split")
            continue
        unnamed.append(addr)
    # The first word of code after a named function's ELF size, past nops, starts code too.
    for lo, hi in covered:
        sec = code_section(lo)
        a = hi
        while a < text[sec][1] and a not in starts and word(data, sections, sec, a) == 0:
            a += 4
        if a < text[sec][1] and a not in starts and a not in unnamed:
            unnamed.append(a)
    for addr in unnamed:
        obj = next((o for lo, hi, o in objs if lo <= addr < hi), "code")
        starts[addr] = (f"{re.sub(r'[^A-Za-z0-9_]', '_', obj)}_{addr:08X}", 0)

    sizes, manual = [], []
    order = sorted(starts)
    for i, addr in enumerate(order):
        name, size = starts[addr]
        sec = code_section(addr)
        bound = lambda j: order[j] if j < len(order) and order[j] < text[sec][1] else text[sec][1]
        nxt = bound(i + 1)
        if size:
            if size > nxt - addr:
                report.append(f"  {name} {addr:#010x}: ELF size {size:#x} runs past the next function, {nxt - addr:#x} on")
            continue
        end, j = nxt, i + 1
        while end < text[sec][1] and not returns(data, sections, sec, addr, end):
            report.append(f"  {name} {addr:#010x} falls through into {starts[end][0]}")
            j += 1
            end = bound(j)
        if addr in unnamed:
            manual.append((name, sec, addr, end - addr))
        elif names_seen[name] > 1:
            report.append(f"  {name} {addr:#010x}: the name is not unique, so it can't be sized by name")
        else:
            sizes.append((name, end - addr))

    with open(out_path, "w", newline="\n") as f:
        f.write("# Made by tools/syms.py for `N64Recomp <this> --dump-context`.\n[input]\n")
        f.write("entrypoint = 0x80001000\n")
        f.write(f'elf_path = "{Path(elf_path).resolve().as_posix()}"\n')
        f.write('output_func_path = "RecompiledFuncs"\n')
        f.write("function_sizes = [\n")
        for name, size in sorted(sizes):
            f.write(f'    {{ name = "{name}", size = {size:#x} }},\n')
        f.write("]\nmanual_funcs = [\n")
        for name, sec, addr, size in manual:
            f.write(f'    {{ name = "{name}", section = "{sec}", vram = {addr:#010x}, size = {size:#x} }},\n')
        f.write("]\n")
    print(f"{out_path}: {len(sizes)} named functions sized here, {len(manual)} unnamed ones (IDO's statics) found")
    print("\n".join(report))


if __name__ == "__main__":
    main(*sys.argv[1:])

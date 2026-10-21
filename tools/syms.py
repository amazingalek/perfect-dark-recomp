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
    """Whether the code from start to end ends in a jump and its delay slot, past any nops padding it out."""
    last = end - 4
    while last > start and word(data, sections, sec, last) == 0:
        last -= 4
    return any(is_jump(word(data, sections, sec, a)) for a in (last, last - 4) if a >= start)


def main(elf_path, map_path, out_path):
    data, sections, syms = elf(elf_path)
    ends = text_ends(map_path)
    text = {s: (sections[s][0], ends.get(s, sections[s][0] + sections[s][2])) for s in CODE}
    STT_NOTYPE, STT_FUNC, STB_GLOBAL = 0, 2, 1
    funcs = {}
    for name, value, size, bind, typ, sec in syms:
        if sec not in text or not text[sec][0] <= value < text[sec][1]:
            continue
        if typ == STT_FUNC or (typ == STT_NOTYPE and bind == STB_GLOBAL):
            funcs.setdefault((sec, value), []).append((name, size, typ, bind))
    names_seen = {}
    for name, *_ in syms:
        names_seen[name] = names_seen.get(name, 0) + 1

    sizes = []
    report = []
    by_sec = {}
    for sec, value in funcs:
        by_sec.setdefault(sec, []).append(value)
    for sec, addrs in by_sec.items():
        addrs.sort()
        bounds = addrs[1:] + [text[sec][1]]
        for i, (addr, nxt) in enumerate(zip(addrs, bounds)):
            entries = funcs[(sec, addr)]
            name, size, typ, bind = entries[0]
            gap = nxt - addr
            if typ == STT_FUNC and size:
                if size > gap or size < gap - 0x10:
                    report.append(f"  {name} {addr:#010x}: IDO size {size:#x}, next function {gap:#x} on")
                continue
            # Sized here: to the next function, and on through every function it falls into.
            end, j = nxt, i
            while end < text[sec][1] and not returns(data, sections, sec, addr, end):
                report.append(f"  {name} {addr:#010x} falls through into {funcs[(sec, end)][0][0]}")
                j += 1
                end = bounds[j]
            if names_seen[name] > 1:
                report.append(f"  {name} {addr:#010x}: the name is not unique, so it can't be sized by name")
                continue
            sizes.append((name, end - addr))

    with open(out_path, "w", newline="\n") as f:
        f.write("# Made by tools/syms.py for `N64Recomp <this> --dump-context`.\n[input]\n")
        f.write("entrypoint = 0x80001000\n")
        f.write(f'elf_path = "{Path(elf_path).resolve().as_posix()}"\n')
        f.write('output_func_path = "RecompiledFuncs"\n')
        f.write("function_sizes = [\n")
        for name, size in sorted(sizes):
            f.write(f'    {{ name = "{name}", size = {size:#x} }},\n')
        f.write("]\n")
    print(f"{out_path}: {len(sizes)} functions sized here")
    print("\n".join(report))


if __name__ == "__main__":
    main(*sys.argv[1:])

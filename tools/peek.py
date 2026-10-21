# Reads the game's variables while it runs, from inside tools/play_linux.sh's container: RDRAM is where the program's
# log says ("[pd] rdram at", src/pd_natives.cpp), and a symbol's address is the relink's (build/relink/pd.map).
#
#     python3 -I tools/peek.py <expr>[:<type>]...
#     python3 -I tools/peek.py offsetof '<struct x>' <field>     a field's offset, from the decompilation's headers
#
# An expression is a symbol or a number, plus or minus numbers, and [...] reads the word at an address in it:
# "[g_Vars+0x284]+0x38:f". Types: w (a 32-bit word, the default), h and b (signed 16 and 8 bits), f (a float), x (16
# bytes in hex). RDRAM keeps each 32-bit word in the host's byte order, so a byte at a is at host offset a ^ 3.
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def offsetof(struct_name, field):
    # The compiler says, as the value of a variable in its assembly: clang for MIPS, as patches/Makefile builds.
    src = (
        '#include <ultra64.h>\n#include "constants.h"\n#include "types.h"\n#include "bss.h"\n'
        f"int offset = __builtin_offsetof({struct_name}, {field});\n"
    )
    decomp = os.path.join(ROOT, "lib/perfect_dark")
    asm = subprocess.run(
        ["clang", "-target", "mips", "-mips2", "-mabi=32", "-S", "-x", "c", "-", "-o", "-", "-nostdinc",
         "-D_LANGUAGE_C", "-D_MIPS_SZLONG=32", "-D_MIPS_SZINT=32", "-DVERSION=2", "-DPAL=0", "-DMATCHING=0",
         "-I", f"{decomp}/include", "-I", f"{decomp}/include/PR", "-I", f"{decomp}/src/include",
         "-I", f"{decomp}/src/generated/ntsc-final"],
        input=src, capture_output=True, text=True, check=True).stdout
    return int(re.search(r"^offset:\s*\n\s*\.4byte\s+(\d+)", asm, re.M).group(1))


def symbols():
    syms = {}
    for line in open(os.path.join(ROOT, "build/relink/pd.map")):
        m = re.match(r"\s+0x([0-9a-f]{8,16})\s+([A-Za-z_][A-Za-z0-9_]*)(\s*=.*)?$", line)
        if m:
            syms[m.group(2)] = int(m.group(1), 16) & 0xFFFFFFFF
    return syms


def rdram_base():
    for line in open(os.path.join(ROOT, "build/play/run.log")):
        if line.startswith("[pd] rdram at "):
            return int(line.split()[-1], 16)
    sys.exit("no '[pd] rdram at' in build/play/run.log yet")


def game_pid():
    for pid in os.listdir("/proc"):
        if pid.isdigit():
            try:
                if os.readlink(f"/proc/{pid}/exe").endswith("PerfectDarkRecompiled"):
                    return int(pid)
            except OSError:
                pass
    sys.exit("the game isn't running")


class Memory:
    def __init__(self):
        self.base = rdram_base()
        self.mem = open(f"/proc/{game_pid()}/mem", "rb")

    def read(self, addr, size):
        self.mem.seek(self.base + (addr & 0x1FFFFFFF))
        return self.mem.read(size)

    def word(self, addr):
        return struct.unpack("<I", self.read(addr, 4))[0]

    def byte(self, addr):
        return self.read(addr ^ 3, 1)[0]


def evaluate(expr, syms, memory):
    expr = expr.strip()
    while True:
        m = re.search(r"\[([^\[\]]*)\]", expr)
        if not m:
            break
        expr = expr[:m.start()] + hex(memory.word(evaluate(m.group(1), syms, memory))) + expr[m.end():]
    total = 0
    for sign, term in re.findall(r"([+-]?)\s*([A-Za-z_0-9]+)", expr):
        value = int(term, 0) if re.match(r"^(0x[0-9a-fA-F]+|\d+)$", term) else syms[term]
        total += -value if sign == "-" else value
    return total & 0xFFFFFFFF


def main():
    if sys.argv[1:2] == ["offsetof"]:
        print(hex(offsetof(sys.argv[2], sys.argv[3])))
        return
    syms = symbols()
    memory = Memory()
    for arg in sys.argv[1:]:
        expr, _, kind = arg.partition(":")
        addr = evaluate(expr, syms, memory)
        if kind in ("", "w"):
            value = hex(memory.word(addr))
        elif kind == "f":
            value = struct.unpack("<f", memory.read(addr, 4))[0]
        elif kind == "h":
            value = struct.unpack(">h", bytes([memory.byte(addr), memory.byte(addr + 1)]))[0]
        elif kind == "b":
            value = struct.unpack("b", bytes([memory.byte(addr)]))[0]
        elif kind == "x":
            value = bytes(memory.byte(addr + i) for i in range(16)).hex(" ")
        else:
            sys.exit(f"no such type: {kind}")
        print(f"{arg} = {value}")


main()

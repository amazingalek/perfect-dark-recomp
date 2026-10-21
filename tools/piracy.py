"""The code checksums of the piracy checks, written into the relink.

Twelve of Perfect Dark's piracy checks sum the code of one function (a different sum for each) and compare it with a
constant built into another (docs/building.md, What the TLB-free relink has to keep). The decomp compiles that constant
as a placeholder, 0x99aabbcc in a lui/ori pair, and its mkrom fills it in while it makes the ROM
(lib/perfect_dark/tools/mkrom/piracy.c), so its stage1.bin, and the relink linked from the same objects, still hold the
placeholder: every check would fail. One that fails writes an empty file system over the start of the EEPROM
(lv_reset), and the others corrupt code or data.

At run time the checks sum RDRAM, which holds the ROM's code, so the constants are mkrom's: this does what mkrom does,
with its sums, its order (a function one check patches is one a later check sums) and its search for the pair, over
the decomp's stage1.bin, and writes each sum into the relink's stage1.bin as well. The two images have the same layout
(each section at the same load address), which it checks pair by pair.

    piracy.py decomp/stage1.bin decomp/pd.map relink/stage1.bin
"""
import struct
import sys

PLACEHOLDER = 0x99AABBCC
M = 0xFFFFFFFF


def algo12(s, w):
    s ^= ~w & M
    s ^= (w << 5) & M
    s ^= w >> 15
    return s


# lib/perfect_dark/tools/mkrom/piracy.c: the algorithm, the function the sum is written into, the function summed.
CHECKS = (
    (lambda s, w: s ^ w, "__scHandleTasks", "boot"),
    (lambda s, w: s ^ (~w & M), "cheat_menu_handle_dialog", "__scHandleTasks"),
    (lambda s, w: ((s + w) * 2) & M, "bot_pickup_prop", "door_finish_close"),
    (lambda s, w: (s + (~w & M)) & M, "chr_uncloak", "bot_pickup_prop"),
    (lambda s, w: (s * 2 + w) & M, "chrs_check_for_noise", "__scHandleRetrace"),
    (lambda s, w: (s + w) & M, "lv_reset", "lv_get_slow_motion_type"),
    (lambda s, w: ((s << 1) & M) ^ w, "body_instantiate_eyespy", "lv_reset"),
    (lambda s, w: (s + w + (w >> 1)) & M, "chr_consider_grenade_throw", "bg_reset"),
    (lambda s, w: (s - (~w & M)) & M, "bgun_tick_gun_load", "tags_reset"),
    (lambda s, w: ((s ^ w) << 1) & M, "explosion_alert_chrs", "glass_destroy"),
    (lambda s, w: ((s ^ (~w & M)) << 1) & M, "menu_tick_timers", "mtx_get_obfuscated_rom_base"),
    (algo12, "bg_tick_counter", "menu_tick_timers"),
)


def function_rompos(map_lines, name):
    """mkrom's map_get_function_rompos: from the function's symbol to the next address the map lists."""
    seg_ram = seg_rom = 0
    start = None
    for line in map_lines:
        if start is not None:
            stripped = line.lstrip()
            if stripped.startswith("0x"):
                return start, int(stripped.split()[0], 16) - seg_ram + seg_rom
        elif line.startswith("."):
            fields = line.split("0x")
            if len(fields) >= 4:
                seg_ram = int(fields[1].split()[0], 16)
                seg_rom = int(fields[3].split()[0], 16)
        elif line.endswith(" " + name + "\n"):
            start = int(line[line.index("0x"):].split()[0], 16) - seg_ram + seg_rom
    sys.exit(f"{name} isn't in the map")


def word(image, offset):
    return struct.unpack_from(">I", image, offset)[0]


def is_branch_likely(w):
    # beql, bnel, blezl, bgtzl. mkrom's tests for bltzl and bgezl compare the opcode with 0x01000000, which no masked
    # opcode equals, so they are left out here too.
    return (w & 0xFC000000) in (0x50000000, 0x54000000, 0x58000000, 0x5C000000)


def placeholder_pair(image, start, end):
    """mkrom's write_sum search: lui $at, 0x99aa then ori $at, $at, 0xbbcc, outside a branch-likely's delay slot."""
    upper = lower = None
    in_likely = False
    for offset in range(start, end, 4):
        if upper is not None and lower is not None:
            break
        w = word(image, offset)
        if in_likely:
            in_likely = False
        elif is_branch_likely(w):
            in_likely = True
        elif w == 0x3C010000 | (PLACEHOLDER >> 16):
            upper = offset
        elif upper is not None and w == 0x34210000 | (PLACEHOLDER & 0xFFFF):
            lower = offset
    if upper is None or lower is None:
        return None
    return upper, lower


def write_sum(image, pair, total):
    upper, lower = pair
    struct.pack_into(">H", image, upper + 2, total >> 16)
    struct.pack_into(">H", image, lower + 2, total & 0xFFFF)


def main():
    decomp_bin, decomp_map, relink_bin = sys.argv[1:4]
    decomp = bytearray(open(decomp_bin, "rb").read())
    relink = bytearray(open(relink_bin, "rb").read())
    map_lines = open(decomp_map).readlines()
    for algo, patch_func, sum_func in CHECKS:
        start, end = function_rompos(map_lines, sum_func)
        total = 0
        for offset in range(start, end, 4):
            total = algo(total, word(decomp, offset))
        start, end = function_rompos(map_lines, patch_func)
        pair = placeholder_pair(decomp, start, end)
        relink_pair = placeholder_pair(relink, start, end)
        if pair is None or pair != relink_pair:
            sys.exit(f"{patch_func}: placeholder at {pair} in the decomp's image, {relink_pair} in the relink's")
        write_sum(decomp, pair, total)
        write_sum(relink, pair, total)
        print(f"{patch_func}: 0x{total:08x}, the sum of {sum_func}")
    open(relink_bin, "wb").write(relink)


main()

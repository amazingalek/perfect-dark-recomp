# Builds patches.elf, the game functions patches/ replaces, for MIPS with clang and ld.lld (CMakeLists.txt runs it;
# N64Recomp then recompiles it with patches.toml). The headers are the decompilation's (lib/perfect_dark), as
# ntsc-final (VERSION 2), and the ones its build generates from the extracted ROM (src/generated/ntsc-final, from
# tools/build_decomp.sh), so patches/ builds only where the ROM is. RT64's extended GBI (lib/rt64/include) adds
# commands for RT64 to a display list.
TARGET = patches.elf

CC      ?= clang
LD      ?= ld.lld

DECOMP   := ../lib/perfect_dark

CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
			-fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset \
			-Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter \
			-Wno-unknown-pragmas -Wno-unused-variable -Wno-unused-but-set-variable -Wno-missing-braces \
			-Wno-unsupported-floating-point-opt -Wno-switch -fno-strict-aliasing

CPPFLAGS := -nostdinc -D_LANGUAGE_C -D_MIPS_SZLONG=32 -D_MIPS_SZINT=32 -DVERSION=2 -DPAL=0 -DMATCHING=0 \
			-DPIRACYCHECKS=0 -DROM_SIZE=32 -I $(DECOMP)/include -I $(DECOMP)/include/PR -I $(DECOMP)/src/include \
			-I $(DECOMP)/src/generated/ntsc-final -I ../lib/rt64/include

LDFLAGS  := -nostdlib -T patches.ld -T syms.ld -Map patches.map --unresolved-symbols=ignore-all --emit-relocs

C_SRCS := $(wildcard *.c)
C_OBJS := $(C_SRCS:.c=.o)
C_DEPS := $(C_SRCS:.c=.d)

$(TARGET): $(C_OBJS) patches.ld syms.ld
	$(LD) $(C_OBJS) $(LDFLAGS) -o $@

$(C_OBJS): %.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MF $(@:.o=.d) -c -o $@

clean:
	rm -rf $(C_OBJS) $(TARGET) $(C_DEPS)

-include $(C_DEPS)

.PHONY: clean

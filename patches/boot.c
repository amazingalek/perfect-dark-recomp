// The decompilation's boot path (src/lib/vm.s, boot.c, vminit.c) without the TLB.
//
// On the console vm_boot maps 0x70000000-0x707fffff onto physical memory from 0 with one TLB entry, boot unzips lib
// and .data through that mapping, and vm_init (on 8 MB) unzips every page of game to 0x80220000 and maps it at
// 0x7f000000. The recompiled code is the TLB-free relink (docs/building.md), where lib, inflate and game are linked at
// those physical addresses, so here every 0x70xxxxxx address is its KSEG0 0x80xxxxxx, nothing is mapped, and after
// each unzip the pointers syms/relocs.txt lists are moved from the retail addresses to the relink's
// (recomp_pd_relocate). The ROM's bytes are the player's, inflated by the game's own inflate as on the console.
//
// The rest is the decompilation's code as ntsc-final builds it, but the piracy check in boot (low RAM IPL3 wrote,
// read through KSEG1) and vm_init's 4 MB path, as the runtime gives the game 8 MB.

#include "patches.h"
#include "lib/vm.h"

extern u8 _libSegmentStart;
extern u8 _dataSegmentStart;
extern u8 _datazipSegmentRomStart;
extern u8 _datazipSegmentRomEnd;
extern u8 _inflateSegmentRomStart;
extern u8 _inflateSegmentRomEnd;
extern u8 _gameSegmentStart;
extern u8 _gameSegmentEnd;
extern u8 _gamezipSegmentRomStart;

extern u32 g_SavedOsMemSize;
extern u8 *g_StackLeftAddrs[NUM_THREADS];
extern u8 *g_StackRightAddrs[NUM_THREADS];
extern OSThread g_MainThread;

extern u8 g_VmInitialised;
extern u8 g_Is4Mb;
extern u8 *g_VmMarker;
extern u32 g_VmNumTlbMisses;
extern u32 g_VmNumPageMisses;
extern u32 g_VmNumPageReplaces;

void boot(void);
void boot_create_threads(void *arg);
void *boot_allocate_stack(s32 threadid, s32 size);
void seg_inflate(void *src, void *dst, void *scratch);
void rzip_init(void);
u32 rzip_inflate(void *src, void *dst, void *scratch);
void dma_exec(void *memaddr, romptr_t romaddr, u32 len);
u32 __osGetFpcCsr(void);
u32 __osSetFpcCsr(u32 arg0);

// vm_boot: the TLB entry for 0x70000000, then boot.
RECOMP_PATCH void vm_boot(void)
{
	boot();
}

RECOMP_PATCH void boot(void)
{
	s32 datacomplen;
	s32 inflatelen;
	u8 *dataziprom;
	u8 *datazipram;
	u32 *libram;
	u32 *libzipram;
	u32 copylen;
	s32 i;
	u32 flags;

	if (osResetType == RESETTYPE_WARM) {
		g_SavedOsMemSize = *(u32 *) STACK_START;
	} else {
		*(u32 *) STACK_START = g_SavedOsMemSize = osMemSize;
	}

	// The compressed .data and inflate, which IPL3 (the runtime) copied from ROM 0x1000 to 0x80001000 with the
	// first MB, to below 0x80200000 and to 0x80200000.
	datacomplen = (romptr_t) &_datazipSegmentRomEnd - (romptr_t) &_datazipSegmentRomStart;
	inflatelen = (romptr_t) &_inflateSegmentRomEnd - (romptr_t) &_inflateSegmentRomStart;
	copylen = datacomplen + inflatelen;
	libram = (u32 *) ((romptr_t) &_libSegmentStart + 0x2000);
	libzipram = (u32 *) 0x80280000;
	dataziprom = (u8 *) ((romptr_t) &_datazipSegmentRomStart | 0x80000000);
	datazipram = (u8 *) (0x80200000 - datacomplen);

	for (i = copylen - 1; i >= 0; i--) {
		datazipram[i] = dataziprom[i];
	}

	// lib is raw for its first 0x2000 bytes, zipped after.
	for (i = 0; i < 75000; i++) {
		libzipram[i] = libram[i];
	}

	seg_inflate((void *) libzipram, (void *) libram, (void *) 0x80300000);
	seg_inflate((void *) datazipram, (void *) &_dataSegmentStart, (void *) 0x80300000);

	recomp_pd_relocate(PD_SEG_LIB);
	recomp_pd_relocate(PD_SEG_DATA);
	recomp_pd_relocate(PD_SEG_INFLATE);

	for (i = 0; i < ARRAYCOUNT(g_StackLeftAddrs); i++) {
		g_StackLeftAddrs[i] = NULL;
		g_StackRightAddrs[i] = NULL;
	}

	osInitialize();
	osWritebackDCacheAll();
	osInvalICache((void *) K0BASE, ICACHE_SIZE);

	flags = __osGetFpcCsr();
	flags |= FPCSR_EI;
	flags |= FPCSR_EO;
	flags |= FPCSR_EZ;
	flags |= FPCSR_EV;
	__osSetFpcCsr(flags);

	osCreateThread(&g_MainThread, THREAD_MAIN, boot_create_threads, NULL, boot_allocate_stack(THREAD_MAIN, STACKSIZE_MAIN), THREADPRI_MAIN);
	osStartThread(&g_MainThread);
}

// vm_init's 8 MB path: every page of game unzipped below the threads' stacks, which is where the relink links it.
RECOMP_PATCH void vm_init(void)
{
	u32 *romaddrs;
	u32 numentries;
	u8 *gameseg;
	u8 *dst;
	u8 *chunkbuffer;
	u8 *zip;
	s32 i;
	u8 scratch[1024 * 5];

	g_VmInitialised = true;

	rzip_init();

	g_Is4Mb = false;
	numentries = (u32) ((&_gameSegmentEnd - &_gameSegmentStart) + (VM_PAGE_SIZE - 1)) / VM_PAGE_SIZE + 1;
	gameseg = (u8 *) ((uintptr_t) ((u8 *) STACK_START - (u8 *) ALIGN64((uintptr_t) &_gameSegmentEnd - (uintptr_t) &_gameSegmentStart)) & 0xfffe0000);

	if (gameseg != &_gameSegmentStart) {
		recomp_pd_crash("vm_init: game would unzip somewhere other than where the relink links it");
	}

	romaddrs = (u32 *) (((uintptr_t) gameseg - ((numentries + 4) << 2)) & ~0xf);
	g_VmMarker = gameseg;

	// The offset of each page's zip, then the zips.
	dma_exec(romaddrs, (romptr_t) &_gamezipSegmentRomStart, ALIGN16((numentries + 1) << 2));

	for (i = 0; i < numentries; i++) {
		romaddrs[i] += (romptr_t) &_gamezipSegmentRomStart;
	}

	dst = gameseg;
	chunkbuffer = (u8 *) ((uintptr_t) romaddrs - VM_PAGE_SIZE * 2);
	zip = chunkbuffer + 2;

	for (i = 0; i < numentries - 1; i++) {
		dma_exec(chunkbuffer, romaddrs[i], ALIGN16(romaddrs[i + 1] - romaddrs[i]));
		dst += rzip_inflate(zip, dst, scratch);
	}

	recomp_pd_relocate(PD_SEG_GAME);

	g_VmNumTlbMisses = 0;
	g_VmNumPageMisses = 0;
	g_VmNumPageReplaces = 0;

	osInvalICache(0, ICACHE_SIZE);
}

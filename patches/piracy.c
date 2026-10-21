// The two piracy checks that read the ROM themselves, past libultra: each waits for the PI through its status
// register, then loads a word through the cartridge's address (objectives.c's func0f095350, mtx_c.c's
// mtx_get_obfuscated_rom_base). Neither is memory here, so each reads the player's ROM through a native instead. The
// checks that use osPiReadIo read it through the runtime (src/pd_natives.cpp).

#include "patches.h"

// The ROM word at offset arg0 into *arg1.
RECOMP_PATCH void func0f095350(u32 arg0, u32 *arg1)
{
	recomp_pd_read_rom(arg0, arg1);
}

// The ROM word at 0xa5c.
RECOMP_PATCH u32 mtx_get_obfuscated_rom_base(void)
{
	u32 value;

	recomp_pd_read_rom(0xa5c, &value);

	return value;
}

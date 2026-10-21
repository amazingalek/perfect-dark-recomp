// The paks in the controllers: a Rumble Pak in each connected one (src/main.cpp, get_connected_device_info), no
// Controller Pak and no Transfer Pak. Saves go to the cartridge's EEPROM, as on a console with no Controller Pak.
//
// The game finds a pak by asking for a Controller Pak first and trying the Rumble Pak only when that answers with
// another device (pak_probe, src/game/pak.c). The runtime's osPfsInitPak answers that nothing is plugged, so it would
// never find one; and osMotorProbe is libultra's own here, which talks to the pak through the controller's RAM. So a
// controller with a Rumble Pak answers PFS_ERR_DEVICE, as a console's does, and osMotorProbe is the runtime's
// osMotorInit, which makes rumble go to the runtime's (__osMotorAccess, src/main.cpp's set_rumble).

#include "patches.h"
#include "game/pak.h"

extern u32 g_PakHasEeprom;

RECOMP_PATCH s32 osMotorProbe(OSMesgQueue *ctrlrqueue, OSPfs *pfs, s32 channel)
{
	return recomp_pd_motor_init(ctrlrqueue, pfs, channel);
}

// src/game/pak.c's, for ntsc-final, with a controller's answer as above.
RECOMP_PATCH PakErr1 pak_init_pak(OSMesgQueue *mq, OSPfs *pfs, s32 channel, s32 *arg3)
{
	if (pfs) {
		return recomp_pd_motor_init(mq, pfs, channel) == 0 ? PFS_ERR_DEVICE : PFS_ERR_NOPACK;
	}

	if (!g_PakHasEeprom) {
		return PAK_ERR1_EEPROMMISSING;
	}

	return PAK_ERR1_OK;
}

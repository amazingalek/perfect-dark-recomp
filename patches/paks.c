// The paks in the controllers: a Rumble Pak in each connected one (src/main.cpp, get_connected_device_info), no
// Controller Pak and no Transfer Pak. Saves go to the cartridge's EEPROM, as on a console with no Controller Pak.
//
// The game finds a pak by asking for a Controller Pak first and trying the Rumble Pak only when that answers with
// another device (pak_probe, src/game/pak.c). The runtime's osPfsInitPak answers that nothing is plugged, so it would
// never find one; and osMotorProbe is libultra's own here, which talks to the pak through the controller's RAM. So a
// controller with a Rumble Pak answers PFS_ERR_DEVICE, as a console's does, and osMotorProbe is the runtime's
// osMotorInit, which makes rumble go to the runtime's (__osMotorAccess, src/main.cpp's set_rumble).
//
// Neither touches the controllers' polling, so the rumble is stopped without stopping it (joy_stop_rumble below).

#include "patches.h"
#include "game/pak.h"
#include "lib/joy.h"
#include "bss.h"

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

// src/lib/joy.c's, for ntsc-final, without the stop and restart of the controllers' polling around the motor's stop.
// On a console the polling stops so the pak can be reached over the controller's bus, and the scheduler answers each
// stop and each start at its next retrace (joys_handle_retrace). paks_stop calls this for each of the 4 ports that
// holds no Controller or Transfer Pak as each shot of a mission's opening cutscene starts (player_start_cutscene2),
// so the game took one frame of 8 VIs (133 ms) there, on a console as on the desktop build and the page
// (N64Recomp-browser's docs/findings.md, Ruled out, measured). Here osMotorProbe and osMotorStop answer at once
// through the runtime, beside the polling, so the shot starts at the next VI.
RECOMP_PATCH void joy_stop_rumble(s8 arg0, bool disablepolling)
{
	if (arg0 != SAVEDEVICE_GAMEPAK) {
		s32 device = arg0;

		if (g_Paks[device].type != PAKTYPE_MEMORY && g_Paks[device].type != PAKTYPE_GAMEBOY) {
			if (osMotorProbe(&g_PiMesgQueue, PFS(device), device) == 0) {
				osMotorStop(PFS(device));
				osMotorStop(PFS(device));
				osMotorStop(PFS(device));
			}

			if (g_Paks[device].rumblestate != RUMBLESTATE_DISABLED_STOPPING
					&& g_Paks[device].rumblestate != RUMBLESTATE_DISABLED_STOPPED) {
				g_Paks[device].rumblestate = RUMBLESTATE_ENABLED_STOPPING;
			}

			g_Paks[device].rumblettl = -1;
		}
	}
}

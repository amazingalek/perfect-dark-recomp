/* Native functions the patches call (src/pd_natives.cpp): N64Recomp turns a call to one of these absolute addresses
 * into a call to the C function of the same name (patches.toml, use_absolute_symbols). */
recomp_pd_relocate = 0x8F000000;
recomp_pd_crash = 0x8F000004;
recomp_pd_read_rom = 0x8F000008;
recomp_pd_motor_init = 0x8F00000C;

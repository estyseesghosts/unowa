/*
 * The state the DS firmware and ARM7 leave in main RAM before a game's main
 * runs, written through sinjoh's NitroPort_BootFirmware (port_boot.c:24-75):
 * the cartridge header, the owner settings, the MAC address, the reset
 * parameter, and the key register and X/Y/lid word at "nothing held, lid
 * open".
 *
 * The key state matters at the first frame. Both words are active-low
 * (port_boot.c:81-82). Left at zero they read as every button held:
 * KEY_PadRead (Aletheia arm9/key.c:111-114) turns them into L+R+Start+Select,
 * UI_Main (arm9/ui.c:272) treats that combination as a soft reset, and
 * UI_SoftReset ends in OS_ResetSystem before NitroPort_OnFrame has written a
 * real pad state.
 *
 * Owner settings are kohjoh's defaults (kohjoh src/app/main.c:218-228) until
 * they are read from the 3DS system configuration; see docs/status.md
 * "Boot firmware state".
 */
#include <nitro.h>
#include <nitro_port.h>
#include <string.h>

/* NitroSDK CARDRomHeader field offsets (kohjoh src/runtime/kj_boot.c:39-44). */
#define ROM_HEADER_SIZE 0x200
#define ROM_TITLE_OFFSET 0x00
#define ROM_GAME_CODE_OFFSET 0x0C
#define ROM_MAKER_CODE_OFFSET 0x10
#define ROM_DEVICE_SIZE_OFFSET 0x14

/* The Black cartridge's identity, as the first 0x15 bytes of its header have
 * it (Aletheia extract/header.bin: "POKEMON B", "IRBO", "01", device size
 * 0x0B). The build opens black.pak (engine/app/game_main.c). */
#define ROM_TITLE "POKEMON B"
#define ROM_GAME_CODE "IRBO"
#define ROM_MAKER_CODE "01"
#define ROM_DEVICE_SIZE 0x0B

/* NVRAM_CONFIG_LANG_ENGLISH (sinjoh compat/hgss/include/nitro/spi/userInfo.h:34). */
#define OWNER_LANGUAGE_ENGLISH 1

/* The game keeps the console's MAC address in the save and compares it on
 * load; one fixed address keeps every save "from this DS" (kohjoh
 * src/runtime/kj_boot.c:34-37). */
static const u8 s_mac_address[6] = { 0x00, 0x09, 0xBF, 0x48, 0x47, 0x53 };

static const u16 s_owner_name[] = { 'P', 'l', 'a', 'y', 'e', 'r', 0 };

void unowa_boot_firmware(void)
{
    static u8 header[ROM_HEADER_SIZE];
    NitroPortFirmware firmware;

    memset(header, 0, sizeof(header));
    memcpy(header + ROM_TITLE_OFFSET, ROM_TITLE, sizeof(ROM_TITLE) - 1);
    memcpy(header + ROM_GAME_CODE_OFFSET, ROM_GAME_CODE, sizeof(ROM_GAME_CODE) - 1);
    memcpy(header + ROM_MAKER_CODE_OFFSET, ROM_MAKER_CODE, sizeof(ROM_MAKER_CODE) - 1);
    header[ROM_DEVICE_SIZE_OFFSET] = ROM_DEVICE_SIZE;

    memset(&firmware, 0, sizeof(firmware));
    firmware.romHeader = header;
    firmware.romHeaderSize = sizeof(header);
    firmware.nickname = s_owner_name;
    firmware.language = OWNER_LANGUAGE_ENGLISH;
    firmware.birthMonth = 1;
    firmware.birthDay = 1;
    memcpy(firmware.macAddress, s_mac_address, sizeof(s_mac_address));
    firmware.rtcOffset = 0;
    NitroPort_BootFirmware(&firmware);
}

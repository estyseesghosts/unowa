/*
 * card_backup_port.c: the Gen 5 save device, on 3DS.
 *
 * SAVE_CONTROL asks CARD_IdentifyBackup for CARD_BACKUP_TYPE_FLASH 4 Mbit, any
 * vendor (0x00ff1302) and then reads the last page of that device
 * (FLASH_ACCESS_Read(0x7F800, ..., 884)). On the DS those calls end up in
 * libcard's card_backup.o, which drives the backup chip through the card bus.
 * ARM11 has no such bus, so identification and the read both fail and
 * FLASH_ACCESS_Read calls its on_error hook, which is
 * APP_POWER_ClearCallback: the power-off screen, before the game can start.
 *
 * This file replaces every export of card_backup.o with the same device, laid
 * out in a 512 KiB image. The image is mirrored to sdmc:/3ds/unowa/save.bin, so
 * a save survives a restart. A missing file starts as erased flash (0xFF),
 * which is what the game sees on a cartridge that was never saved to: the
 * checksums fail, SAVE_CONTROL reports no save data, and the title screen's
 * new-game path is the one that runs.
 *
 * Argument order follows CARDi_RequestStreamCommandCore in
 * src/game/arm9/card_backup.c: a read names the flash address in `src` and the
 * RAM buffer in `dst`; a write names the RAM buffer in `src` and the flash
 * address in `dst`; an erase names the flash address in `dst`.
 *
 * The request is completed where it is issued, so a caller that asked for an
 * asynchronous transfer finds it finished at CARD_WaitBackupAsync. The write
 * callback (FLASH_ACCESS_OnWriteDone) is called before returning, because
 * FLASH_ACCESS_StartWrite clears its done flag immediately before the request.
 */

#include "storage/paths.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned int u32;
typedef int BOOL;

#define TRUE 1
#define FALSE 0

/* CARDBackupType, CARD_common.h: device in bits 0..7, size exponent in 8..15,
 * vendor in 16..23. */
#define BACKUP_DEVICE_FLASH 2
#define BACKUP_TYPE_FLASH_4MBIT 0x00ff1302u
#define BACKUP_SIZE_4MBIT 0x80000u

/* CARDRequest, CARD_common.h. */
#define CARD_REQ_READ_BACKUP 6
#define CARD_REQ_WRITE_BACKUP 7
#define CARD_REQ_PROGRAM_BACKUP 8
#define CARD_REQ_VERIFY_BACKUP 9
#define CARD_REQ_ERASE_PAGE_BACKUP 10
#define CARD_REQ_ERASE_SECTOR_BACKUP 11
#define CARD_REQ_ERASE_CHIP_BACKUP 12

/* CARD_RESULT_*, CARD_common.h: 0 is success. */
#define CARD_RESULT_SUCCESS 0
#define CARD_RESULT_FAILURE 1
#define CARD_RESULT_INVALID_PARAM 2

#define SAVE_PATH_FALLBACK "sdmc:/3ds/unowa/saves/save.sav"
#define SAVE_ERASED 0xFF

/* The path the rest of the port uses (storage/paths.c:42-63), filled in by
 * unowa_paths_init from the M1 entry point. */
static const char *save_path(void)
{
    const unowa_paths *paths = unowa_paths_get();

    return paths != 0 ? paths->save_file : SAVE_PATH_FALLBACK;
}

static u8 s_image[BACKUP_SIZE_4MBIT];
static u8 s_erased[0x1000];
static FILE *s_file;
static u32 s_size = BACKUP_SIZE_4MBIT;
static int s_type = (int)BACKUP_TYPE_FLASH_4MBIT;
static int s_result = CARD_RESULT_SUCCESS;
static int s_opened;

/*
 * Open the save file once. A file that cannot be opened leaves the image in
 * memory only: the game still boots and runs, it just cannot persist anything,
 * and that is the one case where a write reports success without a file.
 */
static void save_open(void)
{
    u32 left;
    size_t got;
    const char *path;

    if (s_opened != 0) {
        return;
    }
    s_opened = 1;

    memset(s_image, SAVE_ERASED, s_size);
    path = save_path();
    s_file = fopen(path, "r+b");
    if (s_file == 0) {
        s_file = fopen(path, "w+b");
        if (s_file == 0) {
            return;
        }
        memset(s_erased, SAVE_ERASED, sizeof(s_erased));
        for (left = s_size; left >= sizeof(s_erased); left -= sizeof(s_erased)) {
            fwrite(s_erased, 1, sizeof(s_erased), s_file);
        }
        if (left != 0) {
            fwrite(s_erased, 1, left, s_file);
        }
        fflush(s_file);
        return;
    }
    got = fread(s_image, 1, s_size, s_file);
    (void)got;  /* a short file keeps the erased tail */
}

static int save_in_range(u32 address, u32 length)
{
    return address <= s_size && length <= s_size - address;
}

static void save_write_file(u32 address, u32 length)
{
    if (s_file == 0) {
        return;
    }
    if (fseek(s_file, (long)address, SEEK_SET) != 0) {
        return;
    }
    fwrite(s_image + address, 1, length, s_file);
    fflush(s_file);
}

static void save_store(u32 address, const void *data, u32 length)
{
    memcpy(s_image + address, data, length);
    save_write_file(address, length);
}

static void save_erase(u32 address, u32 length)
{
    memset(s_image + address, SAVE_ERASED, length);
    save_write_file(address, length);
}

BOOL CARDi_RequestStreamCommand(unsigned int src, unsigned int dst,
                                u32 length, void (*callback)(void *),
                                void *arg, BOOL is_async, int req_type,
                                int req_retry, int req_mode);
BOOL CARD_IdentifyBackup(int type);

BOOL CARDi_RequestStreamCommand(unsigned int src, unsigned int dst,
                                u32 length, void (*callback)(void *),
                                void *arg, BOOL is_async, int req_type,
                                int req_retry, int req_mode)
{
    (void)req_retry;
    (void)req_mode;
    save_open();
    s_result = CARD_RESULT_SUCCESS;

    switch (req_type) {
    case CARD_REQ_READ_BACKUP:
        if (!save_in_range((u32)src, length)) {
            s_result = CARD_RESULT_INVALID_PARAM;
            break;
        }
        memcpy((void *)dst, s_image + src, length);
        break;
    case CARD_REQ_WRITE_BACKUP:
    case CARD_REQ_PROGRAM_BACKUP:
    case CARD_REQ_VERIFY_BACKUP:
        if (!save_in_range((u32)dst, length)) {
            s_result = CARD_RESULT_INVALID_PARAM;
            break;
        }
        save_store((u32)dst, (const void *)src, length);
        break;
    case CARD_REQ_ERASE_PAGE_BACKUP:
    case CARD_REQ_ERASE_SECTOR_BACKUP:
    case CARD_REQ_ERASE_CHIP_BACKUP:
        if (!save_in_range((u32)dst, length)) {
            s_result = CARD_RESULT_INVALID_PARAM;
            break;
        }
        save_erase((u32)dst, length);
        break;
    default:
        s_result = CARD_RESULT_FAILURE;
        break;
    }

    if (callback != 0) {
        callback(arg);
    }
    /* An asynchronous request only reports that it was accepted; the result is
     * read back through CARD_WaitBackupAsync. A synchronous one reports it. */
    if (is_async != 0) {
        return TRUE;
    }
    return s_result == CARD_RESULT_SUCCESS ? TRUE : FALSE;
}

/* Write-and-verify with the sector granularity of the flash part. */
BOOL CARDi_RequestWriteSectorCommand(unsigned int src, unsigned int dst,
                                     u32 length, BOOL verify,
                                     void (*callback)(void *), void *arg,
                                     BOOL is_async);

BOOL CARDi_RequestWriteSectorCommand(unsigned int src, unsigned int dst,
                                     u32 length, BOOL verify,
                                     void (*callback)(void *), void *arg,
                                     BOOL is_async)
{
    (void)verify;
    return CARDi_RequestStreamCommand(src, dst, length, callback, arg, is_async,
                                      CARD_REQ_WRITE_BACKUP, 1, 1);
}

/* Status byte of an erase/program command. The flash part has none of the
 * EEPROM's busy/status protocol, so success is the honest answer. */
int CARDi_AccessStatus(int command, u8 value)
{
    (void)command;
    (void)value;
    return 0;
}

BOOL CARD_IdentifyBackup(int type)
{
    int device = type & 0xFF;
    int sizebits = (type >> 8) & 0xFF;

    /* The vendor byte (0xff for "any") is not a constraint here. */
    if (device != BACKUP_DEVICE_FLASH) {
        return FALSE;
    }
    if (sizebits < 18 || sizebits > 20) {
        return FALSE;
    }
    s_type = type;
    s_size = 1u << sizebits;
    save_open();
    return TRUE;
}

u32 CARD_GetBackupTotalSize(void)
{
    return s_size;
}

u32 CARD_GetBackupSectorSize(void)
{
    return 0x1000;
}

u32 CARD_GetBackupPageSize(void)
{
    return 0x100;
}

int CARD_GetCurrentBackupType(void)
{
    return s_type;
}

/* The transfer is finished before the request returns, so waiting succeeds
 * with whatever result it recorded. */
BOOL CARD_WaitBackupAsync(void)
{
    return s_result == CARD_RESULT_SUCCESS ? TRUE : FALSE;
}

BOOL CARD_TryWaitBackupAsync(void)
{
    return TRUE;
}

void CARD_CancelBackupAsync(void)
{
}

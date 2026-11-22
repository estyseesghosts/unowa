/*
 * wireless_stubs.c: DS wireless and DSi-only entry points with no 3DS port.
 *
 * The 3DS has no DS-compatible wireless in native mode: its local wireless
 * service is UDS, which "is not DS Wireless" (coffeepot ctr/glossary.md:429).
 * Download Play (ov107, ov016 dwc_mov_mbp.c), the Wi-Fi Connection setup
 * utility (ov016 archive.c, font.c) and the WFC network stack (ov114) are
 * therefore stubbed until a UDS or HTTP-based replacement is designed. The
 * DSi DSP (twl/) has no counterpart in the DS-mode game this port runs.
 *
 * The real implementations are blocked from the build: ov107/mb_wm_base.c
 * and ov114/crypto_bobj2.c are Metrowerks inline assembly, twl/dsp_process.c
 * needs DSi-keyed generated data (engine/game_overlay_blocklist.txt), and the
 * arm9 FS/CARD/NNS font files duplicate or conflict with sinjoh's libraries
 * (engine/game_blocklist.txt). Only wireless code references the symbols
 * below; see docs/status.md "Wireless stubs" for the full list.
 *
 * Every stub reports its name once through NitroPort_Unimplemented
 * (nitro_port.h:28-31) and returns the value its callers treat as failure,
 * so reaching one shows up in the log instead of passing silently.
 */

#include <nitro/types.h>
#include <nitro_port.h>
#include <stdint.h>

/* MB_ERRCODE_INVALID_STATE, nitro/mb/mb.h:85. */
#define WIRELESS_MB_UNAVAILABLE 2

/* ------------------------------------------------ Download Play (ov107) */

/* Prototypes: ov107/mb_wm_base.h:10-25. mbp.c:499 and dwc_mov_mbp.c:151
 * treat a nonzero MB_Init result as a fatal error. */
int MB_Init(void *work, const void *user, u32 ggid, u32 tgid, u32 dma)
{
    (void)work;
    (void)user;
    (void)ggid;
    (void)tgid;
    (void)dma;
    NITRO_PORT_UNIMPLEMENTED();
    return WIRELESS_MB_UNAVAILABLE;
}

int MB_SetParentCommParam(unsigned short send_size, unsigned short max_children)
{
    (void)send_size;
    (void)max_children;
    NITRO_PORT_UNIMPLEMENTED();
    return WIRELESS_MB_UNAVAILABLE;
}

/* dwc_mov_mbp.c:165 treats a nonzero result as "parent failed to start". */
int MB_StartParentFromIdle(int channel)
{
    (void)channel;
    NITRO_PORT_UNIMPLEMENTED();
    return WIRELESS_MB_UNAVAILABLE;
}

void MB_EndToIdle(void)
{
    NITRO_PORT_UNIMPLEMENTED();
}

void MB_DisconnectChild(u16 aid)
{
    (void)aid;
    NITRO_PORT_UNIMPLEMENTED();
}

/* mb_fileinfo.c:392 returns 0 from MB_RegisterFile when this is false. */
int MBi_IsStarted(void)
{
    NITRO_PORT_UNIMPLEMENTED();
    return 0;
}

/* Declared extern in ov016/dwc_moveid.c:76 and dwc_mov_mbp.c:98. */
void DWCi_DwcMoveidMB_EndToIdle(void)
{
    NITRO_PORT_UNIMPLEMENTED();
}

/* SDK internals reached only from ov107/mb_fileinfo.c MB_ReadSegment,
 * which reads this game's own ROM to send it to a Download Play child.
 * Prototypes: arm9/card_api.h:18, arm9/card_rom.h:35, arm9/crt0.c:62. */
const void *CARD_GetOwnRomHeader(void)
{
    NITRO_PORT_UNIMPLEMENTED();
    return NULL;
}

void *CARDi_GetRomCacheBuffer(void)
{
    NITRO_PORT_UNIMPLEMENTED();
    return NULL;
}

void _start_AutoloadDoneCallback(void *argv[])
{
    (void)argv;
    NITRO_PORT_UNIMPLEMENTED();
}

/* ------------------------------- FS entry points used only by wireless */

/* Gen V's SDK has these as functions (arm9/fs_file.h:20-23,
 * fs_proc_mem.h:10, fs_proc_default.h:53); sinjoh's are header inlines
 * (nitro/fs/file.h:267, nitro/fs/archive.h:155) or absent. Their only
 * callers are ov016/archive.c (WFC setup) and ov107/mb_fileinfo.c,
 * mb_gameinfo.c (Download Play). */
BOOL FS_OpenFileEx(void *file, const char *path, u32 mode)
{
    (void)file;
    (void)path;
    (void)mode;
    NITRO_PORT_UNIMPLEMENTED();
    return FALSE;
}

u32 FS_GetFileLength(void *file)
{
    (void)file;
    NITRO_PORT_UNIMPLEMENTED();
    return 0;
}

u32 FS_GetFilePosition(void *file)
{
    (void)file;
    NITRO_PORT_UNIMPLEMENTED();
    return 0;
}

u32 FS_GetFileImageTop(void *file)
{
    (void)file;
    NITRO_PORT_UNIMPLEMENTED();
    return 0;
}

void *FS_GetArchiveBase(void *arc)
{
    (void)arc;
    NITRO_PORT_UNIMPLEMENTED();
    return NULL;
}

/* ---------------------- NNS font internals used only by ov016/font.c */

/* Prototypes: arm9/nns_g2d_font.h:62-68 and ov016/font.c:135-138. The
 * game's own text path uses sinjoh's libnitrosystem; these internal entry
 * points exist only for the WFC setup utility's font renderer. */
s8 NNSi_G2dCharCanvasDrawChar(void *canvas, void *font, int x, int y, int cl, u16 code)
{
    (void)canvas;
    (void)font;
    (void)x;
    (void)y;
    (void)cl;
    (void)code;
    NITRO_PORT_UNIMPLEMENTED();
    return 0;
}

void NNSi_G2dInitPenSimple(void *pen, void *char_base, int area_width, int area_height, int dst_bpp)
{
    (void)pen;
    (void)char_base;
    (void)area_width;
    (void)area_height;
    (void)dst_bpp;
    NITRO_PORT_UNIMPLEMENTED();
}

void NNS_G2dCharCanvasInitForVRAM(void *canvas, void *char_base, int area_width, int area_height,
                                  int color_mode)
{
    (void)canvas;
    (void)char_base;
    (void)area_width;
    (void)area_height;
    (void)color_mode;
    NITRO_PORT_UNIMPLEMENTED();
}

u32 NNSi_G2dCalcCharAreaSize(u32 area_width, u32 area_height)
{
    (void)area_width;
    (void)area_height;
    NITRO_PORT_UNIMPLEMENTED();
    return 0;
}

int NNSi_G2dMakeCharObjArea(void *oam, int area_width, int area_height, int x, int y, int color,
                            int char_name, int vram_shift)
{
    (void)oam;
    (void)area_width;
    (void)area_height;
    (void)x;
    (void)y;
    (void)color;
    (void)char_name;
    (void)vram_shift;
    NITRO_PORT_UNIMPLEMENTED();
    return 0;
}

/* Step is a callback pointer type in both prototypes; passed through as
 * void * here because neither stub calls it. */
void NNSi_G2dCalcTextBox(void *ctx, s32 x, s32 y, s32 w, u32 flags, const void *text, void *step)
{
    (void)ctx;
    (void)x;
    (void)y;
    (void)w;
    (void)flags;
    (void)text;
    (void)step;
    NITRO_PORT_UNIMPLEMENTED();
}

void NNSi_G2dAlignTextVertical(void *ctx, s32 x, s32 y, s32 w, u32 h, u32 w2, u32 flags,
                               const void *text, void *step)
{
    (void)ctx;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)w2;
    (void)flags;
    (void)text;
    (void)step;
    NITRO_PORT_UNIMPLEMENTED();
}

/* --------------------------------------- WFC bignum words (ov114) */

/* Prototypes: ov114/crypto_bobj2.h:9-14 (BN_ULONG is unsigned long,
 * crypto_types.h:14). Reached only through the WFC encrypted-session and
 * signature code in ov114, which needs a network connection first. */
unsigned long CRYPTOi__bn_mul_add_words(unsigned long *rp, const unsigned long *ap, int num,
                                        unsigned long w)
{
    (void)rp;
    (void)ap;
    (void)num;
    (void)w;
    NITRO_PORT_UNIMPLEMENTED();
    return 0;
}

unsigned long CRYPTOi__bn_mul_words(unsigned long *rp, const unsigned long *ap, int num,
                                    unsigned long w)
{
    (void)rp;
    (void)ap;
    (void)num;
    (void)w;
    NITRO_PORT_UNIMPLEMENTED();
    return 0;
}

void CRYPTOi__bn_sqr_words(unsigned long *r, const unsigned long *a, int n)
{
    (void)r;
    (void)a;
    (void)n;
    NITRO_PORT_UNIMPLEMENTED();
}

unsigned long long CRYPTOi__bn_div_words(unsigned long h, unsigned long l, unsigned long d)
{
    (void)h;
    (void)l;
    (void)d;
    NITRO_PORT_UNIMPLEMENTED();
    return 0;
}

unsigned long CRYPTOi__bn_add_words(unsigned long *r, const unsigned long *a,
                                    const unsigned long *b, int n)
{
    (void)r;
    (void)a;
    (void)b;
    (void)n;
    NITRO_PORT_UNIMPLEMENTED();
    return 0;
}

unsigned long CRYPTOi__bn_sub_words(unsigned long *r, const unsigned long *a,
                                    const unsigned long *b, int n)
{
    (void)r;
    (void)a;
    (void)b;
    (void)n;
    NITRO_PORT_UNIMPLEMENTED();
    return 0;
}

/* ------------------------------------------------------- DSi DSP (twl) */

/* Prototypes: twl/dsp_process.h:18-25. twl/dsp_util.c:120-123 returns
 * early when DSP_FindProcess gives NULL, and dsp_g711.c:53 leaves the
 * codec unready when DSP_ExecuteProcess returns 0. */
void DSP_InitProcessContext(void *proc, void *name)
{
    (void)proc;
    (void)name;
    NITRO_PORT_UNIMPLEMENTED();
}

u32 DSP_ExecuteProcess(void *proc, void *file, u16 bank_mask_a, u16 bank_mask_b)
{
    (void)proc;
    (void)file;
    (void)bank_mask_a;
    (void)bank_mask_b;
    NITRO_PORT_UNIMPLEMENTED();
    return 0;
}

void DSP_QuitProcess(void *proc)
{
    (void)proc;
    NITRO_PORT_UNIMPLEMENTED();
}

void *DSP_FindProcess(const char *name)
{
    (void)name;
    NITRO_PORT_UNIMPLEMENTED();
    return NULL;
}

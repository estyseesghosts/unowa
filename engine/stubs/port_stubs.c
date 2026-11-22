/*
 * port_stubs.c: values the DS build gets from linker symbols or the port.
 *
 * Aletheia declares several fixed-address symbols as `extern char name[]`
 * but uses them as numbers, for example `(uptr)SDK_IRQ_STACKSIZE` in
 * arm9/os_interrupt.c:191. Those are linker symbol *values*, not arrays of
 * bytes at those addresses, so a real array would give the wrong number.
 * `.set` defines them at their documented values from the source headers.
 *
 * bw_limit_get is declared in port_limits.h:23 as "provided by the port".
 * The values below are the DS defaults from the BW_LIMIT macro calls:
 * arm9/item.h:7 (626), arm9/move.h:7 (559), arm9/species.h:7 (649),
 * ov094/btlv_effvm_types.h:10 (0x231) and ov010/fldmmdl_types.h:33 (799).
 */

#include "arm9/dgt_sha1.h"
#include "arm9/mic.h"
#include "arm9/rc4.h"
#include "port_limits.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

int g_pm_language = 0;  /* English: version.h:61 */
int g_pm_version = 0;   /* Black: version.h:28 */

/*
 * Data symbols with no address annotation in src/game. The names encode the
 * DS address; extents below are minima from the referencing code, not the
 * original sizes. M2 must place them at their real addresses and extents.
 *   twl/pm.c:51,56-57 and twl/nwm_unloaddev.c:32
 */
unsigned int d_020A8724[16];
unsigned char d_02153000[0x18];
void *d_02732FA4;
unsigned char d_027341C8;

/*
 * Memory helpers hand-written in ARM on the DS (mi_memory.h:21-25). The 3DS
 * needs the same operations, not the same instruction sequence, so the libc
 * routines are the port implementation.
 */
void MI_CpuFill(void *dest, unsigned char data, unsigned int size)
{
    memset(dest, data, size);
}

int MI_CpuComp8(const void *mem1, const void *mem2, unsigned int size)
{
    return memcmp(mem1, mem2, size);
}

void MI_CpuCopy(const void *src, void *dest, unsigned int size)
{
    memcpy(dest, src, size);
}

void MI_CpuMove(const void *src, void *dest, unsigned int size)
{
    memmove(dest, src, size);
}

/*
 * DS hardware controls with no ARM11 user-mode equivalent. These keep the
 * link closed; M2 decides what, if anything, they must do on 3DS.
 */
void MIi_DmaSetParameters(unsigned int dmaNo, unsigned int src,
                          unsigned int dest, unsigned int ctrl, unsigned int mode)
{
    (void)dmaNo; (void)src; (void)dest; (void)ctrl; (void)mode;
}

void OS_SpinWaitSysCycles(unsigned int cycle)
{
    volatile unsigned int i;
    for (i = 0; i < cycle; i++) {
        /* spin */
    }
}

void OSi_DisableInterruptsAndHalt(void)
{
}

void OSi_SyncWithOtherProc(int direction, volatile unsigned char *regs)
{
    (void)direction; (void)regs;
}

void SCFGi_SwitchCpuSpeed(int speed)
{
    (void)speed;
}

/*
 * The six sealed bodies in ov230 are copy-protection trampolines. Their
 * observable contract is to call the supplied hook with the target and return
 * its result; callers compare that result with ~target (event_battle.c:592,
 * gamesystem.c:451). The DS indirection through decrypted code has no meaning
 * on 3DS, so the port calls through directly.
 */
typedef u32 (*SealedCallHook)(void (*target)(void));

static u32 sealed_call(SealedCallHook hook, void (*target)(void), u32 unused)
{
    (void)unused;
    return hook(target);
}

u32 OV230_CallSealedBody0(SealedCallHook hook, void (*target)(void), u32 unused)
{
    return sealed_call(hook, target, unused);
}

u32 OV230_CallSealedBody1(SealedCallHook hook, void (*target)(void), u32 unused)
{
    return sealed_call(hook, target, unused);
}

u32 OV230_CallSealedBody2(SealedCallHook hook, void (*target)(void), u32 unused)
{
    return sealed_call(hook, target, unused);
}

u32 OV230_CallSealedBody3(SealedCallHook hook, void (*target)(void), u32 unused)
{
    return sealed_call(hook, target, unused);
}

u32 OV230_CallSealedBody4(SealedCallHook hook, void (*target)(void), u32 unused)
{
    return sealed_call(hook, target, unused);
}

u32 OV230_CallSealedBody5(SealedCallHook hook, void (*target)(void), u32 unused)
{
    return sealed_call(hook, target, unused);
}

/* Wireless/microphone hooks whose DS hardware has no ARM11 equivalent. */
int gpiProcessRecvBuddyMessage(void **connection, char *input)
{
    (void)connection; (void)input;
    return 0;
}

int gpiProcessRecvBuddyStatusInfo(void **connection, char *input)
{
    (void)connection; (void)input;
    return 0;
}

void DWCi_InitReliableQueue(void)
{
}

MICResult MIC_StartLimitedSampling(MICAutoParam *param)
{
    (void)param;
    return MIC_RESULT_FATAL_ERROR;
}

MICResult MIC_StartLimitedSamplingAsync(MICAutoParam *param,
                                        MICCallback callback, void *arg)
{
    (void)param; (void)callback; (void)arg;
    return MIC_RESULT_FATAL_ERROR;
}

MICResult MIC_StopLimitedSampling(void)
{
    return MIC_RESULT_FATAL_ERROR;
}

/*
 * Big-integer limb readers from ov009/biginteger.c:305-333. The originals are
 * Metrowerks asm reading halfwords before the pointer; the C forms below
 * assemble the same 64-bit values from those limbs.
 */
unsigned long long get48bits_2(unsigned short *s)
{
    return ((unsigned long long)s[0] << 32) | ((unsigned long long)s[-1] << 16);
}

unsigned long long get48bits_3(unsigned short *s)
{
    return ((unsigned long long)s[0] << 32) |
           ((unsigned long long)s[-1] << 16) | (unsigned long long)s[-2];
}

unsigned long long get64bits(unsigned short *s)
{
    return ((unsigned long long)s[0] << 48) |
           ((unsigned long long)s[-1] << 32) |
           ((unsigned long long)s[-2] << 16) | (unsigned long long)s[-3];
}

/* MSL runtime helpers the DS got from cw_runtime / runtime.a. */
void *__builtin__clear(void *dest, unsigned long size)
{
    return memset(dest, 0, size);
}

unsigned long long _ull_mul(unsigned long long a, unsigned long long b)
{
    return a * b;
}

double _dsqrt(double x)
{
    return sqrt(x);
}

/*
 * Console identity. OS_IsRunOnTwl reads SCFG_ROM at 0x04004000
 * (arm9/os_emulator.c:77-84), which is not mapped on ARM11. unowa is not a
 * TWL console, so answer that directly. sinjoh's libnitro.a provides
 * OS_GetConsoleType, but its constant 0x82000001 has the 0x03000000 bits set
 * that gfl_use.c:727 uses to enter a TWL-only SCFG block, so the game's calls
 * are redirected to this non-TWL value (arm9/os_emulator.c:61).
 */
u32 unowa_port_OS_GetConsoleType(void)
{
    return 0x80000001;
}

int OS_IsRunOnTwl(void)
{
    return 0;
}

int OSi_IsRunOnTwl(void)
{
    return 0;
}

/*
 * The ROM's Wi-Fi device table is a row of DS Thumb function pointers into
 * ov009, which is not linked. GFL_NET_SetupHeaps calls slot 0 (initLibrary)
 * during boot, so a table of inert stubs keeps that path safe. The network
 * features themselves are outside M2; gen_data_image.py excludes the ROM
 * table so these pointers are not mistaken for callable addresses.
 */
static int unowa_net_stub(void)
{
    return 0;
}

/* GFLNetModeOps is 51 word slots (net_types.h:100). */
int (*volatile _wifiDeviceTable[51])(void) = {
    [0 ... 50] = unowa_net_stub,
};

/*
 * Backing store for the Gen V SDK's "extra heap", whose source hardcodes the
 * DS main-RAM base 0x02000000. sinjoh's arena starts at NitroPort_MainMem[0],
 * so using the shadow array there makes the 0x4000-byte extra heap overwrite
 * the arena records. A dedicated buffer keeps the two regions disjoint.
 */
unsigned char unowa_extra_heap[0x4000];

/*
 * Overlay-load trace for M2 acceptance. GF_OVERLAY_Load calls this once per
 * load; the array lives in ordinary RAM so a debugger can read it even though
 * NitroPort_Log's stderr is not captured by the emulator.
 */
unsigned int unowa_overlay_log[64];
unsigned int unowa_overlay_log_count;

void unowa_log_overlay(int handle)
{
    unsigned int index = unowa_overlay_log_count;
    if (index < sizeof(unowa_overlay_log) / sizeof(unowa_overlay_log[0])) {
        unowa_overlay_log[index] = (unsigned int)handle;
        unowa_overlay_log_count = index + 1;
    }
}

/*
 * Unsupported-service trace for M2 acceptance. sinjoh's port_log.c:37 writes
 * to stderr, which LOTUS does not capture, so the final link wraps that symbol
 * here. Fixed-size slots keep the read side a plain byte dump: slot i starts
 * at i * UNOWA_UNIMPLEMENTED_NAME_BYTES and only indices below the count are
 * valid. __real_ keeps the original once-per-name dedup and return value.
 */
#define UNOWA_UNIMPLEMENTED_SLOTS 64
#define UNOWA_UNIMPLEMENTED_NAME_BYTES 32

char unowa_unimplemented_log_names[UNOWA_UNIMPLEMENTED_SLOTS][UNOWA_UNIMPLEMENTED_NAME_BYTES];
unsigned int unowa_unimplemented_log_count;

extern int __real_NitroPort_Unimplemented(const char *what);

int __wrap_NitroPort_Unimplemented(const char *what)
{
    static const char null_name[] = "<null>";
    const char *name = (what != NULL) ? what : null_name;
    unsigned int index = unowa_unimplemented_log_count;
    int result;

    if (index < UNOWA_UNIMPLEMENTED_SLOTS) {
        unsigned int i;
        for (i = 0; i < UNOWA_UNIMPLEMENTED_NAME_BYTES - 1; i++) {
            if (name[i] == '\0') {
                break;
            }
            unowa_unimplemented_log_names[index][i] = name[i];
        }
        unowa_unimplemented_log_names[index][i] = '\0';
        unowa_unimplemented_log_count = index + 1;
    }

    result = __real_NitroPort_Unimplemented(name);
    return result;
}

/*
 * Aletheia's PM_Init initialises the Gen V PM state at g_02153000 and
 * registers its reply callback. sinjoh's os_init.o calls sinjoh's PM_Init
 * instead, so the game's PMi_WaitBusy spins on a lock nobody clears. PM_Init
 * cannot simply be called here: its static inline OS_GetVBlankCount reads the
 * unmapped DS address 0x02FFFC3C. Initialise the documented state directly.
 * Layout from arm9/pm.c: isInit +0x00, work.lock +0x30, work.callback +0x34,
 * work.callbackArg +0x38, work.work +0x3c.
 */
extern void unowa_gv_PMi_CommonCallback(int tag, unsigned int data, int err);
extern void PXI_SetFifoRecvCallback(int tag, void (*callback)(int, unsigned int, int));
extern unsigned char g_02153000[];

void unowa_pm_ensure_ready(void)
{
    unsigned char *pm = g_02153000;

    if (pm[0] != 0) {
        return;
    }
    pm[0] = 1;
    memset(pm + 0x30, 0, 16);
    PXI_SetFifoRecvCallback(8, unowa_gv_PMi_CommonCallback);
}

/*
 * 3DS HID -> DS pad and touch, once per emulated V-blank.
 *
 * sinjoh's weak NitroPort_Halt (port_os.c:396) calls NitroPort_OnFrame, but
 * its weak definition is empty; the strong one that forwards input lives in
 * kohjoh's runtime (kj_frame.c:153) and is not linked here. Without an
 * override the game never sees a button press and cannot leave its title
 * screen, so an overlay-load trace would stay empty for the wrong reason.
 *
 * libctru's prototypes are declared locally instead of by including <3ds.h>:
 * its u32 is uint32_t, which conflicts with the game's u32 (unsigned int).
 * The ABI is identical for all three calls, so fixed-width declarations are
 * safe and keep this translation unit's include set unchanged.
 */
extern void hidScanInput(void);
extern unsigned int hidKeysHeld(void);
extern void hidTouchRead(unsigned short *position);

/* sinjoh's virtual ARM7 input sinks (port_boot.c, port_tp.c). */
extern void NitroPort_SetButtons(unsigned short held, int lid_closed);
extern void NitroPort_SetTouch(int down, unsigned short x, unsigned short y);

/* libctru key bits (3ds/services/hid.h:10-35). */
enum {
    UNOWA_KEY_DRIGHT = 1u << 4,
    UNOWA_KEY_DLEFT = 1u << 5,
    UNOWA_KEY_DUP = 1u << 6,
    UNOWA_KEY_DDOWN = 1u << 7,
    UNOWA_KEY_TOUCH = 1u << 20,
    UNOWA_KEY_CPAD_RIGHT = 1u << 28,
    UNOWA_KEY_CPAD_LEFT = 1u << 29,
    UNOWA_KEY_CPAD_UP = 1u << 30,
    UNOWA_KEY_CPAD_DOWN = 1u << 31,
};

/* NitroSDK PAD bits (nitro/spi/ARM9/pad.h); bits 0..11 match libctru's. */
enum {
    UNOWA_DS_RIGHT = 0x0010,
    UNOWA_DS_LEFT = 0x0020,
    UNOWA_DS_UP = 0x0040,
    UNOWA_DS_DOWN = 0x0080,
};

/* M2 presentation: prove the frame loop reaches the display before the G2D/
 * PICA game bridge exists. The renderer is a clear-only path (renderer.c:1-8);
 * the alternating shades match the M0 loop (engine/app/main.c:108). The count
 * lets the emulator confirm NitroPort_OnFrame is reached without a screenshot.
 */
extern int unowa_renderer_present(unsigned int top_color, unsigned int bottom_color);
unsigned int unowa_present_count;

void NitroPort_OnFrame(void)
{
    hidScanInput();
    const unsigned int held = hidKeysHeld();

    unsigned short keys = (unsigned short)(held & 0x0FFFu);
    if (held & UNOWA_KEY_CPAD_RIGHT) {
        keys |= UNOWA_DS_RIGHT;
    }
    if (held & UNOWA_KEY_CPAD_LEFT) {
        keys |= UNOWA_DS_LEFT;
    }
    if (held & UNOWA_KEY_CPAD_UP) {
        keys |= UNOWA_DS_UP;
    }
    if (held & UNOWA_KEY_CPAD_DOWN) {
        keys |= UNOWA_DS_DOWN;
    }
    NitroPort_SetButtons(keys, 0);

    if (held & UNOWA_KEY_TOUCH) {
        unsigned short position[2] = { 0, 0 };
        hidTouchRead(position);
        unsigned int x = (unsigned int)position[0] * 4u / 5u;
        unsigned int y = (unsigned int)position[1] * 4u / 5u;
        if (x > 255u) {
            x = 255u;
        }
        if (y > 191u) {
            y = 191u;
        }
        NitroPort_SetTouch(1, (unsigned short)x, (unsigned short)y);
    } else {
        NitroPort_SetTouch(0, 0, 0);
    }

    static const unsigned int s_shades[2] = { 0x101830FFu, 0x1B2A4AFFu };
    unowa_renderer_present(s_shades[unowa_present_count & 1u], 0x181018FFu);
    unowa_present_count++;
}

/* DS ROM read-protection check (fs_proc_rom.h:10); unowa reads from SD. */
int FSi_IsUnreadableRomOffset(void *arc, u32 offset)
{
    (void)arc; (void)offset;
    return 0;
}

/* RC4 state is documented in arm9/rc4.h: i, j, then the 256-byte S box. */
void CRYPTO_RC4Encrypt(CRYPTORC4Context *context, const void *in,
                       u32 length, void *out)
{
    const unsigned char *src = (const unsigned char *)in;
    unsigned char *dst = (unsigned char *)out;
    unsigned int i = context->i;
    unsigned int j = context->j;
    unsigned int n;

    for (n = 0; n < length; n++) {
        unsigned char tmp;
        i = (i + 1) & 0xFF;
        j = (j + context->s[i]) & 0xFF;
        tmp = context->s[i];
        context->s[i] = context->s[j];
        context->s[j] = tmp;
        dst[n] = src[n] ^ context->s[(context->s[i] + context->s[j]) & 0xFF];
    }
    context->i = (unsigned char)i;
    context->j = (unsigned char)j;
}

/* SHA-1 context layout from arm9/dgt_sha1.c:24-35. */
typedef struct Sha1View {
    u32 h[5];
    unsigned char block[64];
    u32 pool;
    u32 blocks_low;
    u32 blocks_high;
} Sha1View;

static u32 sha1_rotl(u32 value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

static u32 sha1_load_be(const unsigned char *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
           ((u32)p[2] << 8) | (u32)p[3];
}

/*
 * Aletheia declares this as an extern function pointer whose ROM value is the
 * raw DS address 0x02081e7c (MATHi_SHA1Transform). An ARM11 process cannot
 * call that address, so gen_data_image.py excludes it from the data image and
 * the port defines it here. The initialiser also keeps the C transform
 * reachable under -Wl,--gc-sections: MATH_SHA1Update calls through this
 * pointer, while MATHi_SetOverlayTableMode (the transform's only direct
 * reference) is not linked.
 */
void MATHi_SHA1Transform(MATHSHA1Context *context);
void (*MATHi_SHA1ProcessMessageBlockFunc)(MATHSHA1Context *context) =
    MATHi_SHA1Transform;

void MATHi_SHA1Transform(MATHSHA1Context *context)
{
    Sha1View *view = (Sha1View *)context;
    u32 w[80];
    u32 a = view->h[0], b = view->h[1], c = view->h[2];
    u32 d = view->h[3], e = view->h[4];
    int i;

    /* The DS asm byte-swaps each word (arm9/sha1.c:39-43). */
    for (i = 0; i < 16; i++) {
        w[i] = sha1_load_be(view->block + i * 4);
    }
    for (i = 16; i < 80; i++) {
        w[i] = sha1_rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    for (i = 0; i < 80; i++) {
        u32 f, k;
        u32 temp;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        temp = sha1_rotl(a, 5) + f + e + k + w[i];
        e = d; d = c; c = sha1_rotl(b, 30); b = a; a = temp;
    }
    view->h[0] += a;
    view->h[1] += b;
    view->h[2] += c;
    view->h[3] += d;
    view->h[4] += e;
}

/*
 * The DSi BIOS SHA1 SVCs (swi 0x24-0x27 in twl/sha1_hmac.c:29-54) are replaced
 * by the software SHA1 above. The state fits the 0x64-byte state[] field of
 * Sha1HmacCtx (twl/sha1_hmac.c:58-62).
 */
static void sha1_core_init(Sha1View *view)
{
    view->h[0] = 0x67452301;
    view->h[1] = 0xEFCDAB89;
    view->h[2] = 0x98BADCFE;
    view->h[3] = 0x10325476;
    view->h[4] = 0xC3D2E1F0;
    view->pool = 0;
    view->blocks_low = 0;
    view->blocks_high = 0;
}

static void sha1_core_update(Sha1View *view, const unsigned char *data, u32 length)
{
    while (length != 0) {
        u32 rest = 64 - view->pool;
        if (rest > length) {
            rest = length;
        }
        memcpy(view->block + view->pool, data, rest);
        view->pool += rest;
        data += rest;
        length -= rest;
        if (view->pool == 64) {
            MATHi_SHA1Transform((MATHSHA1Context *)view);
            view->pool = 0;
        }
    }
}

static void sha1_core_final(Sha1View *view, unsigned char *digest)
{
    unsigned long long bits = ((unsigned long long)view->blocks_high << 32) |
                              view->blocks_low;
    unsigned char pad[72];
    u32 padded;
    int i;

    bits += (unsigned long long)view->pool * 8;
    pad[0] = 0x80;
    padded = 1;
    while ((view->pool + padded) % 64 != 56) {
        pad[padded++] = 0;
    }
    for (i = 0; i < 8; i++) {
        pad[padded++] = (unsigned char)(bits >> (56 - i * 8));
    }
    sha1_core_update(view, pad, padded);
    for (i = 0; i < 5; i++) {
        digest[i * 4] = (unsigned char)(view->h[i] >> 24);
        digest[i * 4 + 1] = (unsigned char)(view->h[i] >> 16);
        digest[i * 4 + 2] = (unsigned char)(view->h[i] >> 8);
        digest[i * 4 + 3] = (unsigned char)view->h[i];
    }
}

void SHA1_HMAC_Init(void *ctx)
{
    sha1_core_init((Sha1View *)ctx);
}

void SHA1_HMAC_Update(void *ctx, const void *data, u32 len)
{
    sha1_core_update((Sha1View *)ctx, (const unsigned char *)data, len);
}

void SHA1_HMAC_GetHash(void *ctx, void *digest)
{
    sha1_core_final((Sha1View *)ctx, (unsigned char *)digest);
}

void SHA1_HMAC_HashRange(void *dst, const void *src, u32 len)
{
    Sha1View view;
    sha1_core_init(&view);
    sha1_core_update(&view, (const unsigned char *)src, len);
    sha1_core_final(&view, (unsigned char *)dst);
}

void SHA1_HMAC_Calc(unsigned char *digest, const void *data, u32 data_len,
                    const void *key, u32 key_len)
{
    unsigned char block[64];
    unsigned char inner[20];
    unsigned char ipad[64];
    unsigned char opad[64];
    Sha1View view;
    u32 i;

    if (key_len > sizeof(block)) {
        SHA1_HMAC_HashRange(block, key, key_len);
        key_len = 20;
    } else {
        memcpy(block, key, key_len);
    }
    for (i = 0; i < 64; i++) {
        ipad[i] = (i < key_len ? block[i] : 0) ^ 0x36;
        opad[i] = (i < key_len ? block[i] : 0) ^ 0x5C;
    }
    sha1_core_init(&view);
    sha1_core_update(&view, ipad, 64);
    sha1_core_update(&view, (const unsigned char *)data, data_len);
    sha1_core_final(&view, inner);
    sha1_core_init(&view);
    sha1_core_update(&view, opad, 64);
    sha1_core_update(&view, inner, 20);
    sha1_core_final(&view, digest);
}

u32 bw_limit_get(int id)
{
    switch (id) {
    case BW_LIMIT_ITEM_LAST:
        return 626;
    case BW_LIMIT_MOVE_LAST:
        return 559;
    case BW_LIMIT_BTLV_EFFECT_MOVE_END:
        return 0x231;
    case BW_LIMIT_FLDMMDL_OBJCODE_TABLE_COUNT:
        return 799;
    case BW_LIMIT_SPECIES_LAST:
        return 649;
    default:
        return 0;
    }
}

/*
 * Address-valued linker symbols. Sources of the values:
 *   SDK_IRQ_STACKSIZE           0x00000800  arm9/os_arena.c:26
 *   SDK_AUTOLOAD_MAIN_TWL_START 0x02FE0000  arm9/gf_overlay.c:29
 *   abs_id_0                    0x00000000  ov029/c_gear.c:187
 *   ov020_id                    0x00000014  src/matched/ov/ov010_func_0215FA00.c:13
 */
__asm__(".global SDK_IRQ_STACKSIZE\n"
        ".set SDK_IRQ_STACKSIZE, 0x800\n"
        ".global SDK_AUTOLOAD_MAIN_TWL_START\n"
        ".set SDK_AUTOLOAD_MAIN_TWL_START, 0x02FE0000\n"
        ".global abs_id_0\n"
        ".set abs_id_0, 0\n"
        ".global ov020_id\n"
        ".set ov020_id, 0x14\n");

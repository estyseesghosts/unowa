/*
 * unowa_frame.c: the DS side of the frame boundary, for the 3DS renderer.
 *
 * Compiled as a 3DS-side translation unit (short enums, libctru ABI) but
 * linking against sinjoh's emulated DS memory. It exposes exactly what
 * kj_bridge.h promises: the read-only DS memory view and one coherent
 * kj_frame_input per presented frame. The game thread is parked in
 * NitroPort_OnFrame when this runs, so the shadows are consistent.
 *
 * This adapts sinjoh engine/runtime/kj_frame.c:83-146 (GPL-3.0): the memory
 * view, the VRAMCNT bank decode and the per-line register handoff transfer;
 * HGSS's two-VBlank publish cadence and its game-specific input policy do
 * not. unowa publishes from NitroPort_OnFrame because that is the proven
 * boundary on this port (docs/status.md, "The frame bridge").
 */

#include "runtime/kj_bridge.h"
#include "runtime/kj_g3.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* sinjoh's emulated DS memory (engine/compat/nitro/src/port_memory.c). These
 * are the same arrays the port's DS I/O header redirects every register,
 * palette, VRAM and OAM view to. */
extern unsigned char NitroPort_IoReg[];
extern unsigned char NitroPort_Pltt[];
extern unsigned char NitroPort_Oam[];
extern unsigned char NitroPort_BgVram[];
extern unsigned char NitroPort_DbBgVram[];
extern unsigned char NitroPort_ObjVram[];
extern unsigned char NitroPort_DbObjVram[];
extern unsigned char NitroPort_LcdcVram[];

/* sinjoh's per-scanline register shadow (port_os.c:120). The H-blank walk
 * fills it when the game changes registers line by line (the intro's wipes
 * do); otherwise `active` is 0 and one span uses the frame registers. */
#define UNOWA_LINE_REGS 0x70
typedef struct UnowaLineRegs {
    int active;
    uint32_t serial;
    uint8_t regs[2][192][UNOWA_LINE_REGS];
} UnowaLineRegs;
extern UnowaLineRegs NitroPort_LineRegs;

static kj_ds_memory s_memory;

const kj_ds_memory *kj_ds_get_memory(void)
{
    if (s_memory.io == NULL) {
        s_memory.io = NitroPort_IoReg;
        s_memory.io_size = 0x00110000;
        s_memory.pltt = NitroPort_Pltt;
        s_memory.oam = NitroPort_Oam;
        s_memory.bg_vram_a = NitroPort_BgVram;
        s_memory.bg_vram_b = NitroPort_DbBgVram;
        s_memory.obj_vram_a = NitroPort_ObjVram;
        s_memory.obj_vram_b = NitroPort_DbObjVram;
        s_memory.lcdc_vram = NitroPort_LcdcVram;
    }
    return &s_memory;
}

/* The nine LCDC banks, as sinjoh's kj_frame.c:72-110 decodes them: bank
 * sizes and their fixed place in the LCDC address space, VRAMCNT_A..I at
 * 0x240..0x246 and 0x248/0x249. */
static const uint32_t BANK_LCDC_OFFSET[9] = {
    0x00000, 0x20000, 0x40000, 0x60000, 0x80000, 0x90000, 0x94000, 0x98000, 0xA0000,
};
static const uint32_t BANK_SIZE[9] = {
    0x20000, 0x20000, 0x20000, 0x20000, 0x10000, 0x4000, 0x4000, 0x8000, 0x4000,
};
static const uint16_t BANK_REG[9] = {
    0x240, 0x241, 0x242, 0x243, 0x244, 0x245, 0x246, 0x248, 0x249,
};

static void decode_banks(kj_vram_bank banks[9])
{
    for (int i = 0; i < 9; ++i) {
        const uint8_t cnt = NitroPort_IoReg[BANK_REG[i]];

        banks[i].lcdc_offset = BANK_LCDC_OFFSET[i];
        banks[i].size = BANK_SIZE[i];
        banks[i].enabled = (cnt & 0x80) != 0;
        banks[i].mst = cnt & 0x07;
        banks[i].ofs = (cnt >> 3) & 0x03;
    }
}

void unowa_frame_input(kj_frame_input *in, uint32_t vblanks, uint32_t game_frame)
{
    memset(in, 0, sizeof(*in));
    in->mem = kj_ds_get_memory();
    decode_banks(in->banks);
    in->lines.active = NitroPort_LineRegs.active != 0;
    in->lines.regs = in->lines.active
                         ? (const uint8_t (*)[192][UNOWA_LINE_REGS])NitroPort_LineRegs.regs
                         : NULL;
    in->g3 = kj_g3_take_frame(); /* NULL until the first 3D swap */
    in->vblank_count = vblanks;
    in->game_frame = game_frame;
    /* POWCNT1 (0x04000304) bit 15: engine A drives the upper screen. */
    in->engine_a_on_top = (NitroPort_IoReg[0x305] & 0x80) != 0;
}

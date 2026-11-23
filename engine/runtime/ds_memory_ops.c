/*
 * DS pointers passed to the SDK's block clear and copy helpers.
 *
 * The game names DS memory by address: `HW_LCDC_VRAM` is 0x06800000
 * (nitro/hw/common/lcd.h), `HW_BG_VRAM` 0x06000000, `HW_OBJ_VRAM` 0x06400000.
 * Those names come from NitroSDK headers, so unlike `IOREG_ADDR(...)` in the
 * game's own sdk_ioreg.h they are not redirected by the generated header
 * (tools/gen_port_ioreg_header.py). A call such as
 *
 *     MIi_CpuClearFast(0, (void *)HW_LCDC_VRAM, 0xA4000);   (bg_sys.c:3438)
 *
 * therefore hands a raw DS address to a libnitro loop, and the ARM11 process
 * maps nothing there: the field-map start (ov020/event_mapchange.c:776, the
 * same clear while the map change event builds its screen) stopped on the
 * first word with an unmapped write to 0x06800000.
 *
 * sinjoh already keeps every DS region in a shadow array (port_ioreg.c,
 * port_memory.c); engine/runtime/flat_memory.c maps those arrays with
 * unowa_ds_to_host. Translating the pointer arguments here fixes every call
 * site at once, including the ones the game has not reached yet (ov071's
 * monolith tool, ov109's idle screen, ov174's local TV and ov205's save
 * screen all clear HW_BG_VRAM/HW_OBJ_VRAM the same way).
 *
 * A host pointer (anything outside the DS regions) passes through unchanged,
 * so the port's own calls to these helpers keep working.
 */
#include "runtime/flat_memory.h"

#include <stddef.h>
#include <stdint.h>

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

static void *to_host(void *pointer)
{
    void *mapped = unowa_ds_to_host((uint32_t)(uintptr_t)pointer);
    return mapped != NULL ? mapped : pointer;
}

static const void *to_host_const(const void *pointer)
{
    const void *mapped = unowa_ds_to_host((uint32_t)(uintptr_t)pointer);
    return mapped != NULL ? mapped : pointer;
}

void __real_MIi_CpuClearFast(u32 data, void *destp, u32 size);
void __real_MIi_CpuClear32(u32 data, void *destp, u32 size);
void __real_MIi_CpuClear16(u16 data, void *destp, u32 size);
void __real_MIi_CpuCopyFast(const void *srcp, void *destp, u32 size);
void __real_MIi_CpuCopy32(const void *srcp, void *destp, u32 size);
void __real_MIi_CpuCopy16(const void *srcp, void *destp, u32 size);
void __real_MI_CpuFill8(void *dest, u8 data, u32 size);
void __real_MI_CpuCopy8(const void *src, void *dest, u32 size);

void __wrap_MIi_CpuClearFast(u32 data, void *destp, u32 size)
{
    __real_MIi_CpuClearFast(data, to_host(destp), size);
}

void __wrap_MIi_CpuClear32(u32 data, void *destp, u32 size)
{
    __real_MIi_CpuClear32(data, to_host(destp), size);
}

void __wrap_MIi_CpuClear16(u16 data, void *destp, u32 size)
{
    __real_MIi_CpuClear16(data, to_host(destp), size);
}

void __wrap_MIi_CpuCopyFast(const void *srcp, void *destp, u32 size)
{
    __real_MIi_CpuCopyFast(to_host_const(srcp), to_host(destp), size);
}

void __wrap_MIi_CpuCopy32(const void *srcp, void *destp, u32 size)
{
    __real_MIi_CpuCopy32(to_host_const(srcp), to_host(destp), size);
}

void __wrap_MIi_CpuCopy16(const void *srcp, void *destp, u32 size)
{
    __real_MIi_CpuCopy16(to_host_const(srcp), to_host(destp), size);
}

void __wrap_MI_CpuFill8(void *dest, u8 data, u32 size)
{
    __real_MI_CpuFill8(to_host(dest), data, size);
}

void __wrap_MI_CpuCopy8(const void *src, void *dest, u32 size)
{
    __real_MI_CpuCopy8(to_host_const(src), to_host(dest), size);
}

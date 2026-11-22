# unowa M1: compile the Aletheia Gen V game source for the 3DS ARM11.
#
#   make -f engine/game.mk TARGET=3ds lib       # libunowa_game.a
#   make -f engine/game.mk verify-pins          # check external revisions
#
# Inputs are external checkouts pinned in upstream.lock. No Aletheia source
# and no Eidos code is copied into this repository. The NitroSDK/NitroSystem
# compatibility layers come from sinjoh (GPL-3.0), the proven Gen IV port.
#
# The compiler flags below are taken from sinjoh engine/compat/common.mk
# lines 34-55 (SDK defines and C dialect) and 75-91 (include paths and ARM11
# flags). Keeping them here makes unowa's build inputs explicit and reviewable.

.DEFAULT_GOAL := lib

ALETHEIA_DIR ?= ../aletheia/aletheia
SINJOH_DIR   ?= ../sinjoh
GAME         := $(ALETHEIA_DIR)/src/game
NITRO        := $(SINJOH_DIR)/engine/compat/nitro
NNS          := $(SINJOH_DIR)/engine/compat/nitrosystem
SDK          := $(SINJOH_DIR)/build/NitroSDK
NNS_SDK      := $(SINJOH_DIR)/build/NitroSystem

EXPECTED_ALETHEIA := 50731dd
EXPECTED_SINJOH   := 1774be6
# Aletheia docs/ROMS.md:12-13, No-Intro SHA-1 for the USA/Europe dumps.
BLACK_SHA1 := 26ad0b9967aa279c4a266ee69f52b9b2332399a5
WHITE_SHA1 := bc696a0dfb448c7b3a8a206f0f8214411a039208

.PHONY: verify-pins verify-rom lib data-image link-probe link-probe-real link-final clean
verify-pins:
	@test "$$(git -C $(ALETHEIA_DIR) rev-parse --short=7 HEAD)" = "$(EXPECTED_ALETHEIA)" \
	    || { echo "Aletheia checkout is not $(EXPECTED_ALETHEIA)"; exit 1; }
	@test "$$(git -C $(SINJOH_DIR) rev-parse --short=7 HEAD)" = "$(EXPECTED_SINJOH)" \
	    || { echo "Sinjoh checkout is not $(EXPECTED_SINJOH)"; exit 1; }
	@echo "pins verified: aletheia $(EXPECTED_ALETHEIA), sinjoh $(EXPECTED_SINJOH)"

# The ROM is a user-supplied input. Verify its identity before deriving data
# from it; Aletheia docs/ROMS.md documents the pinned hashes.
verify-rom:
	@test "$$(sha1sum $(ALETHEIA_DIR)/black.nds | cut -d' ' -f1)" = "$(BLACK_SHA1)" \
	    || { echo "black.nds does not match the pinned SHA-1"; exit 1; }
	@test "$$(sha1sum $(ALETHEIA_DIR)/white.nds | cut -d' ' -f1)" = "$(WHITE_SHA1)" \
	    || { echo "white.nds does not match the pinned SHA-1"; exit 1; }
	@echo "ROMs verified against Aletheia docs/ROMS.md"

ifeq ($(wildcard $(GAME)/arm9/gfl_use.c),)
$(error Aletheia source not found at $(GAME); set ALETHEIA_DIR)
endif
ifeq ($(wildcard $(SDK)/include/nitro.h),)
$(error NitroSDK not found at $(SDK); set SINJOH_DIR to a bootstrapped sinjoh checkout)
endif

OUT := build/game/3ds
LIB := $(OUT)/libunowa_game.a

# M2/M3 presentation. The M0 GPU renderer and its log/storage dependencies are
# compiled with the root Makefile's ARM11 flags (see Makefile:58-68) and linked
# into the test ELF. The test entry initialises the renderer; the strong
# NitroPort_OnFrame in port_stubs.c presents one clear picture per V-blank. This
# proves the frame loop reaches the display; the G2D/PICA game bridge is M3/M4.
ENGINE_SRCS := engine/graphics/renderer.c engine/diagnostics/log.c \
               engine/storage/paths.c
ENGINE_OBJS := $(patsubst %.c,$(OUT)/obj/%.o,$(ENGINE_SRCS))
ENGINE_CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter \
                 -ffunction-sections -fdata-sections -march=armv6k -mtune=mpcore \
                 -mfpu=vfp -mfloat-abi=hard -mtp=soft -mword-relocations \
                 -fno-short-enums -D__3DS__ -Iengine -I$(DEVKITPRO)/libctru/include

BLOCKED := $(strip $(shell grep -v '^#' $(dir $(lastword $(MAKEFILE_LIST)))game_blocklist.txt | grep -v '^$$'))
BLOCKED_OVERLAY := $(strip $(shell grep -v '^#' $(dir $(lastword $(MAKEFILE_LIST)))game_overlay_blocklist.txt | grep -v '^$$'))
GAME_CORE := $(wildcard $(GAME)/arm9/*.c) $(wildcard $(GAME)/arm9_autoload0_ITCM/*.c)
GAME_OVERLAYS := $(filter-out $(GAME_CORE),$(wildcard $(GAME)/*/*.c))
GAME_ALL := $(GAME_CORE) $(GAME_OVERLAYS)
GAME_SRCS := $(filter-out $(addprefix $(GAME)/,$(BLOCKED) $(BLOCKED_OVERLAY)),$(GAME_ALL))
GAME_OBJS := $(patsubst $(GAME)/%.c,$(OUT)/obj/%.o,$(GAME_SRCS))
DEPS     := $(GAME_OBJS:.o=.d)

# Four TUs use Metrowerks `asm { clz x, x }`, which devkitARM GCC cannot parse.
# tools/patch_sources.py rewrites that one pattern to __builtin_clzl in a
# build-tree copy; the pinned Aletheia checkout is never modified. The object
# names are identical, so the rest of the closure sees the original functions.
PATCHED := ov009/wcm_wmif.c ov013/vct_audio.c twl/nwmif.c \
           twl/dsp_pipe.c arm9/scfg_proc.c ov094/btlv_input.c \
           ov119/pokemontrade_2d.c twl/pm.c arm9/gf_overlay.c
PATCHED_OBJS := $(patsubst %.c,$(OUT)/obj/%.o,$(PATCHED))
NORMAL_OBJS  := $(filter-out $(PATCHED_OBJS),$(GAME_OBJS))

# Gen V SDK functions that sinjoh's Gen IV libs do not provide. These TUs are
# in game_blocklist.txt because some of their symbols collide with those libs.
# tools/rename_genv.py prefixes just the colliding names in both the game and
# the Gen V objects, so the Gen V implementation is selected without a duplicate
# definition. The source list is the set that defines symbols missing from the
# normal-archive link; adding more would pull in unsatisfiable game-local refs.
GV_SRCS   := $(strip $(shell grep -v '^#' $(dir $(lastword $(MAKEFILE_LIST)))gv_sdk_sources.txt | grep -v '^$$'))
GV_OBJS   := $(patsubst %.c,$(OUT)/gv_raw/%.o,$(GV_SRCS))
# biginteger.c is C apart from three Metrowerks limb readers, which the port
# supplies in engine/stubs/port_stubs.c. Remove those asm bodies at build time.
GV_PATCHED := ov009/biginteger.c arm9/pm.c arm9/os_alloc.c
GV_PATCHED_OBJS := $(patsubst %.c,$(OUT)/gv_raw/%.o,$(GV_PATCHED))
GV_RAW_OBJS := $(filter-out $(GV_PATCHED_OBJS),$(GV_OBJS))
GV_LIB    := $(OUT)/libunowa_gv.a
GAME_REN  := $(OUT)/libunowa_game_ren.a
REN_DIR   := $(OUT)/ren
REN_DEFS  := $(REN_DIR)/redefs.txt

PORT_DEFINES := -DSDK_ARM9 -DSDK_TS -DSDK_4M -DSDK_CODE_ARM -DSDK_FINALROM \
                -DNNS_FINALROM -DSDK_PORT_3DS -D__3DS__

# Arm11 flags: see sinjoh engine/compat/common.mk lines 49-55 and 90-91.
# -fgnu89-inline and -fno-strict-aliasing recreate Metrowerks semantics;
# -fno-short-enums and -fsigned-char preserve the DS ABI used by the game.
# Every Aletheia TU that names a DS hardware address goes through this header.
# It redirects those addresses to sinjoh's emulated NitroPort_* arrays; see
# tools/gen_port_ioreg_header.py for the region table and source citations.
PORT_IOREG_H := $(OUT)/gen/port_sdk_ioreg.h
PORT_PRELUDE := -include $(PORT_IOREG_H)

PORT_CFLAGS := -std=gnu99 -fgnu89-inline -fno-strict-aliasing -fno-short-enums \
               -fsigned-char -fwrapv -fno-aggressive-loop-optimizations \
               -fno-delete-null-pointer-checks -O2 -g \
               -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces \
               -Wno-unknown-pragmas -Wno-unused-but-set-variable -Wno-parentheses \
               -Wno-switch -Wno-multichar \
               -march=armv6k -mtune=mpcore -mfpu=vfp -mfloat-abi=hard -mtp=soft \
               -mword-relocations -ffunction-sections $(PORT_DEFINES)

SDK_INCLUDES := -I$(NITRO)/include -I$(NITRO)/build/3ds/gen -I$(SDK)/include \
                $(foreach d,$(filter-out %/wl_arm7/include,$(wildcard $(SDK)/libraries/*/include)),-I$(d))
NNS_INCLUDES := -I$(NNS)/include \
                $(foreach d,$(wildcard $(NNS)/libraries/*/include),-I$(d))
GAME_INCLUDES := -I$(GAME)/arm9 -I$(GAME)/arm9_autoload0_ITCM -I$(GAME)/lib \
                 $(addprefix -I,$(patsubst %/,%,$(wildcard $(GAME)/*/)))

ifeq ($(strip $(DEVKITPRO)),)
DEVKITPRO := /opt/devkitpro
endif
ifeq ($(strip $(DEVKITARM)),)
DEVKITARM := $(DEVKITPRO)/devkitARM
endif
AR := $(DEVKITARM)/bin/arm-none-eabi-gcc-ar

lib: $(LIB)

# DS data symbols live in the player's ROM, not in Aletheia's source. The
# generator reads the addresses from Aletheia's own headers and the bytes from
# its extracted region images; the output stays in the gitignored build tree.
data-image: $(OUT)/gen/ds_data.o

$(PORT_IOREG_H): $(GAME)/sdk_ioreg.h tools/gen_port_ioreg_header.py
	@mkdir -p $(dir $@)
	@python3 tools/gen_port_ioreg_header.py --source $(GAME) --out $@

$(OUT)/gen/ds_data.o: $(OUT)/probe-undefined.txt
	@mkdir -p $(dir $@)
	@python3 tools/gen_data_image.py --aletheia $(ALETHEIA_DIR) \
	    --undefined $< --out $(OUT)/gen/ds_data.c \
	    --reloc-name unowa_ds_relocate
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -Wno-unused-const-variable \
	    -c $(OUT)/gen/ds_data.c -o $@

# Sinjoh's geometry interpreter is game-agnostic DS hardware work and supplies
# the NitroPort_Gx* hooks the game calls. Its renderer-side kj_ctr_* callbacks
# are not linked yet; they are counted among the remaining undefined symbols.
$(OUT)/gen/kj_g3.o: $(SINJOH_DIR)/engine/runtime/kj_g3.c
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -DHGSS_PROFILE_OFF \
	    -I$(SINJOH_DIR)/engine/runtime -I$(NITRO)/include \
	    -I$(NITRO)/build/3ds/gen -I$(SDK)/include \
	    -c $< -o $@

# Whole-archive merge with sinjoh's compat libraries. This is a link *probe*:
# it first reports undefined symbols, generates the ROM-backed data image for
# the data ones, then reports what a real link would still have to supply.
$(OUT)/probe-undefined.txt: $(LIB)
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-ld -r -o $(OUT)/probe-combined.o \
	    --whole-archive $(LIB) \
	    $(NITRO)/build/3ds/libnitro.a \
	    $(NNS)/build/3ds/libnitrosystem.a --no-whole-archive
	@$(DEVKITARM)/bin/arm-none-eabi-nm $(OUT)/probe-combined.o \
	    | awk '$$1 == "U" { print $$2 }' > $@

link-probe: $(OUT)/probe-undefined.txt
	@mkdir -p $(OUT)/gen
	@echo "undefined before ROM data image: $$(wc -l < $(OUT)/probe-undefined.txt)"
	@$(MAKE) --no-print-directory -f engine/game.mk TARGET=$(TARGET) data-image
	@$(MAKE) --no-print-directory -f engine/game.mk TARGET=$(TARGET) $(OUT)/gen/kj_g3.o
	@$(DEVKITARM)/bin/arm-none-eabi-ld -r -o $(OUT)/probe-combined2.o \
	    --whole-archive $(LIB) \
	    $(NITRO)/build/3ds/libnitro.a \
	    $(NNS)/build/3ds/libnitrosystem.a --no-whole-archive \
	    $(OUT)/gen/ds_data.o $(OUT)/gen/kj_g3.o
	@echo "undefined after ROM data image (whole-archive, conservative):"
	@$(DEVKITARM)/bin/arm-none-eabi-nm $(OUT)/probe-combined2.o | grep -c ' U '

# A normal link only pulls archive members that resolve a reference. A tiny
# root object calling NitroMain gives a closer estimate of what the final
# .3dsx link would still need. The two archives are in a linker group because
# the game and the Gen V SDK call into each other.
$(OUT)/gen/m1_entry.o:
	@mkdir -p $(dir $@)
	@printf 'extern void NitroMain(void);\nvoid m1_entry(void) { NitroMain(); }\n' > $(OUT)/gen/m1_entry.c
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -c $(OUT)/gen/m1_entry.c -o $@

$(OUT)/probe-gv-undefined.txt: $(REN_DIR)/renamed.stamp
	@$(DEVKITARM)/bin/arm-none-eabi-ld -r -o $(OUT)/probe-gv.o \
	    $(REN_DIR)/m1_entry.o --start-group $(GAME_REN) $(GV_LIB) \
	    $(NITRO)/build/3ds/libnitro.a \
	    $(NNS)/build/3ds/libnitrosystem.a --end-group \
	    $(REN_DIR)/kj_g3.o $(REN_DIR)/port_stubs.o
	@$(DEVKITARM)/bin/arm-none-eabi-nm $(OUT)/probe-gv.o \
	    | awk '$$1 == "U" { print $$2 }' > $@

# The Gen V objects introduce data references the game-only probe never saw.
# Extend the full data image with definitions for those additional symbols.
$(OUT)/gen/ds_data_gv.o: $(OUT)/probe-gv-undefined.txt $(OUT)/probe-undefined.txt
	@mkdir -p $(dir $@)
	@LC_ALL=C sort $(OUT)/probe-undefined.txt > $(OUT)/u-base.sorted
	@LC_ALL=C sort $< > $(OUT)/u-gv.sorted
	@LC_ALL=C comm -13 $(OUT)/u-base.sorted $(OUT)/u-gv.sorted > $(OUT)/u-gv-extra.txt
	@python3 tools/gen_data_image.py --aletheia $(ALETHEIA_DIR) \
	    --undefined $(OUT)/u-gv-extra.txt --out $(OUT)/gen/ds_data_gv.c \
	    --reloc-name unowa_gv_relocate
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -Wno-unused-const-variable \
	    -c $(OUT)/gen/ds_data_gv.c -o $@

link-probe-real: $(OUT)/probe-gv-undefined.txt $(OUT)/gen/ds_data.o $(OUT)/gen/ds_data_gv.o
	@$(DEVKITARM)/bin/arm-none-eabi-ld -r -o $(OUT)/probe-real.o \
	    $(REN_DIR)/m1_entry.o --start-group $(GAME_REN) $(GV_LIB) \
	    $(NITRO)/build/3ds/libnitro.a \
	    $(NNS)/build/3ds/libnitrosystem.a --end-group \
	    $(OUT)/gen/ds_data.o $(OUT)/gen/ds_data_gv.o $(REN_DIR)/kj_g3.o \
	    $(REN_DIR)/port_stubs.o
	@echo "undefined after normal-archive link:"
	@$(DEVKITARM)/bin/arm-none-eabi-nm $(OUT)/probe-real.o | grep -c ' U '

# Final link test: a real ARM11 ELF and .3dsx using devkitPro's runtime.
# This is the same library set as the M0 build plus the game closure.
$(OUT)/obj/engine/%.o: engine/%.c
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(ENGINE_CFLAGS) -c $< -o $@

$(OUT)/gen/m1_main.o:
	@mkdir -p $(dir $@)
	@printf 'typedef unsigned int u32;\ntypedef int BOOL;\nextern void NitroMain(void);\nextern void NitroPort_InitArm7Services(void);\nextern void NitroPort_SetArm7Handler(int tag, void (*handler)(int, u32, BOOL));\nextern void unowa_handle_nvram(int tag, u32 data, BOOL err);\nextern const char *kj_ds_open_pack(const char *pak, const char *manifest);\nextern void kj_log(const char *fmt, ...);\nextern void gfxInitDefault(void);\nextern int unowa_renderer_init(void);\nextern void unowa_ds_relocate(void);\nextern void unowa_gv_relocate(void);\nint main(void) { gfxInitDefault(); if (!unowa_renderer_init()) { return 1; } NitroPort_InitArm7Services(); NitroPort_SetArm7Handler(4, unowa_handle_nvram); unowa_ds_relocate(); unowa_gv_relocate(); const char *e = kj_ds_open_pack("sdmc:/3ds/unowa/data/black.pak", "sdmc:/3ds/unowa/data/black.manifest.json"); if (e) { kj_log("[pack] %%s", e); return 2; } NitroMain(); return 0; }\n' > $(OUT)/gen/m1_main.c
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -c $(OUT)/gen/m1_main.c -o $@

$(OUT)/m1-test.elf: $(OUT)/gen/m1_main.o $(OUT)/probe-gv-undefined.txt \
                    $(OUT)/gen/ds_data.o $(OUT)/gen/ds_data_gv.o $(ENGINE_OBJS)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc -march=armv6k -mtune=mpcore \
	    -mfpu=vfp -mfloat-abi=hard -mtp=soft -mword-relocations \
	    -fno-short-enums -D__3DS__ -specs=3dsx.specs \
	    -L$(DEVKITPRO)/libctru/lib -Wl,--gc-sections \
	    -Wl,--no-enum-size-warning -Wl,--wrap=NitroPort_Unimplemented \
	    -Wl,--start-group $< $(GAME_REN) $(GV_LIB) \
	    $(NITRO)/build/3ds/libnitro.a \
	    $(NNS)/build/3ds/libnitrosystem.a \
	    $(OUT)/gen/ds_data.o $(OUT)/gen/ds_data_gv.o $(REN_DIR)/kj_g3.o \
	    $(REN_DIR)/port_stubs.o $(ENGINE_OBJS) -Wl,--end-group \
	    -lcitro3d -lctru -lm -o $@

$(OUT)/m1-test.3dsx: $(OUT)/m1-test.elf
	@$(DEVKITPRO)/tools/bin/3dsxtool $< $@
	@echo "built $@"

link-final: $(OUT)/m1-test.3dsx

$(LIB): $(GAME_OBJS)
	@mkdir -p $(dir $@)
	@echo "  AR  $@ ($(words $(GAME_OBJS)) objects)"
	@$(file >$(OUT)/objects.rsp,$(GAME_OBJS))
	@$(AR) rcs $@ @$(OUT)/objects.rsp

# Patched build-tree sources. Call the patcher once per file while the build
# is small; the static pattern rule below picks up incremental rebuilds too.
$(OUT)/src/%.c: $(GAME)/%.c
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/src $*.c

# SCFG_SetCpuSpeed reads the DS SCFG_CLK register (arm9/scfg_proc.c:66), which
# is not mapped on ARM11. Replace that one read with a neutral value so the
# function keeps its shape but never touches DS I/O space.
$(OUT)/src/arm9/scfg_proc.c: $(GAME)/arm9/scfg_proc.c
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/src \
	    --fixup scfg_no_scfgclk arm9/scfg_proc.c

# The shadow-array address for OBJ VRAM is not an integer constant expression,
# so these two files cannot use it as an enum enumerator.
# Report each overlay load through sinjoh's port log.
$(OUT)/src/arm9/gf_overlay.c: $(GAME)/arm9/gf_overlay.c
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/src \
	    --fixup overlay_load_log arm9/gf_overlay.c

# NDMA destination-region case label, not an address: see PORT_FIXUPS.
$(OUT)/src/twl/pm.c: $(GAME)/twl/pm.c
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/src \
	    --fixup pm_ndma_case twl/pm.c

$(OUT)/src/ov094/btlv_input.c: $(GAME)/ov094/btlv_input.c
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/src \
	    --fixup vram_enum_base ov094/btlv_input.c

$(OUT)/src/ov119/pokemontrade_2d.c: $(GAME)/ov119/pokemontrade_2d.c
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/src \
	    --fixup vram_enum_base ov119/pokemontrade_2d.c

$(PATCHED_OBJS): $(OUT)/obj/%.o: $(OUT)/src/%.c | $(PORT_IOREG_H)
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) $(PORT_PRELUDE) \
	    -I$(GAME)/arm9 -I$(GAME) $(SDK_INCLUDES) \
	    $(NNS_INCLUDES) $(GAME_INCLUDES) -MMD -MP -MQ"$@" -c $< -o $@

$(NORMAL_OBJS): $(OUT)/obj/%.o: $(GAME)/%.c | $(PORT_IOREG_H)
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) $(PORT_PRELUDE) $(SDK_INCLUDES) \
	    $(NNS_INCLUDES) $(GAME_INCLUDES) -MMD -MP -MQ"$@" -c $< -o $@

# Port-provided values: linker-symbol constants and the BW_LIMIT table.
$(OUT)/gen/port_stubs.o: engine/stubs/port_stubs.c
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -I$(GAME) -c $< -o $@

# Gen V SDK objects, built with the same flags as the game archive.
$(OUT)/gv_src/%.c: $(GAME)/%.c
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/gv_src \
	    --remove-asm get48bits_2 --remove-asm get48bits_3 \
	    --remove-asm get64bits $*.c

# sinjoh delivers PXI replies without entering ARM IRQ mode, so the DS's
# mode-gated FIFO drain in PMi_WaitBusy never runs.
$(OUT)/gv_src/arm9/pm.c: $(GAME)/arm9/pm.c
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/gv_src \
	    --fixup pm_waitbusy_drain arm9/pm.c

# The extra heap's raw DS main-RAM base is unmapped on ARM11.
$(OUT)/gv_src/arm9/os_alloc.c: $(GAME)/arm9/os_alloc.c
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/gv_src \
	    --fixup extra_heap_mainmem arm9/os_alloc.c

$(GV_PATCHED_OBJS): $(OUT)/gv_raw/%.o: $(OUT)/gv_src/%.c | $(PORT_IOREG_H)
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) $(PORT_PRELUDE) \
	    -I$(GAME)/arm9 -I$(GAME) $(SDK_INCLUDES) \
	    $(NNS_INCLUDES) $(GAME_INCLUDES) -c $< -o $@

$(GV_RAW_OBJS): $(OUT)/gv_raw/%.o: $(GAME)/%.c | $(PORT_IOREG_H)
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) $(PORT_PRELUDE) $(SDK_INCLUDES) \
	    $(NNS_INCLUDES) $(GAME_INCLUDES) -c $< -o $@

# Selective symbol renaming. The redefinitions are written to $(REN_DEFS) so
# the transformation stays reviewable; nothing in the pinned checkout changes.
# A stamp keeps the recipe from running once per output archive.
$(REN_DIR):
	@mkdir -p $@

# $(file >...) expands before the recipe runs, so the directory must already
# exist as a prerequisite rather than being made by an earlier recipe line.
$(REN_DIR)/renamed.stamp: $(LIB) $(GV_OBJS) $(OUT)/gen/kj_g3.o $(OUT)/gen/m1_entry.o $(OUT)/gen/port_stubs.o | $(REN_DIR)
	@$(file >$(REN_DIR)/gv_objects.txt,$(GV_OBJS))
	@python3 tools/rename_genv.py --binutils $(DEVKITARM)/bin \
	    --gv-dir $(OUT)/gv_raw --objects-file $(REN_DIR)/gv_objects.txt \
	    --gv-out $(GV_LIB) \
	    --game $(LIB) --game-out $(GAME_REN) \
	    --lib $(NITRO)/build/3ds/libnitro.a \
	    --lib $(NNS)/build/3ds/libnitrosystem.a \
	    --lib $(DEVKITARM)/arm-none-eabi/lib/armv6k/fpu/libc.a \
	    --lib $(DEVKITARM)/arm-none-eabi/lib/armv6k/fpu/libm.a \
	    --lib $(DEVKITARM)/lib/gcc/arm-none-eabi/16.1.0/libgcc.a \
	    --lib $(DEVKITPRO)/libctru/lib/libctru.a \
	    --rename OS_GetConsoleType \
	    --extra $(OUT)/gen/kj_g3.o --extra $(OUT)/gen/m1_entry.o \
	    --extra $(OUT)/gen/port_stubs.o \
	    --extra-out $(REN_DIR) --redefs $(REN_DEFS)
	@touch $@

clean:
	rm -rf $(OUT)

-include $(wildcard $(DEPS))

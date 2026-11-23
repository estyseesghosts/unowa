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

.PHONY: verify-pins verify-rom lib link-final clean
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
TOOLS := $(dir $(lastword $(MAKEFILE_LIST)))../tools

# M2/M3 presentation. The M0 GPU renderer and its log/storage dependencies are
# compiled with the root Makefile's ARM11 flags (see Makefile:58-68) and linked
# into the test ELF. The test entry initialises the renderer and the V-blank
# pacing clock; the strong NitroPort_OnFrame in port_stubs.c paces the game tick
# once per V-blank and presents one clear picture every other V-blank. This
# proves the frame loop reaches the display; the G2D/PICA game bridge is M3/M4.
ENGINE_SRCS := engine/graphics/renderer.c engine/diagnostics/log.c \
               engine/audio/audio.c \
               engine/graphics/g2d/g2d.c engine/graphics/g2d/wideart_stub.c \
               engine/graphics/g3d/g3d.c engine/graphics/g3d/textures.c \
               engine/graphics/pica/gpu.c \
               engine/graphics/pica/targets.c engine/graphics/resources/atlas.c \
               engine/runtime/unowa_frame.c \
               engine/runtime/ds_memory_ops.c \
               engine/storage/paths.c engine/runtime/unowa_edition.c \
               engine/runtime/frame_pacing.c
ENGINE_OBJS := $(patsubst %.c,$(OUT)/obj/%.o,$(ENGINE_SRCS))
# sinjoh's 2D path carries profiling hooks guarded by HGSS_PROFILE_OFF; unowa
# publishes its own counters, so compile them out rather than add the perf TU.
ENGINE_CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter \
                 -Wno-unused-variable \
                 -ffunction-sections -fdata-sections -march=armv6k -mtune=mpcore \
                 -mfpu=vfp -mfloat-abi=hard -mtp=soft -mword-relocations \
                 -fno-short-enums -D__3DS__ -DHGSS_PROFILE_OFF \
                 -Iengine -I$(DEVKITPRO)/libctru/include

# The 2D shader: picasso assembles it, bin2s wraps it with the g2d_shbin
# symbols gpu.c declares. Same toolchain and naming as sinjoh engine/game.mk.
PICASSO := $(DEVKITPRO)/tools/bin/picasso
BIN2S   := $(DEVKITPRO)/tools/bin/bin2s
SHADER_OBJS := $(OUT)/gen/shaders/g2d_shbin.o \
               $(OUT)/gen/shaders/g3d_shbin.o \
               $(OUT)/gen/shaders/g3d_lit_shbin.o
ENGINE_OBJS += $(SHADER_OBJS)

$(OUT)/gen/shaders/%_shbin.o: engine/graphics/pica/shaders/%.v.pica
	@mkdir -p $(dir $@)
	@echo "  SH  $*.v.pica"
	@$(PICASSO) -o $(OUT)/gen/shaders/$*.shbin $<
	@$(BIN2S) $(OUT)/gen/shaders/$*.shbin > $(OUT)/gen/shaders/$*_shbin.s
	@$(DEVKITARM)/bin/arm-none-eabi-as -o $@ $(OUT)/gen/shaders/$*_shbin.s

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
           ov119/pokemontrade_2d.c twl/pm.c arm9/gf_overlay.c \
           ov016/sceneoptmovconfirm.c ov016/sceneoptmovconfirm1.c \
           arm9/gfl_use.c
# Every compiled TU the port-wide rewrites change is patched too: DS main-RAM
# and shared-area address literals into the flat image, code addresses into
# native functions, per-file data names and overlay ids (tools/patch_sources.py).
PORT_SRCS := $(shell python3 tools/patch_sources.py --source $(GAME) --list)
PATCHED += $(filter $(patsubst $(GAME)/%,%,$(GAME_SRCS)),$(PORT_SRCS))
PATCHED := $(sort $(PATCHED))
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
GV_PATCHED := ov009/biginteger.c arm9/pm.c
GV_PATCHED += $(filter $(GV_SRCS),$(PORT_SRCS))
GV_PATCHED := $(sort $(GV_PATCHED))
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
               -fno-delete-null-pointer-checks -fno-builtin -O2 -g \
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

$(PORT_IOREG_H): $(GAME)/sdk_ioreg.h tools/gen_port_ioreg_header.py
	@mkdir -p $(dir $@)
	@python3 tools/gen_port_ioreg_header.py --source $(GAME) --out $@

# Sinjoh's geometry interpreter is game-agnostic DS hardware work and supplies
# the NitroPort_Gx* hooks the game calls.
# UNOWA keeps a local copy of the geometry interpreter: sinjoh's checkout is
# pinned and must stay pristine, and Gen V needs its own matrix-state guard.
$(OUT)/gen/kj_g3.o: engine/runtime/kj_g3.c
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -DHGSS_PROFILE_OFF \
	    -I$(SINJOH_DIR)/engine/runtime -I$(NITRO)/include \
	    -I$(NITRO)/build/3ds/gen -I$(SDK)/include \
	    -c $< -o $@

# The rename step (below) needs a root object that calls NitroMain.
$(OUT)/gen/m1_entry.o:
	@mkdir -p $(dir $@)
	@printf 'extern void NitroMain(void);\nvoid m1_entry(void) { NitroMain(); }\n' > $(OUT)/gen/m1_entry.c
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -c $(OUT)/gen/m1_entry.c -o $@

# The flat DS main-RAM image (tools/gen_flat_image.py, engine/runtime/
# flat_memory.h). The generator needs every name the other link inputs define:
# a ROM pointer to a function, or to data the port defines natively, is
# rewritten to that native symbol. Its output is derived from the player's ROM
# and stays in the gitignored build tree.
FLAT_DIR := $(OUT)/gen/flat
FLAT_PY  := tools/gen_flat_image.py
FLAT_OBJS := $(FLAT_DIR)/flat_image.o $(OUT)/obj/engine/runtime/flat_memory.o \
             $(OUT)/obj/engine/runtime/ds_boot.o
FLAT_LD  := $(FLAT_DIR)/flat_names.ld

# DS wireless and DSi-only entry points; see the file's header comment and
# docs/status.md "Wireless stubs".
WIRELESS_OBJ := $(OUT)/obj/engine/stubs/wireless_stubs.o

$(WIRELESS_OBJ): engine/stubs/wireless_stubs.c
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -Wextra $(SDK_INCLUDES) -c $< -o $@

$(FLAT_DIR)/native.txt: $(REN_DIR)/renamed.stamp $(OUT)/gen/card_backup_port.o $(ENGINE_OBJS) \
                        $(WIRELESS_OBJ)
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-nm --defined-only $(GAME_REN) $(GV_LIB) \
	    $(NITRO)/build/3ds/libnitro.a $(NNS)/build/3ds/libnitrosystem.a \
	    $(REN_DIR)/kj_g3.o $(REN_DIR)/port_stubs.o $(OUT)/gen/card_backup_port.o \
	    $(ENGINE_OBJS) $(WIRELESS_OBJ) 2>/dev/null \
	    | awk 'NF == 3 && $$2 ~ /^[TDBRWV]$$/ { print $$3 }' | LC_ALL=C sort -u > $@

$(FLAT_DIR)/flat_image.c $(FLAT_LD) &: $(FLAT_DIR)/native.txt $(FLAT_PY) tools/gen_port_ioreg_header.py \
                                       engine/game_blocklist.txt engine/game_overlay_blocklist.txt
	@python3 $(FLAT_PY) --aletheia $(ALETHEIA_DIR) --native $< --out-dir $(FLAT_DIR)

$(FLAT_DIR)/flat_image.o: $(FLAT_DIR)/flat_image.c engine/runtime/flat_memory.h
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -Iengine -c $< -o $@

$(OUT)/obj/engine/runtime/flat_memory.o: engine/runtime/flat_memory.c engine/runtime/flat_memory.h
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -Wextra -Iengine $(SDK_INCLUDES) \
	    -c $< -o $@

$(OUT)/obj/engine/runtime/ds_boot.o: engine/runtime/ds_boot.c
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -Wextra $(SDK_INCLUDES) -c $< -o $@

# The test entry. Built with the game's flags: it calls into the game.
$(OUT)/obj/engine/app/game_main.o: engine/app/game_main.c engine/runtime/flat_memory.h
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -Wextra -Iengine -c $< -o $@

# Final link: a real ARM11 ELF and .3dsx using devkitPro's runtime.
$(OUT)/obj/engine/%.o: engine/%.c
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(ENGINE_CFLAGS) -c $< -o $@

# This build reads the Black extract and opens black.pak (engine/app/game_main.c).
$(OUT)/obj/engine/runtime/unowa_edition.o: ENGINE_CFLAGS += -DUNOWA_EDITION=0

# --wrap: engine/runtime/flat_memory.c replaces sinjoh's overlay seams and
# moves the main arena above the DS image; see its header comment.
FLAT_WRAPS := -Wl,--wrap=NitroPort_OverlayInfo -Wl,--wrap=NitroPort_OverlayLoadImage \
              -Wl,--wrap=NitroPort_OverlayClearBss -Wl,--wrap=OS_InitArena

# --wrap: engine/runtime/ds_memory_ops.c translates DS pointers (HW_LCDC_VRAM
# and friends, raw 0x02xxxxxx) passed to the SDK's block clear/copy helpers;
# see its header comment.
DS_MEMORY_WRAPS := -Wl,--wrap=MIi_CpuClearFast -Wl,--wrap=MIi_CpuClear32 \
                   -Wl,--wrap=MIi_CpuClear16 -Wl,--wrap=MIi_CpuCopyFast \
                   -Wl,--wrap=MIi_CpuCopy32 -Wl,--wrap=MIi_CpuCopy16 \
                   -Wl,--wrap=MI_CpuFill8 -Wl,--wrap=MI_CpuCopy8

$(OUT)/m1-test.elf: $(OUT)/obj/engine/app/game_main.o $(REN_DIR)/renamed.stamp \
                    $(FLAT_OBJS) $(FLAT_LD) $(ENGINE_OBJS) $(OUT)/gen/card_backup_port.o \
                    $(WIRELESS_OBJ)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc -march=armv6k -mtune=mpcore \
	    -mfpu=vfp -mfloat-abi=hard -mtp=soft -mword-relocations \
	    -fno-short-enums -D__3DS__ -specs=3dsx.specs \
	    -L$(DEVKITPRO)/libctru/lib -Wl,--gc-sections \
	    -Wl,--no-enum-size-warning -Wl,--wrap=NitroPort_Unimplemented \
	    -Wl,--wrap=OS_Terminate $(FLAT_WRAPS) $(DS_MEMORY_WRAPS) \
	    -Wl,--start-group $< $(OUT)/gen/card_backup_port.o $(GAME_REN) $(GV_LIB) \
	    $(NITRO)/build/3ds/libnitro.a \
	    $(NNS)/build/3ds/libnitrosystem.a \
	    $(FLAT_OBJS) $(REN_DIR)/kj_g3.o \
	    $(REN_DIR)/port_stubs.o $(ENGINE_OBJS) $(WIRELESS_OBJ) -Wl,--end-group \
	    -lcitro3d -lctru -lm $(FLAT_LD) -o $@

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
# Diagnostic: count NitroMain loop iterations (see patch_sources.py).
$(OUT)/src/arm9/gfl_use.c: $(GAME)/arm9/gfl_use.c tools/patch_sources.py
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/src \
	    --fixup mainloop_probe arm9/gfl_use.c

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

$(OUT)/src/ov016/sceneoptmovconfirm.c: $(GAME)/ov016/sceneoptmovconfirm.c
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/src \
	    --fixup ov016_split_unit_caller ov016/sceneoptmovconfirm.c

$(OUT)/src/ov016/sceneoptmovconfirm1.c: $(GAME)/ov016/sceneoptmovconfirm1.c
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/src \
	    --fixup ov016_split_unit_owner ov016/sceneoptmovconfirm1.c

$(OUT)/src/ov094/btlv_input.c: $(GAME)/ov094/btlv_input.c
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/src \
	    --fixup vram_enum_base ov094/btlv_input.c

$(OUT)/src/ov119/pokemontrade_2d.c: $(GAME)/ov119/pokemontrade_2d.c
	@mkdir -p $(dir $@)
	@python3 tools/patch_sources.py --source $(GAME) --out $(OUT)/src \
	    --fixup vram_enum_base ov119/pokemontrade_2d.c

# A change to the patcher regenerates every patched copy.
$(addprefix $(OUT)/src/,$(PATCHED)) $(addprefix $(OUT)/gv_src/,$(GV_PATCHED)): tools/patch_sources.py

$(PATCHED_OBJS): $(OUT)/obj/%.o: $(OUT)/src/%.c | $(PORT_IOREG_H)
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) $(PORT_PRELUDE) \
	    -iquote $(GAME)/$(dir $*) -I$(GAME)/arm9 -I$(GAME) $(SDK_INCLUDES) \
	    $(NNS_INCLUDES) $(GAME_INCLUDES) -MMD -MP -MQ"$@" -c $< -o $@

$(NORMAL_OBJS): $(OUT)/obj/%.o: $(GAME)/%.c | $(PORT_IOREG_H)
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) $(PORT_PRELUDE) $(SDK_INCLUDES) \
	    $(NNS_INCLUDES) $(GAME_INCLUDES) -MMD -MP -MQ"$@" -c $< -o $@

# Port-provided values: linker-symbol constants and the BW_LIMIT table.
$(OUT)/gen/port_stubs.o: engine/stubs/port_stubs.c
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -I$(GAME) -c $< -o $@

# The Gen 5 save device on 3DS, replacing libcard's card_backup.o. It is the
# first object in the link group so its CARD_* definitions are already visible
# when the archives are scanned; otherwise ld would pull the DS bus version out
# of libnitro.a for the same symbols and report a duplicate.
$(OUT)/gen/card_backup_port.o: engine/stubs/card_backup_port.c
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) -Iengine -c $< -o $@

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

$(GV_PATCHED_OBJS): $(OUT)/gv_raw/%.o: $(OUT)/gv_src/%.c | $(PORT_IOREG_H)
	@mkdir -p $(dir $@)
	@$(DEVKITARM)/bin/arm-none-eabi-gcc $(PORT_CFLAGS) $(PORT_PRELUDE) \
	    -iquote $(GAME)/$(dir $*) -I$(GAME)/arm9 -I$(GAME) $(SDK_INCLUDES) \
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
$(REN_DIR)/renamed.stamp: $(LIB) $(GV_OBJS) $(OUT)/gen/kj_g3.o $(OUT)/gen/m1_entry.o $(OUT)/gen/port_stubs.o \
                          $(dir $(lastword $(MAKEFILE_LIST)))gv_sdk_sources.txt \
                          $(dir $(lastword $(MAKEFILE_LIST)))game_blocklist.txt \
                          $(dir $(lastword $(MAKEFILE_LIST)))game_overlay_blocklist.txt | $(REN_DIR)
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

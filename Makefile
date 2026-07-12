# Mario Kart 64 - portable source branch (Stages 1, 1.5, 2, 3)
#
# This branch carries the portable game/common code, the platform-neutral
# service contracts under src/platform/, the graphics, window, and audio
# interface headers (src/gfx/gfx_rendering_api.h,
# src/gfx/gfx_window_manager_api.h, src/audio/audio_api.h), and the Stage 3
# Intuition Engine backend behind those contracts.
#
# Targets range from the focused host test objects (portable math and matrix
# helpers, the audio mixer and table parsing, the Fast3D translator, the
# course-loading seam) up to the full engine images: the gfx and audio
# coprocessor service binaries, the linked game.bin, and the bootable
# self-contained mariokart64.ie68 package.

CC ?= cc

VERSION ?= us

DEFINES := \
  -DGBI_FLOATS \
  -D_LANGUAGE_C \
  -DVERSION_US=1 \
  -DF3DEX_GBI=1 \
  -DF3D_OLD=1 \
  -DNON_MATCHING=1 \
  -DAVOID_UB=1 \
  -DGCC=1

IE_NO_CMDSTREAM ?=
ifneq ($(IE_NO_CMDSTREAM),)
DEFINES += -DIE_NO_CMDSTREAM
endif

INCLUDES := -Iinclude -Isrc -Isrc/racing -Isrc/ending -I.

CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -fsigned-char -fno-strict-aliasing -Wall $(DEFINES) $(INCLUDES)

BUILD_DIR := build/host

IE_PERF_COUNTERS ?=
IE_PERF_DEFINES :=
ifneq ($(IE_PERF_COUNTERS),)
IE_PERF_DEFINES += -DIE_PERF_COUNTERS
endif

# Stage 1 host scope: portable math/matrix helpers, trig tables, the audio
# mixer, and the Fast3D transform core.
HOST_SRCS := \
  src/os/guLookAtF.c \
  src/os/guLookAtRef.c \
  src/os/guMtxCatF.c \
  src/os/guMtxCatL.c \
  src/os/guMtxF2L.c \
  src/os/guNormalize.c \
  src/os/guOrthoF.c \
  src/os/guPerspectiveF.c \
  src/os/guRotateF.c \
  src/os/guScaleF.c \
  src/os/guTranslateF.c \
  src/racing/math_util.c \
  src/racing/course_metadata.c \
  src/racing/course_vertex.c \
  src/racing/displaylist_unpack.c \
  src/math_util_2.c \
  src/audio/mixer.c \
  src/audio/seqfile.c \
  src/gfx/gfx_fast3d.c \
  src/gfx/gfx_cc.c \
  src/gfx/gfx_pc.c \
  src/buffers/trig_tables.c \
  src/buffers/random.c

HOST_OBJS := $(HOST_SRCS:%.c=$(BUILD_DIR)/%.o)

# Objects linked into the test binaries. math_util_2.c is compile-scope
# only for now: it pulls in display-list globals that the tests stub.
TEST_SUPPORT_OBJS := \
  $(BUILD_DIR)/src/os/guLookAtF.o \
  $(BUILD_DIR)/src/os/guLookAtRef.o \
  $(BUILD_DIR)/src/os/guMtxCatF.o \
  $(BUILD_DIR)/src/os/guMtxCatL.o \
  $(BUILD_DIR)/src/os/guMtxF2L.o \
  $(BUILD_DIR)/src/os/guNormalize.o \
  $(BUILD_DIR)/src/os/guOrthoF.o \
  $(BUILD_DIR)/src/os/guPerspectiveF.o \
  $(BUILD_DIR)/src/os/guRotateF.o \
  $(BUILD_DIR)/src/os/guScaleF.o \
  $(BUILD_DIR)/src/os/guTranslateF.o \
  $(BUILD_DIR)/src/racing/math_util.o \
  $(BUILD_DIR)/src/audio/mixer.o \
  $(BUILD_DIR)/src/gfx/gfx_fast3d.o \
  $(BUILD_DIR)/src/buffers/trig_tables.o \
  $(BUILD_DIR)/src/buffers/random.o \
  $(BUILD_DIR)/tests/host_stubs.o

TESTS := \
  $(BUILD_DIR)/tests/test_gu_math \
  $(BUILD_DIR)/tests/test_math_util \
  $(BUILD_DIR)/tests/test_mixer \
  $(BUILD_DIR)/tests/test_fast3d \
  $(BUILD_DIR)/tests/test_course_loading \
  $(BUILD_DIR)/tests/test_seqfile \
  $(BUILD_DIR)/tests/test_gfx_translate \
  $(BUILD_DIR)/tests/test_gfx_voodoo \
  $(BUILD_DIR)/tests/test_audio_voices \
  $(BUILD_DIR)/tests/test_ie_fmt \
  $(BUILD_DIR)/tests/test_perf_counters \
  $(BUILD_DIR)/tests/test_perf_counters_disabled

GFX_TRANSLATE_OBJS := \
  $(BUILD_DIR)/tests/gfx_pc_perf.o \
  $(BUILD_DIR)/src/gfx/gfx_cc.o \
  $(BUILD_DIR)/src/gfx/gfx_fast3d.o \
  $(BUILD_DIR)/tests/host_stubs.o \
  $(BUILD_DIR)/tests/ie_perf_counters.o

GFX_VOODOO_OBJS := \
  $(BUILD_DIR)/tests/ie_gfx_voodoo_host.o \
  $(BUILD_DIR)/src/gfx/gfx_cc.o \
  $(BUILD_DIR)/tests/ie_perf_counters.o

AUDIO_VOICES_OBJS := \
  $(BUILD_DIR)/tests/ie_platform_audio_host.o \
  $(BUILD_DIR)/tests/ie_perf_counters.o

# The course-loading seam test links only the seam objects; the test file
# itself provides the buffers and platform_fatal, so it must not pull in
# the generic test-support set.
COURSE_LOADING_OBJS := \
  $(BUILD_DIR)/src/racing/course_metadata.o \
  $(BUILD_DIR)/src/racing/course_vertex.o \
  $(BUILD_DIR)/src/racing/displaylist_unpack.o

all: host

host: $(HOST_OBJS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD_DIR)/tests/test_course_loading: tests/test_course_loading.c $(COURSE_LOADING_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(COURSE_LOADING_OBJS) -lm

$(BUILD_DIR)/tests/test_seqfile: tests/test_seqfile.c $(BUILD_DIR)/src/audio/seqfile.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(BUILD_DIR)/src/audio/seqfile.o -lm

$(BUILD_DIR)/tests/test_gfx_translate: tests/test_gfx_translate.c $(GFX_TRANSLATE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DIE_PERF_COUNTERS -o $@ $< $(GFX_TRANSLATE_OBJS) -lm

$(BUILD_DIR)/tests/gfx_pc_perf.o: src/gfx/gfx_pc.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -DIE_PERF_COUNTERS -o $@ $<

$(BUILD_DIR)/tests/ie_perf_counters.o: ie/ie_perf_counters.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -DIE_PERF_COUNTERS -o $@ $<

$(BUILD_DIR)/tests/ie_gfx_voodoo_host.o: ie/ie_gfx_voodoo.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -DIE_MMIO_HOST_CAPTURE -DIE_PERF_COUNTERS -DIE_BOOT_STATUS_DRAWS -o $@ $<

$(BUILD_DIR)/tests/test_gfx_voodoo: tests/test_gfx_voodoo.c $(GFX_VOODOO_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DIE_MMIO_HOST_CAPTURE -DIE_PERF_COUNTERS -o $@ $< $(GFX_VOODOO_OBJS) -lm

$(BUILD_DIR)/tests/ie_platform_audio_host.o: ie/ie_platform_audio.c tests/ie_audio_sample_table.h
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -Itests -DIE_AUDIO_VOICES -DIE_MMIO_HOST_CAPTURE -DIE_PERF_COUNTERS -o $@ $<

$(BUILD_DIR)/tests/test_audio_voices: tests/test_audio_voices.c tests/ie_audio_sample_table.h $(AUDIO_VOICES_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Itests -DIE_AUDIO_VOICES -DIE_MMIO_HOST_CAPTURE -DIE_PERF_COUNTERS -o $@ $< $(AUDIO_VOICES_OBJS) -lm

$(BUILD_DIR)/tests/test_ie_fmt: tests/test_ie_fmt.c $(BUILD_DIR)/ie/ie_fmt.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(BUILD_DIR)/ie/ie_fmt.o -lm

$(BUILD_DIR)/tests/test_perf_counters: tests/test_perf_counters.c $(BUILD_DIR)/tests/ie_perf_counters.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DIE_PERF_COUNTERS -o $@ $< $(BUILD_DIR)/tests/ie_perf_counters.o -lm

$(BUILD_DIR)/tests/%: tests/%.c $(TEST_SUPPORT_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_SUPPORT_OBJS) -lm

test: $(TESTS)
	@set -e; for t in $(TESTS); do echo "== $$t"; $$t; done
	@echo "All tests passed."

# ---- Neutral ROM asset pipeline (Stage 1.5) --------------------------------
# The source ROM is a user-supplied local input; it is ignored by git and is
# never committed or listed as a generated asset. The generated layout under
# $(ASSETS_DIR) is ignored by git as well; regenerate it from the ROM, or
# verify an existing layout (no ROM needed) with assets-verify.

PYTHON ?= python3
ROM ?= Mario Kart 64 (USA).z64
ASSETS_DIR ?= rom_assets

assets:
	$(PYTHON) tools/rom_assets.py generate --rom "$(ROM)" --out $(ASSETS_DIR)

assets-verify:
	$(PYTHON) tools/rom_assets.py verify --dir $(ASSETS_DIR)

assets-test:
	$(PYTHON) tests/test_rom_assets.py

pack-test:
	$(PYTHON) tests/test_pack_ie68.py

# ---- Intuition Engine target (Stage 3) --------------------------------------
# Bare-metal M68K image for the Intuition Engine. Built hard-float against
# the engine's 68881 (see the -m68881 rationale below). The engine checkout
# is expected beside this repository.

IE68_CC ?= m68k-atari-mint-gcc
IE_ENGINE ?= ../IntuitionEngine/bin/ie_headless
# The game image never self-modifies guest code, so the engine's per-dispatch
# JIT block re-hash is pure overhead here; SMC stays guarded by the engine's
# code-page write detection and invalidation queue.
IE_RUN_ENV ?= IE_M68K_JIT_DISPATCH_HASH=0
IE_BUILD_DIR := build/ie
IE_AUDIO_PCM16_ASSET := $(ASSETS_DIR)/audiotables.pcm16.bin
IE_AUDIO_SAMPLE_TABLE_C := $(IE_BUILD_DIR)/gen/ie_audio_sample_table.c
IE_AUDIO_SAMPLE_TABLE_H := $(IE_BUILD_DIR)/gen/ie_audio_sample_table.h

# libgcc for 64-bit integer helpers. Query with the same -m68020 -m68881
# flags the image is built with, so the 68881 hard-float multilib is
# selected and the helpers match the FPU ABI. Lazily expanded so the
# compiler probe only runs for IE targets; host-only workflows stay
# independent of the optional M68K toolchain.
IE68_LIBGCC = $(shell $(IE68_CC) -m68020 -m68881 -print-libgcc-file-name)

# -malign-int: the m68k ABI aligns 32-bit types to 2 bytes, but the
# game overlays structs onto big-endian N64 blob data laid out with
# 4-byte alignment (audio banks, sequence files); without it every
# such overlay reads shifted garbage.
# -m68881: hard float. The engine implements the 68881 in full
# (interpreter dispatch + SSE-compiled FPU ops in the JIT); soft-float
# synthesis dominated the CPU profile. -Ofast per the same measured
# experiment; fall back to -O3 if a golden gate ever disagrees.
# -fno-toplevel-reorder: segment 2 resolves numeric 0x02xxxxxx
# addresses as base+offset into the .data of textures.o +
# data_segment2.o (ie/game.ld); the offsets encoded in the symbol
# names only match when the compiler emits the arrays in declaration
# order, which is also ROM order.
IE68_CFLAGS := -m68020 -m68881 -mtune=68020 -Ofast -Wall \
  -malign-int -fno-toplevel-reorder \
  -std=gnu11 -fsigned-char -fno-strict-aliasing \
  -Wno-int-conversion -Wno-incompatible-pointer-types \
  -D__SIZE_TYPE__='unsigned int' -D__PTRDIFF_TYPE__='int' \
  -ffreestanding -nostdlib -fno-builtin -fno-pic -fno-pie \
  -fno-stack-protector -fno-asynchronous-unwind-tables \
  -Iie -Iie/include -Isrc -Isrc/racing -Isrc/ending -Iinclude -I. $(DEFINES) \
  $(IE_PERF_DEFINES)

IE_HELLO_OBJS := \
  $(IE_BUILD_DIR)/start.o \
  $(IE_BUILD_DIR)/ie_runtime.o \
  $(IE_BUILD_DIR)/ie_libc.o \
  $(IE_BUILD_DIR)/hello_main.o

IE_LOG_OBJS := \
  $(IE_BUILD_DIR)/start.o \
  $(IE_BUILD_DIR)/ie_runtime.o \
  $(IE_BUILD_DIR)/ie_libc.o \
  $(IE_BUILD_DIR)/ie_fmt.o \
  $(IE_BUILD_DIR)/ie_platform_log.o \
  $(IE_BUILD_DIR)/log_main.o

IE_ASSET_OBJS := \
  $(IE_BUILD_DIR)/start.o \
  $(IE_BUILD_DIR)/ie_runtime.o \
  $(IE_BUILD_DIR)/ie_libc.o \
  $(IE_BUILD_DIR)/ie_fmt.o \
  $(IE_BUILD_DIR)/ie_platform_log.o \
  $(IE_BUILD_DIR)/ie_platform_asset.o \
  $(IE_BUILD_DIR)/asset_main.o

IE_TIME_OBJS := \
  $(IE_BUILD_DIR)/start.o \
  $(IE_BUILD_DIR)/ie_runtime.o \
  $(IE_BUILD_DIR)/ie_libc.o \
  $(IE_BUILD_DIR)/ie_fmt.o \
  $(IE_BUILD_DIR)/ie_platform_log.o \
  $(IE_BUILD_DIR)/ie_platform_time.o \
  $(IE_BUILD_DIR)/time_main.o

IE_INPUT_OBJS := \
  $(IE_BUILD_DIR)/start.o \
  $(IE_BUILD_DIR)/ie_runtime.o \
  $(IE_BUILD_DIR)/ie_libc.o \
  $(IE_BUILD_DIR)/ie_fmt.o \
  $(IE_BUILD_DIR)/ie_platform_log.o \
  $(IE_BUILD_DIR)/ie_platform_input.o \
  $(IE_BUILD_DIR)/input_main.o

IE_SAVE_OBJS := \
  $(IE_BUILD_DIR)/start.o \
  $(IE_BUILD_DIR)/ie_runtime.o \
  $(IE_BUILD_DIR)/ie_libc.o \
  $(IE_BUILD_DIR)/ie_fmt.o \
  $(IE_BUILD_DIR)/ie_platform_log.o \
  $(IE_BUILD_DIR)/ie_platform_save.o \
  $(IE_BUILD_DIR)/save_main.o

# The graphics smoke runs the real shared translator on the target, so
# it compiles the src/gfx sources with the game defines.
IE_AUDIO_OBJS := \
  $(IE_BUILD_DIR)/start.o \
  $(IE_BUILD_DIR)/ie_runtime.o \
  $(IE_BUILD_DIR)/ie_libc.o \
  $(IE_BUILD_DIR)/ie_fmt.o \
  $(IE_BUILD_DIR)/ie_platform_log.o \
  $(IE_BUILD_DIR)/ie_platform_time.o \
  $(IE_BUILD_DIR)/ie_platform_audio.o \
  $(IE_BUILD_DIR)/audio_main.o

IE_GFX_OBJS := \
  $(IE_BUILD_DIR)/start.o \
  $(IE_BUILD_DIR)/ie_runtime.o \
  $(IE_BUILD_DIR)/ie_libc.o \
  $(IE_BUILD_DIR)/ie_math.o \
  $(IE_BUILD_DIR)/ie_fmt.o \
  $(IE_BUILD_DIR)/ie_platform_log.o \
  $(IE_BUILD_DIR)/ie_platform_time.o \
  $(IE_BUILD_DIR)/ie_gfx_voodoo.o \
  $(IE_BUILD_DIR)/ie_gfx_window.o \
  $(IE_BUILD_DIR)/coproc/ie_coproc.o \
  $(IE_BUILD_DIR)/src_gfx_pc.o \
  $(IE_BUILD_DIR)/src_gfx_cc.o \
  $(IE_BUILD_DIR)/src_gfx_fast3d.o \
  $(IE_BUILD_DIR)/gfx_main.o

ifneq ($(IE_PERF_COUNTERS),)
IE_GFX_OBJS += $(IE_BUILD_DIR)/ie_perf_counters.o
endif

$(IE_BUILD_DIR)/%.o: ie/%.S
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -c -o $@ $<

$(IE_BUILD_DIR)/%.o: ie/%.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -c -o $@ $<

$(IE_BUILD_DIR)/hello.ie68: ie/link.ld $(IE_HELLO_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/link.ld -Wl,--oformat,binary \
	  -o $@ $(IE_HELLO_OBJS) $(IE68_LIBGCC)

$(IE_BUILD_DIR)/log.ie68: ie/link.ld $(IE_LOG_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/link.ld -Wl,--oformat,binary \
	  -o $@ $(IE_LOG_OBJS) $(IE68_LIBGCC)

$(IE_BUILD_DIR)/asset.ie68: ie/link.ld $(IE_ASSET_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/link.ld -Wl,--oformat,binary \
	  -o $@ $(IE_ASSET_OBJS) $(IE68_LIBGCC)

$(IE_BUILD_DIR)/time.ie68: ie/link.ld $(IE_TIME_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/link.ld -Wl,--oformat,binary \
	  -o $@ $(IE_TIME_OBJS) $(IE68_LIBGCC)

$(IE_BUILD_DIR)/input.ie68: ie/link.ld $(IE_INPUT_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/link.ld -Wl,--oformat,binary \
	  -o $@ $(IE_INPUT_OBJS) $(IE68_LIBGCC)

$(IE_BUILD_DIR)/loader_main.o: ie/loader_main.c Makefile
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -DIE_GAME_BIN_NAME='"$(IE_BUILD_DIR)/game.bin"' -c -o $@ $<

$(IE_BUILD_DIR)/save.ie68: ie/link.ld $(IE_SAVE_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/link.ld -Wl,--oformat,binary \
	  -o $@ $(IE_SAVE_OBJS) $(IE68_LIBGCC)

$(IE_BUILD_DIR)/src_gfx_pc.o: src/gfx/gfx_pc.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -c -o $@ $<

$(IE_BUILD_DIR)/src_gfx_cc.o: src/gfx/gfx_cc.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -c -o $@ $<

$(IE_BUILD_DIR)/src_gfx_fast3d.o: src/gfx/gfx_fast3d.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -c -o $@ $<

$(IE_BUILD_DIR)/audio.ie68: ie/link.ld $(IE_AUDIO_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/link.ld -Wl,--oformat,binary \
	  -o $@ $(IE_AUDIO_OBJS) $(IE68_LIBGCC)

$(IE_BUILD_DIR)/gfx.ie68: ie/link.ld $(IE_GFX_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/link.ld -Wl,--oformat,binary \
	  -o $@ $(IE_GFX_OBJS) $(IE68_LIBGCC)

ie-hello: $(IE_BUILD_DIR)/hello.ie68

# -nojit pins the interpreter for deterministic smoke runs (the
# engine's own M68K demo scripts do the same); the images also pass
# under the JIT.
ie-smoke: $(IE_BUILD_DIR)/hello.ie68 $(IE_BUILD_DIR)/log.ie68 $(IE_BUILD_DIR)/time.ie68 $(IE_BUILD_DIR)/input.ie68
	$(IE_ENGINE) -nojit $(IE_BUILD_DIR)/hello.ie68 -script ie/smoke/hello_smoke.ies
	$(IE_ENGINE) -nojit $(IE_BUILD_DIR)/log.ie68 -script ie/smoke/log_smoke.ies
	$(IE_ENGINE) -nojit $(IE_BUILD_DIR)/time.ie68 -script ie/smoke/time_smoke.ies
	$(IE_ENGINE) -nojit $(IE_BUILD_DIR)/input.ie68 -script ie/smoke/input_smoke.ies

ie-time-smoke: $(IE_BUILD_DIR)/time.ie68
	$(IE_ENGINE) -nojit $(IE_BUILD_DIR)/time.ie68 -script ie/smoke/time_smoke.ies

ie-input-smoke: $(IE_BUILD_DIR)/input.ie68
	$(IE_ENGINE) -nojit $(IE_BUILD_DIR)/input.ie68 -script ie/smoke/input_smoke.ies

ie-gfx-smoke: $(IE_BUILD_DIR)/gfx.ie68
	$(IE_ENGINE) -nojit $(IE_BUILD_DIR)/gfx.ie68 -script ie/smoke/gfx_smoke.ies

# T&L coprocessor service, IE64 assembly (ie/coproc/tnl_service_ie64.asm).
# The assembler emits an image based at the engine's 0x1000 program load
# address; the worker loader wants a flat blob for WORKER base 0x3A0000,
# so strip the leading 0x39F000 bytes of padding.
IE64ASM := $(IE_BUILD_DIR)/ie64asm
$(IE64ASM):
	@mkdir -p $(IE_BUILD_DIR)
	cd ../IntuitionEngine && go build -tags ie64 -o $(abspath $@) ./assembler

$(IE_BUILD_DIR)/tnl.ie64: ie/coproc/tnl_service_ie64.asm ie/coproc/tnl_proto.h $(IE64ASM)
	@mkdir -p $(IE_BUILD_DIR)
	cd ie/coproc && $(abspath $(IE64ASM)) -o $(abspath $(IE_BUILD_DIR))/tnl_padded.ie64 tnl_service_ie64.asm
	tail -c +3796993 $(IE_BUILD_DIR)/tnl_padded.ie64 > $@
	@rm -f $(IE_BUILD_DIR)/tnl_padded.ie64

IE_COPROC_OBJS := \
  $(IE_BUILD_DIR)/start.o \
  $(IE_BUILD_DIR)/ie_runtime.o \
  $(IE_BUILD_DIR)/ie_libc.o \
  $(IE_BUILD_DIR)/ie_fmt.o \
  $(IE_BUILD_DIR)/ie_platform_log.o \
  $(IE_BUILD_DIR)/coproc/ie_coproc.o \
  $(IE_BUILD_DIR)/coproc_main.o

IE_ECHO_CALLER_OBJS := $(filter-out $(IE_BUILD_DIR)/coproc_main.o,$(IE_COPROC_OBJS)) \
  $(IE_BUILD_DIR)/ie_platform_time.o \
  $(IE_BUILD_DIR)/echo_caller_main.o

$(IE_BUILD_DIR)/echo_caller.ie68: ie/link.ld $(IE_ECHO_CALLER_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/link.ld -Wl,--oformat,binary \
	  -o $@ $(IE_ECHO_CALLER_OBJS) $(IE68_LIBGCC)

IE_ECHO_SVC_OBJS := \
  $(IE_BUILD_DIR)/echo_svc/start.o \
  $(IE_BUILD_DIR)/echo_svc/ie_platform_time.o \
  $(IE_BUILD_DIR)/echo_svc/echo_svc_main.o

$(IE_BUILD_DIR)/echo_svc/start.o: ie/coproc/m68k_svc_start.S
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -c -o $@ $<

$(IE_BUILD_DIR)/echo_svc/ie_runtime.o: ie/ie_runtime.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -c -o $@ $<

$(IE_BUILD_DIR)/echo_svc/ie_platform_time.o: ie/ie_platform_time.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -c -o $@ $<

$(IE_BUILD_DIR)/echo_svc/echo_svc_main.o: ie/coproc/echo_svc_main.c ie/coproc/echo_proto.h ie/coproc/coproc_layout.h ie/coproc/echo_kernel.h
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -c -o $@ $<

$(IE_BUILD_DIR)/echo.ie68: ie/coproc/m68k_svc.ld $(IE_ECHO_SVC_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/coproc/m68k_svc.ld -Wl,--oformat,binary \
	  -o $@ $(IE_ECHO_SVC_OBJS) $(IE68_LIBGCC)

# ---- M68K gfx service image (plan Phase 1) --------------------------------
# Second compile pass of the shared translator TUs under -DIE_GFX_SERVICE.
# The client seams (-DIE_GFX_SVC) must NOT be compiled into the service
# image, so the game defines are filtered before reuse.
IE_GFXSVC_DEFINES = $(filter-out -DIE_GFX_SVC -DIE_GFX_SVC_SYNC,$(IE_GAME_DEFINES)) -DIE_GFX_SERVICE

IE_GFXSVC_OBJS := \
  $(IE_BUILD_DIR)/gfxsvc/start.o \
  $(IE_BUILD_DIR)/gfxsvc/gfx_svc_main.o \
  $(IE_BUILD_DIR)/gfxsvc/gfx_pc.o \
  $(IE_BUILD_DIR)/gfxsvc/gfx_fast3d.o \
  $(IE_BUILD_DIR)/gfxsvc/gfx_cc.o \
  $(IE_BUILD_DIR)/gfxsvc/ie_gfx_voodoo.o \
  $(IE_BUILD_DIR)/gfxsvc/ie_gfx_window.o \
  $(IE_BUILD_DIR)/gfxsvc/ie_coproc.o \
  $(IE_BUILD_DIR)/gfxsvc/ie_platform_time.o \
  $(IE_BUILD_DIR)/gfxsvc/ie_platform_log.o \
  $(IE_BUILD_DIR)/gfxsvc/ie_runtime.o \
  $(IE_BUILD_DIR)/gfxsvc/ie_fmt.o \
  $(IE_BUILD_DIR)/gfxsvc/ie_libc.o \
  $(IE_BUILD_DIR)/gfxsvc/ie_math.o

$(IE_BUILD_DIR)/gfxsvc/start.o: ie/coproc/m68k_svc_start.S
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -c -o $@ $<

$(IE_BUILD_DIR)/gfxsvc/gfx_svc_main.o: ie/coproc/gfx_svc_main.c ie/coproc/gfx_svc_proto.h ie/coproc/coproc_layout.h
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_GFXSVC_DEFINES) -c -o $@ $<

$(IE_BUILD_DIR)/gfxsvc/%.o: src/gfx/%.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_GFXSVC_DEFINES) -c -o $@ $<

$(IE_BUILD_DIR)/gfxsvc/ie_coproc.o: ie/coproc/ie_coproc.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_GFXSVC_DEFINES) -c -o $@ $<

$(IE_BUILD_DIR)/gfxsvc/%.o: ie/%.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_GFXSVC_DEFINES) -c -o $@ $<

$(IE_BUILD_DIR)/gfxsvc.ie68: ie/coproc/m68k_svc.ld $(IE_GFXSVC_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/coproc/m68k_svc.ld -Wl,--oformat,binary \
	  -Wl,-Map,$(IE_BUILD_DIR)/gfxsvc/gfxsvc.map \
	  -o $@ $(IE_GFXSVC_OBJS) $(IE68_LIBGCC)

# ---- M68K audio service image (plan Phase 2) --------------------------------
# Second compile pass of the audio TUs under -DIE_AUDIO_SERVICE, linked at
# the second M68K worker window (0x420000, ring 6). The client seams
# (-DIE_AUDIO_SVC) must NOT be compiled into the service image.
IE_AUDIOSVC_DEFINES = $(filter-out -DIE_AUDIO_SVC,$(IE_GAME_DEFINES)) -DIE_AUDIO_SERVICE

IE_AUDIOSVC_AUDIO_SRCS := external.c seqplayer.c playback.c effects.c heap.c \
  load.c data.c port_eu.c seqfile.c audio_session_presets.c synthesis_voice.c

IE_AUDIOSVC_OBJS := \
  $(IE_BUILD_DIR)/audiosvc/start.o \
  $(IE_BUILD_DIR)/audiosvc/audio_svc_main.o \
  $(IE_AUDIOSVC_AUDIO_SRCS:%.c=$(IE_BUILD_DIR)/audiosvc/%.o) \
  $(IE_BUILD_DIR)/audiosvc/audio_heap.o \
  $(IE_BUILD_DIR)/audiosvc/audio_svc_stubs.o \
  $(IE_BUILD_DIR)/audiosvc/ie_platform_audio.o \
  $(IE_BUILD_DIR)/audiosvc/ie_audio_sample_table.o \
  $(IE_BUILD_DIR)/audiosvc/ie_platform_time.o \
  $(IE_BUILD_DIR)/audiosvc/ie_platform_log.o \
  $(IE_BUILD_DIR)/audiosvc/ie_runtime.o \
  $(IE_BUILD_DIR)/audiosvc/ie_fmt.o \
  $(IE_BUILD_DIR)/audiosvc/ie_libc.o \
  $(IE_BUILD_DIR)/audiosvc/ie_math.o

$(IE_BUILD_DIR)/audiosvc/start.o: ie/coproc/m68k_svc_start.S
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -DIE_SVC_STACK_TOP=0x0049ff00 -c -o $@ $<

$(IE_BUILD_DIR)/audiosvc/audio_svc_main.o: ie/coproc/audio_svc_main.c ie/coproc/audio_svc_proto.h ie/coproc/coproc_layout.h
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_AUDIOSVC_DEFINES) -c -o $@ $<

$(IE_BUILD_DIR)/audiosvc/audio_svc_stubs.o: ie/coproc/audio_svc_stubs.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_AUDIOSVC_DEFINES) -c -o $@ $<

$(IE_BUILD_DIR)/audiosvc/%.o: src/audio/%.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_AUDIOSVC_DEFINES) -c -o $@ $<

$(IE_BUILD_DIR)/audiosvc/audio_heap.o: src/buffers/audio_heap.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_AUDIOSVC_DEFINES) -c -o $@ $<

$(IE_BUILD_DIR)/audiosvc/ie_audio_sample_table.o: $(IE_AUDIO_SAMPLE_TABLE_C) $(IE_AUDIO_SAMPLE_TABLE_H)
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_AUDIOSVC_DEFINES) -c -o $@ $<

$(IE_BUILD_DIR)/audiosvc/%.o: ie/%.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_AUDIOSVC_DEFINES) -c -o $@ $<

$(IE_BUILD_DIR)/audiosvc.ie68: ie/coproc/m68k_svc2.ld $(IE_AUDIOSVC_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/coproc/m68k_svc2.ld -Wl,--oformat,binary \
	  -Wl,-Map,$(IE_BUILD_DIR)/audiosvc/audiosvc.map \
	  -o $@ $(IE_AUDIOSVC_OBJS) $(IE68_LIBGCC)

$(IE_BUILD_DIR)/coproc.ie68: ie/link.ld $(IE_COPROC_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/link.ld -Wl,--oformat,binary \
	  -o $@ $(IE_COPROC_OBJS) $(IE68_LIBGCC)

# The service COSTARTs from the file root, so run with -file-root . and the
# main CPU under its normal JIT (the coprocessor worker core runs regardless).
ie-coproc-smoke: $(IE_BUILD_DIR)/coproc.ie68 $(IE_BUILD_DIR)/tnl.ie64
	$(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/coproc.ie68 -script ie/smoke/coproc_smoke.ies

ie-m68k-echo-iescript-smoke: $(IE_BUILD_DIR)/coproc.ie68 $(IE_BUILD_DIR)/echo.ie68 $(IE_BUILD_DIR)/tnl.ie64
	$(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/coproc.ie68 -script ie/smoke/m68k_echo_iescript.ies

ie-m68k-echo-smoke: $(IE_BUILD_DIR)/echo_caller.ie68 $(IE_BUILD_DIR)/echo.ie68 $(IE_BUILD_DIR)/tnl.ie64
	$(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/echo_caller.ie68 -script ie/smoke/m68k_echo_caller.ies

# P0.3: worker throughput while the attract-demo race runs on the main CPU.
# Compare against the idle numbers printed by ie-m68k-echo-smoke.
ie-m68k-load-smoke: $(IE_BUILD_DIR)/mariokart64.ie68 $(IE_BUILD_DIR)/echo.ie68
	IE_NO_IPC=1 $(IE_RUN_ENV) $(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/mariokart64.ie68 -script ie/smoke/m68k_worker_load.ies

# Phase 1 parity oracle: same loose build (client seams compiled) run twice,
# gfxsvc.ie68 hidden for the local-path reference. IE_SWAP_HASH=1 makes the
# engine record a per-swap frame hash at present time (Voodoo MMIO 0xF8358+),
# so hash N is deterministically the guest's Nth swap in both runs.
ie-gfx-svc-parity: $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin $(IE_TNL_SVC) $(IE_BUILD_DIR)/gfxsvc.ie68
	mv $(IE_BUILD_DIR)/gfxsvc.ie68 $(IE_BUILD_DIR)/gfxsvc.ie68.off
	IE_SWAP_HASH=1 IE_NO_IPC=1 $(IE_RUN_ENV) $(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/loader.ie68 \
	  -script ie/smoke/gfx_svc_parity.ies 2>&1 | grep -a PARITY > $(IE_BUILD_DIR)/parity_local.txt; \
	  mv $(IE_BUILD_DIR)/gfxsvc.ie68.off $(IE_BUILD_DIR)/gfxsvc.ie68
	IE_SWAP_HASH=1 IE_NO_IPC=1 $(IE_RUN_ENV) $(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/loader.ie68 \
	  -script ie/smoke/gfx_svc_parity.ies 2>&1 | grep -a PARITY > $(IE_BUILD_DIR)/parity_svc.txt
	diff $(IE_BUILD_DIR)/parity_local.txt $(IE_BUILD_DIR)/parity_svc.txt

ie-audio-ring-smoke: $(IE_BUILD_DIR)/audio.ie68
	$(IE_ENGINE) -nojit $(IE_BUILD_DIR)/audio.ie68 -script ie/smoke/audio_ring_smoke.ies

smoke: ie-smoke ie-gfx-smoke ie-audio-ring-smoke ie-asset-smoke ie-game-smoke ie-audio-smoke

ie-counters-smoke:
	$(MAKE) IE_PERF_COUNTERS=1 IE_BUILD_DIR=build/ie-counters ie-counters-smoke-run

ie-counters-smoke-run: $(IE_BUILD_DIR)/gfx.ie68
	$(IE_ENGINE) -nojit $(IE_BUILD_DIR)/gfx.ie68 -script ie/smoke/counters_smoke.ies

ie-counter-profile:
	$(MAKE) IE_PERF_COUNTERS=1 IE_BUILD_DIR=build/ie-counters ie-counter-profile-run

ie-counter-profile-run: $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin
	IE_NO_IPC=1 $(IE_RUN_ENV) $(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/loader.ie68 -script ie/smoke/counter_profile.ies

# ---- Full game image (subsystem 9) ------------------------------------
#
# The game links high at 0x10000000 (ie/game.ld) as a raw binary
# (game.bin) placed under build/ie/; a small loader image (loader.ie68,
# normal 0x1000 link) reads it through the FILE device and jumps to it.
# The game runs with -file-root at the repository root: assets resolve
# under rom_assets/ (IE_ASSET_PREFIX) and saves land in build/ie/
# (IE_SAVE_PREFIX), never inside the asset tree.

IE_GAME_SRC_DIRS := src src/data src/buffers src/racing src/ending \
  src/audio src/debug src/os courses src/compression src/gfx \
  $(wildcard assets/code/*) $(wildcard courses/*)

# crash_screen is the N64 exception handler (needs generated texture
# includes and an N64 thread context); platform_fatal covers the IE
# build. libmio0/libtkmk00 keep their compressors host-only.
# The rainbow tlut arrays are defined by all_kinds_of_buffers.c and the
# rsp-init display list by src/ending/dl_unk_80284EE0.c; compiling the
# assets/code copies would duplicate their symbols.
IE_GAME_EXCLUDE := src/crash_screen.c \
  src/debug/crash_screen_enhancement.c \
  assets/code/rainbow_road_tluts/rainbow_road_tluts.c \
  assets/code/ceremony_rsp_init_80284EE0/ceremony_rsp_init_80284EE0.c \
  src/audio/mixer.c \
  src/audio/synthesis.c
# course_textures.linkonly.c files are generated (gitignored); the rule
# below rebuilds them from each course's offsets table. assets/code/*
# has NO in-tree generator - it must exist in the checkout, so fail
# early with a clear message instead of deep in the compile.
courses/%/course_textures.linkonly.c: courses/%/course_offsets.c tools/linkonly_generator.py
	$(PYTHON) tools/linkonly_generator.py $*

IE_GAME_LINKONLY := $(patsubst courses/%/course_offsets.c,courses/%/course_textures.linkonly.c,$(wildcard courses/*/course_offsets.c))

ie-game-preflight:
	@if [ ! -f assets/code/common_data/common_data.c ] \
	  || [ ! -f assets/code/common_data/common_texture_bomb_1.ci8.inc.c ] \
	  || [ ! -f assets/code/ceremony_data/texture_podium1.rgba16.inc.c ]; then \
	  echo "ERROR: assets/code/ sources missing (wrapper .c and .inc.c payloads have no in-tree generator; they must be tracked in the checkout)"; \
	  exit 1; \
	fi

IE_GAME_C_RAW := $(filter-out $(IE_GAME_EXCLUDE),$(foreach dir,$(IE_GAME_SRC_DIRS),$(wildcard $(dir)/*.c)))
# course_displaylists.inc.c / course_vertices.inc.c are standalone
# translation units; every other .inc.c is #included by a parent.
# IE_GAME_LINKONLY is appended explicitly: on a clean checkout those
# files do not exist yet when the wildcard above expands, so they
# would otherwise drop out of the object list ($(sort) dedups the
# dirty-tree case where the wildcard already found them).
IE_GAME_C := $(sort $(filter-out %.inc.c,$(IE_GAME_C_RAW)) \
  $(filter %/course_displaylists.inc.c %/course_vertices.inc.c,$(IE_GAME_C_RAW)) \
  $(IE_GAME_LINKONLY))

# Binary data segments (kart frames, shared textures) assembled from
# their .incbin sources; ie/macros.inc provides an a.out glabel.
# Excluded as duplicates: zkarts/all_kart.s repeats every data/karts
# symbol; course_player_selection.s and texture_data_2.s are strict
# subsets of textures_0a.s. sound_data/*.s stays out: audio segments
# load at runtime through platform_asset_read.
IE_GAME_S := $(filter-out data/course_player_selection.s data/texture_data_2.s,$(wildcard data/*.s)) \
  $(wildcard data/karts/*.s)

IE_GAME_GAME_OBJS = $(IE_GAME_C:%.c=$(IE_BUILD_DIR)/game/%.o) \
  $(IE_GAME_S:%.s=$(IE_BUILD_DIR)/game/%.o) \
  $(IE_AUDIO_VOICE_GAME_OBJS)

IE_GAME_RUNTIME_OBJS := \
  $(IE_BUILD_DIR)/start_game.o \
  $(IE_BUILD_DIR)/game/ie/ie_runtime.o \
  $(IE_BUILD_DIR)/game/ie/ie_libc.o \
  $(IE_BUILD_DIR)/game/ie/ie_malloc.o \
  $(IE_BUILD_DIR)/game/ie/ie_math.o \
  $(IE_BUILD_DIR)/game/ie/ie_fmt.o \
  $(IE_BUILD_DIR)/game/ie/ie_platform_log.o \
  $(IE_BUILD_DIR)/game/ie/ie_platform_time.o \
  $(IE_BUILD_DIR)/game/ie/ie_platform_asset.o \
  $(IE_BUILD_DIR)/game/ie/ie_platform_input.o \
  $(IE_BUILD_DIR)/game/ie/ie_platform_save.o \
  $(IE_BUILD_DIR)/game/ie/ie_platform_audio.o \
  $(IE_BUILD_DIR)/game/ie/ie_gfx_voodoo.o \
  $(IE_BUILD_DIR)/game/ie/ie_gfx_window.o \
  $(IE_BUILD_DIR)/game/ie/coproc/ie_coproc.o \
  $(IE_BUILD_DIR)/game/ie/ie_game_main.o \
  $(IE_BUILD_DIR)/game/ie/ie_game_stubs.o

# Coprocessor service on/off defaults. These MUST be established before the
# IE_GAME_RUNTIME_OBJS conditionals below, because make evaluates ifneq in
# parse order; the -DIE_GFX_SVC/-DIE_AUDIO_SVC defines and the *_BIN wiring
# are set further down (guarded by the same variables). Default: on.
IE_GFX_SVC ?= 1
IE_AUDIO_SVC ?= 1

ifneq ($(IE_GFX_SVC),)
IE_GAME_RUNTIME_OBJS += $(IE_BUILD_DIR)/game/ie/ie_gfx_svc_client.o
endif

ifneq ($(IE_AUDIO_SVC),)
IE_GAME_RUNTIME_OBJS += $(IE_BUILD_DIR)/game/ie/ie_audio_svc_client.o
endif

ifneq ($(IE_PERF_COUNTERS),)
IE_GAME_RUNTIME_OBJS += $(IE_BUILD_DIR)/game/ie/ie_perf_counters.o
endif

# Opt-in audio diagnostics (sequence loads, note allocation, synthesis
# probes): IE_AUDIO_TRACE=1 make ie-game. Default builds carry none of
# the trace I/O - the target is CPU-bound as it is.
IE_AUDIO_TRACE ?=
IE_GAME_TRACE_DEFINES :=
IE_AUDIO_VOICE_GAME_OBJS := $(IE_BUILD_DIR)/game/gen/ie_audio_sample_table.o
IE_AUDIO_VOICE_ASSETS := $(IE_AUDIO_PCM16_ASSET)
ifneq ($(IE_AUDIO_TRACE),)
IE_GAME_TRACE_DEFINES += -DAUDIO_LOAD_TRACE
endif
IE_GAME_TRACE_DEFINES += -DIE_AUDIO_VOICES

# IE64 T&L coprocessor (build/ie/tnl.ie64) is ON by default: the race A/B
# measured it faster than the local path, and boot falls back to local T&L
# cleanly if the service file is missing. IE_TNL= (empty) builds without it.
IE_TNL ?= 1
IE_GAME_DEFINES := $(IE_GAME_TRACE_DEFINES) \
  -DIE_ASSET_PREFIX='"rom_assets/"' -DIE_SAVE_PREFIX='"build/ie/"' \
  -I$(IE_BUILD_DIR)/gen
ifneq ($(IE_TNL),)
IE_GAME_DEFINES += -DIE_TNL_COPROC
IE_TNL_SVC := $(IE_BUILD_DIR)/tnl.ie64
else
IE_TNL_SVC :=
endif

# M68K gfx-translation service (plan Phase 1). On by default: compiles the
# client seams into the game and embeds/loads build/ie/gfxsvc.ie68. Disable
# with IE_GFX_SVC= (empty) to build the local synchronous path - same sources,
# no fork; the local path also runs automatically if COSTART fails at boot.
# IE_GFX_SVC_SYNC=1 additionally drains every frame immediately after enqueue
# (bring-up / bisection mode, no pipelining).
IE_GFX_SVC ?= 1
IE_GFX_SVC_SYNC ?=
ifneq ($(IE_GFX_SVC),)
IE_GAME_DEFINES += -DIE_GFX_SVC
ifneq ($(IE_GFX_SVC_SYNC),)
IE_GAME_DEFINES += -DIE_GFX_SVC_SYNC
endif
IE_GFX_SVC_BIN := $(IE_BUILD_DIR)/gfxsvc.ie68
else
IE_GFX_SVC_BIN :=
endif

# M68K audio-sequencer service (plan Phase 2, worker B = second M68K
# instance). On by default: compiles the client seams into the game and
# embeds/loads build/ie/audiosvc.ie68. Disable with IE_AUDIO_SVC= (empty) to
# build the local pump - same sources, no fork; the local pump also runs
# automatically if COSTART/INIT fails at boot.
IE_AUDIO_SVC ?= 1
ifneq ($(IE_AUDIO_SVC),)
IE_GAME_DEFINES += -DIE_AUDIO_SVC
IE_AUDIO_SVC_BIN := $(IE_BUILD_DIR)/audiosvc.ie68
else
IE_AUDIO_SVC_BIN :=
endif

# Toggling IE_AUDIO_SVC changes the seams compiled into these objects but
# not their prerequisites; a config stamp forces the rebuild on change.
IE_AUDIO_SVC_STAMP := $(IE_BUILD_DIR)/.audio_svc_$(if $(IE_AUDIO_SVC),on,off).stamp
$(IE_AUDIO_SVC_STAMP):
	@mkdir -p $(IE_BUILD_DIR)
	@rm -f $(IE_BUILD_DIR)/.audio_svc_*.stamp
	@touch $@
$(IE_BUILD_DIR)/game/src/audio/port_eu.o: $(IE_AUDIO_SVC_STAMP)
$(IE_BUILD_DIR)/game/src/audio/external.o: $(IE_AUDIO_SVC_STAMP)
$(IE_BUILD_DIR)/game/src/main.o: $(IE_AUDIO_SVC_STAMP)

# Toggling IE_GFX_SVC/IE_GFX_SVC_SYNC changes gfx_pc.c's compiled seams but
# not its prerequisites; a config stamp forces the rebuild on change.
IE_GFX_SVC_STAMP := $(IE_BUILD_DIR)/.gfx_svc_$(if $(IE_GFX_SVC),on$(if $(IE_GFX_SVC_SYNC),-sync,),off).stamp
$(IE_GFX_SVC_STAMP):
	@mkdir -p $(IE_BUILD_DIR)
	@rm -f $(IE_BUILD_DIR)/.gfx_svc_*.stamp
	@touch $@
$(IE_BUILD_DIR)/game/src/gfx/gfx_pc.o: $(IE_GFX_SVC_STAMP)

# Compiled-in textures: n64graphics converts each PNG to a C texel
# array include, exactly like the previous console build; sources
# reference them as "textures/standalone/<name>.inc.c" resolved via
# -I$(IE_BUILD_DIR)/gen.
IE_TEX_PNG := $(wildcard textures/standalone/*.png)
IE_TEX_INC := $(IE_TEX_PNG:textures/standalone/%.png=$(IE_BUILD_DIR)/gen/textures/standalone/%.inc.c)

$(IE_BUILD_DIR)/gen/textures/standalone/%.inc.c: textures/standalone/%.png
	@mkdir -p $(dir $@)
	@./tools/n64graphics -i $@ -g $< -f $(lastword $(subst ., ,$*)) -s u8

ie-textures: $(IE_TEX_INC)

# Raw and mio0-compressed texture binaries referenced by the data/*.s
# incbin segments (list parsed from the .s sources; lazy so host-only
# invocations never run the shell).
IE_TEX_BIN_NAMES = $(shell grep -rhoE '\.incbin "textures/[^"]+"' data/*.s data/karts/*.s data/zkarts/*.s 2>/dev/null | sed 's/.incbin "//; s/"//' | sort -u)
IE_TEX_BINS = $(addprefix $(IE_BUILD_DIR)/gen/,$(IE_TEX_BIN_NAMES))

define IE_TEX_RAW_RULE
$$(IE_BUILD_DIR)/gen/textures/%.$(1): textures/%.$(1).png
	@mkdir -p $$(dir $$@)
	@./tools/n64graphics -i $$@ -g $$< -f $(1) -s raw
endef
$(foreach fmt,i4 i8 ia8 ia16 rgba16 rgba32,$(eval $(call IE_TEX_RAW_RULE,$(fmt))))

$(IE_BUILD_DIR)/gen/textures/%.mio0: $(IE_BUILD_DIR)/gen/textures/%
	@./tools/mio0 -c $< $@

.PRECIOUS: $(IE_BUILD_DIR)/gen/textures/%

ie-texture-bins:
	@$(MAKE) $(IE_TEX_BINS)

$(IE_BUILD_DIR)/start_game.o: ie/start_game.S
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -c -o $@ $<

$(IE_BUILD_DIR)/game/ie/%.o: ie/%.c
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_GAME_DEFINES) -c -o $@ $<

$(IE_BUILD_DIR)/game/ie/ie_platform_audio.o $(IE_BUILD_DIR)/game/src/main.o: $(IE_AUDIO_SAMPLE_TABLE_H)

$(IE_AUDIO_SAMPLE_TABLE_C) $(IE_AUDIO_SAMPLE_TABLE_H) $(IE_AUDIO_PCM16_ASSET) &: tools/ie_audio_pcm16.py $(ASSETS_DIR)/audiobanks.bin $(ASSETS_DIR)/audiotables.bin src/audio/data.c
	$(PYTHON) tools/ie_audio_pcm16.py \
	  --audiobanks $(ASSETS_DIR)/audiobanks.bin \
	  --audiotables $(ASSETS_DIR)/audiotables.bin \
	  --data-c src/audio/data.c \
	  --pcm-out $(IE_AUDIO_PCM16_ASSET) \
	  --c-out $(IE_AUDIO_SAMPLE_TABLE_C) \
	  --h-out $(IE_AUDIO_SAMPLE_TABLE_H)

ie-audio-assets: $(IE_AUDIO_SAMPLE_TABLE_C) $(IE_AUDIO_SAMPLE_TABLE_H) $(IE_AUDIO_PCM16_ASSET)

ie-audio-selftest:
	$(PYTHON) tools/ie_audio_pcm16.py --self-test

$(IE_BUILD_DIR)/game/gen/ie_audio_sample_table.o: $(IE_AUDIO_SAMPLE_TABLE_C) $(IE_AUDIO_SAMPLE_TABLE_H)
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_GAME_DEFINES) -c -o $@ $(IE_AUDIO_SAMPLE_TABLE_C)

# Segment-2 objects: zero-initialised arrays must stay in .data or
# they leave holes in the segment layout the numeric offsets encode.
$(IE_BUILD_DIR)/game/src/data/textures.o: src/data/textures.c | ie-textures
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_GAME_DEFINES) -fno-zero-initialized-in-bss -c -o $@ $<

# -UAVOID_UB: the guarded display lists are real segment content;
# omitting them punches a 0x300 hole in the numeric-offset layout.
$(IE_BUILD_DIR)/game/src/data/data_segment2.o: src/data/data_segment2.c | ie-textures
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_GAME_DEFINES) -fno-zero-initialized-in-bss -UAVOID_UB -c -o $@ $<

$(IE_BUILD_DIR)/game/%.o: %.c | ie-textures
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) $(IE_GAME_DEFINES) -c -o $@ $<

$(IE_BUILD_DIR)/game/%.o: %.s | ie-texture-bins
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -Wa,-Iie -Wa,-I$(IE_BUILD_DIR)/gen -c -o $@ $<

# The boot logo draws from a mio0-compressed segment the game
# decompresses at runtime (segment 6); build it from the compiled
# startup_logo data, exactly like the previous console port did.
$(IE_BUILD_DIR)/gen/startup_logo.bin: $(IE_BUILD_DIR)/game/assets/code/startup_logo/startup_logo.o
	@mkdir -p $(dir $@)
	m68k-atari-mint-objcopy -O binary --only-section=.data $< $@

$(IE_BUILD_DIR)/gen/startup_logo.mio0: $(IE_BUILD_DIR)/gen/startup_logo.bin
	./tools/mio0 -c $< $@

$(IE_BUILD_DIR)/gen/startup_logo_seg.s: $(IE_BUILD_DIR)/gen/startup_logo.mio0
	@printf '.include "macros.inc"\n.data\nglabel _startupLogoSegmentRomStart\n.incbin "%s"\n' $< > $@

$(IE_BUILD_DIR)/game/startup_logo_seg.o: $(IE_BUILD_DIR)/gen/startup_logo_seg.s
	@mkdir -p $(dir $@)
	$(IE68_CC) $(IE68_CFLAGS) -Wa,-Iie -c -o $@ $<

$(IE_BUILD_DIR)/game.bin $(IE_BUILD_DIR)/game.map &: ie-game-preflight $(IE_GAME_LINKONLY) $(IE_AUDIO_VOICE_ASSETS) ie/game.ld $(IE_GAME_RUNTIME_OBJS) $(IE_GAME_GAME_OBJS) $(IE_BUILD_DIR)/game/startup_logo_seg.o $(IE_TNL_SVC)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/game.ld -Wl,--oformat,binary \
	  -Wl,-Map,$(IE_BUILD_DIR)/game.map \
	  -o $(IE_BUILD_DIR)/game.bin $(IE_GAME_RUNTIME_OBJS) $(IE_GAME_GAME_OBJS) \
	  $(IE_BUILD_DIR)/game/startup_logo_seg.o $(IE68_LIBGCC)
	@# Instantiate the race smoke scripts from their templates with the
	@# fresh gGamestate address; generated copies live in the build dir
	@# so a relink never mutates tracked sources.
	@ADDR=$$(grep -E '_gGamestate$$' $(IE_BUILD_DIR)/game.map | grep -oE '0x[0-9a-fA-F]+' | head -1); \
	for t in race_fps race_fps_lite drive_profile drive_fps_lite pc_sample intro_pc_sample svc_pc_sample svc_timing_probe svc_pan_probe; do \
	  sed "s/@GAMESTATE_ADDR@/$$ADDR/" ie/smoke/$$t.ies.in > $(IE_BUILD_DIR)/$$t.ies; \
	done

IE_LOADER_OBJS := \
  $(IE_BUILD_DIR)/start.o \
  $(IE_BUILD_DIR)/ie_runtime.o \
  $(IE_BUILD_DIR)/ie_libc.o \
  $(IE_BUILD_DIR)/ie_fmt.o \
  $(IE_BUILD_DIR)/ie_platform_log.o \
  $(IE_BUILD_DIR)/ie_platform_asset.o \
  $(IE_BUILD_DIR)/loader_main.o

$(IE_BUILD_DIR)/loader.ie68: ie/link.ld $(IE_LOADER_OBJS)
	$(IE68_CC) $(IE68_CFLAGS) -Wl,-T,ie/link.ld -Wl,--oformat,binary \
	  -o $@ $(IE_LOADER_OBJS) $(IE68_LIBGCC)

# The game image runs with the JIT: soft-float chews through billions
# of instructions per frame and the interpreter is several times
# slower still. Subsystem smokes stay pinned to -nojit for determinism.
# Build-only: produces loader.ie68 + game.bin + tnl.ie64. Launch the
# engine yourself (e.g. go run . -file-root=../mk64-ie/ ../mk64-ie/build/ie/loader.ie68).
ie-game: $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin $(IE_TNL_SVC) $(IE_GFX_SVC_BIN) $(IE_AUDIO_SVC_BIN)

ie-game-run: ie-game
	$(IE_RUN_ENV) $(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/loader.ie68

# Self-contained single-file image: loader + header/TOC + rom_assets +
# T&L service + game image, laid out per ie/ie_pack.h. Boots from any
# directory with no -file-root (saves and the coprocessor need a
# writable root; both degrade gracefully without one).
$(IE_BUILD_DIR)/mariokart64.ie68: $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin $(IE_TNL_SVC) $(IE_GFX_SVC_BIN) $(IE_AUDIO_SVC_BIN) ie/pack_ie68.py ie/ie_pack.h
	python3 ie/pack_ie68.py $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin rom_assets $(if $(IE_TNL),$(IE_BUILD_DIR)/tnl.ie64,-) $@ $(if $(IE_GFX_SVC),$(IE_BUILD_DIR)/gfxsvc.ie68,-) $(if $(IE_AUDIO_SVC),$(IE_BUILD_DIR)/audiosvc.ie68,-)

ie-pack: $(IE_BUILD_DIR)/mariokart64.ie68

# No -file-root: the packed image is self-contained and the engine
# roots guest file I/O at the image's own directory, so runtime saves
# land in $(IE_BUILD_DIR) (ignored) - and the smoke exercises the same
# launch mode users get.
ie-pack-smoke: $(IE_BUILD_DIR)/mariokart64.ie68
	IE_NO_IPC=1 $(IE_RUN_ENV) $(IE_ENGINE) -script ie/smoke/game_smoke.ies $(IE_BUILD_DIR)/mariokart64.ie68

ie-game-smoke: $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin $(IE_TNL_SVC)
	$(IE_RUN_ENV) $(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/loader.ie68 -script ie/smoke/game_smoke.ies

ie-audio-smoke: $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin
	$(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/loader.ie68 -script ie/smoke/audio_smoke.ies

ie-audio-soak: $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin
	$(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/loader.ie68 -script ie/smoke/audio_soak.ies

# Measures fps over a fixed title-attract swap span (scene-identical
# across builds, so numbers are comparable). Takes several minutes to
# warm up to the span at current speeds. IE_NO_IPC keeps the run from
# attaching to a live GUI engine session.
ie-fps: $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin $(IE_TNL_SVC)
	IE_NO_IPC=1 $(IE_RUN_ENV) $(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/loader.ie68 -script ie/smoke/fps_measure.ies

ie-race-fps: $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin $(IE_TNL_SVC) $(IE_GFX_SVC_BIN)
	IE_NO_IPC=1 $(IE_RUN_ENV) $(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/loader.ie68 -script $(IE_BUILD_DIR)/race_fps.ies

# Poll-free variant: two debugger samples bracketing an untouched span.
# Prefer this for absolute numbers; per-freeze stalls make the polling
# scripts understate fps badly once the game runs fast (see PERF-NOTES).
ie-race-fps-lite: $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin $(IE_TNL_SVC) $(IE_GFX_SVC_BIN)
	IE_NO_IPC=1 $(IE_RUN_ENV) $(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/loader.ie68 -script $(IE_BUILD_DIR)/race_fps_lite.ies

# Standalone instantiation rule for templated smoke scripts. The
# game.bin recipe regenerates all of them on relink, but when game.bin
# is already up to date that recipe never runs, so a new or edited
# template needs its own target. game.map (co-produced with game.bin by
# the grouped link above) is a direct prerequisite so deleting or
# replacing it forces regeneration - the recipe reads it for the address.
$(IE_BUILD_DIR)/%.ies: ie/smoke/%.ies.in $(IE_BUILD_DIR)/game.map
	@ADDR=$$(grep -E '_gGamestate$$' $(IE_BUILD_DIR)/game.map | grep -oE '0x[0-9a-fA-F]+' | head -1); \
	sed "s/@GAMESTATE_ADDR@/$$ADDR/" $< > $@

# Correlated PC sampling of main + gfx worker + audio worker over the
# attract race steady state. Freeze overhead depresses fps; the
# distributions stay valid. Map with game.map/gfxsvc.map/audiosvc.map.
ie-svc-pc-sample: $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin $(IE_TNL_SVC) $(IE_GFX_SVC_BIN) $(IE_AUDIO_SVC_BIN) $(IE_BUILD_DIR)/svc_pc_sample.ies
	IE_NO_IPC=1 $(IE_RUN_ENV) $(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/loader.ie68 -script $(IE_BUILD_DIR)/svc_pc_sample.ies

# Worker self-timed frame/translation/pump numbers over the attract
# race, two reads bracketing an untouched span (see the template).
ie-svc-timing-probe: $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin $(IE_TNL_SVC) $(IE_GFX_SVC_BIN) $(IE_AUDIO_SVC_BIN) $(IE_BUILD_DIR)/svc_timing_probe.ies
	IE_NO_IPC=1 $(IE_RUN_ENV) $(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/loader.ie68 -script $(IE_BUILD_DIR)/svc_timing_probe.ies

# Freeze-free profile of the course-intro pan (worker-A self-timed, two
# freezes only): attributes the pan to gfx translation vs a main-side stall.
ie-svc-pan-probe: $(IE_BUILD_DIR)/loader.ie68 $(IE_BUILD_DIR)/game.bin $(IE_TNL_SVC) $(IE_GFX_SVC_BIN) $(IE_AUDIO_SVC_BIN) $(IE_BUILD_DIR)/svc_pan_probe.ies
	IE_NO_IPC=1 $(IE_RUN_ENV) $(IE_ENGINE) -file-root . $(IE_BUILD_DIR)/loader.ie68 -script $(IE_BUILD_DIR)/svc_pan_probe.ies

# Boots the same image twice against a cleared scratch file-root:
# first boot proves fresh-storage round-trips, second boot proves the
# records survived the reboot (plus ghost delete and save rewrite).
IE_SAVE_SCRATCH := $(IE_BUILD_DIR)/savedata

ie-save-smoke: $(IE_BUILD_DIR)/save.ie68
	rm -rf $(IE_SAVE_SCRATCH)
	mkdir -p $(IE_SAVE_SCRATCH)
	$(IE_ENGINE) -nojit -file-root $(IE_SAVE_SCRATCH) $(IE_BUILD_DIR)/save.ie68 \
	  -script ie/smoke/save_smoke_fresh.ies
	$(IE_ENGINE) -nojit -file-root $(IE_SAVE_SCRATCH) $(IE_BUILD_DIR)/save.ie68 \
	  -script ie/smoke/save_smoke_persist.ies

# Needs the generated Stage 1.5 layout (make assets); served to the
# guest via the engine's -file-root.
ie-asset-smoke: $(IE_BUILD_DIR)/asset.ie68
	$(IE_ENGINE) -nojit -file-root $(ASSETS_DIR) $(IE_BUILD_DIR)/asset.ie68 \
	  -script ie/smoke/asset_smoke.ies

clean:
	$(RM) -r $(BUILD_DIR) $(IE_BUILD_DIR)

distclean:
	$(RM) -r build

.PHONY: all host test smoke assets assets-verify assets-test ie-hello ie-smoke ie-time-smoke ie-input-smoke ie-save-smoke ie-gfx-smoke ie-audio-smoke ie-audio-soak ie-audio-ring-smoke ie-audio-selftest ie-asset-smoke ie-counters-smoke ie-counters-smoke-run ie-counter-profile ie-counter-profile-run ie-game ie-game-smoke ie-fps ie-race-fps ie-race-fps-lite ie-coproc-smoke clean distclean

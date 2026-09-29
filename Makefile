# Makefile — Omega Schwung module.
#
# Two compilers in play (D-11):
#   - XCC (aarch64-linux-gnu-gcc) cross-compiles the shipped dsp.so.
#   - CC  (native cc) compiles the offline test harness.
#
# Targets:
#   dsp.so  — cross-compiled aarch64 shared object (expected to fail until
#             Plan A-02 adds move_plugin_init_v2; `make test` is the Wave 0 gate)
#   test    — native offline harness: compiles, runs, writes tests/output WAV
#   clean   — remove build artifacts
#   deploy  — atomic scp deploy to the Move device (D-14)

# --- Compilers ---------------------------------------------------------------
CC  ?= cc
XCC  = aarch64-linux-gnu-gcc

# --- aarch64 (shipped) flags — granular fast-math subset; no CPU pinning (D-15
#     defers the Cortex core flag until on-device /proc/cpuinfo confirms it) ---
# NOTE: -lm is NOT here. GNU ld resolves libraries left-to-right, so -lm must
# appear AFTER the sources that reference it (see $(LDLIBS), appended in the
# recipes below). macOS's linker is order-insensitive, but Linux CI is not.
AARCH_FLAGS = -std=gnu11 -O3 -shared -fPIC -Isrc \
              -fno-math-errno -ffp-contract=fast \
              -fvisibility=hidden -Wl,--no-undefined

# --- Native test flags -------------------------------------------------------
# The malloc trap (-DOMEGA_MALLOC_TRAP) relies on __libc_* interposition, which
# only works under glibc/Linux. On Darwin the trap is compiled out; CI (Linux)
# is the authoritative FNDTN-03 gate.
TEST_FLAGS = -std=gnu11 -O2 -Isrc -Itests
UNAME_S := $(shell uname -s)
ifneq ($(UNAME_S),Darwin)
    TEST_FLAGS += -DOMEGA_MALLOC_TRAP
endif

# Link libraries — MUST be last on the link line for GNU ld (see AARCH_FLAGS note).
LDLIBS = -lm

# --- Sources -----------------------------------------------------------------
DSP_SRCS  = $(wildcard src/*.c) $(wildcard src/models/*.c)
# Full-lifecycle harness (A-02 Task 3): drives move_plugin_init_v2 through the
# real dsp.c / fm2.c / registry (create -> on_midi -> render x512 -> destroy).
TEST_SRCS = tests/test_render.c tests/mock_host.c tests/wav.c tests/malloc_trap.c \
            src/dsp.c src/ui.c src/models/fm2.c src/models/model_registry.c \
            src/dsp_primitives.c
# Focused FM2 engine unit test (A-02 Task 1): exercises fm2_* directly against
# a stack instance (no dsp.c lifecycle) — non-silent, deterministic, params.
FM2_TEST_SRCS = tests/test_fm2.c tests/malloc_trap.c \
                src/models/fm2.c src/dsp_primitives.c
# Post-kick FX chain unit test (B-02 Task 1, KICK-14): drives fx_config/
# fx_process directly — bounded-at-max, transparent-at-zero, audible-at-max,
# Crush statefulness, safe uninitialized crush_levels.
FX_TEST_SRCS = tests/test_fx.c tests/malloc_trap.c src/dsp_primitives.c
# Model-switch hazard harness (B-01 Task 3, KICK-13): drives the real lifecycle
# and asserts A->B->A + trigger stays finite/bounded/non-stale. Uses the
# src/models/*.c wildcard so each new model TU is picked up automatically as
# later plans replace registry NULL slots.
SWITCH_TEST_SRCS = tests/test_switch.c tests/mock_host.c tests/wav.c \
                   tests/malloc_trap.c src/dsp.c src/ui.c \
                   $(wildcard src/models/*.c) src/dsp_primitives.c

.PHONY: dsp.so test test-fm2 test-switch test-fx wavetables clean deploy

# --- Generated wavetables (B-02 Task 3, KICK-15) -----------------------------
# src/wavetables.h defines g_wavetables[NUM_WAVES][BANDS][2049] in .rodata,
# emitted by the host-side (native cc) generator. Committed like sine_table.h
# so CI never regenerates; `make wavetables` (re)emits it. dsp.so and test
# depend on the header EXISTING (order-only) so a clean checkout builds it once.
src/wavetables.h: tools/gen_wavetables.c
	@mkdir -p build
	$(CC) -std=gnu11 -O2 tools/gen_wavetables.c -o build/gen_wavetables $(LDLIBS)
	./build/gen_wavetables > $@

# Convenience: force-regenerate the wavetable header.
wavetables: tools/gen_wavetables.c
	@mkdir -p build
	$(CC) -std=gnu11 -O2 tools/gen_wavetables.c -o build/gen_wavetables $(LDLIBS)
	./build/gen_wavetables > src/wavetables.h

# dsp.so: cross-compiled module. src/*.c wildcard already covers src/ui.c (A-03).
dsp.so: | src/wavetables.h
	@mkdir -p build
	$(XCC) $(AARCH_FLAGS) $(DSP_SRCS) -o build/dsp.so $(LDLIBS)

# test: native gate. Runs the FM2 unit test, the switch harness, then the
# full offline lifecycle harness.
test: test-fm2 test-fx test-switch | src/wavetables.h
	@mkdir -p build tests/output
	$(CC) $(TEST_FLAGS) $(TEST_SRCS) -o build/test_render $(LDLIBS)
	./build/test_render

# test-fm2: focused FM2 DSP unit test (< 5 s).
test-fm2: | src/wavetables.h
	@mkdir -p build
	$(CC) $(TEST_FLAGS) $(FM2_TEST_SRCS) -o build/test_fm2 $(LDLIBS)
	./build/test_fm2

# test-fx: post-kick FX chain unit test (KICK-14, < 5 s).
test-fx: | src/wavetables.h
	@mkdir -p build
	$(CC) $(TEST_FLAGS) $(FX_TEST_SRCS) -o build/test_fx $(LDLIBS)
	./build/test_fx

# test-switch: model-switch re-init hazard harness (KICK-13). Forward-
# compatible — skips NULL registry slots.
test-switch: | src/wavetables.h
	@mkdir -p build
	$(CC) $(TEST_FLAGS) $(SWITCH_TEST_SRCS) -o build/test_switch $(LDLIBS)
	./build/test_switch

clean:
	rm -rf build tests/output

deploy:
	./scripts/deploy.sh

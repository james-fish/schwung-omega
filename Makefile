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
# Uses the src/models/*.c wildcard so each model TU (fm2 + every B-04..B-08
# model) is compiled and its vtable symbol resolves once its registry NULL slot
# is replaced (avoids an undefined-symbol link error when the registry names a
# model whose .c is not in this list).
TEST_SRCS = tests/test_render.c tests/mock_host.c tests/wav.c tests/malloc_trap.c \
            src/dsp.c src/groove.c src/ui.c src/params.c $(wildcard src/models/*.c) \
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
                   tests/malloc_trap.c src/dsp.c src/groove.c src/ui.c src/params.c \
                   $(wildcard src/models/*.c) src/dsp_primitives.c
# Reusable per-model voicing battery (B-03 Task 2, D-B02): drives the real
# lifecycle and asserts non-silent default + each param measurably changes the
# output + bounded-at-extremes. Uses the src/models/*.c wildcard so later model
# TUs are picked up automatically as they register.
PARAMS_TEST_SRCS = tests/test_params.c tests/mock_host.c tests/wav.c \
                   tests/malloc_trap.c src/dsp.c src/groove.c src/ui.c src/params.c \
                   $(wildcard src/models/*.c) src/dsp_primitives.c
# Pairwise distinctness metric across registered models (B-03 Task 3, D-B02):
# renders each registered model's default and asserts pairwise feature deltas.
DISTINCT_TEST_SRCS = tests/test_distinct.c tests/mock_host.c tests/wav.c \
                     tests/malloc_trap.c src/dsp.c src/groove.c src/ui.c src/params.c \
                     $(wildcard src/models/*.c) src/dsp_primitives.c
# GEN determinism + USR off-render load + complete-registry gate (B-08 Task 3):
# KICK-11 seed-stable/seed-differ/density; KICK-10 fixture-load + fallback; and
# a runtime assert that all MODEL_COUNT registry slots are non-NULL.
GEN_TEST_SRCS = tests/test_gen.c tests/mock_host.c tests/wav.c \
                tests/malloc_trap.c src/dsp.c src/groove.c src/ui.c src/params.c \
                $(wildcard src/models/*.c) src/dsp_primitives.c
# Groove rumble engine harness (C-01, GRV-01/02/03/05): drives the real
# lifecycle through the tempo-DRIVABLE mock host and asserts tap positions track
# driven BPM (120/128/174), Page-1 responsiveness, and MONO channel equality.
# EXPECTED to fail RED until C-02 lands the groove engine; goes green then.
GROOVE_TEST_SRCS = tests/test_groove.c tests/mock_host.c tests/wav.c \
                   tests/malloc_trap.c src/dsp.c src/groove.c src/ui.c src/params.c \
                   $(wildcard src/models/*.c) src/dsp_primitives.c
# Param value readback + per-model memory harness (B1, UIX-01/04): set->get
# echo, create defaults, per-model state memory across switches, global
# round-trip, unknown-key -1, locale-independent formatter.
READBACK_TEST_SRCS = tests/test_readback.c tests/mock_host.c tests/wav.c \
                   tests/malloc_trap.c src/dsp.c src/groove.c src/ui.c src/params.c \
                   $(wildcard src/models/*.c) src/dsp_primitives.c
# Sample infrastructure harness (B3, SMPL-01/02/03): enumeration + picker enum +
# selection + absent-dir graceful. Writes WAVs into a temp module_dir/samples/.
SAMPLES_TEST_SRCS = tests/test_samples.c tests/mock_host.c tests/wav.c \
                   tests/malloc_trap.c src/dsp.c src/groove.c src/ui.c src/params.c \
                   $(wildcard src/models/*.c) src/dsp_primitives.c
# Performer chain harness (Phase D, PERF-01..05): duck + DJ filter + soft clip.
PERF_TEST_SRCS = tests/test_perf.c tests/mock_host.c tests/wav.c \
                   tests/malloc_trap.c src/dsp.c src/groove.c src/ui.c src/params.c \
                   $(wildcard src/models/*.c) src/dsp_primitives.c

.PHONY: dsp.so test test-fm2 test-switch test-fx test-params test-distinct test-gen test-groove test-readback test-samples test-perf fixtures wavetables clean deploy

# --- Generated wavetables (B-02 Task 3, KICK-15) -----------------------------
# src/wavetables.h defines g_wavetables[NUM_WAVES][BANDS][2049] in .rodata,
# emitted by the host-side (native cc) generator. Committed like sine_table.h
# so CI never regenerates; `make wavetables` (re)emits it. dsp.so and test
# depend on the header EXISTING (order-only) so a clean checkout builds it once.
src/wavetables.h: tools/gen_wavetables.c
	@mkdir -p build
	$(CC) -std=gnu11 -O2 tools/gen_wavetables.c -o build/gen_wavetables $(LDLIBS)
	./build/gen_wavetables > $@

# --- USR fixture WAV (B-08 Task 1, KICK-10) ----------------------------------
# tests/fixtures/user_kick.wav is a small valid PCM16 WAV the USR off-render
# loader (dsp.c usr_load_wav) reads. Generated by a host-side tool using the
# same tests/wav.c writer; an order-only prereq of `test` so a clean checkout
# always has the fixture before the USR-load test runs.
tests/fixtures/user_kick.wav: tools/gen_user_fixture.c tests/wav.c
	@mkdir -p build tests/fixtures
	$(CC) -std=gnu11 -O2 tools/gen_user_fixture.c tests/wav.c -o build/gen_user_fixture $(LDLIBS)
	./build/gen_user_fixture $@

# Convenience: (re)build the USR fixture.
fixtures: tests/fixtures/user_kick.wav

# Convenience: force-regenerate the wavetable header.
wavetables: tools/gen_wavetables.c
	@mkdir -p build
	$(CC) -std=gnu11 -O2 tools/gen_wavetables.c -o build/gen_wavetables $(LDLIBS)
	./build/gen_wavetables > src/wavetables.h

# dsp.so: cross-compiled module. src/*.c wildcard already covers src/ui.c (A-03).
dsp.so: | src/wavetables.h
	@mkdir -p build
	$(XCC) $(AARCH_FLAGS) $(DSP_SRCS) -o build/dsp.so $(LDLIBS)

# test: native gate. Runs the FM2 unit test, the FX unit test, the switch
# harness, the voicing battery, the distinctness metric, then the full offline
# lifecycle harness.
test: test-fm2 test-fx test-switch test-params test-distinct test-gen test-groove test-readback test-samples test-perf | src/wavetables.h tests/fixtures/user_kick.wav
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

# test-params: reusable per-model voicing battery (D-B02). Forward-compatible —
# each model plan adds its own assert_param_responsive call.
test-params: | src/wavetables.h
	@mkdir -p build tests/output
	$(CC) $(TEST_FLAGS) $(PARAMS_TEST_SRCS) -o build/test_params $(LDLIBS)
	./build/test_params

# test-distinct: pairwise distinctness metric across registered models (D-B02).
# Empty-trivial in Wave 2 (only FM2 registered); a real gate as models land.
test-distinct: | src/wavetables.h
	@mkdir -p build tests/output
	$(CC) $(TEST_FLAGS) $(DISTINCT_TEST_SRCS) -o build/test_distinct $(LDLIBS)
	./build/test_distinct

# test-gen: GEN determinism + USR off-render load + complete-registry gate
# (B-08, KICK-10/KICK-11). Depends on the USR fixture WAV.
test-gen: | src/wavetables.h tests/fixtures/user_kick.wav
	@mkdir -p build tests/output
	$(CC) $(TEST_FLAGS) $(GEN_TEST_SRCS) -o build/test_gen $(LDLIBS)
	./build/test_gen

# test-groove: Groove rumble engine harness (C-01, GRV-01/02/03/05). Drives the
# tempo-drivable mock at 120/128/174 BPM. EXPECTED to fail RED until C-02 lands
# the groove engine — this is the intended enabling state; goes green in C-02.
test-groove: | src/wavetables.h
	@mkdir -p build tests/output
	$(CC) $(TEST_FLAGS) $(GROOVE_TEST_SRCS) -o build/test_groove $(LDLIBS)
	./build/test_groove

test-readback: | src/wavetables.h
	@mkdir -p build
	$(CC) $(TEST_FLAGS) $(READBACK_TEST_SRCS) -o build/test_readback $(LDLIBS)
	./build/test_readback

test-samples: | src/wavetables.h
	@mkdir -p build
	$(CC) $(TEST_FLAGS) $(SAMPLES_TEST_SRCS) -o build/test_samples $(LDLIBS)
	./build/test_samples

test-perf: | src/wavetables.h
	@mkdir -p build
	$(CC) $(TEST_FLAGS) $(PERF_TEST_SRCS) -o build/test_perf $(LDLIBS)
	./build/test_perf

clean:
	rm -rf build tests/output

deploy:
	./scripts/deploy.sh

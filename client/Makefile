# 32-bit Windows build with mingw-w64 (brew install mingw-w64). The server public key header comes from
# tools/og_keygen.c (make keys); the secret seed stays in ../keys (git-ignored).
CC       = i686-w64-mingw32-gcc
# PROFILE_DIR: build folder of a Studio profile (og_modules.h, og_profile.h, og_modules.mk, resources). Without it
# the development profile in src/ with every module is built.
PROFILE_DIR ?=
OUT      ?= build
ifneq ($(PROFILE_DIR),)
include $(PROFILE_DIR)/og_modules.mk
PROFILE_INC = -I$(PROFILE_DIR)
PROFILE_DEPS = $(PROFILE_DIR)/og_modules.h $(PROFILE_DIR)/og_profile.h
else
OG_SRC  := $(filter-out src/og_ui.c,$(wildcard src/*.c)) src/og_ui.c ../proto/og_wire.c
PROFILE_INC = -Isrc/dev
PROFILE_DEPS = src/dev/og_modules.h src/dev/og_profile.h src/dev/og_pubkey.h
endif
# hardening: ASLR (relocations kept), DEP, stack protector; no timestamps (reproducible builds); linker map as
# proof which module objects are linked (tools/check_modules.py)
CFLAGS   = -O2 -Wall -Wextra -Wno-format-truncation -Wno-cast-function-type -std=gnu11 -DOG_BUILD -D_WIN32_WINNT=0x0601 \
           -fstack-protector-strong -ffile-prefix-map=$(CURDIR)=. -ffile-prefix-map=$(abspath ..)=.. $(PROFILE_INC)
LDFLAGS  = -s -static -static-libgcc -Wl,--kill-at -Wl,--no-insert-timestamp -Wl,--dynamicbase -Wl,--nxcompat \
           -Wl,-Map,$(OUT)/odinguard.map
MH_SRC   = ../third_party/minhook/src/hook.c ../third_party/minhook/src/buffer.c ../third_party/minhook/src/trampoline.c \
           ../third_party/minhook/src/hde/hde32.c
LIB_SRC  = ../third_party/monocypher.c $(MH_SRC)
LIBS     = -lws2_32 -liphlpapi -ladvapi32 -luser32 -lgdi32 -lmsimg32 -lssp -lgcc
KEYS     = ../keys
RC_DIR  ?= res

all: $(OUT)/odinguard.dll $(OUT)/ijl15.dll $(OUT)/selftest.exe $(OUT)/nettest.exe $(OUT)/inject.exe

$(OUT):
	mkdir -p $(OUT)

$(OUT)/og_keygen: ../tools/og_keygen.c ../third_party/monocypher.c | $(OUT)
	cc -O2 -o $@ ../tools/og_keygen.c ../third_party/monocypher.c

src/dev/og_pubkey.h: $(OUT)/og_keygen
	mkdir -p $(KEYS)
	$(OUT)/og_keygen $(KEYS) $@

keys: src/dev/og_pubkey.h

$(OUT)/odinguard_res.o: $(RC_DIR)/odinguard.rc $(RC_DIR)/odinguard.ico $(RC_DIR)/splash.bmp src/og_version.h $(PROFILE_DEPS) | $(OUT)
	i686-w64-mingw32-windres $(PROFILE_INC) -I $(RC_DIR) -I src $(RC_DIR)/odinguard.rc -O coff -o $@

# one object per module source: the linker map then names exactly the module sources that are in the DLL
OG_OBJ   = $(addprefix $(OUT)/obj/,$(notdir $(OG_SRC:.c=.o)))
LIB_OBJ  = $(addprefix $(OUT)/lib/,$(notdir $(LIB_SRC:.c=.o)))
vpath %.c src ../proto ../third_party ../third_party/minhook/src ../third_party/minhook/src/hde

$(OUT)/obj/%.o: %.c src/og.h src/og_version.h $(PROFILE_DEPS) ../proto/og_proto.h ../proto/og_wire.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/lib/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -std=gnu11 -w -ffile-prefix-map=$(abspath ..)=.. -c -o $@ $<

$(OUT)/odinguard.dll: $(OG_OBJ) $(LIB_OBJ) $(OUT)/odinguard_res.o | $(OUT)
	$(CC) -shared -o $@ $(OG_OBJ) $(LIB_OBJ) $(OUT)/odinguard_res.o $(LDFLAGS) $(LIBS)

$(OUT)/ijl15.dll: loader/ijl15_loader.c loader/ijl15_loader.def | $(OUT)
	$(CC) -O2 -Wall -s -shared -o $@ loader/ijl15_loader.c loader/ijl15_loader.def -Wl,--kill-at -Wl,--no-insert-timestamp

$(OUT)/selftest.exe: tests/selftest.c | $(OUT)
	$(CC) -O2 -Wall -static -o $@ tests/selftest.c

$(OUT)/nettest.exe: tests/nettest.c | $(OUT)
	$(CC) -O2 -Wall -static -o $@ tests/nettest.c -lws2_32

clean:
	rm -rf $(OUT)
.PHONY: all clean keys

$(OUT)/inject.exe: tests/inject.c | $(OUT)
	$(CC) -O2 -Wall -static -o $@ tests/inject.c

# native fuzz test of the wire parsers (host compiler, ASan + UBSan)
fuzz:
	@mkdir -p $(OUT)/host
	cc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -o $(OUT)/host/fuzz_wire tests/fuzz_wire.c ../proto/og_wire.c
	$(OUT)/host/fuzz_wire 2000000 7
.PHONY: fuzz

# 32-bit Windows build with mingw-w64 (brew install mingw-w64). The server public key header comes from
# tools/og_keygen.c (make keys); the secret seed stays in ../keys (git-ignored).
CC       = i686-w64-mingw32-gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-format-truncation -Wno-cast-function-type -std=gnu11 -DOG_BUILD -D_WIN32_WINNT=0x0601
LDFLAGS ?= -static -static-libgcc -Wl,--kill-at
OUT      = build
SRC      = $(wildcard src/*.c)
MH_SRC   = ../third_party/minhook/src/hook.c ../third_party/minhook/src/buffer.c ../third_party/minhook/src/trampoline.c \
           ../third_party/minhook/src/hde/hde32.c
LIB_SRC  = ../third_party/monocypher.c $(MH_SRC)
LIBS     = -lws2_32 -liphlpapi -ladvapi32 -luser32 -lgcc
KEYS     = ../keys

all: $(OUT)/odinguard.dll $(OUT)/ijl15.dll $(OUT)/selftest.exe $(OUT)/nettest.exe

$(OUT):
	mkdir -p $(OUT)

$(OUT)/og_keygen: ../tools/og_keygen.c ../third_party/monocypher.c | $(OUT)
	cc -O2 -o $@ ../tools/og_keygen.c ../third_party/monocypher.c

src/og_pubkey.h: $(OUT)/og_keygen
	mkdir -p $(KEYS)
	$(OUT)/og_keygen $(KEYS) $@

keys: src/og_pubkey.h

$(OUT)/odinguard.dll: $(SRC) src/og.h src/og_pubkey.h ../proto/og_proto.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $(SRC) $(LIB_SRC) $(LDFLAGS) $(LIBS)

$(OUT)/ijl15.dll: loader/ijl15_loader.c loader/ijl15_loader.def | $(OUT)
	$(CC) -O2 -Wall -shared -o $@ loader/ijl15_loader.c loader/ijl15_loader.def -Wl,--kill-at

$(OUT)/selftest.exe: tests/selftest.c | $(OUT)
	$(CC) -O2 -Wall -static -o $@ tests/selftest.c

$(OUT)/nettest.exe: tests/nettest.c | $(OUT)
	$(CC) -O2 -Wall -static -o $@ tests/nettest.c -lws2_32

clean:
	rm -rf $(OUT)
.PHONY: all clean keys

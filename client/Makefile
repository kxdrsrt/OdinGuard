# 32-bit Windows build with mingw-w64 (brew install mingw-w64). The server public key header comes from
# tools/og_keygen.c (make keys); the secret seed stays in ../keys (git-ignored).
CC       = i686-w64-mingw32-gcc
CFLAGS   = -O2 -Wall -Wextra -Wno-format-truncation -Wno-cast-function-type -std=gnu11 -DOG_BUILD -D_WIN32_WINNT=0x0601
LDFLAGS  = -s -static -static-libgcc -Wl,--kill-at -Wl,--no-insert-timestamp
OUT      = build
SRC      = $(wildcard src/*.c)
MH_SRC   = ../third_party/minhook/src/hook.c ../third_party/minhook/src/buffer.c ../third_party/minhook/src/trampoline.c \
           ../third_party/minhook/src/hde/hde32.c
LIB_SRC  = ../third_party/monocypher.c $(MH_SRC)
LIBS     = -lws2_32 -liphlpapi -ladvapi32 -luser32 -lgdi32 -lmsimg32 -lgcc
KEYS     = ../keys

all: $(OUT)/odinguard.dll $(OUT)/ijl15.dll $(OUT)/selftest.exe $(OUT)/nettest.exe $(OUT)/inject.exe

$(OUT):
	mkdir -p $(OUT)

$(OUT)/og_keygen: ../tools/og_keygen.c ../third_party/monocypher.c | $(OUT)
	cc -O2 -o $@ ../tools/og_keygen.c ../third_party/monocypher.c

src/og_pubkey.h: $(OUT)/og_keygen
	mkdir -p $(KEYS)
	$(OUT)/og_keygen $(KEYS) $@

keys: src/og_pubkey.h

$(OUT)/odinguard_res.o: res/odinguard.rc res/odinguard.ico res/splash.bmp src/og_version.h | $(OUT)
	i686-w64-mingw32-windres -I res -I src res/odinguard.rc -O coff -o $@

$(OUT)/odinguard.dll: $(SRC) src/og.h src/og_version.h src/og_pubkey.h ../proto/og_proto.h $(OUT)/odinguard_res.o | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $(SRC) $(LIB_SRC) $(OUT)/odinguard_res.o $(LDFLAGS) $(LIBS)

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

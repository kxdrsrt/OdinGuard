# 32-bit Windows build with mingw-w64: brew install mingw-w64
CC       = i686-w64-mingw32-gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-format-truncation -std=gnu11 -DOG_BUILD -D_WIN32_WINNT=0x0601
LDFLAGS ?= -static-libgcc -Wl,--kill-at
OUT      = build
SRC      = $(wildcard src/*.c)

all: $(OUT)/odinguard.dll $(OUT)/ijl15.dll $(OUT)/selftest.exe

$(OUT):
	mkdir -p $(OUT)

$(OUT)/odinguard.dll: $(SRC) src/og.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $(SRC) $(LDFLAGS) -ladvapi32 -luser32

$(OUT)/ijl15.dll: loader/ijl15_loader.c loader/ijl15_loader.def | $(OUT)
	$(CC) -O2 -Wall -shared -o $@ loader/ijl15_loader.c loader/ijl15_loader.def -Wl,--kill-at

$(OUT)/selftest.exe: tests/selftest.c | $(OUT)
	$(CC) -O2 -Wall -o $@ tests/selftest.c

clean:
	rm -rf $(OUT)
.PHONY: all clean

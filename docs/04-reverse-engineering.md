# 04 – Reverse engineering of our client (alpha.exe 2018-06-20)

Tools: Ghidra 12.2 (brew, headless) and REA 6.10 (CLI + MCP for Copilot/VS Code, provider Ghidra).
Project: `analysis/_bin/ghidra/ro.gpr` (git-ignored). Scripts: `tools/ghidra/OgSurvey.java`
(callers of imports and string references), `OgDecompile.java`, `OgCallers.java`.

```
export JAVA_HOME=/opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home
$(brew --prefix ghidra)/libexec/support/analyzeHeadless analysis/_bin/ghidra ro -process alpha.exe \
  -noanalysis -readOnly -scriptPath tools/ghidra -postScript OgDecompile.java 0097fa40
```

Addresses are for this exe build only (image base 0x400000).

## Network

| Address | Function | Notes |
|---|---|---|
| `0x00980030` | winsock init | `WSAStartup`, `LoadLibrary("ws2_32.dll")`, `GetProcAddress` of `send`/`recv` into `DAT_010a33e4` / `DAT_010a33e8` ("Module Hooking Error" message boxes if missing) |
| `0x0097f410` | anti-hook check | compares `GetProcAddress` of connect, closesocket, send, select from a second `LoadLibrary` handle; inline hooks inside ws2_32 pass it |
| `0x0097fa40` | `CConnection::Recv` | `select(read, timeout 0)`, then `recv(…, 0x800)`; `WSAEWOULDBLOCK`/`WSAENOTCONN` = no data |
| `0x0097fb60`, `0x0097feb0` | `CConnection::Send` / flush | `select(write)`, then `send` of the pending buffer slice |

Consequence: OdinGuard hooks `connect`, `send`, `recv`, `select`, `closesocket` inside ws2_32
(MinHook). `select` must report sockets with buffered decrypted data as readable.
`alpha.exe` has no static ws2_32 import; it resolves the functions at runtime.

## Main loop and frame cap

| Address | Function | Notes |
|---|---|---|
| `0x009f94e0` | `CModeMgr::Run` | `while running: mode->OnRun(); Sleep(1);` – the frame cap |
| `0x0082e5f0` | precise sleep | only used by the Bink intro video loop (`0x0082e790`) |

`!vsync off` skips `Sleep(1)` on the main thread (the thread that loads the DLL). Verified in the real
client: 1859 skipped waits in about 15 s.

## Chat

The 2018 client prefixes every chat text with a colour code (`|00!ping`). The server strips `|XX`
before it looks for `!` commands.

## Loader

`ijl15.dll` is a static import of `alpha.exe` (WARP rebuilt import table, one descriptor per
function). The OdinGuard loader takes its name and forwards the six exports to `ijl15r.dll`
(ROExt, the previous `ijl15.dll`), which forwards to `ijl15x.dll`.

## Packing

`alpha.exe` is packed: its code section is unpacked only after the static imports (`ijl15.dll`) and their
DLLs have run `DllMain`. Hooks into exe code are therefore installed from a thread that polls up to 60 s
until the expected bytes appear, and every hook checks a byte signature first (`client/src/og_ui.c`).
A different exe build simply leaves the hook inactive ("ui hooks: 0 of 1 active" in `odinguard.log`).

## Text drawing (colour nicks and colour items)

| Address | Function | Notes |
|---|---|---|
| `0x006fd780` | `UIWindow::TextOutWithOutline` | `thiscall`, `ret 0x24`: x, y, text, len, fg colour, outline colour, font, height, bold. Draws the name above characters and the names in windows. Signature `55 8B EC 6A FF 68 98 92 B8 00 64 A1 00 00 00 00 50 83 EC 24 53 56 57`. |
| `0x00cf828c` | `UINameBalloonText` vtable | slot 17 = `0x0058a3c0` draws the balloon; colours at `this+0x90` (text) and `+0x94` |
| `0x0042d6d0` | `TextOutW` wrapper | plain GDI text (chat, item windows, status window) |
| `0x0042cc40` | `SetTextColor` wrapper | |

OdinGuard hooks `TextOutWithOutline` (names with outline) and GDI `TextOutW` in gdi32 (all other texts).
Both look the text up in a hash table that the server fills (`OG_S_COLORS`). Item names are matched
without refine (`+7 `), slots (` [4]`) and amounts (`: 5 ea`); character names up to ` (` (guild/party
suffix). `TextLog=1` in `odinguard.ini` logs every drawn text to find the exact form of a new text.

## Hat effects (auras)

The client draws hat effect ids from `data\luafiles514\lua files\hateffectinfo\hateffectinfo.lub`
(the first GRF in DATA.INI wins; ours comes from `core.grf`). An entry is either a `.str` file under
`data\texture\effect\` or a client effect id built into the exe (`hatEffectID`). Missing files show
"Cannot find File" and can crash the client at login, so the server only allows ids from its `og_aura`
list; `check_auras.py` (now part of the separate rAthena aura feature) checks every `.str` entry and its textures. Results for our client
in `05-tests.md`.

## Ground skills (LGP)

No client hook is needed: the map-server sends players with `@lgp` an existing flat ground unit
(Volcano 0x9a, Deluge 0x9b, Violent Gale 0x9c, Land Protector 0x9d) instead of `UNT_DUMMYSKILL` for the
cells of Storm Gust, Meteor Storm, Lord of Vermilion, Heaven's Drive and the Ninja ground skills, and
leaves out the heavy `ZC_NOTIFY_GROUNDSKILL` animation. The textures are in every data.grf, so there are
no missing-file or BMP errors.

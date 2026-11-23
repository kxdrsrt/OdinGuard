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

## Next targets (for O4 visual features)

- ground skill rendering (LGP): find the code that draws skill units (`CSkill`/ground effect
  textures, strings `effect\\` in the exe); hook point for custom textures from `data\odinguard\`.
- name rendering for colour nicks: `CGameActor` name balloon (`UINameBalloonText`).
- item name colour: item window text drawing (`UIItemWnd`).

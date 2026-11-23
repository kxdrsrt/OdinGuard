OdinGuard Anti Cheat - client protection for {operator}
=======================================================

This folder is installed and updated by the patcher of {operator}. Do not remove or replace any of
these files; the server refuses or flags clients without a valid guard.

Files
  ijl15.dll       OdinGuard loader. The game loads it at start; it forwards the JPEG functions to
                  ijl15r.dll (the previous ijl15.dll, e.g. ROExt) and starts odinguard.dll.
  odinguard.dll   The guard: encrypted connection, hardware id, heartbeat, integrity checks,
                  cheat-tool detection and the client features chosen by the server.
  odin.grf        Assets of the guard (lite ground skill tiles, auras, interface).
  odinguard.ini   Optional settings (splash screen, debug log).

Client commands (type into the chat, if the server enabled them)
  !ping           show the latency to the server
  !vsync [on|off] remove or restore the frame limit
  !crash          write a crash report on demand (odinguard-crash\)

The server commands (@lgp, @aura, @showauras, ...) are listed by the server team.

Logs
  odinguard.log and odinguard-crash\ in the client folder. Please attach both to bug reports.

Legitimate add-ons (ROExt, dxwrapper, skins, overlays) are allowed. Macro, autopot, packet and
memory tools are detected; the server decides what happens (log, kick or a later ban).

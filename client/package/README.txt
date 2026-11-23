OdinGuard - client protection for BurningRO
===========================================

Files
  ijl15.dll       OdinGuard loader. alpha.exe loads it at start; it forwards the JPEG functions to
                  ijl15r.dll (the previous ijl15.dll, e.g. ROExt) and starts odinguard.dll.
  odinguard.dll   The guard: encrypted connection, hardware id, heartbeat, tool detection,
                  colour names, auras, lite ground skills, crash logs and the ! commands.
  odinguard.ini   Optional settings (ports, splash screen).
  install.bat     install.bat <client folder>   (default: current folder)
  uninstall.bat   uninstall.bat <client folder> (restores the previous ijl15.dll)

Load chain
  alpha.exe -> ijl15.dll (OdinGuard loader) -> ijl15r.dll (ROExt) -> ijl15x.dll (Intel JPEG)
                         \-> odinguard.dll

Client commands (type into the chat)
  !ping           show the latency to the server
  !vsync [on|off] remove or restore the frame limit
  !crash          write a crash report on demand (odinguard-crash\)

Player commands (server)
  @lgp            lite ground skills: Storm Gust, Meteor, Lord of Vermilion, ... as flat coloured cells
  @showauras      hide or show the auras of other players

Logs
  odinguard.log and odinguard-crash\ in the client folder. Please attach both to bug reports.

No new client files are needed: lite ground skills use ground textures that are already in data.grf,
auras are limited by the server to effects whose files are in the client.

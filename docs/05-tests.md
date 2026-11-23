# 05 – Tests

All tests below ran on 2026-11-23 against a local xAthena (Docker on the Mac: MariaDB, login, char and
map server with `og_enable: yes`, `og_required: yes`, `og_log_only: no`, `og_alive_interval: 5`) and
the Windows VM "Tiny 11 Pro" (ARM64, x86 emulation), host reachable as 10.211.55.2.

## Tools

- `client/tests/selftest.exe [crash]` – prints the identity report (exe code hash, hardware components,
  unique id, VM flag, resource check, file hashes, modules).
- `client/tests/nettest.exe <ip> <login-port> <user> <password> [char-port [map-port [char-name]]]`
  – loads `odinguard.dll`, logs in, enters the char and map server and types `!ping`, `!vsync off`,
  `!vsync on`, `!crash`. Without `odinguard.dll` next to it, it runs as a plain client.
- Real client: copy of `client-dev/ragnarok` in the VM (`C:\ROog`) with the loader installed, server
  "LOCAL" (10.211.55.2:6900), input through `prlctl send-key-event` scancodes.

## Results

| Test | Result |
|---|---|
| Plain client, `og_required: yes` | login refused (code 5, "EXE not the latest version") |
| OdinGuard client | handshake on login, char and map; encrypted login, char list, map enter |
| Inter-server links (char→login, map→char) | unchanged (detected as plain) |
| Hardware registry | `og_client` + 13 components in `og_component` |
| Exact hardware ban | login refused (code 4) |
| Fuzzy ban (machine_guid + mac equal to a banned machine) | login refused, new id banned "linked to …", `og_event` row |
| `!ping` / `!vsync off` / `!vsync on` / `!crash` (nettest and real client) | answered in chat ("Ping: 24 ms", …), crash report with last packets |
| Tool signature `openkore` (kick) | login and char server disconnect; player never reaches the map |
| Tool signature `x32dbg` (log) | `og_event` row with character name on the map server |
| VM check in Parallels | `vm=1 firmware:Parallels International GmbH.` |
| Resource rule | loose `data\sprite\nodelay.act` reported |
| Real client start with loader chain | starts, renders, logs in, plays |

Live (2026-11-23, after deploy with `ODINGUARD_ENABLE=yes`, `REQUIRED=no`, `LOG_ONLY=yes`):
plain client and OdinGuard client both reach the HR (6900) and M-LR (6901) login servers; the OdinGuard
client completes the handshake with the pinned key and receives the policy (22 signatures).

### v0.3 (2026-11-23, same setup, `og_log_only: yes`)

| Test | Result |
|---|---|
| Splash screen (layered window, fade) | shown at client start, then the game window |
| Colour nick (`og_group_namecolor 99`) | name above the character and in the status window gold with black outline |
| Colour item (`og_item_color 909,FF8000`) | "Jellopy - 2 obtained" with Jellopy in orange |
| `@aura` (`og_aura`) | aura set, saved in the char reg, shown again after relog; unknown name refused |
| Aura resources (`tools/check_auras.py`) | 25 `.str` hat effects complete (file + all textures), 12 miss their `.str` |
| Aura ids in game | 1, 3, 4, 12-14, 18-20, 25-27, 29, 39, 44, 45, 51, 53, 54, 56, 79, 81, 90-92, 95, 97, 98, 122: drawn, no error. 82-84 (`subject_aura`): "Cannot find File" and crash. 59-78, 93, 94, 99-120, 123 (exe effects): nothing drawn |
| `@showauras` | auras of others hidden for this viewer only, own aura stays |
| `@lgp` | Storm Gust as light blue cells, characters visible; without `@lgp` the full blizzard |
| Machine limit (`og_max_per_machine: 1`) | second character from the same machine refused |
| Alive timeout (firewall blocks `alpha.exe` for 30 s) | `og_event` "alive-timeout", action per `og_log_only` |
| `@ogban OgPlayer, test ban` | player kicked, `og_ban` row, next login refused (code 4) |
| `@ogunban 2000001` | ban lifted, login accepted |
| Release package (`install.bat`, stripped DLL) | installs next to ROExt, splash, handshake, login |

Notes: `NtSuspendProcess` does not stop the emulated x86 process in the ARM VM, so the alive test blocks
the traffic with a firewall rule instead. Synthetic input (`SendInput`, `SendKeys`) from a scheduled task
never reaches the guest input queue in Parallels, also not without OdinGuard, so the input filter can
only be tested on a real Windows machine.

Not tested yet (need real machines): input filter, RCX, Warsaw, VM block on x86 hypervisors.

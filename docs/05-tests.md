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

Not tested yet: input filter (needs a desktop session with synthetic input), RCX, Warsaw, alive timeout
kick, machine limit with two accounts, `@ogban` in game.

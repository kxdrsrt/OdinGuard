# MCP server (`og-mcp`)

[MCP](https://modelcontextprotocol.io) lets an AI assistant call tools. `og-mcp` exposes what the Studio and the `og`
CLI do, so a server operator can configure OdinGuard by asking ("switch on the behaviour module, set the nodelay
reaction to flag, approve the GRFs of this client folder, preview the release").

```sh
cd studio && cargo build --release -p og-mcp          # studio/target/release/og-mcp
```

```json
{ "mcpServers": { "odinguard": { "command": "/path/to/og-mcp", "env": { "ODINGUARD_ROOT": "/path/to/OdinGuard" } } } }
```

Transport: stdio, newline-delimited JSON-RPC 2.0, tools only. `OG_MCP_READONLY=1` hides every tool that writes.
`ODINGUARD_ROOT` points at the OdinGuard repository (module registry, client sources) when the server does not run from it.

| Tool | Writes | What it does |
|---|---|---|
| `og_registry` | – | Modules (kind, side, requires, parameters with defaults and ranges) and commands |
| `og_profile_get`, `og_validate` | – | A profile as JSON with its validation result |
| `og_profile_new` | yes | New profile with fresh salts and key files (never overwrites) |
| `og_profile_update` | yes | Deep-merges JSON into the profile; an invalid result is not saved |
| `og_module_set` | yes | Module on/off with its requirements |
| `og_param_set` | yes | A module parameter (type-checked against the registry) |
| `og_command_set` | yes | Enable, rename, group level, rate limit of a command |
| `og_signature_groups`, `og_signature_group_set` | – / yes | Cheat signature groups and the reaction per group |
| `og_grf_scan` | – | Reads the GRFs of a client folder |
| `og_grf_approve` | yes | Adds a client's GRF versions to the approved list (keeps previous versions by default) |
| `og_keep_previous` | yes | Keeps the current release (guard, `odin.grf`, `odin-dark.grf`) accepted by the next server build, so unpatched players are not kicked (INTEGRATION 8a) |
| `og_preview` | – | What a build would contain |
| `og_build` | yes | Builds the release (DLL, `odin.grf`, competitive pack, server package, signed manifest) |
| `og_verify_release` | – | Checks manifest signature and file hashes |
| `og_commands_md` | – | Command reference (en/de) of the profile |
| `og_rollout_status` | – | Rollout gate from the exported `og_event` / `og_client` tables |

**Safety.** Every writing tool needs `confirm: true`, which the assistant should only send after you agreed. A profile
only holds the *paths* of its key files, so no secret is ever returned. The server runs with your user's rights and only
touches the paths you give it. The same pattern is used by the patcher's MCP server,
[OpenThor Patcher](https://github.com/kxdrsrt/OpenThor-Patcher).

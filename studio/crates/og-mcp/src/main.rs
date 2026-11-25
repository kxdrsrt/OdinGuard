//! OdinGuard MCP server (stdio, newline-delimited JSON-RPC 2.0, tools only).
//!
//! Lets an AI assistant configure an OdinGuard profile (modules, parameters, commands, signature groups, GRF manifest,
//! competitive pack, rollout), preview and build a release. It uses exactly the code of the Studio and the `og` CLI.
//! Tools that write need `confirm: true`; `OG_MCP_READONLY=1` hides them. Secrets are never returned: a profile only
//! holds the paths of its key files.

use og_core::{generate, grf, pipeline, profile::Profile, registry::Registry, rollout, sigpack};
use serde_json::{json, Value};
use std::io::{BufRead, Write};
use std::path::{Path, PathBuf};

const PROTOCOL_VERSIONS: &[&str] = &["2025-06-18", "2025-03-26", "2024-11-05"];

struct Tool {
    name: &'static str,
    title: &'static str,
    description: &'static str,
    writes: bool,
    params: &'static [(&'static str, &'static str, &'static str, bool)],
}

const P: (&str, &str, &str, bool) = ("profile", "string", "Path of the profile (*.profile.toml)", true);
const C: (&str, &str, &str, bool) = ("confirm", "boolean", "Must be true: the tool changes files", true);

const TOOLS: &[Tool] = &[
    Tool {
        name: "og_registry",
        title: "List modules and commands",
        description: "All modules (id, kind, side, requires, summary, parameters with defaults and ranges) and all commands (id, !/@ kind, default group and rate limit, help) that a profile can switch on.",
        writes: false,
        params: &[],
    },
    Tool { name: "og_profile_get", title: "Read a profile", description: "Returns the profile as JSON together with its validation result (warnings or the first error).", writes: false, params: &[P] },
    Tool {
        name: "og_profile_new",
        title: "Create a profile",
        description: "Creates a new profile with fresh salts and its key files next to it (never overwrites).",
        writes: true,
        params: &[
            ("profile", "string", "Path of the new profile", true),
            ("id", "string", "Profile id (letters, digits, - _)", true),
            ("operator", "string", "Server/operator name shown on the splash and in the window title", true),
            C,
        ],
    },
    Tool { name: "og_validate", title: "Validate a profile", description: "Validates a profile and returns warnings or the error.", writes: false, params: &[P] },
    Tool {
        name: "og_profile_update",
        title: "Change a profile",
        description: "Deep-merges `changes` (JSON, same shape as og_profile_get) into the profile: objects merge, arrays and values replace. The result is validated; an invalid profile is not saved. Example: {\"general\":{\"operator\":\"MyRO\"},\"rollout\":{\"stage\":1}}.",
        writes: true,
        params: &[P, ("changes", "object", "Fields to change", true), C],
    },
    Tool {
        name: "og_module_set",
        title: "Switch a module on or off",
        description: "Enables or disables a module; enabling also enables everything it requires, disabling is refused while another enabled module needs it.",
        writes: true,
        params: &[P, ("module", "string", "Module id", true), ("enabled", "boolean", "On or off", true), C],
    },
    Tool {
        name: "og_param_set",
        title: "Set a module parameter",
        description: "Sets a parameter of a module (see og_registry for names, types, ranges and the allowed actions log, flag, flagban, kick, ban).",
        writes: true,
        params: &[P, ("module", "string", "Module id", true), ("name", "string", "Parameter name", true), ("value", "string", "Value as text (numbers and true/false are converted by type)", true), C],
    },
    Tool {
        name: "og_command_set",
        title: "Configure a command",
        description: "Enables/disables, renames, restricts the group level or the rate limit of a player/GM command (see og_registry).",
        writes: true,
        params: &[
            P,
            ("command", "string", "Command id", true),
            ("enabled", "boolean", "On or off", false),
            ("name", "string", "New command name (empty string = default)", false),
            ("group", "integer", "Lowest group level that may use it", false),
            ("rate_ms", "integer", "Minimum milliseconds between two uses", false),
            C,
        ],
    },
    Tool { name: "og_signature_groups", title: "List signature groups", description: "Cheat signature groups of the profile's sources (tool, kind, entries, client-checkable entries) and the reaction currently configured.", writes: false, params: &[P] },
    Tool {
        name: "og_signature_group_set",
        title: "Set a signature group reaction",
        description: "Sets the reaction for a whole tool group: off, log, flag, flagban, kick or ban; an empty value restores each signature's own action.",
        writes: true,
        params: &[P, ("group", "string", "Group name from og_signature_groups", true), ("action", "string", "off | log | flag | flagban | kick | ban | empty", true), C],
    },
    Tool {
        name: "og_grf_scan",
        title: "Scan a client's GRFs",
        description: "Reads the GRFs of a client folder (size, table hash, optional file hash) without changing the profile.",
        writes: false,
        params: &[("client", "string", "Client folder", true), ("full", "boolean", "Also hash the whole file (slow on big GRFs)", false)],
    },
    Tool {
        name: "og_grf_approve",
        title: "Approve a client's GRFs",
        description: "Scans a client folder and adds its GRF versions to the profile's approved list. With keep_previous the older versions stay valid (use this before every patch roll-out).",
        writes: true,
        params: &[P, ("client", "string", "Client folder", true), ("full", "boolean", "Also hash the whole file", false), ("keep_previous", "boolean", "Keep previously approved versions (default true)", false), C],
    },
    Tool { name: "og_preview", title: "Preview a release", description: "What a build would produce: modules, files, signature counts, warnings. Writes nothing.", writes: false, params: &[P] },
    Tool { name: "og_build", title: "Build a release", description: "Builds the release (client DLL, odin.grf, competitive pack, server package, signed manifest) into the profile's output folder.", writes: true, params: &[P, C] },
    Tool { name: "og_verify_release", title: "Verify a release", description: "Checks the manifest signature and file hashes of a built release folder.", writes: false, params: &[("dir", "string", "Release folder", true)] },
    Tool { name: "og_commands_md", title: "Generate the command reference", description: "Markdown reference of the commands enabled in the profile (lang en or de).", writes: false, params: &[P, ("lang", "string", "en or de", false)] },
    Tool {
        name: "og_rollout_status",
        title: "Rollout gate",
        description: "Evaluates the exported og_event / og_client tables (TSV from mysql -B) and says whether the next rollout stage (1 warn, 2 kick, 3 patcher, 4 mandatory) is safe.",
        writes: false,
        params: &[P, ("events", "string", "og_event export (TSV)", true), ("clients", "string", "og_client export (TSV)", true), ("active", "integer", "Active accounts", false), ("fp_percent", "number", "False-positive limit in percent (default 2)", false)],
    },
];

fn schema(t: &Tool) -> Value {
    let mut props = serde_json::Map::new();
    let mut req = vec![];
    for (n, ty, d, r) in t.params {
        props.insert((*n).into(), json!({"type": ty, "description": d}));
        if *r {
            req.push(json!(n));
        }
    }
    json!({"type": "object", "properties": props, "required": req, "additionalProperties": false})
}

type Res<T> = Result<T, String>;

fn e<T: std::fmt::Display>(x: T) -> String {
    x.to_string()
}

fn need(a: &Value, k: &str) -> Res<String> {
    a.get(k).and_then(Value::as_str).filter(|s| !s.is_empty()).map(str::to_string).ok_or_else(|| format!("missing argument {k}"))
}

fn confirmed(a: &Value) -> Res<()> {
    if a.get("confirm").and_then(Value::as_bool) == Some(true) {
        Ok(())
    } else {
        Err("this tool changes files: call it again with confirm: true after the user agreed".into())
    }
}

fn registry() -> Res<Registry> {
    Registry::load(&og_core::repo_root().join("modules")).map_err(e)
}

fn load(a: &Value) -> Res<(PathBuf, Profile)> {
    let path = PathBuf::from(need(a, "profile")?);
    let p = Profile::load(&path).map_err(e)?;
    Ok((path, p))
}

fn save(path: &Path, p: &Profile, reg: &Registry) -> Res<Vec<String>> {
    let w = p.validate(reg).map_err(|x| format!("not saved, the profile would be invalid: {x}"))?;
    p.save(path).map_err(e)?;
    Ok(w)
}

fn merge(dst: &mut Value, src: &Value) {
    match (dst, src) {
        (Value::Object(d), Value::Object(s)) => {
            for (k, v) in s {
                merge(d.entry(k.clone()).or_insert(Value::Null), v);
            }
        }
        (d, s) => *d = s.clone(),
    }
}

fn call(name: &str, a: &Value) -> Res<Value> {
    match name {
        "og_registry" => Ok(serde_json::to_value(registry()?).map_err(e)?),
        "og_profile_get" => {
            let (_, p) = load(a)?;
            let (warnings, error) = match p.validate(&registry()?) {
                Ok(w) => (w, None),
                Err(x) => (vec![], Some(x.to_string())),
            };
            Ok(json!({"profile": p, "warnings": warnings, "error": error}))
        }
        "og_profile_new" => {
            confirmed(a)?;
            let path = PathBuf::from(need(a, "profile")?);
            if path.exists() {
                return Err(format!("{} exists", path.display()));
            }
            let p = Profile::new(&need(a, "id")?, &need(a, "operator")?).map_err(e)?;
            let base = path.parent().unwrap_or(Path::new(".")).to_path_buf();
            for k in [&p.keys.server_seed, &p.keys.signing_seed, &p.keys.build_secret] {
                og_core::keys::ensure(&p.resolve(&base, k)).map_err(e)?;
            }
            p.save(&path).map_err(e)?;
            Ok(json!({"profile": path, "note": "key files were created next to the profile; keep them secret"}))
        }
        "og_validate" => {
            let (_, p) = load(a)?;
            match p.validate(&registry()?) {
                Ok(w) => Ok(json!({"valid": true, "warnings": w})),
                Err(x) => Ok(json!({"valid": false, "error": x.to_string()})),
            }
        }
        "og_profile_update" => {
            confirmed(a)?;
            let (path, p) = load(a)?;
            let mut v = serde_json::to_value(&p).map_err(e)?;
            let ch = a.get("changes").filter(|c| c.is_object()).ok_or("changes must be an object")?;
            merge(&mut v, ch);
            let np: Profile = serde_json::from_value(v).map_err(|x| format!("the changed profile is invalid: {x}"))?;
            let w = save(&path, &np, &registry()?)?;
            Ok(json!({"saved": true, "warnings": w}))
        }
        "og_module_set" => {
            confirmed(a)?;
            let (path, mut p) = load(a)?;
            let reg = registry()?;
            let id = need(a, "module")?;
            let on = a.get("enabled").and_then(Value::as_bool).ok_or("missing argument enabled")?;
            if !reg.modules.iter().any(|m| m.id == id) {
                return Err(format!("unknown module {id}"));
            }
            if on {
                fn add(reg: &Registry, p: &mut Profile, id: &str) {
                    if !p.modules.iter().any(|m| m == id) {
                        p.modules.push(id.to_string());
                        if let Some(m) = reg.modules.iter().find(|m| m.id == id) {
                            for r in &m.requires {
                                add(reg, p, r);
                            }
                        }
                    }
                }
                add(&reg, &mut p, &id);
            } else {
                if id == "core" {
                    return Err("core cannot be switched off".into());
                }
                if let Some(user) = reg.modules.iter().find(|m| p.modules.contains(&m.id) && m.requires.contains(&id)) {
                    return Err(format!("{} needs {id}; switch it off first", user.id));
                }
                p.modules.retain(|m| *m != id);
            }
            let w = save(&path, &p, &reg)?;
            Ok(json!({"modules": p.modules, "warnings": w}))
        }
        "og_param_set" => {
            confirmed(a)?;
            let (path, mut p) = load(a)?;
            let reg = registry()?;
            let (m, n, raw) = (need(a, "module")?, need(a, "name")?, need(a, "value")?);
            let def = reg
                .modules
                .iter()
                .find(|x| x.id == m)
                .and_then(|x| x.params.iter().find(|q| q.name == n))
                .ok_or_else(|| format!("module {m} has no parameter {n}"))?;
            let val = match def.kind.as_str() {
                "int" => toml::Value::Integer(raw.parse().map_err(|_| format!("{n} needs a whole number"))?),
                "bool" => toml::Value::Boolean(matches!(raw.as_str(), "true" | "1" | "yes" | "on")),
                _ => toml::Value::String(raw),
            };
            p.params.insert(format!("{m}.{n}"), val);
            let w = save(&path, &p, &reg)?;
            Ok(json!({"saved": true, "warnings": w}))
        }
        "og_command_set" => {
            confirmed(a)?;
            let (path, mut p) = load(a)?;
            let reg = registry()?;
            let id = need(a, "command")?;
            if !reg.commands.iter().any(|c| c.id == id) {
                return Err(format!("unknown command {id}"));
            }
            let s = p.commands.entry(id).or_insert_with(|| serde_json::from_value(json!({"enabled": true})).expect("default command setting"));
            if let Some(v) = a.get("enabled").and_then(Value::as_bool) {
                s.enabled = v;
            }
            if let Some(v) = a.get("name").and_then(Value::as_str) {
                s.name = if v.is_empty() { None } else { Some(v.to_string()) };
            }
            if let Some(v) = a.get("group").and_then(Value::as_i64) {
                s.group = Some(v);
            }
            if let Some(v) = a.get("rate_ms").and_then(Value::as_i64) {
                s.rate_ms = Some(v);
            }
            let w = save(&path, &p, &reg)?;
            Ok(json!({"saved": true, "warnings": w}))
        }
        "og_signature_groups" => {
            let (path, p) = load(a)?;
            let base = path.parent().unwrap_or(Path::new(".")).to_path_buf();
            let src: Vec<PathBuf> = p.signatures.sources.iter().map(|s| p.resolve(&base, s)).collect();
            let al: Vec<PathBuf> = p.signatures.allowlist.iter().map(|s| p.resolve(&base, s)).collect();
            let g = sigpack::catalog(&src.iter().map(PathBuf::as_path).collect::<Vec<_>>(), &al.iter().map(PathBuf::as_path).collect::<Vec<_>>()).map_err(e)?;
            Ok(json!({"groups": g, "configured": p.signatures.groups}))
        }
        "og_signature_group_set" => {
            confirmed(a)?;
            let (path, mut p) = load(a)?;
            let g = need(a, "group")?;
            let act = a.get("action").and_then(Value::as_str).ok_or("missing argument action")?;
            if act.is_empty() {
                p.signatures.groups.remove(&g);
            } else {
                p.signatures.groups.insert(g, act.to_string());
            }
            let w = save(&path, &p, &registry()?)?;
            Ok(json!({"saved": true, "warnings": w}))
        }
        "og_grf_scan" => {
            let full = a.get("full").and_then(Value::as_bool).unwrap_or(false);
            let s = grf::scan_client(Path::new(&need(a, "client")?), full).map_err(e)?;
            Ok(json!({"grfs": s}))
        }
        "og_grf_approve" => {
            confirmed(a)?;
            let (path, mut p) = load(a)?;
            let full = a.get("full").and_then(Value::as_bool).unwrap_or(false);
            let keep = a.get("keep_previous").and_then(Value::as_bool).unwrap_or(true);
            let scan = grf::scan_client(Path::new(&need(a, "client")?), full).map_err(e)?;
            let same = |x: &og_core::profile::GrfFile, y: &og_core::profile::GrfFile| x.name.eq_ignore_ascii_case(&y.name) && x.size == y.size && x.tail == y.tail;
            for s in &scan {
                if !keep {
                    p.grf.files.retain(|f| !f.name.eq_ignore_ascii_case(&s.name) || same(f, s));
                }
                if !p.grf.files.iter().any(|f| same(f, s)) {
                    p.grf.files.push(s.clone());
                }
            }
            let w = save(&path, &p, &registry()?)?;
            Ok(json!({"scanned": scan.len(), "approved_total": p.grf.files.len(), "warnings": w}))
        }
        "og_preview" => {
            let (_, p) = load(a)?;
            Ok(serde_json::to_value(pipeline::preview(&p, &registry()?).map_err(e)?).map_err(e)?)
        }
        "og_build" => {
            confirmed(a)?;
            let path = need(a, "profile")?;
            Ok(serde_json::to_value(pipeline::build(Path::new(&path), &og_core::repo_root()).map_err(e)?).map_err(e)?)
        }
        "og_verify_release" => {
            let m = og_core::manifest::verify_release(Path::new(&need(a, "dir")?), None).map_err(e)?;
            Ok(json!({"ok": true, "profile": m.profile, "files": m.files.len()}))
        }
        "og_commands_md" => {
            let (_, p) = load(a)?;
            let lang = a.get("lang").and_then(Value::as_str).unwrap_or("en");
            Ok(json!({"markdown": generate::commands_md(&p, &registry()?, lang)}))
        }
        "og_rollout_status" => {
            let (_, p) = load(a)?;
            let active = a.get("active").and_then(Value::as_u64).map(|x| x as usize);
            let fp = a.get("fp_percent").and_then(Value::as_f64).unwrap_or(2.0);
            let s = rollout::status(&p, Path::new(&need(a, "events")?), Path::new(&need(a, "clients")?), active, fp).map_err(e)?;
            Ok(serde_json::to_value(s).map_err(e)?)
        }
        _ => Err(format!("unknown tool {name}")),
    }
}

fn reply(id: &Value, result: Value) -> Value {
    json!({"jsonrpc": "2.0", "id": id, "result": result})
}

fn error(id: &Value, code: i64, m: &str) -> Value {
    json!({"jsonrpc": "2.0", "id": id, "error": {"code": code, "message": m}})
}

fn handle(msg: &Value, readonly: bool) -> Option<Value> {
    let method = msg.get("method")?.as_str()?;
    let id = msg.get("id")?;
    let params = msg.get("params").unwrap_or(&Value::Null);
    Some(match method {
        "initialize" => {
            let wanted = params.get("protocolVersion").and_then(Value::as_str);
            let v = PROTOCOL_VERSIONS.iter().find(|v| Some(**v) == wanted).unwrap_or(&PROTOCOL_VERSIONS[0]);
            reply(
                id,
                json!({
                    "protocolVersion": v,
                    "capabilities": {"tools": {"listChanged": false}},
                    "serverInfo": {"name": "og-mcp", "title": "OdinGuard Anti Cheat", "version": env!("CARGO_PKG_VERSION")},
                    "instructions": "OdinGuard is an anti-cheat for Ragnarok Online servers. Typical flow: og_registry, og_profile_new (or og_profile_get), og_module_set / og_param_set / og_command_set, og_signature_groups, og_grf_approve before every patch, og_preview, og_build, og_verify_release. Roll out in stages with og_rollout_status. Tools that write need confirm: true. Set ODINGUARD_ROOT to the OdinGuard repository if the server runs elsewhere.",
                }),
            )
        }
        "ping" => reply(id, json!({})),
        "tools/list" => {
            let tools: Vec<Value> = TOOLS
                .iter()
                .filter(|t| !(readonly && t.writes))
                .map(|t| {
                    json!({"name": t.name, "title": t.title, "description": t.description, "inputSchema": schema(t),
                    "annotations": {"readOnlyHint": !t.writes, "destructiveHint": false, "openWorldHint": false}})
                })
                .collect();
            reply(id, json!({"tools": tools}))
        }
        "tools/call" => {
            let name = params.get("name").and_then(Value::as_str).unwrap_or("");
            let Some(t) = TOOLS.iter().find(|t| t.name == name && !(readonly && t.writes)) else {
                return Some(error(id, -32602, &format!("unknown tool '{name}'")));
            };
            let args = params.get("arguments").cloned().unwrap_or_else(|| json!({}));
            for (n, _, _, r) in t.params {
                if *r && args.get(n).is_none() {
                    return Some(reply(id, json!({"content": [{"type": "text", "text": format!("missing argument {n}")}], "isError": true})));
                }
            }
            match call(name, &args) {
                Ok(v) => reply(id, json!({"content": [{"type": "text", "text": v.to_string()}], "structuredContent": v, "isError": false})),
                Err(m) => reply(id, json!({"content": [{"type": "text", "text": m}], "isError": true})),
            }
        }
        other => error(id, -32601, &format!("method '{other}' is not supported")),
    })
}

fn main() {
    let readonly = std::env::var("OG_MCP_READONLY").is_ok_and(|v| v == "1");
    let mut out = std::io::stdout();
    for line in std::io::stdin().lock().lines() {
        let Ok(line) = line else { break };
        if line.trim().is_empty() {
            continue;
        }
        let r = match serde_json::from_str::<Value>(&line) {
            Ok(m) => handle(&m, readonly),
            Err(x) => Some(error(&Value::Null, -32700, &x.to_string())),
        };
        if let Some(r) = r {
            let _ = writeln!(out, "{r}").and_then(|()| out.flush());
        }
    }
}

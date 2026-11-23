//! `odinguard.profile.toml`: the single source of truth of an operator (modules, challenges, commands, signature
//! package, branding, ports, keys, rollout stage, competitive pack, aura catalog). Paths inside a profile are
//! relative to the profile file. Secrets are never stored in the profile, only the paths of the key files.

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use crate::registry::{ACTIONS, Registry};
use crate::{Error, Result};

pub const SCHEMA_VERSION: u32 = 2;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct General {
    pub id: String,
    /// shown on the splash screen and in message boxes
    pub operator: String,
    /// client build name sent in the hello (e.g. 2018-06-20)
    pub client_build: String,
    pub ports: Vec<u16>,
    pub splash_ms: u32,
    /// hex, 16 bytes: build variation (masks, link order, protocol salt derivation)
    pub variation_seed: String,
    /// hardware id salt; changing it makes every machine ban useless
    pub hwid_salt: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Keys {
    /// server key seed (64 hex), the public key is compiled into the guard
    pub server_seed: String,
    /// Studio signing key seed (signature packages, release manifests)
    pub signing_seed: String,
    /// build secret (64 hex) for the nonce and time challenges
    pub build_secret: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
pub struct CommandSetting {
    #[serde(default = "yes")]
    pub enabled: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub group: Option<i64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub rate_ms: Option<i64>,
}

fn yes() -> bool {
    true
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Signatures {
    pub sources: Vec<String>,
    pub allowlist: Vec<String>,
    /// raises every signature action to at most this one (e.g. "flagban" during the rollout); empty = as written
    #[serde(default)]
    pub max_action: String,
    /// version written into the package; the server refuses packages older than its og_sigpack_min_version
    pub version: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Assets {
    /// folders whose `data/` trees are merged into odin.grf (module asset groups: lgp, auras, ...)
    pub odin_grf: BTreeMap<String, String>,
    /// optional competitive pack (a built GRF), shipped next to odin.grf
    #[serde(default)]
    pub competitive_pack: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Rollout {
    /// 1 warn mode, 2 kick, 3 patcher delivery, 4 required
    pub stage: u8,
    /// rules an operator has checked by hand: not counted as false-alarm candidates
    #[serde(default)]
    pub reviewed_rules: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Profile {
    pub schema_version: u32,
    pub general: General,
    pub keys: Keys,
    pub modules: Vec<String>,
    /// "module.param" -> value
    #[serde(default)]
    pub params: BTreeMap<String, toml::Value>,
    #[serde(default)]
    pub commands: BTreeMap<String, CommandSetting>,
    pub signatures: Signatures,
    pub assets: Assets,
    /// aura catalog (registry auras.toml format); empty = the registry catalog
    #[serde(default)]
    pub aura_catalog: String,
    pub rollout: Rollout,
    /// output folder of builds
    pub output: String,
}

impl Profile {
    /// A new profile with fresh salts and every module of the default set. Key files are created by `keys::ensure`.
    pub fn new(id: &str, operator: &str) -> Result<Self> {
        let seed: [u8; 16] = crate::crypto::random().map_err(|e| Error::Io("random".into(), e))?;
        let salt: [u8; 12] = crate::crypto::random().map_err(|e| Error::Io("random".into(), e))?;
        Ok(Profile {
            schema_version: SCHEMA_VERSION,
            general: General {
                id: id.into(),
                operator: operator.into(),
                client_build: "2018-06-20".into(),
                ports: vec![6900, 6121, 5121],
                splash_ms: 1800,
                variation_seed: hex::encode(seed),
                hwid_salt: hex::encode(salt),
            },
            keys: Keys {
                server_seed: "keys/server.seed".into(),
                signing_seed: "keys/signing.seed".into(),
                build_secret: "keys/build.secret".into(),
            },
            modules: DEFAULT_MODULES.iter().map(|s| s.to_string()).collect(),
            params: BTreeMap::new(),
            commands: BTreeMap::new(),
            signatures: Signatures {
                sources: vec!["signatures/signatures.toml".into()],
                allowlist: vec!["signatures/allowlist.toml".into()],
                max_action: "flagban".into(),
                version: 1,
            },
            assets: Assets { odin_grf: BTreeMap::from([("lgp".into(), "assets/odin-grf/lgp".into())]), competitive_pack: String::new() },
            aura_catalog: String::new(),
            rollout: Rollout { stage: 1, reviewed_rules: vec![] },
            output: format!("dist/{id}"),
        })
    }

    pub fn load(path: &Path) -> Result<Self> {
        let text = std::fs::read_to_string(path).map_err(|e| Error::Io(path.display().to_string(), e))?;
        let mut value: toml::Value = toml::from_str(&text).map_err(|e| Error::Parse(path.display().to_string(), e.to_string()))?;
        migrate(&mut value)?;
        value.try_into().map_err(|e: toml::de::Error| Error::Parse(path.display().to_string(), e.to_string()))
    }

    pub fn save(&self, path: &Path) -> Result<()> {
        let text = toml::to_string_pretty(self).map_err(|e| Error::Invalid(e.to_string()))?;
        std::fs::write(path, format!("# OdinGuard Anti Cheat profile (schema {SCHEMA_VERSION}). Secrets are only referenced by path.\n{text}"))
            .map_err(|e| Error::Io(path.display().to_string(), e))
    }

    /// Value of a module parameter (profile override or registry default).
    pub fn param(&self, reg: &Registry, module: &str, name: &str) -> Option<toml::Value> {
        if let Some(v) = self.params.get(&format!("{module}.{name}")) {
            return Some(v.clone());
        }
        reg.module(module)?.params.iter().find(|p| p.name == name).map(|p| p.default.clone())
    }

    /// Checks the profile against the registry. Returns warnings; errors stop a build.
    pub fn validate(&self, reg: &Registry) -> Result<Vec<String>> {
        let mut warn = Vec::new();
        let bad = |m: String| Err(Error::Invalid(m));
        if self.schema_version != SCHEMA_VERSION {
            return bad(format!("schema {} (expected {SCHEMA_VERSION}); run `og profile migrate`", self.schema_version));
        }
        if self.general.id.is_empty() || !self.general.id.chars().all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_') {
            return bad("general.id: letters, digits, - and _ only".into());
        }
        if self.general.operator.trim().is_empty() || self.general.operator.len() > 60 || self.general.operator.contains('"') {
            return bad("general.operator: 1-60 characters, no quotes".into());
        }
        if self.general.ports.is_empty() {
            return bad("general.ports: at least one game server port".into());
        }
        if hex::decode(&self.general.variation_seed).map(|v| v.len()) != Ok(16) {
            return bad("general.variation_seed: 32 hex characters".into());
        }
        if self.general.hwid_salt.len() < 6 || self.general.hwid_salt.len() > 60 || self.general.hwid_salt.contains('"') {
            return bad("general.hwid_salt: 6-60 characters".into());
        }
        if !self.modules.iter().any(|m| m == "core") {
            return bad("the core module cannot be removed".into());
        }
        for m in &self.modules {
            let module = reg.module(m).ok_or_else(|| Error::Invalid(format!("unknown module {m}")))?;
            for r in &module.requires {
                if !self.modules.contains(r) {
                    return bad(format!("module {m} needs {r}"));
                }
            }
        }
        for (key, v) in &self.params {
            let (module, name) = key.split_once('.').ok_or_else(|| Error::Invalid(format!("parameter {key}: use module.name")))?;
            let p = reg
                .module(module)
                .and_then(|m| m.params.iter().find(|p| p.name == name))
                .ok_or_else(|| Error::Invalid(format!("unknown parameter {key}")))?;
            match p.kind.as_str() {
                "int" => {
                    let n = v.as_integer().ok_or_else(|| Error::Invalid(format!("{key}: integer expected")))?;
                    if p.min.is_some_and(|m| n < m) || p.max.is_some_and(|m| n > m) {
                        return bad(format!("{key} = {n} outside {:?}..{:?}", p.min, p.max));
                    }
                }
                "bool" if !v.is_bool() => return bad(format!("{key}: true/false expected")),
                "action" if !ACTIONS.contains(&v.as_str().unwrap_or("")) => return bad(format!("{key}: one of {ACTIONS:?}")),
                "string" if !v.is_str() => return bad(format!("{key}: text expected")),
                _ => {}
            }
            if !self.modules.iter().any(|m| m == module) {
                warn.push(format!("{key} is set but module {module} is not selected"));
            }
        }
        for (id, c) in &self.commands {
            let cmd = reg.commands.iter().find(|x| &x.id == id).ok_or_else(|| Error::Invalid(format!("unknown command {id}")))?;
            if let Some(n) = &c.name
                && (n.is_empty() || n.len() > 24 || !n.chars().all(|ch| ch.is_ascii_alphanumeric()))
            {
                return bad(format!("command {id}: name must be 1-24 letters/digits"));
            }
            if c.enabled && !self.modules.contains(&cmd.module) {
                warn.push(format!("command {id} is enabled but its module {} is not selected", cmd.module));
            }
        }
        if !self.signatures.max_action.is_empty() && !ACTIONS.contains(&self.signatures.max_action.as_str()) {
            return bad(format!("signatures.max_action: one of {ACTIONS:?}"));
        }
        if !(1..=4).contains(&self.rollout.stage) {
            return bad("rollout.stage: 1-4".into());
        }
        if self.modules.iter().any(|m| m == "competitive-pack") && self.assets.competitive_pack.is_empty() {
            return bad("module competitive-pack needs assets.competitive_pack".into());
        }
        if self.rollout.stage >= 4 && self.param(reg, "core", "required").and_then(|v| v.as_bool()) == Some(false) {
            warn.push("rollout stage 4 forces core.required = true in the generated config".into());
        }
        Ok(warn)
    }

    /// Absolute path of a profile-relative path.
    pub fn resolve(&self, base: &Path, p: &str) -> PathBuf {
        let p = Path::new(p);
        if p.is_absolute() { p.to_path_buf() } else { base.join(p) }
    }
}

/// Default module set: the features of OdinGuard 0.3 (regression profile). New protection modules are opt-in.
pub const DEFAULT_MODULES: &[&str] = &[
    "core", "hwid", "multi-client", "heartbeat", "integrity", "cheat-signatures", "grf-rules", "input-filter",
    "crash-report", "client-commands", "colors", "branding", "lgp", "auras", "ban-system",
];

/// Schema migrations, oldest first. Schema 1 (Studio preview): `[general].mode = "warn|kick|required"`,
/// `signatures.files` instead of `sources`, no allowlist, no rollout table.
pub fn migrate(v: &mut toml::Value) -> Result<u32> {
    let t = v.as_table_mut().ok_or_else(|| Error::Invalid("profile is not a table".into()))?;
    let from = t.get("schema_version").and_then(|x| x.as_integer()).unwrap_or(1) as u32;
    if from > SCHEMA_VERSION {
        return Err(Error::Invalid(format!("profile schema {from} is newer than this Studio ({SCHEMA_VERSION}); update the Studio")));
    }
    if from < 2 {
        let mode = t
            .get_mut("general")
            .and_then(|g| g.as_table_mut())
            .and_then(|g| g.remove("mode"))
            .and_then(|m| m.as_str().map(str::to_string))
            .unwrap_or_else(|| "warn".into());
        let stage = match mode.as_str() {
            "required" => 4,
            "kick" => 2,
            _ => 1,
        };
        let mut r = toml::map::Map::new();
        r.insert("stage".into(), toml::Value::Integer(stage));
        r.insert("reviewed_rules".into(), toml::Value::Array(vec![]));
        t.entry("rollout").or_insert(toml::Value::Table(r));
        if let Some(s) = t.get_mut("signatures").and_then(|s| s.as_table_mut()) {
            if let Some(files) = s.remove("files") {
                s.insert("sources".into(), files);
            }
            s.entry("allowlist").or_insert(toml::Value::Array(vec![]));
            s.entry("max_action").or_insert(toml::Value::String("flagban".into()));
            s.entry("version").or_insert(toml::Value::Integer(1));
        }
        t.insert("schema_version".into(), toml::Value::Integer(2));
    }
    Ok(from)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn reg() -> Registry {
        Registry::load(&Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../modules")).unwrap()
    }

    #[test]
    fn new_profile_validates_and_round_trips() {
        let p = Profile::new("example", "Example RO").unwrap();
        assert!(p.validate(&reg()).unwrap().is_empty());
        let dir = tempfile::tempdir().unwrap();
        let f = dir.path().join("p.toml");
        p.save(&f).unwrap();
        assert_eq!(Profile::load(&f).unwrap(), p);
    }

    #[test]
    fn validation_catches_errors() {
        let r = reg();
        let mut p = Profile::new("x", "X").unwrap();
        p.modules.retain(|m| m != "heartbeat");
        assert!(p.validate(&r).is_err(), "integrity needs heartbeat");
        let mut p = Profile::new("x", "X").unwrap();
        p.params.insert("heartbeat.interval".into(), toml::Value::Integer(1));
        assert!(p.validate(&r).is_err(), "below min");
        let mut p = Profile::new("x", "X").unwrap();
        p.params.insert("integrity.tamper_action".into(), toml::Value::String("explode".into()));
        assert!(p.validate(&r).is_err(), "unknown action");
        let mut p = Profile::new("x", "X").unwrap();
        p.commands.insert("lgp".into(), CommandSetting { enabled: true, name: Some("bad name".into()), ..Default::default() });
        assert!(p.validate(&r).is_err(), "command name");
    }

    #[test]
    fn schema1_profile_migrates_without_data_loss() {
        let old = r#"
schema_version = 1
modules = ["core", "rate-limit"]
output = "dist/old"
aura_catalog = ""
[general]
id = "old"
operator = "Old Server"
client_build = "2018-06-20"
ports = [6900]
splash_ms = 0
variation_seed = "00112233445566778899aabbccddeeff"
hwid_salt = "old-salt-123"
mode = "kick"
[keys]
server_seed = "k/s"
signing_seed = "k/g"
build_secret = "k/b"
[params]
"rate-limit.packets_per_second" = 300
[signatures]
files = ["sig.toml"]
[assets.odin_grf]
lgp = "a/lgp"
"#;
        let dir = tempfile::tempdir().unwrap();
        let f = dir.path().join("old.toml");
        std::fs::write(&f, old).unwrap();
        let p = Profile::load(&f).unwrap();
        assert_eq!(p.schema_version, 2);
        assert_eq!(p.rollout.stage, 2, "mode kick -> stage 2");
        assert_eq!(p.signatures.sources, vec!["sig.toml".to_string()]);
        assert_eq!(p.general.hwid_salt, "old-salt-123");
        assert_eq!(p.params["rate-limit.packets_per_second"].as_integer(), Some(300));
        assert_eq!(p.assets.odin_grf["lgp"], "a/lgp");
        assert!(p.validate(&reg()).is_ok());
        let mut newer: toml::Value = toml::from_str("schema_version = 99").unwrap();
        assert!(migrate(&mut newer).is_err());
    }
}

//! Module, command and aura registries (`modules/*.toml` in the OdinGuard repository).

use serde::{Deserialize, Serialize};
use std::path::Path;

use crate::{Error, Result};

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct Param {
    pub name: String,
    #[serde(rename = "type")]
    pub kind: String,
    pub default: toml::Value,
    #[serde(default)]
    pub min: Option<i64>,
    #[serde(default)]
    pub max: Option<i64>,
    #[serde(default)]
    pub conf: Option<String>,
    #[serde(default)]
    pub help: String,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct Module {
    pub id: String,
    pub version: String,
    pub kind: String,
    pub side: String,
    #[serde(default)]
    pub requires: Vec<String>,
    #[serde(default)]
    pub client_src: Vec<String>,
    #[serde(default)]
    pub define: Option<String>,
    #[serde(default)]
    pub server_key: Option<String>,
    #[serde(default)]
    pub assets: Vec<String>,
    #[serde(default)]
    pub summary: String,
    #[serde(default, rename = "param")]
    pub params: Vec<Param>,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct Command {
    pub id: String,
    pub kind: String,
    pub name: String,
    pub module: String,
    pub group: i64,
    pub rate_ms: i64,
    #[serde(default)]
    pub params: String,
    pub help_en: String,
    pub help_de: String,
}

#[derive(Debug, Deserialize)]
struct ModulesFile {
    schema: u32,
    #[serde(rename = "module")]
    modules: Vec<Module>,
}

#[derive(Debug, Deserialize)]
struct CommandsFile {
    #[serde(rename = "command")]
    commands: Vec<Command>,
}

#[derive(Debug, Clone, Serialize)]
pub struct Registry {
    pub modules: Vec<Module>,
    pub commands: Vec<Command>,
}

pub const ACTIONS: &[&str] = &["log", "flag", "kick", "ban", "flagban"];

fn read<T: serde::de::DeserializeOwned>(path: &Path) -> Result<T> {
    let text = std::fs::read_to_string(path).map_err(|e| Error::Io(path.display().to_string(), e))?;
    toml::from_str(&text).map_err(|e| Error::Parse(path.display().to_string(), e.to_string()))
}

impl Registry {
    /// Loads `modules.toml` and `commands.toml` from the registry folder.
    pub fn load(dir: &Path) -> Result<Self> {
        let m: ModulesFile = read(&dir.join("modules.toml"))?;
        if m.schema != 1 {
            return Err(Error::Invalid(format!("modules.toml schema {} is not supported", m.schema)));
        }
        let c: CommandsFile = read(&dir.join("commands.toml"))?;
        let r = Registry { modules: m.modules, commands: c.commands };
        r.check()?;
        Ok(r)
    }

    pub fn module(&self, id: &str) -> Option<&Module> {
        self.modules.iter().find(|m| m.id == id)
    }

    /// Internal consistency: unique ids, known dependencies and command modules, valid defaults.
    fn check(&self) -> Result<()> {
        let mut ids = std::collections::HashSet::new();
        for m in &self.modules {
            if !ids.insert(m.id.as_str()) {
                return Err(Error::Invalid(format!("module {} defined twice", m.id)));
            }
        }
        for m in &self.modules {
            for r in &m.requires {
                if !ids.contains(r.as_str()) {
                    return Err(Error::Invalid(format!("module {} requires unknown module {r}", m.id)));
                }
            }
            for p in &m.params {
                if p.kind == "action" && !ACTIONS.contains(&p.default.as_str().unwrap_or("")) {
                    return Err(Error::Invalid(format!("{}.{}: unknown default action", m.id, p.name)));
                }
            }
        }
        for c in &self.commands {
            if !ids.contains(c.module.as_str()) {
                return Err(Error::Invalid(format!("command {} belongs to unknown module {}", c.id, c.module)));
            }
        }
        Ok(())
    }

    /// Every module needed by `selected`, in registry order (dependencies resolved transitively).
    pub fn closure(&self, selected: &[String]) -> Result<Vec<String>> {
        let mut want: Vec<String> = Vec::new();
        let mut stack: Vec<String> = selected.to_vec();
        while let Some(id) = stack.pop() {
            let m = self.module(&id).ok_or_else(|| Error::Invalid(format!("unknown module {id}")))?;
            if !want.contains(&id) {
                want.push(id.clone());
                stack.extend(m.requires.iter().cloned());
            }
        }
        Ok(self.modules.iter().filter(|m| want.contains(&m.id)).map(|m| m.id.clone()).collect())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn reg() -> Registry {
        Registry::load(&Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../modules")).unwrap()
    }

    #[test]
    fn repository_registry_is_consistent() {
        let r = reg();
        assert!(r.module("core").is_some());
        assert!(r.commands.iter().any(|c| c.id == "lgp"));
        assert!(r.module("auras").is_none(), "auras are a separate rAthena feature");
    }

    #[test]
    fn closure_pulls_in_dependencies() {
        let r = reg();
        let c = r.closure(&["ch-code".into()]).unwrap();
        for need in ["core", "heartbeat", "integrity", "ch-code"] {
            assert!(c.contains(&need.to_string()), "{need} missing in {c:?}");
        }
        assert!(r.closure(&["nope".into()]).is_err());
    }
}

//! Signature package of the cheat-signatures module: compiled from the TOML sources (analysis/cheat-signatures
//! format) into the binary format the server checks (src/common/odinguard.cpp og_load_sigpack):
//! "OGSP" u8 format(1) u32 version u32 created(0) u16 n, n x { u16 id, u8 kind, u8 action, u8 score, u8 nlen, name,
//! u8 plen, pattern }, then u8 signature[64] = EdDSA(BLAKE2b) of everything before, Studio signing key.

use serde::Deserialize;
use std::path::Path;

use crate::{Error, Result};

#[derive(Debug, Clone, Deserialize)]
pub struct SigEntry {
    pub id: String,
    #[serde(default)]
    pub source: String,
    pub kind: String,
    pub pattern: String,
    #[serde(default = "medium")]
    pub confidence: String,
    #[serde(default = "medium")]
    pub severity: String,
    #[serde(default = "logs")]
    pub action: String,
}

fn medium() -> String {
    "medium".into()
}
fn logs() -> String {
    "log".into()
}

#[derive(Debug, Deserialize)]
struct SigFile {
    #[serde(default, rename = "signature")]
    signatures: Vec<SigEntry>,
    #[serde(default, rename = "allow")]
    allow: Vec<SigEntry>,
}

/// Wire kind (og_proto.h OG_KIND_*) of a signature kind; None = not checkable by the guard.
pub fn wire_kind(kind: &str, allow: bool) -> Option<u8> {
    if allow {
        return match kind {
            "file_sha256" | "grf_sha256" => Some(64),
            "path" | "allow_path" => Some(65),
            _ => None,
        };
    }
    Some(match kind {
        "process" => 1,
        "window_title" | "window" => 2,
        "module" => 3,
        "window_class" => 7,
        "file_name" => 8,
        "file_sha256" => 9,
        "driver" => 10,
        "grf_sha256" => 11,
        _ => return None,
    })
}

/// Server action byte (odinguard.hpp e_og_action) and its strength for capping.
fn action(a: &str) -> Result<(u8, u8)> {
    Ok(match a {
        "log" => (0, 0),
        "flag" => (3, 1),
        "flagban" => (4, 2),
        "kick" => (1, 3),
        "ban" => (2, 4),
        _ => return Err(Error::Invalid(format!("unknown action {a}"))),
    })
}

fn score(severity: &str, confidence: &str) -> u8 {
    let s: f64 = match severity {
        "critical" => 100.0,
        "high" => 60.0,
        "medium" => 30.0,
        _ => 10.0,
    };
    let c = match confidence {
        "high" => 1.0,
        "medium" => 0.6,
        _ => 0.3,
    };
    (s * c).round() as u8
}

#[derive(Debug, Default, Clone, serde::Serialize)]
pub struct Report {
    pub signatures: usize,
    pub allow: usize,
    /// kinds that only the server can use (grf_entry, import/string patterns, registry, rules)
    pub skipped: Vec<(String, String)>,
    pub capped: usize,
}

/// Compiles and signs the package. `max_action` caps every action ("" = none).
pub fn build(sources: &[&Path], allowlists: &[&Path], version: u32, max_action: &str, signing_seed: &[u8; 32]) -> Result<(Vec<u8>, Report)> {
    let mut sigs = Vec::new();
    let mut allows = Vec::new();
    for p in sources.iter().chain(allowlists) {
        let text = std::fs::read_to_string(p).map_err(|e| Error::Io(p.display().to_string(), e))?;
        let f: SigFile = toml::from_str(&text).map_err(|e| Error::Parse(p.display().to_string(), e.to_string()))?;
        sigs.extend(f.signatures);
        allows.extend(f.allow);
    }
    sigs.sort_by(|a, b| a.id.cmp(&b.id));
    allows.sort_by(|a, b| a.id.cmp(&b.id));
    let cap = if max_action.is_empty() { None } else { Some(action(max_action)?) };
    let mut rep = Report::default();
    let mut body = Vec::new();
    let mut n: u16 = 0;
    for (e, allow) in sigs.iter().map(|s| (s, false)).chain(allows.iter().map(|s| (s, true))) {
        let Some(kind) = wire_kind(&e.kind, allow) else {
            rep.skipped.push((e.id.clone(), e.kind.clone()));
            continue;
        };
        let (mut act, strength) = action(&e.action)?;
        if let Some((cap_act, cap_strength)) = cap
            && strength > cap_strength
        {
            act = cap_act;
            rep.capped += 1;
        }
        let pattern = if matches!(kind, 9 | 11 | 64) { e.pattern.to_ascii_lowercase() } else { e.pattern.clone() };
        if e.id.len() > 255 || pattern.len() > 255 || pattern.is_empty() {
            return Err(Error::Invalid(format!("signature {}: id or pattern too long/empty", e.id)));
        }
        n = n.checked_add(1).filter(|&n| n < 30000).ok_or_else(|| Error::Invalid("too many signatures".into()))?;
        body.extend_from_slice(&n.to_le_bytes());
        body.push(kind);
        body.push(act);
        body.push(if allow { 0 } else { score(&e.severity, &e.confidence) });
        body.push(e.id.len() as u8);
        body.extend_from_slice(e.id.as_bytes());
        body.push(pattern.len() as u8);
        body.extend_from_slice(pattern.as_bytes());
        if allow { rep.allow += 1 } else { rep.signatures += 1 }
    }
    let mut out = Vec::with_capacity(body.len() + 79);
    out.extend_from_slice(b"OGSP");
    out.push(1);
    out.extend_from_slice(&version.to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&n.to_le_bytes());
    out.extend_from_slice(&body);
    let sig = crate::crypto::sign(signing_seed, &out);
    out.extend_from_slice(&sig);
    Ok((out, rep))
}

#[derive(Debug, Clone, serde::Serialize, PartialEq)]
pub struct Entry {
    pub id: u16,
    pub kind: u8,
    pub action: u8,
    pub score: u8,
    pub name: String,
    pub pattern: String,
}

/// Verifies and parses a package (same checks as the server). Returns version and entries.
pub fn verify(data: &[u8], public_key: &[u8; 32]) -> Result<(u32, Vec<Entry>)> {
    if data.len() < 15 + 64 || &data[..4] != b"OGSP" || data[4] != 1 {
        return Err(Error::Signature("not a signature package".into()));
    }
    let body = data.len() - 64;
    let sig: [u8; 64] = data[body..].try_into().map_err(|_| Error::Signature("short".into()))?;
    if !crate::crypto::verify(public_key, &data[..body], &sig) {
        return Err(Error::Signature("signature does not match the signing key".into()));
    }
    let version = u32::from_le_bytes(data[5..9].try_into().unwrap_or_default());
    let n = u16::from_le_bytes([data[13], data[14]]);
    let mut pos = 15;
    let mut out = Vec::new();
    let take = |pos: usize, len: usize| -> Result<&[u8]> { data.get(pos..pos + len).filter(|_| pos + len <= body).ok_or_else(|| Error::Signature("truncated".into())) };
    for _ in 0..n {
        let h = take(pos, 6)?;
        let (id, kind, act, sc, nl) = (u16::from_le_bytes([h[0], h[1]]), h[2], h[3], h[4], h[5] as usize);
        let name = String::from_utf8_lossy(take(pos + 6, nl)?).into_owned();
        let pl = take(pos + 6 + nl, 1)?[0] as usize;
        let pattern = String::from_utf8_lossy(take(pos + 7 + nl, pl)?).into_owned();
        pos += 7 + nl + pl;
        out.push(Entry { id, kind, action: act, score: sc, name, pattern });
    }
    Ok((version, out))
}

#[cfg(test)]
mod tests {
    use super::*;

    const SIGS: &str = r#"
schema = 1
[[signature]]
id = "T-002"
kind = "process"
pattern = "cheat.exe"
confidence = "high"
severity = "high"
action = "ban"
[[signature]]
id = "T-001"
kind = "grf_entry"
pattern = "data\\x.act|00"
[[signature]]
id = "T-003"
kind = "file_sha256"
pattern = "ABCDEF"
action = "flag"
"#;
    const ALLOW: &str = r#"
[[allow]]
id = "A-1"
kind = "file_sha256"
pattern = "00FF"
"#;

    fn files() -> (tempfile::TempDir, std::path::PathBuf, std::path::PathBuf) {
        let d = tempfile::tempdir().unwrap();
        let s = d.path().join("s.toml");
        let a = d.path().join("a.toml");
        std::fs::write(&s, SIGS).unwrap();
        std::fs::write(&a, ALLOW).unwrap();
        (d, s, a)
    }

    #[test]
    fn build_verify_cap_and_skip() {
        let (_d, s, a) = files();
        let seed = [3u8; 32];
        let (pack, rep) = build(&[&s], &[&a], 7, "flagban", &seed).unwrap();
        assert_eq!((rep.signatures, rep.allow, rep.capped), (2, 1, 1));
        assert_eq!(rep.skipped, vec![("T-001".to_string(), "grf_entry".to_string())]);
        let (v, e) = verify(&pack, &crate::crypto::public_key(&seed)).unwrap();
        assert_eq!(v, 7);
        assert_eq!(e[0].name, "T-002");
        assert_eq!(e[0].action, 4, "ban capped to flagban");
        assert_eq!(e[0].score, 60);
        assert_eq!(e[1].pattern, "abcdef");
        assert_eq!(e[2].kind, 64);
        // same input, same bytes (reproducible)
        assert_eq!(build(&[&s], &[&a], 7, "flagban", &seed).unwrap().0, pack);
    }

    #[test]
    fn tampered_or_foreign_package_is_rejected() {
        let (_d, s, a) = files();
        let seed = [3u8; 32];
        let (mut pack, _) = build(&[&s], &[&a], 1, "", &seed).unwrap();
        assert!(verify(&pack, &crate::crypto::public_key(&[4u8; 32])).is_err());
        pack[20] ^= 1;
        assert!(verify(&pack, &crate::crypto::public_key(&seed)).is_err());
    }

    #[test]
    fn repository_signatures_compile() {
        let root = crate::repo_root();
        let s = root.join("analysis/cheat-signatures/signatures.toml");
        let a = root.join("analysis/cheat-signatures/allowlist.toml");
        if !s.exists() {
            return;
        }
        let (_, rep) = build(&[&s], &[&a], 1, "flagban", &[1u8; 32]).unwrap();
        assert!(rep.signatures > 40, "{rep:?}");
    }
}

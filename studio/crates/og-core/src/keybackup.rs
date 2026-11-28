//! One encrypted backup file for all the keys of a profile ("Back up keys" in Studio, `og keys-backup`).
//!
//! File: `OGKEYS1\n` | salt (16) | nonce (24) | XChaCha20-Poly1305 ciphertext of a JSON document. The key is derived from
//! the password with Argon2id (64 MiB, 3 passes, 1 lane), the magic is the authenticated header. A wrong password
//! and a damaged file are both reported as "wrong password or damaged file" (the AEAD cannot tell them apart).
//! The same format is used by the OpenThor Patch Maker.

use std::path::{Path, PathBuf};

use argon2::{Algorithm, Argon2, Params, Version};
use chacha20poly1305::aead::{Aead, KeyInit, Payload};
use chacha20poly1305::{XChaCha20Poly1305, XNonce};
use serde::{Deserialize, Serialize};

use crate::profile::Profile;
use crate::{Error, Result};

const MAGIC: &[u8; 8] = b"OGKEYS1\n";
const MIN_PASSWORD: usize = 8;

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
pub struct KeyFile {
    /// `server_seed`, `signing_seed`, `build_secret`, or `extra:<file name>`
    pub role: String,
    pub name: String,
    /// file content, hex
    pub data: String,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Bundle {
    pub product: String,
    pub profile: String,
    pub created: u64,
    pub files: Vec<KeyFile>,
}

fn invalid(m: impl Into<String>) -> Error {
    Error::Invalid(m.into())
}

fn derive(password: &str, salt: &[u8]) -> Result<[u8; 32]> {
    let params = Params::new(64 * 1024, 3, 1, Some(32)).map_err(|e| invalid(e.to_string()))?;
    let mut key = [0u8; 32];
    Argon2::new(Algorithm::Argon2id, Version::V0x13, params)
        .hash_password_into(password.as_bytes(), salt, &mut key)
        .map_err(|e| invalid(e.to_string()))?;
    Ok(key)
}

pub fn check_password(password: &str) -> Result<()> {
    if password.chars().count() < MIN_PASSWORD {
        return Err(invalid(format!("the password needs at least {MIN_PASSWORD} characters")));
    }
    Ok(())
}

pub fn seal(bundle: &Bundle, password: &str) -> Result<Vec<u8>> {
    check_password(password)?;
    let salt: [u8; 16] = crate::crypto::random().map_err(|e| Error::Io("random".into(), e))?;
    let nonce: [u8; 24] = crate::crypto::random().map_err(|e| Error::Io("random".into(), e))?;
    let key = derive(password, &salt)?;
    let plain = serde_json::to_vec(bundle).map_err(|e| invalid(e.to_string()))?;
    let ct = XChaCha20Poly1305::new((&key).into())
        .encrypt(XNonce::from_slice(&nonce), Payload { msg: &plain, aad: MAGIC })
        .map_err(|_| invalid("encryption failed"))?;
    let mut out = MAGIC.to_vec();
    out.extend_from_slice(&salt);
    out.extend_from_slice(&nonce);
    out.extend_from_slice(&ct);
    Ok(out)
}

pub fn open(bytes: &[u8], password: &str) -> Result<Bundle> {
    if bytes.len() < MAGIC.len() + 16 + 24 + 16 || &bytes[..MAGIC.len()] != MAGIC {
        return Err(invalid("this is not an OdinGuard key backup file"));
    }
    let (salt, rest) = bytes[MAGIC.len()..].split_at(16);
    let (nonce, ct) = rest.split_at(24);
    let key = derive(password, salt)?;
    let plain = XChaCha20Poly1305::new((&key).into())
        .decrypt(XNonce::from_slice(nonce), Payload { msg: ct, aad: MAGIC })
        .map_err(|_| invalid("wrong password or damaged file"))?;
    serde_json::from_slice(&plain).map_err(|_| invalid("the backup content is not readable"))
}

fn read(path: &Path) -> Result<Vec<u8>> {
    std::fs::read(path).map_err(|e| Error::Io(path.display().to_string(), e))
}

/// The key files of a profile (and `extra` files such as the Studio update key), read from disk.
pub fn collect(profile_path: &Path, extra: &[PathBuf]) -> Result<Bundle> {
    let p = Profile::load(profile_path)?;
    let base = profile_path.parent().unwrap_or(Path::new("."));
    let mut files = Vec::new();
    for (role, rel) in [("server_seed", &p.keys.server_seed), ("signing_seed", &p.keys.signing_seed), ("build_secret", &p.keys.build_secret)] {
        let path = p.resolve(base, rel);
        files.push(KeyFile { role: role.into(), name: path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default(), data: hex::encode(read(&path)?) });
    }
    for path in extra {
        let name = path.file_name().map(|n| n.to_string_lossy().into_owned()).ok_or_else(|| invalid("extra file without a name"))?;
        files.push(KeyFile { role: format!("extra:{name}"), name, data: hex::encode(read(path)?) });
    }
    let created = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
    Ok(Bundle { product: "OdinGuard".into(), profile: p.general.id.clone(), created, files })
}

pub fn backup(profile_path: &Path, extra: &[PathBuf], out: &Path, password: &str) -> Result<usize> {
    let bundle = collect(profile_path, extra)?;
    let bytes = seal(&bundle, password)?;
    // the file is only worth something if it can be opened again
    if open(&bytes, password)?.files.len() != bundle.files.len() {
        return Err(invalid("the backup could not be read back"));
    }
    std::fs::write(out, bytes).map_err(|e| Error::Io(out.display().to_string(), e))?;
    Ok(bundle.files.len())
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Restored {
    pub path: String,
    /// `written`, `same` (already identical), `kept` (differs and no --force)
    pub action: String,
}

/// Writes the files back: the three key files to the places the profile points at, extras into `extra_dir`.
/// A file that exists with other content is only replaced with `force`.
pub fn restore(bundle: &Bundle, profile_path: &Path, extra_dir: Option<&Path>, force: bool) -> Result<Vec<Restored>> {
    let p = Profile::load(profile_path)?;
    let base = profile_path.parent().unwrap_or(Path::new("."));
    // resolve everything first: a backup that cannot be restored completely must not be restored halfway
    let mut plan = Vec::new();
    for f in &bundle.files {
        let data = hex::decode(&f.data).map_err(|_| invalid("the backup content is not readable"))?;
        let dest = match f.role.as_str() {
            "server_seed" => p.resolve(base, &p.keys.server_seed),
            "signing_seed" => p.resolve(base, &p.keys.signing_seed),
            "build_secret" => p.resolve(base, &p.keys.build_secret),
            r if r.starts_with("extra:") => {
                let dir = extra_dir.ok_or_else(|| invalid(format!("the backup holds {} (an extra file): say where it goes", f.name)))?;
                dir.join(Path::new(&f.name).file_name().ok_or_else(|| invalid("bad file name in the backup"))?)
            }
            other => return Err(invalid(format!("unknown entry {other} in the backup"))),
        };
        plan.push((dest, data));
    }
    let mut out = Vec::new();
    for (dest, data) in plan {
        let action = match std::fs::read(&dest) {
            Ok(cur) if cur == data => "same",
            Ok(_) if !force => "kept",
            _ => {
                if let Some(dir) = dest.parent() {
                    std::fs::create_dir_all(dir).map_err(|e| Error::Io(dir.display().to_string(), e))?;
                }
                std::fs::write(&dest, &data).map_err(|e| Error::Io(dest.display().to_string(), e))?;
                #[cfg(unix)]
                {
                    use std::os::unix::fs::PermissionsExt;
                    let _ = std::fs::set_permissions(&dest, std::fs::Permissions::from_mode(0o600));
                }
                "written"
            }
        };
        out.push(Restored { path: dest.display().to_string(), action: action.into() });
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn profile_in(dir: &Path) -> PathBuf {
        let path = dir.join("p.profile.toml");
        let mut p = Profile::new("t", "Test").unwrap();
        p.keys.server_seed = "keys/server.seed".into();
        p.keys.signing_seed = "keys/signing.seed".into();
        p.keys.build_secret = "keys/build.secret".into();
        p.save(&path).unwrap();
        for k in [&p.keys.server_seed, &p.keys.signing_seed, &p.keys.build_secret] {
            crate::keys::ensure(&dir.join(k)).unwrap();
        }
        path
    }

    #[test]
    fn backup_and_restore_roundtrip() {
        let a = tempfile::tempdir().unwrap();
        let prof = profile_in(a.path());
        let extra = a.path().join("updater.key");
        std::fs::write(&extra, "extra secret\n").unwrap();
        let out = a.path().join("keys.ogkeys");
        assert_eq!(backup(&prof, std::slice::from_ref(&extra), &out, "correct horse").unwrap(), 4);
        let raw = std::fs::read(&out).unwrap();
        assert!(!String::from_utf8_lossy(&raw).contains("extra secret"), "the file must be encrypted");
        assert!(open(&raw, "wrong password").unwrap_err().to_string().contains("wrong password"));
        let mut damaged = raw.clone();
        *damaged.last_mut().unwrap() ^= 1;
        assert!(open(&damaged, "correct horse").is_err());
        assert!(open(b"hello", "correct horse").unwrap_err().to_string().contains("not an OdinGuard key backup"));

        // a new machine: same profile, no keys
        let b = tempfile::tempdir().unwrap();
        let prof_b = b.path().join("p.profile.toml");
        std::fs::copy(&prof, &prof_b).unwrap();
        let bundle = open(&raw, "correct horse").unwrap();
        assert!(restore(&bundle, &prof_b, None, false).is_err(), "an extra file needs a folder");
        assert!(!b.path().join("keys").exists(), "a restore that fails writes nothing");
        let done = restore(&bundle, &prof_b, Some(b.path()), false).unwrap();
        assert!(done.iter().all(|r| r.action == "written" || r.action == "same"));
        for k in ["server.seed", "signing.seed", "build.secret"] {
            assert_eq!(std::fs::read(a.path().join("keys").join(k)).unwrap(), std::fs::read(b.path().join("keys").join(k)).unwrap());
        }
        assert_eq!(std::fs::read_to_string(b.path().join("updater.key")).unwrap(), "extra secret\n");

        // existing different key: kept unless forced
        std::fs::write(b.path().join("keys/server.seed"), "other\n").unwrap();
        let done = restore(&bundle, &prof_b, Some(b.path()), false).unwrap();
        assert_eq!(done.iter().find(|r| r.path.ends_with("server.seed")).unwrap().action, "kept");
        assert_eq!(std::fs::read_to_string(b.path().join("keys/server.seed")).unwrap(), "other\n");
        restore(&bundle, &prof_b, Some(b.path()), true).unwrap();
        assert_eq!(std::fs::read(b.path().join("keys/server.seed")).unwrap(), std::fs::read(a.path().join("keys/server.seed")).unwrap());
    }

    #[test]
    fn short_password_is_refused() {
        assert!(check_password("short").is_err());
        assert!(check_password("long enough").is_ok());
    }
}

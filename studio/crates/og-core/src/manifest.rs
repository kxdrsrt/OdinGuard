//! Release manifest: SHA-256 and size of every shipped file, signed with the Studio signing key (EdDSA/BLAKE2b).
//! `manifest.json` + `manifest.sig` (hex) + `signing.pub` (hex). Secrets are never listed.

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::path::Path;

use crate::{Error, Result};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct FileEntry {
    pub path: String,
    pub sha256: String,
    pub size: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Manifest {
    pub product: String,
    pub version: String,
    pub protocol: u32,
    pub profile: String,
    pub rollout_stage: u8,
    pub sigpack_version: u32,
    pub files: Vec<FileEntry>,
}

pub fn sha256_file(path: &Path) -> Result<(String, u64)> {
    let data = std::fs::read(path).map_err(|e| Error::Io(path.display().to_string(), e))?;
    Ok((hex::encode(Sha256::digest(&data)), data.len() as u64))
}

/// Lists every file below `root` (sorted, '/' separators) except the excluded top-level folders.
pub fn collect(root: &Path, exclude: &[&str]) -> Result<Vec<FileEntry>> {
    fn walk(dir: &Path, root: &Path, exclude: &[&str], out: &mut Vec<FileEntry>) -> Result<()> {
        let mut items: Vec<_> = std::fs::read_dir(dir).map_err(|e| Error::Io(dir.display().to_string(), e))?.flatten().collect();
        items.sort_by_key(|e| e.file_name());
        for e in items {
            let p = e.path();
            let rel = p.strip_prefix(root).unwrap_or(&p).to_string_lossy().replace('\\', "/");
            if exclude.iter().any(|x| rel == *x || rel.starts_with(&format!("{x}/"))) {
                continue;
            }
            if p.is_dir() {
                walk(&p, root, exclude, out)?;
            } else {
                let (sha256, size) = sha256_file(&p)?;
                out.push(FileEntry { path: rel, sha256, size });
            }
        }
        Ok(())
    }
    let mut out = Vec::new();
    walk(root, root, exclude, &mut out)?;
    Ok(out)
}

pub fn write_signed(dir: &Path, m: &Manifest, signing_seed: &[u8; 32]) -> Result<()> {
    let json = serde_json::to_vec_pretty(m).map_err(|e| Error::Invalid(e.to_string()))?;
    let sig = crate::crypto::sign(signing_seed, &json);
    let w = |name: &str, data: &[u8]| std::fs::write(dir.join(name), data).map_err(|e| Error::Io(name.into(), e));
    w("manifest.json", &json)?;
    w("manifest.sig", hex::encode(sig).as_bytes())?;
    w("signing.pub", hex::encode(crate::crypto::public_key(signing_seed)).as_bytes())
}

/// Checks the signature and every file hash of a release folder. Returns the manifest.
pub fn verify_release(dir: &Path, pinned_key: Option<&[u8; 32]>) -> Result<Manifest> {
    let read = |name: &str| std::fs::read(dir.join(name)).map_err(|e| Error::Io(name.into(), e));
    let json = read("manifest.json")?;
    let sig: [u8; 64] = hex::decode(String::from_utf8_lossy(&read("manifest.sig")?).trim())
        .ok()
        .and_then(|v| v.try_into().ok())
        .ok_or_else(|| Error::Signature("manifest.sig".into()))?;
    let key: [u8; 32] = match pinned_key {
        Some(k) => *k,
        None => hex::decode(String::from_utf8_lossy(&read("signing.pub")?).trim())
            .ok()
            .and_then(|v| v.try_into().ok())
            .ok_or_else(|| Error::Signature("signing.pub".into()))?,
    };
    if !crate::crypto::verify(&key, &json, &sig) {
        return Err(Error::Signature("manifest signature invalid".into()));
    }
    let m: Manifest = serde_json::from_slice(&json).map_err(|e| Error::Parse("manifest.json".into(), e.to_string()))?;
    for f in &m.files {
        let (h, size) = sha256_file(&dir.join(&f.path))?;
        if h != f.sha256 || size != f.size {
            return Err(Error::Signature(format!("{} differs from the manifest", f.path)));
        }
    }
    Ok(m)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn signed_release_detects_changes() {
        let d = tempfile::tempdir().unwrap();
        std::fs::create_dir_all(d.path().join("client")).unwrap();
        std::fs::write(d.path().join("client/a.dll"), b"abc").unwrap();
        std::fs::write(d.path().join("secret.env"), b"x").unwrap();
        let files = collect(d.path(), &["secret.env"]).unwrap();
        assert_eq!(files.len(), 1);
        let m = Manifest { product: "OdinGuard Anti Cheat".into(), version: "1.0.0".into(), protocol: 2, profile: "t".into(), rollout_stage: 1, sigpack_version: 1, files };
        let seed = [9u8; 32];
        write_signed(d.path(), &m, &seed).unwrap();
        assert_eq!(verify_release(d.path(), None).unwrap(), m);
        assert!(verify_release(d.path(), Some(&crate::crypto::public_key(&[8u8; 32]))).is_err(), "foreign key");
        std::fs::write(d.path().join("client/a.dll"), b"abd").unwrap();
        assert!(verify_release(d.path(), None).is_err(), "changed file");
    }
}

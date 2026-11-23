//! Updates of the Studio itself (Tauri updater format) and of OdinGuard core/signature packages.
//!
//! Format: the static JSON of the Tauri updater plugin `{version, notes, pub_date, platforms: {<target>:
//! {signature, url}}}`; `signature` is the minisign signature (base64 of the .sig file, as `tauri signer sign`
//! writes it). In addition the manifest itself must be signed (`<manifest>.sig` next to it), so a compromised web
//! space cannot announce versions or notes.
//!
//! Rules: only https (http only for loopback and only when the caller allows it, for tests); manifest signature and
//! artifact signature are checked before anything is stored; a version lower than the last accepted one is refused
//! (downgrade/rollback attack) unless the operator rolls back explicitly to a version kept locally; the SHA-256 of
//! every accepted manifest is pinned in the state file.

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use crate::{Error, Result};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Platform {
    pub signature: String,
    pub url: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct UpdateManifest {
    pub version: String,
    #[serde(default)]
    pub notes: String,
    #[serde(default)]
    pub pub_date: String,
    pub platforms: BTreeMap<String, Platform>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct State {
    /// highest version ever accepted per channel/artifact key
    pub accepted: BTreeMap<String, String>,
    /// SHA-256 of the manifests that were accepted
    pub pinned: Vec<String>,
    /// version in use per key and the one before it (rollback)
    pub current: BTreeMap<String, String>,
    pub previous: BTreeMap<String, String>,
}

pub struct Updater {
    pub endpoint: String,
    pub channel: String,
    pub public_key: String,
    pub dir: PathBuf,
    pub allow_loopback_http: bool,
}

/// Compares versions like 1.2.10 > 1.2.9; a pre-release (1.3.0-beta.1) is lower than the release.
pub fn version_cmp(a: &str, b: &str) -> std::cmp::Ordering {
    let split = |v: &str| {
        let (core, pre) = v.split_once('-').map_or((v, None), |(c, p)| (c, Some(p.to_string())));
        (core.split('.').map(|x| x.parse::<u64>().unwrap_or(0)).collect::<Vec<_>>(), pre)
    };
    let (ca, pa) = split(a);
    let (cb, pb) = split(b);
    ca.cmp(&cb).then_with(|| match (pa, pb) {
        (None, None) => std::cmp::Ordering::Equal,
        (None, Some(_)) => std::cmp::Ordering::Greater,
        (Some(_), None) => std::cmp::Ordering::Less,
        (Some(x), Some(y)) => x.cmp(&y),
    })
}

fn verify_minisign(public_key: &str, data: &[u8], signature_b64_or_text: &str) -> Result<()> {
    let pk = minisign_verify::PublicKey::from_base64(public_key.trim()).map_err(|e| Error::Signature(format!("public key: {e}")))?;
    // Tauri stores the .sig file base64 encoded; accept the plain text too
    let text = decode_b64(signature_b64_or_text.trim()).and_then(|b| String::from_utf8(b).ok()).unwrap_or_else(|| signature_b64_or_text.to_string());
    let sig = minisign_verify::Signature::decode(&text).map_err(|e| Error::Signature(format!("signature: {e}")))?;
    pk.verify(data, &sig, false).map_err(|e| Error::Signature(format!("{e}")))
}

fn decode_b64(s: &str) -> Option<Vec<u8>> {
    const T: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = Vec::new();
    let (mut acc, mut bits) = (0u32, 0u32);
    for c in s.bytes().filter(|c| !c.is_ascii_whitespace() && *c != b'=') {
        let v = T.iter().position(|&x| x == c)? as u32;
        acc = (acc << 6) | v;
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            out.push((acc >> bits) as u8);
        }
    }
    Some(out)
}

impl Updater {
    fn get(&self, url: &str) -> Result<Vec<u8>> {
        let loopback = url.starts_with("http://127.0.0.1:") || url.starts_with("http://localhost:");
        if !(url.starts_with("https://") || (self.allow_loopback_http && loopback)) {
            return Err(Error::Signature(format!("refusing {url}: updates only over https")));
        }
        let mut resp = ureq::get(url).call().map_err(|e| Error::Build(format!("download {url}: {e}")))?;
        resp.body_mut().with_config().limit(256 * 1024 * 1024).read_to_vec().map_err(|e| Error::Build(format!("download {url}: {e}")))
    }

    fn state_path(&self) -> PathBuf {
        self.dir.join("state.json")
    }

    pub fn state(&self) -> State {
        std::fs::read(self.state_path()).ok().and_then(|b| serde_json::from_slice(&b).ok()).unwrap_or_default()
    }

    fn save(&self, s: &State) -> Result<()> {
        std::fs::create_dir_all(&self.dir).map_err(|e| Error::Io(self.dir.display().to_string(), e))?;
        std::fs::write(self.state_path(), serde_json::to_vec_pretty(s).unwrap_or_default()).map_err(|e| Error::Io("state.json".into(), e))
    }

    /// Fetches and verifies the channel manifest. Returns it when it announces a newer version than `current`.
    pub fn check(&self, key: &str, current: &str) -> Result<Option<UpdateManifest>> {
        let url = format!("{}/{}.json", self.endpoint.trim_end_matches('/'), self.channel);
        let body = self.get(&url)?;
        let sig = self.get(&format!("{url}.sig"))?;
        verify_minisign(&self.public_key, &body, &String::from_utf8_lossy(&sig))?;
        let m: UpdateManifest = serde_json::from_slice(&body).map_err(|e| Error::Parse(url.clone(), e.to_string()))?;
        if let Some(acc) = self.state().accepted.get(key)
            && version_cmp(&m.version, acc).is_lt()
        {
            return Err(Error::Signature(format!("manifest offers {} but {acc} was already accepted (downgrade refused)", m.version)));
        }
        Ok(version_cmp(&m.version, current).is_gt().then_some(m))
    }

    /// Downloads, verifies and stores the artifact for `target`; the previous version stays for a rollback.
    pub fn apply(&self, key: &str, m: &UpdateManifest, target: &str) -> Result<PathBuf> {
        let p = m.platforms.get(target).ok_or_else(|| Error::Invalid(format!("no artifact for {target}")))?;
        let data = self.get(&p.url)?;
        verify_minisign(&self.public_key, &data, &p.signature)?;
        let name = p.url.rsplit('/').next().unwrap_or("artifact");
        let dir = self.dir.join("versions").join(key).join(&m.version);
        std::fs::create_dir_all(&dir).map_err(|e| Error::Io(dir.display().to_string(), e))?;
        let file = dir.join(name);
        std::fs::write(&file, &data).map_err(|e| Error::Io(file.display().to_string(), e))?;
        let mut s = self.state();
        if let Some(cur) = s.current.get(key).cloned() {
            s.previous.insert(key.into(), cur);
        }
        s.current.insert(key.into(), m.version.clone());
        s.accepted.insert(key.into(), m.version.clone());
        s.pinned.push(hex::encode(Sha256::digest(serde_json::to_vec(m).unwrap_or_default())));
        self.save(&s)?;
        Ok(file)
    }

    /// Records a version installed by another installer (the Tauri updater plugin) as accepted and pinned, so a later
    /// manifest cannot offer an older one.
    pub fn record_installed(&self, key: &str, m: &UpdateManifest) -> Result<()> {
        let mut s = self.state();
        if let Some(cur) = s.current.get(key).cloned() {
            s.previous.insert(key.into(), cur);
        }
        s.current.insert(key.into(), m.version.clone());
        s.accepted.insert(key.into(), m.version.clone());
        s.pinned.push(hex::encode(Sha256::digest(serde_json::to_vec(m).unwrap_or_default())));
        self.save(&s)
    }

    /// Switches back to the previous locally kept version (explicit operator action).
    pub fn rollback(&self, key: &str) -> Result<String> {
        let mut s = self.state();
        let prev = s.previous.remove(key).ok_or_else(|| Error::Invalid(format!("no previous version of {key}")))?;
        if !self.dir.join("versions").join(key).join(&prev).is_dir() {
            return Err(Error::Invalid(format!("version {prev} of {key} is not kept locally")));
        }
        if let Some(cur) = s.current.insert(key.into(), prev.clone()) {
            s.previous.insert(key.into(), cur);
        }
        self.save(&s)?;
        Ok(prev)
    }

    pub fn current_file(&self, key: &str) -> Option<PathBuf> {
        let v = self.state().current.get(key)?.clone();
        std::fs::read_dir(self.dir.join("versions").join(key).join(v)).ok()?.flatten().next().map(|e| e.path())
    }
}

/// Writes a Tauri-compatible manifest for a local release folder (release script); signatures are added by the
/// release script with `tauri signer sign` / minisign.
pub fn manifest_for(version: &str, notes: &str, artifacts: &[(&str, &str, &str)]) -> UpdateManifest {
    UpdateManifest {
        version: version.into(),
        notes: notes.into(),
        pub_date: String::new(),
        platforms: artifacts.iter().map(|(t, url, sig)| (t.to_string(), Platform { url: url.to_string(), signature: sig.to_string() })).collect(),
    }
}

/// The minisign key line ("RW...") from a key line, a minisign .pub file or its base64 form (Tauri config).
pub fn minisign_key_line(any: &str) -> Option<String> {
    let find = |t: &str| t.lines().map(str::trim).find(|l| l.starts_with("RW") && l.len() == 56).map(str::to_string);
    find(any).or_else(|| decode_b64(any.trim()).and_then(|b| String::from_utf8(b).ok()).and_then(|t| find(&t)))
}

/// Writes `<channel>.json` for a release folder: every artifact must have its minisign `.sig` next to it
/// (as `tauri build` with a signing key or `tauri signer sign` writes it). The manifest itself is signed afterwards.
pub fn write_manifest(version: &str, notes: &str, base_url: &str, artifacts: &[(String, PathBuf)], out: &Path) -> Result<UpdateManifest> {
    if !base_url.starts_with("https://") {
        return Err(Error::Invalid(format!("base url {base_url} must be https")));
    }
    let mut list = Vec::new();
    for (target, file) in artifacts {
        let sig_path = PathBuf::from(format!("{}.sig", file.display()));
        let sig = std::fs::read_to_string(&sig_path).map_err(|e| Error::Io(sig_path.display().to_string(), e))?;
        let name = file.file_name().and_then(|n| n.to_str()).ok_or_else(|| Error::Invalid(format!("{}", file.display())))?;
        list.push((target.clone(), format!("{}/{name}", base_url.trim_end_matches('/')), sig.trim().to_string()));
    }
    let mut m = manifest_for(version, notes, &list.iter().map(|(a, b, c)| (a.as_str(), b.as_str(), c.as_str())).collect::<Vec<_>>());
    m.pub_date = std::env::var("SOURCE_DATE_EPOCH").unwrap_or_default();
    std::fs::write(out, serde_json::to_vec_pretty(&m).unwrap_or_default()).map_err(|e| Error::Io(out.display().to_string(), e))?;
    Ok(m)
}

/// Offline check of a release folder before publishing: manifest signature and every artifact signature against the
/// files in the same folder.
pub fn verify_local(manifest: &Path, public_key: &str) -> Result<UpdateManifest> {
    let body = std::fs::read(manifest).map_err(|e| Error::Io(manifest.display().to_string(), e))?;
    let sig_path = PathBuf::from(format!("{}.sig", manifest.display()));
    let sig = std::fs::read_to_string(&sig_path).map_err(|e| Error::Io(sig_path.display().to_string(), e))?;
    let key = minisign_key_line(public_key).ok_or_else(|| Error::Signature("public key not recognised".into()))?;
    verify_minisign(&key, &body, &sig)?;
    let m: UpdateManifest = serde_json::from_slice(&body).map_err(|e| Error::Parse(manifest.display().to_string(), e.to_string()))?;
    let dir = manifest.parent().unwrap_or(Path::new("."));
    for (target, p) in &m.platforms {
        let name = p.url.rsplit('/').next().unwrap_or_default();
        let data = std::fs::read(dir.join(name)).map_err(|e| Error::Io(format!("{target}: {name}"), e))?;
        verify_minisign(&key, &data, &p.signature).map_err(|e| Error::Signature(format!("{target}: {e}")))?;
    }
    Ok(m)
}

pub fn is_newer(candidate: &str, current: &str) -> bool {
    version_cmp(candidate, current).is_gt()
}

pub fn path_is_safe(p: &Path) -> bool {
    !p.components().any(|c| matches!(c, std::path::Component::ParentDir))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::net::TcpListener;
    use std::sync::{Arc, Mutex};

    /// tiny HTTP server for the tests: serves files from a map, path -> body
    fn serve(files: Arc<Mutex<BTreeMap<String, Vec<u8>>>>) -> String {
        let l = TcpListener::bind("127.0.0.1:0").unwrap();
        let addr = l.local_addr().unwrap();
        std::thread::spawn(move || {
            for s in l.incoming().flatten() {
                let files = files.clone();
                std::thread::spawn(move || {
                    let mut s = s;
                    let mut buf = [0u8; 4096];
                    let n = s.read(&mut buf).unwrap_or(0);
                    let req = String::from_utf8_lossy(&buf[..n]);
                    let path = req.split_whitespace().nth(1).unwrap_or("/").to_string();
                    let body = files.lock().unwrap().get(&path).cloned();
                    let resp = match body {
                        Some(b) => [format!("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n", b.len()).into_bytes(), b].concat(),
                        None => b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".to_vec(),
                    };
                    let _ = s.write_all(&resp);
                });
            }
        });
        format!("http://127.0.0.1:{}", addr.port())
    }

    struct Keys {
        pk: String,
        sk: minisign::SecretKey,
    }

    fn keys() -> Keys {
        let kp = minisign::KeyPair::generate_unencrypted_keypair().unwrap();
        Keys { pk: kp.pk.to_base64(), sk: kp.sk }
    }

    fn sign(k: &Keys, data: &[u8]) -> String {
        let sig = minisign::sign(None, &k.sk, std::io::Cursor::new(data), None, None).unwrap();
        sig.to_string()
    }

    fn b64(s: &str) -> String {
        const T: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        let b = s.as_bytes();
        let mut o = String::new();
        for c in b.chunks(3) {
            let n = (u32::from(c[0]) << 16) | (u32::from(*c.get(1).unwrap_or(&0)) << 8) | u32::from(*c.get(2).unwrap_or(&0));
            for i in 0..4 {
                if i <= c.len() { o.push(T[(n >> (18 - 6 * i) & 63) as usize] as char) } else { o.push('=') }
            }
        }
        o
    }

    fn publish(files: &Arc<Mutex<BTreeMap<String, Vec<u8>>>>, k: &Keys, version: &str, artifact: &[u8], base: &str) {
        let url = format!("{base}/studio-{version}.tar.gz");
        let m = manifest_for(version, "notes", &[("darwin-aarch64", &url, &b64(&sign(k, artifact)))]);
        let body = serde_json::to_vec(&m).unwrap();
        let mut f = files.lock().unwrap();
        f.insert(format!("/studio-{version}.tar.gz"), artifact.to_vec());
        f.insert("/stable.json.sig".into(), sign(k, &body).into_bytes());
        f.insert("/stable.json".into(), body);
    }

    #[test]
    fn update_n_to_n1_tamper_downgrade_and_rollback() {
        let files = Arc::new(Mutex::new(BTreeMap::new()));
        let base = serve(files.clone());
        let k = keys();
        let dir = tempfile::tempdir().unwrap();
        let u = Updater { endpoint: base.clone(), channel: "stable".into(), public_key: k.pk.clone(), dir: dir.path().into(), allow_loopback_http: true };

        // N = 1.0.0 installed, 1.0.1 published
        publish(&files, &k, "1.0.0", b"old build", &base);
        let m = u.check("studio", "0.9.0").unwrap().unwrap();
        u.apply("studio", &m, "darwin-aarch64").unwrap();
        publish(&files, &k, "1.0.1", b"new build", &base);
        let m = u.check("studio", "1.0.0").unwrap().expect("newer version offered");
        let f = u.apply("studio", &m, "darwin-aarch64").unwrap();
        assert_eq!(std::fs::read(&f).unwrap(), b"new build");
        assert!(u.check("studio", "1.0.1").unwrap().is_none(), "nothing newer");

        // manipulated manifest (notes changed after signing) is refused
        files.lock().unwrap().get_mut("/stable.json").unwrap().extend_from_slice(b" ");
        assert!(u.check("studio", "1.0.1").is_err());

        // manipulated artifact is refused, the current version stays
        publish(&files, &k, "1.0.2", b"newest", &base);
        files.lock().unwrap().insert("/studio-1.0.2.tar.gz".into(), b"evil".to_vec());
        let m = u.check("studio", "1.0.1").unwrap().unwrap();
        assert!(u.apply("studio", &m, "darwin-aarch64").is_err());
        assert_eq!(u.state().current["studio"], "1.0.1");

        // downgrade offered by the server is refused
        publish(&files, &k, "1.0.0", b"old build", &base);
        assert!(u.check("studio", "1.0.1").is_err());

        // explicit rollback to the kept version
        assert_eq!(u.rollback("studio").unwrap(), "1.0.0");
        assert_eq!(std::fs::read(u.current_file("studio").unwrap()).unwrap(), b"old build");

        // a version installed by the Tauri plugin is pinned as well
        publish(&files, &k, "1.0.5", b"plugin build", &base);
        let m = u.check("studio", "1.0.1").unwrap().unwrap();
        u.record_installed("studio", &m).unwrap();
        publish(&files, &k, "1.0.4", b"older", &base);
        assert!(u.check("studio", "1.0.5").is_err());

        // foreign key
        let other = keys();
        let u2 = Updater { public_key: other.pk, ..u };
        publish(&files, &k, "1.0.3", b"x", &base);
        assert!(u2.check("studio", "1.0.1").is_err());
    }

    #[test]
    fn only_https_outside_tests() {
        let u = Updater { endpoint: "http://example.com".into(), channel: "stable".into(), public_key: String::new(), dir: PathBuf::from("/tmp"), allow_loopback_http: true };
        assert!(u.check("studio", "1.0.0").is_err());
    }

    #[test]
    fn release_folder_roundtrip_and_tamper() {
        let k = keys();
        let d = tempfile::tempdir().unwrap();
        let art = d.path().join("OdinGuard Studio.app.tar.gz");
        std::fs::write(&art, b"bundle").unwrap();
        std::fs::write(d.path().join("OdinGuard Studio.app.tar.gz.sig"), b64(&sign(&k, b"bundle"))).unwrap();
        let out = d.path().join("stable.json");
        assert!(write_manifest("1.0.1", "n", "http://x", &[("darwin-aarch64".into(), art.clone())], &out).is_err());
        write_manifest("1.0.1", "n", "https://u.example/studio", &[("darwin-aarch64".into(), art.clone())], &out).unwrap();
        std::fs::write(d.path().join("stable.json.sig"), b64(&sign(&k, &std::fs::read(&out).unwrap()))).unwrap();
        let pubfile = b64(&format!("untrusted comment: minisign public key\n{}\n", k.pk));
        assert_eq!(verify_local(&out, &pubfile).unwrap().version, "1.0.1");
        std::fs::write(&art, b"bundle!").unwrap();
        assert!(verify_local(&out, &k.pk).is_err());
    }

    #[test]
    fn versions() {
        assert!(is_newer("1.0.10", "1.0.9"));
        assert!(is_newer("1.1.0", "1.1.0-beta.2"));
        assert!(!is_newer("1.1.0-beta.1", "1.1.0"));
    }
}

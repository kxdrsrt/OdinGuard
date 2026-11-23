//! Key files of a profile: server seed (handshake), signing seed (signature packages and manifests), build secret
//! (nonce/time challenges). 64 hex characters each; created once, never overwritten, never committed.

use std::path::Path;

use crate::{Error, Result};

pub fn read_hex32(path: &Path) -> Result<[u8; 32]> {
    let text = std::fs::read_to_string(path).map_err(|e| Error::Io(path.display().to_string(), e))?;
    let v = hex::decode(text.trim()).map_err(|_| Error::Invalid(format!("{}: 64 hex characters expected", path.display())))?;
    v.try_into().map_err(|_| Error::Invalid(format!("{}: 64 hex characters expected", path.display())))
}

/// Creates the key file with a random value if it does not exist. Returns true when it was created.
pub fn ensure(path: &Path) -> Result<bool> {
    if path.exists() {
        read_hex32(path)?;
        return Ok(false);
    }
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|e| Error::Io(dir.display().to_string(), e))?;
    }
    let v: [u8; 32] = crate::crypto::random().map_err(|e| Error::Io("random".into(), e))?;
    std::fs::write(path, format!("{}\n", hex::encode(v))).map_err(|e| Error::Io(path.display().to_string(), e))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let _ = std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o600));
    }
    Ok(true)
}

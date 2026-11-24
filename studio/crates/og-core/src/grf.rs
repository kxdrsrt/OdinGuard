//! GRF manifest helpers for the grf-rules module: the "tail hash" the guard checks at every scan (SHA-256 of the
//! 46-byte header plus everything from the file table to the end of the file; client/src/og_assets.c computes the
//! same) and the client scan (`og grf-scan`) that lists every GRF of a client's DATA.INI.

use sha2::{Digest, Sha256};
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

use crate::profile::GrfFile;
use crate::{Error, Result};

/// (file size, tail hash hex)
pub fn tail_hash(path: &Path) -> Result<(u64, String)> {
    let io = |e| Error::Io(path.display().to_string(), e);
    let mut f = std::fs::File::open(path).map_err(io)?;
    let size = f.metadata().map_err(io)?.len();
    if size < 46 {
        return Err(Error::Invalid(format!("{}: not a GRF (too small)", path.display())));
    }
    let mut h = [0u8; 46];
    f.read_exact(&mut h).map_err(io)?;
    let version = u32::from_le_bytes([h[42], h[43], h[44], h[45]]);
    let off = if version == 0x300 && h[35..38] == [0, 0, 0] {
        u64::from_le_bytes(h[30..38].try_into().unwrap_or_default())
    } else {
        u64::from(u32::from_le_bytes([h[30], h[31], h[32], h[33]]))
    };
    let mut d = Sha256::new();
    d.update(h);
    let pos = 46u64.saturating_add(off);
    if pos < size {
        f.seek(SeekFrom::Start(pos)).map_err(io)?;
        let mut buf = vec![0u8; 1 << 16];
        loop {
            let n = f.read(&mut buf).map_err(io)?;
            if n == 0 {
                break;
            }
            d.update(&buf[..n]);
        }
    }
    Ok((size, hex::encode(d.finalize())))
}

/// GRF names of a client's DATA.INI ([Data] 0=..., 1=..., in priority order).
pub fn data_ini(client: &Path) -> Result<Vec<String>> {
    let path = ["DATA.INI", "data.ini", "Data.ini"].iter().map(|n| client.join(n)).find(|p| p.is_file()).ok_or_else(|| Error::Invalid(format!("{}: no DATA.INI", client.display())))?;
    let text = String::from_utf8_lossy(&std::fs::read(&path).map_err(|e| Error::Io(path.display().to_string(), e))?).into_owned();
    let mut section = String::new();
    let mut v: Vec<(u32, String)> = Vec::new();
    for line in text.lines().map(str::trim) {
        if line.starts_with('[') {
            section = line.trim_matches(['[', ']']).to_ascii_lowercase();
        } else if section == "data"
            && let Some((k, val)) = line.split_once('=')
            && let Ok(n) = k.trim().parse::<u32>()
            && !val.trim().is_empty()
        {
            v.push((n, val.trim().to_string()));
        }
    }
    v.sort();
    Ok(v.into_iter().map(|(_, n)| n).collect())
}

/// Scans the GRFs of a client folder. `full` = also the whole-file SHA-256 for archives up to `full_max` bytes (the
/// guard re-reads those at every start in the background; larger ones rely on the tail hash, which every GRF tool
/// and patcher changes when it writes the archive).
/// The GRFs the release itself ships (odin.grf, odin-dark.grf) are skipped: the build adds their hashes to the
/// manifest, so a client that already has a release installed scans the same as one without.
pub fn scan_client_limited(client: &Path, full: bool, full_max: u64) -> Result<Vec<GrfFile>> {
    let mut out = Vec::new();
    for name in data_ini(client)? {
        let p = client.join(&name);
        if RELEASE_GRFS.iter().any(|r| r.eq_ignore_ascii_case(&name)) || !p.is_file() {
            continue;
        }
        let (size, tail) = tail_hash(&p)?;
        let full = if full && size <= full_max { crate::manifest::sha256_file(&p)?.0 } else { String::new() };
        out.push(GrfFile { name, size, tail, full, required: true });
    }
    Ok(out)
}

pub const RELEASE_GRFS: [&str; 2] = ["odin.grf", "odin-dark.grf"];

pub const FULL_HASH_MAX: u64 = 512 * 1024 * 1024;

pub fn scan_client(client: &Path, full: bool) -> Result<Vec<GrfFile>> {
    scan_client_limited(client, full, FULL_HASH_MAX)
}

/// Merges a new scan into the accepted list: new versions are added, old versions of the same name are kept when
/// `keep_previous` (patch rollout: old and new clients both pass), otherwise replaced.
pub fn merge(list: &mut Vec<GrfFile>, scan: Vec<GrfFile>, keep_previous: bool) {
    for s in scan {
        if !keep_previous {
            list.retain(|f| !f.name.eq_ignore_ascii_case(&s.name) || (f.size == s.size && f.tail == s.tail));
        }
        if !list.iter().any(|f| f.name.eq_ignore_ascii_case(&s.name) && f.size == s.size && f.tail == s.tail) {
            list.push(s);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fake_grf(table_offset: u32, body: &[u8], table: &[u8]) -> Vec<u8> {
        let mut h = vec![0u8; 46];
        h[..15].copy_from_slice(b"Master of Magic");
        h[30..34].copy_from_slice(&table_offset.to_le_bytes());
        h[42..46].copy_from_slice(&0x200u32.to_le_bytes());
        [h, body.to_vec(), table.to_vec()].concat()
    }

    #[test]
    fn tail_hash_covers_header_and_table_only() {
        let d = tempfile::tempdir().unwrap();
        let a = d.path().join("a.grf");
        std::fs::write(&a, fake_grf(4, b"AAAA", b"table")).unwrap();
        let (size, t1) = tail_hash(&a).unwrap();
        assert_eq!(size, 46 + 4 + 5);
        std::fs::write(&a, fake_grf(4, b"BBBB", b"table")).unwrap();
        assert_eq!(tail_hash(&a).unwrap().1, t1, "content outside the table is the full hash's job");
        std::fs::write(&a, fake_grf(4, b"AAAA", b"tablf")).unwrap();
        assert_ne!(tail_hash(&a).unwrap().1, t1);
    }

    #[test]
    fn scan_and_merge_keep_rollout_versions() {
        let d = tempfile::tempdir().unwrap();
        std::fs::write(d.path().join("DATA.INI"), "[Data]\r\n1=data.grf\r\n0=odin.grf\r\n2=missing.grf\r\n").unwrap();
        std::fs::write(d.path().join("odin.grf"), fake_grf(0, b"", b"t1")).unwrap();
        std::fs::write(d.path().join("data.grf"), fake_grf(0, b"", b"t2")).unwrap();
        let s = scan_client(d.path(), true).unwrap();
        assert_eq!(s.iter().map(|f| f.name.as_str()).collect::<Vec<_>>(), ["data.grf"], "release GRFs are added by the build");
        let mut list = s.clone();
        std::fs::write(d.path().join("data.grf"), fake_grf(0, b"", b"t3")).unwrap();
        merge(&mut list, scan_client(d.path(), false).unwrap(), true);
        assert_eq!(list.iter().filter(|f| f.name == "data.grf").count(), 2);
        merge(&mut list, scan_client(d.path(), false).unwrap(), false);
        assert_eq!(list.iter().filter(|f| f.name == "data.grf").count(), 1);
    }
}

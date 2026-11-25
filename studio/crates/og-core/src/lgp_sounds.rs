//! The staging folder of the LGP skill sounds (asset group `lgp-sound` of odin.grf), made from the operator's own client.
//!
//! The 2018 client plays a ground skill's sound together with its animation. LGP players do not get the animation, so the
//! server sends the sound alone (packet 0x1d3, 23 characters for the file name). The original names are longer
//! (`effect\wizard_stormgust.wav`), so every sound is copied once as `data\wav\og_<skill id>.wav`; songs are cut to the
//! same short parts the BG pack uses. The copies come from the client's GRFs on the operator's machine and are never part
//! of the repository. `assets/lgp-sound/sounds.tsv` lists skill id, original file, note and `trim_ms` (end, or
//! `start-end`; empty = whole file).

use std::path::{Path, PathBuf};

use rgrf_audio::audio::{Audio, Op};
use rgrf_core::dataini::DataIni;
use rgrf_core::{Archive, GrfPath};

use crate::{Error, Result};

#[derive(Debug, Default)]
pub struct Report {
    /// File names written below `data/wav`.
    pub written: Vec<String>,
    /// `(skill id, original file)` not found in any archive of the client.
    pub missing: Vec<(String, String)>,
    /// Archives of DATA.INI that could not be read (`name: reason`).
    pub unreadable: Vec<String>,
}

fn find_ci(dir: &Path, name: &str) -> Option<PathBuf> {
    std::fs::read_dir(dir)
        .ok()?
        .filter_map(|e| e.ok())
        .find(|e| e.file_name().to_str().is_some_and(|n| n.eq_ignore_ascii_case(name)))
        .map(|e| e.path())
}

pub fn build(client: &Path, tsv: &Path, out: &Path) -> Result<Report> {
    let io = |p: &Path| {
        let p = p.display().to_string();
        move |e| Error::Io(p, e)
    };
    let ini_path = find_ci(client, "DATA.INI")
        .ok_or_else(|| Error::Invalid(format!("{}: no DATA.INI (is this a client folder?)", client.display())))?;
    let ini = DataIni::parse(&std::fs::read(&ini_path).map_err(io(&ini_path))?);
    let table = std::fs::read_to_string(tsv).map_err(io(tsv))?;

    let mut report = Report::default();
    let mut archives: Vec<Archive> = Vec::new();
    for item in ini.items() {
        let lower = item.name.to_ascii_lowercase();
        // OdinGuard's own archives are skipped, so the sounds are never made from a build of themselves
        if lower == "odin.grf" || lower == "odin-dark.grf" {
            continue;
        }
        let Some(path) = find_ci(client, &item.name) else {
            report.unreadable.push(format!("{}: not next to DATA.INI", item.name));
            continue;
        };
        match Archive::open(&path) {
            Ok(a) => archives.push(a),
            Err(e) => report.unreadable.push(format!("{}: {e}", item.name)),
        }
    }

    let dir = out.join("data").join("wav");
    if out.exists() {
        std::fs::remove_dir_all(out).map_err(io(out))?;
    }
    std::fs::create_dir_all(&dir).map_err(io(&dir))?;

    for line in table.lines().skip(1) {
        let cols: Vec<&str> = line.trim_end_matches('\r').split('\t').collect();
        let (id, src) = (cols.first().copied().unwrap_or("").trim(), cols.get(1).copied().unwrap_or("").trim());
        if id.is_empty() || src.is_empty() {
            continue;
        }
        let trim = cols.get(3).copied().unwrap_or("").trim();
        // archive names are CP949; the table is UTF-8
        let (name_bytes, _, bad) = encoding_rs::EUC_KR.encode(src);
        if bad {
            return Err(Error::Invalid(format!("{src}: the name cannot be written in CP949")));
        }
        let mut raw = b"data\\wav\\effect\\".to_vec();
        raw.extend_from_slice(&name_bytes);
        let path = GrfPath::new(raw);
        // DATA.INI order: the first archive that has the file wins
        let data = archives.iter().find_map(|a| a.find(&path).and_then(|i| a.read(i).ok()).filter(|d| d.status.is_ok()).map(|d| d.data));
        let Some(data) = data.filter(|d| !d.is_empty()) else {
            report.missing.push((id.to_string(), src.to_string()));
            continue;
        };
        let name = format!("og_{id}.wav");
        let bytes = if trim.is_empty() || trim == "0" {
            data
        } else {
            let range = if trim.contains('-') { trim.to_string() } else { format!("0-{trim}") };
            let (mut audio, _) = Audio::decode(&data).map_err(|e| Error::Build(format!("{src}: {e}")))?;
            for op in [format!("trim:{range}"), "fadein:8".to_string(), "fadeout:60".to_string()] {
                let op = Op::parse(&op).map_err(|e| Error::Build(format!("{src}: {e}")))?;
                audio.apply(&op).map_err(|e| Error::Build(format!("{src}: {e}")))?;
            }
            audio.to_wav()
        };
        let target = dir.join(&name);
        std::fs::write(&target, bytes).map_err(io(&target))?;
        report.written.push(name);
    }
    Ok(report)
}

#[cfg(test)]
mod tests {
    use super::*;
    use rgrf_core::build::{BuildOptions, build as build_grf};
    use rgrf_core::progress::Control;

    fn wav(ms: u32) -> Vec<u8> {
        let n = 8 * ms; // 8000 Hz, 16 bit mono
        let mut v = b"RIFF".to_vec();
        v.extend((36 + 2 * n).to_le_bytes());
        v.extend(b"WAVEfmt ");
        v.extend(16u32.to_le_bytes());
        v.extend([1, 0, 1, 0]);
        v.extend(8000u32.to_le_bytes());
        v.extend(16000u32.to_le_bytes());
        v.extend([2, 0, 16, 0]);
        v.extend(b"data");
        v.extend((2 * n).to_le_bytes());
        v.extend((0..n).flat_map(|i| ((i % 100) as i16 * 100).to_le_bytes()));
        v
    }

    #[test]
    fn sounds_are_found_in_datainiorder_cut_copied_and_missing_ones_reported() {
        let dir = tempfile::tempdir().unwrap();
        let (client, src) = (dir.path().join("client"), dir.path().join("src"));
        std::fs::create_dir_all(src.join("data/wav/effect")).unwrap();
        std::fs::create_dir_all(&client).unwrap();
        std::fs::write(src.join("data/wav/effect/song.wav"), wav(2000)).unwrap();
        std::fs::write(src.join("data/wav/effect/hit.wav"), wav(500)).unwrap();
        build_grf(&src, &client.join("Data.grf"), &BuildOptions::default(), &Control::default()).unwrap();
        // an odin.grf must be ignored even if it comes first
        std::fs::write(client.join("odin.grf"), b"not a grf").unwrap();
        std::fs::write(client.join("DATA.INI"), b"[Data]\r\n0=odin.grf\r\n1=data.grf\r\n").unwrap();
        let tsv = dir.path().join("s.tsv");
        std::fs::write(&tsv, "id\tsrc\tnote\ttrim\n10\tsong.wav\t\t1000\n11\thit.wav\t\t\n12\tgone.wav\t\t\n").unwrap();

        let out = dir.path().join("out");
        let r = build(&client, &tsv, &out).unwrap();
        assert_eq!(r.written, ["og_10.wav", "og_11.wav"]);
        assert_eq!(r.missing, [("12".to_string(), "gone.wav".to_string())]);
        assert!(r.unreadable.is_empty());
        let cut = std::fs::read(out.join("data/wav/og_10.wav")).unwrap();
        assert_eq!(u32::from_le_bytes(cut[40..44].try_into().unwrap()), 2 * 8 * 1000, "cut to 1 s");
        assert_eq!(std::fs::read(out.join("data/wav/og_11.wav")).unwrap(), wav(500), "whole file copied as it is");
    }
}

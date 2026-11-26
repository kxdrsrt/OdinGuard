//! `og` – OdinGuard Anti Cheat Studio on the command line (the GUI calls the same og-core functions).

use clap::{Parser, Subcommand};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use og_core::{Result, keys, pipeline, profile::Profile, registry::Registry, rollout, sigpack, update};

#[derive(Parser)]
#[command(name = "og", version, about = "OdinGuard Anti Cheat Studio (headless)")]
struct Cli {
    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
    /// Create a profile with fresh salts and its key files (never overwrites keys)
    New { profile: PathBuf, #[arg(long)] id: String, #[arg(long)] operator: String },
    /// Create the profile's missing key files (never overwrites existing keys)
    Keys { profile: PathBuf },
    /// Check a profile against the module registry
    Validate { profile: PathBuf },
    /// Print the module and command registry as JSON
    Registry,
    /// Print a profile (migrated) as JSON
    Show { profile: PathBuf },
    /// Read the GRFs of a client folder (DATA.INI) into the profile's GRF manifest
    GrfScan { profile: PathBuf, #[arg(long)] client: PathBuf, #[arg(long)] full: bool, #[arg(long, default_value_t = 512)] full_max_mb: u64, #[arg(long)] keep_previous: bool, #[arg(long)] optional: Vec<String> },
    /// Size, tail hash and full hash of a GRF (the values of an og_grf_file / og_grf_mod line)
    GrfHash { grf: PathBuf },
    /// Make the LGP skill sounds (group lgp-sound) from the operator's own client: data/wav/og_<skill id>.wav
    LgpSounds { #[arg(long)] client: PathBuf, #[arg(long)] out: PathBuf, #[arg(long)] table: Option<PathBuf> },
    /// Allow an extra GRF (skin, darkworld variant, ...) by its tail hash, under any name
    GrfAllow { profile: PathBuf, grf: PathBuf, #[arg(long, default_value = "")] note: String },
    /// Write the public GRF manifest (accepted GRF versions, hashes only) for patchers and web panels
    GrfManifest { profile: PathBuf, #[arg(short, long)] out: Option<PathBuf> },
    /// Tool groups of the profile's signature sources (switch off or change the action with signatures.groups)
    SigGroups { profile: PathBuf, #[arg(long)] json: bool },
    /// Selectable competitive-pack folders of assets/competitive/recipe.toml (JSON)
    PackOptions,
    /// Migrate an older profile to the current schema (keeps a .bak copy)
    Migrate { profile: PathBuf },
    /// What a build would contain
    Preview { profile: PathBuf },
    /// Build the complete release of a profile
    Build { profile: PathBuf },
    /// Verify a release folder (manifest signature and every file hash)
    VerifyRelease { dir: PathBuf, #[arg(long)] key: Option<String> },
    /// Build and sign a signature package
    Sigpack { #[arg(long)] seed: PathBuf, #[arg(long = "source")] sources: Vec<PathBuf>, #[arg(long = "allow")] allow: Vec<PathBuf>, #[arg(long, default_value_t = 1)] version: u32, #[arg(long, default_value = "")] max_action: String, #[arg(long)] out: PathBuf },
    /// Verify a signature package
    SigpackVerify { pack: PathBuf, #[arg(long)] key: String },
    /// Command reference of a profile (Markdown)
    Docs { profile: PathBuf, #[arg(long, default_value = "de")] lang: String },
    /// Rollout status and gate from database exports
    Rollout { profile: PathBuf, #[arg(long)] events: PathBuf, #[arg(long)] clients: PathBuf, #[arg(long)] active: Option<usize>, #[arg(long, default_value_t = 2.0)] fp_percent: f64, #[arg(long)] advance: bool, #[arg(long)] force: bool },
    /// Write `<channel>.json` for a release folder (artifacts need their .sig next to them); sign it afterwards
    UpdateManifest { #[arg(long)] version: String, #[arg(long, default_value = "")] notes: String, #[arg(long)] base_url: String, #[arg(long = "artifact", value_parser = parse_artifact)] artifacts: Vec<(String, PathBuf)>, #[arg(long)] out: PathBuf },
    /// Offline check of a release folder: manifest signature and artifact signatures
    UpdateVerify { manifest: PathBuf, #[arg(long)] pubkey: String },
    /// Check an update channel (Studio, core or signatures) and optionally install
    Update { #[arg(long)] endpoint: String, #[arg(long, default_value = "stable")] channel: String, #[arg(long)] pubkey: String, #[arg(long, default_value = "studio")] key: String, #[arg(long)] current: String, #[arg(long)] target: String, #[arg(long)] dir: PathBuf, #[arg(long)] install: bool, #[arg(long)] rollback: bool },
}

fn parse_artifact(s: &str) -> std::result::Result<(String, PathBuf), String> {
    s.split_once('=').map(|(t, f)| (t.to_string(), PathBuf::from(f))).ok_or_else(|| "expected <target>=<file>".into())
}

fn read_pubkey(arg: &str) -> String {
    std::fs::read_to_string(arg).unwrap_or_else(|_| arg.to_string())
}

fn reg() -> Result<Registry> {
    Registry::load(&og_core::repo_root().join("modules"))
}

fn print<T: serde::Serialize>(v: &T) {
    println!("{}", serde_json::to_string_pretty(v).unwrap_or_default());
}

fn base(p: &Path) -> PathBuf {
    p.parent().unwrap_or(Path::new(".")).to_path_buf()
}

fn run(cli: Cli) -> Result<()> {
    match cli.cmd {
        Cmd::New { profile, id, operator } => {
            let p = Profile::new(&id, &operator)?;
            if profile.exists() {
                return Err(og_core::Error::Invalid(format!("{} exists", profile.display())));
            }
            for k in [&p.keys.server_seed, &p.keys.signing_seed, &p.keys.build_secret] {
                let path = p.resolve(&base(&profile), k);
                println!("{} {}", if keys::ensure(&path)? { "created" } else { "kept" }, path.display());
            }
            p.save(&profile)?;
            println!("profile {} written", profile.display());
        }
        Cmd::Keys { profile } => {
            let p = Profile::load(&profile)?;
            for k in [&p.keys.server_seed, &p.keys.signing_seed, &p.keys.build_secret] {
                let path = p.resolve(&base(&profile), k);
                println!("{} {}", if keys::ensure(&path)? { "created" } else { "kept" }, path.display());
            }
        }
        Cmd::Validate { profile } => {
            let w = Profile::load(&profile)?.validate(&reg()?)?;
            w.iter().for_each(|w| println!("warning: {w}"));
            println!("ok");
        }
        Cmd::Registry => print(&reg()?),
        Cmd::GrfScan { profile, client, full, full_max_mb, keep_previous, optional } => {
            let mut p = Profile::load(&profile)?;
            let mut scan = og_core::grf::scan_client_limited(&client, full, full_max_mb * 1024 * 1024)?;
            for f in &mut scan {
                f.required = !optional.iter().any(|o| o.eq_ignore_ascii_case(&f.name));
                println!("{} {} bytes tail {}{}{}", f.name, f.size, &f.tail[..16], if f.full.is_empty() { "" } else { " + full hash" }, if f.required { "" } else { " (optional)" });
            }
            og_core::grf::merge(&mut p.grf.files, scan, keep_previous);
            p.save(&profile)?;
            println!("{} accepted GRF version(s) in the profile; rebuild and deploy the server config before the patch", p.grf.files.len());
        }
        Cmd::GrfHash { grf } => {
            let (size, tail) = og_core::grf::tail_hash(&grf)?;
            println!("{size} {tail} {}", og_core::manifest::sha256_file(&grf)?.0);
        }
        Cmd::LgpSounds { client, out, table } => {
            let table = table.unwrap_or_else(|| og_core::repo_root().join("assets/lgp-sound/sounds.tsv"));
            let r = og_core::lgp_sounds::build(&client, &table, &out)?;
            r.unreadable.iter().for_each(|u| eprintln!("warning: archive skipped, {u}"));
            r.missing.iter().for_each(|(id, src)| eprintln!("missing in the client: {src} (skill {id})"));
            println!("done: {} sounds in {}", r.written.len(), out.display());
        }
        Cmd::GrfManifest { profile, out } => {
            let p = Profile::load(&profile)?;
            let text = serde_json::to_string_pretty(&og_core::grf::public_manifest(&p, &[])).unwrap_or_default() + "\n";
            match out {
                Some(o) => {
                    std::fs::write(&o, text).map_err(|e| og_core::Error::Io(o.display().to_string(), e))?;
                    println!("{} GRF version(s) written to {}", p.grf.files.len(), o.display());
                }
                None => print!("{text}"),
            }
        }
        Cmd::GrfAllow { profile, grf, note } => {
            let mut p = Profile::load(&profile)?;
            let (_, tail) = og_core::grf::tail_hash(&grf)?;
            let note = if note.is_empty() { grf.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default() } else { note };
            if !p.grf.mods.iter().any(|m| m.tail == tail) {
                p.grf.mods.push(og_core::profile::GrfMod { tail, note: note.replace([',', '|'], " ") });
            }
            p.save(&profile)?;
            println!("allowed: {note}");
        }
        Cmd::PackOptions => print(&og_core::pack::options(&og_core::repo_root().join("assets/competitive/recipe.toml"))?),
        Cmd::SigGroups { profile, json } => {
            let p = Profile::load(&profile)?;
            let b = base(&profile);
            let src: Vec<PathBuf> = p.signatures.sources.iter().map(|s| p.resolve(&b, s)).collect();
            let al: Vec<PathBuf> = p.signatures.allowlist.iter().map(|s| p.resolve(&b, s)).collect();
            let groups = sigpack::catalog(&src.iter().map(PathBuf::as_path).collect::<Vec<_>>(), &al.iter().map(PathBuf::as_path).collect::<Vec<_>>())?;
            if json {
                print(&groups);
                return Ok(());
            }
            for g in groups {
                let set = p.signatures.groups.get(&g.group).cloned().unwrap_or_else(|| "default".into());
                println!("{:<10} {:<5} {:>4} entries ({:>3} client) kinds {:<40} actions {:<20} profile: {set}", g.group, if g.allow { "allow" } else { "sig" }, g.count, g.active, g.kinds.join(","), g.actions.join(","));
            }
        }
        Cmd::Show { profile } => print(&Profile::load(&profile)?),
        Cmd::Migrate { profile } => {
            let text = std::fs::read_to_string(&profile).map_err(|e| og_core::Error::Io(profile.display().to_string(), e))?;
            let p = Profile::load(&profile)?;
            std::fs::write(profile.with_extension("toml.bak"), text).map_err(|e| og_core::Error::Io("backup".into(), e))?;
            p.save(&profile)?;
            println!("migrated to schema {}", og_core::profile::SCHEMA_VERSION);
        }
        Cmd::Preview { profile } => print(&pipeline::preview(&Profile::load(&profile)?, &reg()?)?),
        Cmd::Build { profile } => print(&pipeline::build(&profile, &og_core::repo_root())?),
        Cmd::VerifyRelease { dir, key } => {
            let k = key.and_then(|k| hex::decode(k).ok().and_then(|v| v.try_into().ok()));
            let m = og_core::manifest::verify_release(&dir, k.as_ref())?;
            println!("release ok: profile {}, {} files", m.profile, m.files.len());
        }
        Cmd::Sigpack { seed, sources, allow, version, max_action, out } => {
            let s = keys::read_hex32(&seed)?;
            let (pack, rep) = sigpack::build(&sources.iter().map(PathBuf::as_path).collect::<Vec<_>>(), &allow.iter().map(PathBuf::as_path).collect::<Vec<_>>(), version, &max_action, &s)?;
            std::fs::write(&out, pack).map_err(|e| og_core::Error::Io(out.display().to_string(), e))?;
            print(&rep);
            println!("public key {}", hex::encode(og_core::crypto::public_key(&s)));
        }
        Cmd::SigpackVerify { pack, key } => {
            let data = std::fs::read(&pack).map_err(|e| og_core::Error::Io(pack.display().to_string(), e))?;
            let k: [u8; 32] = hex::decode(key).ok().and_then(|v| v.try_into().ok()).ok_or_else(|| og_core::Error::Invalid("key: 64 hex".into()))?;
            let (v, e) = sigpack::verify(&data, &k)?;
            println!("signature ok, version {v}, {} entries", e.len());
        }
        Cmd::Docs { profile, lang } => print!("{}", og_core::generate::commands_md(&Profile::load(&profile)?, &reg()?, &lang)),
        Cmd::Rollout { profile, events, clients, active, fp_percent, advance, force } => {
            let mut p = Profile::load(&profile)?;
            let s = rollout::status(&p, &events, &clients, active, fp_percent)?;
            print(&s);
            if advance {
                let stage = rollout::advance(&mut p, &s, force)?;
                p.save(&profile)?;
                println!("profile is now at stage {stage}; rebuild and deploy the server config");
            }
        }
        Cmd::UpdateManifest { version, notes, base_url, artifacts, out } => {
            let m = update::write_manifest(&version, &notes, &base_url, &artifacts, &out)?;
            println!("{} written: {} {} platform(s)", out.display(), m.version, m.platforms.len());
        }
        Cmd::UpdateVerify { manifest, pubkey } => {
            let m = update::verify_local(&manifest, &read_pubkey(&pubkey))?;
            println!("ok: {} {}, {} platform(s), manifest and artifact signatures valid", manifest.display(), m.version, m.platforms.len());
        }
        Cmd::Update { endpoint, channel, pubkey, key, current, target, dir, install, rollback } => {
            let pk = update::minisign_key_line(&read_pubkey(&pubkey)).unwrap_or(pubkey);
            let u = update::Updater { endpoint, channel, public_key: pk, dir, allow_loopback_http: false };
            if rollback {
                println!("rolled back to {}", u.rollback(&key)?);
            } else {
                match u.check(&key, &current)? {
                    Some(m) if install => println!("installed {} -> {}", m.version, u.apply(&key, &m, &target)?.display()),
                    Some(m) => println!("update available: {}\n{}", m.version, m.notes),
                    None => println!("up to date"),
                }
            }
        }
    }
    Ok(())
}

fn main() -> ExitCode {
    match run(Cli::parse()) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::FAILURE
        }
    }
}

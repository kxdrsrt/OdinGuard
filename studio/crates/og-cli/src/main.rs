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
    /// Check a profile against the module registry
    Validate { profile: PathBuf },
    /// Print the module, command and aura registry as JSON
    Registry,
    /// Print a profile (migrated) as JSON
    Show { profile: PathBuf },
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
    /// Check an update channel (Studio, core or signatures) and optionally install
    Update { #[arg(long)] endpoint: String, #[arg(long, default_value = "stable")] channel: String, #[arg(long)] pubkey: String, #[arg(long, default_value = "studio")] key: String, #[arg(long)] current: String, #[arg(long)] target: String, #[arg(long)] dir: PathBuf, #[arg(long)] install: bool, #[arg(long)] rollback: bool },
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
        Cmd::Validate { profile } => {
            let w = Profile::load(&profile)?.validate(&reg()?)?;
            w.iter().for_each(|w| println!("warning: {w}"));
            println!("ok");
        }
        Cmd::Registry => print(&reg()?),
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
        Cmd::Update { endpoint, channel, pubkey, key, current, target, dir, install, rollback } => {
            let u = update::Updater { endpoint, channel, public_key: pubkey, dir, allow_loopback_http: false };
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

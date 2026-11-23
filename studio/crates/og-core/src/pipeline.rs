//! Release build of a profile. Layout of the output folder:
//!   client/     odinguard.dll, ijl15.dll (loader), odin.grf, [odin-dark.grf], odinguard.ini, README.txt, licences
//!   patcher/    filelist.txt (path, sha256, size) for the operator's patcher
//!   server/     src/ (module), conf/ (odinguard_athena.conf, import/odinguard_conf.txt, odinguard/ references), sql/
//!   secrets/    odinguard.env (server key, protocol salt, build secret) – never shipped, excluded from the manifest
//!   docs/       handbook.md, commands-de.md, commands-en.md, build-report.json
//!   manifest.json, manifest.sig, signing.pub

use serde::Serialize;
use std::path::{Path, PathBuf};
use std::process::Command;

use crate::generate::{self, Derived};
use crate::profile::Profile;
use crate::registry::{Aura, Registry};
use crate::{Error, Result, keys};

#[derive(Debug, Default, Serialize)]
pub struct BuildReport {
    pub profile: String,
    pub out: String,
    pub modules: Vec<String>,
    pub client_sources: Vec<String>,
    pub linked_objects: Vec<String>,
    pub guard_sha256: String,
    pub loader_sha256: String,
    pub odin_grf_sha256: String,
    pub odin_grf_files: usize,
    pub competitive_pack_sha256: String,
    pub sigpack: Option<crate::sigpack::Report>,
    pub warnings: Vec<String>,
}

/// What a build would do (Studio preview), without writing anything.
#[derive(Debug, Serialize)]
pub struct Preview {
    pub modules: Vec<String>,
    pub client_sources: Vec<String>,
    pub server_switches: Vec<(String, bool)>,
    pub odin_grf_groups: Vec<String>,
    pub protected_files: Vec<String>,
    pub commands: Vec<String>,
    pub warnings: Vec<String>,
}

pub fn preview(p: &Profile, reg: &Registry) -> Result<Preview> {
    let warnings = p.validate(reg)?;
    Ok(Preview {
        modules: p.modules.clone(),
        client_sources: generate::client_sources(p, reg),
        server_switches: reg.modules.iter().filter_map(|m| m.server_key.clone().map(|k| (k, p.modules.contains(&m.id)))).collect(),
        odin_grf_groups: asset_groups(p, reg),
        protected_files: generate::protected_files(p).iter().map(|(f, _)| f.to_string()).collect(),
        commands: reg
            .commands
            .iter()
            .filter(|c| p.modules.contains(&c.module) && p.commands.get(&c.id).is_none_or(|x| x.enabled))
            .map(|c| format!("{}{}", if c.kind == "client" { "!" } else { "@" }, p.commands.get(&c.id).and_then(|x| x.name.clone()).unwrap_or_else(|| c.name.clone())))
            .collect(),
        warnings,
    })
}

fn asset_groups(p: &Profile, reg: &Registry) -> Vec<String> {
    let mut g: Vec<String> = reg.modules.iter().filter(|m| p.modules.contains(&m.id)).flat_map(|m| m.assets.iter().cloned()).filter(|a| a != "competitive").collect();
    g.sort();
    g.dedup();
    g
}

fn io<T>(r: std::io::Result<T>, what: &Path) -> Result<T> {
    r.map_err(|e| Error::Io(what.display().to_string(), e))
}

fn copy(from: &Path, to: &Path) -> Result<()> {
    if let Some(d) = to.parent() {
        io(std::fs::create_dir_all(d), d)?;
    }
    io(std::fs::copy(from, to), from).map(|_| ())
}

fn copy_tree(from: &Path, to: &Path) -> Result<usize> {
    let mut n = 0;
    for e in io(std::fs::read_dir(from), from)?.flatten() {
        let p = e.path();
        let t = to.join(e.file_name());
        if p.is_dir() {
            n += copy_tree(&p, &t)?;
        } else {
            copy(&p, &t)?;
            n += 1;
        }
    }
    Ok(n)
}

/// Aura catalog of the profile, checked: unique names, known rarity, effect files present in odin.grf assets when they
/// come from there (client files are checked with tools/check_auras.py against the operator's client).
pub fn aura_catalog(p: &Profile, reg: &Registry, base: &Path) -> Result<Vec<Aura>> {
    let list: Vec<Aura> = if p.aura_catalog.is_empty() {
        reg.auras.clone()
    } else {
        let path = p.resolve(base, &p.aura_catalog);
        #[derive(serde::Deserialize)]
        struct F {
            aura: Vec<Aura>,
        }
        let text = io(std::fs::read_to_string(&path), &path)?;
        toml::from_str::<F>(&text).map_err(|e| Error::Parse(path.display().to_string(), e.to_string()))?.aura
    };
    let mut seen = std::collections::HashSet::new();
    for a in &list {
        if !seen.insert(a.name.clone()) || a.name.is_empty() || !a.name.chars().all(|c| c.is_ascii_alphanumeric()) {
            return Err(Error::Invalid(format!("aura '{}': names must be unique letters/digits", a.name)));
        }
        if !crate::registry::RARITIES.contains(&a.rarity.as_str()) || a.effect <= 0 {
            return Err(Error::Invalid(format!("aura {}: rarity or effect id", a.name)));
        }
        let ok_unlock = a.unlock == "gm"
            || ["quest:", "event:"].iter().any(|x| a.unlock.starts_with(x) && a.unlock.len() > x.len())
            || ["mvp:", "bg_wins:", "woe_castles:"].iter().any(|x| a.unlock.strip_prefix(x).is_some_and(|n| n.parse::<u32>().is_ok_and(|n| n > 0)));
        if !ok_unlock {
            return Err(Error::Invalid(format!("aura {}: unknown unlock '{}'", a.name, a.unlock)));
        }
    }
    Ok(list)
}

fn sha(path: &Path) -> Result<String> {
    Ok(crate::manifest::sha256_file(path)?.0)
}

/// Builds the release of a profile. `profile_path` locates relative paths; `root` is the OdinGuard repository.
pub fn build(profile_path: &Path, root: &Path) -> Result<BuildReport> {
    let base = profile_path.parent().unwrap_or(Path::new(".")).to_path_buf();
    let p = Profile::load(profile_path)?;
    let reg = Registry::load(&root.join("modules"))?;
    let mut rep = BuildReport { profile: p.general.id.clone(), warnings: p.validate(&reg)?, modules: p.modules.clone(), ..Default::default() };
    let out = p.resolve(&base, &p.output);
    rep.out = out.display().to_string();
    if out.exists() {
        io(std::fs::remove_dir_all(&out), &out)?;
    }
    let server_seed = keys::read_hex32(&p.resolve(&base, &p.keys.server_seed))?;
    let signing_seed = keys::read_hex32(&p.resolve(&base, &p.keys.signing_seed))?;
    let build_secret = keys::read_hex32(&p.resolve(&base, &p.keys.build_secret))?;
    let d = Derived::new(&p, server_seed, build_secret)?;

    // ---- client DLL
    // GNU make cannot handle spaces in paths: build in a fixed space-free work folder in that case
    let work = if std::path::absolute(&out).unwrap_or_else(|_| out.clone()).to_string_lossy().contains(' ') { std::env::temp_dir().join("odinguard-build").join(&p.general.id) } else { out.join("build") };
    if work.exists() {
        io(std::fs::remove_dir_all(&work), &work)?;
    }
    let gendir = work.join("gen");
    let obj = work.join("client");
    generate::write(&gendir.join("og_modules.h"), &generate::modules_h(&p, &reg))?;
    generate::write(&gendir.join("og_modules.mk"), &generate::modules_mk(&p, &reg))?;
    generate::write(&gendir.join("og_profile.h"), &generate::profile_h(&p, &d))?;
    generate::write(&gendir.join("res/odinguard.rc"), &generate::resource_rc(&p))?;
    copy(&root.join("client/res/odinguard.ico"), &gendir.join("res/odinguard.ico"))?;
    copy(&root.join("client/res/splash.bmp"), &gendir.join("res/splash.bmp"))?;
    rep.client_sources = generate::client_sources(&p, &reg);
    let abs = |x: &Path| x.canonicalize().unwrap_or_else(|_| x.to_path_buf());
    io(std::fs::create_dir_all(&obj), &obj)?;
    let status = Command::new("make")
        .arg("-C")
        .arg(root.join("client"))
        .arg(format!("PROFILE_DIR={}", abs(&gendir).display()))
        .arg(format!("OUT={}", abs(&obj).display()))
        .arg(format!("RC_DIR={}", abs(&gendir.join("res")).display()))
        .arg(abs(&obj).join("odinguard.dll"))
        .arg(abs(&obj).join("ijl15.dll"))
        .output()
        .map_err(|e| Error::Build(format!("make: {e} (needs mingw-w64)")))?;
    if !status.status.success() {
        return Err(Error::Build(format!("client build failed:\n{}", String::from_utf8_lossy(&status.stderr))));
    }
    rep.linked_objects = linked_objects(&obj.join("odinguard.map"))?;
    check_linked(&rep.client_sources, &rep.linked_objects)?;
    let client = out.join("client");
    copy(&obj.join("odinguard.dll"), &client.join("odinguard.dll"))?;
    copy(&obj.join("ijl15.dll"), &client.join("ijl15.dll"))?;
    copy(&root.join("client/package/odinguard.ini"), &client.join("odinguard.ini"))?;
    let readme = std::fs::read_to_string(root.join("client/package/README.txt")).map_err(|e| Error::Io("client/package/README.txt".into(), e))?;
    generate::write(&client.join("README.txt"), &readme.replace("{operator}", &p.general.operator).replace('\n', "\r\n"))?;
    copy(&root.join("third_party/LICENCE-monocypher.md"), &client.join("LICENSE-monocypher.md"))?;
    copy(&root.join("third_party/minhook/LICENSE.txt"), &client.join("LICENSE-minhook.txt"))?;
    rep.guard_sha256 = sha(&client.join("odinguard.dll"))?;
    rep.loader_sha256 = sha(&client.join("ijl15.dll"))?;

    // ---- odin.grf (assets only)
    let stage = out.join("build/odin-grf");
    generate::write(
        &stage.join("data/odinguard/profile.txt"),
        &format!("OdinGuard Anti Cheat {}\nprofile {}\nassets {}\n", env!("CARGO_PKG_VERSION"), p.general.id, asset_groups(&p, &reg).join(",")),
    )?;
    for g in asset_groups(&p, &reg) {
        if let Some(dir) = p.assets.odin_grf.get(&g) {
            let dir = p.resolve(&base, dir);
            let data = dir.join("data");
            if !data.is_dir() {
                return Err(Error::Invalid(format!("asset group {g}: {} has no data/ folder", dir.display())));
            }
            copy_tree(&data, &stage.join("data"))?;
        } else {
            rep.warnings.push(format!("asset group {g} has no folder in assets.odin_grf"));
        }
    }
    let grf = client.join("odin.grf");
    let ctl = rgrf_core::progress::Control::default();
    let br = rgrf_core::build::build(&stage, &grf, &rgrf_core::build::BuildOptions::default(), &ctl).map_err(|e| Error::Build(format!("odin.grf: {e}")))?;
    let archive = rgrf_core::Archive::open(&grf).map_err(|e| Error::Build(format!("odin.grf: {e}")))?;
    let vr = rgrf_core::verify::verify(&archive, rgrf_core::verify::VerifyOptions::default(), &ctl).map_err(|e| Error::Build(format!("odin.grf verify: {e}")))?;
    if vr.issues.iter().any(|i| matches!(i.severity, rgrf_core::verify::Severity::Error)) {
        return Err(Error::Build("odin.grf failed verification".into()));
    }
    rep.odin_grf_files = br.files;
    rep.odin_grf_sha256 = sha(&grf)?;

    // ---- competitive pack
    if p.modules.iter().any(|m| m == "competitive-pack") {
        let src = p.resolve(&base, &p.assets.competitive_pack);
        let a = rgrf_core::Archive::open(&src).map_err(|e| Error::Build(format!("competitive pack {}: {e}", src.display())))?;
        let v = rgrf_core::verify::verify(&a, rgrf_core::verify::VerifyOptions::default(), &ctl).map_err(|e| Error::Build(format!("competitive pack: {e}")))?;
        if v.issues.iter().any(|i| matches!(i.severity, rgrf_core::verify::Severity::Error)) {
            return Err(Error::Build("competitive pack failed verification".into()));
        }
        copy(&src, &client.join("odin-dark.grf"))?;
        rep.competitive_pack_sha256 = sha(&client.join("odin-dark.grf"))?;
    }

    // ---- server package
    let server = out.join("server");
    copy_tree(&root.join("server/src"), &server.join("src"))?;
    copy_tree(&root.join("server/sql"), &server.join("sql"))?;
    copy(&root.join("server/conf/odinguard_athena.conf"), &server.join("conf/odinguard_athena.conf"))?;
    let refdir = server.join("conf/odinguard");
    copy(&client.join("odinguard.dll"), &refdir.join("odinguard.dll"))?;
    copy(&grf, &refdir.join("odin.grf"))?;
    if client.join("odin-dark.grf").exists() {
        copy(&client.join("odin-dark.grf"), &refdir.join("odin-dark.grf"))?;
    }
    if p.modules.iter().any(|m| m == "cheat-signatures") {
        let srcs: Vec<PathBuf> = p.signatures.sources.iter().map(|s| p.resolve(&base, s)).collect();
        let allow: Vec<PathBuf> = p.signatures.allowlist.iter().map(|s| p.resolve(&base, s)).collect();
        let (pack, sr) = crate::sigpack::build(
            &srcs.iter().map(PathBuf::as_path).collect::<Vec<_>>(),
            &allow.iter().map(PathBuf::as_path).collect::<Vec<_>>(),
            p.signatures.version,
            &p.signatures.max_action,
            &signing_seed,
        )?;
        io(std::fs::write(refdir.join("odinguard.sigpack"), pack), &refdir)?;
        rep.sigpack = Some(sr);
    }
    let auras = aura_catalog(&p, &reg, &base)?;
    let signing_pub = crate::crypto::public_key(&signing_seed);
    generate::write(&server.join("conf/import/odinguard_conf.txt"), &generate::server_conf(&p, &reg, &signing_pub, &auras, "conf/odinguard"))?;
    generate::write(&out.join("secrets/odinguard.env"), &generate::secrets_env(&d))?;

    // ---- patcher list, docs, manifest
    let files = crate::manifest::collect(&client, &[])?;
    let list: String = files.iter().map(|f| format!("{}\t{}\t{}\n", f.path, f.sha256, f.size)).collect();
    generate::write(&out.join("patcher/filelist.txt"), &list)?;
    generate::write(&out.join("docs/commands-de.md"), &generate::commands_md(&p, &reg, "de"))?;
    generate::write(&out.join("docs/commands-en.md"), &generate::commands_md(&p, &reg, "en"))?;
    generate::write(&out.join("docs/handbook.md"), &handbook(&p, &reg, &rep, &d))?;
    generate::write(&out.join("docs/build-report.json"), &serde_json::to_string_pretty(&rep).unwrap_or_default())?;
    io(std::fs::remove_dir_all(out.join("build")), &out)?;
    let m = crate::manifest::Manifest {
        product: "OdinGuard Anti Cheat".into(),
        version: env!("CARGO_PKG_VERSION").into(),
        protocol: 2,
        profile: p.general.id.clone(),
        rollout_stage: p.rollout.stage,
        sigpack_version: p.signatures.version,
        files: crate::manifest::collect(&out, &["secrets", "manifest.json", "manifest.sig", "signing.pub"])?,
    };
    crate::manifest::write_signed(&out, &m, &signing_seed)?;
    Ok(rep)
}

/// Module objects named in the linker map (client/Makefile writes obj/<source>.o per module source).
pub fn linked_objects(map: &Path) -> Result<Vec<String>> {
    let text = io(std::fs::read_to_string(map), map)?;
    let mut v: Vec<String> = text
        .split(|c: char| c.is_whitespace() || c == '(' || c == ')')
        .filter(|w| w.ends_with(".o") && w.rsplit('/').nth(1) == Some("obj"))
        .filter_map(|w| w.rsplit('/').next().map(|f| f.trim_end_matches(".o").to_string()))
        .collect();
    v.sort();
    v.dedup();
    Ok(v)
}

/// The DLL must contain exactly the module sources of the profile (proof that unselected code is not linked).
pub fn check_linked(sources: &[String], linked: &[String]) -> Result<()> {
    let mut want: Vec<String> = sources.iter().map(|s| s.rsplit('/').next().unwrap_or(s).trim_end_matches(".c").to_string()).collect();
    want.sort();
    if want != linked {
        return Err(Error::Build(format!("linked objects {linked:?} differ from the selected module sources {want:?}")));
    }
    Ok(())
}

fn handbook(p: &Profile, reg: &Registry, rep: &BuildReport, d: &Derived) -> String {
    use std::fmt::Write as _;
    let mut s = format!(
        "# OdinGuard Anti Cheat – operator handbook ({})\n\nGenerated by the OdinGuard Studio {} for profile `{}` (rollout stage {}).\n\n",
        p.general.operator,
        env!("CARGO_PKG_VERSION"),
        p.general.id,
        p.rollout.stage
    );
    s.push_str("## 1. What is in this release\n\n| Folder | Content |\n|---|---|\n| `client/` | files for the game client folder (patcher) |\n| `server/` | server module, config, references, SQL |\n| `secrets/odinguard.env` | server secrets – environment of the servers only, never in git, never to players |\n| `patcher/filelist.txt` | path, SHA-256, size for the patcher |\n| `manifest.json` + `manifest.sig` | signed list of every file (`og verify-release <folder>`) |\n\n");
    let _ = writeln!(s, "Guard `odinguard.dll` SHA-256 `{}`, loader `{}`, odin.grf `{}` ({} files).\n", rep.guard_sha256, rep.loader_sha256, rep.odin_grf_sha256, rep.odin_grf_files);
    let _ = writeln!(s, "Server public key `{}`, profile tag `{}`.\n", hex::encode(d.server_pub), hex::encode(d.tag));
    s.push_str("## 2. Client integration (no player uninstall; only through your patcher)\n\n1. Rename the client's existing `ijl15.dll` (e.g. ROExt) to `ijl15r.dll`; ship `client/ijl15.dll` (loader) and `client/odinguard.dll`.\n2. Ship `odin.grf` and add it to `DATA.INI` **before** `data.grf` (lower number = higher priority), e.g. `0=odin.grf`");
    if rep.competitive_pack_sha256.is_empty() {
        s.push_str(".\n");
    } else {
        s.push_str(", `1=odin-dark.grf` (optional for players; modified copies are refused).\n");
    }
    s.push_str("3. Optional `odinguard.ini` (splash time, ports). Protection switches cannot be changed there.\n4. Deliver everything with the patcher (`patcher/filelist.txt`). Do not change the files: the server checks the guard's own code and the protected files.\n\n");
    s.push_str("## 3. Server integration\n\n1. Copy `server/src/*` into the emulator (see `server/README.md` of the OdinGuard repository for the hook lines) and build.\n2. Copy `server/conf/odinguard_athena.conf`, `server/conf/import/odinguard_conf.txt` and `server/conf/odinguard/` into the server's `conf/`.\n3. Put the three values of `secrets/odinguard.env` into the server environment (Docker/Easypanel) or into `conf/import/odinguard_conf.txt` as `og_key`, `og_proto_salt`, `og_build_secret` (not in git).\n4. The tables are created at the first start.\n\n");
    s.push_str("## 4. Rollout\n\n| Stage | Config | Leave it when |\n|---|---|---|\n| 1 warn | `og_log_only: yes` | a week of logs, `og rollout status` shows no rule above the false-alarm threshold |\n| 2 kick | `og_log_only: no` | kicks are only real cheats (check `og_event`, `@ogflags`) |\n| 3 patcher | stage 2 + every player gets the guard by the patcher | `og rollout status` reports >= 98 % guarded sessions |\n| 4 required | `og_required: yes` | – |\n\nAdvance with `og rollout advance <profile> --events og_event.csv --clients og_client.csv`; the Studio refuses stage 4 when the gate fails.\n\n");
    s.push_str("## 5. Modules\n\n| Module | Side | What it does |\n|---|---|---|\n");
    for m in reg.modules.iter().filter(|m| p.modules.contains(&m.id)) {
        let _ = writeln!(s, "| {} | {} | {} |", m.id, m.side, m.summary);
    }
    s.push_str("\n## 6. Commands\n\nSee `docs/commands-en.md` / `docs/commands-de.md`.\n\n## 7. Honest limits\n\nThe server cannot prove what a client runs. Encryption, challenges and server-side checks (rate limit, behaviour, honeypot, delays) are the real security; client checks (signatures, anti-debug, integrity) raise the cost of cheating and are reported, never trusted. Keep warn mode until the logs are clean.\n");
    s
}

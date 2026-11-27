//! OdinGuard Studio backend: Tauri commands over og-core. The GUI never does anything og-core (and the `og` CLI)
//! cannot do; builds, signatures and updates use exactly the same code.

use std::path::{Path, PathBuf};

use og_core::{pipeline, profile::Profile, registry::Registry, rollout};
use serde::Serialize;

type Res<T> = Result<T, String>;

fn err<E: std::fmt::Display>(e: E) -> String {
    e.to_string()
}

fn registry() -> Res<Registry> {
    Registry::load(&og_core::repo_root().join("modules")).map_err(err)
}

#[derive(Serialize)]
struct Loaded {
    path: String,
    profile: Profile,
    warnings: Vec<String>,
    error: Option<String>,
}

#[tauri::command]
fn get_registry() -> Res<Registry> {
    registry()
}

#[tauri::command]
fn load_profile(path: String) -> Res<Loaded> {
    let p = Profile::load(Path::new(&path)).map_err(err)?;
    let reg = registry()?;
    let (warnings, error) = match p.validate(&reg) {
        Ok(w) => (w, None),
        Err(e) => (vec![], Some(e.to_string())),
    };
    Ok(Loaded { path, profile: p, warnings, error })
}

/// Validates without saving: returns warnings or the first error.
#[tauri::command]
fn validate_profile(profile: Profile) -> Res<Vec<String>> {
    profile.validate(&registry()?).map_err(err)
}

#[tauri::command]
fn save_profile(path: String, profile: Profile) -> Res<Vec<String>> {
    let warnings = profile.validate(&registry()?).map_err(err)?;
    profile.save(Path::new(&path)).map_err(err)?;
    Ok(warnings)
}

/// New profile with fresh salts; the key files are created next to it (never overwritten).
#[tauri::command]
fn new_profile(path: String, id: String, operator: String) -> Res<Loaded> {
    let target = PathBuf::from(&path);
    if target.exists() {
        return Err(format!("{path} exists"));
    }
    let p = Profile::new(&id, &operator).map_err(err)?;
    let base = target.parent().unwrap_or(Path::new(".")).to_path_buf();
    for k in [&p.keys.server_seed, &p.keys.signing_seed, &p.keys.build_secret] {
        og_core::keys::ensure(&p.resolve(&base, k)).map_err(err)?;
    }
    p.save(&target).map_err(err)?;
    load_profile(path)
}

#[tauri::command]
fn preview(profile: Profile) -> Res<pipeline::Preview> {
    pipeline::preview(&profile, &registry()?).map_err(err)
}

#[tauri::command]
async fn build(path: String) -> Res<pipeline::BuildReport> {
    tauri::async_runtime::spawn_blocking(move || pipeline::build(Path::new(&path), &og_core::repo_root()).map_err(err))
        .await
        .map_err(err)?
}

#[tauri::command]
fn verify_release(dir: String) -> Res<String> {
    og_core::manifest::verify_release(Path::new(&dir), None).map(|m| format!("release ok: profile {}, {} files, signature valid", m.profile, m.files.len())).map_err(err)
}

#[tauri::command]
fn commands_md(profile: Profile, lang: String) -> Res<String> {
    Ok(og_core::generate::commands_md(&profile, &registry()?, &lang))
}

#[tauri::command]
fn rollout_status(path: String, events: String, clients: String, active: Option<usize>, fp_percent: f64) -> Res<rollout::Status> {
    let p = Profile::load(Path::new(&path)).map_err(err)?;
    rollout::status(&p, Path::new(&events), Path::new(&clients), active, fp_percent).map_err(err)
}

#[tauri::command]
fn rollout_advance(path: String, events: String, clients: String, active: Option<usize>, fp_percent: f64) -> Res<u8> {
    let mut p = Profile::load(Path::new(&path)).map_err(err)?;
    let s = rollout::status(&p, Path::new(&events), Path::new(&clients), active, fp_percent).map_err(err)?;
    let stage = rollout::advance(&mut p, &s, false).map_err(err)?;
    p.save(Path::new(&path)).map_err(err)?;
    Ok(stage)
}

/// Keeps the release that is built now (guard, odin.grf, odin-dark.grf) as an accepted previous release, so players who
/// have not patched yet still pass the guard and file checks after the next build (`og keep-previous`).
#[tauri::command]
fn keep_previous_release(path: String) -> Res<og_core::pipeline::KeptRelease> {
    og_core::pipeline::keep_previous(Path::new(&path), None).map_err(err)
}

/// Update endpoint base; `<base>/<channel>.json` plus `<channel>.json.sig`. Default: GitHub Releases of the
/// OdinGuard repository (stable = the latest release, beta = the release tagged "beta"). Overridable for
/// staging/tests and self-hosting.
fn update_base(channel: &str) -> String {
    std::env::var("OG_STUDIO_UPDATE_ENDPOINT").unwrap_or_else(|_| {
        if channel == "beta" { "https://github.com/kxdrsrt/OdinGuard/releases/download/beta".into() } else { "https://github.com/kxdrsrt/OdinGuard/releases/latest/download".into() }
    })
}

/// The minisign key line from the updater plugin config (Tauri stores the .pub file base64 encoded).
fn updater_pubkey(app: &tauri::AppHandle) -> Res<String> {
    let b64 = app.config().plugins.0.get("updater").and_then(|u| u.get("pubkey")).and_then(|k| k.as_str()).ok_or("updater pubkey missing")?;
    og_core::update::minisign_key_line(b64).ok_or_else(|| "updater pubkey has no minisign key line".into())
}

#[derive(Serialize)]
struct UpdateInfo {
    version: String,
    notes: String,
    channel: String,
}

/// Step 1 (og-core): signed channel manifest, downgrade refusal and manifest pinning.
/// Step 2 (Tauri updater plugin, same manifest URL): artifact signature check and installation.
#[tauri::command]
async fn studio_update(app: tauri::AppHandle, channel: String, install: bool) -> Res<Option<UpdateInfo>> {
    use tauri::Manager;
    use tauri_plugin_updater::UpdaterExt;
    if !matches!(channel.as_str(), "stable" | "beta") {
        return Err(format!("unknown channel {channel}"));
    }
    let base = update_base(&channel);
    let dir = app.path().app_data_dir().map_err(err)?.join("updates");
    let u = og_core::update::Updater { endpoint: base.clone(), channel: channel.clone(), public_key: updater_pubkey(&app)?, dir, allow_loopback_http: false };
    let current = app.package_info().version.to_string();
    let checked = tauri::async_runtime::spawn_blocking(move || u.check("studio", &current).map(|m| (m, u)).map_err(err)).await.map_err(err)??;
    let (Some(m), u) = checked else { return Ok(None) };
    let info = UpdateInfo { version: m.version.clone(), notes: m.notes.clone(), channel: channel.clone() };
    if !install {
        return Ok(Some(info));
    }
    let url = tauri::Url::parse(&format!("{}/{channel}.json", base.trim_end_matches('/'))).map_err(err)?;
    let update = app.updater_builder().endpoints(vec![url]).map_err(err)?.build().map_err(err)?.check().await.map_err(err)?;
    let Some(update) = update else { return Err("updater plugin found no newer version".into()) };
    if update.version != m.version {
        return Err(format!("manifest changed during the update ({} vs {})", update.version, m.version));
    }
    update.download_and_install(|_, _| {}, || {}).await.map_err(err)?;
    u.record_installed("studio", &m).map_err(err)?;
    Ok(Some(info))
}

/// Tool groups of the profile's signature sources (paths relative to the profile file).
#[tauri::command]
fn signature_groups(path: String, profile: Profile) -> Res<Vec<og_core::sigpack::Group>> {
    let base = Path::new(&path).parent().unwrap_or(Path::new(".")).to_path_buf();
    let src: Vec<PathBuf> = profile.signatures.sources.iter().map(|s| profile.resolve(&base, s)).collect();
    let al: Vec<PathBuf> = profile.signatures.allowlist.iter().map(|s| profile.resolve(&base, s)).collect();
    og_core::sigpack::catalog(&src.iter().map(PathBuf::as_path).collect::<Vec<_>>(), &al.iter().map(PathBuf::as_path).collect::<Vec<_>>()).map_err(err)
}

#[tauri::command]
async fn grf_scan(client: String, full: bool) -> Res<Vec<og_core::profile::GrfFile>> {
    tauri::async_runtime::spawn_blocking(move || og_core::grf::scan_client(Path::new(&client), full).map_err(err)).await.map_err(err)?
}

#[tauri::command]
fn grf_tail(path: String) -> Res<String> {
    og_core::grf::tail_hash(Path::new(&path)).map(|(_, t)| t).map_err(err)
}

/// Selectable staging folders of assets/competitive/recipe.toml in copy order.
#[tauri::command]
fn competitive_recipe() -> Res<Vec<og_core::pack::PackOption>> {
    og_core::pack::options(&og_core::repo_root().join("assets/competitive/recipe.toml")).map_err(err)
}

#[derive(Serialize)]
struct RepoStatus {
    root: String,
    ok: bool,
}

fn repo_status_now() -> RepoStatus {
    let root = og_core::repo_root();
    RepoStatus { ok: og_core::is_repo_root(&root), root: root.to_string_lossy().into_owned() }
}

/// Whether Studio found the OdinGuard repository (registry, client sources) that builds need.
#[tauri::command]
fn repo_status() -> RepoStatus {
    repo_status_now()
}

/// Remembers the repository folder the operator picked (a downloaded Studio is not built inside a checkout).
#[tauri::command]
fn set_repo(app: tauri::AppHandle, path: String) -> Res<RepoStatus> {
    let p = PathBuf::from(&path);
    if !og_core::is_repo_root(&p) {
        return Err(format!("{path} is not an OdinGuard folder (modules/modules.toml and client/src/og_version.h are missing). Download it from https://github.com/kxdrsrt/OdinGuard"));
    }
    og_core::set_repo_root(Some(p));
    let file = repo_choice_file(&app)?;
    if let Some(dir) = file.parent() {
        std::fs::create_dir_all(dir).map_err(err)?;
    }
    std::fs::write(&file, &path).map_err(err)?;
    Ok(repo_status_now())
}

fn repo_choice_file(app: &tauri::AppHandle) -> Res<PathBuf> {
    use tauri::Manager;
    Ok(app.path().app_config_dir().map_err(err)?.join("odinguard-folder.txt"))
}

#[tauri::command]
fn studio_version() -> String {
    env!("CARGO_PKG_VERSION").to_string()
}

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .setup(|app| {
            // an explicit ODINGUARD_ROOT wins over the remembered folder
            if std::env::var_os("ODINGUARD_ROOT").is_none() {
                if let Some(saved) = repo_choice_file(app.handle()).ok().and_then(|f| std::fs::read_to_string(f).ok()) {
                    let p = PathBuf::from(saved.trim());
                    if og_core::is_repo_root(&p) {
                        og_core::set_repo_root(Some(p));
                    }
                }
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            get_registry,
            load_profile,
            validate_profile,
            save_profile,
            new_profile,
            preview,
            build,
            verify_release,
            commands_md,
            rollout_status,
            rollout_advance,
            keep_previous_release,
            studio_version,
            repo_status,
            set_repo,
            studio_update,
            signature_groups,
            grf_scan,
            grf_tail,
            competitive_recipe,
        ])
        .run(tauri::generate_context!())
        .expect("error while running OdinGuard Studio");
}

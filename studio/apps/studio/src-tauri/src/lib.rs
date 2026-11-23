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

#[tauri::command]
fn studio_version() -> String {
    env!("CARGO_PKG_VERSION").to_string()
}

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
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
            studio_version,
        ])
        .run(tauri::generate_context!())
        .expect("error while running OdinGuard Studio");
}

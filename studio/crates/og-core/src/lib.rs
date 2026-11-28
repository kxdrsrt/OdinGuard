//! OdinGuard Anti Cheat Studio core. The GUI (apps/studio) and the CLI (og-cli) are thin frontends of this crate.

pub mod crypto;
pub mod generate;
pub mod grf;
pub mod pack;
pub mod keys;
pub mod keybackup;
pub mod lgp_sounds;
pub mod manifest;
pub mod pipeline;
pub mod profile;
pub mod registry;
pub mod rollout;
pub mod sigpack;
pub mod update;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("{0}: {1}")]
    Io(String, std::io::Error),
    #[error("{0}: {1}")]
    Parse(String, String),
    #[error("{0}")]
    Invalid(String),
    #[error("build step failed: {0}")]
    Build(String),
    #[error("signature check failed: {0}")]
    Signature(String),
}

pub type Result<T> = std::result::Result<T, Error>;

static ROOT_CHOICE: std::sync::Mutex<Option<std::path::PathBuf>> = std::sync::Mutex::new(None);

/// The folder an operator picked in Studio (a downloaded Studio has no checkout next to it); `None` forgets it.
pub fn set_repo_root(p: Option<std::path::PathBuf>) {
    *ROOT_CHOICE.lock().unwrap_or_else(|e| e.into_inner()) = p;
}

/// A folder is an OdinGuard repository when it holds the module registry and the client sources.
pub fn is_repo_root(p: &std::path::Path) -> bool {
    p.join("modules/modules.toml").is_file() && p.join("client/src/og_version.h").is_file()
}

/// Root of the OdinGuard repository (client sources, registry, assets) used by builds: the folder chosen in Studio,
/// else `ODINGUARD_ROOT`, else the checkout this binary was built from.
pub fn repo_root() -> std::path::PathBuf {
    if let Some(p) = ROOT_CHOICE.lock().unwrap_or_else(|e| e.into_inner()).clone() {
        return p;
    }
    std::env::var_os("ODINGUARD_ROOT")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../.."))
}

/// Release of the guard the Studio builds (`OG_VERSION_STR` in client/src/og_version.h of the checkout it builds from).
pub fn guard_release() -> String {
    let text = std::fs::read_to_string(repo_root().join("client/src/og_version.h"))
        .unwrap_or_else(|_| include_str!("../../../../client/src/og_version.h").to_string());
    text.lines()
        .find_map(|l| l.strip_prefix("#define OG_VERSION_STR").map(|v| v.trim().trim_matches('"').to_string()))
        .unwrap_or_default()
}

#[cfg(test)]
mod root_tests {
    use super::*;

    #[test]
    fn chosen_folder_wins_and_only_a_repository_counts() {
        let repo = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..");
        assert!(is_repo_root(&repo));
        assert!(!is_repo_root(&std::env::temp_dir()));
        set_repo_root(Some(std::env::temp_dir()));
        assert_eq!(repo_root(), std::env::temp_dir());
        set_repo_root(None);
        assert!(is_repo_root(&repo_root()));
    }
}

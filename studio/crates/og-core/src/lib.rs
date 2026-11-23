//! OdinGuard Anti Cheat Studio core. The GUI (apps/studio) and the CLI (og-cli) are thin frontends of this crate.

pub mod crypto;
pub mod generate;
pub mod grf;
pub mod pack;
pub mod keys;
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

/// Root of the OdinGuard repository (client sources, registry, assets) used by builds.
pub fn repo_root() -> std::path::PathBuf {
    std::env::var_os("ODINGUARD_ROOT")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../.."))
}

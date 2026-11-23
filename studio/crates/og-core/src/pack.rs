//! Competitive-pack recipe (assets/competitive/recipe.toml): the selectable staging folders in copy order.

use serde::Serialize;
use std::path::Path;

use crate::{Error, Result};

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct PackOption {
    /// staging folder name, the value stored in the profile (assets.competitive)
    pub staging: String,
    pub category: String,
    pub title: String,
    pub description: String,
    pub fairness: String,
    pub default: bool,
    /// another staging folder this one needs (floor-flat-* needs flat-maps-*)
    pub requires: String,
    pub files: i64,
    pub grf_bytes: i64,
}

fn s(v: &toml::Value, k: &str) -> String {
    v.get(k).and_then(|x| x.as_str()).unwrap_or_default().to_string()
}

fn i(v: &toml::Value, k: &str) -> i64 {
    v.get(k).and_then(|x| x.as_integer()).unwrap_or_default()
}

/// Options of the recipe, sorted by its `copy_order` (later folders overwrite earlier ones).
pub fn options(recipe: &Path) -> Result<Vec<PackOption>> {
    let text = std::fs::read_to_string(recipe).map_err(|e| Error::Io(recipe.display().to_string(), e))?;
    let v: toml::Value = toml::from_str(&text).map_err(|e| Error::Parse(recipe.display().to_string(), e.to_string()))?;
    let mut out = Vec::new();
    for c in v.get("category").and_then(|c| c.as_array()).cloned().unwrap_or_default() {
        let (id, title) = (s(&c, "id"), s(&c, "title"));
        let default_variant = s(&c, "default_variant");
        let base = PackOption {
            staging: s(&c, "staging"),
            category: id.clone(),
            title: title.clone(),
            description: s(&c, "description"),
            fairness: s(&c, "fairness"),
            default: c.get("default").and_then(|x| x.as_bool()).unwrap_or(false),
            requires: String::new(),
            files: i(&c, "files"),
            grf_bytes: i(&c, "grf_bytes"),
        };
        if !base.staging.is_empty() {
            out.push(base.clone());
        }
        for var in c.get("variant").and_then(|x| x.as_array()).cloned().unwrap_or_default() {
            let vid = s(&var, "id");
            out.push(PackOption {
                staging: s(&var, "staging"),
                title: format!("{title} ({vid})"),
                description: [base.description.clone(), s(&var, "maps")].into_iter().filter(|x| !x.is_empty()).collect::<Vec<_>>().join(" – "),
                default: base.default && !default_variant.is_empty() && vid == default_variant,
                requires: s(&var, "requires"),
                files: i(&var, "files"),
                grf_bytes: i(&var, "grf_bytes"),
                ..base.clone()
            });
        }
    }
    let order: Vec<String> = v.get("copy_order").and_then(|x| x.as_array()).map(|a| a.iter().filter_map(|x| x.as_str().map(str::to_string)).collect()).unwrap_or_default();
    out.retain(|o| !o.staging.is_empty());
    out.sort_by_key(|o| order.iter().position(|x| *x == o.staging).unwrap_or(usize::MAX));
    Ok(out)
}

/// The selected staging folders in recipe order; unknown names keep their place after the known ones. Checks that
/// required folders are selected too.
pub fn ordered_selection(recipe: &Path, selected: &[String]) -> Result<Vec<String>> {
    if !recipe.is_file() {
        return Ok(selected.to_vec());
    }
    let opts = options(recipe)?;
    for o in opts.iter().filter(|o| selected.contains(&o.staging) && !o.requires.is_empty()) {
        if !selected.contains(&o.requires) {
            return Err(Error::Invalid(format!("competitive category {} needs {}", o.staging, o.requires)));
        }
    }
    let mut v = selected.to_vec();
    v.sort_by_key(|x| opts.iter().position(|o| &o.staging == x).unwrap_or(usize::MAX));
    Ok(v)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn repository_recipe_orders_flat_floor_after_flat_maps() {
        let r = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../assets/competitive/recipe.toml");
        let o = options(&r).unwrap();
        assert!(o.iter().any(|x| x.staging == "trees"));
        let sel = vec!["floor-flat-woe-bg-pvp".to_string(), "trees".into(), "flat-maps-woe-bg-pvp".into()];
        assert_eq!(ordered_selection(&r, &sel).unwrap(), ["flat-maps-woe-bg-pvp", "floor-flat-woe-bg-pvp", "trees"]);
        assert!(ordered_selection(&r, &["floor-flat-all".to_string()]).is_err(), "needs flat-maps-all");
    }
}

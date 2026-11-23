//! Rollout stages of a profile: 1 warn mode (log only), 2 kick, 3 patcher delivery, 4 required. The gate before the
//! next stage is computed from database exports (tab or comma separated with a header, e.g. `mysql -B -e "SELECT *
//! FROM og_event WHERE time > NOW() - INTERVAL 7 DAY"`):
//! - false-alarm candidates: rules that hit more than `fp_percent` of the active accounts and were not reviewed;
//! - coverage: share of active accounts that logged in with the guard (og_client) – needed for stage 4.

use serde::Serialize;
use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

use crate::profile::Profile;
use crate::{Error, Result};

#[derive(Debug, Clone, Serialize, Default)]
pub struct RuleStat {
    pub rule: String,
    pub events: usize,
    pub accounts: usize,
    pub percent: f64,
    pub actions: BTreeMap<String, usize>,
    pub reviewed: bool,
}

#[derive(Debug, Serialize)]
pub struct Status {
    pub stage: u8,
    pub active_accounts: usize,
    pub guarded_accounts: usize,
    pub coverage_percent: f64,
    pub rules: Vec<RuleStat>,
    pub fp_candidates: Vec<String>,
    pub next_stage_ok: bool,
    pub reasons: Vec<String>,
}

/// events, distinct accounts, actions
type RuleAcc = (usize, BTreeSet<String>, BTreeMap<String, usize>);

fn table(path: &Path) -> Result<Vec<BTreeMap<String, String>>> {
    let text = std::fs::read_to_string(path).map_err(|e| Error::Io(path.display().to_string(), e))?;
    let mut lines = text.lines().filter(|l| !l.trim().is_empty());
    let head = lines.next().ok_or_else(|| Error::Invalid(format!("{}: empty", path.display())))?;
    let sep = if head.contains('\t') { '\t' } else { ',' };
    let cols: Vec<String> = head.split(sep).map(|c| c.trim().trim_matches('"').to_string()).collect();
    Ok(lines
        .map(|l| cols.iter().cloned().zip(l.split(sep).map(|v| v.trim().trim_matches('"').to_string())).collect())
        .collect())
}

/// Rule name of an event type: "sig:OG-RCX-001" stays, "challenge:code" stays, details are not part of it.
fn rule_of(t: &str) -> String {
    t.to_string()
}

pub fn status(p: &Profile, events: &Path, clients: &Path, active_accounts: Option<usize>, fp_percent: f64) -> Result<Status> {
    let ev = table(events)?;
    let cl = table(clients)?;
    let guarded: BTreeSet<String> = cl.iter().filter_map(|r| r.get("account_id").cloned()).filter(|a| a != "0").collect();
    let active = active_accounts.unwrap_or(guarded.len()).max(1);
    let mut stats: BTreeMap<String, RuleAcc> = BTreeMap::new();
    for e in &ev {
        let rule = rule_of(e.get("type").map(String::as_str).unwrap_or(""));
        if rule.is_empty() || rule == "gm-ban" {
            continue;
        }
        let s = stats.entry(rule).or_default();
        s.0 += 1;
        if let Some(a) = e.get("account_id").filter(|a| *a != "0") {
            s.1.insert(a.clone());
        }
        *s.2.entry(e.get("action").cloned().unwrap_or_default()).or_default() += 1;
    }
    let mut rules: Vec<RuleStat> = stats
        .into_iter()
        .map(|(rule, (events, accs, actions))| RuleStat {
            reviewed: p.rollout.reviewed_rules.iter().any(|r| r == &rule),
            percent: accs.len() as f64 * 100.0 / active as f64,
            rule,
            events,
            accounts: accs.len(),
            actions,
        })
        .collect();
    rules.sort_by_key(|r| std::cmp::Reverse(r.accounts));
    let fp_candidates: Vec<String> = rules.iter().filter(|r| r.percent > fp_percent && !r.reviewed).map(|r| format!("{} ({:.1} % of the accounts)", r.rule, r.percent)).collect();
    let coverage = guarded.len() as f64 * 100.0 / active as f64;
    let mut reasons = Vec::new();
    if !fp_candidates.is_empty() {
        reasons.push(format!("{} rule(s) hit more than {fp_percent} % of the accounts: check them, then add them to rollout.reviewed_rules or fix the signature", fp_candidates.len()));
    }
    if p.rollout.stage == 3 && coverage < 98.0 {
        reasons.push(format!("only {coverage:.1} % of the active accounts play with the guard (needs 98 %)"));
    }
    if p.rollout.stage >= 4 {
        reasons.push("already at the last stage".into());
    }
    if ev.is_empty() && p.rollout.stage == 1 {
        reasons.push("no events yet: collect at least a few days of warn-mode logs".into());
    }
    Ok(Status {
        stage: p.rollout.stage,
        active_accounts: active,
        guarded_accounts: guarded.len(),
        coverage_percent: coverage,
        rules,
        fp_candidates,
        next_stage_ok: reasons.is_empty(),
        reasons,
    })
}

/// Moves the profile to the next stage when the gate allows it (`force` skips the gate, logged in the reasons).
pub fn advance(p: &mut Profile, s: &Status, force: bool) -> Result<u8> {
    if !s.next_stage_ok && !force {
        return Err(Error::Invalid(format!("rollout gate: {}", s.reasons.join("; "))));
    }
    if p.rollout.stage >= 4 {
        return Err(Error::Invalid("already at stage 4".into()));
    }
    p.rollout.stage += 1;
    Ok(p.rollout.stage)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn files(dir: &Path, events: &str, clients: &str) -> (std::path::PathBuf, std::path::PathBuf) {
        let e = dir.join("e.tsv");
        let c = dir.join("c.tsv");
        std::fs::write(&e, events).unwrap();
        std::fs::write(&c, clients).unwrap();
        (e, c)
    }

    #[test]
    fn gate_blocks_on_false_alarm_candidates_and_coverage() {
        let d = tempfile::tempdir().unwrap();
        let mut ev = String::from("time\tunique_id\taccount_id\tchar_name\ttype\tdetail\taction\n");
        for a in 0..30 {
            ev.push_str(&format!("t\tu\t{}\tc\tsig:OG-AHK-001\tAutoHotkey\tlog\n", 2000000 + a));
        }
        ev.push_str("t\tu\t2000001\tc\tsig:OG-RCX-001\trcxdraw.exe\tlog\n");
        let mut cl = String::from("unique_id\taccount_id\n");
        for a in 0..100 {
            cl.push_str(&format!("u{a}\t{}\n", 2000000 + a));
        }
        let (e, c) = files(d.path(), &ev, &cl);
        let mut p = Profile::new("t", "T").unwrap();
        let s = status(&p, &e, &c, Some(100), 2.0).unwrap();
        assert_eq!(s.fp_candidates.len(), 1, "{:?}", s.fp_candidates);
        assert!(!s.next_stage_ok);
        assert!(advance(&mut p, &s, false).is_err());
        p.rollout.reviewed_rules.push("sig:OG-AHK-001".into());
        let s = status(&p, &e, &c, Some(100), 2.0).unwrap();
        assert!(s.next_stage_ok, "{:?}", s.reasons);
        assert_eq!(advance(&mut p, &s, false).unwrap(), 2);
        p.rollout.stage = 3;
        let s = status(&p, &e, &c, Some(200), 2.0).unwrap();
        assert!(!s.next_stage_ok, "50 % coverage must block stage 4");
        let s = status(&p, &e, &c, Some(101), 2.0).unwrap();
        assert!(s.next_stage_ok, "{:?}", s.reasons);
        assert_eq!(advance(&mut p, &s, false).unwrap(), 4);
    }
}

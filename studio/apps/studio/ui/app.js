// OdinGuard Studio UI. Everything goes through the Tauri commands (og-core); the UI only edits the profile JSON.
"use strict";
const T = window.__TAURI__;
const invoke = (cmd, args) => T.core.invoke(cmd, args);
const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
const icon = (n) => `<svg class="ic" aria-hidden="true"><use href="#i-${n}"/></svg>`;
const el = (tag, cls, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; return e; };

let reg = null, profile = null, path = null, tab = "profile", toastTimer = 0;
const TITLES = { profile: "Profile", modules: "Modules", commands: "Commands", signatures: "Signatures", grf: "GRF & Mods", pack: "Competitive pack", build: "Build", rollout: "Rollout", updates: "Updates" };

// Feedback: a toast for events, a pill in the toolbar for the standing validation state.
function status(msg, kind = true) {
  const k = kind === true ? "ok" : kind === false ? "bad" : kind;
  const t = $("#toast");
  t.className = "toast " + k;
  t.innerHTML = icon(k === "ok" ? "check" : k === "warn" ? "warn" : "error") + `<span>${esc(msg)}</span>`;
  t.hidden = false;
  clearTimeout(toastTimer);
  if (k !== "bad") toastTimer = setTimeout(() => (t.hidden = true), 4500);
  else t.onclick = () => (t.hidden = true);
}

function pill(kind, text, title) {
  const p = $("#valid");
  p.hidden = !kind;
  if (!kind) return;
  p.className = "pill " + kind;
  p.title = title || "";
  p.innerHTML = icon(kind === "ok" ? "check" : kind === "warn" ? "warn" : "error") + esc(text);
}

async function revalidate() {
  if (!profile) return false;
  try {
    const w = await invoke("validate_profile", { profile });
    if (w.length) pill("warn", `${w.length} note${w.length > 1 ? "s" : ""}`, w.join("\n")); else pill("ok", "Valid", "Profile is valid.");
    return true;
  } catch (e) {
    pill("bad", "Invalid", String(e));
    status("Invalid: " + e, false);
    return false;
  }
}

function field(label, value, onchange, type = "text", hint = "") {
  if (type === "checkbox") {
    const l = el("label", "switch-row", `<span>${esc(label)}${hint ? `<small>${esc(hint)}</small>` : ""}</span><span class="switch"><input type="checkbox"><i></i></span>`);
    const i = l.querySelector("input");
    i.checked = !!value;
    i.addEventListener("change", () => { onchange(i.checked); revalidate(); });
    return l;
  }
  const l = el("label", "fld", `<span>${esc(label)}</span>`);
  const i = document.createElement("input");
  i.type = type;
  i.value = value ?? "";
  if (type === "text") i.spellcheck = false;
  i.addEventListener("change", () => { onchange(type === "number" ? Number(i.value) : i.value); revalidate(); });
  l.appendChild(i);
  if (hint) l.title = hint;
  return l;
}

const list = (s) => s.split(",").map((x) => x.trim()).filter(Boolean);
const ACTIONS = ["log", "flag", "flagban", "kick", "ban"];

function select(options, value, onchange) {
  const s = document.createElement("select");
  for (const [v, t] of options) s.add(new Option(t, v, false, v === value));
  s.addEventListener("change", () => { onchange(s.value); revalidate(); });
  return s;
}

function group(title, ...children) {
  const wrap = el("div");
  wrap.append(el("h3", "group-title", esc(title)));
  const card = el("div", "card form");
  card.append(...children);
  wrap.append(card);
  return wrap;
}

function renderGeneral() {
  const g = $("#general");
  g.innerHTML = "";
  const gen = profile.general, sg = profile.signatures;
  g.append(
    group("Identity",
      field("Profile id", gen.id, (v) => (gen.id = v)),
      field("Operator (splash, window title, messages)", gen.operator, (v) => (gen.operator = v)),
      field("Client build", gen.client_build, (v) => (gen.client_build = v)),
      field("Ports (comma separated)", gen.ports.join(","), (v) => (gen.ports = list(v).map(Number).filter((x) => x > 0))),
      field("Splash time (ms)", gen.splash_ms, (v) => (gen.splash_ms = v), "number"),
      field("HWID salt (never change after go-live)", gen.hwid_salt, (v) => (gen.hwid_salt = v)),
    ),
    group("Key files",
      field("Server key (file)", profile.keys.server_seed, (v) => (profile.keys.server_seed = v)),
      field("Signing key (file)", profile.keys.signing_seed, (v) => (profile.keys.signing_seed = v)),
      field("Build secret (file)", profile.keys.build_secret, (v) => (profile.keys.build_secret = v)),
    ),
    group("Signatures",
      field("Signature sources (comma separated)", sg.sources.join(","), (v) => (sg.sources = list(v))),
      field("Allowlists (comma separated)", sg.allowlist.join(","), (v) => (sg.allowlist = list(v))),
      field("Highest signature action", sg.max_action, (v) => (sg.max_action = v)),
      field("Signature pack version", sg.version, (v) => (sg.version = v), "number"),
    ),
    group("Output", field("Output folder", profile.output, (v) => (profile.output = v))),
  );
}

function paramValue(m, p) {
  const k = `${m.id}.${p.name}`;
  return k in profile.params ? profile.params[k] : p.default;
}

function renderModules() {
  const root = $("#modules");
  root.innerHTML = "";
  for (const m of reg.modules) {
    const on = profile.modules.includes(m.id);
    const box = el("div", "module" + (on ? " on" : ""));
    const head = el("header", null, `<div class="grow"><h3>${esc(m.id)}<span class="tag">${esc(m.kind)} · ${esc(m.side)}</span></h3><p class="sum">${esc(m.summary)}${m.requires.length ? ` <span class="tag">needs ${esc(m.requires.join(", "))}</span>` : ""}</p></div><span class="switch"><input type="checkbox" aria-label="Enable ${esc(m.id)}"><i></i></span>`);
    const cb = head.querySelector("input");
    cb.checked = on;
    cb.disabled = m.id === "core";
    cb.addEventListener("change", () => {
      if (cb.checked) {
        const add = (id) => { if (!profile.modules.includes(id)) { profile.modules.push(id); (reg.modules.find((x) => x.id === id).requires || []).forEach(add); } };
        add(m.id);
      } else profile.modules = profile.modules.filter((x) => x !== m.id);
      renderModules();
      renderCommands();
      revalidate();
    });
    box.append(head);
    if (on && m.param && m.param.length) {
      const ps = el("div", "params");
      for (const p of m.param) {
        const k = `${m.id}.${p.name}`;
        const v = paramValue(m, p);
        const set = (x) => { profile.params[k] = x; };
        let f;
        if (p.type === "action") {
          f = el("label", "fld", `<span>${esc(p.name)}</span>`);
          f.appendChild(select(ACTIONS.map((a) => [a, a]), v, set));
        } else f = field(`${p.name}${p.min != null ? ` (${p.min}–${p.max})` : ""}`, v, set, p.type === "bool" ? "checkbox" : p.type === "int" ? "number" : "text", p.help || "");
        f.title = p.help || "";
        ps.appendChild(f);
      }
      box.appendChild(ps);
    }
    root.appendChild(box);
  }
}

function renderCommands() {
  const t = $("#commands");
  const q = $("#q-commands").value.trim().toLowerCase();
  t.innerHTML = "<thead><tr><th>On</th><th>Command</th><th>Rename</th><th>Min. group</th><th>Rate limit (ms)</th><th>Module</th><th>What it does</th></tr></thead>";
  const body = el("tbody");
  let shown = 0;
  for (const c of reg.commands) {
    const help = c.help_en || c.help_de || "";
    const label = `${c.kind === "client" ? "!" : "@"}${c.name} ${c.params || ""}`;
    if (q && !(label + " " + help + " " + c.module).toLowerCase().includes(q)) continue;
    shown++;
    const s = profile.commands[c.id] || { enabled: true };
    const ensure = () => (profile.commands[c.id] = profile.commands[c.id] || { enabled: true });
    const active = profile.modules.includes(c.module);
    const tr = el("tr", null, `<td><span class="switch"><input type="checkbox" aria-label="Enable ${esc(label)}" ${s.enabled !== false ? "checked" : ""} ${active ? "" : "disabled"}><i></i></span></td>
      <td><code>${esc(label.trim())}</code></td>
      <td><input value="${esc(s.name || "")}" placeholder="${esc(c.name)}" size="10" aria-label="Rename"></td>
      <td><input type="number" value="${s.group ?? c.group}" style="width:70px" aria-label="Minimum group"></td>
      <td><input type="number" value="${s.rate_ms ?? c.rate_ms}" style="width:90px" aria-label="Rate limit"></td>
      <td class="muted">${esc(c.module)}${active ? "" : " (off)"}</td><td class="muted">${esc(help)}</td>`);
    const [en, name, grp, rate] = tr.querySelectorAll("input");
    en.addEventListener("change", () => { ensure().enabled = en.checked; revalidate(); });
    name.addEventListener("change", () => { const x = ensure(); if (name.value) x.name = name.value; else delete x.name; revalidate(); });
    grp.addEventListener("change", () => { ensure().group = Number(grp.value); revalidate(); });
    rate.addEventListener("change", () => { ensure().rate_ms = Number(rate.value); revalidate(); });
    body.appendChild(tr);
  }
  if (!shown) body.appendChild(el("tr", "empty-row", `<td colspan="7">No command matches “${esc(q)}”.</td>`));
  t.appendChild(body);
}

async function renderSignatures() {
  const t = $("#siggroups");
  const q = $("#q-sigs").value.trim().toLowerCase();
  let groups = [];
  try { groups = await invoke("signature_groups", { path, profile }); } catch (e) { status("Signatures: " + e, false); return; }
  profile.signatures.groups = profile.signatures.groups || {};
  t.innerHTML = "<thead><tr><th>Group</th><th>Kind</th><th class=\"num\">Entries</th><th class=\"num\">Client</th><th>Checks</th><th>Source</th><th>Reaction</th></tr></thead>";
  const body = el("tbody");
  for (const g of groups) {
    if (q && !g.group.toLowerCase().includes(q)) continue;
    const tr = el("tr", null, `<td><b>${esc(g.group)}</b></td><td>${g.allow ? "Allowlist" : "Signature"}</td><td class="num">${g.count}</td><td class="num">${g.active}</td><td class="muted">${esc(g.kinds.join(", "))}</td><td class="muted">${esc(g.sources.slice(0, 2).join(" · "))}</td><td></td>`);
    const opts = [["", "Default (" + g.actions.join("/") + ")"], ["off", "Off"]].concat(g.allow ? [] : ACTIONS.map((a) => [a, "All: " + a]));
    tr.lastElementChild.appendChild(select(opts, profile.signatures.groups[g.group] || "", (v) => { if (v) profile.signatures.groups[g.group] = v; else delete profile.signatures.groups[g.group]; }));
    body.appendChild(tr);
  }
  if (!body.children.length) body.appendChild(el("tr", "empty-row", `<td colspan="7">No group matches.</td>`));
  t.appendChild(body);
}

function delBtn(attr, i, label) {
  return `<button class="btn icon small danger" ${attr}="${i}" title="Remove" aria-label="Remove ${esc(label)}">${icon("trash")}</button>`;
}

function renderGrf() {
  const g = profile.grf;
  const a = $("#grf-actions");
  a.innerHTML = "";
  for (const [k, label] of [["modified_action", "GRF modified"], ["unknown_action", "Unknown GRF"], ["missing_action", "Required GRF missing"]]) {
    const l = el("label", "fld", `<span>${label}</span>`);
    l.appendChild(select(ACTIONS.map((x) => [x, x]), g[k], (v) => (g[k] = v)));
    a.appendChild(l);
  }
  $("#grf-files").innerHTML = "<thead><tr><th>Name</th><th class=\"num\">Size</th><th>Table hash</th><th>File hash</th><th>Required</th><th></th></tr></thead><tbody>" +
    (g.file.length ? g.file.map((f, i) => `<tr><td><b>${esc(f.name)}</b></td><td class="num">${f.size}</td><td class="muted mono">${esc(f.tail.slice(0, 16))}…</td><td class="muted mono">${f.full ? esc(f.full.slice(0, 16)) + "…" : "–"}</td><td><span class="switch"><input type="checkbox" data-req="${i}" aria-label="Required: ${esc(f.name)}" ${f.required ? "checked" : ""}><i></i></span></td><td>${delBtn("data-del", i, f.name)}</td></tr>`).join("") : `<tr class="empty-row"><td colspan="6">No approved GRF yet. Scan a client to add them.</td></tr>`) + "</tbody>";
  $("#grf-mods").innerHTML = "<thead><tr><th>Note</th><th>Table hash</th><th></th></tr></thead><tbody>" +
    (g.mod.length ? g.mod.map((m, i) => `<tr><td><b>${esc(m.note)}</b></td><td class="muted mono">${esc(m.tail.slice(0, 16))}…</td><td>${delBtn("data-delmod", i, m.note)}</td></tr>`).join("") : `<tr class="empty-row"><td colspan="3">No mods allowed.</td></tr>`) + "</tbody>";
  $("#grf-files").querySelectorAll("[data-req]").forEach((c) => c.addEventListener("change", () => { g.file[c.dataset.req].required = c.checked; revalidate(); }));
  $("#grf-files").querySelectorAll("[data-del]").forEach((b) => b.addEventListener("click", () => { g.file.splice(b.dataset.del, 1); renderGrf(); revalidate(); }));
  $("#grf-mods").querySelectorAll("[data-delmod]").forEach((b) => b.addEventListener("click", () => { g.mod.splice(b.dataset.delmod, 1); renderGrf(); revalidate(); }));
  $("#grf-overlays").value = g.overlays.join(",");
}

async function renderPack() {
  const src = $("#pack-src");
  src.innerHTML = "";
  src.append(
    field("Finished GRF instead of categories (optional)", profile.assets.competitive_pack, (v) => (profile.assets.competitive_pack = v)),
    field("Folder with the category staging", profile.assets.competitive_source || "", (v) => (profile.assets.competitive_source = v)),
  );
  const t = $("#pack-cats");
  let cats = [];
  try { cats = await invoke("competitive_recipe"); } catch (e) { t.innerHTML = `<tbody><tr class="empty-row"><td>No recipe found: ${esc(e)}</td></tr></tbody>`; return; }
  profile.assets.competitive = profile.assets.competitive || [];
  t.innerHTML = "<thead><tr><th>On</th><th>Category</th><th>Description</th><th class=\"num\">Files</th><th class=\"num\">Size</th><th>Fairness note</th></tr></thead>";
  const body = el("tbody");
  for (const c of cats) {
    const on = profile.assets.competitive.includes(c.staging);
    const mb = c.grf_bytes ? (c.grf_bytes / 1e6).toFixed(1) + " MB" : "";
    const tr = el("tr", null, `<td><span class="switch"><input type="checkbox" aria-label="${esc(c.title)}" ${on ? "checked" : ""}><i></i></span></td><td><b>${esc(c.title)}</b><span class="sub">${esc(c.staging)}${c.default ? " · default" : ""}${c.requires ? " · needs " + esc(c.requires) : ""}</span></td><td class="muted">${esc(c.description)}</td><td class="num">${c.files || ""}</td><td class="num">${mb}</td><td class="muted">${esc(c.fairness)}</td>`);
    tr.querySelector("input").addEventListener("change", (e) => {
      const l = profile.assets.competitive.filter((x) => x !== c.staging);
      if (e.target.checked) { l.push(c.staging); if (c.requires && !l.includes(c.requires)) l.push(c.requires); }
      profile.assets.competitive = cats.map((x) => x.staging).filter((x) => l.includes(x));
      renderPack(); revalidate();
    });
    body.appendChild(tr);
  }
  t.appendChild(body);
}

function renderStages() {
  const names = ["Warn mode", "Kick", "Patcher", "Mandatory"];
  $("#stages").innerHTML = names.map((n, i) => {
    const s = i + 1 < profile.rollout.stage ? "done" : i + 1 === profile.rollout.stage ? "now" : "";
    return `<div class="${s}"><small>${s === "done" ? "Done" : s === "now" ? "Current" : "Stage " + (i + 1)}</small><b>${i + 1}. ${n}</b></div>`;
  }).join("");
}

function output(sel, text) {
  const o = $(sel);
  o.hidden = !text;
  o.textContent = text || "";
}

// A button shows a busy state while its action runs, so a click always has an answer.
async function busy(btn, label, fn) {
  const html = btn.innerHTML;
  btn.disabled = true;
  btn.innerHTML = icon("updates") + label;
  try { return await fn(); } finally { btn.disabled = false; btn.innerHTML = html; }
}

function render() {
  const has = !!profile;
  document.body.classList.toggle("no-profile", !has);
  // the left-to-right mark keeps the leading "/" in place in the right-to-left (ellipsis at the start) label
  $("#path").textContent = path ? "\u200E" + path : "No profile open";
  $("#path").title = path || "";
  $("#btn-save").disabled = !has;
  setTab(tab);
  if (!has) { pill(null); return; }
  renderGeneral();
  renderModules();
  renderCommands();
  renderGrf();
  renderStages();
  renderSignatures();
  renderPack();
}

function setTab(name) {
  tab = name;
  document.querySelectorAll("nav button").forEach((b) => { const on = b.dataset.tab === name; b.classList.toggle("active", on); b.setAttribute("aria-current", on ? "page" : "false"); });
  document.querySelectorAll(".tab").forEach((s) => s.classList.toggle("active", s.id === "tab-" + name));
  $("#page-title").textContent = TITLES[name];
  $("#welcome").hidden = !(document.body.classList.contains("no-profile") && name !== "updates");
  $("#main").scrollTop = 0;
}

async function openProfile(p) {
  const r = await invoke("load_profile", { path: p });
  path = r.path;
  profile = r.profile;
  render();
  if (r.error) { pill("bad", "Invalid", r.error); status("Invalid: " + r.error, false); }
  else { await revalidate(); status(r.warnings.length ? "Loaded. Notes: " + r.warnings.join(" · ") : "Profile loaded.", r.warnings.length ? "warn" : "ok"); }
}

async function openDialog() {
  const p = await T.dialog.open({ filters: [{ name: "OdinGuard profile", extensions: ["toml"] }] });
  if (p) openProfile(p).catch((e) => status(String(e), false));
}

async function newProfile() {
  const p = await T.dialog.save({ defaultPath: "odinguard.profile.toml", filters: [{ name: "OdinGuard profile", extensions: ["toml"] }] });
  if (!p) return;
  const id = (prompt("Profile id (letters, digits, - _)", "myserver") || "").trim();
  const op = (prompt("Operator name (shown on the splash)", "My Ragnarok Server") || "").trim();
  if (!id || !op) return;
  try { const r = await invoke("new_profile", { path: p, id, operator: op }); path = r.path; profile = r.profile; render(); await revalidate(); status("Profile and key files created."); }
  catch (e) { status(String(e), false); }
}

async function save() {
  try { const w = await invoke("save_profile", { path, profile }); status("Saved." + (w.length ? " Notes: " + w.join(" · ") : ""), w.length ? "warn" : "ok"); }
  catch (e) { status("Not saved: " + e, false); }
}

document.querySelectorAll("nav button").forEach((b) => b.addEventListener("click", () => setTab(b.dataset.tab)));
$("#btn-open").addEventListener("click", openDialog);
$("#btn-open2").addEventListener("click", openDialog);
$("#btn-new").addEventListener("click", newProfile);
$("#btn-new2").addEventListener("click", newProfile);
$("#btn-save").addEventListener("click", save);
$("#q-commands").addEventListener("input", () => profile && renderCommands());
$("#q-sigs").addEventListener("input", () => profile && renderSignatures());
document.addEventListener("keydown", (e) => {
  if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "s") { e.preventDefault(); if (profile) save(); }
  if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "o") { e.preventDefault(); openDialog(); }
});

$("#btn-preview").addEventListener("click", () => busy($("#btn-preview"), "Working…", async () => {
  try { output("#build-out", JSON.stringify(await invoke("preview", { profile }), null, 2)); } catch (e) { status(String(e), false); }
}));
$("#btn-build").addEventListener("click", () => busy($("#btn-build"), "Building…", async () => {
  if (!(await revalidate())) return;
  await invoke("save_profile", { path, profile });
  output("#build-out", "Building …");
  try {
    const r = await invoke("build", { path });
    output("#build-out", JSON.stringify(r, null, 2));
    status(`Release built: ${r.out}`);
  } catch (e) { output("#build-out", String(e)); status("Build failed", false); }
}));
$("#btn-verify").addEventListener("click", async () => {
  const d = await T.dialog.open({ directory: true });
  if (d) invoke("verify_release", { dir: d }).then((m) => status(m)).catch((e) => status(String(e), false));
});
$("#btn-grf-scan").addEventListener("click", () => busy($("#btn-grf-scan"), "Scanning…", async () => {
  try {
    const scan = await invoke("grf_scan", { client: $("#grf-client").value, full: $("#grf-full").checked });
    const keep = $("#grf-keep").checked;
    const same = (a, b) => a.name.toLowerCase() === b.name.toLowerCase() && a.size === b.size && a.tail === b.tail;
    for (const s of scan) {
      if (!keep) profile.grf.file = profile.grf.file.filter((f) => f.name.toLowerCase() !== s.name.toLowerCase() || same(f, s));
      if (!profile.grf.file.some((f) => same(f, s))) profile.grf.file.push(s);
    }
    renderGrf(); revalidate(); status(`${scan.length} GRF(s) scanned.`);
  } catch (e) { status("Scan failed: " + e, false); }
}));
$("#btn-grf-mod").addEventListener("click", async () => {
  try {
    const f = $("#grf-mod").value;
    const tail = await invoke("grf_tail", { path: f });
    if (!profile.grf.mod.some((m) => m.tail === tail)) profile.grf.mod.push({ tail, note: f.split(/[\\/]/).pop().replace(/[,|]/g, " ") });
    renderGrf(); revalidate();
  } catch (e) { status(String(e), false); }
});
$("#grf-overlays").addEventListener("change", () => { profile.grf.overlays = list($("#grf-overlays").value); revalidate(); });
const roArgs = () => ({ path, events: $("#ro-events").value, clients: $("#ro-clients").value, active: $("#ro-active").value ? Number($("#ro-active").value) : null, fpPercent: Number($("#ro-fp").value) });
$("#btn-ro-status").addEventListener("click", () => busy($("#btn-ro-status"), "Evaluating…", async () => {
  try { const s = await invoke("rollout_status", roArgs()); output("#ro-out", JSON.stringify(s, null, 2)); status(s.next_stage_ok ? "Next stage is possible." : "Gate: " + s.reasons.join("; "), s.next_stage_ok ? "ok" : "warn"); }
  catch (e) { status(String(e), false); }
}));
$("#btn-ro-advance").addEventListener("click", async () => {
  try { const st = await invoke("rollout_advance", roArgs()); await openProfile(path); status(`Stage ${st} set. Rebuild the release and roll out the server config.`); }
  catch (e) { status(String(e), false); }
});
$("#btn-upd").addEventListener("click", () => busy($("#btn-upd"), "Checking…", async () => {
  const channel = $("#upd-channel").value;
  try {
    const u = await invoke("studio_update", { channel, install: false });
    if (!u) { output("#upd-out", `Up to date (channel ${channel}).`); return; }
    output("#upd-out", `Version ${u.version} (${u.channel}) is available, manifest signature valid:\n${u.notes || ""}`);
    if (!confirm(`Install Studio ${u.version}?`)) return;
    await invoke("studio_update", { channel, install: true });
    output("#upd-out", $("#upd-out").textContent + "\nInstalled. Restart Studio.");
  } catch (e) { output("#upd-out", "Update refused or failed: " + e); }
}));

(async () => {
  document.body.classList.add("no-profile");
  setTab("profile");
  try {
    reg = await invoke("get_registry");
    $("#ver").textContent = "v" + (await invoke("studio_version"));
  } catch (e) { status("Registry not found: " + e, false); }
})();

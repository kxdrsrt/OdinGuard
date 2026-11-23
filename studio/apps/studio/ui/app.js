// OdinGuard Studio UI. Everything goes through the Tauri commands (og-core); the UI only edits the profile JSON.
"use strict";
const T = window.__TAURI__;
const invoke = (cmd, args) => T.core.invoke(cmd, args);
const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

let reg = null, profile = null, path = null;

function status(msg, ok = true) {
  const s = $("#status");
  s.textContent = msg;
  s.className = ok ? "ok" : "bad";
}

async function revalidate() {
  if (!profile) return;
  try {
    const w = await invoke("validate_profile", { profile });
    status(w.length ? "Gültig mit Hinweisen: " + w.join(" · ") : "Profil gültig.", true);
    return true;
  } catch (e) {
    status("Ungültig: " + e, false);
    return false;
  }
}

function field(label, value, onchange, type = "text") {
  const l = document.createElement("label");
  l.textContent = label;
  const i = document.createElement("input");
  i.type = type;
  if (type === "checkbox") i.checked = !!value; else i.value = value ?? "";
  i.addEventListener("change", () => { onchange(type === "checkbox" ? i.checked : type === "number" ? Number(i.value) : i.value); revalidate(); });
  l.appendChild(i);
  return l;
}

function renderGeneral() {
  const g = $("#general");
  g.innerHTML = "";
  const gen = profile.general;
  g.append(
    field("Profil-ID", gen.id, (v) => (gen.id = v)),
    field("Betreiber (Splash, Meldungen)", gen.operator, (v) => (gen.operator = v)),
    field("Client-Build", gen.client_build, (v) => (gen.client_build = v)),
    field("Ports (Komma)", gen.ports.join(","), (v) => (gen.ports = v.split(",").map((x) => Number(x.trim())).filter((x) => x > 0))),
    field("Splash ms", gen.splash_ms, (v) => (gen.splash_ms = v), "number"),
    field("HWID-Salt (nie ändern nach Livegang)", gen.hwid_salt, (v) => (gen.hwid_salt = v)),
    field("Server-Schlüssel (Datei)", profile.keys.server_seed, (v) => (profile.keys.server_seed = v)),
    field("Signier-Schlüssel (Datei)", profile.keys.signing_seed, (v) => (profile.keys.signing_seed = v)),
    field("Build-Secret (Datei)", profile.keys.build_secret, (v) => (profile.keys.build_secret = v)),
    field("Signatur-Quellen (Komma)", profile.signatures.sources.join(","), (v) => (profile.signatures.sources = v.split(",").map((s) => s.trim()).filter(Boolean))),
    field("Allowlists (Komma)", profile.signatures.allowlist.join(","), (v) => (profile.signatures.allowlist = v.split(",").map((s) => s.trim()).filter(Boolean))),
    field("Höchste Signatur-Aktion", profile.signatures.max_action, (v) => (profile.signatures.max_action = v)),
    field("Signaturpaket-Version", profile.signatures.version, (v) => (profile.signatures.version = v), "number"),
    field("Competitive-Pack (GRF)", profile.assets.competitive_pack, (v) => (profile.assets.competitive_pack = v)),
    field("Ausgabe-Ordner", profile.output, (v) => (profile.output = v)),
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
    const box = document.createElement("div");
    box.className = "module";
    const h = document.createElement("h3");
    const cb = document.createElement("input");
    cb.type = "checkbox";
    cb.checked = on;
    cb.disabled = m.id === "core";
    cb.addEventListener("change", () => {
      if (cb.checked) {
        // add the module and everything it needs
        const add = (id) => { if (!profile.modules.includes(id)) { profile.modules.push(id); (reg.modules.find((x) => x.id === id).requires || []).forEach(add); } };
        add(m.id);
      } else profile.modules = profile.modules.filter((x) => x !== m.id);
      renderModules();
      revalidate();
    });
    h.append(cb, Object.assign(document.createElement("span"), { textContent: m.id }), Object.assign(document.createElement("span"), { className: "kind", textContent: `${m.kind} · ${m.side}` }));
    box.append(h, Object.assign(document.createElement("div"), { className: "muted", textContent: m.summary + (m.requires.length ? `  (braucht: ${m.requires.join(", ")})` : "") }));
    if (on && m.param && m.param.length) {
      const ps = document.createElement("div");
      ps.className = "params";
      for (const p of m.param) {
        const k = `${m.id}.${p.name}`;
        const v = paramValue(m, p);
        const set = (x) => { profile.params[k] = x; };
        let el;
        if (p.type === "action") {
          el = document.createElement("label");
          el.textContent = p.name;
          const s = document.createElement("select");
          for (const a of ["log", "flag", "flagban", "kick", "ban"]) s.add(new Option(a, a, false, a === v));
          s.addEventListener("change", () => { set(s.value); revalidate(); });
          el.appendChild(s);
        } else el = field(`${p.name}${p.min != null ? ` (${p.min}–${p.max})` : ""}`, v, set, p.type === "bool" ? "checkbox" : p.type === "int" ? "number" : "text");
        el.title = p.help || "";
        ps.appendChild(el);
      }
      box.appendChild(ps);
    }
    root.appendChild(box);
  }
}

function renderCommands() {
  const t = $("#commands");
  t.innerHTML = "<tr><th>An</th><th>Command</th><th>Name</th><th>Gruppe ab</th><th>Ratenlimit ms</th><th>Modul</th><th>Hilfe</th></tr>";
  for (const c of reg.commands) {
    const s = profile.commands[c.id] || { enabled: true };
    const ensure = () => (profile.commands[c.id] = profile.commands[c.id] || { enabled: true });
    const tr = document.createElement("tr");
    const active = profile.modules.includes(c.module);
    tr.innerHTML = `<td><input type="checkbox" ${s.enabled !== false ? "checked" : ""} ${active ? "" : "disabled"}></td><td>${c.kind === "client" ? "!" : "@"}${esc(c.name)} ${esc(c.params)}</td>
      <td><input value="${esc(s.name || "")}" placeholder="${esc(c.name)}" size="10"></td><td><input type="number" value="${s.group ?? c.group}" style="width:70px"></td>
      <td><input type="number" value="${s.rate_ms ?? c.rate_ms}" style="width:90px"></td><td class="muted">${esc(c.module)}${active ? "" : " (aus)"}</td><td class="muted">${esc(c.help_de)}</td>`;
    const [en, name, group, rate] = tr.querySelectorAll("input");
    en.addEventListener("change", () => { ensure().enabled = en.checked; revalidate(); });
    name.addEventListener("change", () => { const x = ensure(); if (name.value) x.name = name.value; else delete x.name; revalidate(); });
    group.addEventListener("change", () => { ensure().group = Number(group.value); revalidate(); });
    rate.addEventListener("change", () => { ensure().rate_ms = Number(rate.value); revalidate(); });
    t.appendChild(tr);
  }
}

function renderAuras() {
  $("#auras").innerHTML = "<tr><th>Name</th><th>Effekt</th><th>Seltenheit</th><th>Freischaltung</th><th>Datei</th></tr>" +
    reg.auras.map((a) => `<tr><td>${esc(a.name)}</td><td>${a.effect}</td><td>${esc(a.rarity)}</td><td>${esc(a.unlock)}</td><td class="muted">${esc(a.file || "(im Client)")}</td></tr>`).join("");
}

function renderStages() {
  const names = ["Warnmodus", "Kick", "Patcher", "Pflicht"];
  $("#stages").innerHTML = names.map((n, i) => `<div class="${i + 1 < profile.rollout.stage ? "done" : i + 1 === profile.rollout.stage ? "now" : ""}">${i + 1}. ${n}</div>`).join("");
}

function render() {
  $("#path").textContent = path || "kein Profil";
  $("#btn-save").disabled = !profile;
  if (!profile) return;
  renderGeneral();
  renderModules();
  renderCommands();
  renderAuras();
  renderStages();
}

async function openProfile(p) {
  const r = await invoke("load_profile", { path: p });
  path = r.path;
  profile = r.profile;
  render();
  if (r.error) status("Ungültig: " + r.error, false); else status(r.warnings.length ? "Geladen, Hinweise: " + r.warnings.join(" · ") : "Profil geladen.");
}

document.querySelectorAll("nav button").forEach((b) => b.addEventListener("click", () => {
  document.querySelectorAll("nav button, .tab").forEach((x) => x.classList.remove("active"));
  b.classList.add("active");
  $("#tab-" + b.dataset.tab).classList.add("active");
}));

$("#btn-open").addEventListener("click", async () => {
  const p = await T.dialog.open({ filters: [{ name: "OdinGuard-Profil", extensions: ["toml"] }] });
  if (p) openProfile(p).catch((e) => status(String(e), false));
});
$("#btn-new").addEventListener("click", async () => {
  const p = await T.dialog.save({ defaultPath: "odinguard.profile.toml", filters: [{ name: "OdinGuard-Profil", extensions: ["toml"] }] });
  if (!p) return;
  const id = (prompt("Profil-ID (Buchstaben, Ziffern, - _)", "myserver") || "").trim();
  const op = (prompt("Betreibername (Splash)", "My Ragnarok Server") || "").trim();
  if (!id || !op) return;
  try { const r = await invoke("new_profile", { path: p, id, operator: op }); path = r.path; profile = r.profile; render(); status("Profil und Schlüsseldateien angelegt."); }
  catch (e) { status(String(e), false); }
});
$("#btn-save").addEventListener("click", async () => {
  try { const w = await invoke("save_profile", { path, profile }); status("Gespeichert." + (w.length ? " Hinweise: " + w.join(" · ") : "")); }
  catch (e) { status("Nicht gespeichert: " + e, false); }
});
$("#btn-preview").addEventListener("click", async () => {
  try { $("#build-out").textContent = JSON.stringify(await invoke("preview", { profile }), null, 2); } catch (e) { status(String(e), false); }
});
$("#btn-build").addEventListener("click", async () => {
  if (!(await revalidate())) return;
  await invoke("save_profile", { path, profile });
  $("#build-out").textContent = "Baue …";
  try {
    const r = await invoke("build", { path });
    $("#build-out").textContent = JSON.stringify(r, null, 2);
    status(`Release gebaut: ${r.out}`);
  } catch (e) { $("#build-out").textContent = String(e); status("Build fehlgeschlagen", false); }
});
$("#btn-verify").addEventListener("click", async () => {
  const d = await T.dialog.open({ directory: true });
  if (d) invoke("verify_release", { dir: d }).then((m) => status(m)).catch((e) => status(String(e), false));
});
const roArgs = () => ({ path, events: $("#ro-events").value, clients: $("#ro-clients").value, active: $("#ro-active").value ? Number($("#ro-active").value) : null, fpPercent: Number($("#ro-fp").value) });
$("#btn-ro-status").addEventListener("click", async () => {
  try { const s = await invoke("rollout_status", roArgs()); $("#ro-out").textContent = JSON.stringify(s, null, 2); status(s.next_stage_ok ? "Nächste Stufe möglich." : "Gate: " + s.reasons.join("; "), s.next_stage_ok); }
  catch (e) { status(String(e), false); }
});
$("#btn-ro-advance").addEventListener("click", async () => {
  try { const st = await invoke("rollout_advance", roArgs()); await openProfile(path); status(`Stufe ${st} gesetzt – Release neu bauen und Server-Konfig ausrollen.`); }
  catch (e) { status(String(e), false); }
});
$("#btn-upd").addEventListener("click", async () => {
  const channel = $("#upd-channel").value;
  try {
    const u = await invoke("studio_update", { channel, install: false });
    if (!u) { $("#upd-out").textContent = `Aktuell (Kanal ${channel}).`; return; }
    $("#upd-out").textContent = `Version ${u.version} (${u.channel}) verfügbar, Manifest-Signatur gültig:\n${u.notes || ""}`;
    if (!confirm(`Studio ${u.version} installieren?`)) return;
    await invoke("studio_update", { channel, install: true });
    $("#upd-out").textContent += "\nInstalliert. Studio neu starten.";
  } catch (e) { $("#upd-out").textContent = "Update abgelehnt oder fehlgeschlagen: " + e; }
});

(async () => {
  try {
    reg = await invoke("get_registry");
    $("#ver").textContent = "v" + (await invoke("studio_version"));
    status(`${reg.modules.length} Module, ${reg.commands.length} Commands, ${reg.auras.length} Auren geladen. Profil öffnen oder neu anlegen.`);
  } catch (e) { status("Registry nicht gefunden: " + e, false); }
})();

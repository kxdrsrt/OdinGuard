// DOM test of the Studio window's update notice (no Tauri: the commands are mocked). Run: npm install && node smoke.js
const { JSDOM } = require("jsdom");
const fs = require("fs");
const dir = require("path").join(__dirname, "../../studio/apps/studio/ui/");
const PROFILE = JSON.parse(fs.readFileSync(require("path").join(__dirname, "profile.json"), "utf8"));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const ok = (c, m) => { console.log((c ? "PASS " : "FAIL ") + m); if (!c) failed = 1; };

async function mk({ update, fail, storage = {}, noRepo = false, wait = 2900 }) {
  const html = fs.readFileSync(dir + "index.html", "utf8").replace('<script src="app.js"></script>', "");
  const dom = new JSDOM(html, { runScripts: "outside-only", pretendToBeVisual: true, url: "https://studio.test/" });
  const w = dom.window;
  w.errors = [];
  // jsdom has no modal dialogs
  w.HTMLDialogElement.prototype.showModal = function () { this.setAttribute("open", ""); };
  w.HTMLDialogElement.prototype.close = function () { this.removeAttribute("open"); };
  w.addEventListener("error", (e) => w.errors.push(e.message));
  for (const [k, v] of Object.entries(storage)) w.localStorage.setItem("og." + k, v);
  w.calls = [];
  w.confirm = () => true;
  w.__TAURI__ = {
    core: { invoke: async (c, a) => {
      w.calls.push([c, a]);
      switch (c) {
        case "repo_status": return { root: "/x", ok: !noRepo };
        case "set_repo": if (a.path === "/bad") throw new Error("not an OdinGuard folder"); return { root: a.path, ok: true };
        case "keys_extra": return "/x/keys/studio-updater.key";
        case "keys_backup": if (a.password === "badbadbad") throw new Error("boom"); return 4;
        case "keys_restore": return [{ path: "/k/server.seed", action: "written" }, { path: "/k/signing.seed", action: "kept" }];
        case "load_profile": return { path: "/p/a.toml", profile: PROFILE, warnings: [], error: null };
        case "validate_profile": return [];
        case "signature_groups": return [];
        case "competitive_recipe": return [];
        case "save_profile": return null;
        case "get_registry": return { modules: [], commands: [] };
        case "studio_version": return "1.0.6";
        case "studio_update": if (fail) throw new Error("offline"); return a.install ? update : update || null;
        default: return null;
      } } },
    dialog: { open: async () => w.picked ?? null, save: async () => w.saveTo ?? null },
  };
  w.eval(fs.readFileSync(dir + "app.js", "utf8"));
  await sleep(wait); // the check runs 2.5 s after the registry is loaded
  return w;
}

(async () => {
  const upd = { version: "1.0.7", channel: "stable", notes: "Server module changed: update it." };
  let w = await mk({ update: upd });
  let d = w.document;
  ok(w.calls.some((c) => c[0] === "studio_update" && c[1].install === false), "start: one quiet check, without installing");
  ok(!d.getElementById("upd-banner").hidden && d.getElementById("upd-banner-text").textContent.includes("1.0.7") && d.getElementById("upd-banner-text").textContent.includes("1.0.6"), "banner names the new and the current version");
  ok(d.getElementById("upd-banner-text").textContent.includes("Nothing changes for players or servers"), "banner says that nobody is updated by it");
  ok(!d.getElementById("upd-dot").hidden, "dot on the Updates tab");
  d.getElementById("upd-banner-open").click();
  ok(d.getElementById("tab-updates").classList.contains("active") && d.getElementById("upd-notes").textContent.includes("Server module changed"), "What's new opens the Updates tab with the notes");
  ok(d.querySelectorAll(".steps li").length >= 6 && d.querySelector(".steps").textContent.includes("Keep the built release"), "the roll-out steps are shown");
  d.getElementById("upd-banner-hide").click();
  ok(d.getElementById("upd-banner").hidden && w.localStorage.getItem("og.upd.hidden") === "1.0.7", "Later hides the banner for this version");

  w = await mk({ update: upd, storage: { "upd.hidden": "1.0.7" } });
  d = w.document;
  ok(d.getElementById("upd-banner").hidden && !d.getElementById("upd-dot").hidden, "a hidden version stays hidden, the dot stays");
  w = await mk({ update: { ...upd, version: "1.0.8" }, storage: { "upd.hidden": "1.0.7" } });
  ok(!w.document.getElementById("upd-banner").hidden, "a newer version shows the banner again");

  w = await mk({ update: upd, storage: { "upd.auto": "0" } });
  ok(!w.calls.some((c) => c[0] === "studio_update") && !w.document.getElementById("upd-auto").checked, "switched off: no request at start");
  w = await mk({ update: upd, fail: true });
  ok(w.document.getElementById("upd-banner").hidden && w.errors.length === 0, "no network or no release file: silent, no error");

  w = await mk({ update: null });
  d = w.document;
  ok(d.getElementById("upd-banner").hidden && d.getElementById("upd-dot").hidden, "up to date: no banner");
  d.getElementById("btn-keep-prev").click(); await sleep(50);
  ok(d.getElementById("toast").textContent.includes("Open a profile first"), "Keep the built release without a profile says so");
  ok(w.errors.length === 0, "no script errors " + w.errors.join(";"));

  // a downloaded Studio has no repository next to it: it asks for the folder, remembers it, then works
  w = await mk({ update: null, noRepo: true, wait: 300 });
  d = w.document;
  ok(!d.getElementById("repo-banner").hidden && !w.calls.some((c) => c[0] === "get_registry"), "no OdinGuard folder: banner, nothing loaded");
  w.picked = "/bad"; d.getElementById("repo-banner-pick").click(); await sleep(100);
  ok(!d.getElementById("repo-banner").hidden && d.getElementById("toast").textContent.includes("not an OdinGuard folder"), "a wrong folder is refused with the reason");
  w.picked = "/good"; d.getElementById("repo-banner-pick").click(); await sleep(100);
  ok(d.getElementById("repo-banner").hidden && w.calls.some((c) => c[0] === "get_registry") && d.getElementById("ver").textContent === "v1.0.6", "the right folder: banner gone, registry loaded");
  ok(w.errors.length === 0, "no script errors (folder) " + w.errors.join(";"));

  // key backup: one password-protected file, asked for with a dialog; "not backed up" until it was done
  w = await mk({ update: null, wait: 300 });
  d = w.document;
  w.picked = "/p/a.toml"; d.getElementById("btn-open").click(); await sleep(300);
  const keyCard = () => [...d.querySelectorAll("#general .group-title")].find((h) => h.textContent === "Key backup").nextElementSibling;
  ok(keyCard().textContent.includes("Not backed up yet") && keyCard().classList.contains("warn-card"), "key backup: warns while the keys were never backed up");
  [...keyCard().querySelectorAll("button")].find((b) => b.textContent.includes("Back up")).click(); await sleep(50);
  const dlg = d.getElementById("keys-dlg"), go = () => d.getElementById("keys-form").dispatchEvent(new w.Event("submit", { cancelable: true }));
  ok(dlg.open === true || dlg.hasAttribute("open"), "the password dialog opens");
  ok(!d.getElementById("keys-upd-row").hidden, "the maintainer's update key is offered when it exists");
  d.getElementById("keys-pw").value = "short"; go(); await sleep(50);
  ok(!d.getElementById("keys-err").hidden && d.getElementById("keys-err").textContent.includes("8 characters"), "a short password is refused");
  d.getElementById("keys-pw").value = "long enough pw"; d.getElementById("keys-pw2").value = "different one"; go(); await sleep(50);
  ok(d.getElementById("keys-err").textContent.includes("differ"), "two different passwords are refused");
  d.getElementById("keys-pw2").value = "long enough pw"; w.saveTo = "/tmp/k.ogkeys"; go(); await sleep(100);
  const call = w.calls.filter((c) => c[0] === "keys_backup").pop();
  ok(call && call[1].out === "/tmp/k.ogkeys" && call[1].password === "long enough pw" && call[1].withUpdater === true, "the backup is made with the chosen file and password");
  ok(!dlg.hasAttribute("open") && keyCard().textContent.includes("Last backed up") && !keyCard().classList.contains("warn-card"), "afterwards the card shows the date, no warning");
  [...keyCard().querySelectorAll("button")].find((b) => b.textContent.includes("Restore")).click(); await sleep(50);
  ok(d.getElementById("keys-pw2-row").hidden && !d.getElementById("keys-force-row").hidden, "restore asks once and offers to replace differing keys");
  d.getElementById("keys-pw").value = "long enough pw"; w.picked = "/tmp/k.ogkeys"; go(); await sleep(100);
  ok(d.getElementById("toast").textContent.includes("1 written") && d.getElementById("toast").textContent.includes("1 differ"), "restore reports what was written and what was kept");
  ok(w.errors.length === 0, "no script errors (keys) " + w.errors.join(";"));
  process.exit(failed);
})();

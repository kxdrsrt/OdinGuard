// DOM test of the Studio window's update notice (no Tauri: the commands are mocked). Run: npm install && node smoke.js
const { JSDOM } = require("jsdom");
const fs = require("fs");
const dir = require("path").join(__dirname, "../../studio/apps/studio/ui/");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const ok = (c, m) => { console.log((c ? "PASS " : "FAIL ") + m); if (!c) failed = 1; };

async function mk({ update, fail, storage = {}, noRepo = false, wait = 2900 }) {
  const html = fs.readFileSync(dir + "index.html", "utf8").replace('<script src="app.js"></script>', "");
  const dom = new JSDOM(html, { runScripts: "outside-only", pretendToBeVisual: true, url: "https://studio.test/" });
  const w = dom.window;
  w.errors = [];
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
        case "get_registry": return { modules: [], commands: [] };
        case "studio_version": return "1.0.6";
        case "studio_update": if (fail) throw new Error("offline"); return a.install ? update : update || null;
        default: return null;
      } } },
    dialog: { open: async () => w.picked ?? null },
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
  process.exit(failed);
})();

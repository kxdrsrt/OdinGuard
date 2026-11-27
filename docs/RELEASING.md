# Releasing OdinGuard (for the maintainer)

A release has four parts. Every one of them is needed, because **the Studio of every operator asks GitHub for
`releases/latest/download/stable.json`** when it starts and shows a banner if the version in it is newer. A release without
that file is invisible to them.

| Part | What | How |
|---|---|---|
| 1. Version | `client/src/og_version.h`, `studio/Cargo.toml`, `studio/apps/studio/src-tauri/Cargo.toml`, `tauri.conf.json` | the same number everywhere; the Studio **is** the guard release (it builds the guard) |
| 2. Tag and CI | `git tag vX.Y.Z && git push origin vX.Y.Z` | CI must be green (Studio tests, client, GCC and Visual Studio build of the server patch) |
| 3. GitHub release | server patch `odinguard-X.Y.Z-rathena-<commit>.patch` and `SHA256SUMS.txt`, plus the notes | `gh release create` (see the notes template) |
| 4. Studio update file | signed `stable.json` + `.sig` + the signed Studio builds for macOS, Windows and Linux | automatic: publishing the GitHub release starts `.github/workflows/studio-release.yml` (about 8 minutes) |

Part 4 builds the Studio on all three platforms and uploads **one** `stable.json` that lists them, signed with the Studio
update key. The key is the repository secret `TAURI_SIGNING_PRIVATE_KEY` (the content of `keys/studio-updater.key`; add
`TAURI_SIGNING_PRIVATE_KEY_PASSWORD` if the key gets a password). **Keep that key private and backed up**: Studios only
accept updates signed with it.

- Re-run or check by hand: Actions → studio-release → *Run workflow* with the tag; leave *publish* off to only build and
  keep the files as workflow artifacts (`studio-release-files` holds the manifest and signatures).
- Without CI (one platform, e.g. for a test): `OG_PUBLISH_TAG=vX.Y.Z tools/release_studio.sh stable <base url>` builds on the
  current platform. Do **not** publish that over a release that CI already filled: its `stable.json` would list only the
  one platform. `tools/studio_manifest.sh` merges a folder of builds from several platforms into one manifest.

## The notes are what operators read

The text of the GitHub release is shown in the Studio banner's "What's new" page. Use this template, and always fill in
the **Roll-out** block; it is what lets an operator decide how careful to be:

```
OdinGuard Anti Cheat X.Y.Z

**New** / **Fixed**
- …

**Roll-out**
- Guard changed: yes | no   (yes = every new build changes the guard code: keep the previous release first)
- Server module / patch changed: yes | no   (yes = apply the new patch or copy the module before the clients)
- Protocol: unchanged | new record types, old guards still accepted | older guards are refused
- Players: nothing happens until you patch them. Order: keep previous release → build → server → clients.

**Tests:** …
```

## What an operator sees

1. When Studio starts it does one request for the signed `stable.json` (the operator can switch this off under
   Updates). Nothing about the operator or the server is sent.
2. Newer version: a banner "OdinGuard X is available … nothing changes for players or servers until you roll it out"
   with *What's new* and *Later* (hides it until the next version).
3. *Updates* tab: the notes, the install button (Studio only) and the seven roll-out steps.
4. Players and servers are **never** updated by this. The operator keeps the previous release (Rollout tab → *Keep the
   built release*), builds, deploys the server config and then patches the clients with their patcher.

An operator who does not use the Studio can watch the repository's releases on GitHub (Watch → Custom → Releases).

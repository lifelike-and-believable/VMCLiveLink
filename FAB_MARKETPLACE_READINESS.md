# Fab Marketplace Readiness Report

**Date:** 2026-07-13

> **Status (2026-10-07).** This report predates the 1.0.0 work; parts of it are out of date.
> - **Engine versions:** both plugins now build and are tested on UE 5.6, 5.7 and 5.8, and the Fab workflow makes a package per engine (see the [support plan](Planning%20Docs/UE_5.7_5.8_Support_Plan_2026-10.md)). Sections 1, 2 and 4 are updated for this.
> - **Packages contain `Binaries` and `Intermediate`:** `RunUAT BuildPlugin -Package` output includes them, and `build_fab.ps1` doesn't remove them (section 2 said otherwise). Check whether Fab's code-plugin submission wants them removed before submitting.
> - **Done since:** `DocsURL` and `SupportURL` are set in both `.uplugin` files (both point at the root README, which leads to the [User Guide](docs/USER_GUIDE.md)); both plugins are version 1.0.0; PR #96 merged (2026-07-13); every pull request builds the Game target in Shipping and runs the tests (`pr-build.yml`); Win64-only is a decision (D-2) and the READMEs say so.
> - **Still open:** `Plugins/VRMInterchange/Resources/Icon128.png` (missing); `SupportedTargetPlatforms` in both `.uplugin` files; a LICENSE file in each plugin root (the plan, P7.5, asks for one before the 1.0.0 tag; the owner must supply the terms); the `release/1.0.0` tag, then a check that the Fab zips package clean; listing assets and example content; the publisher-side steps.
**Scope:** `Plugins/VMCLiveLink` and `Plugins/VRMInterchange` (both plugins in this repo), assessed against Epic's Fab publisher/technical requirements.

## Sourcing note (read this first)

This report was requested against two specific pages:
- `https://www.fab.com/o/technical-requirements`
- `https://dev.epicgames.com/documentation/fab/publisher-get-started-in-fab`

Both were **directly inaccessible** from this environment: the outbound agent proxy returned `403` (CONNECT tunnel rejected) for `fab.com` and `dev.epicgames.com`, confirmed via a direct `curl` test against the proxy — this is an organization-level egress policy block, not a site-side error, so it wasn't retried or routed around. Several other externally-linked pages surfaced by search (`support.fab.com`, `forums.unrealengine.com`, `mythiclemon.com`) were also blocked the same way.

The findings below are therefore based on **web search results with citations** (Fab support articles, Epic dev docs, community write-ups referencing the same official requirements) plus general knowledge of Unreal plugin packaging, **not a direct read of the two linked pages**. Requirements move over time and Fab's own copy is authoritative — **verify the current state of both linked pages yourself before submitting**, especially anything marked with a source citation below.

---

## Executive Summary

Both plugins are in reasonable technical shape for an eventual Fab submission — they compile as proper code plugins, have real READMEs, and already have a CI pipeline that packages Fab-ready zips via `RunUAT.bat BuildPlugin -Rocket`. However, several gaps would likely cause a submission to be rejected or bounced back in review, and a few "unknowns" need a human decision (pricing, licensing terms, marketing assets) that no amount of code review can resolve. Nothing found here is a deep architectural problem — it's submission-readiness, not product-readiness.

**Bottom line: not ready to submit today, but the remaining work is packaging/listing/asset work, not engineering work.**

---

## 1. Plugin Structure & Metadata (`.uplugin`)

| Field | VMCLiveLink | VRMInterchange | Fab expectation | Status |
|---|---|---|---|---|
| `FriendlyName` / `Description` | Set, accurate | Set, accurate | Required | ✅ |
| `Category` | `"Live Link"` | `"Importers"` | Should match a real Fab/Marketplace category | ✅ (verify current Fab category taxonomy — categories were reorganized when Marketplace → Fab migrated) |
| `CreatedBy` / `CreatedByURL` | Set | Set | Required for listing attribution | ✅ |
| `EngineVersion` | Not set in the source; each package sets it | Not set in the source; each package sets it | Fab takes one package per engine version. A `"5.6.0"` in the source descriptor makes UE 5.7 and later refuse to load the plugin, so `build_fab.ps1` sets it per package instead | ✅ Packaged per engine for 5.6, 5.7 and 5.8 by `fab-plugin-build.yml` ([plan](Planning%20Docs/UE_5.7_5.8_Support_Plan_2026-10.md)) |
| `SupportedTargetPlatforms` | Not set (module-level `IncludeListPlatforms: ["Win64"]` only) | Not set (Win64-only on Editor/Spring-Bones-Editor modules) | Fab wants explicit, accurate platform support declared | ⚠️ Should be declared explicitly; also both plugins are Windows-only today (see §4) |
| `DocsURL` / `SupportURL` | Empty | Empty | Not strictly required, but expected for a paid/listed plugin and improves review odds | ⚠️ Empty |
| `MarketplaceURL` | Empty | Empty | Populated by Epic after first listing, not a submission blocker | ➖ N/A pre-submission |
| `IsBetaVersion` / `IsExperimentalVersion` | `false` / `false` | `false` / `false` | Fab requires these off for a public listing | ✅ |
| Version number | `0.1.0` | `0.1.0` | No hard rule, but a `0.x` version signals "not production" to reviewers and buyers | ⚠️ Consider bumping to `1.0.0` for the public launch (also flagged in `docs/archive/PRODUCTION_READINESS_ANALYSIS.md` §7.1) |

## 2. Code Plugin Packaging Requirements

- **Module structure**: Both plugins declare proper Runtime/Editor/UncookedOnly modules with correct `LoadingPhase` values — this matches Fab's "must contain compiled code, not just Blueprints/content" bar for a Code Plugin listing. ✅
- **Build pipeline**: `.github/workflows/fab-plugin-build.yml` invokes `scripts/build_fab.ps1`, which shells out to `RunUAT.bat BuildPlugin -Plugin=... -Package=... -Rocket -VeryVerbose`. UAT's `BuildPlugin` step is the same mechanism Epic's own docs describe for packaging a plugin for distribution, but its packaged output **does** include `Binaries` and `Intermediate` (checked on a 5.8 package), and `build_fab.ps1` doesn't strip them. ⚠️ Check Fab's current rule for code plugins before submitting; removing them would also mean a packaged GitHub release needs Visual Studio to build (the User Guide says a release with `Binaries` doesn't).
- **No errors/warnings**: Not verifiable in this sandbox (no UE toolchain available here). Fab's technical review explicitly checks that a plugin packages clean in a stock project via the editor's own "Package Plugin" flow — **this must be manually verified on a Windows box, in each engine (5.6, 5.7, 5.8), before submission**. The Fab workflow's BuildPlugin runs for every engine build without errors, and CI's builds have no plugin warnings. ⚠️ Editor check still to do
- **Zip output**: The workflow produces, for each engine, a combined zip plus one zip per plugin, and checks that each package's `.uplugin` names its engine. Fab accepts a `.zip` (optionally password-protected, with the password disclosed in the submission's version notes) — the current setup produces an unprotected zip, which is the simpler and equally valid path. ✅

## 3. Licensing / Third-Party Content

- **`cgltf`** (`Plugins/VRMInterchange/ThirdParty/cgltf/cgltf.h`) — confirmed MIT license, embedded license text present at the bottom of the file (Copyright (c) 2018-2021 Johannes Kuhlmann). MIT is permissive and compatible with Fab's EULA.
- **`jsmn`** (embedded inside `cgltf.h`, from `github.com/zserge/jsmn`) — also MIT-licensed upstream; the embedded copy inherits the same permissive terms as the cgltf.h license block covers the whole file.
- Per search results, the Fab EULA/Distribution Agreement prohibits combining Licensed Technology with **GPL/LGPL/copyleft/"ShareAlike"-style** licensed code. Neither `cgltf` nor `jsmn` triggers this — **no copyleft-license conflict found**. ✅
- Both plugin READMEs already have a "Credits" section attributing `cgltf` to Johannes Kuhlmann and the VRM format to the VRM Consortium — good practice for the Fab listing description, which typically wants third-party attributions spelled out. ✅
- **No repository-root `LICENSE` file exists.** Both plugin READMEs state "All Rights Reserved" for the plugin's own code. This is fine for a commercial Fab listing (Fab's own EULA governs buyer terms), but there's no harm in adding an explicit root `LICENSE` file for clarity. ⚠️ Open: the plan (P7.5) now asks for a LICENSE file in each plugin root before the 1.0.0 tag; the owner must supply the terms.
- **`Plugins/VRMInterchange/README.md`** currently on `main` still says `Copyright (c) 2024 ... All Rights Reserved.` and links the stale `atgoldberg/VMCLiveLink` GitHub URL — both already fixed in the still-open, unmerged **PR #96**. Worth merging before a Fab submission so the listing description (often derived from the README) doesn't ship stale text.

## 4. Platform & Engine-Version Coverage

- Both plugins currently build **Win64-only** (`VMCLiveLink`'s Runtime module, `VRMInterchangeEditor`, and `VRMSpringBonesEditor` all restrict `IncludeListPlatforms` to `["Win64"]`). VMC/OSC networking and VRM parsing have no inherent Windows dependency, so this is very likely a build-target choice rather than a technical constraint — but it does mean the Fab listing must clearly state "Windows only" (Fab allows single-platform listings; it must just be accurately declared, not silently assumed). ⚠️ Declare explicitly in the listing, or invest in cross-platform module support if broader reach is wanted later.
- **Engine versions: UE 5.6, 5.7 and 5.8.** CI builds and tests every pull request on all three, and the Fab workflow packages each plugin for each (decision D-9 of the [support plan](Planning%20Docs/UE_5.7_5.8_Support_Plan_2026-10.md): support the three newest engines). Upload one package per engine version to each listing. ✅

## 5. Listing Assets (Icons, Screenshots, Description)

- **VMCLiveLink** has a plugin icon (`Plugins/VMCLiveLink/Resources/Icon128.png`). ✅
- **VRMInterchange has no `Resources/Icon128.png` at all** — it will fall back to Unreal's generic default plugin icon in the editor's Plugins browser, and Fab listings need their own thumbnail/icon art regardless (the in-editor icon and the Fab store thumbnail are separate assets, but a plugin with no icon at all is a visible gap even in-editor). ❌ **Action needed**
- No marketing screenshots, demo video, or Fab store thumbnail/gallery images found anywhere in the repo for either plugin (expected — these are typically produced outside the source tree, but flagging since Fab submission requires them). ❌ **Action needed** — not a code task; needs actual content creation
- No example/sample content package (a demo map, sample `.vrm` file, or example Live Link setup) ships with either plugin. `docs/archive/PRODUCTION_READINESS_ANALYSIS.md` §6.3 already flags this ("Example Content: ❌ Recommended") — worth prioritizing since reviewers and buyers both lean heavily on example content to evaluate a plugin quickly. ⚠️

## 6. Publisher/Process Requirements (per search results — verify against the linked docs directly)

These aren't things this repo can satisfy — they're account/business steps on Epic's side:
1. **Fab Publisher Profile** — sign the Fab Distribution Agreement, complete a Creator Code / Trader Verification (tax + payout identity) flow before a paid listing can go live.
2. **Revenue share** — reported as 88% to the creator on Fab (down from Epic taking a cut) — confirm current terms directly against Fab's own docs before pricing decisions.
3. **Review process** — submissions enter a "Pending approval" queue; Epic's Fab team reviews and emails a decision (approve / changes requested / reject). Typical turnaround wasn't reliably confirmed via search — don't commit to a launch date until this is confirmed on the actual publisher dashboard.
4. **Listing content** — category, tags, description, compatible engine versions, and pricing tier are all set at submission time via the Fab publisher dashboard, not from repo content directly (though the README is a natural source to draft the listing description from).

## 7. Cross-check Against Existing Repo Analysis

`docs/archive/PRODUCTION_READINESS_ANALYSIS.md` §6.3 ("Fab Marketplace Requirements") already contains an internal checklist reaching similar conclusions independently: copyright headers/metadata/docs marked done, but **plugin icons, Shipping-config testing, and example content marked as open items** — this report corroborates that assessment and adds the specific finding that VRMInterchange has no icon file at all (VMCLiveLink does), plus the licensing and platform-declaration details that document didn't cover.

---

## Action Items Before Submission (priority order)

1. **Merge PR #96** (documentation fixes) — no reason to submit with a stale README copyright line/URL still on `main`.
2. **Add a `Resources/Icon128.png` to `Plugins/VRMInterchange`** — currently missing entirely.
3. **Manually verify a clean package** for both plugins in each engine (5.6, 5.7, 5.8): install the Fab zips in a clean project (the support plan's U6).
4. **Produce listing assets**: Fab store thumbnail, screenshots, and ideally a short demo video for both plugins — not a code task.
5. **Add example content** (sample `.vrm` file + demo map/setup) — flagged independently by both this report and the existing `docs/archive/PRODUCTION_READINESS_ANALYSIS.md`.
6. **Declare `SupportedTargetPlatforms` explicitly** in both `.uplugin` files (currently implicit via module-level `IncludeListPlatforms`), and state "Windows only" plainly in both listings unless cross-platform support is added first.
7. **Decide on version number** — bump `0.1.0` → `1.0.0` for public launch, or keep as-is and accept it reads as pre-release to buyers.
8. **Complete the publisher-side steps** (Distribution Agreement, Trader Verification, pricing) — independent of this repo, needs to happen on the Fab dashboard directly.
9. **Re-verify everything in this report against the two originally-linked pages directly** once they're reachable (or via a browser outside this sandboxed environment) — this report is a best-effort secondary-source assessment, not a substitute for reading Fab's current authoritative requirements.

Nothing in this list is a code-architecture blocker; it's submission packaging and content work.

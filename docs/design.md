# snes2DHD — design

Approved 2026-10-31 by the maintainer ([@nissifield](https://github.com/nissifield)). Changes to this document go through a core member.

## 1. Intent

An open-source SNES emulator that plays unmodified games but **presents them in an HD-2D style** — between 3dSen (NES games as voxel dioramas) and Octopath Traveler II (pixel-art sprites in lit 3D scenes with depth of field and bloom). Each game can have a **profile** (plus optional project-original assets — no redrawn game art) that tunes how its layers, tiles and sprites become 3D. Games without a profile still play normally in 2D.

The project is also an experiment in **AI-driven open source**: a small core team of agents (run by core members) sets direction, writes precise task cards, reviews and auto-merges; anyone can contribute by pasting a card's prompt into whatever AI tool they already use.

Why: 3dSen proved per-game profiles make old games feel new; nothing open does this for SNES. Super ZSNES (closed source) does per-game HD enhancements including Chrono Trigger, but not full HD-2D scenes and not community-owned.

## 2. Goals, non-goals, anti-goals

**Goals**
- G1. Accurate emulation via the ares SNES core; gameplay identical to the original.
- G2. A GPU renderer that rebuilds each frame as a 3D scene from PPU state, with two element modes chosen per profile: **flat** (upright pixel billboards/planes, Octopath-like) and **voxel** (extruded tiles/sprites, 3dSen-like), mixable within one game.
- G3. Per-game profiles (data, not code), keyed by ROM hash, safe by default: any profile mismatch falls back to stock rendering for that element.
- G4. Showcase games: **Terranigma** (PAL/EU) and **Chrono Trigger** (US). First public milestone = both look clearly HD-2D for at least their opening areas, and Chrono Trigger visibly beats Super ZSNES's enhancement.
- G5. Contribution with near-zero friction: one copy-paste prompt per card, working in browser chats and coding agents alike.
- G6. Auto-merge with hard rails, core-team notifications, nightly builds and a playtest section.
- G7. An attractive repo with original, AI-assisted art.

**Non-goals (v1)**: netplay, RetroAchievements, VR, mobile, libretro core, special chips beyond what ares provides for the showcase games, AI upscaling, a from-scratch emulator.

**Anti-goals** — failure even if it "works":
- Shipping or committing any ROM data or game-derived data dumps.
- Copying code or data from Super ZSNES or any licence-incompatible emulator.
- A reviewer agent that can be talked (prompt-injected) into approving; contributors able to change the rules that judge them.
- Bloat: abstractions, options and frameworks nobody needed (the project is "ponytail" style).
- A game looking worse than stock with its profile on.

## 3. Constraints

- **Legal**: users supply their own ROMs; the emulator verifies SHA-256 against the profile. CI uses only open-licensed homebrew/test ROMs and synthetic PPU fixtures. Real-game verification happens on contributors' machines (playtest).
- **Base**: ares (`ares-emulator/ares`, ISC). Only its SNES core and required support library, vendored at a pinned commit. ares has no public hook for per-scanline PPU/VRAM writes, so the tap needs a **small patch set** kept as separate, upstreamable files in `patches/ares/` — no hidden edits in the vendored tree.
- **Platforms**: Linux and Windows first; macOS best-effort later.
- **Hardware**: a mid-range GPU from the last ~6 years holds 60 fps at 1080p.
- **Ponytail**: minimal code; prefer platform/library features; every non-trivial piece has a small test. Ponytail rule text carries its MIT notice (`third_party/ponytail/`).

## 4. Approaches (decided: A)

| | Approach | Trade-off | Verdict |
|---|---|---|---|
| **A** | Extract the ares SNES core, write our own GPU HD-2D renderer | ISC allows our licence choice; active upstream; extracting the core is upfront work | **Chosen** |
| B | Fork bsnes-hd | Quick start and HD Mode 7, but CPU-only 2D output (the renderer is new work anyway), dormant since 2021, forces GPL-3 | Rejected |
| — | Super ZSNES | Closed source | Prior art / quality bar only |
| — | Framebuffer post-processing | Loses layer/sprite info → no real HD-2D | Rejected |
| — | Per-game native recompilation | Years per game; doesn't generalise | Rejected |

## 5. Architecture

```
ROM ─► ares SNES core (pinned; small tap patch set in patches/ares/)
          │  every PPU register + VRAM/OAM/CGRAM write with its scanline, selected WRAM bytes
          ▼
       tap adapter  ──►  FrameState (plain data, serialisable → test fixtures)
                                │
             profile (JSON) ──► scene builder ──► Scene (planes, billboards, voxel meshes, lights, camera)
                                                     │
                                                     ▼
                                              GPU renderer (SDL3 GPU: Vulkan/D3D12/Metal)
                                              + post FX (DoF, bloom, tilt-shift, shadows)
                                                     │
                                       SDL3 window / input / audio (audio straight from ares)
```

- **Tap adapter**: records what the PPU was told — every $2100–$213F write and every VRAM/OAM/CGRAM write with its scanline, not just end-of-frame memory (mid-frame DMA matters) — so Mode 7 perspective, wavy screens and split layers survive. FrameState is plain data, so it can be dumped and replayed in tests without a ROM (only synthetic fixtures are committed).
- **Scene builder**: pure function `(FrameState, Profile) → Scene`. No profile → one textured plane = the stock 2D picture. Flat/voxel modes live here.
  - Background layers → planes at profile-set depth/tilt; tile classes → flat, raised, or voxel blocks.
  - Sprites → billboards (flat) or voxel meshes; multi-tile sprites grouped into one object (grouping rules in the profile).
  - Mode 7 → a textured ground plane in real 3D. Games fake perspective by changing the Mode 7 matrix per scanline, so the profile supplies (or a tool fits) the camera per scene; texture detail stays the SNES's 1024×1024 map unless the profile adds a height/detail map.
- **Renderer**: SDL3 GPU API (one API, three backends). Lighting, shadows, depth of field, bloom, tilt-shift, palette-accurate colour.
- **Frontend**: minimal SDL3 app: open ROM, profile on/off toggle (instant A/B), save states, fast-forward. Rewind later.

### 5.1 Profiles

A directory per game, keyed by ROM SHA-256 (several hashes allowed for revisions), JSON validated by a JSON Schema in CI:

```
profiles/<game-id>/
  profile.json      # identity, hashes, scenes, rules
  README.md         # what's tuned, known issues, playtest status
  assets/           # optional: height maps, normal maps, voxel overrides, lighting presets
```

`profile.json` covers: game identity and hashes; **scene selectors** (WRAM addresses/values identifying the current map/scene); per-scene camera, lighting and post-FX; per-layer plane depth/tilt; tile classes by tile-graphics hash; sprite grouping and mode; Mode 7 settings. Anything the builder can't match → stock rendering for that element (never a crash, never garbage).

JSON because browser-chat models produce it reliably, and 3dSen's profiles are JSON too.

A profile **inspector** (pause, click a tile/sprite, see its hash, assign a rule) is a later milestone; until then the frontend can dump FrameState and tile hashes for the current frame.

## 6. The agent team (ponytail style)

Two agents and plain automation. Nothing becomes an agent if a GitHub feature or a script does it.

| Role | Kind | Runs where | Does |
|---|---|---|---|
| **Project Manager (PM)** | agent | core members' machines, any agent tool | Turns `ROADMAP.md` into small task cards; triages playtest reports into cards; writes the weekly core digest |
| **Reviewer** | agent | core members' machines | Reviews each PR **against its card**: scope, ponytail rules, tests, licence/provenance |
| Merge gate | GitHub-native | GitHub | Branch protection, native auto-merge, CODEOWNERS |
| Claim bot | small GitHub Action (no PR code, no secrets) | GitHub | `/claim` assigns a card; stale claims expire |
| Nightly | GitHub Action | GitHub | Builds Linux and Windows, publishes a rolling `nightly` prerelease |
| Notifier | cron script | a core member's machine | Polls GitHub and relays to the core team's private chat |

- **Core members** each run the PM/Reviewer with their own model quota. Work is split by GitHub assignment so two reviewers never duplicate.
- Agent definitions live in the repo (`.agents/pm.md`, `.agents/reviewer.md`) so any core member's tool can load the same role. Read-only roles get no edit tools.
- **A card isn't published (`card:ready`) until the Reviewer has passed it** — a contributor's model knows only what the card says.

### 6.1 Task cards

A GitHub issue from a template:

- **Goal** (one sentence), **type** (`profile` / `code` / `docs` / `playtest` / `art`), size (S/M).
- **Files you may touch** (allowlist) and **must not touch**.
- **Done when**: acceptance commands and expected output; for profile cards, the screenshots/notes to attach.
- **The prompt**: a fenced, self-contained block the contributor pastes as-is. It embeds the rules (no ROM data, no copied code, stay in the allowlist, minimalism, how to hand back the result).

### 6.2 Contributor flow (see `CONTRIBUTE.md`)

1. Pick a `card:ready` issue and comment `/claim`.
2. Copy the card's prompt into your AI tool.
   - **Coding agents** (Codex, Claude Code, Kimi, …): run it in a clone of your fork; it opens the PR.
   - **Browser chats** (ChatGPT, Claude, Gemini, …): it replies with complete files plus a PR title/body; paste them via GitHub's web editor on your fork.
3. The PR body links the card (`Closes #N`) and ticks one box: "I have the right to submit this under the project's licences and it contains no ROM data or copied code." No CLA, no DCO sign-off (the web editor can't add it).

Playtesters: download the nightly, play with your own ROM, and file a Playtest report (game, ROM hash prefix, profile version, what looked wrong, screenshot of emulator output).

### 6.3 Auto-merge rails

A PR merges automatically only when **all** hold:
1. A core reviewer approved **the latest commit** (stale approvals dismissed; the most recent push needs approval). Reviewer agents post as their core member's own account; a separate chores bot account is never a CODEOWNER.
2. CI is green: build, unit tests, fixture image comparisons, profile schema, and the ROM/licence scanner (binary blobs, large files, ROM headers, encoded payloads, files outside the card's allowlist).
3. No **protected path** is touched — everything that defines or executes the rules: `.github/`, `.agents/`, `CODEOWNERS`, licences, `third_party/`, `external/`, `patches/`, `tools/ci/`, `tools/review/`, `tools/admin/`, `tests/ci/`, `tests/review/`, `tests/golden/`, the profile schema, `ROADMAP.md`, `AGENTS.md`, git metadata files. These wait for a human core member. Binary files always wait for one too.
4. Fork PRs never receive secrets. The required `checks` workflow runs on `pull_request_target` from the **base branch** with a read-only token and treats the PR head as data only (fetched and read, never executed) — under plain `pull_request` a fork could run its own copy of the workflow and fake the status. The review runner re-runs the deterministic checks on the exact reviewed commit before approving, and the merge is pinned to that commit.

PR content is untrusted data; the review prompt says so, and approval also requires the deterministic checks, so injected text alone can't merge anything. Every merge is revertable; a red `main` after merge opens a revert PR and alerts the core team.

### 6.4 Notifications

- GitHub: a pinned **Core digest** Discussion — auto-merges, rejections, protected-path holds, red `main`.
- The core team's private chat, via the notifier; humans are pinged only for holds, red `main` and security flags.

## 7. Repo presentation

`README.md`: banner and logo, one-line pitch, side-by-side "stock vs snes2DHD" stills for both showcase games, feature list, "Play" (nightly download + bring your own ROM), "Contribute in 2 minutes", architecture diagram, credits (ares, ponytail; inspiration: 3dSen, Octopath Traveler). Branding is original, AI-assisted art in `assets/branding/`. Comparison media come from real emulator captures with the core team's own ROMs, never generated.

## 8. Technical calls

- C++20 + CMake (matches ares); SDL3 for window, input, audio and GPU.
- JSON + JSON Schema for profiles.
- Two agents only (PM, Reviewer); everything else is GitHub-native or a script.
- Agents run on core members' machines, not in GitHub Actions — no model API keys in CI.
- No CLA, no DCO: a PR-template rights checkbox.
- Core reviewers approve as themselves; one chores bot, never a CODEOWNER.
- "Beats Super ZSNES" is judged by the maintainer on side-by-side stills of named scenes (Chrono Trigger: Millennial Fair, Truce, Guardia Forest; Terranigma: Crysta, first overworld flight).
- Chrono Trigger scene selectors are new reverse-engineering work (M3).

## 9. Milestones

- **M0 — Repo & team**: repo, README and branding, CONTRIBUTE.md, card/playtest templates, agent definitions, branch protection, claim bot, notifier.
- **M1 — It plays**: ares core vendored, SDL3 frontend, plain 2D output, headless test harness (homebrew ROMs), nightlies.
- **M2 — Flat HD-2D**: scene builder and GPU renderer, layers as planes, sprite billboards, lighting, DoF/bloom, Mode 7 ground plane; stock 2D remains the default without a profile.
- **M3 — Showcase profiles v1**: profile format and schema, FrameState dump tool, Terranigma (Crysta + overworld) and Chrono Trigger (opening: Millennial Fair). **First public announcement.**
- **M4 — Voxel mode** and mixing.
- **M5 — Profile inspector** (in-app editor).

## 10. Decisions

- **D1 Licences**: code MPL-2.0; profiles and assets CC BY-SA 4.0. ares stays ISC; ponytail text MIT (notices in `third_party/`).
- **D2 No redrawn HD art in the repo.** Profiles carry geometry, lighting and rules only (plus project-original height/normal maps).
- **D3 Core team at launch: the maintainer only** (sole human CODEOWNER). Protected-path PRs and golden-image changes queue for him; the notifier batches them.
- **D4 Real-game FrameState dumps stay local** — never shared or committed. Tile-graphics hashes and WRAM addresses are allowed in profiles, so profile cards target contributors with their own ROM and a local build; browser-chat contributors get code, docs, tooling and schema cards.
- **D5 Golden images are protected** (renderer-changing PRs wait for a human).
- **D6 Prior Terranigma work by the maintainer** (tile decoder, asset schema, scene selectors — project-authored code only, never ROM data) may be relicensed into snes2DHD under MPL-2.0.
